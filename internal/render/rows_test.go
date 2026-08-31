package render_test

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/scriptedworld/infobot/internal/payload"
	"github.com/scriptedworld/infobot/internal/render"
)

// full is a payload with everything present, for tests about where things go
// rather than about whether they appear.
func full() payload.Map {
	return payload.Map{
		"session_id": "a5e58a4d-2a4e-4774-aa6c-1e7745721df6",
		"model":      map[string]any{"display_name": "Opus 5", "id": "claude-opus-5"},
		"effort":     map[string]any{"level": "high"},
		"workspace": map[string]any{
			"current_dir": "/home/me/.projects/demo/bin",
			"project_dir": "/home/me/.projects/demo",
		},
		"context_window": window(48),
		"rate_limits": map[string]any{
			"five_hour": map[string]any{"used_percentage": 34.0, "resets_at": at(3600)},
			"seven_day": map[string]any{"used_percentage": 62.0, "resets_at": at(86400)},
		},
	}
}

func rows(t *testing.T, data payload.Map, width int) []string {
	t.Helper()
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	return render.Build(data, "/home/me", width, clock)
}

// COVERS: FR-1.1 | positive
//
// Reads the session JSON on standard input and writes COMPLETE rows to standard
// output. Complete is the requirement; the count is a rendering decision.
func TestMainWritesCompleteRows(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	var out strings.Builder
	in := `{"session_id":"a5e58a4d-0000-4000-8000-000000000000",` +
		`"model":{"display_name":"Opus 5"},` +
		`"context_window":{"context_window_size":200000,"used_percentage":48}}`
	if code := render.Main(strings.NewReader(in), &out); code != 0 {
		t.Fatalf("exit = %d", code)
	}
	got := out.String()
	if !strings.HasSuffix(got, "\n") {
		t.Errorf("last row is not terminated: %q", got)
	}
	for _, line := range strings.Split(strings.TrimSuffix(got, "\n"), "\n") {
		if line == "" {
			t.Errorf("a blank row was written:\n%q", got)
		}
	}
	if !strings.Contains(got, "Opus 5") || !strings.Contains(got, "consumed") {
		t.Errorf("row is not complete: %q", got)
	}
}

// COVERS: FR-5.4, FR-5.13 | property
//
// The session id CLOSES the identity row, which puts the bar between two fixed
// things. Each segment opens with a fixed marker, so which meter is which is
// read from the shape rather than from the numbers.
func TestRowShapeIsMarkersAndAClosingSessionId(t *testing.T) {
	got := rows(t, full(), 200)
	if !strings.HasSuffix(got[0], "⟨a5e58a4d⟩") {
		t.Errorf("identity row does not close with the session id: %q", got[0])
	}
	for marker, what := range map[string]string{
		"🧠": "context", "⏳": "five hour", "📅": "seven day",
	} {
		if !strings.Contains(strings.Join(got, "\n"), marker) {
			t.Errorf("%s segment carries no marker", what)
		}
	}
}

// COVERS: FR-1.11a | property
//
// The bar and the file come through ONE measurement. Two formulas would let a
// bar reading 56% sit beside a file saying something else, and a reader has no
// way to tell which is wrong.
func TestBarAndFileAgreeBecauseTheyShareAMeasurement(t *testing.T) {
	for _, given := range []map[string]any{
		// A stated percentage.
		{"context_window_size": 200000.0, "used_percentage": 48.0},
		// Counts alone, so the percentage is derived.
		{
			"context_window_size": 200000.0,
			"current_usage":       map[string]any{"input_tokens": 50000.0},
		},
		// Both, disagreeing: the payload's percentage is the authority.
		{
			"context_window_size": 200000.0, "used_percentage": 48.0,
			"current_usage": map[string]any{"input_tokens": 10.0},
		},
	} {
		state := t.TempDir()
		t.Setenv("HOME", t.TempDir())
		t.Setenv("XDG_STATE_HOME", state)
		t.Setenv("NO_COLOR", "1")
		t.Setenv("TMUX", "")
		t.Setenv("HERDR_PANE_ID", "")

		data := payload.Map{"session_id": "agree", "context_window": given}
		var out strings.Builder
		if code := render.Main(strings.NewReader(asJSON(t, data)), &out); code != 0 {
			t.Fatalf("exit = %d", code)
		}

		file, err := os.ReadFile(filepath.Join(state, "infobot", "agree.status.yaml"))
		if err != nil {
			t.Fatalf("no state file: %v", err)
		}
		onDisk := field(string(file), "context_percent")
		onScreen := percentInRow(out.String())
		if onDisk == "" || onScreen == "" {
			t.Fatalf("could not read both: file %q, row %q", onDisk, out.String())
		}
		// The row prints whole percent and the file keeps a decimal, so they
		// agree when the file rounds to what the row shows.
		if !strings.HasPrefix(onDisk, onScreen) {
			t.Errorf("file says %s and the row says %s%%", onDisk, onScreen)
		}
	}
}

func asJSON(t *testing.T, data payload.Map) string {
	t.Helper()
	raw, err := json.Marshal(data)
	if err != nil {
		t.Fatal(err)
	}
	return string(raw)
}

func field(file, key string) string {
	for _, line := range strings.Split(file, "\n") {
		if name, value, found := strings.Cut(line, ": "); found && name == `"`+key+`"` {
			return value
		}
	}
	return ""
}

func percentInRow(row string) string {
	cut := strings.Index(row, "% consumed")
	if cut < 0 {
		return ""
	}
	start := strings.LastIndexAny(row[:cut], "( ") + 1
	return row[start:cut]
}

// COVERS: FR-5.2 | property
//
// The context segment rides the identity row when that row can hold it with no
// bar at all, and moves to the meter row when it cannot. Truncation eats the
// end of the row, which is the session id, so relocating costs less.
func TestContextRelocatesRatherThanOverflowing(t *testing.T) {
	wide := rows(t, full(), 200)
	if !strings.Contains(wide[0], "🧠") {
		t.Errorf("at 200 the context should ride row one: %q", wide[0])
	}

	long := full()
	long["workspace"] = map[string]any{
		"current_dir": "/home/me/" + strings.Repeat("deeply/", 9) + "leaf",
	}
	narrow := rows(t, long, 100)
	if strings.Contains(narrow[0], "🧠") {
		t.Errorf("with a long path the context should leave row one: %q", narrow[0])
	}
	if !strings.Contains(narrow[1], "🧠") {
		t.Errorf("the context went nowhere: %q", narrow[1])
	}
}

// COVERS: FR-5.3 | property
//
// The context bar takes every column the identity row has left once everything
// else is placed, so a wider pane spends all of it on the bar.
func TestBarTakesWhatTheRowHasLeft(t *testing.T) {
	// A short path, so the context stays on row one at every width tested and
	// this measures the bar's growth rather than FR-5.2's relocation.
	short := full()
	short["workspace"] = map[string]any{"current_dir": "/home/me/x"}

	previous := 0
	for _, width := range []int{140, 180, 220} {
		got := rows(t, short, width)
		if !strings.Contains(got[0], "🧠") {
			t.Fatalf("width %d: the context left row one, which this is not about", width)
		}
		cells := strings.Count(got[0], "▰") + strings.Count(got[0], "▱")
		// Every extra column of pane becomes a cell, so the growth tracks the
		// width exactly rather than lagging it.
		if previous != 0 && cells-previous != 40 {
			t.Errorf("width %d added %d cells for 40 columns", width, cells-previous)
		}
		previous = cells
	}
}

// COVERS: FR-5.5 | property
//
// The rate limit gauges are a FIXED ten cells rather than a share of the slack,
// so the row below does not move under them as the bar above grows.
func TestGaugesAreFixedAtTenCells(t *testing.T) {
	// No context window, so the meter row carries the two gauges and nothing
	// else and the count is theirs alone.
	windows := payload.Map{"rate_limits": full()["rate_limits"]}
	for _, width := range []int{120, 160, 200, 250} {
		got := rows(t, windows, width)
		meters := got[len(got)-1]
		cells := strings.Count(meters, "▰") + strings.Count(meters, "▱")
		if cells != 20 {
			t.Errorf("width %d: %d gauge cells, want 10 per window whatever the slack",
				width, cells)
		}
	}
}

// COVERS: FR-5.10 | property
//
// Parts within a row are divided by a separator costing exactly one column,
// whatever glyph the font provides, so a font without it shifts nothing.
func TestSeparatorCostsExactlyOneColumn(t *testing.T) {
	// The powerline glyph and the plain bar it falls back to are one column
	// each. Measured through the renderer's own width function, which is what
	// the fitting uses.
	for _, glyph := range []string{"", "│", "|"} {
		if got := render.VisibleWidth(glyph); got != 1 {
			t.Errorf("VisibleWidth(%q) = %d, want 1", glyph, got)
		}
	}
	// And in a row, each separator adds exactly the glyph plus its two spaces.
	one := rows(t, payload.Map{"model": map[string]any{"display_name": "Opus 5"}}, 200)
	two := rows(t, payload.Map{
		"model":     map[string]any{"display_name": "Opus 5"},
		"workspace": map[string]any{"current_dir": "/home/me/x"},
	}, 200)
	added := render.VisibleWidth(two[0]) - render.VisibleWidth(one[0])
	if want := render.VisibleWidth("  ") + render.VisibleWidth("~/x"); added != want {
		t.Errorf("adding a part cost %d columns, want %d", added, want)
	}
}

// COVERS: FR-5.11 | property
//
// The rail is part of what a row costs. A bar sized against the parts alone
// overflows by exactly the rail, which is three columns.
func TestRailIsCountedInTheRowsCost(t *testing.T) {
	got := rows(t, full(), 200)
	for i, row := range got {
		lead := []string{"╭─ ", "╰─ "}[i]
		if !strings.HasPrefix(row, lead) {
			t.Fatalf("row %d does not start with the rail: %q", i, row)
		}
		if render.VisibleWidth(lead) != 3 {
			t.Fatalf("the rail is not three columns")
		}
		// The row fits INCLUDING its rail, which is the thing a fit that
		// forgets the rail gets wrong by exactly three.
		if w := render.VisibleWidth(row); w > 200-3 {
			t.Errorf("row %d is %d columns with the rail, over budget", i, w)
		}
	}
}
