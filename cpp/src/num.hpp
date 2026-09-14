// The rounding the port has to preserve exactly.
//
// Python's round() breaks a tie to the EVEN neighbour, and so does the Go port.
// std::round breaks it away from zero. A tie is reachable twice: the bar's
// filled-cell count is round(pct/100*cells), and every colour channel is
// rounded out of an interpolation. Either one wrong by one is a visible
// difference against the golden corpus.
#pragma once

namespace infobot::num {

// x rounded to an integer, ties to even.
[[nodiscard]] double round(double x);

// round as an int, for counts and colour channels.
[[nodiscard]] int round_int(double x);

// x rounded to `places` decimal places, as Python's two-argument round().
// NaN and infinity come back unchanged.
[[nodiscard]] double round_to(double x, int places);

// x held between lo and hi. NaN comes back unchanged, as Go's math.Max and
// math.Min return it, rather than being clamped to a bound that looks measured.
[[nodiscard]] double clamp(double x, double lo, double hi);

}  // namespace infobot::num
