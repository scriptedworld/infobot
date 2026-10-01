package pricing_test

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/scriptedworld/infobot/internal/pricing"
)

// standard is a rate table shaped like the one on disk, with the multipliers
// the pricing page gives. Nothing is compiled in, so every test that prices
// anything names the table it prices from.
const standard = `{
  "taken": "2099-01-01",
  "source": "https://example.invalid/pricing",
  "cache_read": 0.1,
  "cache_write": {"ephemeral_5m_input_tokens": 1.25, "ephemeral_1h_input_tokens": 2.0},
  "rates": {
    "claude-opus-5": [5.0, 25.0],
    "claude-haiku-4-5": [1.0, 5.0],
    "claude-opus-5-5": [4.0, 20.0, 0.05],
    "claude-fable-5-1": [10.0, 50.0, 0.025]
  }
}`

// withTable points the config directory at a fresh one holding content as the
// rate table.
func withTable(t *testing.T, content string) {
	t.Helper()
	dir := t.TempDir()
	t.Setenv("XDG_CONFIG_HOME", dir)
	if err := os.MkdirAll(filepath.Join(dir, "infobot"), 0o750); err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(dir, "infobot", "pricing.json")
	if err := os.WriteFile(path, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
}

func totals(model string, fields map[string]float64) map[string]map[string]float64 {
	return map[string]map[string]float64{model: fields}
}

// COVERS FR-8.17 | property
func TestMoneyPrintsThePrecisionTheNumberDeserves(t *testing.T) {
	for _, c := range []struct {
		in   float64
		want string
	}{
		{0, "$0.00"},
		{1.5, "$1.50"},
		{99.994, "$99.99"},
		{100, "$100"},
		{263.4, "$263"},
		{999, "$999"},
		{1000, "$1.0k"},
		// Ties break to even here too, so 1.25k is 1.2k and 1.35k is 1.4k.
		// Measured against the Python this replaced rather than assumed.
		{1250, "$1.2k"},
		{1350, "$1.4k"},
	} {
		if got := pricing.Money(c.in); got != c.want {
			t.Errorf("Money(%v) = %q, want %q", c.in, got, c.want)
		}
	}
}

// COVERS FR-8.11 | positive
func TestPriceChargesEachModelAtItsOwnRate(t *testing.T) {
	withTable(t, standard)
	both := map[string]map[string]float64{
		"claude-opus-5":    {"input_tokens": 1e6},
		"claude-haiku-4-5": {"input_tokens": 1e6},
	}
	got, ok := pricing.Price(both)
	if !ok {
		t.Fatal("Price returned nothing for two priced models")
	}
	// The sum of them, not an average: opus 5.00 plus haiku 1.00.
	if got.Spent != 6.0 {
		t.Errorf("Spent = %v, want 6.0", got.Spent)
	}
	if !got.Complete {
		t.Error("Complete = false, want true when every model has a rate")
	}
}

// COVERS FR-8.12 | negative
//
// One unknown model costs the exactness of the figure and not the figure.
func TestPriceFlagsAnUnknownModelWithoutAbandoningTheTotal(t *testing.T) {
	withTable(t, standard)
	mixed := map[string]map[string]float64{
		"claude-opus-5":            {"input_tokens": 1e6},
		"some-model-nobody-priced": {"input_tokens": 1e6},
	}
	got, ok := pricing.Price(mixed)
	if !ok {
		t.Fatal("Price abandoned a total it could partly compute")
	}
	if got.Spent != 5.0 {
		t.Errorf("Spent = %v, want 5.0 from the priced model alone", got.Spent)
	}
	if got.Complete {
		t.Error("Complete = true, want false when a model had no rate")
	}
}

// COVERS FR-8.26 | regression
//
// Claude Code writes an interrupted turn under "<synthetic>" with every count
// zero. Found on a Fable 5.1 session that rendered $997+ with nothing unpriced.
func TestAModelWithNoTokensLeavesTheTotalComplete(t *testing.T) {
	withTable(t, standard)
	got, ok := pricing.Price(map[string]map[string]float64{
		"claude-opus-5": {"input_tokens": 1e6},
		"<synthetic>":   {"input_tokens": 0, "output_tokens": 0, "cache_read_input_tokens": 0},
	})
	if !ok {
		t.Fatal("Price returned nothing")
	}
	if !got.Complete {
		t.Error("Complete = false, want true when the unpriced model carried no tokens")
	}
	if got.Spent != 5.0 {
		t.Errorf("Spent = %v, want 5.0", got.Spent)
	}
}

// COVERS FR-8.12 | edge
func TestPriceReturnsNothingWhenNothingCouldBePriced(t *testing.T) {
	withTable(t, standard)
	if _, ok := pricing.Price(nil); ok {
		t.Error("Price returned a figure for no totals")
	}
	if _, ok := pricing.Price(totals("unknown-model", map[string]float64{"input_tokens": 1e6})); ok {
		t.Error("Price returned a figure when no model could be priced")
	}
}

// COVERS FR-8.29 | property
//
// A cache read is CHARGED, at the table's cache_read for a model with no
// multiplier of its own. 90% off is not free.
func TestCacheReadsAreChargedAtTheTablesMultiplier(t *testing.T) {
	withTable(t, standard)
	got, ok := pricing.Price(totals("claude-opus-5", map[string]float64{
		"cache_read_input_tokens": 1e6,
	}))
	if !ok {
		t.Fatal("Price returned nothing for a cache-read-only session")
	}
	if got.Spent != 0.5 {
		t.Errorf("Spent = %v, want 0.5 (5.00 input rate at a tenth)", got.Spent)
	}
}

// COVERS FR-8.14 | property
//
// The saving is those tokens charged at the plain input rate instead, less what
// they did cost: 5.00 uncached against 0.50 charged.
func TestSavingIsTheUncachedCounterfactual(t *testing.T) {
	withTable(t, standard)
	got, _ := pricing.Price(totals("claude-opus-5", map[string]float64{
		"cache_read_input_tokens": 1e6,
	}))
	if got.Saved != 4.5 {
		t.Errorf("Saved = %v, want 4.5", got.Saved)
	}
}

// COVERS FR-8.29 | property
//
// A write costs MORE than a fresh input token, which is why the two are priced
// apart rather than lumped together as "cache".
func TestCacheWritesCostMoreThanFreshInput(t *testing.T) {
	withTable(t, standard)
	write, _ := pricing.Price(totals("claude-opus-5", map[string]float64{
		"ephemeral_1h_input_tokens": 1e6,
	}))
	fresh, _ := pricing.Price(totals("claude-opus-5", map[string]float64{
		"input_tokens": 1e6,
	}))
	if write.Spent <= fresh.Spent {
		t.Errorf("a 1h cache write cost %v, want more than fresh input at %v",
			write.Spent, fresh.Spent)
	}
	// Its saving is negative: it cost double what the plain rate would have.
	if write.Saved >= 0 {
		t.Errorf("Saved = %v, want negative for a write never read back", write.Saved)
	}
}

// COVERS FR-8.27 | positive
func TestLoadReadsTheTableOnDisk(t *testing.T) {
	withTable(t, standard)
	got, ok := pricing.Load()
	if !ok {
		t.Fatal("Load found no table")
	}
	if got.Taken != "2099-01-01" {
		t.Errorf("Taken = %q, want the file's date", got.Taken)
	}
	if got.Rates["claude-opus-5"].In != 5.0 {
		t.Errorf("rate = %v, want 5.0 from the file", got.Rates["claude-opus-5"].In)
	}
}

// COVERS FR-8.28 | negative
//
// Every way the table can fail to be usable leaves no figure, rather than a
// figure priced from anything else.
func TestNoUsableTableLeavesNoFigure(t *testing.T) {
	writes := `"cache_write":{"ephemeral_5m_input_tokens":1.25}`
	for _, c := range []struct {
		name    string
		content string
		write   bool
	}{
		{"missing", "", false},
		{"malformed", "{not json", true},
		{"no rates", `{"cache_read":0.1,` + writes + `,"rates":{}}`, true},
		{"only short rates", `{"cache_read":0.1,` + writes + `,"rates":{"m":[1.0]}}`, true},
		{"no cache_read", `{` + writes + `,"rates":{"m":[1.0,2.0]}}`, true},
		{"no cache_write", `{"cache_read":0.1,"rates":{"m":[1.0,2.0]}}`, true},
	} {
		t.Run(c.name, func(t *testing.T) {
			if c.write {
				withTable(t, c.content)
			} else {
				t.Setenv("XDG_CONFIG_HOME", t.TempDir())
			}
			if _, ok := pricing.Load(); ok {
				t.Error("Load reported a usable table")
			}
			if _, ok := pricing.Price(totals("m", map[string]float64{"input_tokens": 1e6})); ok {
				t.Error("Price returned a figure with no usable table")
			}
		})
	}
}

// COVERS FR-8.23 | property
func TestCacheMultipliersAreConfigurable(t *testing.T) {
	// A cache read priced at half rather than at a tenth, with no code change.
	withTable(t, `{"rates":{"m":[10.0,20.0]},"cache_read":0.5,
		"cache_write":{"ephemeral_5m_input_tokens":1.25}}`)
	got, ok := pricing.Price(totals("m", map[string]float64{"cache_read_input_tokens": 1e6}))
	if !ok {
		t.Fatal("Price returned nothing")
	}
	if got.Spent != 5.0 {
		t.Errorf("Spent = %v, want 5.0 (10.00 at the configured half)", got.Spent)
	}
}

// COVERS FR-8.29 | positive
//
// A third number on a rate is that model's read multiplier, and a model without
// one reads at the table's cache_read.
func TestAThirdNumberOnARateIsItsReadMultiplier(t *testing.T) {
	withTable(t, standard)
	for _, c := range []struct {
		model string
		want  float64
	}{
		{"claude-opus-5-5", 0.2},   // 4.00 at 0.05
		{"claude-fable-5-1", 0.25}, // 10.00 at 0.025
		{"claude-opus-5", 0.5},     // 5.00 at the table's tenth
	} {
		got, ok := pricing.Price(totals(c.model, map[string]float64{"cache_read_input_tokens": 1e6}))
		if !ok {
			t.Fatalf("%s: Price returned nothing", c.model)
		}
		if got.Spent != c.want {
			t.Errorf("%s: Spent = %v, want %v", c.model, got.Spent, c.want)
		}
	}
}
