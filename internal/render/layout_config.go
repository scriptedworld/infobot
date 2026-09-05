package render

import (
	"encoding/json"
	"os"
	"path/filepath"
)

// The margin is CONFIGURATION, for the same reason the palette is: it is a
// claim about the host's chrome that cannot be measured from inside this
// process, and getting it wrong is visible on every render.
//
// ~/.config/infobot/layout.json, a sibling of palette.json and deliberately a
// SEPARATE file. The palette is a symlink into g0bl1n.theme and is the same on
// any machine that adopts the theme; the margin is a fact about the terminal
// and the Claude Code build in front of it, so it belongs to the machine.

// marginMax bounds what the file may set. A margin wider than this is a
// mistyped number rather than a wide chrome, and honouring it would leave no
// room to render into, which looks identical to the bug it was meant to fix.
const marginMax = 40

type layoutFile struct {
	Margin *int `json:"margin"`
}

// LayoutPath is where the layout settings on disk live.
func LayoutPath() string {
	root := os.Getenv("XDG_CONFIG_HOME")
	if root == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return ""
		}
		root = filepath.Join(home, ".config")
	}
	return filepath.Join(root, "infobot", "layout.json")
}

// loadLayout overlays the file onto the compiled default.
//
// A pointer for Margin so that ABSENT and ZERO are different. A file setting
// margin to 0 is saying "the host reserves nothing", which is a legitimate
// answer for a bare terminal, and it must not read as "no value supplied".
func loadLayout() {
	path := LayoutPath()
	if path == "" {
		return
	}
	raw, err := os.ReadFile(path) //nolint:gosec // a path this process built
	if err != nil {
		return
	}
	var wire layoutFile
	if json.Unmarshal(raw, &wire) != nil || wire.Margin == nil {
		return
	}
	if *wire.Margin < 0 || *wire.Margin > marginMax {
		return
	}
	margin = *wire.Margin
}
