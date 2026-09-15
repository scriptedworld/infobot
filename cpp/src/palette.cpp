#include "palette.hpp"

#include <simdjson.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "files.hpp"
#include "gotext.hpp"
#include "gojson.hpp"
#include "num.hpp"

namespace infobot::palette {

namespace {

constexpr std::size_t hex_length = 7;
constexpr std::size_t channel_width = 2;
constexpr int base16 = 16;
constexpr int decimal_digits = 10;

// One hex digit's value, or -1.
int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + decimal_digits;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + decimal_digits;
    }
    return -1;
}

// fmt's SkipSpace for Sscanf, which does not count newlines as space: white
// space is passed over, and a newline, or a carriage return before one, is an
// error. False on the error.
bool skip_space(std::string_view text, std::size_t& at) {
    while (at < text.size()) {
        if (text[at] == '\n' || text.substr(at).starts_with("\r\n")) {
            return false;
        }
        const gotext::Decoded decoded = gotext::decode_rune(text.substr(at));
        if (!gotext::is_space(decoded.rune)) {
            break;
        }
        at += decoded.size;
    }
    return true;
}

// One %02x from fmt's scanner, reading text from at. White space before it is
// skipped and does not count toward the width; within the two columns there
// may be a sign, then at least one hex digit. nullopt when there is no digit.
std::optional<int> scan_channel(std::string_view text, std::size_t& at) {
    if (!skip_space(text, at)) {
        return std::nullopt;
    }
    const std::size_t limit = std::min(at + channel_width, text.size());
    int sign = 1;
    if (at < limit && (text[at] == '+' || text[at] == '-')) {
        sign = text[at] == '-' ? -1 : 1;
        ++at;
    }
    int value = 0;
    bool any = false;
    for (; at < limit && hex_value(text[at]) >= 0; ++at) {
        value = (value * base16) + hex_value(text[at]);
        any = true;
    }
    if (!any) {
        return std::nullopt;
    }
    return sign * value;
}

void overlay(Palette& palette, const gojson::Map<std::string>& colours) {
    const auto named = [&](std::string_view name) {
        const auto found = colours.find(name);
        return found == colours.end() ? Parsed{} : parse_hex(found->second);
    };
    const std::array<std::pair<std::string_view, Rgb*>, 8> rgbs = {{
        {"green", &palette.green},
        {"yellow", &palette.yellow},
        {"red", &palette.red},
        {"backdrop", &palette.backdrop},
        {"alarm_fg", &palette.alarm_fg},
        {"alarm_bg", &palette.alarm_bg},
        {"pace_low", &palette.pace[0].colour},
        {"pace_under", &palette.pace[1].colour},
    }};
    for (const auto& [name, target] : rgbs) {
        if (const Parsed parsed = named(name); parsed.ok) {
            *target = parsed.colour;
        }
    }
    const std::array<std::pair<std::string_view, std::string*>, 4> escapes = {{
        {"path", &palette.path},
        {"empty", &palette.empty},
        {"separator", &palette.separator},
        {"dim", &palette.dim},
    }};
    for (const auto& [name, target] : escapes) {
        if (const Parsed parsed = named(name); parsed.ok) {
            *target = fg(parsed.colour);
        }
    }
    // The stops after the first two are the ramp colours themselves.
    palette.pace[2].colour = palette.green;
    palette.pace[3].colour = palette.yellow;
    palette.pace[4].colour = palette.red;
}

std::optional<gojson::Map<std::string>> colours_in(const std::string& raw) {
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (parser.parse(raw).get(root) != simdjson::SUCCESS) {
        return std::nullopt;
    }
    gojson::Decode decode;
    std::string taken;
    std::string source;
    std::optional<gojson::Map<std::string>> colours;
    const auto text = [](gojson::Decode& d, simdjson::dom::element e, std::string& out) {
        d.string_into(e, out);
    };
    decode.as_struct(root, [&](simdjson::dom::object top) {
        decode.fields(top, "taken", [&](auto v) { decode.string_into(v, taken); });
        decode.fields(top, "source", [&](auto v) { decode.string_into(v, source); });
        decode.fields(top, "colours", [&](auto v) { decode.map_into(v, colours, text); });
    });
    if (!decode.ok() || !colours || colours->empty()) {
        return std::nullopt;
    }
    return colours;
}

}  // namespace

std::string fg(Rgb colour) {
    return std::format("\033[38;2;{};{};{}m", colour.r, colour.g, colour.b);
}

Palette seed() {
    constexpr Rgb green{.r = 43, .g = 255, .b = 158};
    constexpr Rgb yellow{.r = 255, .g = 212, .b = 38};
    constexpr Rgb red{.r = 255, .g = 46, .b = 110};
    constexpr Rgb backdrop{.r = 13, .g = 10, .b = 32};
    constexpr Rgb alarm_fg{.r = 255, .g = 232, .b = 92};
    constexpr Rgb alarm_bg{.r = 115, .g = 21, .b = 50};
    constexpr Rgb pace_low{.r = 46, .g = 123, .b = 255};
    constexpr Rgb pace_under{.r = 211, .g = 198, .b = 245};
    // Where each pace stop sits, as the percentage of the window a projection
    // lands at: barely touched, under-spent, exactly full, a fifth early, a
    // third early.
    constexpr double low_at = 0.0;
    constexpr double under_at = 70.0;
    constexpr double full_at = 100.0;
    constexpr double early_at = 125.0;
    constexpr double empty_at = 150.0;
    return {
        .green = green,
        .yellow = yellow,
        .red = red,
        .backdrop = backdrop,
        .alarm_fg = alarm_fg,
        .alarm_bg = alarm_bg,
        .pace = {{
            {.at = low_at, .colour = pace_low},
            {.at = under_at, .colour = pace_under},
            {.at = full_at, .colour = green},
            {.at = early_at, .colour = yellow},
            {.at = empty_at, .colour = red},
        }},
        .path = "\033[38;2;255;43;214m",
        .empty = "\033[38;2;12;90;102m",
        .separator = "\033[38;2;59;21;102m",
        .dim = "\033[38;2;142;124;195m",
    };
}

// fmt.Sscanf(text[1:], "%02x%02x%02x") after checking the length and the #,
// which is how the Go port reads a colour. A channel may therefore be one
// digit, carry a sign, or follow white space, and a colour Go accepts is
// accepted here so one palette file themes both builds alike.
Parsed parse_hex(std::string_view text) {
    Parsed parsed;
    if (text.size() != hex_length || text[0] != '#') {
        return parsed;
    }
    const std::string_view body = text.substr(1);
    std::size_t at = 0;
    const auto r = scan_channel(body, at);
    const auto g = r ? scan_channel(body, at) : std::nullopt;
    const auto b = g ? scan_channel(body, at) : std::nullopt;
    if (!r || !g || !b) {
        return parsed;
    }
    parsed.colour = {.r = *r, .g = *g, .b = *b};
    parsed.ok = true;
    return parsed;
}

Rgb mix(Rgb a, Rgb b, double t) {
    t = num::clamp(t, 0, 1);
    const auto lerp = [t](int from, int to) {
        return num::round_int(static_cast<double>(from) +
                              (static_cast<double>(to - from) * t));
    };
    return {.r = lerp(a.r, b.r), .g = lerp(a.g, b.g), .b = lerp(a.b, b.b)};
}

Palette load(const std::string& path) {
    Palette palette = seed();
    if (path.empty()) {
        return palette;
    }
    const auto raw = files::read(path);
    if (!raw) {
        return palette;
    }
    if (const auto colours = colours_in(*raw)) {
        overlay(palette, *colours);
    }
    return palette;
}

}  // namespace infobot::palette
