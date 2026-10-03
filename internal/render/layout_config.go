package render

import (
	"encoding/json"
	"os"
	"path/filepath"
)

// look is everything the rows are drawn with that configuration can change:
// the colours, and the columns the host keeps for itself.
//
// Main loads it once and passes it down. Nothing a render reads is a package
// variable, so one render cannot leave a value behind for the next, and a test
// draws with the seed whatever this machine has configured.
type look struct {
	palette
	margin int
}

// defaultMargin is what Claude Code keeps for itself, so the pane width is not
// the budget.
//
// 3 is the default and is too small on this machine. It assumes the host
// indents two columns and keeps one at the right. At a real 313 columns it
// renders 309, and Claude Code cuts both rows with its own ellipsis, losing the
// end of the session id and the saved figure. 8 renders complete.
//
// The default stays 3, deliberately. Raising it breaks
// TestCostShortensThenDropsAsTheRowNarrows at width 79, which asserts the cost
// never recovers a form it has already surrendered as the pane narrows. That
// is a real non-monotonicity in the layout, latent at 3 and exposed at 8, and
// editing the test to pass would hide the bug it exists to catch. This machine
// sets 8 in layout.json instead.
//
// The number is a claim about the host's chrome, which this process cannot
// measure from the inside, and being wrong by a column truncates every render.
// So it is configuration, adjusted against what is actually drawn without a
// rebuild.
const defaultMargin = 3

// marginMax bounds what the file may set. A margin wider than this is a
// mistyped number rather than a wide chrome, and honouring it would leave no
// room to render into, which looks identical to the bug it was meant to fix.
const marginMax = 40

func seedLook() look {
	return look{palette: seedPalette(), margin: defaultMargin}
}

// configuredLook is the seed with palette.json and layout.json laid over it.
//
// Two files, deliberately. The palette is a symlink into g0bl1n.theme and is
// the same on any machine that adopts the theme; the margin is a fact about
// the terminal and the Claude Code build in front of it, so it belongs to the
// machine.
func configuredLook() look {
	result := seedLook()
	if raw := readConfig("palette.json"); raw != nil {
		result.palette = overlayPalette(result.palette, raw)
	}
	if raw := readConfig("layout.json"); raw != nil {
		result.margin = overlayMargin(result.margin, raw)
	}
	return result
}

// readConfig is one of infobot's files under the XDG config home, or nil.
//
// A missing file is the normal case on a machine that has not configured one,
// and an unreadable one is treated the same way: the seed stands. The read goes
// through a root on infobot's config directory, so it cannot leave it.
func readConfig(name string) []byte {
	base := os.Getenv("XDG_CONFIG_HOME")
	if base == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return nil
		}
		base = filepath.Join(home, ".config")
	}
	root, err := os.OpenRoot(filepath.Join(base, "infobot"))
	if err != nil {
		return nil
	}
	defer func() { _ = root.Close() }()
	raw, err := root.ReadFile(name)
	if err != nil {
		return nil
	}
	return raw
}

type layoutFile struct {
	Margin *int `json:"margin"`
}

// overlayMargin is the file's margin where it gives a usable one.
//
// A pointer for Margin so that absent and zero are different. A file setting
// margin to 0 is saying "the host reserves nothing", which is a legitimate
// answer for a bare terminal, and it must not read as "no value supplied".
func overlayMargin(seed int, raw []byte) int {
	var wire layoutFile
	if json.Unmarshal(raw, &wire) != nil || wire.Margin == nil {
		return seed
	}
	if *wire.Margin < 0 || *wire.Margin > marginMax {
		return seed
	}
	return *wire.Margin
}
