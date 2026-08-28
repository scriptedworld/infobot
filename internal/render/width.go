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
const askTimeout = 2 * time.Second

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
func ask(argv ...string) string {
	ctx, cancel := context.WithTimeout(context.Background(), askTimeout)
	defer cancel()
	out, err := exec.CommandContext(ctx, argv[0], argv[1:]...).Output()
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(out))
}

// tmuxWidth is the pane columns from tmux, measured at 3.2ms and affordable
// once per render.
func tmuxWidth() int {
	if os.Getenv("TMUX") == "" {
		return 0
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
		return layout.Area.Width
	}
	return layout.Panes[index].Rect.Width
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
