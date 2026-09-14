// The colours, and how a percentage becomes one.
//
// TRUECOLOR. The ramp is continuous rather than stepped: green to yellow across
// the long stretch where nothing is happening, then yellow to red compressed
// into 75-90, so the colour moves fastest where a glance needs to tell 80 from
// 88. At 90 and above the style inverts, fading in across the band.
//
// EVERY COLOUR BELOW IS A SEED, NOT THE SETTING. ~/.config/infobot/palette.json
// is overlaid on it at startup, so the palette is configuration and a re-cut
// reaches the status line without a rebuild. The seed is D1C3 Goblin.
#pragma once

#include <array>
#include <string>
#include <string_view>

namespace infobot::palette {

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;

    // The 24-bit foreground escape.
    [[nodiscard]] std::string fg() const;

    friend bool operator==(const Rgb&, const Rgb&) = default;
};

// One anchor on the diverging pace scale.
struct PaceStop {
    double at = 0;
    Rgb colour;
};

inline constexpr std::string_view reset = "\033[0m";

struct Palette {
    Rgb green{.r = 43, .g = 255, .b = 158};
    Rgb yellow{.r = 255, .g = 212, .b = 38};
    Rgb red{.r = 255, .g = 46, .b = 110};
    // The terminal's own background, where the alarm's background fade starts
    // so its first frame paints nothing a reader can see.
    Rgb backdrop{.r = 13, .g = 10, .b = 32};
    Rgb alarm_fg{.r = 255, .g = 232, .b = 92};
    Rgb alarm_bg{.r = 115, .g = 21, .b = 50};
    // Blue where a window is barely touched, pale where it is under-spent, then
    // green, yellow and red. The last three are the ramp's own colours.
    std::array<PaceStop, 5> pace{{
        {.at = 0.0, .colour = {.r = 46, .g = 123, .b = 255}},
        {.at = 70.0, .colour = {.r = 211, .g = 198, .b = 245}},
        {.at = 100.0, .colour = green},
        {.at = 125.0, .colour = yellow},
        {.at = 150.0, .colour = red},
    }};
    std::string path = "\033[38;2;255;43;214m";
    // The bar's unused cells and the rail. Its red channel keeps the pace scale
    // ordered where a barely-used bar's first colour is this rather than a pace
    // colour.
    std::string empty = "\033[38;2;12;90;102m";
    std::string separator = "\033[38;2;59;21;102m";
    std::string dim = "\033[38;2;142;124;195m";
};

// Colours from a palette file, over the seed. A missing or malformed file is
// the seed, and one bad entry keeps its seed value while the rest load.
[[nodiscard]] Palette load(const std::string& path);

// "#RRGGBB" as a colour, or nullopt-like false in ok.
struct Parsed {
    Rgb colour;
    bool ok = false;
};
[[nodiscard]] Parsed parse_hex(std::string_view text);

// a to b by t, each channel rounded ties-to-even.
[[nodiscard]] Rgb mix(Rgb a, Rgb b, double t);

}  // namespace infobot::palette
