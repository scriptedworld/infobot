//! The individual segments, each rendering itself.

const std = @import("std");
const num = @import("num.zig");
const payload = @import("payload.zig");
const palette = @import("palette.zig");
const state = @import("state.zig");
const width = @import("width.zig");
const Ctx = @import("ctx.zig").Ctx;

pub const brain = "🧠";
pub const hourglass = "⏳";
pub const calendar = "📅";
pub const money_glyph = "💵";
pub const bullseye = "🎯";
pub const root_glyph = "⌂";

/// Powerline thin separator from Iosevka Nerd Font, which kitty is configured
/// with. A plain vertical bar is the fallback anywhere the glyph is missing, one
/// column either way, so nothing shifts.
pub const sep_glyph = "\u{e0b1}";

/// The left rail, out of oh-my-zsh's multiline prompt: a rounded corner opening
/// the block, a rounded one closing it, and a tee for any row between. It says
/// the two rows are one block rather than two neighbours, which matters when the
/// row above is full width and the row below is half of it.
///
/// A lone row gets the stub instead. An opening corner with no closing corner
/// under it reads as a block that failed to finish.
pub const rail_top = "╭─ ";
pub const rail_mid = "├─ ";
pub const rail_end = "╰─ ";
pub const rail_one = "╶─ ";

/// The glyphs Claude Code draws in its own compaction meter, so the two read as
/// one instrument. They carry no sub-cell steps, so every bit of resolution
/// comes from the cell count, which is why the bar takes whatever columns row
/// one has left.
pub const filled = "▰";
pub const empty = "▱";

/// Used when the width is unknown, so there is nothing to subtract from and 50
/// is a chosen number rather than a fit.
pub const bar_fallback = 50;
/// The width below which the bar is DROPPED rather than clamped to. Clamping
/// overflows: in a 120-column pane a long path leaves six columns, so an
/// eight-cell floor wraps the row, costing the whole line to save a bar that at
/// 12% a cell was not saying much.
pub const bar_min = 8;
/// What Claude Code keeps for itself, so the pane width is NOT the budget. It
/// indents two columns and then keeps one at the right.
pub const margin = 3;
/// Fixed rather than a share of the slack. The rate limit windows are checked
/// occasionally, not watched, so 10% a cell is enough resolution, and a fixed
/// width keeps the row from moving under them as the context bar above grows.
pub const window_cells = 10;
/// The clear space kept between the meter row and the cost pushed to its right,
/// so the two read as separate things.
pub const cost_gutter = 3;

/// The window lengths come from the payload's own field names.
pub const five_hour = 5 * 3600;
pub const seven_day = 7 * 86400;

pub fn sep(ctx: Ctx) []const u8 {
    if (palette.plain(ctx.env)) return " " ++ sep_glyph ++ " ";
    return " " ++ palette.sep_colour ++ sep_glyph ++ palette.reset ++ " ";
}

fn tinted(ctx: Ctx, text: []const u8, escape: []const u8) []const u8 {
    if (palette.plain(ctx.env)) return text;
    return std.fmt.allocPrint(ctx.gpa, "{s}{s}{s}", .{ escape, text, palette.reset }) catch text;
}

pub fn cyan(ctx: Ctx, text: []const u8) []const u8 {
    return tinted(ctx, text, palette.path_colour);
}

/// Marks a word present to be scanned past rather than read.
///
/// An empty span would still emit an open and a reset with nothing between,
/// which happens at both ends of the bar where the fill or the track is empty.
pub fn dim(ctx: Ctx, text: []const u8) []const u8 {
    if (text.len == 0) return text;
    return tinted(ctx, text, palette.dim_colour);
}

/// Wraps text in a colour interpolated from the percentage consumed.
fn colour(ctx: Ctx, pct: f64, text: []const u8) []const u8 {
    if (text.len == 0 or palette.plain(ctx.env)) return text;
    // Inverted rather than merely red past the alarm: the message there is not
    // "high" but "about to matter", and a hue change alone stops being seen
    // after the twentieth time.
    var buf: [64]u8 = undefined;
    const escape = palette.ramp(pct, &buf);
    return std.fmt.allocPrint(ctx.gpa, "{s}{s}{s}", .{ escape, text, palette.reset }) catch text;
}

/// A proportional bar whose fill fades along the ramp, cell by cell.
///
/// Each cell is coloured for the percentage IT stands for, not for the bar's
/// total: the first is green because it means 2%, and the last filled one
/// matches the colour of the number beside it because they mean the same thing.
/// So the fade is a fixed property of the bar and only its length moves.
///
/// The bar is never cut into. The percentage is printed with the counts, where
/// it can be read as a number rather than found among the parallelograms.
///
/// A tint overrides the fade and paints every filled cell one colour. The rate
/// limit gauges use it to mean something the fade cannot: not where each cell
/// sits, but whether the whole reading is a problem.
pub fn bar(ctx: Ctx, pct_in: f64, cells: usize, tint: []const u8) []const u8 {
    const pct = num.clamp(pct_in, 0, 100);
    if (cells == 0) return "";
    const full: usize = @intCast(@max(0, num.roundInt(pct / 100 * @as(f64, @floatFromInt(cells)))));

    var out: std.ArrayList(u8) = .empty;
    if (palette.plain(ctx.env)) {
        for (0..cells) |i| out.appendSlice(ctx.gpa, if (i < full) filled else empty) catch {};
        return out.items;
    }

    // One escape per RUN of cells sharing a style, not one per cell. Below the
    // ramp's pivot a forty-cell bar resolves to a handful of distinct shades,
    // so this is most of the escapes saved for nothing given up.
    var held: []const u8 = "";
    var buf: [64]u8 = undefined;
    for (0..cells) |i| {
        var style: []const u8 = palette.empty_colour;
        var glyph: []const u8 = empty;
        if (i < full) {
            glyph = filled;
            style = tint;
            if (style.len == 0) {
                style = palette.ramp(@as(f64, @floatFromInt(i + 1)) / @as(f64, @floatFromInt(cells)) * 100, &buf);
            }
        }
        if (!std.mem.eql(u8, style, held)) {
            // RESET first, always. The alarm style carries bold and a
            // background as well as a colour, so a bare colour change after it
            // leaves both switched on for the rest of the line.
            out.appendSlice(ctx.gpa, palette.reset) catch {};
            out.appendSlice(ctx.gpa, style) catch {};
            held = ctx.gpa.dupe(u8, style) catch style;
        }
        out.appendSlice(ctx.gpa, glyph) catch {};
    }
    out.appendSlice(ctx.gpa, palette.reset) catch {};
    return out.items;
}

/// Hangs the rows off a left rail, corners rounded.
///
/// Drawn in the same dimcyan as the bar's empty cells rather than in its own
/// colour, so the frame stays one voice with the meter and neither competes with
/// the numbers.
pub fn rail(ctx: Ctx, rows: []const []const u8) []const []const u8 {
    if (rows.len == 0) return rows;
    const out = ctx.gpa.alloc([]const u8, rows.len) catch return rows;
    for (rows, 0..) |row, i| {
        const lead = if (rows.len == 1)
            rail_one
        else if (i == 0)
            rail_top
        else if (i == rows.len - 1)
            rail_end
        else
            rail_mid;

        out[i] = if (palette.plain(ctx.env))
            std.fmt.allocPrint(ctx.gpa, "{s}{s}", .{ lead, row }) catch row
        else
            std.fmt.allocPrint(ctx.gpa, "{s}{s}{s}{s}", .{ palette.empty_colour, lead, palette.reset, row }) catch row;
    }
    return out;
}

/// Compact token counts. 142000 becomes 142k, 1000000 becomes 1.0M.
pub fn tokens(gpa: std.mem.Allocator, n: f64) []const u8 {
    // num.fixed rather than {d:.N}, because Zig rounds a tie away from zero
    // and Go takes the even neighbour. See num.zig.
    if (n >= 1_000_000) return std.fmt.allocPrint(gpa, "{s}M", .{num.fixed(gpa, n / 1_000_000, 1)}) catch "";
    if (n >= 1_000) return std.fmt.allocPrint(gpa, "{s}k", .{num.fixed(gpa, n / 1_000, 0)}) catch "";
    return num.fixed(gpa, n, 0);
}

/// The time until reset, as the largest two units that are non-zero.
///
/// Empty when the timestamp is missing or already past. A countdown reading "0m"
/// suggests a reset is imminent when it has already happened and the number is
/// simply stale.
pub fn countdown(gpa: std.mem.Allocator, resets_at: f64, now: f64) []const u8 {
    if (resets_at == 0) return "";
    const remaining: i64 = @intFromFloat(resets_at - now);
    if (remaining <= 0) return "";
    // Unsigned from here on. `remaining` is positive by the guard above, and a
    // zero-padded SIGNED value prints its sign, so `{d:0>2}` on an i64 minute
    // gives `3h+0m` where the row wants `3h00m`.
    const whole: u64 = @intCast(remaining);
    const days = whole / 86400;
    const hours = (whole % 86400) / 3600;
    const minutes = (whole % 3600) / 60;
    if (days != 0) return std.fmt.allocPrint(gpa, "{d}d{d}h", .{ days, hours }) catch "";
    if (hours != 0) return std.fmt.allocPrint(gpa, "{d}h{d:0>2}m", .{ hours, minutes }) catch "";
    return std.fmt.allocPrint(gpa, "{d}m", .{minutes}) catch "";
}

pub fn homeRelative(gpa: std.mem.Allocator, path: []const u8, home: []const u8) []const u8 {
    if (std.mem.eql(u8, path, home)) return "~";
    if (home.len != 0 and path.len > home.len and
        std.mem.startsWith(u8, path, home) and path[home.len] == '/')
    {
        return std.fmt.allocPrint(gpa, "~{s}", .{path[home.len..]}) catch path;
    }
    return path;
}

/// Puts the non-empty parts together with a single space, so dropping the bar
/// drops its separating space with it instead of leaving a gap the emoji makes
/// conspicuous.
pub fn join(gpa: std.mem.Allocator, parts: []const []const u8) []const u8 {
    var kept: std.ArrayList([]const u8) = .empty;
    defer kept.deinit(gpa);
    for (parts) |part| {
        if (part.len != 0) kept.append(gpa, part) catch {};
    }
    return std.mem.join(gpa, " ", kept.items) catch "";
}

/// used/total and a percentage, or nothing if the numbers are not there yet.
///
/// cells of 0 drops the bar and keeps the numbers, which is both what a pane too
/// narrow to carry a bar gets and how the row measures the room for one.
///
/// The WHOLE span is coloured here, while the rate-limit segments colour only
/// their number. That asymmetry is deliberate: the context window is watched
/// constantly while working and the other two are infrequent details, so a wider
/// block of colour makes the loud one loud.
pub fn contextSegment(ctx: Ctx, cw: payload.Map, cells: usize) []const u8 {
    // One formula, shared with the file the state module leaves behind, so a
    // bar reading 56% cannot sit beside a file saying something else.
    const window = state.figures(cw) orelse return "";
    // The percentage sits with the counts, where it reads as one measurement
    // rather than as a number to be found inside the picture of it.
    const counts = std.fmt.allocPrint(ctx.gpa, "{s}/{s} ({s}% consumed)", .{
        tokens(ctx.gpa, window.used),
        tokens(ctx.gpa, window.size),
        num.fixed(ctx.gpa, window.percent, 0),
    }) catch return "";
    return join(ctx.gpa, &.{ brain, bar(ctx, window.percent, cells, ""), colour(ctx, window.percent, counts) });
}

/// The columns a row costs: its parts joined the way they will be joined, so the
/// separators between them count, and the rail it hangs off.
pub fn rowWidth(ctx: Ctx, parts: []const []const u8) usize {
    const joined = std.mem.join(ctx.gpa, sep(ctx), parts) catch return 0;
    return width.visible(joined) + width.visible(rail_top);
}

/// How far through the window we are, and whether that is knowable at all.
fn elapsedFraction(resets_at: f64, span: i64, now: f64) ?f64 {
    if (resets_at == 0 or span == 0) return null;
    const remaining = resets_at - now;
    if (remaining <= 0) return null;
    return num.clamp(1 - remaining / @as(f64, @floatFromInt(span)), 0, 1);
}

/// A gauge and a countdown. No token counts exist for these windows.
///
/// label carries the emoji and the name together because they are one fixed
/// string at both call sites.
///
/// The percentage is drawn rather than printed. It is the same bar as the
/// context window's, on the same ramp, so 80% is the same shade wherever it
/// appears and one glance reads all three meters.
///
/// The countdown stays as a number because a bar cannot carry it, and because it
/// is what makes the gauge actionable: half spent with four hours to go reads
/// very differently from half spent with ten minutes to go.
///
/// It is deliberately UNCOLOURED. It wants its own scale and probably an
/// inverted one, running TOWARD green as it nears zero, since a reset getting
/// closer is good news. Undecided, so left plain rather than guessed at.
pub fn limitSegment(
    ctx: Ctx,
    label: []const u8,
    window: payload.Map,
    span: i64,
    compact: bool,
    now: f64,
) []const u8 {
    if (window.isEmpty()) return "";
    const pct = window.num("used_percentage") orelse return "";
    const resets_at = window.count("resets_at");

    // Concern where it can be worked out, raw spend where it cannot: with no
    // reset time there is no window position, so the gauge falls back to
    // meaning what the context meter's colour means.
    var buf: [64]u8 = undefined;
    var tint = palette.ramp(pct, &buf);
    if (elapsedFraction(resets_at, span, now)) |elapsed| {
        tint = palette.paceTint(pct, elapsed, &buf);
    }
    const held = ctx.gpa.dupe(u8, tint) catch tint;

    var gauge = bar(ctx, pct, window_cells, held);
    if (compact) {
        const text = std.fmt.allocPrint(ctx.gpa, "{s}%", .{num.fixed(ctx.gpa, pct, 0)}) catch "";
        gauge = tinted(ctx, text, held);
    }
    return join(ctx.gpa, &.{ label, gauge, countdown(ctx.gpa, resets_at, now) });
}

test "tokens are compact and keep their shape" {
    // An arena, because that is what the program runs on: every helper here
    // allocates its pieces and the process frees the lot at once. Threading
    // ownership through them to satisfy a leak-checking allocator would be
    // ceremony the real caller never performs.
    var scratch = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer scratch.deinit();
    const gpa = scratch.allocator();
    for ([_]struct { in: f64, want: []const u8 }{
        .{ .in = 0, .want = "0" },
        .{ .in = 999, .want = "999" },
        .{ .in = 1000, .want = "1k" },
        .{ .in = 142000, .want = "142k" },
        .{ .in = 999999, .want = "1000k" },
        .{ .in = 1000000, .want = "1.0M" },
        .{ .in = 2700000, .want = "2.7M" },
    }) |c| {
        try std.testing.expectEqualStrings(c.want, tokens(gpa, c.in));
    }
}

test "a countdown gives the largest two units, and nothing once past" {
    var scratch = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer scratch.deinit();
    const gpa = scratch.allocator();
    for ([_]struct { at: f64, want: []const u8 }{
        .{ .at = 0, .want = "" },
        .{ .at = 1000, .want = "" }, // already past
        .{ .at = 10_000 + 3 * 3600, .want = "3h00m" },
        .{ .at = 10_000 + 30 * 60, .want = "30m" },
        .{ .at = 10_000 + 2 * 86400 + 5 * 3600, .want = "2d5h" },
        .{ .at = 10_000 + 3600 + 5 * 60, .want = "1h05m" },
    }) |c| {
        try std.testing.expectEqualStrings(c.want, countdown(gpa, c.at, 10_000));
    }
}

test "the home directory folds to a tilde, and a sibling does not" {
    var scratch = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer scratch.deinit();
    const gpa = scratch.allocator();
    try std.testing.expectEqualStrings("~", homeRelative(gpa, "/home/me", "/home/me"));
    try std.testing.expectEqualStrings("~/x", homeRelative(gpa, "/home/me/x", "/home/me"));
    // /home/mears must not fold against /home/me.
    try std.testing.expectEqualStrings("/home/mears", homeRelative(gpa, "/home/mears", "/home/me"));
}
