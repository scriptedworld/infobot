//! The colours, and the two ramps that carry meaning.

const std = @import("std");
const num = @import("num.zig");

/// TRUECOLOR. COLORTERM=truecolor and CLAUDE_CODE_TMUX_TRUECOLOR=1 are set in
/// settings.json, which is what defeats Claude Code's habit of capping colour
/// at the 256 palette when it sees $TMUX. A 24-bit escape reaches the terminal
/// intact, so the ramp can be continuous instead of stepped.
///
/// Two straight lines rather than one. Green to yellow across the long stretch
/// where nothing is happening, then yellow to red compressed into 75-90, so the
/// colour moves fastest exactly where a glance needs to tell 80 from 88.
pub const green: Rgb = .{ .r = 60, .g = 200, .b = 90 };
pub const yellow: Rgb = .{ .r = 235, .g = 220, .b = 40 };
pub const red: Rgb = .{ .r = 225, .g = 45, .b = 45 };

const pivot = 75.0;
const alarm_at = 90.0;
/// Where the alarm has arrived in full.
const alarm_top = 100.0;

pub const reset = "\x1b[0m";

/// The alarm FADES IN across 90 to 100 rather than switching on at 90.
///
/// Its foreground starts at red, which is exactly where the ramp below it
/// arrives, so nothing jumps at the boundary: the last yellow-to-red cell and
/// the first alarm cell are the same colour. It then runs to a pale yellow.
///
/// Its background starts at the TERMINAL'S OWN and fills to a deep red, so the
/// first frame of the fade paints nothing a reader can see and the inversion
/// arrives instead of slamming on.
///
/// BLACK IS NOT INVISIBLE, which is the trap this walked into first. The
/// palette here is Tokyo Night, #1a1b26 at full opacity in kitty and inherited
/// by herdr, so a pure black background is a dark notch against it: a seam
/// exactly where the fade exists to have none. Matching the backdrop is what
/// makes it vanish.
///
/// Bold is the one part that cannot fade, so it is on across the whole band.
const backdrop: Rgb = .{ .r = 26, .g = 27, .b = 38 };
const alarm_fg: Rgb = .{ .r = 250, .g = 240, .b = 120 };
const alarm_bg: Rgb = .{ .r = 180, .g = 25, .b = 25 };

/// The ENCOM teal, the same value the i3 bar and claws use ($encom_teal,
/// #00a595), so the status line reads as part of the desktop instead of beside
/// it.
pub const path_colour = "\x1b[38;2;0;165;149m";

/// The unused cells are an outline glyph and NO background. The glyph carries
/// its own shape, and a background behind it would fill the gaps between the
/// parallelograms and turn the tail of the bar into a solid slab.
///
/// $encom_dimcyan from the i3 config, one step up from the deepcyan the i3 bar
/// uses for inactive_workspace.
pub const empty_colour = "\x1b[38;2;0;95;95m";

/// Dim, so it divides without competing.
pub const sep_colour = "\x1b[38;2;70;80;85m";
pub const dim_colour = "\x1b[38;2;120;130;135m";

pub const Rgb = struct {
    r: i64,
    g: i64,
    b: i64,

    /// The foreground escape, written into buf.
    pub fn fg(self: Rgb, buf: []u8) []const u8 {
        return std.fmt.bufPrint(buf, "\x1b[38;2;{d};{d};{d}m", .{ self.r, self.g, self.b }) catch "";
    }
};

/// Interpolates between two colours, rounding each channel the way Python's
/// round() does. See num.zig for why that matters.
pub fn mix(a: Rgb, b: Rgb, t_in: f64) Rgb {
    const t = num.clamp(t_in, 0, 1);
    return .{
        .r = num.roundInt(@as(f64, @floatFromInt(a.r)) + @as(f64, @floatFromInt(b.r - a.r)) * t),
        .g = num.roundInt(@as(f64, @floatFromInt(a.g)) + @as(f64, @floatFromInt(b.g - a.g)) * t),
        .b = num.roundInt(@as(f64, @floatFromInt(a.b)) + @as(f64, @floatFromInt(b.b - a.b)) * t),
    };
}

/// One anchor on the diverging pace scale.
const PaceStop = struct { at: f64, colour: Rgb };

const pace_stops = [_]PaceStop{
    .{ .at = 0.0, .colour = .{ .r = 70, .g = 140, .b = 235 } }, // blue: barely touched
    .{ .at = 70.0, .colour = .{ .r = 220, .g = 225, .b = 230 } }, // white: under-spending
    .{ .at = 100.0, .colour = green }, // lands exactly full as it resets
    .{ .at = 125.0, .colour = yellow }, // empties a fifth of the way early
    .{ .at = 150.0, .colour = red }, // empties a third of the way early
};

/// How much of a window has to elapse before its verdict is shown in full. A
/// fraction rather than a duration, so the seven day window matures at the same
/// point in its own life instead of after an afternoon.
pub const pace_confident = 0.6;
/// An arithmetic floor only, to divide by. The fade stops a wild early
/// projection from being believed; this stops it being infinite.
pub const pace_min_elapsed = 0.01;

/// Whether every escape is to be dropped.
///
/// NO_COLOR is honoured for EVERY escape, not just the obvious ones. A hardcoded
/// separator or bracket surviving NO_COLOR=1 stripping the segments around it
/// gives output that is neither coloured nor clean.
pub fn plain(env: *const std.process.Environ.Map) bool {
    const value = env.get("NO_COLOR") orelse return false;
    return value.len != 0;
}

/// The ramp's colour at pct, below the alarm band.
pub fn consumption(pct: f64) Rgb {
    if (pct <= pivot) return mix(green, yellow, pct / pivot);
    return mix(yellow, red, (pct - pivot) / (alarm_at - pivot));
}

/// Just the escape for a percentage, with no text and no reset.
///
/// `colour` closes itself with a reset after every call, so a bar built from it
/// would emit an open and a close around each of forty cells. The bar needs the
/// code alone, so it can open a span once and hold it for every cell that shares
/// a colour.
pub fn ramp(pct: f64, buf: []u8) []const u8 {
    if (pct >= alarm_at) return alarm(pct, buf);
    return consumption(pct).fg(buf);
}

/// The alarm style at pct, faded in from the top of the ramp.
///
/// At alarm_at it is red on the terminal's own background, which is the colour
/// the ramp beneath it arrives at and a background nothing can see, so the
/// boundary has nothing to show. At alarm_top it is pale yellow on deep red.
///
/// Both ends interpolate together, so the background filling in and the
/// foreground brightening are one movement rather than two.
pub fn alarm(pct: f64, buf: []u8) []const u8 {
    const into = num.clamp((pct - alarm_at) / (alarm_top - alarm_at), 0, 1);
    const front = mix(red, alarm_fg, into);
    const back = mix(backdrop, alarm_bg, into);
    return std.fmt.bufPrint(buf, "\x1b[1;38;2;{d};{d};{d};48;2;{d};{d};{d}m", .{
        front.r, front.g, front.b, back.r, back.g, back.b,
    }) catch "";
}

/// Walks the diverging stops and interpolates between the two that bracket the
/// projection.
///
/// Outside the ends it clamps, so a window projected to land at 400% is the same
/// red as one landing at 150: once it will not last, by how much it will not
/// last stops changing what to do about it.
pub fn paceRgb(projected: f64) Rgb {
    var low = pace_stops[0];
    for (pace_stops[1..]) |high| {
        if (projected <= high.at) {
            return mix(low.colour, high.colour, (projected - low.at) / (high.at - low.at));
        }
        low = high;
    }
    return pace_stops[pace_stops.len - 1].colour;
}

/// The window's verdict, faded toward green by how much it can say yet.
///
/// Two independent readings in one colour. Where the window is projected to land
/// decides the hue; how far through the window we are decides how much of that
/// hue is shown, against green for the rest.
///
/// Green rather than grey or nothing, because green is this scale's "no comment"
/// as well as its "on rate", and both mean there is nothing to act on.
pub fn paceTint(pct: f64, elapsed: f64, buf: []u8) []const u8 {
    const projected = pct / @max(elapsed, pace_min_elapsed);
    // Squared, so the verdict stays quiet through the middle of the window and
    // arrives late. Running hot with most of the window still ahead is not
    // something to act on, and a linear fade is already half shouting at the
    // halfway mark.
    var trust = @min(1.0, elapsed / pace_confident);
    trust *= trust;
    return mix(green, paceRgb(projected), trust).fg(buf);
}

test "the ramp meets the alarm at the same colour" {
    var a: [64]u8 = undefined;
    var b: [64]u8 = undefined;
    // At exactly 90 the alarm's foreground is red, which is where the ramp
    // beneath it arrives. The boundary has nothing to show.
    const at_alarm = alarm(90, &a);
    const ramp_top = consumption(90).fg(&b);
    try std.testing.expect(std.mem.indexOf(u8, at_alarm, "225;45;45") != null);
    try std.testing.expect(std.mem.indexOf(u8, ramp_top, "225;45;45") != null);
}

test "mix rounds ties to even, not away from zero" {
    // 60 to 61 at t=0.5 is 60.5, which rounds to 60 rather than 61.
    const a: Rgb = .{ .r = 60, .g = 0, .b = 0 };
    const b: Rgb = .{ .r = 61, .g = 0, .b = 0 };
    try std.testing.expectEqual(@as(i64, 60), mix(a, b, 0.5).r);
}
