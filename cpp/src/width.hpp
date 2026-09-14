// How many columns a rendered string takes.
//
// Sizing the bar by subtraction only works if the subtrahend is what the
// terminal will draw. A byte count is not that: it counts every byte of an
// escape as a column and an emoji as one column when it takes two, and both
// errors run toward a bar too wide for the line.
//
// Ambiguous-width characters, the parallelograms among them, count as one,
// which is what kitty draws them as.
#pragma once

#include <string_view>

namespace infobot::width {

// The columns one code point takes: 0 for a combining mark, 2 for east-asian
// wide or fullwidth, 1 otherwise.
[[nodiscard]] int rune_width(char32_t rune);

// The columns a string takes, SGR escapes costing nothing.
[[nodiscard]] int visible(std::string_view text);

}  // namespace infobot::width
