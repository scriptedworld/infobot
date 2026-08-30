//! The two rows, composed from segments that each render themselves.

const std = @import("std");
const payload = @import("payload.zig");
const palette = @import("palette.zig");
const pricing = @import("pricing.zig");
const seg = @import("segments.zig");
const state = @import("state.zig");
const usage = @import("usage.zig");
const width = @import("width.zig");
const Ctx = @import("ctx.zig").Ctx;

/// The model, and the effort level when one is set.
fn modelPart(ctx: Ctx, data: payload.Map) []const u8 {
    var model = data.obj("model").str("display_name");
    if (model.len == 0) model = data.obj("model").str("id");
    if (model.len == 0) return "";
    const effort = data.obj("effort").str("level");
    if (effort.len != 0) {
        return std.fmt.allocPrint(ctx.gpa, "{s} - {s}", .{ model, effort }) catch model;
    }
    return model;
}

/// Where you are, and where the project root is when it differs.
///
/// Repeating the root is noise on the common case, which is a session started
/// where the work is.
fn pathParts(ctx: Ctx, data: payload.Map, home: []const u8, into: *std.ArrayList([]const u8)) void {
    const workspace = data.obj("workspace");
    const cwd = workspace.str("current_dir");
    const project = workspace.str("project_dir");

    if (cwd.len != 0) {
        into.append(ctx.gpa, seg.cyan(ctx, seg.homeRelative(ctx.gpa, cwd, home))) catch {};
    }
    if (project.len != 0 and !std.mem.eql(u8, project, cwd)) {
        // The marker sits outside the colour the path carries, so what is
        // tinted is the path and not the annotation.
        const tinted = seg.cyan(ctx, seg.homeRelative(ctx.gpa, project, home));
        const part = std.fmt.allocPrint(ctx.gpa, "{s} {s}", .{ seg.root_glyph, tinted }) catch return;
        into.append(ctx.gpa, part) catch {};
    }
}

/// The first eight of the session id, which is what the harness shows.
fn sessionPart(ctx: Ctx, data: payload.Map) []const u8 {
    var id = data.str("session_id");
    if (id.len == 0) return "";
    if (id.len > 8) id = id[0..8];
    return std.fmt.allocPrint(ctx.gpa, "⟨{s}⟩", .{id}) catch "";
}

/// The two rate limit windows, each dropped when its data is absent.
fn limitParts(ctx: Ctx, data: payload.Map, compact: bool, now: f64) []const []const u8 {
    const limits = data.obj("rate_limits");
    var parts: std.ArrayList([]const u8) = .empty;
    const windows = [_]struct { label: []const u8, key: []const u8, span: i64 }{
        .{ .label = seg.hourglass ++ " 5hr", .key = "five_hour", .span = seg.five_hour },
        .{ .label = seg.calendar ++ " 7d", .key = "seven_day", .span = seg.seven_day },
    };
    for (windows) |w| {
        const part = seg.limitSegment(ctx, w.label, limits.obj(w.key), w.span, compact, now);
        if (part.len != 0) parts.append(ctx.gpa, part) catch {};
    }
    return parts.items;
}

/// The context segment with its bar filling whatever the row leaves over.
///
/// others is the rest of the row and at is where this segment sits in it, so
/// what gets measured is the line as it will be printed rather than a sum of
/// pieces that forgets the separators.
///
/// One pass, because the rest of the segment is the same width whatever the bar
/// does: one more cell is one more column and nothing else moves.
fn fitted(ctx: Ctx, cw: payload.Map, others: []const []const u8, at: usize, w: usize) []const u8 {
    if (w == 0) return seg.contextSegment(ctx, cw, seg.bar_fallback);

    // The bar also brings the space that separates it from the counts, which
    // the bar-less form measured here does not have.
    const bare = seg.contextSegment(ctx, cw, 0);
    var row: std.ArrayList([]const u8) = .empty;
    defer row.deinit(ctx.gpa);
    row.appendSlice(ctx.gpa, others[0..at]) catch return bare;
    row.append(ctx.gpa, bare) catch return bare;
    row.appendSlice(ctx.gpa, others[at..]) catch return bare;

    const used = seg.rowWidth(ctx, row.items) + seg.margin + 1;
    if (used >= w) return bare;
    const spare = w - used;
    if (spare < seg.bar_min) return bare;
    return seg.contextSegment(ctx, cw, spare);
}

/// Puts the context segment on whichever row can hold it, bar sized to fit, and
/// hands both rows back.
///
/// Row one is where it belongs, but only if row one can hold it with no bar at
/// all. A narrow split or a long path puts it over the budget before a single
/// cell of bar is added, and then it goes to the meter row rather than being
/// truncated: what truncation eats is the end of the row, the session id.
fn placeContext(
    ctx: Ctx,
    cw: payload.Map,
    identity: []const []const u8,
    tail: []const []const u8,
    meters: []const []const u8,
    w: usize,
) struct { identity: []const []const u8, meters: []const []const u8 } {
    const bare = seg.contextSegment(ctx, cw, 0);
    if (bare.len == 0) return .{ .identity = identity, .meters = meters };

    if (w != 0) {
        var full: std.ArrayList([]const u8) = .empty;
        defer full.deinit(ctx.gpa);
        full.appendSlice(ctx.gpa, identity) catch {};
        full.append(ctx.gpa, bare) catch {};
        full.appendSlice(ctx.gpa, tail) catch {};
        if (seg.rowWidth(ctx, full.items) > w -| seg.margin) {
            var moved: std.ArrayList([]const u8) = .empty;
            moved.append(ctx.gpa, fitted(ctx, cw, meters, 0, w)) catch {};
            moved.appendSlice(ctx.gpa, meters) catch {};
            return .{ .identity = identity, .meters = moved.items };
        }
    }

    var others: std.ArrayList([]const u8) = .empty;
    defer others.deinit(ctx.gpa);
    others.appendSlice(ctx.gpa, identity) catch {};
    others.appendSlice(ctx.gpa, tail) catch {};

    var out: std.ArrayList([]const u8) = .empty;
    out.appendSlice(ctx.gpa, identity) catch {};
    out.append(ctx.gpa, fitted(ctx, cw, others.items, identity.len, w)) catch {};
    return .{ .identity = out.items, .meters = meters };
}

/// The two rows as lists of parts, before they are joined.
fn compose(ctx: Ctx, data: payload.Map, home: []const u8, w: usize, compact: bool, now: f64) [2][]const []const u8 {
    var identity: std.ArrayList([]const u8) = .empty;
    const model = modelPart(ctx, data);
    if (model.len != 0) identity.append(ctx.gpa, model) catch {};
    pathParts(ctx, data, home, &identity);

    var tail: std.ArrayList([]const u8) = .empty;
    const session = sessionPart(ctx, data);
    if (session.len != 0) tail.append(ctx.gpa, session) catch {};

    const meters = limitParts(ctx, data, compact, now);
    const placed = placeContext(ctx, data.obj("context_window"), identity.items, tail.items, meters, w);

    var first: std.ArrayList([]const u8) = .empty;
    first.appendSlice(ctx.gpa, placed.identity) catch {};
    first.appendSlice(ctx.gpa, tail.items) catch {};
    return .{ first.items, placed.meters };
}

/// The two rows.
///
/// Claude Code renders each printed line as its own row. The context window is
/// the meter watched constantly while working, so it rides the top row with the
/// model and the path, and its bar is given every column the row has left over.
/// The rate-limit windows are the infrequent detail and go below.
///
/// The session id closes the top row, which puts the bar between two fixed
/// things and makes "the rest of the line" an arithmetic question rather than a
/// guess: render the row with an empty bar, measure it, and the bar is the
/// difference.
///
/// A row with nothing in it is omitted rather than printed blank: early in a
/// session the context and rate-limit fields are all absent, and a stray empty
/// row looks like a fault.
pub fn build(ctx: Ctx, data: payload.Map, home: []const u8, w: usize, now: f64, transcripts: []const u8) []const []const u8 {
    var rows = compose(ctx, data, home, w, false, now);
    // The gauges give way to the percentages they draw rather than letting the
    // row run past the budget, which is the rule the context bar follows too.
    // Measured after composing rather than before, because the context segment
    // relocates onto this row when row one cannot hold it, and that is exactly
    // the case where the row is too long.
    if (w != 0 and rows[1].len > 0 and seg.rowWidth(ctx, rows[1]) > w -| seg.margin) {
        rows = compose(ctx, data, home, w, true, now);
    }

    var joined: std.ArrayList([]const u8) = .empty;
    for (rows) |row| {
        if (row.len == 0) continue;
        const line = std.mem.join(ctx.gpa, seg.sep(ctx), row) catch continue;
        joined.append(ctx.gpa, line) catch {};
    }
    return alignCost(ctx, seg.rail(ctx, joined.items), data, w, transcripts);
}

/// The cost segment at full length and shortened, from one read.
///
/// Both forms come from the same totals because working them out means tailing a
/// file, and doing that twice to decide which of two strings fits would double
/// the only expensive thing on the row.
fn costForms(ctx: Ctx, data: payload.Map, transcripts: []const u8) struct { long: []const u8, short: []const u8 } {
    const totals = usage.sum(ctx, data.str("session_id"), transcripts);
    const figures = pricing.price(ctx, totals) orelse return .{ .long = "", .short = "" };

    // A trailing plus says the session ran a model the table has no rate for,
    // so the figure is a floor rather than a total.
    var total = std.fmt.allocPrint(ctx.gpa, "{s} {s}", .{
        seg.money_glyph,
        pricing.money(ctx.gpa, figures.spent),
    }) catch return .{ .long = "", .short = "" };
    if (!figures.complete) {
        total = std.fmt.allocPrint(ctx.gpa, "{s}+", .{total}) catch total;
    }
    if (figures.saved <= 0) return .{ .long = total, .short = total };

    const full = std.fmt.allocPrint(ctx.gpa, "{s}{s}{s} {s} {s}", .{
        total,
        seg.sep(ctx),
        seg.bullseye,
        seg.dim(ctx, "saved"),
        pricing.money(ctx.gpa, figures.saved),
    }) catch total;
    return .{ .long = full, .short = total };
}

/// Pushes the cost to the right of the meter row, shortening it or dropping it.
///
/// It goes on the meter row rather than the identity row because the identity
/// row has already given its slack to the context bar. With no meter row there
/// is nowhere for it that is not somewhere else's space, so it is dropped.
fn alignCost(ctx: Ctx, lines: []const []const u8, data: payload.Map, w: usize, transcripts: []const u8) []const []const u8 {
    if (lines.len < 2 or w == 0) return lines;
    const last = lines.len - 1;
    const drawn = width.visible(lines[last]);
    if (drawn + seg.margin >= w) return lines;
    const room = w - seg.margin - drawn;

    const forms = costForms(ctx, data, transcripts);
    for ([_][]const u8{ forms.long, forms.short }) |segment| {
        if (segment.len == 0) continue;
        const cost = width.visible(segment);
        if (cost > room) continue;
        const gap = room - cost;
        if (gap < seg.cost_gutter) continue;
        const padded = ctx.gpa.alloc(u8, gap) catch return lines;
        @memset(padded, ' ');
        const out = ctx.gpa.dupe([]const u8, lines) catch return lines;
        out[last] = std.fmt.allocPrint(ctx.gpa, "{s}{s}{s}", .{ lines[last], padded, segment }) catch return lines;
        return out;
    }
    return lines;
}

test "the module compiles and its constants hold" {
    // The row arithmetic is exercised end to end by the parity harness against
    // the Go binary, which is a stronger check than any assertion available
    // here: it compares the bytes both implementations actually print.
    try std.testing.expectEqual(@as(usize, 3), seg.margin);
    try std.testing.expectEqual(@as(usize, 8), seg.bar_min);
}
