//! What a session would have cost through the API.
//!
//! I am on a subscription, so nothing here is a bill. It is the counterfactual:
//! what the same tokens would have come to had they gone through the API, which
//! is what makes the cache worth anything visible.
//!
//! THE RATES ARE A CACHED COPY AND THEY DRIFT. A status line that runs on every
//! event cannot make a network call to ask, so they are read from a table on
//! disk and refreshed by something else.
//!
//! ~/.config/infobot/pricing.json is that table and wins when it is there.
//! Config rather than state, because the offsets under ~/.local/state/infobot
//! are disposable machine bookkeeping and this is a file worth editing by hand.
//!
//! The table below is the seed and the fallback, so a fresh clone renders with
//! no config file and no network. A model absent from the table is priced at
//! nothing and reported as tokens: a cost computed from a guessed rate is worse
//! than no cost.

const std = @import("std");
const num = @import("num.zig");
const payload = @import("payload.zig");
const usage = @import("usage.zig");
const Ctx = @import("ctx.zig").Ctx;

pub const taken = "2026-08-28";
pub const source = "https://platform.claude.com/docs/en/about-claude/pricing";

/// Input and output dollars per million tokens, read from `source`.
pub const Rate = struct { in: f64, out: f64 };

const Seed = struct { model: []const u8, rate: Rate };

const seed_rates = [_]Seed{
    .{ .model = "claude-fable-5", .rate = .{ .in = 10.00, .out = 50.00 } },
    .{ .model = "claude-mythos-5", .rate = .{ .in = 10.00, .out = 50.00 } },
    .{ .model = "claude-opus-5", .rate = .{ .in = 5.00, .out = 25.00 } },
    .{ .model = "claude-opus-4-8", .rate = .{ .in = 5.00, .out = 25.00 } },
    .{ .model = "claude-opus-4-7", .rate = .{ .in = 5.00, .out = 25.00 } },
    .{ .model = "claude-opus-4-6", .rate = .{ .in = 5.00, .out = 25.00 } },
    .{ .model = "claude-opus-4-5", .rate = .{ .in = 5.00, .out = 25.00 } },
    .{ .model = "claude-opus-4-1", .rate = .{ .in = 15.00, .out = 75.00 } },
    .{ .model = "claude-opus-4-0", .rate = .{ .in = 15.00, .out = 75.00 } },
    .{ .model = "claude-sonnet-5", .rate = .{ .in = 2.00, .out = 10.00 } },
    .{ .model = "claude-sonnet-4-6", .rate = .{ .in = 3.00, .out = 15.00 } },
    .{ .model = "claude-sonnet-4-5", .rate = .{ .in = 3.00, .out = 15.00 } },
    .{ .model = "claude-sonnet-4-0", .rate = .{ .in = 3.00, .out = 15.00 } },
    .{ .model = "claude-haiku-4-5", .rate = .{ .in = 1.00, .out = 5.00 } },
    .{ .model = "claude-3-5-haiku-20241022", .rate = .{ .in = 0.80, .out = 4.00 } },
};

/// Multipliers on the input rate, uniform across models: the per-model cache
/// columns at `source` are these applied. A cache read is CHARGED at a tenth,
/// 90% off rather than free, and on a long session it is the largest single
/// line. A write costs more than a fresh input token, which is why the two are
/// priced apart rather than lumped together as "cache".
pub const seed_cache_read = 0.1;

const seed_cache_write = [_]struct { field: []const u8, multiplier: f64 }{
    .{ .field = "ephemeral_5m_input_tokens", .multiplier = 1.25 },
    .{ .field = "ephemeral_1h_input_tokens", .multiplier = 2.0 },
};

/// The rate table, from disk or from the seed.
pub const Table = struct {
    rates: std.StringHashMapUnmanaged(Rate) = .empty,
    cache_read: f64 = seed_cache_read,
    cache_write: std.StringHashMapUnmanaged(f64) = .empty,
};

fn seedTable(ctx: Ctx) Table {
    var table: Table = .{};
    for (seed_rates) |entry| table.rates.put(ctx.gpa, entry.model, entry.rate) catch {};
    for (seed_cache_write) |entry| table.cache_write.put(ctx.gpa, entry.field, entry.multiplier) catch {};
    return table;
}

/// Where the rates on disk live.
pub fn tablePath(ctx: Ctx) ?[]u8 {
    const configured = ctx.getenv("XDG_CONFIG_HOME");
    if (configured.len != 0) {
        return std.fs.path.join(ctx.gpa, &.{ configured, "infobot", "pricing.json" }) catch null;
    }
    const home = ctx.getenv("HOME");
    if (home.len == 0) return null;
    return std.fs.path.join(ctx.gpa, &.{ home, ".config", "infobot", "pricing.json" }) catch null;
}

/// The rates on disk, or the seed when there are none to be had.
///
/// Anything malformed falls back rather than failing. A status line that fails
/// shows nothing at all, and a stale rate is a smaller wrong than a blank row.
pub fn load(ctx: Ctx) Table {
    const path = tablePath(ctx) orelse return seedTable(ctx);
    const file = ctx.dir().openFile(ctx.io, path, .{}) catch return seedTable(ctx);
    defer file.close(ctx.io);

    const size = file.length(ctx.io) catch return seedTable(ctx);
    if (size == 0 or size > 4 * 1024 * 1024) return seedTable(ctx);
    const raw = ctx.gpa.alloc(u8, @intCast(size)) catch return seedTable(ctx);
    const got = file.readPositionalAll(ctx.io, raw, 0) catch return seedTable(ctx);

    const parsed = std.json.parseFromSlice(std.json.Value, ctx.gpa, raw[0..got], .{}) catch return seedTable(ctx);
    const wire = payload.Map.from(parsed.value);

    var table: Table = .{};
    table.rates = readRates(ctx, wire.obj("rates"));
    if (table.rates.count() == 0) return seedTable(ctx);

    table.cache_read = wire.num("cache_read") orelse seed_cache_read;
    table.cache_write = readMultipliers(ctx, wire.obj("cache_write"));
    if (table.cache_write.count() == 0) {
        for (seed_cache_write) |entry| table.cache_write.put(ctx.gpa, entry.field, entry.multiplier) catch {};
    }
    return table;
}

/// Rates arrive as [input, output] pairs, which is the shape the seed is
/// written in and the shape a person editing the file would copy.
fn readRates(ctx: Ctx, rates: payload.Map) std.StringHashMapUnmanaged(Rate) {
    var out: std.StringHashMapUnmanaged(Rate) = .empty;
    const object = switch (rates.value orelse return out) {
        .object => |o| o,
        else => return out,
    };
    var it = object.iterator();
    while (it.next()) |entry| {
        const pair = switch (entry.value_ptr.*) {
            .array => |a| a.items,
            else => continue,
        };
        if (pair.len < 2) continue;
        const in = payload.asNumber(pair[0]) orelse continue;
        const cost = payload.asNumber(pair[1]) orelse continue;
        out.put(ctx.gpa, entry.key_ptr.*, .{ .in = in, .out = cost }) catch {};
    }
    return out;
}

fn readMultipliers(ctx: Ctx, writes: payload.Map) std.StringHashMapUnmanaged(f64) {
    var out: std.StringHashMapUnmanaged(f64) = .empty;
    const object = switch (writes.value orelse return out) {
        .object => |o| o,
        else => return out,
    };
    var it = object.iterator();
    while (it.next()) |entry| {
        const n = payload.asNumber(entry.value_ptr.*) orelse continue;
        out.put(ctx.gpa, entry.key_ptr.*, n) catch {};
    }
    return out;
}

/// The outcome of pricing a session's totals.
pub const Priced = struct {
    spent: f64,
    saved: f64,
    complete: bool,
};

/// Dollars spent, dollars the cache took off, and whether that is all of it.
/// Null when nothing at all could be priced.
///
/// totals is keyed by model, each holding the token counts as the transcript
/// records them, and each is charged at its own rate: a session that ran work on
/// several models is the sum of them, not an average.
///
/// A model with no rate is left out and the total is flagged incomplete rather
/// than abandoned, so one unknown model costs the exactness of the figure and
/// not the figure.
///
/// The saving is every cached token, read or written, charged at the plain input
/// rate instead: what the session would have cost with no caching, less what it
/// did cost.
pub fn price(ctx: Ctx, totals: usage.Totals) ?Priced {
    if (totals.count() == 0) return null;
    const table = load(ctx);

    var spent: f64 = 0;
    var uncached: f64 = 0;
    var cached: f64 = 0;
    var complete = true;

    var it = totals.iterator();
    while (it.next()) |entry| {
        const rate = table.rates.get(entry.key_ptr.*) orelse {
            complete = false;
            continue;
        };
        const counts = entry.value_ptr.*;
        spent += (counts.get("input_tokens") orelse 0) / 1e6 * rate.in;
        spent += (counts.get("output_tokens") orelse 0) / 1e6 * rate.out;

        var tokens: f64 = 0;
        var w = table.cache_write.iterator();
        while (w.next()) |write| {
            const n = counts.get(write.key_ptr.*) orelse 0;
            tokens += n;
            cached += n / 1e6 * rate.in * write.value_ptr.*;
            spent += n / 1e6 * rate.in * write.value_ptr.*;
        }
        const reads = counts.get("cache_read_input_tokens") orelse 0;
        tokens += reads;
        cached += reads / 1e6 * rate.in * table.cache_read;
        spent += reads / 1e6 * rate.in * table.cache_read;
        uncached += tokens / 1e6 * rate.in;
    }
    if (spent == 0) return null;
    return .{ .spent = spent, .saved = uncached - cached, .complete = complete };
}

/// Dollars at the precision the number deserves rather than always two decimals.
pub fn money(gpa: std.mem.Allocator, dollars: f64) []const u8 {
    if (dollars >= 1000) return std.fmt.allocPrint(gpa, "${d:.1}k", .{dollars / 1000}) catch "";
    if (dollars >= 100) return std.fmt.allocPrint(gpa, "${d:.0}", .{dollars}) catch "";
    return std.fmt.allocPrint(gpa, "${d:.2}", .{dollars}) catch "";
}

test "money picks its precision by magnitude" {
    var scratch = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer scratch.deinit();
    const gpa = scratch.allocator();
    for ([_]struct { in: f64, want: []const u8 }{
        .{ .in = 0, .want = "$0.00" },
        .{ .in = 4.5, .want = "$4.50" },
        .{ .in = 99.994, .want = "$99.99" },
        .{ .in = 100, .want = "$100" },
        .{ .in = 814.3, .want = "$814" },
        .{ .in = 1000, .want = "$1.0k" },
        .{ .in = 4523, .want = "$4.5k" },
    }) |c| {
        try std.testing.expectEqualStrings(c.want, money(gpa, c.in));
    }
}
