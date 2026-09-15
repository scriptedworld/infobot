#include "width.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <string_view>

namespace width = infobot::width;

namespace {

struct Text {
    std::string_view in;
    int want;
};

}  // namespace

// COVERS: FR-3.9 | property
//
// Escapes cost nothing and east-asian wide glyphs cost two. len() is wrong in
// both directions and both errors run toward a row too wide for the line.
TEST_CASE("visible width measures what the terminal draws") {
    for (const auto c : {
             Text{.in = "", .want = 0},
             Text{.in = "abc", .want = 3},
             Text{.in = "\033[0mabc\033[0m", .want = 3},
             Text{.in = "\033[38;2;1;2;3mx\033[0m", .want = 1},
             Text{.in = "\U0001F9E0", .want = 2},
             Text{.in = "\U0001F9E0 x", .want = 4},
             // Ambiguous, counted as one each.
             Text{.in = "\u25B0\u25B1", .want = 2},
             // The rail.
             Text{.in = "\u256D\u2500 ", .want = 3},
             Text{.in = "\u27E8abcd\u27E9", .want = 6},
         }) {
        CAPTURE(c.in);
        CHECK(width::visible(c.in) == c.want);
    }
}

// COVERS: FR-3.10 | property
//
// Ambiguous-width characters are counted as ONE column, which is what kitty
// draws them as. A terminal treating ambiguous as wide would draw every bar at
// twice its measured width.
TEST_CASE("ambiguous width counts as one") {
    for (const auto c : {
             // The bar's own glyphs.
             Text{.in = "\u25B0", .want = 1},
             Text{.in = "\u25B1", .want = 1},
             // The rail.
             Text{.in = "\u2500", .want = 1},
             Text{.in = "\u256D", .want = 1},
             Text{.in = "\u251C", .want = 1},
             Text{.in = "\u2570", .want = 1},
             Text{.in = "\u2575", .want = 1},
             // The powerline separator, private use.
             Text{.in = "\uE0B0", .want = 1},
             // The project root marker.
             Text{.in = "\u2302", .want = 1},
             // Against a genuinely wide glyph, so this is not asserting that
             // everything is one column.
             Text{.in = "\U0001F9E0", .want = 2},
         }) {
        CAPTURE(c.in);
        CHECK(width::visible(c.in) == c.want);
    }
}

// COVERS: FR-3.9 | property
//
// The table is generated from Go's runeWidth, so a handful of code points from
// each class it distinguishes is enough to show it was read the right way
// round. The values are what the Go renderer returned for these runes, measured
// 2026-09-14 against internal/render.VisibleWidth.
TEST_CASE("rune width agrees with Go for each class") {
    struct Rune {
        char32_t in;
        int want;
    };
    for (const auto c : {
             // Combining, Mn and Me.
             Rune{.in = U'\u0301', .want = 0},
             Rune{.in = U'\u20DD', .want = 0},
             // Mn AND east-asian wide: the mark rule wins, as it is tested first.
             Rune{.in = U'\u302A', .want = 0},
             Rune{.in = U'\u3099', .want = 0},
             // Wide and fullwidth.
             Rune{.in = U'\u1100', .want = 2},
             Rune{.in = U'\u4E00', .want = 2},
             Rune{.in = U'\uAC00', .want = 2},
             Rune{.in = U'\uFF01', .want = 2},
             Rune{.in = U'\U0001F9E0', .want = 2},
             Rune{.in = U'\U00020000', .want = 2},
             Rune{.in = U'\U0003FFFD', .want = 2},
             // Ambiguous.
             Rune{.in = U'\u00E9', .want = 1},
             Rune{.in = U'\uE0B0', .want = 1},
             // Everything else, including below the table's first range, a
             // spacing mark, format characters and the last code point.
             Rune{.in = U'\0', .want = 1},
             Rune{.in = U'a', .want = 1},
             Rune{.in = U'\u0903', .want = 1},
             Rune{.in = U'\u00AD', .want = 1},
             Rune{.in = U'\u200B', .want = 1},
             Rune{.in = U'\U0010FFFF', .want = 1},
         }) {
        CAPTURE(static_cast<unsigned>(c.in));
        CHECK(width::rune_width(c.in) == c.want);
    }
}

// COVERS: FR-3.9 | edge
//
// Only `ESC [ digits-and-; m` costs nothing, the pattern Go's renderer strips.
// Anything wider is drawn as whatever its bytes are, and a strip is one pass,
// so an escape exposed by removing another is not removed again. Values
// measured 2026-09-14 against internal/render.VisibleWidth.
TEST_CASE("only an SGR escape costs nothing") {
    for (const auto c : {
             Text{.in = "\033[m", .want = 0},
             Text{.in = "\033[38;2;1;2;3m", .want = 0},
             Text{.in = "\033[\033[0mm", .want = 3},
             Text{.in = "\033[1;2", .want = 5},
             Text{.in = "\033]0mx", .want = 5},
             Text{.in = "\033[0", .want = 3},
             Text{.in = "\033", .want = 1},
         }) {
        CAPTURE(c.in);
        CHECK(width::visible(c.in) == c.want);
    }
}

// COVERS: FR-3.9 | edge
//
// An invalid byte decodes to U+FFFD, one column, exactly as ranging over a Go
// string does, and the escape is stripped BEFORE decoding, so one sitting
// between the bytes of a character joins them back up. Values measured
// 2026-09-14 against internal/render.VisibleWidth.
TEST_CASE("invalid UTF-8 is measured as Go decodes it") {
    for (const auto c : {
             Text{.in = "\xff", .want = 1},
             Text{.in = "\xed\xa0\x80", .want = 3},
             Text{.in = "\xf0\x9f\xa7", .want = 3},
             Text{.in = "e\u0301", .want = 1},
             Text{.in = "\xf0\x9f\033[0m\xa7\xa0", .want = 2},
         }) {
        CAPTURE(c.in);
        CHECK(width::visible(c.in) == c.want);
    }
}
