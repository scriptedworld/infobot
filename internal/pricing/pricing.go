// Package pricing says what a session would have cost through the API.
//
// I am on a subscription, so nothing here is a bill. It is the counterfactual:
// what the same tokens would have come to had they gone through the API, which
// is what makes the cache worth anything visible.
//
// THE RATES ARE A CACHED COPY AND THEY DRIFT. A status line that runs on every
// event cannot make a network call to ask, so they are read from a table on
// disk and refreshed by something else.
//
// ~/.config/infobot/pricing.json is that table and wins when it is there.
// Config rather than state, because the offsets under ~/.local/state/infobot
// are disposable machine bookkeeping and this is a file worth editing by hand.
// Sonnet 5 carrying an introductory rate with an expiry is the case that wants
// a person, not a fetch.
//
// That case has since resolved, and how it resolved is the argument. The
// introductory $2/$10 became the standard price and the $3/$15 rise booked for
// September was cancelled. A fetch reading only the table would have carried
// the right numbers by luck; what said the rise was cancelled was a sentence
// beside the table, and a scheduled refresh that had run in September without
// one would have had no way to tell a cancelled increase from an unapplied one.
//
// The table below is the seed and the fallback, so a fresh clone renders with
// no config file and no network. Both carry the date they were taken.
//
// A model absent from the table is priced at nothing and reported as tokens. A
// cost computed from a guessed rate is worse than no cost.
package pricing

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

const (
	Taken  = "2026-08-28"
	Source = "https://platform.claude.com/docs/en/about-claude/pricing"
)

// Rate is input and output dollars per million tokens, read from Source.
type Rate struct {
	In  float64
	Out float64
}

var seedRates = map[string]Rate{
	"claude-fable-5":            {10.00, 50.00},
	"claude-mythos-5":           {10.00, 50.00},
	"claude-opus-5":             {5.00, 25.00},
	"claude-opus-4-8":           {5.00, 25.00},
	"claude-opus-4-7":           {5.00, 25.00},
	"claude-opus-4-6":           {5.00, 25.00},
	"claude-opus-4-5":           {5.00, 25.00},
	"claude-opus-4-1":           {15.00, 75.00},
	"claude-opus-4-0":           {15.00, 75.00},
	"claude-sonnet-5":           {2.00, 10.00},
	"claude-sonnet-4-6":         {3.00, 15.00},
	"claude-sonnet-4-5":         {3.00, 15.00},
	"claude-sonnet-4-0":         {3.00, 15.00},
	"claude-haiku-4-5":          {1.00, 5.00},
	"claude-3-5-haiku-20241022": {0.80, 4.00},
}

// Multipliers on the input rate, uniform across models: the per-model cache
// columns at Source are these applied. A cache read is CHARGED at a tenth, 90%
// off rather than free, and on a long session it is the largest single line. A
// write costs more than a fresh input token, which is why the two are priced
// apart rather than lumped together as "cache".

// SeedCacheRead is the read multiplier: a tenth of the input rate.
const SeedCacheRead = 0.1

var seedCacheWrite = map[string]float64{
	"ephemeral_5m_input_tokens": 1.25,
	"ephemeral_1h_input_tokens": 2.0,
}

// Table is the rate table, from disk or from the seed.
type Table struct {
	Taken      string             `json:"taken"`
	Source     string             `json:"source"`
	Rates      map[string]Rate    `json:"rates"`
	CacheRead  *float64           `json:"cache_read"`
	CacheWrite map[string]float64 `json:"cache_write"`
}

// Read returns the cache read multiplier, falling back to the seed.
func (t Table) Read() float64 {
	if t.CacheRead == nil {
		return SeedCacheRead
	}
	return *t.CacheRead
}

// Writes returns the cache write multipliers, falling back to the seed.
func (t Table) Writes() map[string]float64 {
	if len(t.CacheWrite) == 0 {
		return seedCacheWrite
	}
	return t.CacheWrite
}

func seed() Table {
	return Table{Taken: Taken, Source: Source, Rates: seedRates, CacheWrite: seedCacheWrite}
}

// TablePath is where the rates on disk live.
func TablePath() string {
	root := os.Getenv("XDG_CONFIG_HOME")
	if root == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return ""
		}
		root = filepath.Join(home, ".config")
	}
	return filepath.Join(root, "infobot", "pricing.json")
}

// Load returns the rates on disk, or the seed when there are none to be had.
//
// Anything malformed falls back rather than failing. A status line that fails
// shows nothing at all, and a stale rate is a smaller wrong than a blank row.
func Load() Table {
	path := TablePath()
	if path == "" {
		return seed()
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return seed()
	}
	// Rates arrive as [input, output] pairs, which is the shape the seed is
	// written in and the shape a person editing the file would copy.
	var wire struct {
		Taken      string               `json:"taken"`
		Source     string               `json:"source"`
		Rates      map[string][]float64 `json:"rates"`
		CacheRead  *float64             `json:"cache_read"`
		CacheWrite map[string]float64   `json:"cache_write"`
	}
	if json.Unmarshal(raw, &wire) != nil || len(wire.Rates) == 0 {
		return seed()
	}
	rates := make(map[string]Rate, len(wire.Rates))
	for model, pair := range wire.Rates {
		if len(pair) >= 2 {
			rates[model] = Rate{In: pair[0], Out: pair[1]}
		}
	}
	if len(rates) == 0 {
		return seed()
	}
	return Table{wire.Taken, wire.Source, rates, wire.CacheRead, wire.CacheWrite}
}

// Priced is the outcome of pricing a session's totals.
type Priced struct {
	Spent    float64
	Saved    float64
	Complete bool
}

// Price returns dollars spent, dollars the cache took off, and whether that is
// all of it. The second return is false when nothing at all could be priced.
//
// totals is keyed by model, each holding the token counts as the transcript
// records them, and each is charged at its own rate: a session that ran work on
// several models is the sum of them, not an average.
//
// A model with no rate is left out and the total is flagged incomplete rather
// than abandoned, so one unknown model costs the exactness of the figure and
// not the figure.
//
// The saving is every cached token, read or written, charged at the plain input
// rate instead: what the session would have cost with no caching, less what it
// did cost.
func Price(totals map[string]map[string]float64) (Priced, bool) {
	if len(totals) == 0 {
		return Priced{}, false
	}
	table := Load()
	var spent, uncached, cached float64
	complete := true
	for model, counts := range totals {
		rate, known := table.Rates[model]
		if !known {
			complete = false
			continue
		}
		spent += counts["input_tokens"] / 1e6 * rate.In
		spent += counts["output_tokens"] / 1e6 * rate.Out

		var tokens float64
		for field, multiplier := range table.Writes() {
			tokens += counts[field]
			cached += counts[field] / 1e6 * rate.In * multiplier
			spent += counts[field] / 1e6 * rate.In * multiplier
		}
		reads := counts["cache_read_input_tokens"]
		tokens += reads
		cached += reads / 1e6 * rate.In * table.Read()
		spent += reads / 1e6 * rate.In * table.Read()
		uncached += tokens / 1e6 * rate.In
	}
	if spent == 0 {
		return Priced{}, false
	}
	return Priced{Spent: spent, Saved: uncached - cached, Complete: complete}, true
}

// Money prints dollars at the precision the number deserves rather than always
// two decimals.
func Money(dollars float64) string {
	if dollars >= 1000 {
		return fmt.Sprintf("$%.1fk", dollars/1000)
	}
	if dollars >= 100 {
		return fmt.Sprintf("$%.0f", dollars)
	}
	return fmt.Sprintf("$%.2f", dollars)
}
