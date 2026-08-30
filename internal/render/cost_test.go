package render_test

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/scriptedworld/infobot/internal/payload"
	"github.com/scriptedworld/infobot/internal/render"
)

const costSession = "cost-0000-4000-8000-000000000000"

// priced sets up a home whose transcripts and rate table are known, so the
// money on the row is arithmetic rather than whatever this machine happens to
// hold. Returns the payload that reaches it.
//
// usage counts are chosen to make the figures round: 10M input at $5/M is $50,
// and 100M cache reads at a tenth of $5/M is another $50.
func priced(t *testing.T, records string) payload.Map {
	t.Helper()
	home := t.TempDir()
	t.Setenv("HOME", home)
	t.Setenv("XDG_STATE_HOME", t.TempDir())
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	t.Setenv("NO_COLOR", "1")
	t.Setenv("TMUX", "")
	t.Setenv("HERDR_PANE_ID", "")

	dir := filepath.Join(home, ".claude", "projects", "-p")
	if err := os.MkdirAll(dir, 0o750); err != nil {
		t.Fatal(err)
	}
	transcript := filepath.Join(dir, costSession+".jsonl")
	if err := os.WriteFile(transcript, []byte(records), 0o600); err != nil {
		t.Fatal(err)
	}
	return payload.Map{
		"session_id":     costSession,
		"model":          map[string]any{"display_name": "Opus 5"},
		"context_window": window(20),
		"rate_limits": map[string]any{
			"five_hour": map[string]any{"used_percentage": 34.0, "resets_at": at(3600)},
		},
	}
}

func usageLine(fields string) string {
	return `{"message":{"model":"claude-opus-5","usage":{` + fields + `}}}` + "\n"
}

// COVERS: FR-8.1 | property
//
// The figure is a COUNTERFACTUAL and not a bill: what the same tokens would
// have cost through the API at list rates. 10M input tokens at Opus 5's $5 per
// million is $50, and that is the number, not a share of a subscription.
func TestCostIsTheListRateCounterfactual(t *testing.T) {
	data := priced(t, usageLine(`"input_tokens":10000000`))
	joined := strings.Join(render.Build(data, "/home/me", 200, clock), "\n")
	if !strings.Contains(joined, "💵 $50") {
		t.Errorf("cost is not the list-rate figure: %q", joined)
	}
}

// COVERS: FR-8.18 | negative
//
// The saving is shown only when there IS one. A session that wrote cache blocks
// and never read them back spent MORE than it would have with no caching, so
// the figure is negative and the clause is dropped rather than printed as a
// loss. The total is still shown.
func TestSavingIsDroppedWhenItIsNegative(t *testing.T) {
	// A 1h write costs double the plain input rate and is never read back.
	loss := priced(t, usageLine(`"ephemeral_1h_input_tokens":10000000`))
	joined := strings.Join(render.Build(loss, "/home/me", 220, clock), "\n")
	if strings.Contains(joined, "saved") {
		t.Errorf("a loss was printed as a saving: %q", joined)
	}
	if !strings.Contains(joined, "💵") {
		t.Errorf("the total went with the saving: %q", joined)
	}

	// And it IS shown when reads make it positive.
	gain := priced(t, usageLine(`"cache_read_input_tokens":100000000`))
	joined = strings.Join(render.Build(gain, "/home/me", 220, clock), "\n")
	if !strings.Contains(joined, "🎯") || !strings.Contains(joined, "saved") {
		t.Errorf("a real saving was not shown: %q", joined)
	}
}

// COVERS: FR-5.8 | property
//
// The cost goes on the METER row rather than the identity row, because the
// identity row has already given its slack to the context bar. With no meter
// row there is nowhere for it that is not somewhere else's space.
func TestCostRidesTheMeterRowAndIsDroppedWithoutOne(t *testing.T) {
	data := priced(t, usageLine(`"input_tokens":10000000`))
	got := render.Build(data, "/home/me", 220, clock)
	if len(got) != 2 {
		t.Fatalf("expected two rows, got %d", len(got))
	}
	if strings.Contains(got[0], "💵") {
		t.Errorf("cost landed on the identity row: %q", got[0])
	}
	if !strings.Contains(got[1], "💵") {
		t.Errorf("cost is not on the meter row: %q", got[1])
	}

	// No rate limits, so no meter row, so nowhere for it.
	delete(data, "rate_limits")
	delete(data, "context_window")
	lone := render.Build(data, "/home/me", 220, clock)
	if len(lone) != 1 {
		t.Fatalf("expected one row, got %d", len(lone))
	}
	if strings.Contains(lone[0], "💵") {
		t.Errorf("cost appeared with no meter row to carry it: %q", lone[0])
	}
}

// COVERS: FR-5.7 | property
//
// Pushed to the RIGHT with at least three columns of clear space, shortened to
// the total alone when the full form will not fit, and dropped when neither
// will.
// form is which of the three shapes the cost took at a width.
type form int

const (
	dropped form = iota
	shortened
	whole
)

func costForm(t *testing.T, data payload.Map, width int) form {
	t.Helper()
	got := strings.Join(render.Build(data, "/home/me", width, clock), "\n")
	switch {
	case strings.Contains(got, "saved"):
		return whole
	case strings.Contains(got, "💵"):
		return shortened
	default:
		return dropped
	}
}

// COVERS: FR-5.7 | property
//
// It gives up DETAIL before it gives up the row, in order: the full form, then
// the total alone, then nothing. The assertion is the ordering across every
// width rather than three magic numbers, because which width crosses which
// boundary depends on how wide the meter row already is.
func TestCostShortensThenDropsAsTheRowNarrows(t *testing.T) {
	data := priced(t, usageLine(`"cache_read_input_tokens":100000000`))
	// Both windows, so the meter row is wide enough that the cost has to
	// compete for the slack rather than always fitting.
	data["rate_limits"] = map[string]any{
		"five_hour": map[string]any{"used_percentage": 34.0, "resets_at": at(3600)},
		"seven_day": map[string]any{"used_percentage": 62.0, "resets_at": at(86400)},
	}

	seen := map[form]int{}
	previous := whole
	for width := 240; width >= 40; width-- {
		got := costForm(t, data, width)
		if _, already := seen[got]; !already {
			seen[got] = width
		}
		// It gives up detail before it gives up the row, and never recovers a
		// form it has already surrendered as the pane keeps shrinking.
		if got > previous {
			t.Errorf("width %d went back to a fuller form than %d had", width, width+1)
		}
		previous = got
	}

	for shape, name := range map[form]string{
		whole: "the full form", shortened: "the total alone", dropped: "dropped",
	} {
		if _, ok := seen[shape]; !ok {
			t.Errorf("no width between 40 and 240 produced %s", name)
		}
	}
	if seen[whole] <= seen[shortened] || seen[shortened] <= seen[dropped] {
		t.Errorf("the three forms are not ordered by width: %v", seen)
	}
}

// COVERS: FR-5.7 | edge
func TestCostKeepsThreeColumnsOfClearSpace(t *testing.T) {
	data := priced(t, usageLine(`"input_tokens":10000000`))
	for width := 120; width <= 240; width += 4 {
		got := render.Build(data, "/home/me", width, clock)
		meters := got[len(got)-1]
		cut := strings.Index(meters, "💵")
		if cut < 0 {
			continue
		}
		gap := len(meters[:cut]) - len(strings.TrimRight(meters[:cut], " "))
		if gap < 3 {
			t.Errorf("width %d: only %d columns before the cost", width, gap)
		}
	}
}

// COVERS: FR-5.9 | property
//
// Both cost forms come from ONE reading of the transcripts. Deciding which of
// two strings fits must not double the only expensive thing on the row, and the
// consequence a test can see is that the two forms carry the SAME total: a
// second read would advance the offsets and the shortened form would price only
// what the first read had not already counted.
func TestBothCostFormsCarryTheSameTotal(t *testing.T) {
	data := priced(t, usageLine(`"cache_read_input_tokens":100000000`))
	data["rate_limits"] = map[string]any{
		"five_hour": map[string]any{"used_percentage": 34.0, "resets_at": at(3600)},
		"seven_day": map[string]any{"used_percentage": 62.0, "resets_at": at(86400)},
	}

	var wide, narrow string
	for width := 240; width >= 40; width-- {
		got := strings.Join(render.Build(data, "/home/me", width, clock), "\n")
		if wide == "" && strings.Contains(got, "saved") {
			wide = got
		}
		if narrow == "" && strings.Contains(got, "💵") && !strings.Contains(got, "saved") {
			narrow = got
		}
	}
	if wide == "" || narrow == "" {
		t.Fatal("never saw both forms")
	}
	if total := money(wide); total == "" || total != money(narrow) {
		t.Errorf("the two forms disagree on the total: %q against %q",
			money(wide), money(narrow))
	}
}

// money pulls the dollar figure that follows the note marker.
func money(row string) string {
	cut := strings.Index(row, "💵 ")
	if cut < 0 {
		return ""
	}
	rest := row[cut+len("💵 "):]
	if end := strings.IndexAny(rest, " \n"); end >= 0 {
		return rest[:end]
	}
	return rest
}
