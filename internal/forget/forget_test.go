package forget_test

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/scriptedworld/infobot/internal/forget"
)

// COVERS: FR-1.11f | negative
//
// A session id names one file each. Anything else, including a path separator
// smuggled through the payload, is refused rather than joined onto a directory
// this walks with unlink.
func TestSessionIdCarryingAPathIsRefused(t *testing.T) {
	for _, bad := range []string{
		"",
		"..",
		"../../etc/passwd",
		"a/b",
		`a\b`,
		"..%2f",
		"ok/../../..",
	} {
		if forget.Named(bad) {
			t.Errorf("Named(%q) = true, want refused", bad)
		}
	}
	for _, good := range []string{
		"abcd-1234",
		"a5e58a4d-2a4e-4774-aa6c-1e7745721df6",
		"plain",
	} {
		if !forget.Named(good) {
			t.Errorf("Named(%q) = false, want accepted", good)
		}
	}
}

// COVERS: FR-1.11e | positive
func TestRemoveTakesBothFilesAndSaysWhich(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("XDG_STATE_HOME", dir)
	state := filepath.Join(dir, "infobot")
	if err := os.MkdirAll(state, 0o755); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"s.status.yaml", "s.json"} {
		if err := os.WriteFile(filepath.Join(state, name), []byte("x"), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	removed := forget.Remove("s")
	if len(removed) != 2 {
		t.Errorf("Remove reported %v, want both files", removed)
	}
	entries, err := os.ReadDir(state)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 0 {
		t.Errorf("%d files survived", len(entries))
	}
}

// COVERS: FR-1.11f | edge
//
// A cleanup that exits 0 having removed nothing is the same shape as a gate
// that passes having checked nothing, so it says which it was.
func TestNothingToRemoveIsSaidRatherThanImplied(t *testing.T) {
	t.Setenv("XDG_STATE_HOME", t.TempDir())
	var log strings.Builder
	code := forget.Main(strings.NewReader(`{"session_id":"never-existed"}`), &log)
	if code != 0 {
		t.Errorf("exit = %d, want 0", code)
	}
	if !strings.Contains(log.String(), "nothing to remove") {
		t.Errorf("log = %q, want it to say nothing was there", log.String())
	}
}

// COVERS: FR-1.11f | negative
func TestMainExitsZeroWhateverItIsGiven(t *testing.T) {
	t.Setenv("XDG_STATE_HOME", t.TempDir())
	for _, in := range []string{
		"", "not json", "[]", "null", "{}",
		`{"session_id":123}`,
		`{"session_id":"../escape"}`,
	} {
		var log strings.Builder
		if code := forget.Main(strings.NewReader(in), &log); code != 0 {
			t.Errorf("Main(%q) = %d, want 0", in, code)
		}
	}
}

// COVERS: FR-1.11f | negative
func TestRefusedIdIsReportedAndRemovesNothing(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("XDG_STATE_HOME", dir)
	state := filepath.Join(dir, "infobot")
	if err := os.MkdirAll(state, 0o755); err != nil {
		t.Fatal(err)
	}
	keep := filepath.Join(state, "innocent.status.yaml")
	if err := os.WriteFile(keep, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}

	var log strings.Builder
	forget.Main(strings.NewReader(`{"session_id":"../infobot/innocent"}`), &log)
	if !strings.Contains(log.String(), "refused") {
		t.Errorf("log = %q, want the refusal said out loud", log.String())
	}
	if _, err := os.Stat(keep); err != nil {
		t.Error("a refused id still reached a file")
	}
}
