package render

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

// The palette is CONFIGURATION, not source. ~/.config/infobot/palette.json is
// read at startup and wins when it is there.
//
// This follows internal/pricing exactly, including the reason: anything
// malformed FALLS BACK rather than failing, because a status line that fails
// shows nothing at all and the wrong colour is a smaller wrong than a blank
// row. A missing file is the normal case on a machine that has not themed it.
//
// The seed below is D1C3 Goblin, so an unthemed machine still matches the
// terminals rather than reverting to something older. On this estate the file
// is a symlink into g0bl1n.theme, which is what makes a re-cut of the palette
// reach the status line without a rebuild.

// paletteFile is the wire shape. Hex strings rather than triples, because a
// person editing this copies them out of a palette that is written in hex.
type paletteFile struct {
	Taken   string            `json:"taken"`
	Source  string            `json:"source"`
	Colours map[string]string `json:"colours"`
}

// PalettePath is where the colours on disk live.
func PalettePath() string {
	root := os.Getenv("XDG_CONFIG_HOME")
	if root == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return ""
		}
		root = filepath.Join(home, ".config")
	}
	return filepath.Join(root, "infobot", "palette.json")
}

// parseHex turns "#RRGGBB" into an rgb, reporting whether it could.
//
// A bad entry is rejected on its own rather than poisoning the file: one
// mistyped colour keeps its seed value and every other colour still loads.
func parseHex(s string) (rgb, bool) {
	var r, g, b int
	if len(s) != 7 || s[0] != '#' {
		return rgb{}, false
	}
	if _, err := fmt.Sscanf(s[1:], "%02x%02x%02x", &r, &g, &b); err != nil {
		return rgb{}, false
	}
	return rgb{r, g, b}, true
}

// loadPalette overlays whatever the file supplies onto the seed values.
//
// It takes pointers so a caller states which name fills which variable in one
// place, and so a name absent from the file leaves its seed untouched.
func loadPalette() {
	path := PalettePath()
	if path == "" {
		return
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return
	}
	var wire paletteFile
	if json.Unmarshal(raw, &wire) != nil || len(wire.Colours) == 0 {
		return
	}

	into := map[string]*rgb{
		"green":      &green,
		"yellow":     &yellow,
		"red":        &red,
		"backdrop":   &backdrop,
		"alarm_fg":   &alarmFG,
		"alarm_bg":   &alarmBG,
		"pace_low":   &paceStops[0].colour,
		"pace_under": &paceStops[1].colour,
	}
	for name, target := range into {
		if c, ok := parseHex(wire.Colours[name]); ok {
			*target = c
		}
	}

	// These four are escape sequences rather than rgb values, because they are
	// emitted directly rather than interpolated.
	escapes := map[string]*string{
		"path":      &pathColour,
		"empty":     &emptyColour,
		"separator": &sepColour,
		"dim":       &dimColour,
	}
	for name, target := range escapes {
		if c, ok := parseHex(wire.Colours[name]); ok {
			*target = c.fg()
		}
	}

	// The stops after the first two are the ramp colours themselves, so they
	// follow green/yellow/red rather than being named again.
	paceStops[2].colour = green
	paceStops[3].colour = yellow
	paceStops[4].colour = red
}
