// Package num carries the rounding the port has to preserve exactly.
//
// THIS IS NOT PEDANTRY. Python's round() breaks a tie to the EVEN neighbour and
// Go's math.Round breaks it away from zero, so round(2.5) is 2 in one and 3 in
// the other. Both appear where a tie is reachable: the bar's filled-cell count
// is round(pct/100*cells), which ties whenever a percentage lands mid-cell, and
// every colour channel is round()ed out of an interpolation. A bar one cell
// long in the wrong direction is a visible difference against the golden
// corpus and would read as a rendering bug rather than as a rounding mode.
package num

import (
	"math"
	"strconv"
)

// Round returns x rounded to an integer, breaking ties to even, which is what
// Python's one-argument round() does.
func Round(x float64) float64 {
	return math.RoundToEven(x)
}

// RoundInt is Round as an int, for counts and colour channels.
func RoundInt(x float64) int {
	return int(Round(x))
}

// RoundTo returns x rounded to the given number of decimal places, matching
// Python's two-argument round(). Formatting and parsing back is the accurate
// route: scaling by a power of ten and rounding introduces an error of its own
// at exactly the ties this exists to get right.
func RoundTo(x float64, places int) float64 {
	if math.IsNaN(x) || math.IsInf(x, 0) {
		return x
	}
	rounded, err := strconv.ParseFloat(strconv.FormatFloat(x, 'f', places, 64), 64)
	if err != nil {
		return x
	}
	return rounded
}

// Clamp holds x between lo and hi.
func Clamp(x, lo, hi float64) float64 {
	return math.Max(lo, math.Min(hi, x))
}
