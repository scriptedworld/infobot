//! The status line's entry point, and nothing else.
//!
//! The render lives in rows.zig, where a test can reach it and a checker can
//! read it. A body written in an entry point can only be exercised by running
//! the binary.

const std = @import("std");
const payload = @import("payload.zig");
const rows = @import("rows.zig");
const state = @import("state.zig");
const width = @import("width.zig");
const ctxmod = @import("ctx.zig");
const Ctx = ctxmod.Ctx;
const View = ctxmod.View;

/// Reads the session JSON on stdin and writes the rows.
///
/// It exits 0 whatever it is given. Unparseable input is not worth a traceback
/// in the status bar, and a status line that fails shows nothing at all.
pub fn main(init: std.process.Init) u8 {
    // One arena for the whole render. The process draws one line and exits, so
    // there is nothing to free and no point pretending otherwise.
    var arena = std.heap.ArenaAllocator.init(init.gpa);
    defer arena.deinit();

    const ctx: Ctx = .{
        .gpa = arena.allocator(),
        .io = init.io,
        .env = init.environ_map,
    };

    var in_buf: [64 * 1024]u8 = undefined;
    var in = std.Io.File.stdin().reader(ctx.io, &in_buf);
    const raw = in.interface.allocRemaining(ctx.gpa, .limited(16 * 1024 * 1024)) catch return 0;

    const parsed = std.json.parseFromSlice(std.json.Value, ctx.gpa, raw, .{}) catch return 0;
    const data = payload.Map.from(parsed.value);
    if (data.isEmpty()) return 0;

    // The wall clock, in whole seconds. It is read ONCE and passed down, so
    // every segment of one render agrees about what time it is: a countdown
    // computed against a clock read twice can cross a minute boundary between
    // the two and print two different remaining times on one line.
    const now: i64 = @intCast(@divFloor(std.Io.Timestamp.now(ctx.io, .real).nanoseconds, std.time.ns_per_s));

    // Guarded inside write, and called before the render so a payload the bar
    // cannot draw still leaves its numbers on disk.
    state.write(ctx, data, now);

    const lines = rows.build(ctx, data, .{
        .home = ctx.getenv("HOME"),
        .width = width.terminal(ctx.gpa, ctx.io, ctx.env),
        .now = @floatFromInt(now),
    });

    var out_buf: [64 * 1024]u8 = undefined;
    var out = std.Io.File.stdout().writer(ctx.io, &out_buf);
    for (lines) |line| {
        // STOP AT THE FIRST FAILED WRITE rather than discarding the error.
        // There is nowhere to report it, since stdout is the thing that failed
        // and the exit code is 0 by contract, but a second row written after
        // the first failed is a torn status line rather than a missing one, and
        // torn is harder to read as broken.
        out.interface.print("{s}\n", .{line}) catch break;
    }
    out.interface.flush() catch {};
    return 0;
}
