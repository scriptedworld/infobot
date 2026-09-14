// Text handled the way Go's standard library handles it.
//
// PARITY IS BYTES, so these are ports rather than equivalents. Ranging over a
// Go string decodes UTF-8 with its own rules for what is invalid, and
// strings.TrimSpace trims Unicode white space rather than the six ASCII
// characters std::isspace knows. A port that differs on either renders a
// different row from the same payload.
#pragma once

#include <cstddef>
#include <string_view>

namespace infobot::gotext {

// U+FFFD, what an invalid byte decodes to.
inline constexpr char32_t rune_error = 0xFFFD;

struct Decoded {
    char32_t rune;
    std::size_t size;
};

// utf8.DecodeRuneInString: the first rune and its length. An invalid sequence
// is rune_error of length 1, and an empty string is rune_error of length 0.
[[nodiscard]] Decoded decode_rune(std::string_view text);

// utf8.DecodeLastRuneInString: the same, for the last rune.
[[nodiscard]] Decoded decode_last_rune(std::string_view text);

// unicode.IsSpace.
[[nodiscard]] bool is_space(char32_t rune);

// strings.TrimSpace.
[[nodiscard]] std::string_view trim_space(std::string_view text);

}  // namespace infobot::gotext
