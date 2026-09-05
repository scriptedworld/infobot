// Package usage reads token totals for the running session by tailing its
// transcript.
//
// The status line's payload carries no cumulative usage, but Claude Code writes
// a usage record per assistant message into the session transcript, and the
// payload carries the session id that names it.
//
// THIS IS THE ONE PLACE INFOBOT READS A FILE IT WAS NOT HANDED, and it is
// bounded: a stat on every render, a parse only of what has been appended since
// the last one. A full re-sum of a 2.7MB transcript measured 21ms to 29ms
// against a render budget of 33ms, and it grows for the life of the session.
//
// Every read is guarded. A missing transcript, an unreadable state file, a half
// written line: each yields no totals rather than an error.
package usage

import (
	"bufio"
	"encoding/json"
	"maps"
	"os"
	"path/filepath"
	"sort"
)

// claimLines is how far into a transcript to look for the session it belongs
// to. A transcript opens with bookkeeping that carries no session at all:
// measured on one opened by a clear, the field first appears on record 18.
const claimLines = 40

var counts = []string{ //nolint:gochecknoglobals // the counter names are read-only after init
	"input_tokens",
	"output_tokens",
	"cache_read_input_tokens",
	"ephemeral_5m_input_tokens",
	"ephemeral_1h_input_tokens",
}

// Totals is token counts keyed by model, then by field.
type Totals map[string]map[string]float64

// StateDir is where the offsets live: generated state, so outside the
// repository.
func StateDir() string {
	root := os.Getenv("XDG_STATE_HOME")
	if root == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return ""
		}
		root = filepath.Join(home, ".local", "state")
	}
	return filepath.Join(root, "infobot")
}

// Projects is the default transcript root.
func Projects() string {
	home, err := os.UserHomeDir()
	if err != nil {
		return ""
	}
	return filepath.Join(home, ".claude", "projects")
}

// Transcripts returns every transcript the session bills for: its own, and its
// subagents'.
//
// root is where the projects live, taken as an argument so a test can point it
// at a fixture tree rather than patching anything. Empty means the real one,
// which is the only thing the status line ever passes.
//
// Found by name rather than by rebuilding a path. The directory is a slug of
// the working directory, but a session may have been started somewhere other
// than where it now is, so the id is globbed for instead of the slug being
// reconstructed from workspace.current_dir.
//
// THE SUBAGENTS ARE NOT OPTIONAL. Each runs in its own transcript under
// <session>/subagents/, and on one measured session they were 51% of output
// tokens and 33% of cache reads.
//
// NEITHER ARE THE TRANSCRIPTS A CLEAR LEFT BEHIND. /clear opens a new
// transcript under a NEW sessionId, so a name match alone follows one side of
// it. The two ids point opposite ways, so transcripts are grouped by ROOT: a
// transcript's recorded origin where it has one, its own name where it does
// not. Every transcript sharing a root is one session's spending, whichever id
// the payload handed over.
func Transcripts(sessionID, root string) []string {
	if sessionID == "" {
		return nil
	}
	if root == "" {
		root = Projects()
	}
	named, _ := filepath.Glob(filepath.Join(root, "*", sessionID+".jsonl"))

	// Scoped to the project the session belongs to. A clear opens its new
	// transcript beside the old one, so the search never leaves that directory,
	// and the cost is proportional to one project's sessions rather than to
	// every session ever recorded on the machine.
	var pool []string
	origin := sessionID
	if len(named) > 0 {
		pool, _ = filepath.Glob(filepath.Join(filepath.Dir(named[0]), "*.jsonl"))
		origin = originOf(named[0])
	} else {
		pool, _ = filepath.Glob(filepath.Join(root, "*", "*.jsonl"))
	}

	var found []string
	for _, path := range pool {
		if originOf(path) == origin || stem(path) == origin {
			found = append(found, path)
		}
	}
	subagents := make([]string, 0, len(found))
	for _, path := range found {
		pattern := filepath.Join(filepath.Dir(path), stem(path), "subagents", "*.jsonl")
		matched, _ := filepath.Glob(pattern)
		subagents = append(subagents, matched...)
	}
	return append(found, subagents...)
}

func stem(path string) string {
	base := filepath.Base(path)
	return base[:len(base)-len(filepath.Ext(base))]
}

// originOf is the session a transcript belongs to: its recorded origin, else
// its own name.
func originOf(path string) string {
	handle, err := os.Open(path) //nolint:gosec // the transcript path arrives in the hook payload
	if err != nil {
		return stem(path)
	}
	// Read-only, so nothing is buffered and a close error carries no news.
	defer func() { _ = handle.Close() }()

	scanner := bufio.NewScanner(handle)
	scanner.Buffer(make([]byte, 0, 64*1024), 16*1024*1024)
	for line := 0; line < claimLines && scanner.Scan(); line++ {
		var record struct {
			SessionID string `json:"session_id"`
		}
		if json.Unmarshal(scanner.Bytes(), &record) != nil {
			continue
		}
		if record.SessionID != "" {
			return record.SessionID
		}
	}
	return stem(path)
}

// fileState is how far into one transcript the last render read, and what it
// counted getting there.
type fileState struct {
	Size   int64  `json:"size"`
	Totals Totals `json:"totals"`
}

type offsets struct {
	Files map[string]fileState `json:"files"`
}

// Sum returns token counts for the session, keyed by model. Empty when
// unknowable.
//
// State is per file rather than one running sum, because subagent transcripts
// appear part way through a session and a single offset cannot say which of
// them a total already includes.
//
// root is passed through to Transcripts. The OFFSETS it writes are not covered
// by it: they follow XDG_STATE_HOME, so a test giving a fixture root should
// move that too, or the offsets it records land beside the real ones and a
// later render skips bytes it never counted.
func Sum(sessionID, root string) Totals {
	known := load(sessionID).Files
	seen := make(map[string]fileState, len(known))
	changed := false

	for _, path := range Transcripts(sessionID, root) {
		info, err := os.Stat(path)
		if err != nil {
			continue
		}
		size := info.Size()
		previous := known[path]
		if previous.Size == size {
			seen[path] = previous
			continue
		}
		// A file that shrank was rotated or replaced, so its offset means
		// nothing against the new one and it is read from the start.
		var start int64
		counted := Totals{}
		if size > previous.Size {
			start = previous.Size
			for model, fields := range previous.Totals {
				counted[model] = copyOf(fields)
			}
		}
		read, consumed := scan(path, start)
		merge(counted, read)
		seen[path] = fileState{Size: start + consumed, Totals: counted}
		changed = true
	}

	if changed || len(seen) != len(known) {
		save(sessionID, offsets{Files: seen})
	}
	summed := Totals{}
	for _, entry := range seen {
		merge(summed, entry.Totals)
	}
	return summed
}

func copyOf(from map[string]float64) map[string]float64 {
	to := make(map[string]float64, len(from))
	maps.Copy(to, from)
	return to
}

// scan returns usage from start onward, and how many bytes of it were whole
// lines.
//
// A transcript being appended to by the session that is rendering can end
// mid-line, so the offset advances only over lines that arrived complete.
func scan(path string, start int64) (Totals, int64) {
	handle, err := os.Open(path) //nolint:gosec // the transcript path arrives in the hook payload
	if err != nil {
		return Totals{}, 0
	}
	// Read-only, so nothing is buffered and a close error carries no news.
	defer func() { _ = handle.Close() }()
	if _, err := handle.Seek(start, 0); err != nil {
		return Totals{}, 0
	}

	found := Totals{}
	var consumed int64
	reader := bufio.NewReaderSize(handle, 256*1024)
	for {
		// ReadBytes reports no error only when it found the delimiter, so a
		// non-nil error IS the half-written last line. Breaking without
		// consuming leaves the offset short of it, and the next render reads it
		// again once the rest has arrived.
		line, err := reader.ReadBytes('\n')
		if err != nil {
			break
		}
		consumed += int64(len(line))
		take(found, line)
	}
	return found, consumed
}

// take adds one transcript line's usage, ignoring anything that is not usage.
func take(found Totals, raw []byte) {
	var record struct {
		Message struct {
			Model string         `json:"model"`
			Usage map[string]any `json:"usage"`
		} `json:"message"`
	}
	if json.Unmarshal(raw, &record) != nil || len(record.Message.Usage) == 0 {
		return
	}
	model := record.Message.Model
	if model == "" {
		// Counted under a placeholder no rate table answers to, so it flags the
		// total incomplete rather than being priced at whatever model happened
		// to be next to it.
		model = "?"
	}
	if found[model] == nil {
		found[model] = map[string]float64{}
	}
	creation, _ := record.Message.Usage["cache_creation"].(map[string]any)
	for _, field := range counts {
		value, present := record.Message.Usage[field]
		if !present {
			value, present = creation[field]
			if !present {
				continue
			}
		}
		// Only numeric values are added. A field carrying anything else is
		// ignored rather than coerced, and it does not abandon the record.
		if number, ok := value.(float64); ok {
			found[model][field] += number
		}
	}
}

func merge(into, more Totals) {
	for model, fields := range more {
		if into[model] == nil {
			into[model] = map[string]float64{}
		}
		for field, value := range fields {
			into[model][field] += value
		}
	}
}

// OffsetPath is where this session's offsets live.
func OffsetPath(sessionID string) string {
	dir := StateDir()
	if dir == "" {
		return ""
	}
	return filepath.Join(dir, sessionID+".json")
}

func load(sessionID string) offsets {
	empty := offsets{Files: map[string]fileState{}}
	path := OffsetPath(sessionID)
	if path == "" {
		return empty
	}
	raw, err := os.ReadFile(path) //nolint:gosec // the transcript path arrives in the hook payload
	if err != nil {
		return empty
	}
	var state offsets
	if json.Unmarshal(raw, &state) != nil || state.Files == nil {
		return empty
	}
	return state
}

// save is best effort. A state file that cannot be written costs a re-sum, and
// a re-sum is slow rather than wrong, so the failure is not worth reporting.
func save(sessionID string, state offsets) {
	path := OffsetPath(sessionID)
	if path == "" {
		return
	}
	// OWNER ONLY. The offsets are this user's own byte positions into their own
	// transcripts, read by nothing but this program, so the group and world
	// bits were breadth nobody asked for.
	if os.MkdirAll(filepath.Dir(path), 0o750) != nil {
		return
	}
	raw, err := json.Marshal(state)
	if err != nil {
		return
	}
	_ = os.WriteFile(path, raw, 0o600)
}

// Forget drops this session's offsets.
//
// Called when the session ends. The offsets record how far into each transcript
// the last render read, which is worth nothing once nothing will render again,
// and they accumulate one file per session forever otherwise.
func Forget(sessionID string) {
	if path := OffsetPath(sessionID); path != "" {
		_ = os.Remove(path)
	}
}

// Models returns the model keys in sorted order, so anything walking totals is
// deterministic rather than at the mercy of map iteration.
func (t Totals) Models() []string {
	names := make([]string, 0, len(t))
	for model := range t {
		names = append(names, model)
	}
	sort.Strings(names)
	return names
}
