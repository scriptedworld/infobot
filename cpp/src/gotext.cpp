#include "gotext.hpp"

#include <cstddef>
#include <string_view>

namespace infobot::gotext {

namespace {

constexpr unsigned char rune_self = 0x80;
constexpr unsigned char continuation_mask = 0xC0;
constexpr unsigned char continuation_tag = 0x80;
constexpr unsigned char payload_mask = 0x3F;
constexpr unsigned char two_byte_min = 0xC2;
constexpr unsigned char three_byte_min = 0xE0;
constexpr unsigned char four_byte_min = 0xF0;
constexpr unsigned char lead_max = 0xF4;
constexpr unsigned char two_byte_mask = 0x1F;
constexpr unsigned char three_byte_mask = 0x0F;
constexpr unsigned char four_byte_mask = 0x07;
constexpr int bits = 6;
constexpr std::size_t utf_max = 4;

// The range a second byte must fall in, which is narrower than a continuation
// byte for four leads: E0 and F0 would otherwise admit overlong forms, ED the
// surrogates, and F4 code points past U+10FFFF.
struct Accept {
    unsigned char low;
    unsigned char high;
};

constexpr Accept accept_any{.low = 0x80, .high = 0xBF};

Accept second_byte_range(unsigned char lead) {
    switch (lead) {
        case three_byte_min:
            return {.low = 0xA0, .high = 0xBF};
        case 0xED:
            return {.low = 0x80, .high = 0x9F};
        case four_byte_min:
            return {.low = 0x90, .high = 0xBF};
        case lead_max:
            return {.low = 0x80, .high = 0x8F};
        default:
            return accept_any;
    }
}

bool is_continuation(unsigned char byte) {
    return (byte & continuation_mask) == continuation_tag;
}

// How long the sequence a lead byte opens is, and the bits the lead carries. 0
// means the byte cannot open a sequence at all.
struct Lead {
    std::size_t size;
    unsigned char mask;
};

Lead lead_of(unsigned char byte) {
    if (byte < two_byte_min || byte > lead_max) {
        return {.size = 0, .mask = 0};
    }
    if (byte < three_byte_min) {
        return {.size = 2, .mask = two_byte_mask};
    }
    if (byte < four_byte_min) {
        return {.size = 3, .mask = three_byte_mask};
    }
    return {.size = utf_max, .mask = four_byte_mask};
}

constexpr Decoded invalid{.rune = rune_error, .size = 1};

}  // namespace

Decoded decode_rune(std::string_view text) {
    if (text.empty()) {
        return {.rune = rune_error, .size = 0};
    }
    const auto first = static_cast<unsigned char>(text[0]);
    if (first < rune_self) {
        return {.rune = first, .size = 1};
    }
    const Lead lead = lead_of(first);
    if (lead.size == 0 || text.size() < lead.size) {
        return invalid;
    }
    const Accept range = second_byte_range(first);
    const auto second = static_cast<unsigned char>(text[1]);
    if (second < range.low || second > range.high) {
        return invalid;
    }
    char32_t rune = first & lead.mask;
    rune = (rune << bits) | (second & payload_mask);
    for (std::size_t i = 2; i < lead.size; ++i) {
        const auto next = static_cast<unsigned char>(text[i]);
        if (!is_continuation(next)) {
            return invalid;
        }
        rune = (rune << bits) | (next & payload_mask);
    }
    return {.rune = rune, .size = lead.size};
}

Decoded decode_last_rune(std::string_view text) {
    if (text.empty()) {
        return {.rune = rune_error, .size = 0};
    }
    const std::size_t end = text.size();
    const auto last = static_cast<unsigned char>(text[end - 1]);
    if (last < rune_self) {
        return {.rune = last, .size = 1};
    }
    // Step back to the nearest byte that can start a rune, no further than a
    // rune is long, then decode forward and check it reaches the end. That is
    // Go's algorithm step for step, including starting the search one byte
    // before the last, which is what decides a truncated sequence.
    const auto signed_end = static_cast<std::ptrdiff_t>(end);
    const std::ptrdiff_t limit = signed_end >= static_cast<std::ptrdiff_t>(utf_max)
                                     ? signed_end - static_cast<std::ptrdiff_t>(utf_max)
                                     : 0;
    std::ptrdiff_t start = signed_end - 2;
    while (start >= limit && is_continuation(static_cast<unsigned char>(
                                 text[static_cast<std::size_t>(start)]))) {
        --start;
    }
    const auto from = static_cast<std::size_t>(start < 0 ? 0 : start);
    const Decoded decoded = decode_rune(text.substr(from));
    if (from + decoded.size != end) {
        return invalid;
    }
    return decoded;
}

bool is_space(char32_t rune) {
    switch (rune) {
        case U'\t':
        case U'\n':
        case U'\v':
        case U'\f':
        case U'\r':
        case U' ':
        case U'':
        case U' ':
        case U' ':
        case U' ':
        case U' ':
        case U' ':
        case U' ':
        case U'　':
            return true;
        default:
            return rune >= U' ' && rune <= U' ';
    }
}

std::string_view trim_space(std::string_view text) {
    while (!text.empty()) {
        const Decoded decoded = decode_rune(text);
        if (!is_space(decoded.rune)) {
            break;
        }
        text.remove_prefix(decoded.size);
    }
    while (!text.empty()) {
        const Decoded decoded = decode_last_rune(text);
        if (!is_space(decoded.rune)) {
            break;
        }
        text.remove_suffix(decoded.size);
    }
    return text;
}

}  // namespace infobot::gotext
