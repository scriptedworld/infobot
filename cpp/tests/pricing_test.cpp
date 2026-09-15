#include "pricing.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "support.hpp"
#include "usage.hpp"

namespace pricing = infobot::pricing;
using infobot::test::Scratch;
using infobot::usage::Totals;

namespace {

constexpr double million = 1e6;

// One model's counts, as the Go suite's totals helper builds them.
Totals totals(const std::string& model,
              std::initializer_list<std::pair<const std::string, double>> fields) {
    return Totals{{model, {fields}}};
}

// What price makes of counts, required to be something.
pricing::Priced priced(const Totals& counts, const pricing::Table& table) {
    const auto got = pricing::price(counts, table);
    REQUIRE(got.has_value());
    return got.value_or(pricing::Priced{});
}

// The table a file holding body loads as.
pricing::Table load_body(std::string_view body) {
    const Scratch dir;
    return pricing::load(dir.write("infobot/pricing.json", body));
}

// Whether a table is the seed, by the two things the Go suite checks.
bool is_seed(const pricing::Table& table) {
    return table.taken == pricing::taken && table.rates.contains("claude-opus-5");
}

}  // namespace

// COVERS: FR-8.17 | property
TEST_CASE("money prints the precision the number deserves") {
    struct Case {
        double in;
        std::string_view want;
    };
    for (const auto c : {
             Case{.in = 0, .want = "$0.00"},
             Case{.in = 1.5, .want = "$1.50"},
             Case{.in = 99.994, .want = "$99.99"},
             Case{.in = 100, .want = "$100"},
             Case{.in = 263.4, .want = "$263"},
             Case{.in = 999, .want = "$999"},
             Case{.in = 1000, .want = "$1.0k"},
             // Ties break to even here too, so 1.25k is 1.2k and 1.35k is 1.4k.
             // Measured against the Python this replaced rather than assumed.
             Case{.in = 1250, .want = "$1.2k"},
             Case{.in = 1350, .want = "$1.4k"},
         }) {
        CAPTURE(c.in);
        CHECK(pricing::money(c.in) == c.want);
    }
}

// COVERS: FR-8.11 | positive
TEST_CASE("price charges each model at its own rate") {
    const Totals both = {
        {"claude-opus-5", {{"input_tokens", million}}},
        {"claude-haiku-4-5", {{"input_tokens", million}}},
    };
    const auto got = priced(both, pricing::seed());
    // The sum of them, not an average: opus 5.00 plus haiku 1.00.
    constexpr double sum = 6.0;
    CHECK(got.spent == sum);
    CHECK(got.complete);
}

// COVERS: FR-8.12 | negative
//
// One unknown model costs the exactness of the figure and not the figure.
TEST_CASE("price flags an unknown model without abandoning the total") {
    const Totals mixed = {
        {"claude-opus-5", {{"input_tokens", million}}},
        {"some-model-nobody-priced", {{"input_tokens", million}}},
    };
    const auto got = priced(mixed, pricing::seed());
    constexpr double priced_alone = 5.0;
    CHECK(got.spent == priced_alone);
    CHECK_FALSE(got.complete);
}

// COVERS: FR-8.12 | edge
TEST_CASE("price returns nothing when nothing could be priced") {
    CHECK_FALSE(pricing::price(Totals{}, pricing::seed()).has_value());
    CHECK_FALSE(pricing::price(totals("unknown-model", {{"input_tokens", million}}),
                               pricing::seed())
                    .has_value());
}

// COVERS: FR-8.12 | edge
//
// A priced model whose counts come to nothing is nothing priced, so no segment
// appears rather than a figure of zero dollars.
TEST_CASE("price returns nothing for a known model with no counted tokens") {
    CHECK_FALSE(pricing::price(totals("claude-opus-5", {{"unrelated", million}}),
                               pricing::seed())
                    .has_value());
}

// COVERS: FR-8.13 | property
//
// A cache read is CHARGED, at a tenth of input. 90% off is not free.
TEST_CASE("cache reads are charged at a tenth") {
    const auto got =
        priced(totals("claude-opus-5", {{"cache_read_input_tokens", million}}),
               pricing::seed());
    constexpr double tenth_of_five = 0.5;
    CHECK(got.spent == tenth_of_five);
}

// COVERS: FR-8.14 | property
//
// The saving is those tokens charged at the plain input rate instead, less what
// they did cost: 5.00 uncached against 0.50 charged.
TEST_CASE("saving is the uncached counterfactual") {
    const auto got =
        priced(totals("claude-opus-5", {{"cache_read_input_tokens", million}}),
               pricing::seed());
    constexpr double saved = 4.5;
    CHECK(got.saved == saved);
}

// COVERS: FR-8.13 | property
//
// A write costs MORE than a fresh input token, which is why the two are priced
// apart rather than lumped together as "cache".
TEST_CASE("cache writes cost more than fresh input") {
    const auto write =
        priced(totals("claude-opus-5", {{"ephemeral_1h_input_tokens", million}}),
               pricing::seed());
    const auto fresh =
        priced(totals("claude-opus-5", {{"input_tokens", million}}), pricing::seed());
    CHECK(write.spent > fresh.spent);
    // Its saving is negative: it cost double what the plain rate would have.
    CHECK(write.saved < 0);
}

// COVERS: FR-8.15 | positive
TEST_CASE("load prefers the table on disk") {
    const auto got =
        load_body(R"({"taken":"2099-01-01","rates":{"only-model":[7.0,9.0]}})");
    constexpr double in = 7.0;
    constexpr double out = 9.0;
    CHECK(got.taken == "2099-01-01");
    REQUIRE(got.rates.contains("only-model"));
    CHECK(got.rates.at("only-model").in == in);
    CHECK(got.rates.at("only-model").out == out);
    CHECK_FALSE(got.rates.contains("claude-opus-5"));
}

// COVERS: FR-8.16 | negative
//
// Missing, malformed and carrying no rates all fall back rather than failing. A
// stale rate is a smaller wrong than a blank row.
TEST_CASE("load falls back to the seed") {
    const Scratch dir;
    CHECK(is_seed(pricing::load(dir.file("infobot/pricing.json"))));
    // No path at all is what config_file reports with no home to be had.
    CHECK(is_seed(pricing::load("")));
    for (const auto* body : {"{not json", R"({"taken":"2099-01-01","rates":{}})"}) {
        CAPTURE(body);
        CHECK(is_seed(load_body(body)));
    }
}

// COVERS: FR-8.16 | negative
//
// Go's decoder reports a value of the wrong type as an error and Load tests that
// error, so ONE wrong type anywhere in the file rejects it whole, including in a
// field the table would otherwise have taken nothing from.
TEST_CASE("load rejects a table with a wrong type anywhere") {
    for (const auto* body : {
             R"([1,2])",
             R"({"taken":5,"rates":{"m":[1,2]}})",
             R"({"source":[],"rates":{"m":[1,2]}})",
             R"({"rates":[[1,2]]})",
             R"({"rates":{"m":"cheap"}})",
             R"({"rates":{"m":[1,"two"]}})",
             R"({"rates":{"m":[1,2]},"cache_read":"half"})",
             R"({"rates":{"m":[1,2]},"cache_write":5})",
             R"({"rates":{"m":[1,2]},"cache_write":{"ephemeral_5m_input_tokens":"x"}})",
         }) {
        CAPTURE(body);
        CHECK(is_seed(load_body(body)));
    }
}

// COVERS: FR-8.16 | edge
//
// A rate pair shorter than two is dropped rather than read past its end, and a
// table whose every pair is dropped carries no rates, so it falls back.
TEST_CASE("load drops a rate pair shorter than two") {
    const auto some = load_body(R"({"rates":{"short":[1],"none":null,"m":[1,2]}})");
    CHECK_FALSE(is_seed(some));
    CHECK_FALSE(some.rates.contains("short"));
    CHECK_FALSE(some.rates.contains("none"));
    CHECK(some.rates.contains("m"));

    CHECK(is_seed(load_body(R"({"rates":{"short":[1],"none":null}})")));
}

// COVERS: FR-8.16 | edge
//
// A null document and null values are not type errors to Go's decoder. A null
// document carries no rates and falls back; a null cache_read reads as unset.
TEST_CASE("load reads null as unset rather than as an error") {
    CHECK(is_seed(load_body("null")));
    const auto table =
        load_body(R"({"rates":{"m":[1,2]},"cache_read":null,"taken":null})");
    CHECK_FALSE(is_seed(table));
    CHECK_FALSE(table.cache_read.has_value());
    constexpr double seed_read = 0.1;
    CHECK(pricing::cache_read(table) == seed_read);
}

// COVERS: FR-8.23 | property
TEST_CASE("cache multipliers are configurable") {
    // A cache read priced at half rather than at a tenth, with no code change.
    const auto table = load_body(R"({"rates":{"m":[10.0,20.0]},"cache_read":0.5})");
    const auto got = priced(totals("m", {{"cache_read_input_tokens", million}}), table);
    constexpr double at_half = 5.0;
    CHECK(got.spent == at_half);
}

// COVERS: FR-8.23 | property
//
// A configured cache_write REPLACES the seed's multipliers rather than merging
// into them, as Go's Writes returns the table's own map whole. A write field
// the file leaves out is then priced at nothing.
TEST_CASE("a configured cache_write overrides the seed") {
    const auto table = load_body(
        R"({"rates":{"m":[10.0,20.0]},"cache_write":{"ephemeral_5m_input_tokens":3}})");
    constexpr double tripled = 30.0;
    const auto five =
        priced(totals("m", {{"ephemeral_5m_input_tokens", million}}), table);
    CHECK(five.spent == tripled);
    CHECK_FALSE(
        pricing::price(totals("m", {{"ephemeral_1h_input_tokens", million}}), table)
            .has_value());
}

// COVERS: FR-8.23 | positive
//
// A file naming no multipliers keeps the seed's for both reads and writes.
TEST_CASE("a table with no multipliers keeps the seed's") {
    const auto table = load_body(R"({"rates":{"m":[10.0,20.0]}})");
    constexpr double doubled = 20.0;
    const auto got =
        priced(totals("m", {{"ephemeral_1h_input_tokens", million}}), table);
    CHECK(got.spent == doubled);
    CHECK(&pricing::cache_writes(table) != &table.cache_write);
}
