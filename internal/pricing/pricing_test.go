package pricing_test

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/scriptedworld/infobot/internal/pricing"
)

func totals(model string, fields map[string]float64) map[string]map[string]float64 {
	return map[string]map[string]float64{model: fields}
}

// COVERS: FR-8.17 | property
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

// COVERS: FR-8.11 | positive
func TestPriceChargesEachModelAtItsOwnRate(t *testing.T) {
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

// COVERS: FR-8.12 | negative
//
// One unknown model costs the exactness of the figure and not the figure.
func TestPriceFlagsAnUnknownModelWithoutAbandoningTheTotal(t *testing.T) {
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

// COVERS: FR-8.12 | edge
func TestPriceReturnsNothingWhenNothingCouldBePriced(t *testing.T) {
	if _, ok := pricing.Price(nil); ok {
		t.Error("Price returned a figure for no totals")
	}
	if _, ok := pricing.Price(totals("unknown-model", map[string]float64{"input_tokens": 1e6})); ok {
		t.Error("Price returned a figure when no model could be priced")
	}
}

// COVERS: FR-8.13 | property
//
// A cache read is CHARGED, at a tenth of input. 90% off is not free.
func TestCacheReadsAreChargedAtATenth(t *testing.T) {
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

// COVERS: FR-8.14 | property
//
// The saving is those tokens charged at the plain input rate instead, less what
// they did cost: 5.00 uncached against 0.50 charged.
func TestSavingIsTheUncachedCounterfactual(t *testing.T) {
	got, _ := pricing.Price(totals("claude-opus-5", map[string]float64{
		"cache_read_input_tokens": 1e6,
	}))
	if got.Saved != 4.5 {
		t.Errorf("Saved = %v, want 4.5", got.Saved)
	}
}

// COVERS: FR-8.13 | property
//
// A write costs MORE than a fresh input token, which is why the two are priced
// apart rather than lumped together as "cache".
func TestCacheWritesCostMoreThanFreshInput(t *testing.T) {
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

// COVERS: FR-8.15 | positive
func TestLoadPrefersTheTableOnDisk(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("XDG_CONFIG_HOME", dir)
	if err := os.MkdirAll(filepath.Join(dir, "infobot"), 0o755); err != nil {
		t.Fatal(err)
	}
	table := `{"taken":"2099-01-01","rates":{"only-model":[7.0,9.0]}}`
	if err := os.WriteFile(filepath.Join(dir, "infobot", "pricing.json"), []byte(table), 0o644); err != nil {
		t.Fatal(err)
	}
	got := pricing.Load()
	if got.Taken != "2099-01-01" {
		t.Errorf("Taken = %q, want the file's date", got.Taken)
	}
	if got.Rates["only-model"].In != 7.0 {
		t.Errorf("rate = %v, want 7.0 from the file", got.Rates["only-model"].In)
	}
}

// COVERS: FR-8.16 | negative
//
// Missing, malformed and carrying no rates all fall back rather than failing. A
// stale rate is a smaller wrong than a blank row.
func TestLoadFallsBackToTheSeed(t *testing.T) {
	for _, c := range []struct {
		name    string
		content string
		write   bool
	}{
		{"missing", "", false},
		{"malformed", "{not json", true},
		{"no rates", `{"taken":"2099-01-01","rates":{}}`, true},
	} {
		t.Run(c.name, func(t *testing.T) {
			dir := t.TempDir()
			t.Setenv("XDG_CONFIG_HOME", dir)
			if c.write {
				if err := os.MkdirAll(filepath.Join(dir, "infobot"), 0o755); err != nil {
					t.Fatal(err)
				}
				if err := os.WriteFile(filepath.Join(dir, "infobot", "pricing.json"),
					[]byte(c.content), 0o644); err != nil {
					t.Fatal(err)
				}
			}
			got := pricing.Load()
			if got.Taken != pricing.Taken {
				t.Errorf("Taken = %q, want the seed's %q", got.Taken, pricing.Taken)
			}
			if _, ok := got.Rates["claude-opus-5"]; !ok {
				t.Error("seed rates absent after fallback")
			}
		})
	}
}

// COVERS: FR-8.23 | property
func TestCacheMultipliersAreConfigurable(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("XDG_CONFIG_HOME", dir)
	if err := os.MkdirAll(filepath.Join(dir, "infobot"), 0o755); err != nil {
		t.Fatal(err)
	}
	// A cache read priced at half rather than at a tenth, with no code change.
	table := `{"rates":{"m":[10.0,20.0]},"cache_read":0.5}`
	if err := os.WriteFile(filepath.Join(dir, "infobot", "pricing.json"), []byte(table), 0o644); err != nil {
		t.Fatal(err)
	}
	got, ok := pricing.Price(totals("m", map[string]float64{"cache_read_input_tokens": 1e6}))
	if !ok {
		t.Fatal("Price returned nothing")
	}
	if got.Spent != 5.0 {
		t.Errorf("Spent = %v, want 5.0 (10.00 at the configured half)", got.Spent)
	}
}
