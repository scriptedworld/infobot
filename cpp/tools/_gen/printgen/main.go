// Command printgen prints the C++ table of what strconv.IsPrint accepts, so the
// C++ port's %q quoting agrees with Go's by construction.
//
//	python3 cpp/tools/check_tables.py --write
package main

import (
	"fmt"
	"strconv"
	"unicode"
)

func main() {
	fmt.Print(`// GENERATED from strconv.IsPrint by cpp/tools/_gen/printgen, and checked
// against it for every code point by cpp/tools/check_tables.py.
// Regenerate with that script's --write when Go's Unicode version moves.
#include "print_table.hpp"

#include <array>
#include <span>

namespace infobot::goquote {

namespace {

constexpr auto ranges = std::to_array<Printable>({
`)
	start, inside := rune(0), false
	for r := rune(0); r <= unicode.MaxRune+1; r++ {
		printable := r <= unicode.MaxRune && strconv.IsPrint(r)
		switch {
		case printable && !inside:
			start, inside = r, true
		case !printable && inside:
			fmt.Printf("    {.low = 0x%04X, .high = 0x%04X},\n", start, r-1)
			inside = false
		}
	}
	fmt.Print(`});

}  // namespace

std::span<const Printable> printable() {
    return ranges;
}

}  // namespace infobot::goquote
`)
}
