#include "state.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <optional>

#include "payload.hpp"

using infobot::payload::Document;
using infobot::payload::Map;
namespace state = infobot::state;

namespace {

// What figures makes of a context window, required to be something.
state::Window measured(const Map& context_window) {
    const auto got = state::figures(context_window);
    REQUIRE(got.has_value());
    return got.value_or(state::Window{});
}

}  // namespace

// COVERS: FR-2.5, FR-2.6 | property
//
// current_usage is the authority and total_input_tokens the fallback, and the
// counts are the INPUT side only: output is never in the context percentage.
TEST_CASE("figures counts the input side only") {
    const Document doc(R"({
        "context_window_size": 200000,
        "current_usage": {
            "input_tokens": 20000,
            "cache_creation_input_tokens": 5000,
            "cache_read_input_tokens": 1000,
            "output_tokens": 99999
        },
        "total_input_tokens": 1
    })");
    constexpr double input_side = 26000;
    const auto got = measured(doc.root());
    CHECK(got.used == input_side);
}

// COVERS: FR-2.6 | positive
TEST_CASE("figures falls back to total_input_tokens") {
    const Document doc(R"({"context_window_size":200000,"total_input_tokens":50000})");
    constexpr double fallback = 50000;
    const auto got = measured(doc.root());
    CHECK(got.used == fallback);
}

// COVERS: FR-2.6 | edge
//
// An empty current_usage is not present in the sense that decides authority:
// Go tests len(current) > 0, so an object with no keys falls back to
// total_input_tokens rather than counting zero.
TEST_CASE("figures treats an empty current_usage as absent") {
    const Document doc(
        R"({"context_window_size":200000,"current_usage":{},"total_input_tokens":50000})");
    constexpr double fallback = 50000;
    const auto got = measured(doc.root());
    CHECK(got.used == fallback);
}

// COVERS: FR-2.7 | property
//
// A percentage without counts derives the counts, and counts without a
// percentage derive the percentage. Printing the literal zero gave
// "0/200k (3% consumed)", which reads as a fault rather than as data.
TEST_CASE("figures derives whichever half is missing") {
    constexpr double derived_used = 50000;
    constexpr double derived_percent = 25;

    const Document from_pct(R"({"context_window_size":200000,"used_percentage":25})");
    const auto pct = measured(from_pct.root());
    CHECK(pct.used == derived_used);

    const Document from_counts(
        R"({"context_window_size":200000,"current_usage":{"input_tokens":50000}})");
    const auto counts = measured(from_counts.root());
    CHECK(counts.percent == derived_percent);
}

// COVERS: FR-1.3 | negative
TEST_CASE("figures refuses a window with no size") {
    CHECK_FALSE(state::figures(Map()).has_value());
    for (const auto* text : {
             "{}",
             R"({"context_window_size":0})",
             R"({"used_percentage":40})",
         }) {
        CAPTURE(text);
        const Document doc(text);
        CHECK_FALSE(state::figures(doc.root()).has_value());
    }
}

// COVERS: FR-1.11h | edge
//
// context_percent is neither floored nor capped, so a reader sees an over-full
// window as over-full. The clamp on context_remaining belongs to the state file,
// which is not what figures decides.
TEST_CASE("an over-full window keeps its percent") {
    const Document doc(R"({
        "context_window_size": 100,
        "used_percentage": 130,
        "current_usage": {"input_tokens": 130}
    })");
    constexpr double over = 130;
    const auto got = measured(doc.root());
    CHECK(got.percent == over);
    CHECK(got.used == over);
}
