package render

import (
	"context"
	"encoding/json"
	"os"
	"os/exec"
	"regexp"
	"strings"
	"time"
	"unicode"

	"golang.org/x/text/width"
)

// askTimeout bounds the wait on a host. A hung multiplexer must not hang a line
// that renders on every event.
const (
	askTimeout = 2 * time.Second
	// waitDelay bounds how long Wait may block on inherited pipes after the
	// context has killed the process. Without it the bound is the grandchild.
	waitDelay = 250 * time.Millisecond
)

var ansi = regexp.MustCompile("\033\\[[0-9;]*m")

// VisibleWidth is the columns a rendered string occupies, escapes and all.
//
// Sizing the bar by subtraction only works if the subtrahend is what the
// terminal will actually draw. len() is not that: it counts every byte of an
// escape sequence as a column and the emoji as one column when they take two.
// Both errors run toward a bar too wide for the line.
//
// Ambiguous-width characters, the parallelograms among them, are counted as
// one, which is what kitty draws them as. A terminal configured to treat
// ambiguous as wide would need this changed, and would double the bar.
func VisibleWidth(text string) int {
	total := 0
	for _, r := range ansi.ReplaceAllString(text, "") {
		total += runeWidth(r)
	}
	return total
}

func runeWidth(r rune) int {
	if unicode.In(r, unicode.Mn, unicode.Me) {
		return 0
	}
	switch width.LookupRune(r).Kind() {
	case width.EastAsianWide, width.EastAsianFullwidth:
		return 2
	default:
		return 1
	}
}

// TerminalWidth is the columns, or 0 when it genuinely cannot be known.
//
// Every ordinary route fails here. Claude Code captures stdout, so the standard
// descriptors all fail; COLUMNS is unset; /dev/tty is "No such device or
// address"; and any library terminal-size call therefore returns its fabricated
// 80x24 fallback.
//
// THAT FALLBACK IS THE TRAP. Believing it would truncate a 223-column pane to
// 80, worse than not adapting at all. So it is never used: whatever owns the
// pane is asked directly, and a host that cannot be asked returns 0, meaning
// "render the full form and let the caller fit it".
//
// The hosts are tried INNERMOST FIRST. tmux running inside a herdr pane draws
// this line in the tmux pane, which is the narrower of the two, so tmux answers
// whenever it is there and herdr answers when it is not. Each route costs one
// subprocess of a few milliseconds and only the winning one runs.
func TerminalWidth() int {
	if w := tmuxWidth(); w != 0 {
		return w
	}
	return herdrWidth()
}

// ask puts a question to a host and hands back its stdout, trimmed. An empty
// answer means the host could not be asked, which is a rendering decision
// rather than an error.
//
// WaitDelay IS THE HALF THAT ACTUALLY BOUNDS IT. The context kills the process
// it started, and that is not enough: a host is a script, and killing the shell
// leaves any child it spawned holding the inherited stdout pipe. Output() then
// blocks reading that pipe until the GRANDCHILD exits, so a two second timeout
// waited thirty against a host that ran `sleep 30`. WaitDelay closes the pipes
// a beat after the kill and returns.
//
// The line renders on every Claude Code event, so an unbounded wait here is the
// whole status line hanging on a multiplexer that is already in trouble.
func ask(argv ...string) string {
	ctx, cancel := context.WithTimeout(context.Background(), askTimeout)
	defer cancel()
	command := exec.CommandContext(ctx, argv[0], argv[1:]...)
	command.WaitDelay = waitDelay
	out, err := command.Output()
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(out))
}

// tmuxWidth is the pane columns from tmux, measured at 3.2ms and affordable
// once per render.
//
// TARGET THE CALLING PANE. An untargeted `display-message` resolves against the
// ACTIVE pane of the current client, not the pane whose process is asking. In a
// split those are different panes, so the line was fitted to whichever pane
// happened to be focused when the render fired. Measured 2026-09-01: called
// from pane %5 at 257 columns with a 60-column %6 focused, the untargeted form
// answered 60 and `-t %5` answered 257.
//
// Focusing a WIDER pane is the damaging direction. The row is then built past
// the edge and the host cuts its tail, which is the end of the meter row, and
// the line re-renders on a ten second interval so no interaction is needed for
// it to happen. This is FR-3.7 for tmux: the width is the one the pane is DRAWN
// at, not whatever a rectangle elsewhere reports.
//
// TMUX_PANE is set in every pane's environment and Claude Code passes it
// through, which is what makes the target available. Without it there is no
// better question to ask than the old one, so the untargeted form stays as the
// fallback rather than the route returning unknown.
func tmuxWidth() int {
	if os.Getenv("TMUX") == "" {
		return 0
	}
	if pane := os.Getenv("TMUX_PANE"); pane != "" {
		return atoi(ask("tmux", "display-message", "-p", "-t", pane, "#{pane_width}"))
	}
	return atoi(ask("tmux", "display-message", "-p", "#{pane_width}"))
}

// herdrLayout is the shape of `herdr pane layout --current`, which is the only
// command carrying a rectangle: `pane current` and `pane get` describe the pane
// in full and never say how wide it is.
type herdrLayout struct {
	Result struct {
		Layout struct {
			Area          struct{ Width int } `json:"area"`
			FocusedPaneID string              `json:"focused_pane_id"`
			Zoomed        bool                `json:"zoomed"`
			Panes         []struct {
				PaneID string              `json:"pane_id"`
				Rect   struct{ Width int } `json:"rect"`
			} `json:"panes"`
		} `json:"layout"`
	} `json:"result"`
}

// herdrWidth is the pane columns from herdr, measured at 2-4ms, the same order
// as tmux.
//
// It answers for the CALLING PANE'S WHOLE TAB, so the pane has to be picked out
// of the list it returns, and HERDR_PANE_ID is what names it.
//
// A tab holding exactly one pane answers whatever id that pane carries. It is
// the one case where not matching the id costs nothing, because there is only
// one rectangle it could be, and it covers an id in the environment that no
// longer names the pane the process now sits in. Every other mismatch is
// reported unknown rather than guessed at.
//
// A ZOOMED PANE'S RECTANGLE IS THE UNZOOMED ONE. `zoomed` goes true and every
// rect in the reply stays exactly where it was, so a pane zoomed out of a
// two-way split reports half the columns it is drawn in. The tab's area is the
// width to use, and the zoomed pane is the focused one: zooming the neighbour
// moves focused_pane_id to it and leaves this pane hidden, which is the case
// where the unzoomed rectangle is right because it is what unzooming restores.
//
// HERDR_BIN_PATH is preferred over the name because it pins the version that
// owns this pane, and because PATH in a status line subprocess is whatever
// Claude Code inherited rather than whatever a shell would have built.
func herdrWidth() int {
	pane := os.Getenv("HERDR_PANE_ID")
	if pane == "" {
		return 0
	}
	binary := os.Getenv("HERDR_BIN_PATH")
	if binary == "" {
		binary = "herdr"
	}
	answer := ask(binary, "pane", "layout", "--current")
	if answer == "" {
		return 0
	}
	var reply herdrLayout
	if json.Unmarshal([]byte(answer), &reply) != nil {
		return 0
	}
	layout := reply.Result.Layout
	index := -1
	for i, p := range layout.Panes {
		if p.PaneID == pane {
			index = i
		}
	}
	if index < 0 {
		if len(layout.Panes) != 1 {
			return 0
		}
		index = 0
	}
	if layout.Zoomed && layout.Panes[index].PaneID == layout.FocusedPaneID {
		return usable(layout.Area.Width)
	}
	return usable(layout.Panes[index].Rect.Width)
}

// usable trims what herdr reports to what can actually be drawn into.
//
// herdr's rectangle is the pane it owns, and it is wider than the columns the
// line gets: the reported number overshoots by enough to push a full-width row
// past the edge, and the host then cuts the tail with an ellipsis.
//
// THE TWO ERRORS ARE NOT EQUALLY BAD, which is why this leans one way. A width
// read too small wastes a few columns and nobody ever notices. One read too
// large truncates, visibly, on every render. So the trim is deliberate rather
// than a fudge, and it is applied here rather than to every host because it is
// herdr's rectangle that is generous.
func usable(reported int) int {
	if reported <= herdrTrim {
		return 0
	}
	return reported - herdrTrim
}

func atoi(text string) int {
	total := 0
	for _, r := range text {
		if r < '0' || r > '9' {
			return 0
		}
		total = total*10 + int(r-'0')
	}
	if text == "" {
		return 0
	}
	return total
}
