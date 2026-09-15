// The colours, and how a percentage becomes one.
//
// TRUECOLOR. The ramp is continuous rather than stepped: green to yellow across
// the long stretch where nothing is happening, then yellow to red compressed
// into 75-90, so the colour moves fastest where a glance needs to tell 80 from
// 88. At 90 and above the style inverts, fading in across the band.
//
// EVERY COLOUR IS A SEED, NOT THE SETTING. ~/.config/infobot/palette.json is
// overlaid on it at startup, so the palette is configuration and a re-cut
// reaches the status line without a rebuild. The seed is D1C3 Goblin.
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace infobot::palette {

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

// The 24-bit foreground escape for a colour.
[[nodiscard]] std::string fg(Rgb colour);

// One anchor on the diverging pace scale.
struct PaceStop {
    double at = 0;
    Rgb colour;
};

inline constexpr std::string_view reset = "\033[0m";

// How many anchors the pace scale has.
inline constexpr std::size_t pace_stops = 5;

struct Palette {
    Rgb green;
    Rgb yellow;
    Rgb red;
    // The terminal's own background, where the alarm's background fade starts
    // so its first frame paints nothing a reader can see.
    Rgb backdrop;
    Rgb alarm_fg;
    Rgb alarm_bg;
    // Blue where a window is barely touched, pale where it is under-spent, then
    // green, yellow and red. The last three are the ramp's own colours.
    std::array<PaceStop, pace_stops> pace;
    // Emitted directly rather than interpolated, so held as escapes. `empty` is
    // the bar's unused cells and the rail.
    std::string path;
    std::string empty;
    std::string separator;
    std::string dim;
};

// D1C3 Goblin, which a machine with no palette.json still matches.
[[nodiscard]] Palette seed();

// Colours from a palette file, over the seed. A missing or malformed file is
// the seed, and one bad entry keeps its seed value while the rest load.
[[nodiscard]] Palette load(const std::string& path);

// "#RRGGBB" as a colour. ok is false for anything else.
struct Parsed {
    Rgb colour;
    bool ok = false;
};
[[nodiscard]] Parsed parse_hex(std::string_view text);

// a to b by t, each channel rounded ties-to-even.
[[nodiscard]] Rgb mix(Rgb a, Rgb b, double t);

}  // namespace infobot::palette
