// Package pricing says what a session would have cost through the API.
//
// I am on a subscription, so nothing here is a bill. It is the counterfactual:
// what the same tokens would have come to had they gone through the API, which
// is what makes the cache worth anything visible.
//
// The rates are a cached copy and they drift. A status line that runs on every
// event cannot make a network call to ask, so they are read from a table on
// disk and refreshed by something else.
//
// ~/.config/infobot/pricing.json is that table. It is config, not state,
// because the offsets under ~/.local/state/infobot are disposable machine
// bookkeeping and this is a file edited by hand. Why a person refreshes it and
// not a fetch is in docs/PROJECT.md, "Perishable: the pricing table".
//
// Nothing is compiled in. The rates and the cache multipliers live only in that
// file, so a price change is an edit to it and never a rebuild. With no usable
// table the cost segment is left out, and a model missing from it leaves the
// total flagged as a floor. A cost computed from a guessed rate is worse than no
// cost, and a copy kept in the code is a guess once nobody refreshes it.
package pricing

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

// Rate is input and output dollars per million tokens.
type Rate struct {
	In  float64
	Out float64
}

// Table is the rate table on disk.
//
// The cache multipliers apply to the input rate. A cache read is CHARGED, 90%
// off rather than free, and on a long session it is the largest single line. A
// write costs more than a fresh input token, which is why the two are priced
// apart rather than lumped together as "cache". Writes are uniform across
// models; a read is CacheRead unless the model has its own in Reads.
type Table struct {
	Taken      string
	Source     string
	Rates      map[string]Rate
	CacheRead  float64
	CacheWrite map[string]float64
	Reads      map[string]float64
}

// ReadFor returns the cache read multiplier for one model: its own where the
// table gives one, and CacheRead otherwise.
func (t Table) ReadFor(model string) float64 {
	if multiplier, own := t.Reads[model]; own {
		return multiplier
	}
	return t.CacheRead
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

// Load returns the table on disk and whether there is a usable one.
//
// Usable means rates, cache_read and cache_write are all present. Anything less
// reports false rather than failing: a status line that fails shows nothing at
// all, and the caller drops only the cost segment.
func Load() (Table, bool) {
	path := TablePath()
	if path == "" {
		return Table{}, false
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return Table{}, false
	}
	// Rates arrive as [input, output] pairs, the shape a person editing the file
	// would copy. A third number is that model's own cache read multiplier.
	var wire struct {
		Taken      string               `json:"taken"`
		Source     string               `json:"source"`
		Rates      map[string][]float64 `json:"rates"`
		CacheRead  *float64             `json:"cache_read"`
		CacheWrite map[string]float64   `json:"cache_write"`
	}
	if json.Unmarshal(raw, &wire) != nil || wire.CacheRead == nil || len(wire.CacheWrite) == 0 {
		return Table{}, false
	}
	rates := make(map[string]Rate, len(wire.Rates))
	reads := map[string]float64{}
	for model, pair := range wire.Rates {
		if len(pair) >= 2 {
			rates[model] = Rate{In: pair[0], Out: pair[1]}
		}
		if len(pair) >= 3 {
			reads[model] = pair[2]
		}
	}
	if len(rates) == 0 {
		return Table{}, false
	}
	return Table{wire.Taken, wire.Source, rates, *wire.CacheRead, wire.CacheWrite, reads}, true
}

// Priced is the outcome of pricing a session's totals.
type Priced struct {
	Spent    float64
	Saved    float64
	Complete bool
}

// Price returns dollars spent, dollars the cache took off, and whether that is
// all of it. The second return is false when nothing at all could be priced,
// which includes there being no usable rate table.
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
	table, usable := Load()
	if !usable {
		return Priced{}, false
	}
	var spent, uncached, cached float64
	complete := true
	for model, counts := range totals {
		// Claude Code records an interrupted or failed turn under the model
		// "<synthetic>" with every count zero. Nothing is missing from the
		// figure, so it must not be flagged as a floor.
		if noTokens(counts) {
			continue
		}
		rate, known := table.Rates[model]
		if !known {
			complete = false
			continue
		}
		spent += counts["input_tokens"] / 1e6 * rate.In
		spent += counts["output_tokens"] / 1e6 * rate.Out

		var tokens float64
		for field, multiplier := range table.CacheWrite {
			tokens += counts[field]
			cached += counts[field] / 1e6 * rate.In * multiplier
			spent += counts[field] / 1e6 * rate.In * multiplier
		}
		reads := counts["cache_read_input_tokens"]
		tokens += reads
		cached += reads / 1e6 * rate.In * table.ReadFor(model)
		spent += reads / 1e6 * rate.In * table.ReadFor(model)
		uncached += tokens / 1e6 * rate.In
	}
	if spent == 0 {
		return Priced{}, false
	}
	return Priced{Spent: spent, Saved: uncached - cached, Complete: complete}, true
}

func noTokens(counts map[string]float64) bool {
	for _, n := range counts {
		if n != 0 {
			return false
		}
	}
	return true
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
