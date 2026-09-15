// A string quoted the way Go's %q verb quotes it, which is strconv.Quote.
//
// forget writes a refused session id into its log with %q, so the port writes
// the same line for the same id: printable runes as they are, the usual
// backslash escapes, \x for a stray byte, and \u or \U for anything else.
#pragma once

#include <string>
#include <string_view>

namespace infobot::goquote {

// strconv.IsPrint.
[[nodiscard]] bool is_print(char32_t rune);

// strconv.Quote.
[[nodiscard]] std::string quote(std::string_view text);

}  // namespace infobot::goquote
