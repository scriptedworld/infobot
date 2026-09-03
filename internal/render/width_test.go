package render_test

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/scriptedworld/infobot/internal/render"
)

// fakeTmux puts an executable named `tmux` at the front of PATH and returns the
// file it records its arguments to.
//
// The reply is the same whatever it is asked, so an assertion here is about the
// QUESTION rather than the answer. That is the point: the bug this guards was a
// well-formed reply about the wrong pane, which no assertion on the number can
// catch.
func fakeTmux(t *testing.T, reply string) string {
	t.Helper()
	dir := t.TempDir()
	argv := filepath.Join(dir, "argv")
	script := "#!/bin/sh\nprintf '%s\\n' \"$*\" > " + argv + "\nprintf '%s\\n' " + reply + "\n"
	if err := os.WriteFile(filepath.Join(dir, "tmux"), []byte(script), 0o755); err != nil {
		t.Fatal(err)
	}
	t.Setenv("PATH", dir+string(os.PathListSeparator)+os.Getenv("PATH"))
	return argv
}

// fakeHost writes an executable that prints body on stdout, so the width
// routes can be exercised without a multiplexer.
func fakeHost(t *testing.T, body string) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "host")
	script := "#!/bin/sh\ncat <<'REPLY'\n" + body + "\nREPLY\n"
	if err := os.WriteFile(path, []byte(script), 0o755); err != nil {
		t.Fatal(err)
	}
	return path
}

func noHosts(t *testing.T) {
	t.Helper()
	t.Setenv("TMUX", "")
	t.Setenv("HERDR_PANE_ID", "")
	t.Setenv("HERDR_BIN_PATH", "")
}

// COVERS: FR-3.3, FR-3.4 | negative
//
// A host that cannot be asked yields unknown, which means render the full form.
// The fabricated 80x24 a library would return is never used: believing it would
// truncate a 223-column pane to 80, worse than not adapting at all.
func TestNoHostMeansUnknownRatherThanEighty(t *testing.T) {
	noHosts(t)
	if got := render.TerminalWidth(); got != 0 {
		t.Errorf("TerminalWidth = %d, want 0 for unknown", got)
	}
}

// COVERS: FR-3.7 | regression
//
// An untargeted `display-message` answers for the ACTIVE pane of the current
// client, not the pane that asked. Measured 2026-09-01 from pane %5 at 257
// columns with a 60-column %6 focused: the untargeted form said 60.
//
// Focusing a wider pane is the damaging direction, because the row is then
// built past the edge and the host cuts its tail. The assertion is on the
// argv rather than on the width: a reply about the wrong pane is well formed,
// so only the question distinguishes the two.
func TestTmuxIsAskedAboutTheCallingPaneNotTheActiveOne(t *testing.T) {
	noHosts(t)
	t.Setenv("TMUX", "/tmp/tmux-1000/default,1,0")
	t.Setenv("TMUX_PANE", "%5")
	argv := fakeTmux(t, "257")

	if got := render.TerminalWidth(); got != 257 {
		t.Errorf("TerminalWidth = %d, want the calling pane's 257", got)
	}
	asked, err := os.ReadFile(argv)
	if err != nil {
		t.Fatalf("tmux was never run: %v", err)
	}
	if !strings.Contains(string(asked), "-t %5") {
		t.Errorf("tmux was asked %q, want it targeted at the calling pane %%5", strings.TrimSpace(string(asked)))
	}
}

// COVERS: FR-3.3 | edge
//
// Without TMUX_PANE there is no better question than the old one, so the
// untargeted form stays as the fallback rather than the route going unknown.
// A width read from the active pane beats no width at all.
func TestTmuxWithoutAPaneIdStillAsks(t *testing.T) {
	noHosts(t)
	t.Setenv("TMUX", "/tmp/tmux-1000/default,1,0")
	t.Setenv("TMUX_PANE", "")
	argv := fakeTmux(t, "180")

	if got := render.TerminalWidth(); got != 180 {
		t.Errorf("TerminalWidth = %d, want 180 from the untargeted fallback", got)
	}
	asked, err := os.ReadFile(argv)
	if err != nil {
		t.Fatalf("tmux was never run: %v", err)
	}
	if strings.Contains(string(asked), "-t") {
		t.Errorf("tmux was asked %q, want no target when the pane id is unknown", strings.TrimSpace(string(asked)))
	}
}

// COVERS: FR-3.7 | property
//
// A ZOOMED PANE'S RECTANGLE IS THE UNZOOMED ONE. The tab's area is the width to
// use, and the zoomed pane is the focused one.
func TestZoomedPaneIsFittedToTheTabArea(t *testing.T) {
	noHosts(t)
	t.Setenv("HERDR_PANE_ID", "w4:p1")
	t.Setenv("HERDR_BIN_PATH", fakeHost(t, `{"result":{"layout":{
      "area":{"width":195},
      "focused_pane_id":"w4:p1",
      "zoomed":true,
      "panes":[{"pane_id":"w4:p1","rect":{"width":99}},
               {"pane_id":"w4:p2","rect":{"width":96}}]}}}`))
	if got := render.TerminalWidth(); got != 185 {
		t.Errorf("TerminalWidth = %d, want the tab area 195 less the 10-column trim, not the stale rect 99", got)
	}
}

// COVERS: FR-3.7 | edge
//
// Zooming the NEIGHBOUR moves focused_pane_id to it and leaves this pane
// hidden, which is the case where the unzoomed rectangle is right because it is
// what unzooming restores.
func TestUnfocusedPaneUnderZoomKeepsItsOwnRectangle(t *testing.T) {
	noHosts(t)
	t.Setenv("HERDR_PANE_ID", "w4:p1")
	t.Setenv("HERDR_BIN_PATH", fakeHost(t, `{"result":{"layout":{
      "area":{"width":195},
      "focused_pane_id":"w4:p2",
      "zoomed":true,
      "panes":[{"pane_id":"w4:p1","rect":{"width":99}},
               {"pane_id":"w4:p2","rect":{"width":96}}]}}}`))
	if got := render.TerminalWidth(); got != 89 {
		t.Errorf("TerminalWidth = %d, want this pane's own 99 less the 10-column trim", got)
	}
}

// COVERS: FR-3.3 | edge
//
// A tab holding exactly one pane answers whatever id that pane carries. It
// covers an id in the environment that no longer names the pane the process
// sits in, and it is the one case where not matching costs nothing.
func TestLonePaneAnswersEvenWhenTheIdDoesNotMatch(t *testing.T) {
	noHosts(t)
	t.Setenv("HERDR_PANE_ID", "stale:id")
	t.Setenv("HERDR_BIN_PATH", fakeHost(t, `{"result":{"layout":{
      "area":{"width":120},
      "focused_pane_id":"w1:p1",
      "zoomed":false,
      "panes":[{"pane_id":"w1:p1","rect":{"width":120}}]}}}`))
	if got := render.TerminalWidth(); got != 110 {
		t.Errorf("TerminalWidth = %d, want the lone pane's 120 less the 10-column trim", got)
	}
}

// COVERS: FR-3.3 | negative
//
// Every other mismatch is reported unknown rather than guessed at.
func TestMismatchedIdAmongSeveralPanesIsUnknown(t *testing.T) {
	noHosts(t)
	t.Setenv("HERDR_PANE_ID", "stale:id")
	t.Setenv("HERDR_BIN_PATH", fakeHost(t, `{"result":{"layout":{
      "area":{"width":195},
      "focused_pane_id":"w1:p1",
      "zoomed":false,
      "panes":[{"pane_id":"w1:p1","rect":{"width":99}},
               {"pane_id":"w1:p2","rect":{"width":96}}]}}}`))
	if got := render.TerminalWidth(); got != 0 {
		t.Errorf("TerminalWidth = %d, want 0 rather than a guess", got)
	}
}

// COVERS: FR-3.3 | negative
func TestUnreadableHostReplyIsUnknown(t *testing.T) {
	for _, reply := range []string{"", "not json", "{}", `{"result":{}}`} {
		t.Run(reply, func(t *testing.T) {
			noHosts(t)
			t.Setenv("HERDR_PANE_ID", "w1:p1")
			t.Setenv("HERDR_BIN_PATH", fakeHost(t, reply))
			if got := render.TerminalWidth(); got != 0 {
				t.Errorf("TerminalWidth = %d, want 0", got)
			}
		})
	}
}

// COVERS: FR-3.3 | positive
//
// tmux answers whenever it is there, because tmux inside a herdr pane is the
// one this line is drawn in and it is the narrower of the two.
func TestTmuxIsAskedInnermostFirst(t *testing.T) {
	noHosts(t)
	t.Setenv("TMUX", "/tmp/tmux-1000/default,123,0")
	tmux := fakeHost(t, "137")
	dir := filepath.Dir(tmux)
	if err := os.Rename(tmux, filepath.Join(dir, "tmux")); err != nil {
		t.Fatal(err)
	}
	t.Setenv("PATH", dir+string(os.PathListSeparator)+os.Getenv("PATH"))
	// herdr would answer differently, and must not be reached.
	t.Setenv("HERDR_PANE_ID", "w1:p1")
	t.Setenv("HERDR_BIN_PATH", fakeHost(t, `{"result":{"layout":{
      "area":{"width":999},"focused_pane_id":"w1:p1","zoomed":false,
      "panes":[{"pane_id":"w1:p1","rect":{"width":999}}]}}}`))

	if got := render.TerminalWidth(); got != 137 {
		t.Errorf("TerminalWidth = %d, want tmux's 137", got)
	}
}
