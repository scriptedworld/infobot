//! Says what the session's context window is doing, for the bar and for a file.
//!
//! The bar shows it to a person and the file leaves it where a program can read
//! it. Both come through `figures` so the two cannot disagree: a bar saying 56%
//! beside a file saying something else is worse than either alone.
//!
//! Written in canonical YAML, hand-emitted: block style, one key to a line, keys
//! sorted, a string quoted and a number bare. THE FORM IS A PUBLISHED INTERFACE
//! (FR-1.11o): silo's coordination board reads these files with patterns
//! anchored on the quoted key and the single space after the colon, and takes
//! the number bare. Changing the shape breaks it, and adding a key does not.
//!
//! This implementation must emit the SAME BYTES as the Go one. FR-1.11p pins
//! the form with a fixture at each end, and a second emitter that disagrees on
//! one escape spelling is a second form, whatever either of them calls it.

const std = @import("std");
const num = @import("num.zig");
const payload = @import("payload.zig");
const usage = @import("usage.zig");
const Ctx = @import("ctx.zig").Ctx;

/// The context window as used, size and percent.
pub const Window = struct {
    used: f64,
    size: f64,
    percent: f64,
};

/// The input side only, never output: what used_percentage is computed from.
const input_keys = [_][]const u8{
    "input_tokens",
    "cache_creation_input_tokens",
    "cache_read_input_tokens",
};

/// The context window, or null when it cannot be said at all.
///
/// current_usage is the authority when present and total_input_tokens is the
/// fallback, matching used_percentage's own formula. Where the counts are absent
/// and a percentage is present, the count is derived from it, so the two halves
/// agree rather than reporting a literal zero that reads as a bug.
pub fn figures(cw: payload.Map) ?Window {
    const size = cw.num("context_window_size") orelse return null;
    if (size == 0) return null;

    var used: f64 = 0;
    const current = cw.obj("current_usage");
    if (!current.isEmpty()) {
        for (input_keys) |key| used += current.count(key);
    } else {
        used = cw.count("total_input_tokens");
    }

    var percent: f64 = 0;
    if (cw.num("used_percentage")) |given| {
        percent = given;
        if (used == 0) used = size * percent / 100;
    } else {
        percent = used / size * 100;
    }
    return .{ .used = used, .size = size, .percent = percent };
}

/// Where this session's state is left. One path, named by session id.
pub fn path(ctx: Ctx, session: []const u8) ?[]u8 {
    const dir = usage.stateDir(ctx) orelse return null;
    const name = std.fmt.allocPrint(ctx.gpa, "{s}.status.yaml", .{session}) catch return null;
    return std.fs.path.join(ctx.gpa, &.{ dir, name }) catch null;
}

/// One emitted scalar, keeping its own type so the round trip does.
const Value = struct {
    text: []const u8 = "",
    quoted: bool = false,
    rendered: []const u8 = "",
};

fn str(text: []const u8) Value {
    return .{ .text = text, .quoted = true };
}

fn raw(text: []const u8) Value {
    return .{ .rendered = text };
}

const Field = struct { key: []const u8, value: Value };

/// Leaves the last check on disk.
///
/// Best effort in the same sense as the usage offsets: a state file that cannot
/// be written costs a reader a measurement it can take another way, and the
/// status line's one hard guarantee is that it renders or shows nothing.
pub fn write(ctx: Ctx, data: payload.Map, now_seconds: i64) void {
    const session = data.str("session_id");
    if (session.len == 0) return;
    const target = path(ctx, session) orelse return;

    const stamp = timestamp(ctx, now_seconds) catch return;
    const body = canonical(ctx.gpa, data, session, stamp) catch return;

    // WRITTEN WHOLE OR NOT AT ALL. A reader stat-ing this file between a
    // truncate and a write would otherwise see an empty one and conclude the
    // session had no context, which is worse than seeing the previous check.
    //
    // The platform's own atomic create rather than a hand-rolled temporary and
    // rename: same guarantee, and it cleans up after itself on the failure path
    // where the hand-rolled version needs to remember to.
    var atomic = ctx.dir().createFileAtomic(ctx.io, target, .{
        .make_path = true,
        .replace = true,
        .permissions = .default_file,
    }) catch return;
    defer atomic.deinit(ctx.io);

    atomic.file.writeStreamingAll(ctx.io, body) catch return;
    atomic.replace(ctx.io) catch {};
}

/// The nine published keys, sorted, one to a line.
///
/// A key whose value is empty is omitted rather than written blank, so a reader
/// tells "not said" from "said to be nothing".
///
/// `written` is passed in rather than read from a clock, so this is a pure
/// function of the payload and a test can assert bytes against a fixed instant.
pub fn canonical(
    gpa: std.mem.Allocator,
    data: payload.Map,
    session: []const u8,
    written: []const u8,
) ![]u8 {
    var fields: std.ArrayList(Field) = .empty;
    defer fields.deinit(gpa);

    try fields.append(gpa, .{ .key = "session", .value = str(session) });
    try fields.append(gpa, .{ .key = "written", .value = str(written) });
    try describedFields(gpa, data, &fields);

    var scratch: std.ArrayList([]u8) = .empty;
    defer {
        for (scratch.items) |item| gpa.free(item);
        scratch.deinit(gpa);
    }
    try contextFields(gpa, data, &fields, &scratch);

    std.mem.sort(Field, fields.items, {}, struct {
        fn lt(_: void, a: Field, b: Field) bool {
            return std.mem.order(u8, a.key, b.key) == .lt;
        }
    }.lt);

    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    for (fields.items) |field| {
        // The key is quoted the same way a string value is, rather than with a
        // language's own quoting, whose escape syntax is a wider language than
        // the two escapes this form defines.
        try scalar(gpa, &out, str(field.key));
        try out.appendSlice(gpa, ": ");
        try scalar(gpa, &out, field.value);
        try out.append(gpa, '\n');
    }
    return out.toOwnedSlice(gpa);
}

/// The three keys the payload describes rather than measures, each omitted when
/// the payload does not carry it.
fn describedFields(gpa: std.mem.Allocator, data: payload.Map, fields: *std.ArrayList(Field)) !void {
    // The model is display_name where the payload has one and id where it does
    // not, which is FR-2.8.
    const model = data.obj("model");
    const chosen = if (model.str("display_name").len != 0)
        model.str("display_name")
    else
        model.str("id");

    for ([_]Field{
        .{ .key = "cwd", .value = str(data.obj("workspace").str("current_dir")) },
        .{ .key = "model", .value = str(chosen) },
        .{ .key = "effort", .value = str(data.obj("effort").str("level")) },
    }) |field| {
        if (field.value.text.len != 0) try fields.append(gpa, field);
    }
}

/// The four context keys, which are present together or not at all.
///
/// `scratch` owns the rendered numbers, because a `Field` holds a slice rather
/// than a copy and the caller outlives this frame.
fn contextFields(
    gpa: std.mem.Allocator,
    data: payload.Map,
    fields: *std.ArrayList(Field),
    scratch: *std.ArrayList([]u8),
) !void {
    const window = figures(data.obj("context_window")) orelse return;
    const used: i64 = @intFromFloat(window.used);
    const size: i64 = @intFromFloat(window.size);
    // Derived rather than read, and never negative: a payload reporting more
    // used than the window holds gives zero.
    const remaining = @max(0, size - used);

    const numbers = [_]struct { key: []const u8, text: []u8 }{
        .{ .key = "context_used", .text = try std.fmt.allocPrint(gpa, "{d}", .{used}) },
        .{ .key = "context_size", .text = try std.fmt.allocPrint(gpa, "{d}", .{size}) },
        .{ .key = "context_remaining", .text = try std.fmt.allocPrint(gpa, "{d}", .{remaining}) },
        // Neither floored nor capped, so a reader sees an over-full window as
        // over-full where the bar can only draw it as full.
        .{ .key = "context_percent", .text = try decimal(gpa, num.roundTo(window.percent, 1)) },
    };
    for (numbers) |entry| {
        try scratch.append(gpa, entry.text);
        try fields.append(gpa, .{ .key = entry.key, .value = raw(entry.text) });
    }
}

/// A float written so its type is never in question on the way back in: it
/// always carries a decimal point, which a YAML reader needs to give back a
/// float rather than an integer.
///
/// NEVER AN EXPONENT. `1e+06` is a legal spelling of a million and a reader
/// matching `[0-9.]+` against it captures `1`, which is a plausible small
/// number rather than a parse failure. silo's coordination board matches exactly
/// that, so an exponent here is a silent wrong answer on a board a person uses
/// to decide which session to clear. This is FR-1.11q.
fn decimal(gpa: std.mem.Allocator, f: f64) ![]u8 {
    // A plain `{d}` reaches for an exponent at magnitude the way Go's 'g' does,
    // so the decimal form is asked for explicitly and the trailing zeros are
    // trimmed back, which is what Go's 'f' at -1 precision produces.
    var buf: [512]u8 = undefined;
    const written = try std.fmt.bufPrint(&buf, "{d:.10}", .{f});
    if (std.mem.indexOfScalar(u8, written, '.') == null) {
        return std.fmt.allocPrint(gpa, "{s}.0", .{written});
    }
    const text = std.mem.trimEnd(u8, written, "0");
    if (text[text.len - 1] == '.') {
        return std.fmt.allocPrint(gpa, "{s}0", .{text});
    }
    return gpa.dupe(u8, text);
}

fn scalar(gpa: std.mem.Allocator, out: *std.ArrayList(u8), v: Value) !void {
    if (!v.quoted) {
        try out.appendSlice(gpa, v.rendered);
        return;
    }
    try out.append(gpa, '"');
    var it = std.unicode.Utf8Iterator{ .bytes = v.text, .i = 0 };
    while (it.nextCodepoint()) |cp| try escape(gpa, out, cp);
    try out.append(gpa, '"');
}

/// Spells one code point for a double-quoted scalar, which is FR-1.11r.
///
/// THE FORM WAS NEVER THE LIMIT. A double-quoted YAML scalar carries the whole
/// C-style escape set and stays on one line, so it is a JSON string with more in
/// it. Escaping two characters out of the set was under-implementation rather
/// than a format that could not say the value.
///
/// The ranges are the ones a strict reader treats specially. C0 and DEL and C1
/// are rejected outright bar three, and those three are the dangerous ones:
/// `\n`, `\r` and U+0085 are accepted raw and each comes back as a SPACE. So the
/// characters a parser lets through are exactly the ones it corrupts, which is
/// why this belongs in the emitter rather than being left to a stricter reader.
///
/// THE LINE-BREAK SET IS THE SPEC'S, NOT THE OBSERVED ONE. YAML 1.1 makes five
/// characters line breaks, LF CR NEL LS PS; 1.2 cuts the set to LF and CR for
/// JSON compatibility and calls the other three non-breaks. U+2028 and U+2029
/// are escaped for that reason rather than an observed one: a 1.1 parser folds
/// them, and this file must not depend on which version its reader implements.
fn escape(gpa: std.mem.Allocator, out: *std.ArrayList(u8), cp: u21) !void {
    switch (cp) {
        '\\' => return out.appendSlice(gpa, "\\\\"),
        '"' => return out.appendSlice(gpa, "\\\""),
        '\t' => return out.appendSlice(gpa, "\\t"),
        '\n' => return out.appendSlice(gpa, "\\n"),
        '\r' => return out.appendSlice(gpa, "\\r"),
        // `\u` because the code point does not fit `\x`.
        0x2028, 0x2029 => return out.print(gpa, "\\u{x}", .{cp}),
        else => {},
    }
    if (cp < 0x20 or cp == 0x7f or (cp >= 0x80 and cp <= 0x9f)) {
        // Exactly two digits, which is what \x takes. One digit for anything
        // under 0x10 would read as a truncated escape.
        return out.print(gpa, "\\x{x:0>2}", .{cp});
    }
    var buf: [4]u8 = undefined;
    const len = std.unicode.utf8Encode(cp, &buf) catch return;
    try out.appendSlice(gpa, buf[0..len]);
}

/// ISO 8601 carrying an offset, to the second, which is FR-1.11g.
pub fn timestamp(ctx: Ctx, epoch_seconds: i64) ![]u8 {
    const offset = localOffsetSeconds(ctx, epoch_seconds);
    return format8601(ctx.gpa, epoch_seconds, offset);
}

fn format8601(gpa: std.mem.Allocator, epoch_seconds: i64, offset: i64) ![]u8 {
    const local = epoch_seconds + offset;
    const day: std.time.epoch.EpochDay = .{ .day = @intCast(@divFloor(local, 86400)) };
    const year_day = day.calculateYearDay();
    const month_day = year_day.calculateMonthDay();
    const rest: u64 = @intCast(@mod(local, 86400));

    const sign: u8 = if (offset < 0) '-' else '+';
    const magnitude: u64 = @intCast(if (offset < 0) -offset else offset);

    return std.fmt.allocPrint(gpa, "{d:0>4}-{d:0>2}-{d:0>2}T{d:0>2}:{d:0>2}:{d:0>2}{c}{d:0>2}:{d:0>2}", .{
        year_day.year,
        month_day.month.numeric(),
        @as(u32, month_day.day_index) + 1,
        rest / 3600,
        (rest % 3600) / 60,
        rest % 60,
        sign,
        magnitude / 3600,
        (magnitude % 3600) / 60,
    });
}

/// The local UTC offset in seconds, at that instant.
///
/// AT THAT INSTANT is the part that matters. A fixed offset would be right for
/// half the year and an hour out for the other half, and `written` is a
/// timestamp somebody may compare against another clock.
///
/// Read from the TZ database rather than from libc, so the binary links nothing
/// and behaves the same however it was built. A zone that cannot be read gives
/// UTC, which is wrong by a known amount rather than wrong by a guess.
fn localOffsetSeconds(ctx: Ctx, epoch_seconds: i64) i64 {
    const file = ctx.dir().openFile(ctx.io, "/etc/localtime", .{}) catch return 0;
    defer file.close(ctx.io);

    var read_buf: [16 * 1024]u8 = undefined;
    var reader = file.reader(ctx.io, &read_buf);
    const zone = std.tz.Tz.parse(ctx.gpa, &reader.interface) catch return 0;

    // The last transition at or before the instant names the offset in force.
    var offset: i64 = if (zone.timetypes.len > 0) zone.timetypes[0].offset else 0;
    for (zone.transitions) |transition| {
        if (transition.ts > epoch_seconds) break;
        offset = transition.timetype.offset;
    }
    return offset;
}

test "the canonical form is the published one" {
    const gpa = std.testing.allocator;
    const text =
        \\{"session_id":"abcd-1234",
        \\ "workspace":{"current_dir":"/tmp/x"},
        \\ "model":{"display_name":"Opus 5"},
        \\ "effort":{"level":"high"},
        \\ "context_window":{"context_window_size":200000,"used_percentage":48.0}}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, gpa, text, .{});
    defer parsed.deinit();

    const got = try canonical(gpa, payload.Map.from(parsed.value), "abcd-1234", "2026-08-30T01:39:47-07:00");
    defer gpa.free(got);

    // Keys quoted and sorted, one to a line, a single space after the colon,
    // numbers bare. Everything silo's board anchors on.
    try std.testing.expect(std.mem.indexOf(u8, got, "\"context_percent\": 48.0\n") != null);
    try std.testing.expect(std.mem.indexOf(u8, got, "\"context_size\": 200000\n") != null);
    try std.testing.expect(std.mem.indexOf(u8, got, "\"context_used\": 96000\n") != null);
    try std.testing.expect(std.mem.indexOf(u8, got, "\"context_remaining\": 104000\n") != null);
    try std.testing.expect(std.mem.indexOf(u8, got, "\"cwd\": \"/tmp/x\"\n") != null);
    try std.testing.expect(std.mem.indexOf(u8, got, "\"written\": \"2026-08-30T01:39:47-07:00\"\n") != null);

    // Sorted: context_percent before context_remaining before cwd.
    const a = std.mem.indexOf(u8, got, "context_percent").?;
    const b = std.mem.indexOf(u8, got, "context_remaining").?;
    const c = std.mem.indexOf(u8, got, "\"cwd\"").?;
    try std.testing.expect(a < b and b < c);
}

test "a key whose value is empty is omitted rather than written blank" {
    const gpa = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, gpa, "{\"session_id\":\"s\"}", .{});
    defer parsed.deinit();
    const got = try canonical(gpa, payload.Map.from(parsed.value), "s", "t");
    defer gpa.free(got);
    try std.testing.expectEqualStrings("\"session\": \"s\"\n\"written\": \"t\"\n", got);
}

test "a whole float still carries its point, and never an exponent" {
    const gpa = std.testing.allocator;
    for ([_]struct { in: f64, want: []const u8 }{
        .{ .in = 0.0, .want = "0.0" },
        .{ .in = 48.0, .want = "48.0" },
        .{ .in = 23.1, .want = "23.1" },
        .{ .in = 1000000.0, .want = "1000000.0" },
        .{ .in = 0.1, .want = "0.1" },
    }) |c| {
        const got = try decimal(gpa, c.in);
        defer gpa.free(got);
        try std.testing.expectEqualStrings(c.want, got);
    }
}

test "the line-break set is escaped" {
    const gpa = std.testing.allocator;
    var out: std.ArrayList(u8) = .empty;
    defer out.deinit(gpa);
    try scalar(gpa, &out, str("a\nb\rc\td\"e\\f"));
    try std.testing.expectEqualStrings("\"a\\nb\\rc\\td\\\"e\\\\f\"", out.items);

    out.clearRetainingCapacity();
    try scalar(gpa, &out, str("/a\u{2028}b"));
    try std.testing.expectEqualStrings("\"/a\\u2028b\"", out.items);

    out.clearRetainingCapacity();
    try scalar(gpa, &out, str("/a\u{0085}b"));
    try std.testing.expectEqualStrings("\"/a\\x85b\"", out.items);

    out.clearRetainingCapacity();
    try scalar(gpa, &out, str("/a\u{0001}b"));
    try std.testing.expectEqualStrings("\"/a\\x01b\"", out.items);
}

test "the timestamp carries its offset, and the offset moves the clock" {
    const gpa = std.testing.allocator;
    // 1756543187 is 2025-08-30T08:39:47Z, checked with `date -u -d @1756543187`.
    const west = try format8601(gpa, 1756543187, -7 * 3600);
    defer gpa.free(west);
    try std.testing.expectEqualStrings("2025-08-30T01:39:47-07:00", west);

    const utc = try format8601(gpa, 1756543187, 0);
    defer gpa.free(utc);
    try std.testing.expectEqualStrings("2025-08-30T08:39:47+00:00", utc);

    // East of Greenwich, and across a half-hour offset, which is where a
    // minutes field that only ever prints 00 would go unnoticed.
    const east = try format8601(gpa, 1756543187, 5 * 3600 + 1800);
    defer gpa.free(east);
    try std.testing.expectEqualStrings("2025-08-30T14:09:47+05:30", east);
}
