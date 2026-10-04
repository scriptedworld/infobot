package render_test

import (
	"os"
	"path/filepath"
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

// COVERS FR-3.3, FR-3.4 | positive
//
// The bare terminal case: no multiplexer, and Claude Code hands the status line
// pipes rather than a terminal, so nothing among fd0, fd1, fd2, COLUMNS or
// /dev/tty carries a size. The terminal is still there, held by an ancestor,
// and walking to it is what makes a width available at all.
//
// In a bare kitty this process has no pts and `claude`, two hops up, holds
// /dev/pts/0; without the walk the line renders at width 0.
func TestBareTerminalIsFoundThroughAnAncestor(t *testing.T) {
	if ancestorTTY(t) == "" {
		t.Skip("no terminal in this process tree; nothing to find")
	}
	noHosts(t)
	if got := render.TerminalWidth(); got <= 0 {
		t.Errorf("TerminalWidth = %d, want the terminal an ancestor holds", got)
	}
}

// process adds pid to a fake process table: its stat line, and its standard
// descriptors as links whose text is what the kernel would report.
func process(t *testing.T, proc string, pid int, stat string, fds map[string]string) {
	t.Helper()
	dir := filepath.Join(proc, strconv.Itoa(pid))
	if err := os.MkdirAll(filepath.Join(dir, "fd"), 0o750); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "stat"), []byte(stat), 0o600); err != nil {
		t.Fatal(err)
	}
	for fd, target := range fds {
		if err := os.Symlink(target, filepath.Join(dir, "fd", fd)); err != nil {
			t.Fatal(err)
		}
	}
}

// fakeTerminals is a pts directory holding name as a plain file, which opens
// like a terminal and has no window size.
func fakeTerminals(t *testing.T, name string) string {
	t.Helper()
	pts := t.TempDir()
	if err := os.WriteFile(filepath.Join(pts, name), nil, 0o600); err != nil {
		t.Fatal(err)
	}
	return pts
}

// COVERS FR-3.3, FR-3.4 | edge
//
// The walk passes a process holding only pipes, finds a pts on its parent's
// stderr, and keeps walking when that pts reports no size, until init. A
// terminal with no size is not a width, so the answer is unknown rather than a
// guess. Built from a fake process table, so it runs where no terminal exists.
func TestTheWalkPassesAPtsWithNoSizeAndStopsAtInit(t *testing.T) {
	proc := t.TempDir()
	pts := fakeTerminals(t, "7")
	process(t, proc, 100, "100 (statusline) S 50 0 0", map[string]string{
		"0": "socket:[11]", "1": "pipe:[12]", "2": "/dev/null",
	})
	process(t, proc, 50, "50 (claude code) S 1 0 0", map[string]string{
		"0": "pipe:[13]", "2": filepath.Join(pts, "7"),
	})
	if got := render.AncestorTerminalWidth(proc, pts, 100); got != 0 {
		t.Errorf("width = %d, want 0 from a pts with no size", got)
	}
}

// COVERS FR-3.3 | negative
//
// A process table that cannot be read, a stat that does not parse, and a
// process that is its own parent each end the walk at unknown. The last is
// what the ancestor limit is for: the walk stops rather than loops.
func TestAnUnreadableTreeIsUnknown(t *testing.T) {
	pts := fakeTerminals(t, "7")
	missing := filepath.Join(t.TempDir(), "absent")
	if got := render.AncestorTerminalWidth(missing, pts, 100); got != 0 {
		t.Errorf("no process table: width = %d, want 0", got)
	}
	for _, stat := range []string{
		"100 no parenthesis at all",
		"100 (statusline)",
		"100 (statusline) S not-a-pid",
		"100 (statusline) S 100",
	} {
		proc := t.TempDir()
		process(t, proc, 100, stat, nil)
		if got := render.AncestorTerminalWidth(proc, missing, 100); got != 0 {
			t.Errorf("stat %q, no pts directory: width = %d, want 0", stat, got)
		}
		if got := render.AncestorTerminalWidth(proc, pts, 100); got != 0 {
			t.Errorf("stat %q: width = %d, want 0", stat, got)
		}
	}
}

// COVERS FR-3.4 | negative
//
// A host that is present but silent does not fall through to the terminal.
//
// This is the damaging direction and the reason the terminal route is guarded,
// not merely last. The terminal behind a pane is wider than the pane,
// so answering with it builds a row past the edge and the host cuts the tail
// on every ten-second render, with no interaction needed.
//
// Unknown costs the compact form, where overflow truncates every render.
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
