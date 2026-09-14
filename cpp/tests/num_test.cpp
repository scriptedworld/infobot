#include "num.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>
#include <limits>

namespace num = infobot::num;

// COVERS: FR-6.3 | property
//
// Ties break to EVEN, which is what Python's round() did and what the bar's
// filled-cell count depends on. Breaking away from zero instead moves a bar by
// one cell at every percentage that lands exactly mid-cell.
TEST_CASE("round breaks ties to even") {
    struct Case {
        double in;
        double want;
    };
    for (const auto c : {
             Case{.in = 0.5, .want = 0},
             Case{.in = 1.5, .want = 2},
             Case{.in = 2.5, .want = 2},
             Case{.in = 3.5, .want = 4},
             Case{.in = 4.5, .want = 4},
             Case{.in = -0.5, .want = 0},
             Case{.in = -1.5, .want = -2},
             Case{.in = -2.5, .want = -2},
             Case{.in = 1.4, .want = 1},
             Case{.in = 1.6, .want = 2},
         }) {
        CAPTURE(c.in);
        CHECK(num::round(c.in) == c.want);
    }
}

// COVERS: FR-1.11h | property
TEST_CASE("round_to rounds to places") {
    struct Case {
        double in;
        int places;
        double want;
    };
    for (const auto c : {
             Case{.in = 48.24, .places = 1, .want = 48.2},
             Case{.in = 48.26, .places = 1, .want = 48.3},
             Case{.in = 11.0, .places = 1, .want = 11.0},
             Case{.in = 0.0, .places = 1, .want = 0.0},
             Case{.in = 99.95, .places = 1, .want = 100.0},
         }) {
        CAPTURE(c.in);
        CHECK(num::round_to(c.in, c.places) == c.want);
    }
}

// COVERS: FR-6.3 | property
TEST_CASE("round_int carries the same tie rule") {
    struct Case {
        double in;
        int want;
    };
    for (const auto c : {
             Case{.in = 0.5, .want = 0},
             Case{.in = 1.5, .want = 2},
             Case{.in = 2.5, .want = 2},
             Case{.in = 7.5, .want = 8},
             Case{.in = -1.5, .want = -2},
         }) {
        CAPTURE(c.in);
        CHECK(num::round_int(c.in) == c.want);
    }
}

// COVERS: FR-1.11h | edge
//
// A percentage is neither floored nor capped, so a division that has gone wrong
// reaches here rather than being caught upstream. It comes back unchanged
// rather than becoming a number that looks measured.
TEST_CASE("round_to passes NaN and infinity through") {
    CHECK(std::isnan(num::round_to(std::numeric_limits<double>::quiet_NaN(), 1)));
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(num::round_to(inf, 1) == inf);
    CHECK(num::round_to(-inf, 1) == -inf);
}

// COVERS: FR-1.11h | edge
//
// The format-and-parse route has a failure path, and it returns x unchanged as
// Go's does. A precision past what the buffer holds is the one way to reach it.
TEST_CASE("round_to returns x when the formatted form does not fit") {
    const double big = std::numeric_limits<double>::max();
    CHECK(num::round_to(big, 400) == big);
}

// COVERS: FR-6.8 | edge
TEST_CASE("clamp holds between bounds") {
    struct Case {
        double in;
        double want;
    };
    for (const auto c : {
             Case{.in = -5, .want = 0},
             Case{.in = 150, .want = 100},
             Case{.in = 42, .want = 42},
             Case{.in = 0, .want = 0},
             Case{.in = 100, .want = 100},
         }) {
        CAPTURE(c.in);
        CHECK(num::clamp(c.in, 0, 100) == c.want);
    }
}

// COVERS: FR-6.8 | edge
//
// Go's math.Max and math.Min return NaN when either side is NaN. std::min and
// std::max would return a bound instead, which draws a bar for a percentage
// nothing measured.
TEST_CASE("clamp passes NaN through") {
    CHECK(std::isnan(num::clamp(std::numeric_limits<double>::quiet_NaN(), 0, 100)));
}
