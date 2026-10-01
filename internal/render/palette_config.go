package render

import (
	"encoding/json"
	"fmt"
)

// The palette is configuration, not source. ~/.config/infobot/palette.json is
// read once, in Main, and wins when it is there.
//
// This follows internal/pricing exactly, including the reason: anything
// malformed falls back to the seed rather than failing, because a status line
// that fails shows nothing at all, where a seed colour only looks wrong.

// paletteFile is the wire shape. Hex strings rather than triples, because a
// person editing this copies them out of a palette that is written in hex.
type paletteFile struct {
	Taken   string            `json:"taken"`
	Source  string            `json:"source"`
	Colours map[string]string `json:"colours"`
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

// overlayPalette is the seed with whatever the file supplies laid over it.
//
// The pointers name which entry fills which field in one place, and an entry
// absent from the file leaves its seed untouched.
func overlayPalette(seed palette, raw []byte) palette {
	var wire paletteFile
	if json.Unmarshal(raw, &wire) != nil || len(wire.Colours) == 0 {
		return seed
	}
	result := seed
	into := map[string]*rgb{
		"green":      &result.green,
		"yellow":     &result.yellow,
		"red":        &result.red,
		"backdrop":   &result.backdrop,
		"alarm_fg":   &result.alarmFG,
		"alarm_bg":   &result.alarmBG,
		"pace_low":   &result.paceLow,
		"pace_under": &result.paceUnder,
	}
	for name, target := range into {
		if c, ok := parseHex(wire.Colours[name]); ok {
			*target = c
		}
	}

	// These four are escape sequences rather than rgb values, because they are
	// emitted directly rather than interpolated.
	escapes := map[string]*string{
		"path":      &result.pathFG,
		"empty":     &result.emptyFG,
		"separator": &result.sepFG,
		"dim":       &result.dimFG,
	}
	for name, target := range escapes {
		if c, ok := parseHex(wire.Colours[name]); ok {
			*target = c.fg()
		}
	}
	return result
}
