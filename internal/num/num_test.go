package num_test

import (
	"math"
	"testing"

	"github.com/scriptedworld/infobot/internal/num"
)

// COVERS: FR-6.3 | property
//
// Ties break to EVEN, which is what Python's round() did and what the bar's
// filled-cell count depends on. Breaking away from zero instead moves a bar by
// one cell at every percentage that lands exactly mid-cell.
func TestRoundBreaksTiesToEven(t *testing.T) {
	for _, c := range []struct {
		in   float64
		want float64
	}{
		{0.5, 0},
		{1.5, 2},
		{2.5, 2},
		{3.5, 4},
		{4.5, 4},
		{-0.5, 0},
		{-1.5, -2},
		{-2.5, -2},
		{1.4, 1},
		{1.6, 2},
	} {
		if got := num.Round(c.in); got != c.want {
			t.Errorf("Round(%v) = %v, want %v", c.in, got, c.want)
		}
	}
}

// COVERS: FR-1.11h | property
func TestRoundToPlaces(t *testing.T) {
	for _, c := range []struct {
		in     float64
		places int
		want   float64
	}{
		{48.24, 1, 48.2},
		{48.26, 1, 48.3},
		{11.0, 1, 11.0},
		{0.0, 1, 0.0},
		{99.95, 1, 100.0},
	} {
		if got := num.RoundTo(c.in, c.places); got != c.want {
			t.Errorf("RoundTo(%v, %d) = %v, want %v", c.in, c.places, got, c.want)
		}
	}
}

// COVERS: FR-6.3 | property
func TestRoundIntCarriesTheSameTieRule(t *testing.T) {
	for _, c := range []struct {
		in   float64
		want int
	}{
		{0.5, 0}, {1.5, 2}, {2.5, 2}, {7.5, 8}, {-1.5, -2},
	} {
		if got := num.RoundInt(c.in); got != c.want {
			t.Errorf("RoundInt(%v) = %d, want %d", c.in, got, c.want)
		}
	}
}

// COVERS: FR-1.11h | edge
//
// A percentage is neither floored nor capped, so a division that has gone wrong
// reaches here rather than being caught upstream. It comes back unchanged
// rather than becoming a number that looks measured.
func TestRoundToPassesNaNAndInfinityThrough(t *testing.T) {
	nan := math.NaN()
	if got := num.RoundTo(nan, 1); !math.IsNaN(got) {
		t.Errorf("RoundTo(NaN) = %v, want NaN", got)
	}
	for _, inf := range []float64{math.Inf(1), math.Inf(-1)} {
		if got := num.RoundTo(inf, 1); got != inf {
			t.Errorf("RoundTo(%v) = %v, want it unchanged", inf, got)
		}
	}
}

// COVERS: FR-6.8 | edge
func TestClampHoldsBetweenBounds(t *testing.T) {
	for _, c := range []struct{ in, lo, hi, want float64 }{
		{-5, 0, 100, 0},
		{150, 0, 100, 100},
		{42, 0, 100, 42},
		{0, 0, 100, 0},
		{100, 0, 100, 100},
	} {
		if got := num.Clamp(c.in, c.lo, c.hi); got != c.want {
			t.Errorf("Clamp(%v) = %v, want %v", c.in, got, c.want)
		}
	}
}
