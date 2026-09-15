// Command widthgen prints the C++ character-width table by asking the Go
// renderer itself how wide each code point is, so the two agree by
// construction rather than by a copy of the rule.
//
//	python3 cpp/tools/check_tables.py --write
package main

import (
	"fmt"
	"unicode"

	"github.com/scriptedworld/infobot/internal/render"
)

func runeWidth(r rune) int {
	return render.VisibleWidth(string(r))
}

func main() {
	fmt.Print(`// GENERATED from internal/render's VisibleWidth by cpp/tools/_gen/widthgen, and
// checked against it for every code point by cpp/tools/check_tables.py.
// Regenerate with that script's --write when golang.org/x/text moves.
#include "width_table.hpp"

#include <array>
#include <span>

namespace infobot::width {

namespace {

constexpr auto ranges = std::to_array<Range>({
`)
	start, current := rune(0), runeWidth(0)
	flush := func(end rune) {
		if current != 1 {
			fmt.Printf("    {.low = 0x%04X, .high = 0x%04X, .width = %d},\n", start, end, current)
		}
	}
	for r := rune(1); r <= unicode.MaxRune; r++ {
		if w := runeWidth(r); w != current {
			flush(r - 1)
			start, current = r, w
		}
	}
	flush(unicode.MaxRune)
	fmt.Print(`});

}  // namespace

std::span<const Range> table() {
    return ranges;
}

}  // namespace infobot::width
`)
}
