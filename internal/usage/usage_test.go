package usage_test

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/scriptedworld/infobot/internal/usage"
)

// record is one transcript line carrying usage.
func record(model string, fields string) string {
	return `{"message":{"model":"` + model + `","usage":{` + fields + `}}}`
}

// tree writes a fixture projects directory and returns its root.
func tree(t *testing.T, files map[string]string) string {
	t.Helper()
	root := t.TempDir()
	for name, body := range files {
		path := filepath.Join(root, name)
		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	return root
}

func scratch(t *testing.T) {
	t.Helper()
	t.Setenv("XDG_STATE_HOME", t.TempDir())
}

// COVERS: FR-8.2 | property
//
// Found by globbing for the session id, not by rebuilding the directory slug
// from the working directory: a session may have been started elsewhere.
func TestTranscriptsFindTheSessionByName(t *testing.T) {
	root := tree(t, map[string]string{
		"-some-other-slug/wanted.jsonl": record("m", `"input_tokens":1`),
		"-some-other-slug/other.jsonl":  record("m", `"input_tokens":1`),
	})
	got := usage.Transcripts("wanted", root)
	if len(got) != 1 || filepath.Base(got[0]) != "wanted.jsonl" {
		t.Errorf("Transcripts = %v, want just wanted.jsonl", got)
	}
}

// COVERS: FR-8.3 | property
//
// THE SUBAGENTS ARE NOT OPTIONAL. On one measured session they were 51% of
// output tokens and 33% of cache reads.
func TestTranscriptsIncludeSubagents(t *testing.T) {
	root := tree(t, map[string]string{
		"-p/s.jsonl":                   record("m", `"input_tokens":1`),
		"-p/s/subagents/agent-a.jsonl": record("m", `"input_tokens":1`),
		"-p/s/subagents/agent-b.jsonl": record("m", `"input_tokens":1`),
	})
	if got := usage.Transcripts("s", root); len(got) != 3 {
		t.Errorf("Transcripts found %d, want the session and both subagents: %v", len(got), got)
	}
}

// COVERS: FR-8.4 | property
//
// /clear opens a new transcript under a NEW id, and the two ids point opposite
// ways, so transcripts are grouped by ROOT: a recorded origin where there is
// one, the file's own name where there is not.
func TestTranscriptsFollowBothSidesOfAClear(t *testing.T) {
	root := tree(t, map[string]string{
		"-p/original.jsonl": record("m", `"input_tokens":1`),
		// The transcript a clear opened: its own id is new, and it records the
		// session it came from.
		"-p/after-clear.jsonl": `{"session_id":"original"}` + "\n" +
			record("m", `"input_tokens":1`),
		"-p/unrelated.jsonl": record("m", `"input_tokens":1`),
	})
	// Asked about EITHER id, both halves come back.
	for _, asked := range []string{"original", "after-clear"} {
		got := usage.Transcripts(asked, root)
		if len(got) != 2 {
			t.Errorf("asked %q, got %d transcripts, want both halves: %v", asked, len(got), got)
		}
	}
}

// COVERS: FR-8.5 | property
//
// The search stays inside the project directory the session belongs to, so its
// cost is proportional to one project's sessions.
func TestSearchStaysInsideTheProject(t *testing.T) {
	root := tree(t, map[string]string{
		"-project-a/s.jsonl":     record("m", `"input_tokens":1`),
		"-project-b/other.jsonl": `{"session_id":"s"}`,
	})
	got := usage.Transcripts("s", root)
	for _, path := range got {
		if filepath.Base(filepath.Dir(path)) == "-project-b" {
			t.Errorf("search left the project: %v", got)
		}
	}
}

// COVERS: FR-8.19, FR-8.22 | property
//
// Cache creation counts live in a nested object as well as at the top level,
// and reading only the top level counts the writes as nothing.
func TestNestedCacheCreationIsCounted(t *testing.T) {
	scratch(t)
	root := tree(t, map[string]string{
		"-p/s.jsonl": `{"message":{"model":"m","usage":{"input_tokens":10,` +
			`"cache_creation":{"ephemeral_5m_input_tokens":7,"ephemeral_1h_input_tokens":3}}}}` + "\n",
	})
	got := usage.Sum("s", root)["m"]
	if got["ephemeral_5m_input_tokens"] != 7 || got["ephemeral_1h_input_tokens"] != 3 {
		t.Errorf("nested cache creation = %v, want 7 and 3", got)
	}
	if got["input_tokens"] != 10 {
		t.Errorf("input_tokens = %v, want 10", got["input_tokens"])
	}
}

// COVERS: FR-8.21 | negative
//
// Only numeric values are added. A field carrying anything else is ignored
// rather than coerced, and does not abandon the record it appeared in.
func TestNonNumericFieldsAreIgnoredNotCoerced(t *testing.T) {
	scratch(t)
	root := tree(t, map[string]string{
		"-p/s.jsonl": `{"message":{"model":"m","usage":{"input_tokens":"lots","output_tokens":5}}}` + "\n",
	})
	got := usage.Sum("s", root)["m"]
	if _, present := got["input_tokens"]; present {
		t.Errorf("a string was counted: %v", got)
	}
	if got["output_tokens"] != 5 {
		t.Errorf("the rest of the record was abandoned: %v", got)
	}
}

// COVERS: FR-8.20 | negative
func TestRecordWithNoModelGoesUnderAPlaceholder(t *testing.T) {
	scratch(t)
	root := tree(t, map[string]string{
		"-p/s.jsonl": `{"message":{"usage":{"input_tokens":10}}}` + "\n",
	})
	got := usage.Sum("s", root)
	if got["?"]["input_tokens"] != 10 {
		t.Errorf("Sum = %v, want the counts under the placeholder", got)
	}
}

// COVERS: FR-8.7 | edge
//
// A transcript is appended to by the session that is rendering and can be read
// mid-line, so the offset advances only over lines that arrived complete.
func TestHalfWrittenLineIsNotCounted(t *testing.T) {
	scratch(t)
	whole := record("m", `"input_tokens":10`) + "\n"
	root := tree(t, map[string]string{
		"-p/s.jsonl": whole + record("m", `"input_tokens":999`), // no newline
	})
	got := usage.Sum("s", root)["m"]
	if got["input_tokens"] != 10 {
		t.Errorf("input_tokens = %v, want 10 with the half line left alone", got["input_tokens"])
	}
}

// COVERS: FR-8.6 | property
//
// Only the bytes appended since the last render are parsed, and the total is
// the running one rather than a re-sum.
func TestOnlyAppendedBytesAreParsed(t *testing.T) {
	scratch(t)
	path := filepath.Join(t.TempDir(), "-p", "s.jsonl")
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	root := filepath.Dir(filepath.Dir(path))
	first := record("m", `"input_tokens":10`) + "\n"
	if err := os.WriteFile(path, []byte(first), 0o644); err != nil {
		t.Fatal(err)
	}
	if got := usage.Sum("s", root)["m"]["input_tokens"]; got != 10 {
		t.Fatalf("first read = %v, want 10", got)
	}

	handle, err := os.OpenFile(path, os.O_APPEND|os.O_WRONLY, 0o644)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := handle.WriteString(record("m", `"input_tokens":5`) + "\n"); err != nil {
		t.Fatal(err)
	}
	handle.Close()

	if got := usage.Sum("s", root)["m"]["input_tokens"]; got != 15 {
		t.Errorf("second read = %v, want 15", got)
	}
}

// COVERS: FR-8.8 | edge
//
// A file that shrank was rotated or replaced, so its offset means nothing
// against the new one and it is read from the start.
func TestShrunkTranscriptIsReadFromTheStart(t *testing.T) {
	scratch(t)
	dir := t.TempDir()
	path := filepath.Join(dir, "-p", "s.jsonl")
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	root := dir
	long := record("m", `"input_tokens":10`) + "\n" + record("m", `"input_tokens":10`) + "\n"
	if err := os.WriteFile(path, []byte(long), 0o644); err != nil {
		t.Fatal(err)
	}
	if got := usage.Sum("s", root)["m"]["input_tokens"]; got != 20 {
		t.Fatalf("first read = %v, want 20", got)
	}

	short := record("m", `"input_tokens":3`) + "\n"
	if err := os.WriteFile(path, []byte(short), 0o644); err != nil {
		t.Fatal(err)
	}
	if got := usage.Sum("s", root)["m"]["input_tokens"]; got != 3 {
		t.Errorf("after shrinking = %v, want 3 from a full re-read", got)
	}
}

// COVERS: FR-8.9 | property
//
// State is per file rather than one running sum, because subagent transcripts
// appear part way through a session.
func TestSubagentAppearingLaterIsCountedOnce(t *testing.T) {
	scratch(t)
	dir := t.TempDir()
	main := filepath.Join(dir, "-p", "s.jsonl")
	if err := os.MkdirAll(filepath.Dir(main), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(main, []byte(record("m", `"input_tokens":10`)+"\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if got := usage.Sum("s", dir)["m"]["input_tokens"]; got != 10 {
		t.Fatalf("first read = %v, want 10", got)
	}

	sub := filepath.Join(dir, "-p", "s", "subagents", "agent-a.jsonl")
	if err := os.MkdirAll(filepath.Dir(sub), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(sub, []byte(record("m", `"input_tokens":4`)+"\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if got := usage.Sum("s", dir)["m"]["input_tokens"]; got != 14 {
		t.Errorf("with the subagent = %v, want 14", got)
	}
	// And again, with nothing new: the main transcript is not re-counted.
	if got := usage.Sum("s", dir)["m"]["input_tokens"]; got != 14 {
		t.Errorf("third read = %v, want 14", got)
	}
}

// COVERS: FR-8.10 | negative
func TestUnknowableSessionYieldsNoTotals(t *testing.T) {
	scratch(t)
	root := tree(t, map[string]string{"-p/other.jsonl": record("m", `"input_tokens":1`)})
	if got := usage.Sum("", root); len(got) != 0 {
		t.Errorf("Sum with no session id = %v, want empty", got)
	}
}

// COVERS: FR-8.24 | edge
//
// Looking for the session a transcript belongs to gives up after a bounded
// number of records. The field first appeared on record 18 of a transcript
// opened by a clear, so the bound is 40: a large transcript that never carries
// one costs a bounded read rather than a full scan.
func TestOriginSearchGivesUpAfterFortyRecords(t *testing.T) {
	within := strings.Repeat(`{"type":"bookkeeping"}`+"\n", 30) +
		`{"session_id":"origin"}` + "\n"
	beyond := strings.Repeat(`{"type":"bookkeeping"}`+"\n", 45) +
		`{"session_id":"origin"}` + "\n"

	root := tree(t, map[string]string{
		"-p/origin.jsonl": record("m", `"input_tokens":1`),
		"-p/early.jsonl":  within,
		"-p/late.jsonl":   beyond,
	})
	got := usage.Transcripts("origin", root)

	var names []string
	for _, path := range got {
		names = append(names, filepath.Base(path))
	}
	joined := strings.Join(names, " ")
	if !strings.Contains(joined, "early.jsonl") {
		t.Errorf("a claim at record 31 was missed: %v", names)
	}
	if strings.Contains(joined, "late.jsonl") {
		t.Errorf("a claim at record 46 was read, so the bound is not holding: %v", names)
	}
}

// COVERS: FR-4.4 | property
//
// The transcript root is a PARAMETER and the offsets follow XDG_STATE_HOME, so
// section 8 is tested against a fixture tree with nothing patched. A test giving
// a root must move XDG_STATE_HOME too, or the offsets it writes land beside the
// real ones and a later render skips bytes it never counted.
func TestRootAndStateAreBothSeams(t *testing.T) {
	root := tree(t, map[string]string{
		"-p/s.jsonl": record("m", `"input_tokens":42`) + "\n",
	})

	// The root reaches the reader: a fixture tree is counted, and the real one
	// is never consulted, which is what makes the figure 42 rather than whatever
	// this machine holds.
	state := t.TempDir()
	t.Setenv("XDG_STATE_HOME", state)
	if got := usage.Sum("s", root)["m"]["input_tokens"]; got != 42 {
		t.Errorf("Sum against a fixture root = %v, want 42", got)
	}

	// And the offsets landed in the scratch directory rather than beside the
	// real ones.
	if _, err := os.Stat(filepath.Join(state, "infobot", "s.json")); err != nil {
		t.Errorf("offsets did not follow XDG_STATE_HOME: %v", err)
	}
	if usage.StateDir() != filepath.Join(state, "infobot") {
		t.Errorf("StateDir = %q, want it under the scratch home", usage.StateDir())
	}
}

// COVERS: FR-1.11e | positive
func TestForgetRemovesTheOffsets(t *testing.T) {
	scratch(t)
	root := tree(t, map[string]string{"-p/s.jsonl": record("m", `"input_tokens":1`)})
	usage.Sum("s", root)
	path := usage.OffsetPath("s")
	if _, err := os.Stat(path); err != nil {
		t.Fatalf("no offsets written: %v", err)
	}
	usage.Forget("s")
	if _, err := os.Stat(path); !os.IsNotExist(err) {
		t.Error("offsets survived Forget")
	}
	usage.Forget("s") // twice is not an error
}
