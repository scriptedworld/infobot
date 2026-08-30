//! Reads the session JSON Claude Code puts on stdin.
//!
//! EVERY FIELD IS OPTIONAL and is treated as optional. rate_limits appears only
//! for subscribers and only after the first API response; either window can be
//! absent on its own; used_percentage can be null early in a session. So the
//! payload is held as a dynamic tree rather than as a struct: an absent field
//! and a zero field must stay distinguishable. Zero is a claim, absence is not.
//!
//! ONE DIFFERENCE FROM THE GO IMPLEMENTATION, and it is a language difference
//! rather than a decision. Go's encoding/json decodes every JSON number into a
//! float64, so `1000000` and `1000000.0` are one case there. Zig's parser keeps
//! the distinction, handing back `.integer` for the first and `.float` for the
//! second, and `.number_string` for anything too large for either. `num` folds
//! all three, so a payload written either way reads the same as it does in Go.

const std = @import("std");

/// One JSON object. A null Map answers like an empty one, so a caller never has
/// to check before reaching through it.
pub const Map = struct {
    value: ?std.json.Value = null,

    pub const empty: Map = .{ .value = null };

    pub fn from(value: std.json.Value) Map {
        return switch (value) {
            .object => .{ .value = value },
            else => empty,
        };
    }

    fn get(self: Map, key: []const u8) ?std.json.Value {
        const value = self.value orelse return null;
        return switch (value) {
            .object => |object| object.get(key),
            else => null,
        };
    }

    /// The nested object at key, or an empty Map when it is absent or is not one.
    pub fn obj(self: Map, key: []const u8) Map {
        const found = self.get(key) orelse return empty;
        return from(found);
    }

    /// The string at key, or "" when it is absent or is not one.
    pub fn str(self: Map, key: []const u8) []const u8 {
        const found = self.get(key) orelse return "";
        return switch (found) {
            .string, .number_string => |text| text,
            else => "",
        };
    }

    /// The number at key and whether one was there. Anything that is not a
    /// number is absent data rather than a zero.
    pub fn num(self: Map, key: []const u8) ?f64 {
        const found = self.get(key) orelse return null;
        return asNumber(found);
    }

    /// The number at key or zero, for fields that are summed rather than
    /// displayed.
    ///
    /// The difference from `num` matters: a token count that is not there
    /// contributes nothing, where a percentage that is not there drops a
    /// segment.
    pub fn count(self: Map, key: []const u8) f64 {
        return self.num(key) orelse 0;
    }

    /// Whether key holds anything at all, null included.
    pub fn has(self: Map, key: []const u8) bool {
        return self.get(key) != null;
    }

    /// Whether the object holds no keys, which is how a caller tells an absent
    /// window from a present but empty one.
    pub fn isEmpty(self: Map) bool {
        const value = self.value orelse return true;
        return switch (value) {
            .object => |object| object.count() == 0,
            else => true,
        };
    }
};

/// A JSON number as f64, whichever of the three shapes the parser chose.
pub fn asNumber(value: std.json.Value) ?f64 {
    return switch (value) {
        .integer => |i| @floatFromInt(i),
        .float => |f| f,
        .number_string => |text| std.fmt.parseFloat(f64, text) catch null,
        else => null,
    };
}

test "absence and zero stay apart" {
    const text =
        \\{"a": 0, "b": "x", "c": {"d": 1.5}, "e": null}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, std.testing.allocator, text, .{});
    defer parsed.deinit();
    const m = Map.from(parsed.value);

    try std.testing.expectEqual(@as(?f64, 0), m.num("a"));
    try std.testing.expectEqual(@as(?f64, null), m.num("missing"));
    try std.testing.expectEqualStrings("x", m.str("b"));
    try std.testing.expectEqual(@as(?f64, 1.5), m.obj("c").num("d"));
    try std.testing.expect(m.has("e"));
    try std.testing.expect(!m.has("nope"));
    try std.testing.expect(m.obj("missing").isEmpty());
}

test "an integer and a float read the same, which Go gets for free" {
    const text =
        \\{"i": 1000000, "f": 1000000.0}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, std.testing.allocator, text, .{});
    defer parsed.deinit();
    const m = Map.from(parsed.value);
    try std.testing.expectEqual(m.num("i"), m.num("f"));
}
