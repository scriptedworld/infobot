// The columns a code point takes, as the Go renderer counts them.
#pragma once

#include <span>

namespace infobot::width {

// A run of code points sharing one width other than 1. Everything not in the
// table is one column.
struct Range {
    char32_t low;
    char32_t high;
    int width;
};

// Sorted, and never overlapping.
[[nodiscard]] std::span<const Range> table();

}  // namespace infobot::width
