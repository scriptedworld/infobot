// What Go's strconv.IsPrint accepts, as ranges of code points.
#pragma once

#include <span>

namespace infobot::goquote {

// One run of printable code points.
struct Printable {
    char32_t low;
    char32_t high;
};

// Sorted, and never overlapping.
[[nodiscard]] std::span<const Printable> printable();

}  // namespace infobot::goquote
