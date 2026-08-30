//! What every part of the render needs and nothing else.
//!
//! Zig 0.16 threads an `Io` through every file and process call, so the choice
//! is between three parameters everywhere or one. This is the one. It also
//! makes the seams testable the way the Go implementation's are: `env` is read
//! rather than looked up globally, and `now` is a parameter rather than a call,
//! so a countdown is asserted against a fixed instant.

const std = @import("std");

/// What one render is being drawn against: the things that are the same for
/// every segment of one line and change between lines.
///
/// A struct rather than five parameters, because the row builders pass the
/// whole set down and a chain of them ran past the project's five-argument
/// limit. Grouping them also makes the seams explicit: `now` is a parameter
/// rather than a clock read, so a countdown is asserted against a fixed
/// instant, and `transcripts` is a root so a test can point the cost segment at
/// a fixture tree.
pub const View = struct {
    /// The home directory, for folding a path to a tilde.
    home: []const u8 = "",
    /// The pane columns, or 0 when they genuinely cannot be known.
    width: usize = 0,
    /// The wall clock in seconds, read once per render.
    now: f64 = 0,
    /// Gauges give way to their percentages when the row will not fit.
    compact: bool = false,
    /// Where the transcripts live. Empty means the real one, which is the only
    /// thing the status line ever passes.
    transcripts: []const u8 = "",

    pub fn withCompact(self: View, compact: bool) View {
        var out = self;
        out.compact = compact;
        return out;
    }
};

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
