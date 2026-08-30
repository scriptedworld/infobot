//! Removes what a finished session left on disk.
//!
//! Reads the hook payload on standard input and exits 0 whatever it is given. A
//! cleanup that fails is litter; a cleanup that raises is a hook failure
//! reported to somebody who was closing their terminal.
//!
//! It SAYS WHAT IT REMOVED, on stderr, while still exiting 0. A cleanup that
//! exits 0 having removed nothing is the same shape as a gate that passes having
//! checked nothing, and the difference has to be visible in a log rather than
//! invisible by design.

const std = @import("std");
const state = @import("state.zig");
const usage = @import("usage.zig");
const Ctx = @import("ctx.zig").Ctx;

/// Whether a session id names one file each.
///
/// Anything else, including a path separator smuggled through the payload, is
/// refused rather than joined onto a directory this walks with unlink.
pub fn named(session: []const u8) bool {
    if (session.len == 0) return false;
    if (std.mem.indexOf(u8, session, "..") != null) return false;
    if (std.mem.indexOfAny(u8, session, "/\\") != null) return false;
    return true;
}

/// Drops both of the session's files and returns the paths that were actually
/// there, so the caller can say what it did.
pub fn remove(ctx: Ctx, session: []const u8) []const []const u8 {
    var removed: std.ArrayList([]const u8) = .empty;
    const candidates = [_]?[]u8{
        state.path(ctx, session),
        usage.offsetPath(ctx, session),
    };
    for (candidates) |maybe| {
        const path = maybe orelse continue;
        ctx.dir().access(ctx.io, path, .{}) catch continue;
        ctx.dir().deleteFile(ctx.io, path) catch continue;
        removed.append(ctx.gpa, path) catch {};
    }
    return removed.items;
}

test "a session id that could escape the directory is refused" {
    try std.testing.expect(named("abcd-1234"));
    try std.testing.expect(!named(""));
    try std.testing.expect(!named("../etc/passwd"));
    try std.testing.expect(!named("a/b"));
    try std.testing.expect(!named("a\\b"));
    try std.testing.expect(!named(".."));
}
