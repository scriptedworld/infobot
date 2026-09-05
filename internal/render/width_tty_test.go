package render_test

import (
	"os"
	"strconv"
	"strings"
	"testing"

	"github.com/scriptedworld/infobot/internal/render"
)

// ancestorTTY is the terminal an ancestor of this test process holds, found the
// same way the code under test finds it, or "" when the tests are not running
// under one. CI has no terminal anywhere in the tree, so every test here has to
// skip rather than fail when this is empty.
func ancestorTTY(t *testing.T) string {
	t.Helper()
	for pid, hops := os.Getpid(), 0; pid > 1 && hops < 16; hops++ {
		for _, fd := range []string{"0", "1", "2"} {
			link, err := os.Readlink("/proc/" + strconv.Itoa(pid) + "/fd/" + fd)
			if err == nil && strings.HasPrefix(link, "/dev/pts/") {
				return link
			}
		}
		raw, err := os.ReadFile("/proc/" + strconv.Itoa(pid) + "/stat")
		if err != nil {
			return ""
		}
		closing := strings.LastIndex(string(raw), ")")
		if closing < 0 {
			return ""
		}
		fields := strings.Fields(string(raw)[closing+1:])
		if len(fields) < 2 {
			return ""
		}
		parent, err := strconv.Atoi(fields[1])
		if err != nil || parent <= 1 {
			return ""
		}
		pid = parent
	}
	return ""
}

// COVERS: FR-3.3, FR-3.4 | positive
//
// The bare terminal case: no multiplexer, and Claude Code hands the status line
// pipes rather than a terminal, so nothing among fd0, fd1, fd2, COLUMNS or
// /dev/tty carries a size. The terminal is still there, held by an ancestor,
// and walking to it is what makes a width available at all.
//
// Measured 2026-09-05 in a bare kitty: this process had no pts, `claude` two
// hops up held /dev/pts/0 at 313 columns, and the line had been rendering at
// width 0.
func TestBareTerminalIsFoundThroughAnAncestor(t *testing.T) {
	if ancestorTTY(t) == "" {
		t.Skip("no terminal in this process tree; nothing to find")
	}
	noHosts(t)
	if got := render.TerminalWidth(); got <= 0 {
		t.Errorf("TerminalWidth = %d, want the terminal an ancestor holds", got)
	}
}

// COVERS: FR-3.4 | negative
//
// A HOST THAT IS PRESENT BUT SILENT MUST NOT FALL THROUGH TO THE TERMINAL.
//
// This is the damaging direction and the reason the terminal route is guarded
// rather than merely last. The terminal behind a pane is WIDER than the pane,
// so answering with it builds a row past the edge and the host cuts the tail,
// on every render, ten seconds apart, with no interaction needed.
//
// Unknown costs the compact form once. Overflow costs a truncated line forever.
func TestAPresentHostThatCannotAnswerDoesNotBorrowTheTerminal(t *testing.T) {
	if ancestorTTY(t) == "" {
		t.Skip("no terminal in this process tree; the fallthrough cannot happen")
	}
	// tmux claimed, and no tmux binary answer available to the test.
	t.Setenv("TMUX", "/tmp/tmux-1000/default,1,0")
	t.Setenv("TMUX_PANE", "%99")
	t.Setenv("HERDR_PANE_ID", "")
	t.Setenv("HERDR_BIN_PATH", "")
	if got := render.TerminalWidth(); got != 0 {
		t.Errorf("TerminalWidth = %d, want 0: a claimed pane must not borrow "+
			"the terminal's width", got)
	}
}
