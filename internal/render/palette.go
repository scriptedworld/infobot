package render

import (
	"fmt"
	"os"

	"github.com/scriptedworld/infobot/internal/num"
)

// TRUECOLOR. COLORTERM=truecolor and CLAUDE_CODE_TMUX_TRUECOLOR=1 are set in
// settings.json, which is what defeats Claude Code's habit of capping colour at
// the 256 palette when it sees $TMUX. A 24-bit escape reaches the terminal
// intact, so the ramp can be continuous instead of stepped.
//
// Two straight lines rather than one. Green to yellow across the long stretch
// where nothing is happening, then yellow to red compressed into 75-90, so the
// colour moves fastest exactly where a glance needs to tell 80 from 88.
// EVERY COLOUR BELOW IS A SEED, NOT THE SETTING. palette_config.go overlays
// ~/.config/infobot/palette.json over these at startup, so the palette is
// configuration and a re-cut reaches the status line without a rebuild. On this
// estate that file is a symlink into g0bl1n.theme.
//
// The seed is D1C3 Goblin, so a machine with no palette.json still matches the
// terminal behind it. It replaced a MIXTURE on 2026-09-05: the backdrop was
// Tokyo Night while the path and empty colours were still ENCOM's teal, so the
// status line matched neither the terminal nor itself.
var (
	green  = rgb{43, 255, 158} //nolint:gochecknoglobals // #2BFF9E, built once
	yellow = rgb{255, 212, 38} //nolint:gochecknoglobals // #FFD426
	red    = rgb{255, 46, 110} //nolint:gochecknoglobals // #FF2E6E
)

const (
	pivot    = 75.0
	alarmAt  = 90.0
	alarmTop = 100.0 // where the alarm has arrived in full
	reset    = "\033[0m"
)

// The alarm FADES IN across 90 to 100 rather than switching on at 90.
//
// Its foreground starts at red, which is exactly where the ramp below it
// arrives, so nothing jumps at the boundary: the last yellow-to-red cell and
// the first alarm cell are the same colour. It then runs to a pale yellow.
//
// Its background starts at the TERMINAL'S OWN and fills to a deep red, so the
// first frame of the fade paints nothing a reader can see and the inversion
// arrives instead of slamming on.
//
// BLACK IS NOT INVISIBLE, which is the trap this walked into first. The palette
// here is D1C3 Goblin, #0D0A20 at full opacity in kitty and inherited by herdr,
// so a pure black background is a dark notch against it: a seam exactly where
// the fade exists to have none. Matching the backdrop is what makes it vanish.
//
// It follows the desktop's palette rather than being picked, so a theme change
// moves it. Read it out of kitty.conf if that happens.
//
// Bold is the one part that cannot fade, so it is on across the whole band.
var (
	//nolint:gochecknoglobals // #0D0A20, the terminal's own background
	backdrop = rgb{13, 10, 32}
	//nolint:gochecknoglobals // #FFE85C bright_yellow, far end of the foreground fade
	alarmFG = rgb{255, 232, 92}
	//nolint:gochecknoglobals // deep red, far end of the background fade. Derived:
	// #FF2E6E at 45%, because the palette carries no red dark enough to sit
	// under text and still read as an alarm rather than as a block of colour.
	alarmBG = rgb{115, 21, 50}
)

// The accent, the same value the window frames and the active tag use, so the
// status line reads as part of the desktop instead of beside it. This was
// ENCOM's teal (#00a595) until 2026-09-05, long after ENCOM stopped being the
// palette anywhere else.
// var rather than const: palette.json overwrites these at startup. See
// palette_config.go.
var pathColour = "\033[38;2;255;43;214m" //nolint:gochecknoglobals // #FF2BD6 accent

// The unused cells are an outline glyph and NO background. The glyph carries
// its own shape, and a background behind it would fill the gaps between the
// parallelograms and turn the tail of the bar into a solid slab.
//
// A DIM CYAN, derived: #1FE0FF at 40%, because the palette carries no colour
// both dim enough to read as unused and cool enough to keep the pace scale
// ordered. It is the faithful translation of what this was, ENCOM's dim cyan.
//
// THE RED CHANNEL IS LOAD-BEARING, which is not obvious. The pace scale is
// checked by asserting that red decreases from hot through on-rate to cold,
// and `firstFG` on a barely-used bar picks up THIS colour rather than a pace
// colour. Muted (#6E5A9E, red 110) sits above green (#2BFF9E, red 43) and
// inverts that order; this (red 12) sits below it. Tried muted first and the
// pace test caught it.
var emptyColour = "\033[38;2;12;90;102m" //nolint:gochecknoglobals // #0C5A66, dim cyan

var (
	sepColour = "\033[38;2;59;21;102m"   //nolint:gochecknoglobals // #3B1566 selection
	dimColour = "\033[38;2;142;124;195m" //nolint:gochecknoglobals // #8E7CC3 dark_foreground
)

// paceStop is one anchor on the diverging pace scale.
type paceStop struct {
	at     float64
	colour rgb
}

// The pace stops, here because they are built from the palette above.
var paceStops = []paceStop{ //nolint:gochecknoglobals // colours built once and read per render
	{0.0, rgb{46, 123, 255}},   // #2E7BFF blue: the window is barely being touched
	{70.0, rgb{211, 198, 245}}, // #D3C6F5 light_foreground: under-spending it
	{100.0, green},             // lands exactly full as it resets
	{125.0, yellow},            // empties a fifth of the way early
	{150.0, red},               // empties a third of the way early
}

type rgb struct{ r, g, b int }

// mix interpolates between two colours, rounding each channel the way Python's
// round() does. See internal/num for why that matters.
func mix(a, b rgb, t float64) rgb {
	t = num.Clamp(t, 0, 1)
	return rgb{
		num.RoundInt(float64(a.r) + float64(b.r-a.r)*t),
		num.RoundInt(float64(a.g) + float64(b.g-a.g)*t),
		num.RoundInt(float64(a.b) + float64(b.b-a.b)*t),
	}
}

func (c rgb) fg() string {
	return fmt.Sprintf("\033[38;2;%d;%d;%dm", c.r, c.g, c.b)
}

// plain reports whether every escape is to be dropped.
//
// NO_COLOR is honoured for EVERY escape, not just the obvious ones. A hardcoded
// separator or bracket surviving NO_COLOR=1 stripping the segments around it
// gives output that is neither coloured nor clean.
func plain() bool {
	return os.Getenv("NO_COLOR") != ""
}

// consumption is the ramp's colour at pct, below the alarm band.
func consumption(pct float64) rgb {
	if pct <= pivot {
		return mix(green, yellow, pct/pivot)
	}
	return mix(yellow, red, (pct-pivot)/(alarmAt-pivot))
}

// ramp is just the escape for a percentage, with no text and no reset.
//
// colour() closes itself with a reset after every call, so a bar built from it
// would emit an open and a close around each of forty cells. The bar needs the
// code alone, so it can open a span once and hold it for every cell that shares
// a colour.
func ramp(pct float64) string {
	if pct >= alarmAt {
		return alarm(pct)
	}
	return consumption(pct).fg()
}

// alarm is the alarm style at pct, faded in from the top of the ramp.
//
// At alarmAt it is red on the terminal's own background, which is the colour
// the ramp beneath it arrives at and a background nothing can see, so the
// boundary has nothing to show. At alarmTop it is pale yellow on deep red.
//
// Both ends interpolate together, so the background filling in and the
// foreground brightening are one movement rather than two.
func alarm(pct float64) string {
	into := num.Clamp((pct-alarmAt)/(alarmTop-alarmAt), 0, 1)
	front := mix(red, alarmFG, into)
	back := mix(backdrop, alarmBG, into)
	return fmt.Sprintf("\033[1;38;2;%d;%d;%d;48;2;%d;%d;%dm",
		front.r, front.g, front.b, back.r, back.g, back.b)
}

// colour wraps text in a colour interpolated from the percentage consumed.
func colour(pct float64, text string) string {
	if text == "" || plain() {
		return text
	}
	// Inverted rather than merely red past the alarm: the message there is not
	// "high" but "about to matter", and a hue change alone stops being seen
	// after the twentieth time.
	if pct >= alarmAt {
		return alarm(pct) + text + reset
	}
	return consumption(pct).fg() + text + reset
}

// tinted puts one colour over a whole span, closed with a reset.
func tinted(text, escape string) string {
	if plain() {
		return text
	}
	return escape + text + reset
}

func cyan(text string) string { return tinted(text, pathColour) }

// dim marks a word present to be scanned past rather than read.
//
// An empty span would still emit an open and a reset with nothing between,
// which happens at both ends of the bar where the fill or the track is empty.
func dim(text string) string {
	if text == "" {
		return text
	}
	return tinted(text, dimColour)
}

// paceRGB walks the diverging stops and interpolates between the two that
// bracket the projection.
//
// Outside the ends it clamps, so a window projected to land at 400% is the same
// red as one landing at 150: once it will not last, by how much it will not
// last stops changing what to do about it.
func paceRGB(projected float64) rgb {
	low := paceStops[0]
	for _, high := range paceStops[1:] {
		if projected <= high.at {
			return mix(low.colour, high.colour, (projected-low.at)/(high.at-low.at))
		}
		low = high
	}
	return paceStops[len(paceStops)-1].colour
}

// paceTint is the window's verdict, faded toward green by how much it can say
// yet.
//
// Two independent readings in one colour. Where the window is projected to land
// decides the hue; how far through the window we are decides how much of that
// hue is shown, against green for the rest.
//
// Green rather than grey or nothing, because green is this scale's "no comment"
// as well as its "on rate", and both mean there is nothing to act on.
func paceTint(pct, elapsed float64) string {
	projected := pct / max(elapsed, paceMinElapsed)
	// Squared, so the verdict stays quiet through the middle of the window and
	// arrives late. Running hot with most of the window still ahead is not
	// something to act on, and a linear fade is already half shouting at the
	// halfway mark.
	trust := min(1.0, elapsed/paceConfident)
	trust *= trust
	return mix(green, paceRGB(projected), trust).fg()
}
