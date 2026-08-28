package main_test

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
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

// COVERS: FR-1.9 | regression
//
// REACHED THROUGH A SYMLINK, the shim must resolve to where the REAL file is
// and find the binary there.
//
// Claude Code reaches the status line through `silo/bin/statusline`, a symlink
// to `infobot/bin/infobot`. `dirname "$0"` on the symlink's own path gives
// silo/bin, so the binary the shim looked for was silo/bin/statusline, which is
// the symlink, which is the shim. It exec'd itself.
//
// That ran for 35 minutes across ten sessions on 2026-08-28 and the only
// visible symptom was every session's state file ceasing to update. The loop
// never reaches the render, so nothing is printed, and a status line printing
// nothing is indistinguishable from a quiet one.
//
// The Python this replaced called Path(__file__).resolve(), which follows
// symlinks. The port dropped it without noticing it was load-bearing.
func TestShimReachedThroughASymlinkDoesNotExecItself(t *testing.T) {
	real, code := shimIn(t, "infobot", true)
	if code != 0 || !strings.Contains(real, "RAN-THE-BINARY") {
		t.Fatalf("direct call already broken: %q", real)
	}

	// A symlink to the shim, in a directory holding nothing else, which is the
	// shape silo/bin has.
	root, err := filepath.Abs("../..")
	if err != nil {
		t.Fatal(err)
	}
	source, err := os.ReadFile(filepath.Join(root, "bin", "infobot"))
	if err != nil {
		t.Fatal(err)
	}
	home := t.TempDir()
	shim := filepath.Join(home, "infobot")
	if err := os.WriteFile(shim, source, 0o755); err != nil {
		t.Fatal(err)
	}
	stub := "#!/bin/sh\ncat >/dev/null\necho RAN-THE-BINARY\n"
	if err := os.WriteFile(filepath.Join(home, "statusline"), []byte(stub), 0o755); err != nil {
		t.Fatal(err)
	}

	elsewhere := t.TempDir()
	link := filepath.Join(elsewhere, "statusline")
	if err := os.Symlink(shim, link); err != nil {
		t.Fatal(err)
	}

	cmd := exec.Command(link)
	cmd.Stdin = strings.NewReader(`{"session_id":"symlink-check"}`)
	// A loop would run until this fires rather than failing, so the timeout is
	// the assertion as much as the output is.
	done := make(chan struct{})
	var out []byte
	var runErr error
	go func() {
		out, runErr = cmd.Output()
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(10 * time.Second):
		_ = cmd.Process.Kill()
		t.Fatal("the shim did not terminate: it is exec'ing itself through the symlink")
	}
	if runErr != nil {
		var exit *exec.ExitError
		if !asExitError(runErr, &exit) {
			t.Fatal(runErr)
		}
	}
	if !strings.Contains(string(out), "RAN-THE-BINARY") {
		t.Errorf("through a symlink the shim produced %q, want the binary beside the real file", out)
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
