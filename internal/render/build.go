package render

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"
	"time"

	"github.com/scriptedworld/infobot/internal/payload"
	"github.com/scriptedworld/infobot/internal/pricing"
	"github.com/scriptedworld/infobot/internal/state"
	"github.com/scriptedworld/infobot/internal/usage"
)

// modelPart is the model, and the effort level when one is set.
func modelPart(data payload.Map) string {
	model := data.Obj("model").Str("display_name")
	if model == "" {
		model = data.Obj("model").Str("id")
	}
	if model == "" {
		return ""
	}
	if effort := data.Obj("effort").Str("level"); effort != "" {
		return model + " - " + effort
	}
	return model
}

// pathParts is where you are, and where the project root is when it differs.
//
// Repeating the root is noise on the common case, which is a session started
// where the work is.
func pathParts(data payload.Map, home string) []string {
	workspace := data.Obj("workspace")
	cwd := workspace.Str("current_dir")
	project := workspace.Str("project_dir")

	var parts []string
	if cwd != "" {
		parts = append(parts, cyan(homeRelative(cwd, home)))
	}
	if project != "" && project != cwd {
		// The marker sits outside the colour the path carries, so what is
		// tinted is the path and not the annotation.
		parts = append(parts, rootGlyph+" "+cyan(homeRelative(project, home)))
	}
	return parts
}

// sessionPart is the first eight of the session id, which is what the harness
// shows.
func sessionPart(data payload.Map) string {
	id := data.Str("session_id")
	if id == "" {
		return ""
	}
	if len(id) > 8 {
		id = id[:8]
	}
	return "⟨" + id + "⟩"
}

// limitParts is the two rate limit windows, each dropped when its data is
// absent.
func limitParts(data payload.Map, compact bool, now time.Time) []string {
	limits := data.Obj("rate_limits")
	var parts []string
	for _, window := range []struct {
		label string
		key   string
		span  int
	}{
		{hourglass + " 5hr", "five_hour", fiveHour},
		{calendar + " 7d", "seven_day", sevenDay},
	} {
		seg := LimitSegment(window.label, limits.Obj(window.key), window.span, compact, now)
		if seg != "" {
			parts = append(parts, seg)
		}
	}
	return parts
}

// placeContext puts the context segment on whichever row can hold it, bar sized
// to fit, and hands both rows back.
//
// Row one is where it belongs, but only if row one can hold it with no bar at
// all. A narrow split or a long path puts it over the budget before a single
// cell of bar is added, and then it goes to the meter row rather than being
// truncated: what truncation eats is the end of the row, the session id.
func placeContext(cw payload.Map, identity, tail, meters []string, width int) ([]string, []string) {
	bare := ContextSegment(cw, 0)
	if bare == "" {
		return identity, meters
	}
	if width != 0 {
		full := append(append(append([]string{}, identity...), bare), tail...)
		if rowWidth(full) > width-margin {
			return identity, append([]string{fitted(cw, meters, 0, width)}, meters...)
		}
	}
	others := append(append([]string{}, identity...), tail...)
	return append(identity, fitted(cw, others, len(identity), width)), meters
}

// compose is the two rows as lists of parts, before they are joined.
func compose(data payload.Map, home string, width int, compact bool, now time.Time) [][]string {
	var identity []string
	for _, part := range append([]string{modelPart(data)}, pathParts(data, home)...) {
		if part != "" {
			identity = append(identity, part)
		}
	}
	var tail []string
	if part := sessionPart(data); part != "" {
		tail = append(tail, part)
	}
	meters := limitParts(data, compact, now)

	identity, meters = placeContext(data.Obj("context_window"), identity, tail, meters, width)
	return [][]string{append(identity, tail...), meters}
}

// Build is the two rows, composed from segments that each render themselves.
//
// Claude Code renders each printed line as its own row. The context window is
// the meter watched constantly while working, so it rides the top row with the
// model and the path, and its bar is given every column the row has left over.
// The rate-limit windows are the infrequent detail and go below.
//
// The session id closes the top row, which puts the bar between two fixed
// things and makes "the rest of the line" an arithmetic question rather than a
// guess: render the row with an empty bar, measure it, and the bar is the
// difference.
//
// A row with nothing in it is omitted rather than printed blank: early in a
// session the context and rate-limit fields are all absent, and a stray empty
// row looks like a fault.
func Build(data payload.Map, home string, width int, now time.Time) []string {
	rows := compose(data, home, width, false, now)
	// The gauges give way to the percentages they draw rather than letting the
	// row run past the budget, which is the rule the context bar follows too.
	// Measured after composing rather than before, because the context segment
	// relocates onto this row when row one cannot hold it, and that is exactly
	// the case where the row is too long.
	if width != 0 && len(rows[1]) > 0 && rowWidth(rows[1]) > width-margin {
		rows = compose(data, home, width, true, now)
	}

	var joined []string
	for _, row := range rows {
		if len(row) > 0 {
			joined = append(joined, strings.Join(row, sep()))
		}
	}
	return alignCost(rail(joined), data, width)
}

// costForms is the cost segment at full length and shortened, from one read.
//
// Both forms come from the same totals because working them out means tailing a
// file, and doing that twice to decide which of two strings fits would double
// the only expensive thing on the row.
func costForms(data payload.Map, transcripts string) (string, string) {
	figures, ok := pricing.Price(usage.Sum(data.Str("session_id"), transcripts))
	if !ok {
		return "", ""
	}
	// A trailing plus says the session ran a model the table has no rate for,
	// so the figure is a floor rather than a total.
	total := money + " " + pricing.Money(figures.Spent)
	if !figures.Complete {
		total += "+"
	}
	if figures.Saved <= 0 {
		return total, total
	}
	full := total + sep() + bullseye + " " + dim("saved") + " " + pricing.Money(figures.Saved)
	return full, total
}

// alignCost pushes the cost to the right of the meter row, shortening it or
// dropping it.
//
// It goes on the meter row rather than the identity row because the identity
// row has already given its slack to the context bar. With no meter row there
// is nowhere for it that is not somewhere else's space, so it is dropped.
func alignCost(lines []string, data payload.Map, width int) []string {
	if len(lines) < 2 || width == 0 {
		return lines
	}
	last := len(lines) - 1
	room := width - margin - VisibleWidth(lines[last])
	long, short := costForms(data, "")
	for _, segment := range []string{long, short} {
		gap := room - VisibleWidth(segment)
		if segment != "" && gap >= costGutter {
			lines[last] += strings.Repeat(" ", gap) + segment
			break
		}
	}
	return lines
}

// Main reads the session JSON on stdin and writes the rows.
//
// It exits 0 whatever it is given. Unparseable input is not worth a traceback
// in the status bar, and a status line that fails shows nothing at all.
func Main(stdin io.Reader, stdout io.Writer) int {
	raw, err := io.ReadAll(stdin)
	if err != nil {
		return 0
	}
	var data payload.Map
	if json.Unmarshal(raw, &data) != nil || data == nil {
		return 0
	}

	// Guarded inside Write, and called before the render so a payload the bar
	// cannot draw still leaves its numbers on disk.
	state.Write(data, time.Now())

	home, err := os.UserHomeDir()
	if err != nil {
		home = ""
	}
	// STOP AT THE FIRST FAILED WRITE rather than discarding the error. There is
	// nowhere to report it, since stdout is the thing that failed and the exit
	// code is 0 by contract, but a second row written after the first failed is
	// a torn status line rather than a missing one, and torn is harder to read
	// as broken.
	for _, row := range Build(data, home, TerminalWidth(), time.Now()) {
		if _, err := fmt.Fprintln(stdout, row); err != nil {
			break
		}
	}
	return 0
}
