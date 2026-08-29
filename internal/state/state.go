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
// Written in canonical YAML, hand-emitted: block style, one key to a line, keys
// sorted, a string quoted and a number bare. THE FORM IS A PUBLISHED INTERFACE
// (FR-1.11o): silo's coordination board reads these files with patterns
// anchored on the quoted key and the single space after the colon, and takes
// the number bare. Changing the shape breaks it, and adding a key does not.
package state

import (
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

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
	dir := filepath.Dir(target)
	if os.MkdirAll(dir, 0o755) != nil {
		return
	}

	// Written whole or not at all. A reader stat-ing this file between a
	// truncate and a write would otherwise see an empty one and conclude the
	// session had no context, which is worse than seeing the previous check.
	handle, err := os.CreateTemp(dir, "."+session+".*.yaml")
	if err != nil {
		return
	}
	temporary := handle.Name()
	_, err = handle.WriteString(canonical(fields(data, session, now)))
	if closeErr := handle.Close(); err == nil {
		err = closeErr
	}
	if err == nil {
		err = os.Chmod(temporary, 0o644)
	}
	if err == nil {
		err = os.Rename(temporary, target)
	}
	if err != nil {
		// A failed write leaves nothing behind, so a directory of state files
		// never accumulates half-written ones beside the real ones.
		_ = os.Remove(temporary)
	}
}

// value is one emitted scalar, keeping its own type so the round trip does.
type value struct {
	text     string
	quoted   bool
	rendered string
}

func str(text string) value  { return value{text: text, quoted: true} }
func raw(text string) value  { return value{rendered: text} }
func intv(n float64) value   { return raw(strconv.FormatInt(int64(n), 10)) }
func floatv(f float64) value { return raw(decimal(f)) }

// decimal writes a float so its type is never in question on the way back in:
// it always carries a decimal point, which a YAML reader needs to give back a
// float rather than an integer.
//
// NEVER AN EXPONENT. `1e+06` is a legal spelling of a million and a reader
// matching `[0-9.]+` against it captures `1`, which is a plausible small number
// rather than a parse failure. silo's coordination board matches exactly that,
// so an exponent here is a silent wrong answer on a board a person uses to
// decide which session to clear.
//
// wrench measured the same divergence across its Python, Go and Rust packs on
// 2026-08-28: four of six values spelled differently and every pack was the odd
// one out for something, invisible because no fixture held a float outside the
// range where all three agree. The canonical spelling for the ecosystem is a
// question above this repository. This is infobot's own answer meanwhile, and
// it is the conservative one: 'f' never reaches for an exponent at any
// magnitude.
func decimal(f float64) string {
	text := strconv.FormatFloat(f, 'f', -1, 64)
	if strings.Contains(text, ".") {
		return text
	}
	return text + ".0"
}

func fields(data payload.Map, session string, now time.Time) map[string]value {
	out := map[string]value{
		"session": str(session),
		"written": str(now.Format("2006-01-02T15:04:05-07:00")),
	}
	// A key whose value is empty is omitted rather than written blank, so a
	// reader tells "not said" from "said to be nothing".
	if cwd := data.Obj("workspace").Str("current_dir"); cwd != "" {
		out["cwd"] = str(cwd)
	}
	model := data.Obj("model").Str("display_name")
	if model == "" {
		model = data.Obj("model").Str("id")
	}
	if model != "" {
		out["model"] = str(model)
	}
	if effort := data.Obj("effort").Str("level"); effort != "" {
		out["effort"] = str(effort)
	}

	window, ok := Figures(data.Obj("context_window"))
	if !ok {
		return out
	}
	used, size := int64(window.Used), int64(window.Size)
	out["context_used"] = intv(window.Used)
	out["context_size"] = intv(window.Size)
	// Derived rather than read, and never negative: a payload reporting more
	// used than the window holds gives zero.
	remaining := size - used
	if remaining < 0 {
		remaining = 0
	}
	out["context_remaining"] = intv(float64(remaining))
	// Neither floored nor capped, so a reader sees an over-full window as
	// over-full where the bar can only draw it as full.
	out["context_percent"] = floatv(num.RoundTo(window.Percent, 1))
	return out
}

// canonical emits the mapping: keys quoted, sorted, one to a line.
func canonical(state map[string]value) string {
	keys := make([]string, 0, len(state))
	for key := range state {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	// The key is quoted the same way a string value is, rather than with %q,
	// whose Go escape syntax is a wider language than the two escapes this form
	// defines.
	var out strings.Builder
	for _, key := range keys {
		out.WriteString(scalar(str(key)) + ": " + scalar(state[key]) + "\n")
	}
	return out.String()
}

func scalar(v value) string {
	if !v.quoted {
		return v.rendered
	}
	var out strings.Builder
	out.WriteByte('"')
	for _, r := range v.text {
		out.WriteString(escape(r))
	}
	out.WriteByte('"')
	return out.String()
}

// escape spells one rune for a double-quoted scalar, which is FR-1.11r.
//
// THE FORM WAS NEVER THE LIMIT. A double-quoted YAML scalar carries the whole
// C-style escape set and stays on one line, so it is a JSON string with more in
// it, and it is the style this file already emitted. Escaping two characters
// out of the set was under-implementation rather than a format that could not
// say the value.
//
// The ranges are the ones a strict reader treats specially. C0 and DEL and C1
// are rejected outright bar three, and those three are the dangerous ones:
// `\n`, `\r` and U+0085 are accepted raw and each comes back as a SPACE. So the
// characters a parser lets through are exactly the ones it corrupts, which is
// why this belongs in the emitter rather than being left to a stricter reader.
//
// THE LINE-BREAK SET IS THE SPEC'S, NOT THE OBSERVED ONE. YAML 1.1 makes five
// characters line breaks, LF CR NEL LS PS; 1.2 cuts the set to LF and CR for
// JSON compatibility and calls the other three non-breaks. Checked against both
// specification texts on 2026-08-28 rather than inherited.
//
// PyYAML folds LF, CR and U+0085 and preserves U+2028 and U+2029, so it
// implements three fifths of its own version's rule. A range derived from what
// it does would escape U+0085 and leave its two spec siblings raw, which is a
// split in the spec's class with nothing behind it.
//
// Measured, 70 code points through the built binary, `.ephemera/ctrl-sweep.py`:
// 6 ok, 61 unreadable and 3 silently changed became 70 ok. The 61 was the
// parser's rule rather than a score, so what this moves
// is the third column, which is the one FR-1.11r is about.
//
// `\xNN` names a code point rather than a byte, so it is right for C1 as well
// as C0. Nothing outside these ranges is touched, and no value that held none
// of them renders differently than it did before.
func escape(r rune) string {
	switch r {
	case '\\':
		return `\\`
	case '"':
		return `\"`
	case '\t':
		return `\t`
	case '\n':
		return `\n`
	case '\r':
		return `\r`
	}
	// U+2028 and U+2029 are line breaks in YAML 1.1 and non-breaks in 1.2, the
	// same change that demoted U+0085. They are escaped for that reason rather
	// than an observed one: a 1.1 parser folds them, and this file must not
	// depend on which version its reader implements. `\u` because the code
	// point does not fit `\x`.
	if r == 0x2028 || r == 0x2029 {
		return `\u` + strconv.FormatInt(int64(r), 16)
	}
	if r < 0x20 || r == 0x7f || (r >= 0x80 && r <= 0x9f) {
		// Exactly two digits, which is what \x takes. FormatInt gives one for
		// anything under 0x10, and `\x9` reads as a truncated escape.
		hex := strconv.FormatInt(int64(r), 16)
		if len(hex) == 1 {
			hex = "0" + hex
		}
		return `\x` + hex
	}
	return string(r)
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
