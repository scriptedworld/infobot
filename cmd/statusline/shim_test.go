package main_test

import (
	"context"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"testing/fstest"
	"time"
)

// stub stands in for a built binary and says that it ran.
const stub = "#!/bin/sh\ncat >/dev/null\necho RAN-THE-BINARY\n"

// committedShim is the shim as committed in bin/, read through a root on that
// directory.
func committedShim(t *testing.T, name string) []byte {
	t.Helper()
	bin, err := os.OpenRoot("../../bin")
	if err != nil {
		t.Fatal(err)
	}
	defer func() { _ = bin.Close() }()
	source, err := bin.ReadFile(name)
	if err != nil {
		t.Fatal(err)
	}
	return source
}

// executables writes each script into dir, runnable. os.CopyFS keeps the
// execute bits of the entries it copies, so nothing is chmodded afterwards.
func executables(t *testing.T, dir string, scripts map[string][]byte) {
	t.Helper()
	fixture := fstest.MapFS{}
	for name, body := range scripts {
		fixture[name] = &fstest.MapFile{Data: body, Mode: 0o755}
	}
	if err := os.CopyFS(dir, fixture); err != nil {
		t.Fatal(err)
	}
}

// shimIn copies the committed shim into a scratch directory, optionally with a
// binary beside it, and runs it. The shim resolves the binary from its own
// location, so a copy exercises exactly what the real path does.
func shimIn(t *testing.T, name string, withBinary bool) (string, int) {
	t.Helper()
	dir := t.TempDir()
	shim := filepath.Join(dir, name)
	scripts := map[string][]byte{name: committedShim(t, name)}
	if withBinary {
		target := map[string]string{"infobot": "statusline", "forget-session": "forget"}[name]
		scripts[target] = []byte(stub)
	}
	executables(t, dir, scripts)

	cmd := exec.CommandContext(t.Context(), shim) //nolint:gosec // the shim this test wrote
	cmd.Stdin = strings.NewReader(`{"session_id":"shim-check"}`)
	out, err := cmd.Output()
	code := 0
	var exit *exec.ExitError
	if err != nil {
		if !errors.As(err, &exit) {
			t.Fatal(err)
		}
		code = exit.ExitCode()
	}
	return string(out), code
}

// COVERS FR-1.13 | negative
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

// COVERS FR-3.8 | negative
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

// COVERS FR-1.9 | regression
//
// REACHED THROUGH A SYMLINK, the shim must resolve to where the REAL file is
// and find the binary there.
//
// Claude Code reaches the status line through a symlink in another directory
// to `infobot/bin/infobot`. `dirname "$0"` on the symlink's own path gives
// that directory, where the `statusline` it finds is the symlink, which is the
// shim, so a shim that does not resolve its own path execs itself.
//
// The loop never reaches the render, so nothing is printed, and a status line
// printing nothing is indistinguishable from a quiet one. The only visible
// symptom is every session's state file ceasing to update.
func TestShimReachedThroughASymlinkDoesNotExecItself(t *testing.T) {
	direct, code := shimIn(t, "infobot", true)
	if code != 0 || !strings.Contains(direct, "RAN-THE-BINARY") {
		t.Fatalf("direct call already broken: %q", direct)
	}

	// A symlink to the shim, in a directory holding nothing else, which is the
	// shape that directory has.
	home := t.TempDir()
	shim := filepath.Join(home, "infobot")
	executables(t, home, map[string][]byte{
		"infobot":    committedShim(t, "infobot"),
		"statusline": []byte(stub),
	})

	elsewhere := t.TempDir()
	link := filepath.Join(elsewhere, "statusline")
	if err := os.Symlink(shim, link); err != nil {
		t.Fatal(err)
	}

	// A loop would run until the deadline kills it rather than failing, so the
	// deadline is the assertion as much as the output is.
	ctx, cancel := context.WithTimeout(t.Context(), 10*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, link) //nolint:gosec // the shim this test wrote
	cmd.Stdin = strings.NewReader(`{"session_id":"symlink-check"}`)
	out, runErr := cmd.Output()
	if errors.Is(ctx.Err(), context.DeadlineExceeded) {
		t.Fatal("the shim did not terminate: it is exec'ing itself through the symlink")
	}
	if runErr != nil {
		var exit *exec.ExitError
		if !errors.As(runErr, &exit) {
			t.Fatal(runErr)
		}
	}
	if !strings.Contains(string(out), "RAN-THE-BINARY") {
		t.Errorf("through a symlink the shim produced %q, want the binary beside the real file", out)
	}
}

// COVERS FR-1.13 | positive
func TestPresentBinaryIsExecuted(t *testing.T) {
	out, code := shimIn(t, "infobot", true)
	if code != 0 {
		t.Errorf("exit = %d, want 0", code)
	}
	if !strings.Contains(out, "RAN-THE-BINARY") {
		t.Errorf("output = %q, want the binary to have been reached", out)
	}
}

// COVERS FR-1.11f | negative
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
