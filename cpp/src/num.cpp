#include "num.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <system_error>

namespace infobot::num {

// nearbyint rounds in the current mode, and the default mode is to nearest with
// ties to even. Nothing in this program changes the mode.
double round(double x) { return std::nearbyint(x); }

int round_int(double x) { return static_cast<int>(round(x)); }

// Formatting and parsing back is the accurate route, as it is in Go:
// std::to_chars rounds the exact binary value, where scaling by a power of ten
// and rounding introduces an error of its own at exactly the ties this exists
// to get right.
double round_to(double x, int places) {
    if (!std::isfinite(x)) {
        return x;
    }
    // 309 integer digits is the most a finite double has, so this holds any
    // places a caller passes. A failure returns x unchanged, as Go's does.
    constexpr std::size_t longest = 512;
    std::array<char, longest> text{};
    const auto written = std::to_chars(
        text.data(), text.data() + text.size(), x, std::chars_format::fixed, places);
    if (written.ec != std::errc{}) {
        return x;
    }
    double rounded = x;
    std::from_chars(text.data(), written.ptr, rounded);
    return rounded;
}

double clamp(double x, double lo, double hi) {
    if (std::isnan(x)) {
        return x;
    }
    return std::max(lo, std::min(hi, x));
}

}  // namespace infobot::num
