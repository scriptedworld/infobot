// Package state says what the session's context window is doing, for the bar
// and for a file.
//
// The bar shows it to a person and the file leaves it where a program can read
// it. Both come through Figures so the two cannot disagree: a bar saying 56%
// beside a file saying something else is worse than either alone.
//
// WHY THE FILE EXISTS. An agent has no way to measure its own context. The
// payload Claude Code hands the status line carries the numbers, and nothing
// else in the session sees them. Reconstructing them means tailing the
// transcript and summing usage records by hand, which is what the absence of
// this file cost once already.
//
// It sits beside the usage offsets, in usage.StateDir(): generated state, keyed
// by session, outside every repository. One predictable path, so a reader that
// knows its own session id knows where to look and needs to know nothing else.
//
// Written in canonical YAML through wrench, which validates it against the
// schema beside this file on the way out: block style, one key to a line, keys
// sorted, a string quoted and a number bare. THE FORM IS A PUBLISHED INTERFACE
// (FR-1.11o): silo's coordination board reads these files with patterns
// anchored on the quoted key and the single space after the colon, and takes
// the number bare. Changing the shape breaks it, and adding a key does not.
//
// It emitted that form by hand until the Go port could link wrench's pack. Two
// emitters of one published form was a considered duplicate rather than an
// oversight, and what ended it was the port removing the reason: the argument
// for hand-emitting was the Python pack's import cost and an interpreter that
// might not resolve, and a statically linked pack has neither. wrench's
// docs/DECISIONS/infobots-hand-emitted-yaml-is-a-considered-duplicate.md is the
// decision this supersedes.
//
// The schema is infobot's own and is not in wrench's shipped set: the only
// consumer is bin/board and the subject is a Claude Code session, where that
// set is the vocabulary a generic runner ecosystem shares.
package state

import (
	_ "embed"
	"math"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	wrench "github.com/scriptedworld/wrench/go"

	"github.com/scriptedworld/infobot/internal/num"
	"github.com/scriptedworld/infobot/internal/payload"
	"github.com/scriptedworld/infobot/internal/usage"
)

// inputKeys are what used_percentage is computed from: the input side only,
// never output.
var inputKeys = []string{
	"input_tokens",
	"cache_creation_input_tokens",
	"cache_read_input_tokens",
}

// Window is the context window as used, size and percent.
type Window struct {
	Used    float64
	Size    float64
	Percent float64
}

// Figures returns the context window, or false when it cannot be said at all.
//
// current_usage is the authority when present and total_input_tokens is the
// fallback, matching used_percentage's own formula. Where the counts are absent
// and a percentage is present, the count is derived from it, so the two halves
// agree rather than reporting a literal zero that reads as a bug.
func Figures(cw payload.Map) (Window, bool) {
	size, ok := cw.Num("context_window_size")
	if !ok || size == 0 {
		return Window{}, false
	}

	var used float64
	if current := cw.Obj("current_usage"); len(current) > 0 {
		for _, key := range inputKeys {
			used += current.Count(key)
		}
	} else {
		used = cw.Count("total_input_tokens")
	}

	pct, given := cw.Num("used_percentage")
	switch {
	case !given:
		pct = used / size * 100
	case used == 0:
		used = size * pct / 100
	}
	return Window{Used: used, Size: size, Percent: pct}, true
}

// Path is where this session's state is left. One path, named by session id.
func Path(sessionID string) string {
	dir := usage.StateDir()
	if dir == "" {
		return ""
	}
	return filepath.Join(dir, sessionID+".status.yaml")
}

// Write leaves the last check on disk.
//
// Best effort in the same sense as the usage offsets: a state file that cannot
// be written costs a reader a measurement it can take another way, and the
// status line's one hard guarantee is that it renders or shows nothing.
func Write(data payload.Map, now time.Time) {
	session := data.Str("session_id")
	if session == "" {
		return
	}
	target := Path(session)
	if target == "" {
		return
	}
	if os.MkdirAll(filepath.Dir(target), 0o755) != nil {
		return
	}

	schema, err := statusSchema()
	if err != nil {
		return
	}

	// wrench validates on the way out and then writes whole or not at all. A
	// reader stat-ing this file between a truncate and a write would otherwise
	// see an empty one and conclude the session had no context, which is worse
	// than seeing the previous check. A failed write leaves nothing behind, so a
	// directory of state files never accumulates half-written ones.
	//
	// The error is dropped rather than reported, which is what best effort means
	// here: a state file that cannot be written costs a reader a measurement it
	// can take another way, and the status line's one hard guarantee is that it
	// renders or shows nothing.
	_ = wrench.SaveYAMLFile(fields(data, session, now), target, schema, wrench.LocalFile)
}

//go:embed status.schema.json
var statusSchemaJSON string

// statusSchema compiles the schema once. It is infobot's own rather than one of
// wrench's shipped set, because the file's only consumer is bin/board and its
// subject is a Claude Code session: wrench's shipped set is the vocabulary a
// generic runner ecosystem shares, and this is not a word in it. See wrench's
// docs/DECISIONS/what-earns-a-place-in-the-shipped-set.md, part 3.
var statusSchema = sync.OnceValues(func() (wrench.Schema, error) {
	return wrench.CompileSchema(
		"https://scriptedworld.github.io/infobot/status.schema.json",
		strings.NewReader(statusSchemaJSON),
	)
})

func fields(data payload.Map, session string, now time.Time) map[string]any {
	out := map[string]any{
		"session": session,
		"written": now.Format("2006-01-02T15:04:05-07:00"),
	}
	// A key whose value is empty is omitted rather than written blank, so a
	// reader tells "not said" from "said to be nothing".
	if cwd := data.Obj("workspace").Str("current_dir"); cwd != "" {
		out["cwd"] = cwd
	}
	model := data.Obj("model").Str("display_name")
	if model == "" {
		model = data.Obj("model").Str("id")
	}
	if model != "" {
		out["model"] = model
	}
	if effort := data.Obj("effort").Str("level"); effort != "" {
		out["effort"] = effort
	}

	window, ok := Figures(data.Obj("context_window"))
	if !ok {
		return out
	}
	used, size := tokens(window.Used), tokens(window.Size)
	out["context_used"] = used
	out["context_size"] = size
	// Derived rather than read, and never negative: a payload reporting more
	// used than the window holds gives zero.
	remaining := size - used
	if remaining < 0 {
		remaining = 0
	}
	out["context_remaining"] = remaining
	// Neither floored nor capped, so a reader sees an over-full window as
	// over-full where the bar can only draw it as full.
	out["context_percent"] = num.RoundTo(window.Percent, 1)
	return out
}

// tokens converts a payload's count to an integer, saturating rather than
// overflowing.
//
// CONVERTING AN OUT-OF-RANGE FLOAT TO int64 IS UNDEFINED IN GO, and on amd64 it
// lands on the minimum int64. A payload reporting `used_percentage: 1e21`
// against a window of 100 therefore wrote `"context_used": -9223372036854775808`
// and the board drew it, because a hand emitter formats whatever it is handed.
// The schema refused it the first time this file was written through wrench,
// which is validation on the way out doing the job it is there for.
//
// Saturating rather than refusing: the four context keys are all-or-nothing, so
// dropping them over one nonsense figure would take the other three with it.
func tokens(f float64) int64 {
	switch {
	case !(f > 0):
		// Also catches NaN, which every comparison is false against.
		return 0
	case f >= math.MaxInt64:
		return math.MaxInt64
	default:
		return int64(f)
	}
}

// Forget drops this session's state file.
//
// Called when the session ends. Nothing will read it again: it reports a
// context window that no longer exists, and a reader finding it later would
// have no way to tell a live session from a finished one.
func Forget(sessionID string) {
	if path := Path(sessionID); path != "" {
		_ = os.Remove(path)
	}
}
