//! What a rendered string costs in columns, and how wide the pane is.

const std = @import("std");
const eaw = @import("eaw.zig");
const payload = @import("payload.zig");

/// Bounds the wait on a host. A hung multiplexer must not hang a line that
/// renders on every Claude Code event.
const ask_timeout_ms = 2000;

/// The columns a rendered string occupies, escapes and all.
///
/// Sizing the bar by subtraction only works if the subtrahend is what the
/// terminal will actually draw. A byte count is not that: it counts every byte
/// of an escape sequence as a column and the emoji as one column when they take
/// two. Both errors run toward a bar too wide for the line.
///
/// Ambiguous-width characters, the parallelograms among them, are counted as
/// one, which is what kitty draws them as. A terminal configured to treat
/// ambiguous as wide would need `eaw.zig` regenerated, and would double the bar.
pub fn visible(text: []const u8) usize {
    var total: usize = 0;
    var i: usize = 0;
    while (i < text.len) {
        if (text[i] == 0x1b) {
            const run = sgrLength(text[i..]);
            if (run != 0) {
                i += run;
                continue;
            }
            // NOT a complete SGR sequence, so it is not stripped. The Go
            // implementation strips with a regex that matches whole sequences
            // only, leaving anything else to count its own width, and an escape
            // byte counts one there. Matching that matters more than being
            // right about a string this program never emits.
            i += 1;
            total += 1;
            continue;
        }
        const len = std.unicode.utf8ByteSequenceLength(text[i]) catch {
            // Not the start of a sequence: count the byte and carry on rather
            // than abandoning the row.
            i += 1;
            total += 1;
            continue;
        };
        if (i + len > text.len) {
            i += 1;
            total += 1;
            continue;
        }
        const cp = std.unicode.utf8Decode(text[i..][0..len]) catch {
            i += 1;
            total += 1;
            continue;
        };
        total += eaw.codepointWidth(cp);
        i += len;
    }
    return total;
}

/// The length of a complete CSI SGR sequence at the head of text, or 0 when
/// there is not one.
///
/// This is the Go implementation's regex, `\033\[[0-9;]*m`, written out: only a
/// whole `ESC [ digits-and-semicolons m` counts, which is the set this program
/// emits.
fn sgrLength(text: []const u8) usize {
    if (text.len < 2 or text[1] != '[') return 0;
    var i: usize = 2;
    while (i < text.len) : (i += 1) {
        switch (text[i]) {
            '0'...'9', ';' => {},
            'm' => return i + 1,
            else => return 0,
        }
    }
    return 0;
}

/// The pane columns, or 0 when it genuinely cannot be known.
///
/// Every ordinary route fails here. Claude Code captures stdout, so the standard
/// descriptors all fail; COLUMNS is unset; /dev/tty is "No such device or
/// address"; and any library terminal-size call therefore returns its fabricated
/// 80x24 fallback.
///
/// THAT FALLBACK IS THE TRAP. Believing it would truncate a 223-column pane to
/// 80, worse than not adapting at all. So it is never used: whatever owns the
/// pane is asked directly, and a host that cannot be asked returns 0, meaning
/// "render the full form and let the caller fit it".
///
/// The hosts are tried INNERMOST FIRST. tmux running inside a herdr pane draws
/// this line in the tmux pane, which is the narrower of the two, so tmux answers
/// whenever it is there and herdr answers when it is not.
pub fn terminal(gpa: std.mem.Allocator, io: std.Io, env: *const std.process.Environ.Map) usize {
    if (tmuxWidth(gpa, io, env)) |w| {
        if (w != 0) return w;
    }
    return herdrWidth(gpa, io, env) orelse 0;
}

fn tmuxWidth(gpa: std.mem.Allocator, io: std.Io, env: *const std.process.Environ.Map) ?usize {
    const set = env.get("TMUX") orelse return null;
    if (set.len == 0) return null;
    const answer = ask(gpa, io, env, &.{ "tmux", "display-message", "-p", "#{pane_width}" }) orelse return null;
    defer gpa.free(answer);
    return digits(std.mem.trim(u8, answer, " \t\r\n"));
}

/// The pane columns from herdr.
///
/// It answers for the CALLING PANE'S WHOLE TAB, so the pane has to be picked
/// out of the list it returns, and HERDR_PANE_ID is what names it.
///
/// A tab holding exactly one pane answers whatever id that pane carries. It is
/// the one case where not matching the id costs nothing, because there is only
/// one rectangle it could be, and it covers an id in the environment that no
/// longer names the pane the process now sits in. Every other mismatch is
/// reported unknown rather than guessed at.
///
/// A ZOOMED PANE'S RECTANGLE IS THE UNZOOMED ONE. `zoomed` goes true and every
/// rect in the reply stays exactly where it was, so a pane zoomed out of a
/// two-way split reports half the columns it is drawn in. The tab's area is the
/// width to use, and the zoomed pane is the focused one.
///
/// HERDR_BIN_PATH is preferred over the name because it pins the version that
/// owns this pane, and because PATH in a status line subprocess is whatever
/// Claude Code inherited rather than whatever a shell would have built.
fn herdrWidth(gpa: std.mem.Allocator, io: std.Io, env: *const std.process.Environ.Map) ?usize {
    const pane = env.get("HERDR_PANE_ID") orelse return null;
    if (pane.len == 0) return null;
    var binary = env.get("HERDR_BIN_PATH") orelse "";
    if (binary.len == 0) binary = "herdr";

    const answer = ask(gpa, io, env, &.{ binary, "pane", "layout", "--current" }) orelse return null;
    defer gpa.free(answer);
    if (answer.len == 0) return null;

    const parsed = std.json.parseFromSlice(std.json.Value, gpa, answer, .{}) catch return null;
    defer parsed.deinit();
    return columnsFromLayout(payload.Map.from(parsed.value).obj("result").obj("layout"), pane);
}

/// The columns for one pane, out of a tab's layout.
fn columnsFromLayout(layout: payload.Map, pane: []const u8) ?usize {
    const value = layout.value orelse return null;
    const panes = switch (value.object.get("panes") orelse return null) {
        .array => |a| a.items,
        else => return null,
    };
    const chosen = panes[pickPane(panes, pane) orelse return null];
    const m = payload.Map.from(chosen);

    if (isZoomed(value) and std.mem.eql(u8, m.str("pane_id"), layout.str("focused_pane_id"))) {
        return @intFromFloat(layout.obj("area").count("width"));
    }
    return @intFromFloat(m.obj("rect").count("width"));
}

/// Which entry is this pane.
///
/// A tab holding exactly one pane answers whatever id that pane carries. It is
/// the one case where not matching the id costs nothing, because there is only
/// one rectangle it could be, and it covers an id in the environment that no
/// longer names the pane the process now sits in. Every other mismatch is
/// reported unknown rather than guessed at.
fn pickPane(panes: []const std.json.Value, pane: []const u8) ?usize {
    for (panes, 0..) |p, i| {
        if (std.mem.eql(u8, payload.Map.from(p).str("pane_id"), pane)) return i;
    }
    return if (panes.len == 1) 0 else null;
}

fn isZoomed(layout: std.json.Value) bool {
    return switch (layout.object.get("zoomed") orelse return false) {
        .bool => |b| b,
        else => false,
    };
}

/// Puts a question to a host and hands back its stdout. Null means the host
/// could not be asked, which is a rendering decision rather than an error.
///
/// THE TIMEOUT IS ON THE READ, NOT ON THE PROCESS, and that is what makes it a
/// bound. The Go implementation learned this the hard way: killing the child is
/// not enough, because a host is a script and killing the shell leaves any
/// grandchild it spawned holding the inherited stdout pipe, so a two second
/// timeout waited thirty against a host running `sleep 30`.
///
/// Polling the pipe with a deadline sidesteps that entirely. When the deadline
/// passes this stops reading and returns; whether the grandchild ever closes
/// the pipe stops being this program's problem.
fn ask(
    gpa: std.mem.Allocator,
    io: std.Io,
    env: *const std.process.Environ.Map,
    argv: []const []const u8,
) ?[]u8 {
    var child = std.process.spawn(io, .{
        .argv = argv,
        .environ_map = env,
        .stdin = .ignore,
        .stdout = .pipe,
        .stderr = .ignore,
    }) catch return null;

    const out = child.stdout orelse {
        child.kill(io);
        return null;
    };

    var collected: std.ArrayList(u8) = .empty;
    defer collected.deinit(gpa);

    const deadline = nowMillis(io) + ask_timeout_ms;
    var buf: [4096]u8 = undefined;
    while (true) {
        const left = deadline - nowMillis(io);
        if (left <= 0) break;

        var fds = [_]std.posix.pollfd{.{
            .fd = out.handle,
            .events = std.posix.POLL.IN,
            .revents = 0,
        }};
        const ready = std.posix.poll(&fds, @intCast(left)) catch break;
        if (ready == 0) break; // the deadline, not an answer

        const n = std.posix.read(out.handle, &buf) catch break;
        if (n == 0) break; // clean end of output
        collected.appendSlice(gpa, buf[0..n]) catch break;
    }

    // Kill unconditionally, and NEVER wait afterwards. `kill` is idempotent, it
    // blocks only until the child itself terminates, and it reaps: a `wait`
    // after it asserts on an id that is already gone.
    //
    // Killing the child is also all that is wanted. A host is a script, so any
    // grandchild it spawned survives, and this deliberately does not wait for
    // one: the Go implementation's two second timeout waited thirty against a
    // host running `sleep 30` for exactly that reason.
    child.kill(io);

    return collected.toOwnedSlice(gpa) catch null;
}

/// The monotonic clock in milliseconds, for the deadline. Monotonic rather than
/// the wall clock, so a clock step during the wait cannot extend or collapse
/// the bound.
fn nowMillis(io: std.Io) i64 {
    return @intCast(@divFloor(std.Io.Timestamp.now(io, .awake).nanoseconds, std.time.ns_per_ms));
}

/// A run of digits as a number, or null for anything else. Deliberately strict:
/// a host answering with a word should read as no answer rather than as zero.
fn digits(text: []const u8) ?usize {
    if (text.len == 0) return null;
    var total: usize = 0;
    for (text) |c| {
        if (c < '0' or c > '9') return null;
        total = total * 10 + (c - '0');
    }
    return total;
}

test "escapes cost nothing and the emoji costs two" {
    try std.testing.expectEqual(@as(usize, 3), visible("abc"));
    try std.testing.expectEqual(@as(usize, 3), visible("\x1b[38;2;1;2;3mabc\x1b[0m"));
    try std.testing.expectEqual(@as(usize, 2), visible("🧠"));
    try std.testing.expectEqual(@as(usize, 0), visible(""));
    // The parallelograms are ambiguous width and count as one, which is what
    // the bar's arithmetic depends on.
    try std.testing.expectEqual(@as(usize, 2), visible("▰▱"));
}

test "a lone escape byte is not mistaken for a sequence" {
    try std.testing.expectEqual(@as(usize, 2), visible("\x1ba"));
}

test "digits refuses what is not a number" {
    try std.testing.expectEqual(@as(?usize, 120), digits("120"));
    try std.testing.expectEqual(@as(?usize, null), digits(""));
    try std.testing.expectEqual(@as(?usize, null), digits("12a"));
}
