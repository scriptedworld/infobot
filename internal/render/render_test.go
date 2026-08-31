package render_test

import (
	"regexp"
	"strings"
	"testing"
	"time"

	"github.com/scriptedworld/infobot/internal/payload"
	"github.com/scriptedworld/infobot/internal/render"
)

var ansi = regexp.MustCompile("\033\\[[0-9;]*m")

func strip(text string) string { return ansi.ReplaceAllString(text, "") }

// clock is a fixed instant, so a countdown is asserted as a string rather than
// as a shape.
var clock = time.Unix(1_800_000_000, 0)

func at(seconds int64) float64 { return float64(clock.Unix() + seconds) }

// isolate points HOME and the state directory at scratch, so a render reaches
// no real transcript and leaves no file naming a session that never existed.
func isolate(t *testing.T) {
	t.Helper()
	home := t.TempDir()
	t.Setenv("HOME", home)
	t.Setenv("XDG_STATE_HOME", t.TempDir())
	// No host to ask, so the width is whatever the caller passes.
	t.Setenv("TMUX", "")
	t.Setenv("HERDR_PANE_ID", "")
}

func window(pct float64) map[string]any {
	return map[string]any{"context_window_size": 200000.0, "used_percentage": pct}
}

// COVERS: FR-6.9 | property
func TestTokensAreCompact(t *testing.T) {
	for _, c := range []struct {
		in   float64
		want string
	}{
		{0, "0"},
		{999, "999"},
		{1000, "1k"},
		{21000, "21k"},
		{142000, "142k"},
		{999999, "1000k"},
		{1000000, "1.0M"},
		{2700000, "2.7M"},
	} {
		if got := render.Tokens(c.in); got != c.want {
			t.Errorf("Tokens(%v) = %q, want %q", c.in, got, c.want)
		}
	}
}

// COVERS: FR-7.9 | property
func TestCountdownIsTheLargestTwoNonZeroUnits(t *testing.T) {
	for _, c := range []struct {
		in   int64
		want string
	}{
		{3*3600 + 30*60, "3h30m"},
		{3*3600 + 5*60, "3h05m"},
		{45 * 60, "45m"},
		{2*86400 + 5*3600, "2d5h"},
		{7 * 86400, "7d0h"},
		{90, "1m"},
	} {
		if got := render.Countdown(at(c.in), clock); got != c.want {
			t.Errorf("Countdown(+%ds) = %q, want %q", c.in, got, c.want)
		}
	}
}

// COVERS: FR-2.4 | negative
//
// A countdown reading "0m" suggests a reset is imminent when it has already
// happened and the number is simply stale.
func TestCountdownIsEmptyWhenMissingOrPast(t *testing.T) {
	for _, in := range []float64{0, at(-1), at(-86400)} {
		if got := render.Countdown(in, clock); got != "" {
			t.Errorf("Countdown(%v) = %q, want empty", in, got)
		}
	}
}

// COVERS: FR-3.9 | property
//
// Escapes cost nothing and east-asian wide glyphs cost two. len() is wrong in
// both directions and both errors run toward a row too wide for the line.
func TestVisibleWidthMeasuresWhatTheTerminalDraws(t *testing.T) {
	for _, c := range []struct {
		in   string
		want int
	}{
		{"", 0},
		{"abc", 3},
		{"\033[0mabc\033[0m", 3},
		{"\033[38;2;1;2;3mx\033[0m", 1},
		{"🧠", 2},
		{"🧠 x", 4},
		{"▰▱", 2},  // ambiguous, counted as one each
		{"╭─ ", 3}, // the rail
		{"⟨abcd⟩", 6},
	} {
		if got := render.VisibleWidth(c.in); got != c.want {
			t.Errorf("VisibleWidth(%q) = %d, want %d", c.in, got, c.want)
		}
	}
}

// COVERS: FR-6.7, FR-6.8 | property
func TestBarIsFilledAndEmptyParallelogramsOnly(t *testing.T) {
	t.Setenv("NO_COLOR", "1")
	for _, c := range []struct {
		pct   float64
		cells int
		want  string
	}{
		{0, 10, "▱▱▱▱▱▱▱▱▱▱"},
		{50, 10, "▰▰▰▰▰▱▱▱▱▱"},
		{100, 10, "▰▰▰▰▰▰▰▰▰▰"},
		{-20, 10, "▱▱▱▱▱▱▱▱▱▱"}, // below 0 renders all empty
		{140, 10, "▰▰▰▰▰▰▰▰▰▰"}, // above 100 renders all filled
	} {
		if got := render.Bar(c.pct, c.cells, ""); got != c.want {
			t.Errorf("Bar(%v, %d) = %q, want %q", c.pct, c.cells, got, c.want)
		}
	}
}

// COVERS: FR-6.8 | edge
func TestBarOfNoCellsRendersNothing(t *testing.T) {
	for _, cells := range []int{0, -1} {
		if got := render.Bar(50, cells, ""); got != "" {
			t.Errorf("Bar(50, %d) = %q, want empty", cells, got)
		}
	}
}

// COVERS: FR-3.8 | property
//
// NO_COLOR strips EVERY escape, the separator and the rail included, so the
// output is either coloured or clean and never half of each.
func TestNoColorLeavesNoEscapeAnywhere(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	data := payload.Map{
		"session_id":     "a5e58a4d-0000-4000-8000-000000000000",
		"model":          map[string]any{"display_name": "Opus 5"},
		"workspace":      map[string]any{"current_dir": "/tmp/x"},
		"context_window": window(48),
		"rate_limits": map[string]any{
			"five_hour": map[string]any{"used_percentage": 34.0, "resets_at": at(3600)},
		},
	}
	for _, row := range render.Build(data, "/tmp", 120, clock) {
		if strings.Contains(row, "\033") {
			t.Errorf("escape survived NO_COLOR: %q", row)
		}
	}
}

// COVERS: FR-5.1 | property
//
// An opening corner with no closing corner under it reads as a block that
// failed to finish, so a lone row gets the stub instead.
func TestRailCornersMatchTheRowCount(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")

	one := render.Build(payload.Map{
		"session_id": "a5e58a4d-0000-4000-8000-000000000000",
	}, "/tmp", 120, clock)
	if len(one) != 1 {
		t.Fatalf("got %d rows, want 1", len(one))
	}
	if !strings.HasPrefix(one[0], "╶─ ") {
		t.Errorf("lone row lead = %q, want the stub", one[0])
	}

	two := render.Build(payload.Map{
		"session_id":     "a5e58a4d-0000-4000-8000-000000000000",
		"context_window": window(20),
		"rate_limits": map[string]any{
			"five_hour": map[string]any{"used_percentage": 34.0},
		},
	}, "/tmp", 120, clock)
	if len(two) != 2 {
		t.Fatalf("got %d rows, want 2", len(two))
	}
	if !strings.HasPrefix(two[0], "╭─ ") || !strings.HasPrefix(two[1], "╰─ ") {
		t.Errorf("two-row leads = %q / %q, want opening and closing corners",
			two[0], two[1])
	}
}

// COVERS: FR-1.5 | negative
//
// A row with no parts in it is omitted rather than printed blank. Early in a
// session the context and rate-limit fields are all absent.
func TestEmptyRowIsOmitted(t *testing.T) {
	isolate(t)
	rows := render.Build(payload.Map{"session_id": "a5e58a4d-0000-4000-8000-000000000000"},
		"/tmp", 120, clock)
	if len(rows) != 1 {
		t.Errorf("got %d rows, want the meter row omitted", len(rows))
	}
}

// COVERS: FR-1.3 | negative
//
// A segment whose data is absent is DROPPED, never rendered as zero, because
// zero is a claim and absence is not.
func TestAbsentDataIsDroppedNotZeroed(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	rows := render.Build(payload.Map{
		"session_id": "a5e58a4d-0000-4000-8000-000000000000",
		"model":      map[string]any{"display_name": "Opus 5"},
	}, "/tmp", 120, clock)
	joined := strings.Join(rows, "\n")
	for _, absent := range []string{"0%", "0/", "🧠", "⏳", "📅"} {
		if strings.Contains(joined, absent) {
			t.Errorf("%q rendered for absent data:\n%s", absent, joined)
		}
	}
}

// COVERS: FR-2.13 | property
func TestUnknownPayloadFieldsChangeNothing(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	base := payload.Map{
		"session_id":     "a5e58a4d-0000-4000-8000-000000000000",
		"model":          map[string]any{"display_name": "Opus 5"},
		"context_window": window(48),
	}
	before := render.Build(base, "/tmp", 120, clock)

	base["something_claude_code_added_later"] = map[string]any{"x": 1.0}
	after := render.Build(base, "/tmp", 120, clock)

	if strings.Join(before, "\n") != strings.Join(after, "\n") {
		t.Error("a field infobot does not read changed the output")
	}
}

// COVERS: FR-2.9, FR-2.10, FR-2.11 | property
func TestIdentityRowShowsPathsAndSession(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	rows := render.Build(payload.Map{
		"session_id": "a5e58a4d-2a4e-4774-aa6c-1e7745721df6",
		"model":      map[string]any{"display_name": "Opus 5"},
		"effort":     map[string]any{"level": "high"},
		"workspace": map[string]any{
			"current_dir": "/home/me/.projects/demo/bin",
			"project_dir": "/home/me/.projects/demo",
		},
	}, "/home/me", 200, clock)

	row := rows[0]
	if !strings.Contains(row, "Opus 5 - high") {
		t.Errorf("model and effort missing: %q", row)
	}
	if !strings.Contains(row, "~/.projects/demo/bin") {
		t.Errorf("path not shown relative to home: %q", row)
	}
	if !strings.Contains(row, "⌂ ~/.projects/demo") {
		t.Errorf("project root not marked: %q", row)
	}
	if !strings.Contains(row, "⟨a5e58a4d⟩") {
		t.Errorf("session id not the first eight: %q", row)
	}
}

// COVERS: FR-2.9 | negative
//
// Repeating the root is noise on the common case, a session started where the
// work is.
func TestProjectRootHiddenWhenItMatchesTheWorkingDirectory(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	rows := render.Build(payload.Map{
		"model": map[string]any{"display_name": "Opus 5"},
		"workspace": map[string]any{
			"current_dir": "/home/me/.projects/demo",
			"project_dir": "/home/me/.projects/demo",
		},
	}, "/home/me", 200, clock)
	if strings.Contains(rows[0], "⌂") {
		t.Errorf("root repeated when it equals the working directory: %q", rows[0])
	}
}

// COVERS: FR-3.5, FR-3.6 | property
//
// The width a row is fitted to is the pane less the three columns Claude Code
// keeps, and what exceeds it is cut rather than wrapped.
// Below about 70 columns two rate limit windows cannot be made to fit even
// once their gauges are given up for the percentages, so the row runs over and
// the terminal cuts it. Measured on the Python this replaced: at width 60 its
// meter row was the same 65 columns against the same 57 budget. That is FR-3.6
// working, not a fitting bug, so the fitting assertion starts where fitting is
// achievable.
func TestRowsFitInsideTheBudget(t *testing.T) {
	isolate(t)
	for _, width := range []int{80, 120, 191, 223} {
		rows := render.Build(payload.Map{
			"session_id":     "a5e58a4d-2a4e-4774-aa6c-1e7745721df6",
			"model":          map[string]any{"display_name": "Opus 5"},
			"workspace":      map[string]any{"current_dir": "/home/me/.projects/demo"},
			"context_window": window(48),
			"rate_limits": map[string]any{
				"five_hour": map[string]any{"used_percentage": 34.0, "resets_at": at(3600)},
				"seven_day": map[string]any{"used_percentage": 62.0, "resets_at": at(86400)},
			},
		}, "/home/me", width, clock)
		for i, row := range rows {
			if got := render.VisibleWidth(row); got > width-3 {
				t.Errorf("width %d: row %d is %d columns, over the budget of %d",
					width, i, got, width-3)
			}
		}
	}
}

// COVERS: FR-3.6, FR-5.12 | edge
//
// The bar is DROPPED rather than clamped when the row has no room for it, and
// the counts stay either way.
func TestBarIsDroppedNotClampedWhenThereIsNoRoom(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	rows := render.Build(payload.Map{
		"model": map[string]any{"display_name": "Opus 5"},
		"workspace": map[string]any{
			"current_dir": "/home/me/" + strings.Repeat("deeply/", 8) + "leaf",
		},
		"context_window": window(48),
	}, "/home/me", 40, clock)

	joined := strings.Join(rows, "\n")
	if strings.Contains(joined, "▰") || strings.Contains(joined, "▱") {
		t.Errorf("a bar was drawn with no room for one:\n%s", joined)
	}
	if !strings.Contains(joined, "consumed") {
		t.Errorf("the counts went with the bar:\n%s", joined)
	}
}

// COVERS: FR-3.6 | property
//
// The bar shrinks with the pane and is dropped whole below BAR_MIN rather than
// clamped to it. Clamping overflows, which costs the whole row to save a bar
// that at 12% a cell was not saying much. Cell counts measured against the
// Python this replaced, at the widths where the two must agree.
func TestBarShrinksWithThePaneThenGoesWhole(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	for _, c := range []struct {
		width int
		cells int
	}{
		{40, 0}, {45, 12}, {50, 17}, {60, 27}, {70, 37},
	} {
		rows := render.Build(payload.Map{
			"model": map[string]any{"display_name": "Opus 5"},
			"workspace": map[string]any{
				"current_dir": "/home/me/" + strings.Repeat("deeply/", 8) + "leaf",
			},
			"context_window": window(48),
		}, "/home/me", c.width, clock)

		joined := strings.Join(rows, "")
		got := strings.Count(joined, "▰") + strings.Count(joined, "▱")
		if got != c.cells {
			t.Errorf("width %d: bar is %d cells, want %d", c.width, got, c.cells)
		}
	}
}

// COVERS: FR-3.3, FR-5.12 | edge
//
// With the width unknown there is NO BAR AT ALL. A bar is a claim about how
// much room there is, and with nothing to fit against, any length is a guess
// the host then truncates. The numbers say the same thing at a known cost, so
// the row stays short and left-aligned.
func TestUnknownWidthDrawsNoBar(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	rows := render.Build(payload.Map{
		"context_window": window(50),
	}, "/home/me", 0, clock)
	if got := strings.Count(rows[0], "▰") + strings.Count(rows[0], "▱"); got != 0 {
		t.Errorf("bar is %d cells, want none when the width is unknown", got)
	}
	if !strings.Contains(rows[0], "50%") {
		t.Errorf("the percentage must survive when the bar does not: %q", rows[0])
	}
}

// COVERS: FR-2.2 | property
//
// rate_limits carries a percentage and a reset time and NOTHING ELSE, so no
// token counts appear there.
func TestLimitSegmentShowsNoTokenCounts(t *testing.T) {
	t.Setenv("NO_COLOR", "1")
	got := render.LimitSegment("⏳ 5hr", payload.Map{
		"used_percentage": 34.0, "resets_at": at(3600),
	}, 5*3600, false, true, clock)
	if strings.Contains(got, "/") || strings.Contains(got, "consumed") {
		t.Errorf("counts appeared in a window that carries none: %q", got)
	}
	if !strings.Contains(got, "1h00m") {
		t.Errorf("countdown missing: %q", got)
	}
}

// COVERS: FR-1.3 | negative
func TestLimitSegmentDroppedWithoutAPercentage(t *testing.T) {
	for _, w := range []payload.Map{nil, {}, {"resets_at": at(3600)}} {
		if got := render.LimitSegment("⏳ 5hr", w, 5*3600, false, true, clock); got != "" {
			t.Errorf("LimitSegment(%v) = %q, want empty", w, got)
		}
	}
}

// COVERS: FR-5.6 | property
//
// A meter row over budget gives up its gauges for the percentages they were
// drawing before anything is cut.
func TestCompactGaugesReplaceBarsWhenTheRowIsTight(t *testing.T) {
	t.Setenv("NO_COLOR", "1")
	wide := render.LimitSegment("⏳ 5hr", payload.Map{"used_percentage": 34.0}, 5*3600, false, true, clock)
	tight := render.LimitSegment("⏳ 5hr", payload.Map{"used_percentage": 34.0}, 5*3600, true, true, clock)
	if !strings.Contains(wide, "▰") {
		t.Errorf("wide form has no gauge: %q", wide)
	}
	if strings.Contains(tight, "▰") || !strings.Contains(tight, "34%") {
		t.Errorf("compact form = %q, want the percentage and no gauge", tight)
	}
	if render.VisibleWidth(tight) >= render.VisibleWidth(wide) {
		t.Errorf("compact form is not narrower: %q vs %q", tight, wide)
	}
}

// COVERS: FR-1.4 | regression
//
// A failure confined to one segment costs only that segment. The Python this
// replaced wrapped the whole render in a bare except, so a malformed resets_at
// blanked BOTH rows; golden/malformed-resets.txt captured that. Here the field
// is absent data and the rest of the line survives.
func TestOneBadFieldCostsOnlyItsSegment(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	rows := render.Build(payload.Map{
		"model": map[string]any{"display_name": "Opus 5"},
		"rate_limits": map[string]any{
			"five_hour": map[string]any{"used_percentage": 11.0, "resets_at": "BAD-ISO"},
		},
	}, "/home/me", 120, clock)

	if len(rows) != 2 {
		t.Fatalf("got %d rows, want both to survive a bad field", len(rows))
	}
	if !strings.Contains(rows[0], "Opus 5") {
		t.Errorf("identity row lost to another segment's bad field: %q", rows[0])
	}
	if !strings.Contains(rows[1], "⏳ 5hr") {
		t.Errorf("the window itself was dropped rather than its countdown: %q", rows[1])
	}
	// The countdown is what could not be read, so it is what goes.
	if strings.Contains(rows[1], "h") && strings.Contains(rows[1], "m") {
		t.Errorf("a countdown was rendered from an unreadable timestamp: %q", rows[1])
	}
}

// COVERS: FR-1.2 | negative
func TestMainExitsZeroWhateverItIsGiven(t *testing.T) {
	isolate(t)
	for _, in := range []string{"", "not json at all", "[]", "null", "{}", `{"session_id":123}`} {
		var out strings.Builder
		if code := render.Main(strings.NewReader(in), &out); code != 0 {
			t.Errorf("Main(%q) = %d, want 0", in, code)
		}
		if strings.Contains(out.String(), "panic") {
			t.Errorf("Main(%q) wrote a traceback: %q", in, out.String())
		}
	}
}

// COVERS: FR-6.4 | property
//
// The last filled cell carries the same colour as the number printed beside it,
// because they mean the same thing.
func TestLastFilledCellMatchesTheNumbersColour(t *testing.T) {
	t.Setenv("NO_COLOR", "")
	segment := render.ContextSegment(payload.Map{
		"context_window_size": 200000.0,
		"used_percentage":     48.0,
	}, 50)
	codes := ansi.FindAllString(segment, -1)
	if len(codes) < 2 {
		t.Fatalf("no escapes in %q", segment)
	}
	// The escape opening the counts is the last distinct colour in the string,
	// and the bar's final filled cell carries the one before the empty track.
	if !strings.Contains(segment, "consumed") {
		t.Fatalf("counts missing from %q", segment)
	}
	countsColour := codes[len(codes)-2]
	if !strings.Contains(segment, countsColour) {
		t.Errorf("counts colour %q not found in the bar", countsColour)
	}
}
