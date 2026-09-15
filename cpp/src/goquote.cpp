#include "goquote.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include "gotext.hpp"
#include "print_table.hpp"

namespace infobot::goquote {

namespace {

constexpr char32_t ascii_space = 0x20;
constexpr char32_t ascii_delete = 0x7F;
constexpr char32_t bmp_end = 0x10000;

// appendEscapedRune for a rune that is not printable and not a quote or a
// backslash.
void escape(std::string& out, char32_t rune) {
    switch (rune) {
        case U'\a':
            out.append("\\a");
            return;
        case U'\b':
            out.append("\\b");
            return;
        case U'\f':
            out.append("\\f");
            return;
        case U'\n':
            out.append("\\n");
            return;
        case U'\r':
            out.append("\\r");
            return;
        case U'\t':
            out.append("\\t");
            return;
        case U'\v':
            out.append("\\v");
            return;
        default:
            break;
    }
    // Go also maps an invalid rune to U+FFFD here. A rune that came out of
    // decode_rune is always valid, so that case cannot arise.
    if (rune < ascii_space || rune == ascii_delete) {
        out.append(std::format("\\x{:02x}", static_cast<std::uint32_t>(rune)));
    } else if (rune < bmp_end) {
        out.append(std::format("\\u{:04x}", static_cast<std::uint32_t>(rune)));
    } else {
        out.append(std::format("\\U{:08x}", static_cast<std::uint32_t>(rune)));
    }
}

}  // namespace

bool is_print(char32_t rune) {
    const auto ranges = printable();
    const auto found = std::ranges::upper_bound(
        ranges, rune, {}, [](const Printable& range) { return range.low; });
    return found != ranges.begin() && rune <= (found - 1)->high;
}

std::string quote(std::string_view text) {
    std::string out = "\"";
    while (!text.empty()) {
        const gotext::Decoded decoded = gotext::decode_rune(text);
        if (decoded.size == 1 && decoded.rune == gotext::rune_error) {
            out.append(std::format("\\x{:02x}", static_cast<unsigned char>(text[0])));
        } else if (decoded.rune == U'"' || decoded.rune == U'\\') {
            out.push_back('\\');
            out.push_back(static_cast<char>(decoded.rune));
        } else if (is_print(decoded.rune)) {
            out.append(text.substr(0, decoded.size));
        } else {
            escape(out, decoded.rune);
        }
        text.remove_prefix(decoded.size);
    }
    out.push_back('"');
    return out;
}

}  // namespace infobot::goquote
