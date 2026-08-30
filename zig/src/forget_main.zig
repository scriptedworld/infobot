//! The SessionEnd entry point, and nothing else.
//!
//! Claude Code runs the status line once per transcript entry and never again
//! once a session ends, so nothing would remove what the last render left
//! behind. One file per session, forever, is what this exists to prevent.

const std = @import("std");
const forget = @import("forget.zig");
const payload = @import("payload.zig");
const Ctx = @import("ctx.zig").Ctx;

/// Runs the cleanup and always reports success to the hook runner.
pub fn main(init: std.process.Init) u8 {
    var arena = std.heap.ArenaAllocator.init(init.gpa);
    defer arena.deinit();

    const ctx: Ctx = .{
        .gpa = arena.allocator(),
        .io = init.io,
        .env = init.environ_map,
    };

    var err_buf: [4096]u8 = undefined;
    var log = std.Io.File.stderr().writer(ctx.io, &err_buf);
    defer log.interface.flush() catch {};

    var in_buf: [64 * 1024]u8 = undefined;
    var in = std.Io.File.stdin().reader(ctx.io, &in_buf);
    const raw = in.interface.allocRemaining(ctx.gpa, .limited(16 * 1024 * 1024)) catch return 0;

    const parsed = std.json.parseFromSlice(std.json.Value, ctx.gpa, raw, .{}) catch return 0;
    const data = payload.Map.from(parsed.value);
    if (data.isEmpty()) return 0;

    // The three writes below discard their error deliberately. The log is
    // stderr, so a failed write has nowhere to be reported, and the exit code
    // is 0 by contract because a hook that raises interrupts somebody closing
    // their terminal.
    const session = data.str("session_id");
    if (!forget.named(session)) {
        log.interface.print("forget-session: refused session id \"{s}\"\n", .{session}) catch {};
        return 0;
    }

    const removed = forget.remove(ctx, session);
    if (removed.len == 0) {
        log.interface.print("forget-session: {s} had nothing to remove\n", .{session}) catch {};
        return 0;
    }
    const list = std.mem.join(ctx.gpa, " ", removed) catch "";
    log.interface.print("forget-session: {s} removed {s}\n", .{ session, list }) catch {};
    return 0;
}
