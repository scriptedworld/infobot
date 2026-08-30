//! Token totals for the running session, by tailing its transcript.
//!
//! The status line's payload carries no cumulative usage, but Claude Code writes
//! a usage record per assistant message into the session transcript, and the
//! payload carries the session id that names it.
//!
//! THIS IS THE ONE PLACE INFOBOT READS A FILE IT WAS NOT HANDED, and it is
//! bounded: a length check on every render, a parse only of what has been
//! appended since the last one.
//!
//! Every read is guarded. A missing transcript, an unreadable state file, a half
//! written line: each yields no totals rather than an error.

const std = @import("std");
const payload = @import("payload.zig");
const Ctx = @import("ctx.zig").Ctx;

/// How far into a transcript to look for the session it belongs to. A transcript
/// opens with bookkeeping that carries no session at all: measured on one opened
/// by a clear, the field first appears on record 18.
const claim_lines = 40;

/// The largest single read this will do. A first pass over a long session's
/// transcript is megabytes; a cap is what stops a pathological file being an
/// unbounded allocation.
const max_read = 64 * 1024 * 1024;

/// The buffer originOf reads claim lines through. A transcript record carrying
/// a pasted file can be long, and a line that does not fit is skipped rather
/// than read in pieces, so this is sized to clear the ones that matter.
const claim_line_buffer = 4 * 1024 * 1024;

const counted_fields = [_][]const u8{
    "input_tokens",
    "output_tokens",
    "cache_read_input_tokens",
    "ephemeral_5m_input_tokens",
    "ephemeral_1h_input_tokens",
};

/// Token counts by field, for one model.
pub const Fields = std.StringHashMapUnmanaged(f64);
/// Token counts keyed by model, then by field.
pub const Totals = std.StringHashMapUnmanaged(Fields);

/// Where the offsets live: generated state, so outside the repository.
pub fn stateDir(ctx: Ctx) ?[]u8 {
    const configured = ctx.getenv("XDG_STATE_HOME");
    if (configured.len != 0) {
        return std.fs.path.join(ctx.gpa, &.{ configured, "infobot" }) catch null;
    }
    const home = ctx.getenv("HOME");
    if (home.len == 0) return null;
    return std.fs.path.join(ctx.gpa, &.{ home, ".local", "state", "infobot" }) catch null;
}

/// The default transcript root.
pub fn projects(ctx: Ctx) ?[]u8 {
    const home = ctx.getenv("HOME");
    if (home.len == 0) return null;
    return std.fs.path.join(ctx.gpa, &.{ home, ".claude", "projects" }) catch null;
}

/// Where this session's offsets live.
pub fn offsetPath(ctx: Ctx, session: []const u8) ?[]u8 {
    const dir = stateDir(ctx) orelse return null;
    const name = std.fmt.allocPrint(ctx.gpa, "{s}.json", .{session}) catch return null;
    return std.fs.path.join(ctx.gpa, &.{ dir, name }) catch null;
}

fn stem(path: []const u8) []const u8 {
    const base = std.fs.path.basename(path);
    const dot = std.mem.lastIndexOfScalar(u8, base, '.') orelse return base;
    return base[0..dot];
}

/// Every transcript the session bills for: its own, and its subagents'.
///
/// Found by name rather than by rebuilding a path. The directory is a slug of
/// the working directory, but a session may have been started somewhere other
/// than where it now is, so the id is looked for instead of the slug being
/// reconstructed from workspace.current_dir.
///
/// THE SUBAGENTS ARE NOT OPTIONAL. Each runs in its own transcript under
/// <session>/subagents/, and on one measured session they were 51% of output
/// tokens and 33% of cache reads.
///
/// NEITHER ARE THE TRANSCRIPTS A CLEAR LEFT BEHIND. /clear opens a new
/// transcript under a NEW sessionId, so a name match alone follows one side of
/// it. The two ids point opposite ways, so transcripts are grouped by ROOT: a
/// transcript's recorded origin where it has one, its own name where it does
/// not. Every transcript sharing a root is one session's spending, whichever id
/// the payload handed over.
pub fn transcripts(ctx: Ctx, session: []const u8, root_in: []const u8) []const []const u8 {
    var found: std.ArrayList([]const u8) = .empty;
    if (session.len == 0) return found.items;

    const root = if (root_in.len != 0) root_in else (projects(ctx) orelse return found.items);
    const wanted = std.fmt.allocPrint(ctx.gpa, "{s}.jsonl", .{session}) catch return found.items;

    var root_dir = ctx.dir().openDir(ctx.io, root, .{ .iterate = true }) catch return found.items;
    defer root_dir.close(ctx.io);

    // The transcript named by the session id, wherever under the root it sits.
    var named: ?[]const u8 = null;
    var projects_it = root_dir.iterate();
    while (projects_it.next(ctx.io) catch null) |entry| {
        if (entry.kind != .directory) continue;
        const candidate = std.fs.path.join(ctx.gpa, &.{ root, entry.name, wanted }) catch continue;
        ctx.dir().access(ctx.io, candidate, .{}) catch continue;
        named = candidate;
        break;
    }

    // Scoped to the project the session belongs to. A clear opens its new
    // transcript beside the old one, so the search never leaves that directory,
    // and the cost is proportional to one project's sessions rather than to
    // every session ever recorded on the machine.
    var pool: std.ArrayList([]const u8) = .empty;
    var origin: []const u8 = session;
    if (named) |path| {
        const dir = std.fs.path.dirname(path).?;
        collectJsonl(ctx, dir, &pool);
        origin = originOf(ctx, path);
    } else {
        var all_it = root_dir.iterate();
        while (all_it.next(ctx.io) catch null) |entry| {
            if (entry.kind != .directory) continue;
            const dir = std.fs.path.join(ctx.gpa, &.{ root, entry.name }) catch continue;
            collectJsonl(ctx, dir, &pool);
        }
    }

    for (pool.items) |path| {
        const belongs = std.mem.eql(u8, originOf(ctx, path), origin) or
            std.mem.eql(u8, stem(path), origin);
        if (belongs) found.append(ctx.gpa, path) catch {};
    }

    // The subagents of everything found, billed to the same session.
    const direct = found.items.len;
    var i: usize = 0;
    while (i < direct) : (i += 1) {
        const path = found.items[i];
        const dir = std.fs.path.dirname(path).?;
        const sub = std.fs.path.join(ctx.gpa, &.{ dir, stem(path), "subagents" }) catch continue;
        collectJsonl(ctx, sub, &found);
    }
    return found.items;
}

fn collectJsonl(ctx: Ctx, dir: []const u8, into: *std.ArrayList([]const u8)) void {
    var handle = ctx.dir().openDir(ctx.io, dir, .{ .iterate = true }) catch return;
    defer handle.close(ctx.io);
    var it = handle.iterate();
    while (it.next(ctx.io) catch null) |entry| {
        if (entry.kind != .file) continue;
        if (!std.mem.endsWith(u8, entry.name, ".jsonl")) continue;
        const path = std.fs.path.join(ctx.gpa, &.{ dir, entry.name }) catch continue;
        into.append(ctx.gpa, path) catch {};
    }
}

/// The session a transcript belongs to: its recorded origin, else its own name.
fn originOf(ctx: Ctx, path: []const u8) []const u8 {
    const file = ctx.dir().openFile(ctx.io, path, .{}) catch return stem(path);
    defer file.close(ctx.io);

    const buf = ctx.gpa.alloc(u8, claim_line_buffer) catch return stem(path);
    var reader = file.reader(ctx.io, buf);
    var lines: usize = 0;
    while (lines < claim_lines) : (lines += 1) {
        // INCLUSIVE, and the newline is trimmed back off. The exclusive form
        // leaves the delimiter in the stream, so every call after the first
        // returns an empty slice and the loop reads one line forever. That is
        // not an error at any point: it looks like a transcript whose records
        // all fail to parse, and it falls through to the filename.
        const raw = reader.interface.takeDelimiterInclusive('\n') catch break;
        const line = std.mem.trimEnd(u8, raw, "\n");
        const parsed = std.json.parseFromSlice(std.json.Value, ctx.gpa, line, .{}) catch continue;
        const id = payload.Map.from(parsed.value).str("session_id");
        if (id.len != 0) return ctx.gpa.dupe(u8, id) catch stem(path);
    }
    return stem(path);
}

/// How far into one transcript the last render read, and what it counted
/// getting there.
const FileState = struct {
    size: u64 = 0,
    totals: Totals = .empty,
};

/// Token counts for the session, keyed by model. Empty when unknowable.
///
/// State is per file rather than one running sum, because subagent transcripts
/// appear part way through a session and a single offset cannot say which of
/// them a total already includes.
pub fn sum(ctx: Ctx, session: []const u8, root: []const u8) Totals {
    var known = load(ctx, session);
    var seen: std.StringHashMapUnmanaged(FileState) = .empty;
    var changed = false;

    for (transcripts(ctx, session, root)) |path| {
        const size = lengthOf(ctx, path) orelse continue;
        const previous: FileState = known.get(path) orelse .{};
        if (previous.size == size) {
            seen.put(ctx.gpa, path, previous) catch {};
            continue;
        }
        // A file that shrank was rotated or replaced, so its offset means
        // nothing against the new one and it is read from the start.
        var start: u64 = 0;
        var counted: Totals = .empty;
        if (size > previous.size) {
            start = previous.size;
            copyInto(ctx, &counted, previous.totals);
        }
        const read = scan(ctx, path, start);
        mergeInto(ctx, &counted, read.totals);
        seen.put(ctx.gpa, path, .{ .size = start + read.consumed, .totals = counted }) catch {};
        changed = true;
    }

    if (changed or seen.count() != known.count()) save(ctx, session, seen);

    var summed: Totals = .empty;
    var it = seen.valueIterator();
    while (it.next()) |entry| mergeInto(ctx, &summed, entry.totals);
    return summed;
}

fn lengthOf(ctx: Ctx, path: []const u8) ?u64 {
    const file = ctx.dir().openFile(ctx.io, path, .{}) catch return null;
    defer file.close(ctx.io);
    return file.length(ctx.io) catch null;
}

fn copyInto(ctx: Ctx, into: *Totals, from: Totals) void {
    var it = from.iterator();
    while (it.next()) |entry| {
        var fields: Fields = .empty;
        var f = entry.value_ptr.iterator();
        while (f.next()) |pair| fields.put(ctx.gpa, pair.key_ptr.*, pair.value_ptr.*) catch {};
        into.put(ctx.gpa, entry.key_ptr.*, fields) catch {};
    }
}

fn mergeInto(ctx: Ctx, into: *Totals, more: Totals) void {
    var it = more.iterator();
    while (it.next()) |entry| {
        const got = into.getOrPut(ctx.gpa, entry.key_ptr.*) catch continue;
        if (!got.found_existing) got.value_ptr.* = .empty;
        var f = entry.value_ptr.iterator();
        while (f.next()) |pair| {
            const slot = got.value_ptr.getOrPut(ctx.gpa, pair.key_ptr.*) catch continue;
            if (!slot.found_existing) slot.value_ptr.* = 0;
            slot.value_ptr.* += pair.value_ptr.*;
        }
    }
}

const Scanned = struct { totals: Totals, consumed: u64 };

/// Usage from start onward, and how many bytes of it were whole lines.
///
/// A transcript being appended to by the session that is rendering can end
/// mid-line, so THE OFFSET ADVANCES ONLY OVER LINES THAT ARRIVED COMPLETE. A
/// trailing fragment is left unconsumed and read again next render, once the
/// rest of it has landed.
fn scan(ctx: Ctx, path: []const u8, start: u64) Scanned {
    var found: Totals = .empty;
    const nothing: Scanned = .{ .totals = found, .consumed = 0 };

    const file = ctx.dir().openFile(ctx.io, path, .{}) catch return nothing;
    defer file.close(ctx.io);
    const size = file.length(ctx.io) catch return nothing;
    if (size <= start) return nothing;

    const wanted = size - start;
    if (wanted > max_read) return nothing;

    const raw = ctx.gpa.alloc(u8, @intCast(wanted)) catch return nothing;
    const got = file.readPositionalAll(ctx.io, raw, start) catch return nothing;
    const text = raw[0..got];

    var consumed: u64 = 0;
    var rest = text;
    while (std.mem.indexOfScalar(u8, rest, '\n')) |end| {
        take(ctx, &found, rest[0..end]);
        consumed += end + 1;
        rest = rest[end + 1 ..];
    }
    return .{ .totals = found, .consumed = consumed };
}

/// Adds one transcript line's usage, ignoring anything that is not usage.
fn take(ctx: Ctx, found: *Totals, raw: []const u8) void {
    if (raw.len == 0) return;
    const parsed = std.json.parseFromSlice(std.json.Value, ctx.gpa, raw, .{}) catch return;
    const message = payload.Map.from(parsed.value).obj("message");
    const usage_map = message.obj("usage");
    if (usage_map.isEmpty()) return;

    var model = message.str("model");
    if (model.len == 0) {
        // Counted under a placeholder no rate table answers to, so it flags the
        // total incomplete rather than being priced at whatever model happened
        // to be next to it.
        model = "?";
    }
    const slot = found.getOrPut(ctx.gpa, model) catch return;
    if (!slot.found_existing) slot.value_ptr.* = .empty;

    const creation = usage_map.obj("cache_creation");
    for (counted_fields) |field| {
        // Only numeric values are added. A field carrying anything else is
        // ignored rather than coerced, and it does not abandon the record.
        const value = usage_map.num(field) orelse creation.num(field) orelse continue;
        const cell = slot.value_ptr.getOrPut(ctx.gpa, field) catch continue;
        if (!cell.found_existing) cell.value_ptr.* = 0;
        cell.value_ptr.* += value;
    }
}

fn load(ctx: Ctx, session: []const u8) std.StringHashMapUnmanaged(FileState) {
    var files: std.StringHashMapUnmanaged(FileState) = .empty;
    const path = offsetPath(ctx, session) orelse return files;
    const file = ctx.dir().openFile(ctx.io, path, .{}) catch return files;
    defer file.close(ctx.io);

    const size = file.length(ctx.io) catch return files;
    if (size > max_read) return files;
    const raw = ctx.gpa.alloc(u8, @intCast(size)) catch return files;
    const got = file.readPositionalAll(ctx.io, raw, 0) catch return files;

    const parsed = std.json.parseFromSlice(std.json.Value, ctx.gpa, raw[0..got], .{}) catch return files;
    const recorded = payload.Map.from(parsed.value).obj("files");
    const object = switch (recorded.value orelse return files) {
        .object => |o| o,
        else => return files,
    };

    var it = object.iterator();
    while (it.next()) |entry| {
        const one = payload.Map.from(entry.value_ptr.*);
        var totals: Totals = .empty;
        const models = one.obj("totals");
        if (models.value) |mv| {
            if (mv == .object) {
                var m = mv.object.iterator();
                while (m.next()) |model| {
                    var fields: Fields = .empty;
                    if (payload.Map.from(model.value_ptr.*).value) |fv| {
                        if (fv == .object) {
                            var f = fv.object.iterator();
                            while (f.next()) |pair| {
                                const n = payload.asNumber(pair.value_ptr.*) orelse continue;
                                fields.put(ctx.gpa, pair.key_ptr.*, n) catch {};
                            }
                        }
                    }
                    totals.put(ctx.gpa, model.key_ptr.*, fields) catch {};
                }
            }
        }
        files.put(ctx.gpa, entry.key_ptr.*, .{
            .size = @intFromFloat(one.count("size")),
            .totals = totals,
        }) catch {};
    }
    return files;
}

/// Best effort. A state file that cannot be written costs a re-sum, and a
/// re-sum is slow rather than wrong, so the failure is not worth reporting.
fn save(ctx: Ctx, session: []const u8, seen: std.StringHashMapUnmanaged(FileState)) void {
    const path = offsetPath(ctx, session) orelse return;

    var out: std.ArrayList(u8) = .empty;
    out.appendSlice(ctx.gpa, "{\"files\":{") catch return;
    var first = true;
    var it = seen.iterator();
    while (it.next()) |entry| {
        if (!first) out.append(ctx.gpa, ',') catch return;
        first = false;
        quote(ctx, &out, entry.key_ptr.*);
        out.print(ctx.gpa, ":{{\"size\":{d},\"totals\":{{", .{entry.value_ptr.size}) catch return;
        var first_model = true;
        var m = entry.value_ptr.totals.iterator();
        while (m.next()) |model| {
            if (!first_model) out.append(ctx.gpa, ',') catch return;
            first_model = false;
            quote(ctx, &out, model.key_ptr.*);
            out.appendSlice(ctx.gpa, ":{") catch return;
            var first_field = true;
            var f = model.value_ptr.iterator();
            while (f.next()) |pair| {
                if (!first_field) out.append(ctx.gpa, ',') catch return;
                first_field = false;
                quote(ctx, &out, pair.key_ptr.*);
                out.print(ctx.gpa, ":{d}", .{pair.value_ptr.*}) catch return;
            }
            out.appendSlice(ctx.gpa, "}") catch return;
        }
        out.appendSlice(ctx.gpa, "}}") catch return;
    }
    out.appendSlice(ctx.gpa, "}}") catch return;

    // OWNER ONLY. The offsets are this user's own byte positions into their own
    // transcripts, read by nothing but this program, so the group and world
    // bits were breadth nobody asked for.
    var atomic = ctx.dir().createFileAtomic(ctx.io, path, .{
        .make_path = true,
        .replace = true,
        .permissions = .default_file,
    }) catch return;
    defer atomic.deinit(ctx.io);
    atomic.file.writeStreamingAll(ctx.io, out.items) catch return;
    atomic.replace(ctx.io) catch {};
}

/// A JSON string. The keys here are file paths and model names, so the escape
/// set that matters is the quote and the backslash.
fn quote(ctx: Ctx, out: *std.ArrayList(u8), text: []const u8) void {
    out.append(ctx.gpa, '"') catch return;
    for (text) |c| {
        switch (c) {
            '"' => out.appendSlice(ctx.gpa, "\\\"") catch return,
            '\\' => out.appendSlice(ctx.gpa, "\\\\") catch return,
            0x00...0x1f => out.print(ctx.gpa, "\\u{x:0>4}", .{c}) catch return,
            else => out.append(ctx.gpa, c) catch return,
        }
    }
    out.append(ctx.gpa, '"') catch return;
}

/// Drops this session's offsets.
///
/// Called when the session ends. The offsets record how far into each transcript
/// the last render read, which is worth nothing once nothing will render again,
/// and they accumulate one file per session forever otherwise.
pub fn forget(ctx: Ctx, session: []const u8) void {
    const path = offsetPath(ctx, session) orelse return;
    ctx.dir().deleteFile(ctx.io, path) catch {};
}

test "stem drops the extension and the directory" {
    try std.testing.expectEqualStrings("abc", stem("/a/b/abc.jsonl"));
    try std.testing.expectEqualStrings("abc", stem("abc"));
    try std.testing.expectEqualStrings("a.b", stem("/x/a.b.jsonl"));
}
