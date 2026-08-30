//! What every part of the render needs and nothing else.
//!
//! Zig 0.16 threads an `Io` through every file and process call, so the choice
//! is between three parameters everywhere or one. This is the one. It also
//! makes the seams testable the way the Go implementation's are: `env` is read
//! rather than looked up globally, and `now` is a parameter rather than a call,
//! so a countdown is asserted against a fixed instant.

const std = @import("std");

pub const Ctx = struct {
    /// An arena, in practice. The process renders one line and exits, so
    /// nothing here frees and the whole allocation goes back at once.
    gpa: std.mem.Allocator,
    io: std.Io,
    env: *const std.process.Environ.Map,

    pub fn dir(self: Ctx) std.Io.Dir {
        _ = self;
        return .cwd();
    }

    pub fn getenv(self: Ctx, key: []const u8) []const u8 {
        return self.env.get(key) orelse "";
    }
};
