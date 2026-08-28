package render_test

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/scriptedworld/infobot/internal/render"
)

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
	if got := render.TerminalWidth(); got != 195 {
		t.Errorf("TerminalWidth = %d, want the tab area 195, not the stale rect 99", got)
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
	if got := render.TerminalWidth(); got != 99 {
		t.Errorf("TerminalWidth = %d, want this pane's own 99", got)
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
	if got := render.TerminalWidth(); got != 120 {
		t.Errorf("TerminalWidth = %d, want the lone pane's 120", got)
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
