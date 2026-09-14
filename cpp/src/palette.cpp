#include "palette.hpp"

#include <simdjson.h>

#include <charconv>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "files.hpp"
#include "gojson.hpp"
#include "num.hpp"

namespace infobot::palette {

namespace {

constexpr std::size_t hex_length = 7;
constexpr int base16 = 16;

bool channel(std::string_view digits, int& out) {
    const auto parsed =
        std::from_chars(digits.data(), digits.data() + digits.size(), out, base16);
    return parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size();
}

}  // namespace

std::string Rgb::fg() const { return std::format("\033[38;2;{};{};{}m", r, g, b); }

Parsed parse_hex(std::string_view text) {
    Parsed parsed;
    if (text.size() != hex_length || text[0] != '#') {
        return parsed;
    }
    parsed.ok = channel(text.substr(1, 2), parsed.colour.r) &&
                channel(text.substr(3, 2), parsed.colour.g) &&
                channel(text.substr(5, 2), parsed.colour.b);
    if (!parsed.ok) {
        parsed.colour = {};
    }
    return parsed;
}

Rgb mix(Rgb a, Rgb b, double t) {
    t = num::clamp(t, 0, 1);
    const auto lerp = [t](int from, int to) {
        return num::round_int(static_cast<double>(from) +
                              static_cast<double>(to - from) * t);
    };
    return {.r = lerp(a.r, b.r), .g = lerp(a.g, b.g), .b = lerp(a.b, b.b)};
}

Palette load(const std::string& path) {
    Palette palette;
    if (path.empty()) {
        return palette;
    }
    const auto raw = files::read(path);
    if (!raw) {
        return palette;
    }
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (parser.parse(*raw).get(root) != simdjson::SUCCESS) {
        return palette;
    }
    gojson::Decode decode;
    std::string taken;
    std::string source;
    std::optional<gojson::Map<std::string>> colours;
    decode.as_struct(root, [&](simdjson::dom::object top) {
        decode.fields(top, "taken", [&](auto v) { decode.string_into(v, taken); });
        decode.fields(top, "source", [&](auto v) { decode.string_into(v, source); });
        decode.fields(top, "colours", [&](auto v) {
            decode.map_into(
                v,
                colours,
                [](gojson::Decode& d, simdjson::dom::element e, std::string& out) {
                    d.string_into(e, out);
                });
        });
    });
    if (!decode.ok() || !colours || colours->empty()) {
        return palette;
    }
    const auto named = [&](std::string_view name) {
        const auto found = colours->find(name);
        return found == colours->end() ? Parsed{} : parse_hex(found->second);
    };
    const std::pair<std::string_view, Rgb*> rgbs[] = {
        {"green", &palette.green},
        {"yellow", &palette.yellow},
        {"red", &palette.red},
        {"backdrop", &palette.backdrop},
        {"alarm_fg", &palette.alarm_fg},
        {"alarm_bg", &palette.alarm_bg},
        {"pace_low", &palette.pace[0].colour},
        {"pace_under", &palette.pace[1].colour},
    };
    for (const auto& [name, target] : rgbs) {
        if (const Parsed parsed = named(name); parsed.ok) {
            *target = parsed.colour;
        }
    }
    // Emitted directly rather than interpolated, so held as escapes.
    const std::pair<std::string_view, std::string*> escapes[] = {
        {"path", &palette.path},
        {"empty", &palette.empty},
        {"separator", &palette.separator},
        {"dim", &palette.dim},
    };
    for (const auto& [name, target] : escapes) {
        if (const Parsed parsed = named(name); parsed.ok) {
            *target = parsed.colour.fg();
        }
    }
    // The stops after the first two are the ramp colours themselves.
    palette.pace[2].colour = palette.green;
    palette.pace[3].colour = palette.yellow;
    palette.pace[4].colour = palette.red;
    return palette;
}

}  // namespace infobot::palette
