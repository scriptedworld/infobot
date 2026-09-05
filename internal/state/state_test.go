package state_test

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/scriptedworld/infobot/internal/payload"
	"github.com/scriptedworld/infobot/internal/state"
)

//nolint:gochecknoglobals // fixed, for determinism
var stamp = time.Date(2026, 8, 26, 16, 25, 52, 0,
	time.FixedZone("", -7*3600))

// write runs Write into a scratch state directory and hands back the file.
//
// XDG_STATE_HOME is moved for every test that renders. A test that does not
// move it drops a file into the real state directory naming a session that
// never existed, and no SessionEnd will ever be handed it.
func write(t *testing.T, data payload.Map) string {
	t.Helper()
	t.Setenv("XDG_STATE_HOME", t.TempDir())
	state.Write(data, stamp)
	path := state.Path(data.Str("session_id"))
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("no state file written: %v", err)
	}
	return string(raw)
}

func full() payload.Map {
	return payload.Map{
		"session_id": "abcd-1234",
		"model":      map[string]any{"display_name": `Opus 5 "1M" \ context`},
		"effort":     map[string]any{"level": "xhigh"},
		"workspace":  map[string]any{"current_dir": "/home/me/.projects/infobot"},
		"context_window": map[string]any{
			"context_window_size": 1000000.0,
			"used_percentage":     48.2,
			"current_usage":       map[string]any{"input_tokens": 480000.0},
		},
	}
}

// COVERS: FR-1.11g, FR-1.11o, FR-1.11p | property
//
// The exact bytes, because the form is a published interface: silo's board
// matches anchored patterns on the quoted key and the single space after the
// colon, and takes the number bare.
//
// It is also FR-1.11p, which changed shape on 2026-09-03 without changing what
// it is for. It was a mutual pin between two emitters: infobot's hand emitter
// here, and a frozen copy of its output as a fixture in wrench. The port deleted
// the hand emitter, so wrench's pack now writes these bytes and this is a
// conformance check against it rather than half of a pin.
//
// That is stronger, because the thing it guards is real. wrench's fixture never
// re-derived infobot's output and so could not fail when infobot moved; this
// test runs the emitter that actually writes the file. Editing it to make it
// pass is still how the guarantee comes undone.
func TestCanonicalForm(t *testing.T) {
	want := `"context_percent": 48.2
"context_remaining": 520000
"context_size": 1000000
"context_used": 480000
"cwd": "/home/me/.projects/infobot"
"effort": "xhigh"
"model": "Opus 5 \"1M\" \\ context"
"session": "abcd-1234"
"written": "2026-08-26T16:25:52-07:00"
`
	if got := write(t, full()); got != want {
		t.Errorf("canonical form drifted.\ngot:\n%s\nwant:\n%s", got, want)
	}
}

// COVERS: FR-1.11g | property
//
// A key whose value is empty is omitted rather than written blank, so a reader
// tells "not said" from "said to be nothing".
func TestEmptyValuesAreOmitted(t *testing.T) {
	got := write(t, payload.Map{"session_id": "bare"})
	for _, key := range []string{"cwd", "model", "effort", "context_used"} {
		if strings.Contains(got, `"`+key+`"`) {
			t.Errorf("%q written for a payload that carries none:\n%s", key, got)
		}
	}
	// session and written are always there.
	for _, key := range []string{"session", "written"} {
		if !strings.Contains(got, `"`+key+`"`) {
			t.Errorf("%q missing, which is always written:\n%s", key, got)
		}
	}
}

// COVERS: FR-1.11b | property
func TestFileIsReadableByOtherPrograms(t *testing.T) {
	t.Setenv("XDG_STATE_HOME", t.TempDir())
	state.Write(full(), stamp)
	info, err := os.Stat(state.Path("abcd-1234"))
	if err != nil {
		t.Fatal(err)
	}
	if mode := info.Mode().Perm(); mode != 0o644 {
		t.Errorf("mode = %o, want 644", mode)
	}
}

// COVERS: FR-1.11b | property
//
// Written whole or not at all, and the temporary is gone either way, so a
// directory of state files never accumulates half-written ones beside the real.
func TestNoTemporaryIsLeftBehind(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("XDG_STATE_HOME", dir)
	state.Write(full(), stamp)
	entries, err := os.ReadDir(filepath.Join(dir, "infobot"))
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 1 {
		var names []string
		for _, e := range entries {
			names = append(names, e.Name())
		}
		t.Errorf("state directory holds %v, want the one file", names)
	}
}

// COVERS: FR-1.11 | negative
func TestNoSessionIdWritesNothing(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("XDG_STATE_HOME", dir)
	state.Write(payload.Map{"model": map[string]any{"display_name": "Opus 5"}}, stamp)
	if _, err := os.Stat(filepath.Join(dir, "infobot")); err == nil {
		entries, _ := os.ReadDir(filepath.Join(dir, "infobot"))
		if len(entries) != 0 {
			t.Errorf("wrote %d files for a payload with no session id", len(entries))
		}
	}
}

// COVERS: FR-2.5, FR-2.6 | property
//
// current_usage is the authority and total_input_tokens the fallback, and the
// counts are the INPUT side only: output is never in the context percentage.
func TestFiguresCountsTheInputSideOnly(t *testing.T) {
	got, ok := state.Figures(payload.Map{
		"context_window_size": 200000.0,
		"current_usage": map[string]any{
			"input_tokens":                20000.0,
			"cache_creation_input_tokens": 5000.0,
			"cache_read_input_tokens":     1000.0,
			"output_tokens":               99999.0,
		},
		"total_input_tokens": 1.0,
	})
	if !ok {
		t.Fatal("Figures said the window could not be measured")
	}
	if got.Used != 26000 {
		t.Errorf("Used = %v, want 26000 with output excluded", got.Used)
	}
}

// COVERS: FR-2.6 | positive
func TestFiguresFallsBackToTotalInputTokens(t *testing.T) {
	got, ok := state.Figures(payload.Map{
		"context_window_size": 200000.0,
		"total_input_tokens":  50000.0,
	})
	if !ok || got.Used != 50000 {
		t.Errorf("Used = %v, want the fallback 50000", got.Used)
	}
}

// COVERS: FR-2.7 | property
//
// A percentage without counts derives the counts, and counts without a
// percentage derive the percentage. Printing the literal zero gave
// "0/200k (3% consumed)", which reads as a fault rather than as data.
func TestFiguresDerivesWhicheverHalfIsMissing(t *testing.T) {
	fromPct, _ := state.Figures(payload.Map{
		"context_window_size": 200000.0,
		"used_percentage":     25.0,
	})
	if fromPct.Used != 50000 {
		t.Errorf("Used = %v, want 50000 derived from the percentage", fromPct.Used)
	}
	fromCounts, _ := state.Figures(payload.Map{
		"context_window_size": 200000.0,
		"current_usage":       map[string]any{"input_tokens": 50000.0},
	})
	if fromCounts.Percent != 25 {
		t.Errorf("Percent = %v, want 25 derived from the counts", fromCounts.Percent)
	}
}

// COVERS: FR-1.3 | negative
func TestFiguresRefusesAWindowWithNoSize(t *testing.T) {
	for _, cw := range []payload.Map{
		nil,
		{},
		{"context_window_size": 0.0},
		{"used_percentage": 40.0},
	} {
		if _, ok := state.Figures(cw); ok {
			t.Errorf("Figures measured %v, which carries no size", cw)
		}
	}
}

// COVERS: FR-1.11h | edge
//
// context_remaining is never negative, and context_percent is neither floored
// nor capped, so a reader sees an over-full window as over-full.
func TestOverFullWindowClampsRemainingButNotPercent(t *testing.T) {
	got := write(t, payload.Map{
		"session_id": "over",
		"context_window": map[string]any{
			"context_window_size": 100.0,
			"used_percentage":     130.0,
			"current_usage":       map[string]any{"input_tokens": 130.0},
		},
	})
	if !strings.Contains(got, `"context_remaining": 0`) {
		t.Errorf("remaining went negative:\n%s", got)
	}
	if !strings.Contains(got, `"context_percent": 130`) {
		t.Errorf("percent was capped:\n%s", got)
	}
}

// COVERS: FR-1.11q | property
//
// A number is spelled without an exponent, ever. `1e+06` is a legal spelling of
// a million and silo's board matches `[0-9.]+` against the value, so it would
// capture `1`, report a plausible small number, and never fail.
//
// wrench measured the same divergence across its three packs on 2026-08-28 and
// four of six values disagreed, so this is not hypothetical and not only
// infobot's. Held here for infobot's own emitter whatever the ecosystem settles.
func TestNumbersAreNeverSpelledWithAnExponent(t *testing.T) {
	for _, pct := range []float64{
		1e6, 1.23456789e8, 1e21, 1e-7, 0.0000001, 48.2, 0, 100,
	} {
		got := write(t, payload.Map{
			"session_id": "exponent",
			"context_window": map[string]any{
				"context_window_size": 100.0,
				"used_percentage":     pct,
			},
		})
		for _, line := range strings.Split(got, "\n") {
			key, value, found := strings.Cut(line, ": ")
			// The KEY carries an "e", in "percent", so only the value is
			// scanned. Checking the whole line reports every row as a failure
			// and reads as the emitter being far more broken than it is.
			if !found || key != `"context_percent"` {
				continue
			}
			if strings.ContainsAny(value, "eE") {
				t.Errorf("%v spelled with an exponent: %q", pct, value)
			}
		}
	}
}

// COVERS: FR-1.11q | property
//
// A float keeps its decimal point so a reader gets a float back, and an integer
// does not have one. Both must survive without reaching for an exponent.
func TestFloatKeepsItsPointAndIntegerDoesNot(t *testing.T) {
	got := write(t, payload.Map{
		"session_id": "types",
		"context_window": map[string]any{
			"context_window_size": 1000000.0,
			"used_percentage":     50.0,
			"current_usage":       map[string]any{"input_tokens": 500000.0},
		},
	})
	if !strings.Contains(got, `"context_percent": 50.0`) {
		t.Errorf("a round percentage lost its decimal point:\n%s", got)
	}
	if !strings.Contains(got, `"context_size": 1000000`) ||
		strings.Contains(got, `"context_size": 1000000.0`) {
		t.Errorf("a count is not a bare integer:\n%s", got)
	}
}

// COVERS: FR-1.11e | positive
func TestForgetRemovesTheStateFile(t *testing.T) {
	t.Setenv("XDG_STATE_HOME", t.TempDir())
	state.Write(full(), stamp)
	path := state.Path("abcd-1234")
	if _, err := os.Stat(path); err != nil {
		t.Fatalf("nothing to forget: %v", err)
	}
	state.Forget("abcd-1234")
	if _, err := os.Stat(path); !os.IsNotExist(err) {
		t.Error("state file survived Forget")
	}
	// Forgetting twice is not an error.
	state.Forget("abcd-1234")
}

// controlRanges is every code point FR-1.11r names, plus the five separators
// that look like they belong and do not. The separators are here to assert they
// are left alone: a sweep found them already round tripping, so escaping them
// would be a change with no defect behind it.
func controlRanges() (escaped, untouched []rune) {
	for r := rune(0); r < 0x20; r++ {
		escaped = append(escaped, r)
	}
	escaped = append(escaped, 0x7f)
	for r := rune(0x80); r <= 0x9f; r++ {
		escaped = append(escaped, r)
	}
	// U+2028 and U+2029 are escaped for a spec reason rather than an observed
	// one: they are line breaks in YAML 1.1 and not in 1.2, exactly as U+0085
	// is. U+00A0 and U+200B are neither, and are left alone.
	escaped = append(escaped, 0x2028, 0x2029)
	return escaped, []rune{0x00a0, 0x200b, 0xfeff}
}

// COVERS: FR-1.11r | property
//
// The whole range rather than a sample. A first pass at this defect tested six
// characters and reported three failures against an actual 61, and wrench made
// the same error the same day on its own packs, sampling twelve and reporting
// eight. Neither number was worth quoting, so this asserts the range.
func TestEveryControlCodePointSurvivesTheRoundTrip(t *testing.T) {
	escaped, untouched := controlRanges()
	for _, r := range append(append([]rune{}, escaped...), untouched...) {
		data := full()
		data["workspace"] = map[string]any{"current_dir": "/a" + string(r) + "b"}
		got := write(t, data)

		line := ""
		for _, candidate := range strings.Split(got, "\n") {
			if strings.HasPrefix(candidate, `"cwd"`) {
				line = candidate
			}
		}
		if line == "" {
			t.Fatalf("U+%04X: no cwd line, so the value broke the record", r)
		}
		if strings.ContainsRune(line, r) && r >= 0x20 && r != 0x7f && r < 0x80 {
			continue // a separator, left alone deliberately
		}
		if strings.ContainsRune(line, r) && (r < 0x20 || r == 0x7f || r <= 0x9f) {
			t.Errorf("U+%04X: emitted raw, so a reader gets a space or a refusal", r)
		}
	}
}

// cwdLine renders a payload whose cwd carries text, and returns that one line.
func cwdLine(t *testing.T, text string) string {
	t.Helper()
	data := full()
	data["workspace"] = map[string]any{"current_dir": text}
	for _, line := range strings.Split(write(t, data), "\n") {
		if strings.HasPrefix(line, `"cwd"`) {
			return line
		}
	}
	t.Fatalf("no cwd line for %q", text)
	return ""
}

// COVERS: FR-1.11r | negative
//
// The three a parser accepts raw and then changes. They are the dangerous
// members: the other 61 make a file no parser will read, which is loud, and
// these come back as a space with nothing reporting it.
func TestTheLineBreakSetIsEscaped(t *testing.T) {
	for _, c := range []struct{ r, want string }{
		{"\n", `"cwd": "/a\nb"`},
		{"\r", `"cwd": "/a\rb"`},
		// Not folded by any parser reachable here, and escaped anyway: YAML
		// 1.1 makes these line breaks alongside the three above, and 1.2 does
		// not, so which of them fold is the reader's version rather than the
		// character.
		//
		// THE SPELLING CHANGED WHEN THE EMITTER DID, and the requirement did
		// not. FR-1.11r is that these three are escaped rather than emitted
		// raw, because a parser accepts them raw and hands back a space. The
		// hand emitter spelled them `\u2028`, `\u2029` and `\x85`; wrench
		// spells them with YAML's own names for the same code points. Both
		// escape; both round-trip. This is the byte change FR-1.11o obliges
		// infobot to announce, and it reaches only values carrying one of these
		// characters, which a cwd or a model name does not.
		{"\u2028", `"cwd": "/a\Lb"`},
		{"\u2029", `"cwd": "/a\Pb"`},
		{"\u0085", `"cwd": "/a\Nb"`},
	} {
		if got := cwdLine(t, "/a"+c.r+"b"); got != c.want {
			t.Errorf("got %q, want %q", got, c.want)
		}
	}
}

// COVERS: FR-1.11r | edge
//
// Two hex digits, because `\x9` is a truncated escape rather than a tab.
func TestShortEscapesAreZeroPadded(t *testing.T) {
	if got := cwdLine(t, "/a\x01b"); got != `"cwd": "/a\x01b"` {
		t.Errorf("got %q, want %q", got, `"cwd": "/a\x01b"`)
	}
}

// COVERS: FR-1.11r | positive
//
// Nothing outside the ranges changes, which is what makes this a fix rather
// than a change to what the form emits for every value it has ever held.
func TestOrdinaryTextIsUntouched(t *testing.T) {
	for _, s := range []string{"/home/x/proj", "Opus 5 (1M)", "é日本", "a b"} {
		if got := cwdLine(t, s); got != `"cwd": "`+s+`"` {
			t.Errorf("got %q, want it left alone", got)
		}
	}
}
