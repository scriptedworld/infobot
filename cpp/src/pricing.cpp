#include "pricing.hpp"

#include <simdjson.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "files.hpp"
#include "gojson.hpp"
#include "usage.hpp"

namespace infobot::pricing {

namespace {

// A cache read is CHARGED at a tenth of input, 90% off rather than free.
constexpr double seed_cache_read = 0.1;
constexpr double per_million = 1e6;
constexpr double thousand = 1000;
constexpr double hundred = 100;

const gojson::Map<double>& seed_cache_write() {
    static const gojson::Map<double> writes = {
        {"ephemeral_5m_input_tokens", 1.25},
        {"ephemeral_1h_input_tokens", 2.0},
    };
    return writes;
}

const gojson::Map<Rate>& seed_rates() {
    static const gojson::Map<Rate> rates = {
        {"claude-fable-5", {.in = 10.00, .out = 50.00}},
        {"claude-mythos-5", {.in = 10.00, .out = 50.00}},
        {"claude-opus-5", {.in = 5.00, .out = 25.00}},
        {"claude-opus-4-8", {.in = 5.00, .out = 25.00}},
        {"claude-opus-4-7", {.in = 5.00, .out = 25.00}},
        {"claude-opus-4-6", {.in = 5.00, .out = 25.00}},
        {"claude-opus-4-5", {.in = 5.00, .out = 25.00}},
        {"claude-opus-4-1", {.in = 15.00, .out = 75.00}},
        {"claude-opus-4-0", {.in = 15.00, .out = 75.00}},
        {"claude-sonnet-5", {.in = 2.00, .out = 10.00}},
        {"claude-sonnet-4-6", {.in = 3.00, .out = 15.00}},
        {"claude-sonnet-4-5", {.in = 3.00, .out = 15.00}},
        {"claude-sonnet-4-0", {.in = 3.00, .out = 15.00}},
        {"claude-haiku-4-5", {.in = 1.00, .out = 5.00}},
        {"claude-3-5-haiku-20241022", {.in = 0.80, .out = 4.00}},
    };
    return rates;
}

// The file as Go's wire struct reads it: rates arrive as [input, output] pairs.
struct Wire {
    std::string taken;
    std::string source;
    std::optional<gojson::Map<std::optional<std::vector<double>>>> rates;
    std::optional<double> cache_read;
    std::optional<gojson::Map<double>> cache_write;
};

void decode_wire(gojson::Decode& decode, simdjson::dom::object top, Wire& wire) {
    decode.fields(top, "taken", [&](simdjson::dom::element v) {
        decode.string_into(v, wire.taken);
    });
    decode.fields(top, "source", [&](simdjson::dom::element v) {
        decode.string_into(v, wire.source);
    });
    decode.fields(top, "rates", [&](simdjson::dom::element v) {
        decode.map_into(
            v,
            wire.rates,
            [](gojson::Decode& d,
               simdjson::dom::element pair,
               std::optional<std::vector<double>>& out) {
                d.slice_into(
                    pair,
                    out,
                    [](gojson::Decode& e, simdjson::dom::element n, double& number) {
                        e.float_into(n, number);
                    });
            });
    });
    decode.fields(top, "cache_read", [&](simdjson::dom::element v) {
        decode.optional_float_into(v, wire.cache_read);
    });
    decode.fields(top, "cache_write", [&](simdjson::dom::element v) {
        decode.map_into(
            v,
            wire.cache_write,
            [](gojson::Decode& d, simdjson::dom::element n, double& number) {
                d.float_into(n, number);
            });
    });
}

}  // namespace

double Table::read() const { return cache_read.value_or(seed_cache_read); }

const gojson::Map<double>& Table::writes() const {
    return cache_write.empty() ? seed_cache_write() : cache_write;
}

Table seed() {
    return {.taken = std::string(taken),
            .source = std::string(source),
            .rates = seed_rates(),
            .cache_read = std::nullopt,
            .cache_write = seed_cache_write()};
}

Table load(const std::string& path) {
    if (path.empty()) {
        return seed();
    }
    const auto raw = files::read(path);
    if (!raw) {
        return seed();
    }
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (parser.parse(*raw).get(root) != simdjson::SUCCESS) {
        return seed();
    }
    gojson::Decode decode;
    Wire wire;
    decode.as_struct(
        root, [&](simdjson::dom::object top) { decode_wire(decode, top, wire); });
    if (!decode.ok() || !wire.rates || wire.rates->empty()) {
        return seed();
    }
    Table table{.taken = wire.taken,
                .source = wire.source,
                .rates = {},
                .cache_read = wire.cache_read,
                .cache_write = wire.cache_write.value_or(gojson::Map<double>{})};
    for (const auto& [model, pair] : *wire.rates) {
        if (pair && pair->size() >= 2) {
            table.rates.insert_or_assign(model,
                                         Rate{.in = (*pair)[0], .out = (*pair)[1]});
        }
    }
    if (table.rates.empty()) {
        return seed();
    }
    return table;
}

std::optional<Priced> price(const usage::Totals& totals, const Table& table) {
    if (totals.empty()) {
        return std::nullopt;
    }
    const auto count = [](const gojson::Map<double>& counts, std::string_view field) {
        const auto found = counts.find(field);
        return found == counts.end() ? 0.0 : found->second;
    };
    Priced priced;
    double uncached = 0;
    double cached = 0;
    for (const auto& [model, counts] : totals) {
        const auto rate = table.rates.find(model);
        if (rate == table.rates.end()) {
            priced.complete = false;
            continue;
        }
        const double in = rate->second.in;
        priced.spent += count(counts, "input_tokens") / per_million * in;
        priced.spent += count(counts, "output_tokens") / per_million * rate->second.out;

        double tokens = 0;
        for (const auto& [field, multiplier] : table.writes()) {
            const double written = count(counts, field);
            tokens += written;
            cached += written / per_million * in * multiplier;
            priced.spent += written / per_million * in * multiplier;
        }
        const double reads = count(counts, "cache_read_input_tokens");
        tokens += reads;
        cached += reads / per_million * in * table.read();
        priced.spent += reads / per_million * in * table.read();
        uncached += tokens / per_million * in;
    }
    if (priced.spent == 0) {
        return std::nullopt;
    }
    priced.saved = uncached - cached;
    return priced;
}

std::string money(double dollars) {
    if (dollars >= thousand) {
        return std::format("${:.1f}k", dollars / thousand);
    }
    if (dollars >= hundred) {
        return std::format("${:.0f}", dollars);
    }
    return std::format("${:.2f}", dollars);
}

}  // namespace infobot::pricing
