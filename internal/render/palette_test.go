package render_test

import (
	"regexp"
	"strings"
	"testing"

	"github.com/scriptedworld/infobot/internal/payload"
	"github.com/scriptedworld/infobot/internal/render"
)

var fgCode = regexp.MustCompile(`\033\[(?:1;)?38;2;(\d+);(\d+);(\d+)`)

// firstFG is the first foreground colour in a rendered span.
func firstFG(t *testing.T, text string) [3]int {
	t.Helper()
	match := fgCode.FindStringSubmatch(text)
	if match == nil {
		t.Fatalf("no foreground colour in %q", text)
	}
	var out [3]int
	for i := range out {
		out[i] = atoi(t, match[i+1])
	}
	return out
}

func atoi(t *testing.T, text string) int {
	t.Helper()
	n := 0
	for _, r := range text {
		n = n*10 + int(r-'0')
	}
	return n
}

// cellColours pulls every foreground colour out of a bar, in order.
func cellColours(t *testing.T, text string) [][3]int {
	t.Helper()
	var out [][3]int
	for _, match := range fgCode.FindAllStringSubmatch(text, -1) {
		out = append(out, [3]int{
			atoi(t, match[1]), atoi(t, match[2]), atoi(t, match[3]),
		})
	}
	return out
}

func colourful(t *testing.T) {
	t.Helper()
	t.Setenv("NO_COLOR", "")
}

// rampAt samples the ramp at pct through the printed NUMBER rather than through
// a bar cell. A cell is coloured for the percentage IT stands for, so the one
// cell of a one-cell bar is always 100% whatever the bar's own reading; the
// number is coloured from the real percentage, which is the thing under test.
func rampAt(t *testing.T, pct float64) [3]int {
	t.Helper()
	return firstFG(t, render.ContextSegment(payload.Map{
		"context_window_size": 200000.0, "used_percentage": pct,
	}, 0))
}

// COVERS: FR-6.1 | property
//
// Two straight lines, green to yellow from 0 to 75 and yellow to red from 75 to
// 90, so the colour moves fastest exactly where a glance needs to tell 80 from
// 88.
func TestRampIsTwoStraightLines(t *testing.T) {
	colourful(t)
	at := func(pct float64) [3]int { return rampAt(t, pct) }
	if got, want := at(0), [3]int{60, 200, 90}; got != want {
		t.Errorf("bottom of the ramp = %v, want green %v", got, want)
	}
	if got, want := at(75), [3]int{235, 220, 40}; got != want {
		t.Errorf("the pivot = %v, want yellow %v", got, want)
	}
	// Red arrives at the alarm boundary, not before it.
	if got, want := at(89.999), [3]int{225, 45, 45}; got != want {
		t.Errorf("top of the ramp = %v, want red %v", got, want)
	}
	// The second leg is steeper: 75 to 90 covers the same colour distance as
	// 0 to 75.
	lower := at(37.5) // halfway along the first leg
	upper := at(82.5) // halfway along the second
	if lower == upper {
		t.Error("both legs produced the same colour")
	}
}

// COVERS: FR-6.2, FR-6.2a | property
//
// At 90 the foreground is the red the ramp arrives at and the background is the
// TERMINAL'S OWN, so the boundary has nothing to show. At 100 it is pale yellow
// on deep red. Bold is on across the whole band.
func TestAlarmFadesInRatherThanSwitchingOn(t *testing.T) {
	colourful(t)
	counts := func(pct float64) string {
		return render.ContextSegment(payload.Map{
			"context_window_size": 200000.0, "used_percentage": pct,
		}, 0)
	}
	if got := counts(90); !strings.Contains(got, "\033[1;38;2;225;45;45;48;2;26;27;38m") {
		t.Errorf("at 90 the alarm is not red on the terminal backdrop: %q", got)
	}
	if got := counts(100); !strings.Contains(got, "\033[1;38;2;250;240;120;48;2;180;25;25m") {
		t.Errorf("at 100 the alarm has not arrived in full: %q", got)
	}
	// Bold is on across the whole band and cannot fade.
	for _, pct := range []float64{90, 95, 100} {
		if !strings.Contains(counts(pct), "\033[1;38;2;") {
			t.Errorf("bold absent at %v: %q", pct, counts(pct))
		}
	}
}

// COVERS: FR-6.2a | edge
//
// Nothing jumps at the boundary: the last cell below the alarm and the first
// cell in it are the same foreground.
func TestAlarmBoundaryHasNothingToShow(t *testing.T) {
	colourful(t)
	below := rampAt(t, 89.999)
	at := rampAt(t, 90)
	if below != at {
		t.Errorf("ramp arrives at %v and the alarm starts at %v; they must match", below, at)
	}
}

// COVERS: FR-6.2b | property
//
// The band is the top tenth of the bar, so its resolution is the cell count: 11
// cells of 103, 5 of 40, and 1 at BAR_MIN, where it is a step rather than a
// fade. The gauges are fixed at 10 cells so their band is 2.
func TestAlarmResolutionIsTheCellCount(t *testing.T) {
	colourful(t)
	for _, c := range []struct {
		cells int
		want  int
	}{{103, 11}, {40, 5}, {10, 2}, {8, 1}} {
		bar := render.Bar(100, c.cells, "")
		alarmCells := strings.Count(bar, "\033[1;38;2;")
		if alarmCells != c.want {
			t.Errorf("%d cells: %d in the alarm band, want %d", c.cells, alarmCells, c.want)
		}
	}
}

// COVERS: FR-6.3 | property
//
// Each filled cell is coloured for the percentage IT stands for, not for the
// bar's total, so the fade is a fixed property of the bar and only its length
// moves.
func TestEachCellIsColouredForItsOwnPercentage(t *testing.T) {
	colourful(t)
	half := cellColours(t, render.Bar(50, 20, ""))
	full := cellColours(t, render.Bar(100, 20, ""))
	if len(half) < 2 || len(full) < 2 {
		t.Fatalf("too few colours: %d and %d", len(half), len(full))
	}
	// The first cell means 5% in both, so it is the same colour in both.
	if half[0] != full[0] {
		t.Errorf("first cell differs by bar length: %v vs %v", half[0], full[0])
	}
}

// COVERS: FR-6.5, FR-6.6 | property
//
// One escape per RUN of cells sharing a style, and every run opens with a reset
// BEFORE the style: the alarm carries bold and a background, so a bare colour
// change after it leaves both switched on for the rest of the line.
func TestRunsShareOneEscapeAndOpenWithAReset(t *testing.T) {
	colourful(t)
	// A tint paints every filled cell one colour, so the whole fill is ONE run
	// and the escapes are the fill, the track, and the closing reset. The fade
	// is the opposite case and gives most cells their own shade, which is what
	// it is for; the sharing is what makes the tinted gauge cheap.
	tinted := render.Bar(50, 40, "\033[38;2;1;2;3m")
	if got := strings.Count(tinted, "\033[38;2;1;2;3m"); got != 1 {
		t.Errorf("tinted fill emitted %d escapes, want 1 for the run", got)
	}
	if got := strings.Count(tinted, "\033[38;2;0;95;95m"); got != 1 {
		t.Errorf("empty track emitted %d escapes, want 1 for the run", got)
	}

	// Every style change opens with a reset BEFORE the style, because the alarm
	// carries bold and a background as well as a colour and a bare colour
	// change after it leaves both switched on for the rest of the line.
	for _, bar := range []string{tinted, render.Bar(50, 40, ""), render.Bar(95, 40, "")} {
		for _, glyph := range []string{"▰", "▱"} {
			if strings.Contains(bar, glyph+"\033[38;2;") ||
				strings.Contains(bar, glyph+"\033[1;38;2;") {
				t.Errorf("a style change was not preceded by a reset: %q", bar)
			}
		}
	}
}

// COVERS: FR-6.10, FR-6.12 | property
//
// The empty cells carry an outline glyph and NO background, and the rail is
// drawn in that same colour rather than one of its own.
func TestEmptyCellsAndRailShareOneColourAndNoBackground(t *testing.T) {
	colourful(t)
	bar := render.Bar(10, 20, "")
	if !strings.Contains(bar, "\033[38;2;0;95;95m▱") {
		t.Errorf("empty cells are not the dim cyan: %q", bar)
	}
	if strings.Contains(bar, "48;2;0;95;95") {
		t.Errorf("empty cells carry a background: %q", bar)
	}

	isolate(t)
	rows := render.Build(payload.Map{
		"context_window": map[string]any{
			"context_window_size": 200000.0, "used_percentage": 20.0,
		},
		"rate_limits": map[string]any{
			"five_hour": map[string]any{"used_percentage": 30.0},
		},
	}, "/home/me", 120, clock)
	if !strings.HasPrefix(rows[0], "\033[38;2;0;95;95m") {
		t.Errorf("the rail is not the empty cells' colour: %q", rows[0])
	}
}

// COVERS: FR-6.11 | property
func TestPathCarriesTheDesktopTeal(t *testing.T) {
	colourful(t)
	isolate(t)
	rows := render.Build(payload.Map{
		"workspace": map[string]any{"current_dir": "/home/me/x"},
	}, "/home/me", 120, clock)
	if !strings.Contains(rows[0], "\033[38;2;0;165;149m") {
		t.Errorf("path is not the ENCOM teal: %q", rows[0])
	}
}

// COVERS: FR-6.13 | property
//
// A word present to be scanned past is dimmed; the figures beside it are not.
func TestOnlyConnectiveWordsAreDimmed(t *testing.T) {
	colourful(t)
	segment := render.ContextSegment(payload.Map{
		"context_window_size": 200000.0, "used_percentage": 40.0,
	}, 10)
	if strings.Contains(segment, "\033[38;2;120;130;135m") {
		t.Errorf("the counts were dimmed: %q", segment)
	}
}

// COVERS: FR-7.1, FR-7.2, FR-7.3 | property
//
// The gauge's LENGTH is the spend and its COLOUR is the verdict, and the verdict
// is where the window is projected to LAND. Landing exactly full is green,
// because that is the best outcome available rather than an alarm.
func TestPaceColoursTheGaugeByWhereItLands(t *testing.T) {
	colourful(t)
	// Half the window elapsed, half of it spent: lands exactly full.
	onRate := render.LimitSegment("5hr", payload.Map{
		"used_percentage": 50.0, "resets_at": at(5 * 3600 / 2),
	}, 5*3600, false, clock)
	// Same elapsed, far more spent: empties early.
	hot := render.LimitSegment("5hr", payload.Map{
		"used_percentage": 90.0, "resets_at": at(5 * 3600 / 2),
	}, 5*3600, false, clock)
	// Same elapsed, barely touched: goes unspent.
	cold := render.LimitSegment("5hr", payload.Map{
		"used_percentage": 5.0, "resets_at": at(5 * 3600 / 2),
	}, 5*3600, false, clock)

	green, warm, blue := firstFG(t, onRate), firstFG(t, hot), firstFG(t, cold)
	if !(warm[0] > green[0] && green[0] > blue[0]) {
		t.Errorf("pace does not diverge: hot %v, on-rate %v, cold %v", warm, green, blue)
	}
	// The LENGTH still says the spend, whatever the colour says.
	if strings.Count(hot, "▰") != 9 {
		t.Errorf("gauge length stopped tracking spend: %q", hot)
	}
}

// COVERS: FR-7.4, FR-7.5, FR-7.6 | property
//
// The verdict fades in against GREEN rather than switching on, and the fade is
// a fraction of the window, so the seven day window matures at the same point
// in its own life instead of after an afternoon.
func TestVerdictFadesInAgainstGreen(t *testing.T) {
	colourful(t)
	// The same overspend, judged early and late in the window.
	early := firstFG(t, render.LimitSegment("w", payload.Map{
		"used_percentage": 20.0, "resets_at": at(5*3600 - 5*60),
	}, 5*3600, false, clock))
	late := firstFG(t, render.LimitSegment("w", payload.Map{
		"used_percentage": 90.0, "resets_at": at(5 * 60),
	}, 5*3600, false, clock))

	// Early is close to green because almost nothing can be said yet.
	if early != [3]int{60, 200, 90} {
		t.Errorf("early verdict = %v, want it still green", early)
	}
	if late[0] <= early[0] {
		t.Errorf("late verdict %v is no warmer than the early one %v", late, early)
	}

	// A fraction, not a duration: the same position in each window judges the
	// same overspend identically.
	fiveHour := firstFG(t, render.LimitSegment("w", payload.Map{
		"used_percentage": 80.0, "resets_at": at(5 * 3600 / 2),
	}, 5*3600, false, clock))
	sevenDay := firstFG(t, render.LimitSegment("w", payload.Map{
		"used_percentage": 80.0, "resets_at": at(7 * 86400 / 2),
	}, 7*86400, false, clock))
	if fiveHour != sevenDay {
		t.Errorf("windows judged differently at the same fraction: %v vs %v",
			fiveHour, sevenDay)
	}
}

// COVERS: FR-7.7 | edge
//
// Beyond the ends the colour clamps: once it will not last, by how much stops
// changing what to do about it.
func TestPaceClampsBeyondTheEndsOfTheScale(t *testing.T) {
	colourful(t)
	// Both project far past the top stop of 150, at the same window position,
	// so both clamp to the same red.
	bad := firstFG(t, render.LimitSegment("w", payload.Map{
		"used_percentage": 200.0, "resets_at": at(60),
	}, 5*3600, false, clock))
	worse := firstFG(t, render.LimitSegment("w", payload.Map{
		"used_percentage": 400.0, "resets_at": at(60),
	}, 5*3600, false, clock))
	if bad != worse {
		t.Errorf("clamp failed: %v against %v", bad, worse)
	}
}

// COVERS: FR-7.8 | negative
//
// With no reset time there is no window position, so the gauge falls back to
// meaning what the context meter's colour means.
func TestWindowWithNoResetFallsBackToTheConsumptionRamp(t *testing.T) {
	colourful(t)
	noReset := firstFG(t, render.LimitSegment("w", payload.Map{
		"used_percentage": 50.0,
	}, 5*3600, false, clock))
	ramp := rampAt(t, 50)
	if noReset != ramp {
		t.Errorf("fallback = %v, want the consumption ramp's %v", noReset, ramp)
	}
}

// COVERS: FR-7.11 | edge
//
// The pace arithmetic cannot divide by nothing and cannot run backwards. A
// reset further out than the window is long reads as the start, and a
// projection made a moment in is large rather than infinite.
func TestPaceArithmeticSurvivesItsEdges(t *testing.T) {
	colourful(t)
	for _, c := range []struct {
		name     string
		resetsAt float64
	}{
		{"a moment into the window", at(5*3600 - 1)},
		{"a reset further out than the window is long", at(9 * 3600)},
		{"a reset one second away", at(1)},
	} {
		got := render.LimitSegment("w", payload.Map{
			"used_percentage": 50.0, "resets_at": c.resetsAt,
		}, 5*3600, false, clock)
		if got == "" {
			t.Errorf("%s: segment vanished", c.name)
			continue
		}
		colour := firstFG(t, got)
		for i, channel := range colour {
			if channel < 0 || channel > 255 {
				t.Errorf("%s: channel %d out of range in %v", c.name, i, colour)
			}
		}
	}
}

// COVERS: FR-7.10 | property
func TestWindowLengthsComeFromTheFieldNames(t *testing.T) {
	colourful(t)
	isolate(t)
	rows := render.Build(payload.Map{
		"rate_limits": map[string]any{
			// Half of five hours, and half of seven days.
			"five_hour": map[string]any{"used_percentage": 50.0, "resets_at": at(5 * 3600 / 2)},
			"seven_day": map[string]any{"used_percentage": 50.0, "resets_at": at(7 * 86400 / 2)},
		},
	}, "/home/me", 200, clock)
	// Both are at the same point in their own window, so both read the same
	// verdict despite the durations differing by a factor of thirty-three.
	parts := strings.Split(strip(rows[len(rows)-1]), "📅")
	if len(parts) != 2 {
		t.Fatalf("both windows did not render: %q", rows)
	}
}
