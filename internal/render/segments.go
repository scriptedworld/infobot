package render

import (
	"fmt"
	"strings"
	"time"

	"github.com/scriptedworld/infobot/internal/num"
	"github.com/scriptedworld/infobot/internal/payload"
	"github.com/scriptedworld/infobot/internal/state"
)

// margin is what Claude Code keeps for itself, so the pane width is NOT the
// budget.
//
// 3 IS THE DEFAULT AND IS KNOWN TO BE TOO SMALL HERE. It assumes the host
// indents two columns and keeps one at the right. Measured 2026-09-05 by
// screenshotting the terminal: at a real 313 columns the line rendered 309 and
// Claude Code cut BOTH rows with its own ellipsis, losing the end of the
// session id and the saved figure. 8 renders complete.
//
// The default stays 3 rather than moving to 8, and that is deliberate. Raising
// it breaks TestCostShortensThenDropsAsTheRowNarrows at width 79, which asserts
// the cost never recovers a form it has already surrendered as the pane
// narrows. That is a REAL non-monotonicity in the layout, latent at 3 and
// exposed at 8, and papering over it by editing the test would hide a bug the
// test exists to catch. Filed rather than fixed; this machine sets 8 in
// layout.json, which is what configuration is for.
//
// A var, not a const: ~/.config/infobot/layout.json overrides it. The number is
// a claim about the HOST'S CHROME, which this process cannot measure from the
// inside, and being wrong by a column truncates every render. Configuration is
// what lets it be dialled against what is actually drawn rather than rebuilt
// against a guess, and it is why the wrong value survived as long as it did.
// See layout_config.go.
var margin = 3 //nolint:gochecknoglobals // overridden by layout.json at startup

const (
	brain     = "🧠"
	hourglass = "⏳"
	calendar  = "📅"
	money     = "💵"
	bullseye  = "🎯"
	rootGlyph = "⌂"
)

// Powerline thin separator from Iosevka Nerd Font, which kitty is configured
// with. A plain vertical bar is the fallback anywhere the glyph is missing, one
// column either way, so nothing shifts.
const sepGlyph = ""

// The left rail, out of oh-my-zsh's multiline prompt: a rounded corner opening
// the block, a rounded one closing it, and a tee for any row between. It says
// the two rows are one block rather than two neighbours, which matters when the
// row above is full width and the row below is half of it.
//
// A lone row gets the stub instead. An opening corner with no closing corner
// under it reads as a block that failed to finish.
const (
	railTop = "╭─ "
	railMid = "├─ "
	railEnd = "╰─ "
	railOne = "╶─ "
)

// The glyphs Claude Code draws in its own compaction meter, so the two read as
// one instrument. They carry no sub-cell steps, so every bit of resolution
// comes from the cell count, which is why the bar takes whatever columns row
// one has left.
const (
	filled = "▰"
	empty  = "▱"
)

const (
	// barFallback is used when the width is unknown, so there is nothing to
	// subtract from and 50 is a chosen number rather than a fit.
	barFallback = 50
	// barMin is the width below which the bar is DROPPED rather than clamped
	// to. Clamping overflows: in a 120-column pane a long path leaves six
	// columns, so an eight-cell floor wraps the row, costing the whole line to
	// save a bar that at 12% a cell was not saying much.
	barMin = 8
	// windowCells is fixed rather than a share of the slack. The rate limit
	// windows are checked occasionally, not watched, so 10% a cell is enough
	// resolution, and a fixed width keeps the row from moving under them as the
	// context bar above grows.
	windowCells = 10
	// readingMin is the width at which the percentage is printed beside each
	// gauge. Below it the row renders as it always has: the number is declined
	// where there is no room to hold it, rather than shown and then truncated.
	readingMin = 80
	// herdrTrim is how much of herdr's reported pane width is not actually
	// drawable. Set from measurement rather than derived: at the full reported
	// figure a wide row overshoots and the host truncates it.
	herdrTrim = 10
	// costGutter is the clear space kept between the meter row and the cost
	// pushed to its right, so the two read as separate things.
	costGutter = 3
)

// The window lengths come from the payload's own field names.
const (
	fiveHour = 5 * 3600
	sevenDay = 7 * 86400
)

const (
	// paceConfident is how much of a window has to elapse before its verdict is
	// shown in full. A fraction rather than a duration, so the seven day window
	// matures at the same point in its own life instead of after an afternoon.
	paceConfident = 0.6
	// paceMinElapsed is an arithmetic floor only, to divide by. The fade stops
	// a wild early projection from being believed; this stops it being infinite.
	paceMinElapsed = 0.01
)

func sep() string {
	if plain() {
		return " " + sepGlyph + " "
	}
	return " " + sepColour + sepGlyph + reset + " "
}

// Bar is a proportional bar whose fill fades along the ramp, cell by cell.
//
// Each cell is coloured for the percentage IT stands for, not for the bar's
// total: the first is green because it means 2%, and the last filled one
// matches the colour of the number beside it because they mean the same thing.
// So the fade is a fixed property of the bar and only its length moves.
//
// The bar is never cut into. The percentage is printed with the counts, where
// it can be read as a number rather than found among the parallelograms.
//
// A tint overrides the fade and paints every filled cell one colour. The rate
// limit gauges use it to mean something the fade cannot: not where each cell
// sits, but whether the whole reading is a problem.
func Bar(pct float64, cells int, tint string) string {
	pct = num.Clamp(pct, 0, 100)
	if cells <= 0 {
		return ""
	}
	full := num.RoundInt(pct / 100 * float64(cells))

	if plain() {
		return strings.Repeat(filled, full) + strings.Repeat(empty, cells-full)
	}

	// One escape per RUN of cells sharing a style, not one per cell. Below the
	// ramp's pivot a forty-cell bar resolves to a handful of distinct shades,
	// so this is most of the escapes saved for nothing given up.
	var out strings.Builder
	held := ""
	for i := 0; i < cells; i++ {
		style, glyph := emptyColour, empty
		if i < full {
			glyph = filled
			style = tint
			if style == "" {
				style = ramp(float64(i+1) / float64(cells) * 100)
			}
		}
		if style != held {
			// RESET first, always. The alarm style carries bold and a
			// background as well as a colour, so a bare colour change after it
			// leaves both switched on for the rest of the line.
			out.WriteString(reset + style)
			held = style
		}
		out.WriteString(glyph)
	}
	out.WriteString(reset)
	return out.String()
}

// rail hangs the rows off a left rail, corners rounded.
//
// Drawn in the same dimcyan as the bar's empty cells rather than in its own
// colour, so the frame stays one voice with the meter and neither competes with
// the numbers.
func rail(rows []string) []string {
	if len(rows) == 0 {
		return rows
	}
	leads := make([]string, len(rows))
	if len(rows) == 1 {
		leads[0] = railOne
	} else {
		for i := range leads {
			switch i {
			case 0:
				leads[i] = railTop
			case len(rows) - 1:
				leads[i] = railEnd
			default:
				leads[i] = railMid
			}
		}
	}
	out := make([]string, len(rows))
	for i, row := range rows {
		if plain() {
			out[i] = leads[i] + row
		} else {
			out[i] = emptyColour + leads[i] + reset + row
		}
	}
	return out
}

// Tokens prints compact token counts. 142000 becomes 142k, 1000000 becomes 1.0M.
func Tokens(n float64) string {
	if n >= 1_000_000 {
		return fmt.Sprintf("%.1fM", n/1_000_000)
	}
	if n >= 1_000 {
		return fmt.Sprintf("%.0fk", n/1_000)
	}
	return fmt.Sprintf("%.0f", n)
}

// seconds is the clock as a float, matching time.time(). Truncating to whole
// seconds first shifts the remaining time by up to a second, which is enough to
// move a countdown across a minute boundary and print a different string.
func seconds(t time.Time) float64 { return float64(t.UnixNano()) / 1e9 }

// Countdown is the time until reset, as the largest two units that are
// non-zero.
//
// Empty when the timestamp is missing or already past. A countdown reading "0m"
// suggests a reset is imminent when it has already happened and the number is
// simply stale.
func Countdown(resetsAt float64, now time.Time) string {
	if resetsAt == 0 {
		return ""
	}
	remaining := int64(resetsAt - seconds(now))
	if remaining <= 0 {
		return ""
	}
	days, rest := remaining/86400, remaining%86400
	hours, rest := rest/3600, rest%3600
	minutes := rest / 60
	if days != 0 {
		return fmt.Sprintf("%dd%dh", days, hours)
	}
	if hours != 0 {
		return fmt.Sprintf("%dh%02dm", hours, minutes)
	}
	return fmt.Sprintf("%dm", minutes)
}

func homeRelative(path, home string) string {
	if path == home {
		return "~"
	}
	if home != "" && strings.HasPrefix(path, home+"/") {
		return "~" + path[len(home):]
	}
	return path
}

// join puts the non-empty parts together with a single space, so dropping the
// bar drops its separating space with it instead of leaving a gap the emoji
// makes conspicuous.
func join(parts ...string) string {
	kept := parts[:0]
	for _, part := range parts {
		if part != "" {
			kept = append(kept, part)
		}
	}
	return strings.Join(kept, " ")
}

// ContextSegment is used/total and a percentage, or nothing if the numbers are
// not there yet.
//
// cells of 0 drops the bar and keeps the numbers, which is both what a pane too
// narrow to carry a bar gets and how the row measures the room for one.
//
// The WHOLE span is coloured here, while the rate-limit segments colour only
// their number. That asymmetry is deliberate: the context window is watched
// constantly while working and the other two are infrequent details, so a wider
// block of colour makes the loud one loud.
func ContextSegment(cw payload.Map, cells int) string {
	// One formula, shared with the file the state package leaves behind, so a
	// bar reading 56% cannot sit beside a file saying something else.
	window, ok := state.Figures(cw)
	if !ok {
		return ""
	}
	// The percentage sits with the counts, where it reads as one measurement
	// rather than as a number to be found inside the picture of it.
	counts := fmt.Sprintf("%s/%s (%.0f%% consumed)",
		Tokens(window.Used), Tokens(window.Size), window.Percent)
	return join(brain, Bar(window.Percent, cells, ""), colour(window.Percent, counts))
}

// rowWidth is the columns a row costs: its parts joined the way they will be
// joined, so the separators between them count, and the rail it hangs off.
func rowWidth(parts []string) int {
	return VisibleWidth(strings.Join(parts, sep())) + VisibleWidth(railTop)
}

// fitted is the context segment with its bar filling whatever the row leaves
// over.
//
// others is the rest of the row and at is where this segment sits in it, so
// what gets measured is the line as it will be printed rather than a sum of
// pieces that forgets the separators.
//
// One pass, because the rest of the segment is the same width whatever the bar
// does: one more cell is one more column and nothing else moves.
func fitted(cw payload.Map, others []string, at, width int) string {
	if width == 0 {
		// NO BAR WHEN THE WIDTH IS UNKNOWN. A bar is a claim about how much
		// room there is, and with nothing to fit against, any length is a
		// guess that the host then truncates. The numbers say the same thing
		// and cost a known handful of columns, so the row stays short and
		// left-aligned instead of being cut.
		return ContextSegment(cw, 0)
	}
	// The bar also brings the space that separates it from the counts, which
	// the bar-less form measured here does not have.
	bare := ContextSegment(cw, 0)
	row := make([]string, 0, len(others)+1)
	row = append(row, others[:at]...)
	row = append(row, bare)
	row = append(row, others[at:]...)

	spare := width - margin - rowWidth(row) - 1
	if spare < barMin {
		return bare
	}
	return ContextSegment(cw, spare)
}

// elapsedFraction is how far through the window we are, and whether that is
// knowable at all.
func elapsedFraction(resetsAt float64, span int, now time.Time) (float64, bool) {
	if resetsAt == 0 || span == 0 {
		return 0, false
	}
	remaining := resetsAt - seconds(now)
	if remaining <= 0 {
		return 0, false
	}
	return num.Clamp(1-remaining/float64(span), 0, 1), true
}

// LimitSegment is a gauge and a countdown. No token counts exist for these
// windows.
//
// label carries the emoji and the name together because they are one fixed
// string at both call sites.
//
// The percentage is drawn AND printed. The bar is the same one the context
// window uses, on the same ramp, so 80% is the same shade wherever it appears
// and one glance reads all three meters.
//
// The number is there because a bar stops discriminating exactly where it
// matters most: near its end, one cell covers several points, so 91% and 99%
// draw the same and the reading is least useful when the decision is most
// expensive. The bar answers "roughly where am I" at a glance and the number
// answers "how bad is it" when the glance is not enough.
//
// The countdown stays as a number because a bar cannot carry it, and because it
// is what makes the gauge actionable: half spent with four hours to go reads
// very differently from half spent with ten minutes to go.
//
// It is deliberately UNCOLOURED. It wants its own scale and probably an
// inverted one, running TOWARD green as it nears zero, since a reset getting
// closer is good news. Undecided, so left plain rather than guessed at.
func LimitSegment(label string, window payload.Map, span int, compact, reading bool, now time.Time) string {
	if window == nil {
		return ""
	}
	pct, given := window.Num("used_percentage")
	if !given {
		return ""
	}
	resetsAt := window.Count("resets_at")
	// Concern where it can be worked out, raw spend where it cannot: with no
	// reset time there is no window position, so the gauge falls back to
	// meaning what the context meter's colour means.
	tint := ramp(pct)
	if elapsed, ok := elapsedFraction(resetsAt, span, now); ok {
		tint = paceTint(pct, elapsed)
	}
	// The `@` is inside the tint, so the reading is one coloured token rather
	// than a plain sigil against a coloured number. It carries the same colour
	// as the bar it stands beside, which is what says the two mean one thing.
	number := tinted(fmt.Sprintf("@%.0f%%", pct), tint)
	gauge := Bar(pct, windowCells, tint)
	if reading {
		// Two spaces, not one. The bar's last cell and the number mean the same
		// thing and are the same colour, so a single space reads as one run and
		// the eye does not find the boundary.
		gauge += "  " + number
	}
	if compact {
		gauge = number
	}
	return join(label, gauge, Countdown(resetsAt, now))
}
