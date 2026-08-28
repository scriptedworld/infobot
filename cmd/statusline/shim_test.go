package main_test

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// shimIn copies the committed shim into a scratch directory, optionally with a
// binary beside it, and runs it. The shim resolves the binary from its own
// location, so a copy exercises exactly what the real path does.
func shimIn(t *testing.T, name string, withBinary bool) (string, int) {
	t.Helper()
	root, err := filepath.Abs("../..")
	if err != nil {
		t.Fatal(err)
	}
	source, err := os.ReadFile(filepath.Join(root, "bin", name))
	if err != nil {
		t.Fatal(err)
	}
	dir := t.TempDir()
	shim := filepath.Join(dir, name)
	if err := os.WriteFile(shim, source, 0o755); err != nil {
		t.Fatal(err)
	}
	if withBinary {
		target := map[string]string{"infobot": "statusline", "forget-session": "forget"}[name]
		stub := "#!/bin/sh\ncat >/dev/null\necho RAN-THE-BINARY\n"
		if err := os.WriteFile(filepath.Join(dir, target), []byte(stub), 0o755); err != nil {
			t.Fatal(err)
		}
	}

	cmd := exec.Command(shim)
	cmd.Stdin = strings.NewReader(`{"session_id":"shim-check"}`)
	out, err := cmd.Output()
	code := 0
	var exit *exec.ExitError
	if err != nil {
		if !asExitError(err, &exit) {
			t.Fatal(err)
		}
		code = exit.ExitCode()
	}
	return string(out), code
}

func asExitError(err error, target **exec.ExitError) bool {
	if e, ok := err.(*exec.ExitError); ok {
		*target = e
		return true
	}
	return false
}

// COVERS: FR-1.13 | negative
//
// A build that has not run and a symlink that dangles fail identically, as a
// blank line nobody is told about, and a status line has no other tell: it is
// drawn or it is not. So an absent binary SAYS SO, in the place the line would
// have been, and still exits 0 so Claude Code is not handed a failure.
func TestAbsentBinaryIsReportedRatherThanBlank(t *testing.T) {
	out, code := shimIn(t, "infobot", false)
	if code != 0 {
		t.Errorf("exit = %d, want 0", code)
	}
	if !strings.Contains(out, "infobot is not built") {
		t.Errorf("output = %q, want it to say the binary is absent", out)
	}
	if !strings.Contains(out, "go build") {
		t.Errorf("output = %q, want it to say how to fix it", out)
	}
}

// COVERS: FR-3.8 | negative
//
// NO_COLOR reaches the shim too. A hardcoded escape surviving the stripping of
// everything around it is what FR-3.8 exists to prevent, and this row is
// hardcoded by definition.
func TestAbsentBinaryHonoursNoColor(t *testing.T) {
	t.Setenv("NO_COLOR", "1")
	out, _ := shimIn(t, "infobot", false)
	if strings.Contains(out, "\033") {
		t.Errorf("escape survived NO_COLOR: %q", out)
	}
	if !strings.Contains(out, "infobot is not built") {
		t.Errorf("output = %q, want the message to survive too", out)
	}
}

// COVERS: FR-1.13 | positive
func TestPresentBinaryIsExecuted(t *testing.T) {
	out, code := shimIn(t, "infobot", true)
	if code != 0 {
		t.Errorf("exit = %d, want 0", code)
	}
	if !strings.Contains(out, "RAN-THE-BINARY") {
		t.Errorf("output = %q, want the binary to have been reached", out)
	}
}

// COVERS: FR-1.11f | negative
//
// The cleanup hook says nothing to the terminal when its binary is absent:
// there is no line to occupy and nobody is looking, because a hook runs as a
// session ends. What it leaves is the state files it did not remove, which is
// the condition itself rather than a report of it.
func TestAbsentCleanupBinaryIsSilentAndStillExitsZero(t *testing.T) {
	out, code := shimIn(t, "forget-session", false)
	if code != 0 {
		t.Errorf("exit = %d, want 0", code)
	}
	if out != "" {
		t.Errorf("output = %q, want nothing", out)
	}
}
