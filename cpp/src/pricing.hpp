// What a session would have cost through the API.
//
// On a subscription nothing here is a bill. It is the counterfactual: what the
// same tokens would have come to through the API, which is what makes the
// cache worth anything visible.
//
// THE RATES ARE A CACHED COPY AND THEY DRIFT. A status line that runs on every
// event makes no network call, so they are read from
// ~/.config/infobot/pricing.json and refreshed by something else. The table in
// pricing.cpp is the seed and the fallback, so a fresh clone renders with no
// config file.
//
// A model absent from the table is left out and flags the figure incomplete. A
// cost computed from a guessed rate is worse than no cost.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "gojson.hpp"
#include "usage.hpp"

namespace infobot::pricing {

inline constexpr std::string_view taken = "2026-08-28";
inline constexpr std::string_view source =
    "https://platform.claude.com/docs/en/about-claude/pricing";

// Input and output dollars per million tokens.
struct Rate {
    double in = 0;
    double out = 0;
};

struct Table {
    std::string taken;
    std::string source;
    gojson::Map<Rate> rates;
    std::optional<double> cache_read;
    gojson::Map<double> cache_write;

    // The cache read multiplier, falling back to the seed's tenth.
    [[nodiscard]] double read() const;

    // The cache write multipliers, falling back to the seed's.
    [[nodiscard]] const gojson::Map<double>& writes() const;
};

[[nodiscard]] Table seed();

// The table at path, or the seed when there is none to be had. Missing,
// malformed and carrying no rates all fall back: a stale rate is a smaller
// wrong than a blank row.
[[nodiscard]] Table load(const std::string& path);

struct Priced {
    double spent = 0;
    double saved = 0;
    bool complete = true;
};

// Dollars spent, dollars the cache took off, and whether that is all of it.
// nullopt when nothing at all could be priced.
//
// Each model is charged at its own rate, so a session that ran on several is
// the sum of them. The saving is every cached token charged at the plain input
// rate instead, less what those tokens did cost.
[[nodiscard]] std::optional<Priced> price(const usage::Totals& totals,
                                          const Table& table);

// Dollars at the precision the number deserves.
[[nodiscard]] std::string money(double dollars);

}  // namespace infobot::pricing
