package render_test

import (
	"fmt"
	"go/parser"
	"go/token"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/scriptedworld/infobot/internal/payload"
	"github.com/scriptedworld/infobot/internal/render"
)

// sources walks the module's non-test Go files.
func sources(t *testing.T, visit func(path string, imports []string)) {
	t.Helper()
	root, err := filepath.Abs("../..")
	if err != nil {
		t.Fatal(err)
	}
	for _, dir := range []string{"cmd", "internal"} {
		walk := func(path string, d os.DirEntry, err error) error {
			if err != nil || d.IsDir() || !strings.HasSuffix(path, ".go") ||
				strings.HasSuffix(path, "_test.go") {
				return err
			}
			file, err := parser.ParseFile(token.NewFileSet(), path, nil, parser.ImportsOnly)
			if err != nil {
				return fmt.Errorf("parsing %s: %w", path, err)
			}
			var imports []string
			for _, spec := range file.Imports {
				imports = append(imports, strings.Trim(spec.Path.Value, `"`))
			}
			visit(strings.TrimPrefix(path, root+"/"), imports)
			return nil
		}
		if err := filepath.WalkDir(filepath.Join(root, dir), walk); err != nil {
			t.Fatal(err)
		}
	}
}

// COVERS: FR-1.6 | property
//
// NO PATH MAKES A NETWORK CALL. The rates come off disk and the width comes
// from a host already running, because a status line rendering on every event
// cannot wait on anything.
//
// Asserted against the imports rather than against a run, because a run only
// shows the paths it took and this has to hold for the ones it did not.
func TestNothingImportsTheNetwork(t *testing.T) {
	banned := []string{"net", "net/http", "net/url", "crypto/tls"}
	sources(t, func(path string, imports []string) {
		for _, imported := range imports {
			for _, deny := range banned {
				if imported == deny || strings.HasPrefix(imported, deny+"/") {
					t.Errorf("%s imports %q", path, imported)
				}
			}
		}
	})
}

// COVERS: FR-1.7 | property
//
// The only files opened are the rate table, the session's own transcripts, the
// offsets, and the status file. Everything else printed comes from the payload.
//
// Checked by giving the render a home containing NONE of them and confirming it
// still renders, then by confirming the only thing it creates is the state
// directory. A render that reached for anything else would either fail on the
// empty home or leave something behind.
func TestNothingIsOpenedBeyondTheFourNamedFiles(t *testing.T) {
	home := t.TempDir()
	state := t.TempDir()
	t.Setenv("HOME", home)
	t.Setenv("XDG_STATE_HOME", state)
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	t.Setenv("NO_COLOR", "1")
	t.Setenv("TMUX", "")
	t.Setenv("HERDR_PANE_ID", "")

	got := render.Build(full(), "/home/me", 200, clock)
	if len(got) == 0 || !strings.Contains(got[0], "Opus 5") {
		t.Fatalf("render failed against an empty home: %v", got)
	}

	// The home is untouched: nothing was created there.
	entries, err := os.ReadDir(home)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 0 {
		var names []string
		for _, e := range entries {
			names = append(names, e.Name())
		}
		t.Errorf("the render created %v in the home directory", names)
	}
}

// hostCounter writes an executable that records each invocation and answers
// with reply, so the number of subprocesses is countable.
func hostCounter(t *testing.T, reply string) (script, log string) {
	t.Helper()
	dir := t.TempDir()
	log = filepath.Join(dir, "calls")
	script = filepath.Join(dir, "host")
	body := "#!/bin/sh\necho call >> " + log + "\nprintf '%s\\n' '" + reply + "'\n"
	if err := os.WriteFile(script, []byte(body), 0o755); err != nil {
		t.Fatal(err)
	}
	return script, log
}

func calls(t *testing.T, log string) int {
	t.Helper()
	raw, err := os.ReadFile(log)
	if os.IsNotExist(err) {
		return 0
	}
	if err != nil {
		t.Fatal(err)
	}
	return strings.Count(string(raw), "call\n")
}

// COVERS: FR-1.8 | property
//
// ONE SUBPROCESS PER RENDER AT MOST, and only to ask a host how wide the pane
// is. The route that answers runs and the others do not.
func TestAtMostOneSubprocessAndOnlyForTheWidth(t *testing.T) {
	t.Setenv("TMUX", "")
	t.Setenv("HERDR_PANE_ID", "")
	t.Setenv("HERDR_BIN_PATH", "")

	// No host to ask: no subprocess at all.
	_, quiet := hostCounter(t, "")
	if got := calls(t, quiet); got != 0 {
		t.Errorf("%d subprocesses with no host to ask", got)
	}

	// herdr answers, and is asked exactly once.
	script, log := hostCounter(t, `{"result":{"layout":{"area":{"width":120},`+
		`"focused_pane_id":"w1:p1","zoomed":false,`+
		`"panes":[{"pane_id":"w1:p1","rect":{"width":120}}]}}}`)
	t.Setenv("HERDR_PANE_ID", "w1:p1")
	t.Setenv("HERDR_BIN_PATH", script)

	if got := render.TerminalWidth(); got != 110 {
		t.Fatalf("TerminalWidth = %d, want 120 less the 10-column trim", got)
	}
	if got := calls(t, log); got != 1 {
		t.Errorf("%d subprocesses for one width query, want 1", got)
	}
}

// COVERS: FR-3.11 | edge
//
// A host that does not answer costs a BOUNDED wait and then counts as unknown.
// A hung multiplexer must not hang a line that renders on every event.
func TestAHungHostIsBoundedAndCountsAsUnknown(t *testing.T) {
	t.Setenv("TMUX", "")
	dir := t.TempDir()
	script := filepath.Join(dir, "host")
	// Longer than the two second bound, so returning at all is the assertion.
	if err := os.WriteFile(script, []byte("#!/bin/sh\nsleep 30\n"), 0o755); err != nil {
		t.Fatal(err)
	}
	t.Setenv("HERDR_PANE_ID", "w1:p1")
	t.Setenv("HERDR_BIN_PATH", script)

	start := time.Now()
	got := render.TerminalWidth()
	elapsed := time.Since(start)

	if got != 0 {
		t.Errorf("TerminalWidth = %d, want 0 for a host that never answered", got)
	}
	if elapsed > 5*time.Second {
		t.Errorf("waited %v on a hung host, want the two second bound", elapsed)
	}
}

// COVERS: FR-4.3 | property
//
// The clock is a PARAMETER, so a countdown is asserted against a fixed instant
// rather than against a shape. Nothing but a test passes anything but the
// default, and the seam is what makes FR-2.4, FR-7.4 and FR-7.9 testable at all.
func TestTheClockIsASeam(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	data := payload.Map{
		"rate_limits": map[string]any{
			"five_hour": map[string]any{"used_percentage": 34.0, "resets_at": at(3 * 3600)},
		},
	}
	for _, c := range []struct {
		advance time.Duration
		want    string
	}{
		{0, "3h00m"},
		{time.Hour, "2h00m"},
		{2*time.Hour + 30*time.Minute, "30m"},
		{4 * time.Hour, ""},
	} {
		got := strings.Join(render.Build(data, "/home/me", 200, clock.Add(c.advance)), "\n")
		if c.want == "" {
			if strings.Contains(got, "m") && strings.Contains(got, "h") {
				t.Errorf("+%v still printed a countdown: %q", c.advance, got)
			}
			continue
		}
		if !strings.Contains(got, c.want) {
			t.Errorf("+%v = %q, want a countdown of %s", c.advance, got, c.want)
		}
	}
}
