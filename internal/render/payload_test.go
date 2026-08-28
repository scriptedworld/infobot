package render_test

import (
	"strings"
	"testing"
	"time"

	"github.com/scriptedworld/infobot/internal/payload"
	"github.com/scriptedworld/infobot/internal/render"
)

// COVERS: FR-2.1 | property
//
// resets_at is a NUMERIC EPOCH rather than a timestamp string, which is what
// the running Claude Code sends. A string is not a timestamp in another
// spelling, it is absent data, and it costs its own countdown and nothing else.
func TestResetsAtIsANumericEpoch(t *testing.T) {
	t.Setenv("NO_COLOR", "1")
	numeric := render.LimitSegment("5hr", payload.Map{
		"used_percentage": 34.0, "resets_at": at(3600),
	}, 5*3600, false, clock)
	if !strings.Contains(numeric, "1h00m") {
		t.Errorf("a numeric epoch gave no countdown: %q", numeric)
	}

	for _, spelled := range []any{"2026-08-28T12:00:00Z", "4102444800", true, nil} {
		got := render.LimitSegment("5hr", payload.Map{
			"used_percentage": 34.0, "resets_at": spelled,
		}, 5*3600, false, clock)
		if got == "" {
			t.Errorf("resets_at %v dropped the whole window", spelled)
		}
		if strings.ContainsAny(strings.TrimPrefix(got, "5hr"), "hm") {
			t.Errorf("resets_at %v produced a countdown: %q", spelled, got)
		}
	}
}

// COVERS: FR-2.3 | property
//
// A percentage the status line prints is computed by the SAME formula as the
// percentage the payload supplies, so the two halves of a segment agree. Where
// the payload states one, that is the one printed, rather than a second figure
// derived from the counts.
func TestPrintedPercentageIsThePayloadsOwn(t *testing.T) {
	t.Setenv("NO_COLOR", "1")
	// Counts that would give 25% if recomputed, beside a stated 48%.
	got := render.ContextSegment(payload.Map{
		"context_window_size": 200000.0,
		"used_percentage":     48.0,
		"current_usage":       map[string]any{"input_tokens": 50000.0},
	}, 0)
	if !strings.Contains(got, "(48% consumed)") {
		t.Errorf("printed percentage disagrees with the payload's: %q", got)
	}
	if strings.Contains(got, "25%") {
		t.Errorf("a second percentage was derived from the counts: %q", got)
	}
}

// COVERS: FR-2.8 | property
func TestModelIsDisplayNameThenIdWithEffortAppended(t *testing.T) {
	for _, c := range []struct {
		name string
		data payload.Map
		want string
	}{
		{"display name wins", payload.Map{
			"model": map[string]any{"display_name": "Opus 5", "id": "claude-opus-5"},
		}, "Opus 5"},
		{"id is the fallback", payload.Map{
			"model": map[string]any{"id": "claude-opus-5"},
		}, "claude-opus-5"},
		{"effort is appended", payload.Map{
			"model":  map[string]any{"display_name": "Opus 5"},
			"effort": map[string]any{"level": "high"},
		}, "Opus 5 - high"},
		{"no effort, no suffix", payload.Map{
			"model": map[string]any{"display_name": "Opus 5"},
		}, "Opus 5"},
	} {
		t.Run(c.name, func(t *testing.T) {
			got := rows(t, c.data, 200)
			if !strings.Contains(got[0], c.want) {
				t.Errorf("row = %q, want it to carry %q", got[0], c.want)
			}
			if c.want == "Opus 5" && strings.Contains(got[0], "Opus 5 -") {
				t.Errorf("an effort suffix appeared with no effort set: %q", got[0])
			}
		})
	}
}

// COVERS: FR-2.12 | property
//
// The project root is MARKED as a root rather than left to read as a second
// path, and the marker sits OUTSIDE the colour the path carries, so what is
// tinted is the path and not the annotation.
func TestProjectRootMarkerSitsOutsideTheColour(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "")
	got := render.Build(payload.Map{
		"workspace": map[string]any{
			"current_dir": "/home/me/.projects/silo/bin",
			"project_dir": "/home/me/.projects/silo",
		},
	}, "/home/me", 200, clock)

	row := got[0]
	if !strings.Contains(row, "⌂ \033[38;2;0;165;149m") {
		t.Errorf("the marker is inside the path's colour, or absent: %q", row)
	}
}

// COVERS: FR-3.1 | property
//
// Colour is 24-bit. Every escape carries three channels rather than a palette
// index, so the ramp is continuous instead of stepped.
func TestEveryColourIsTwentyFourBit(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "")
	joined := strings.Join(render.Build(full(), "/home/me", 200, clock), "\n")

	for _, code := range ansi.FindAllString(joined, -1) {
		if code == "\033[0m" {
			continue
		}
		if !strings.Contains(code, "38;2;") && !strings.Contains(code, "48;2;") {
			t.Errorf("not a 24-bit escape: %q", code)
		}
		// A 256-palette escape is 38;5;N, which is what Claude Code falls back
		// to under $TMUX when the environment is not set.
		if strings.Contains(code, "38;5;") || strings.Contains(code, "48;5;") {
			t.Errorf("a 256-colour escape reached the output: %q", code)
		}
	}
}

// COVERS: FR-3.2 | property
//
// Refresh is time-based as well as event-based, which is Claude Code's
// `refreshInterval` and lives in silo's settings.json. INFOBOT'S HALF of that
// is being safe to call on a timer: a render with no new data must cost nothing
// and change nothing, or a periodic refresh accumulates state or drifts.
func TestRepeatedRendersAreStable(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	data := full()

	first := render.Build(data, "/home/me", 200, clock)
	for i := range 5 {
		again := render.Build(data, "/home/me", 200, clock)
		if strings.Join(first, "\n") != strings.Join(again, "\n") {
			t.Fatalf("render %d differs from the first at the same instant", i+2)
		}
	}
	// And the countdown moves with the clock rather than with the call count,
	// which is what makes a timed refresh worth doing.
	later := render.Build(data, "/home/me", 200, clock.Add(30*time.Minute))
	if strings.Join(first, "\n") == strings.Join(later, "\n") {
		t.Error("a render half an hour later is identical; the clock is not reaching it")
	}
}

// COVERS: FR-3.10 | property
//
// Ambiguous-width characters are counted as ONE column, which is what kitty
// draws them as. A terminal treating ambiguous as wide would draw every bar at
// twice its measured width.
func TestAmbiguousWidthCountsAsOne(t *testing.T) {
	for _, r := range []string{
		"▰", "▱", // the bar's own glyphs
		"─", "╭", "├", "╰", "╵", // the rail
		// Written as an escape rather than as the glyph: it is invisible in
		// most editors and a raw copy of it is easily lost, which is how this
		// line first asserted the width of an empty string.
		"", // the powerline separator, private use
		"⌂", // the project root marker
	} {
		if got := render.VisibleWidth(r); got != 1 {
			t.Errorf("VisibleWidth(%q) = %d, want 1", r, got)
		}
	}
	// Against a genuinely wide glyph, so this is not asserting that everything
	// is one column.
	if got := render.VisibleWidth("🧠"); got != 2 {
		t.Errorf("VisibleWidth(brain) = %d, want 2", got)
	}
}
