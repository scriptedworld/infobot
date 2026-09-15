#include "gotext.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace gotext = infobot::gotext;

namespace {

struct Utf8Map {
    char32_t rune;
    std::string_view str;
};

// utf8map from unicode/utf8/utf8_test.go.
const std::vector<Utf8Map>& utf8map() {
    static const std::vector<Utf8Map> table = {
        {.rune = 0x0000, .str = std::string_view("\x00", 1)},
        {.rune = 0x0001, .str = "\x01"},
        {.rune = 0x007e, .str = "~"},
        {.rune = 0x007f, .str = "\x7f"},
        {.rune = 0x0080, .str = "\xc2\x80"},
        {.rune = 0x0081, .str = "\xc2\x81"},
        {.rune = 0x00bf, .str = "\xc2\xbf"},
        {.rune = 0x00c0, .str = "\xc3\x80"},
        {.rune = 0x00c1, .str = "\xc3\x81"},
        {.rune = 0x00c8, .str = "\xc3\x88"},
        {.rune = 0x00d0, .str = "\xc3\x90"},
        {.rune = 0x00e0, .str = "\xc3\xa0"},
        {.rune = 0x00f0, .str = "\xc3\xb0"},
        {.rune = 0x00f8, .str = "\xc3\xb8"},
        {.rune = 0x00ff, .str = "\xc3\xbf"},
        {.rune = 0x0100, .str = "\xc4\x80"},
        {.rune = 0x07ff, .str = "\xdf\xbf"},
        {.rune = 0x0400, .str = "\xd0\x80"},
        {.rune = 0x0800, .str = "\xe0\xa0\x80"},
        {.rune = 0x0801, .str = "\xe0\xa0\x81"},
        {.rune = 0x1000, .str = "\xe1\x80\x80"},
        {.rune = 0xd000, .str = "\xed\x80\x80"},
        {.rune = 0xd7ff,
         .str = "\xed\x9f\xbf"},  // last code point before surrogate half.
        {.rune = 0xe000,
         .str = "\xee\x80\x80"},  // first code point after surrogate half.
        {.rune = 0xfffe, .str = "\xef\xbf\xbe"},
        {.rune = 0xffff, .str = "\xef\xbf\xbf"},
        {.rune = 0x10000, .str = "\xf0\x90\x80\x80"},
        {.rune = 0x10001, .str = "\xf0\x90\x80\x81"},
        {.rune = 0x40000, .str = "\xf1\x80\x80\x80"},
        {.rune = 0x10fffe, .str = "\xf4\x8f\xbf\xbe"},
        {.rune = 0x10ffff, .str = "\xf4\x8f\xbf\xbf"},
        {.rune = 0xFFFD, .str = "\xef\xbf\xbd"},
    };
    return table;
}

std::string concat(std::initializer_list<std::string_view> parts) {
    std::string out;
    for (const std::string_view part : parts) {
        out.append(part);
    }
    return out;
}

bool decodes_to(gotext::Decoded got, char32_t rune, std::size_t size) {
    return got.rune == rune && got.size == size;
}

// TestDecodeRune's body for one row: the whole sequence, the sequence with a
// byte after it, the sequence missing its last byte, and the sequence with its
// last byte made bad.
void check_decode_row(const Utf8Map& m) {
    CAPTURE(static_cast<unsigned>(m.rune));
    const std::string s(m.str);
    CHECK(decodes_to(gotext::decode_rune(s), m.rune, s.size()));
    CHECK(decodes_to(gotext::decode_rune(s + std::string(1, '\0')), m.rune, s.size()));

    const std::size_t wantsize = s.size() > 1 ? 1 : 0;
    CHECK(decodes_to(gotext::decode_rune(std::string_view(s).substr(0, s.size() - 1)),
                     gotext::rune_error,
                     wantsize));

    constexpr char continuation = '\x80';
    constexpr char ascii = '\x7F';
    std::string bad = s;
    bad.back() = bad.size() == 1 ? continuation : ascii;
    CHECK(decodes_to(gotext::decode_rune(bad), gotext::rune_error, 1));
}

// testSequence from utf8_test.go: decoding forward and decoding backward visit
// the same runes at the same offsets.
struct Visit {
    std::size_t index;
    char32_t rune;
};

std::vector<Visit> forward(std::string_view s) {
    std::vector<Visit> visits;
    for (std::size_t i = 0; i < s.size();) {
        const gotext::Decoded decoded = gotext::decode_rune(s.substr(i));
        visits.push_back({.index = i, .rune = decoded.rune});
        i += decoded.size;
    }
    return visits;
}

void check_sequence(std::string_view s) {
    CAPTURE(s);
    const std::vector<Visit> visits = forward(s);
    std::size_t si = s.size();
    for (auto visit = visits.rbegin(); visit != visits.rend(); ++visit) {
        const gotext::Decoded decoded = gotext::decode_last_rune(s.substr(0, si));
        REQUIRE(decoded.rune == visit->rune);
        si -= decoded.size;
        REQUIRE(si == visit->index);
    }
    CHECK(si == 0);
}

}  // namespace

// COVERS: FR-3.9 | property
//
// A row's width is measured over the runes Go's range loop decodes, so every
// valid sequence has to decode to the code point and length Go gives it, and a
// truncated or damaged one to U+FFFD of the length Go gives that.
TEST_CASE("decode_rune decodes Go's utf8map") {
    for (const Utf8Map& m : utf8map()) {
        check_decode_row(m);
    }
}

// COVERS: FR-3.9 | negative
//
// A surrogate half is not a code point UTF-8 may carry, and decodes as one
// invalid byte.
TEST_CASE("a surrogate decodes as one invalid byte") {
    for (const std::string_view s : {"\xed\xa0\x80", "\xed\xbf\xbf"}) {
        CAPTURE(s);
        CHECK(decodes_to(gotext::decode_rune(s), gotext::rune_error, 1));
    }
}

// COVERS: FR-3.9 | negative
//
// invalidSequenceTests: every second, third and fourth byte outside the range
// its lead admits, overlong forms, surrogates and code points past U+10FFFF.
TEST_CASE("an invalid sequence decodes as rune_error") {
    for (const std::string_view s : {
             "\xed\xa0\x80\x80", "\xed\xbf\xbf\x80", "\x91\x80\x80\x80",
             "\xC2\x7F\x80\x80", "\xC2\xC0\x80\x80", "\xDF\x7F\x80\x80",
             "\xDF\xC0\x80\x80", "\xE0\x9F\xBF\x80", "\xE0\xA0\x7F\x80",
             "\xE0\xBF\xC0\x80", "\xE0\xC0\x80\x80", "\xE1\x7F\xBF\x80",
             "\xE1\x80\x7F\x80", "\xE1\xBF\xC0\x80", "\xE1\xC0\x80\x80",
             "\xED\x7F\xBF\x80", "\xED\x80\x7F\x80", "\xED\x9F\xC0\x80",
             "\xED\xA0\x80\x80", "\xF0\x8F\xBF\xBF", "\xF0\x90\x7F\xBF",
             "\xF0\x90\x80\x7F", "\xF0\xBF\xBF\xC0", "\xF0\xBF\xC0\x80",
             "\xF0\xC0\x80\x80", "\xF1\x7F\xBF\xBF", "\xF1\x80\x7F\xBF",
             "\xF1\x80\x80\x7F", "\xF1\xBF\xBF\xC0", "\xF1\xBF\xC0\x80",
             "\xF1\xC0\x80\x80", "\xF4\x7F\xBF\xBF", "\xF4\x80\x7F\xBF",
             "\xF4\x80\x80\x7F", "\xF4\x8F\xBF\xC0", "\xF4\x8F\xC0\x80",
             "\xF4\x90\x80\x80",
         }) {
        CAPTURE(s);
        CHECK(decodes_to(gotext::decode_rune(s), gotext::rune_error, 1));
    }
}

// COVERS: FR-3.9 | edge
//
// C0 and C1 could only open an overlong two-byte form, and F5 to FF a code point
// past U+10FFFF, so none of them opens a sequence. TestFullRune treats C0 and C1
// as complete runes for the same reason: one byte is all they can be.
TEST_CASE("a byte that cannot lead decodes as one invalid byte") {
    for (const std::string_view s :
         {"\xc0\x80", "\xc1\xbf", "\xf5\x80\x80\x80", "\xff", "\x80"}) {
        CAPTURE(s);
        CHECK(decodes_to(gotext::decode_rune(s), gotext::rune_error, 1));
    }
}

// COVERS: FR-3.9 | edge
TEST_CASE("an empty string decodes as rune_error of length 0") {
    CHECK(decodes_to(gotext::decode_rune(""), gotext::rune_error, 0));
    CHECK(decodes_to(gotext::decode_last_rune(""), gotext::rune_error, 0));
}

// COVERS: FR-3.9 | property
//
// TestSequencing: each of Go's test strings before, after and around each
// utf8map sequence, decoded forward and then backward, visits the same runes.
TEST_CASE("decode_last_rune retraces decode_rune") {
    constexpr std::string_view japanese = "日a本b語ç日ð本Ê語þ日¥本¼語i日©";
    const std::initializer_list<std::string> strings = {
        "",
        "abcd",
        "☺☻☹",
        std::string(japanese),
        concat({japanese, japanese, japanese}),
        "\x80\x80\x80\x80",
    };
    for (const std::string& ts : strings) {
        for (const Utf8Map& m : utf8map()) {
            check_sequence(concat({ts, m.str}));
            check_sequence(concat({m.str, ts}));
            check_sequence(concat({ts, m.str, ts}));
        }
    }
}

// COVERS: FR-3.9 | edge
//
// DecodeLastRuneInString steps back no further than a rune is long, and a
// sequence that does not reach the end of the string is one invalid byte.
TEST_CASE("decode_last_rune on truncated and overlong tails") {
    struct Case {
        std::string_view in;
        char32_t rune;
        std::size_t size;
    };
    for (const auto c : {
             Case{.in = "a\xe2\x98", .rune = gotext::rune_error, .size = 1},
             Case{.in = "\xe2\x98\xba", .rune = U'☺', .size = 3},
             Case{.in = "\xe2\x80\x80\x80", .rune = gotext::rune_error, .size = 1},
             Case{.in = "\x80\x80\x80\x80\x80", .rune = gotext::rune_error, .size = 1},
             Case{.in = "\xf0\x90\x80\x80", .rune = 0x10000, .size = 4},
             Case{.in = "x\xf0\x90\x80\x80", .rune = 0x10000, .size = 4},
             Case{.in = "\xc0", .rune = gotext::rune_error, .size = 1},
             Case{.in = "z", .rune = U'z', .size = 1},
         }) {
        CAPTURE(c.in);
        CHECK(decodes_to(gotext::decode_last_rune(c.in), c.rune, c.size));
    }
}

namespace {

struct Range {
    char32_t low;
    char32_t high;
};

// unicode.White_Space from unicode/tables.go, expanded from its strides.
constexpr auto white_space = std::to_array<Range>({
    {.low = 0x0009, .high = 0x000d},
    {.low = 0x0020, .high = 0x0020},
    {.low = 0x0085, .high = 0x0085},
    {.low = 0x00a0, .high = 0x00a0},
    {.low = 0x1680, .high = 0x1680},
    {.low = 0x2000, .high = 0x200a},
    {.low = 0x2028, .high = 0x2029},
    {.low = 0x202f, .high = 0x202f},
    {.low = 0x205f, .high = 0x205f},
    {.low = 0x3000, .high = 0x3000},
});

bool in_white_space_table(char32_t rune) {
    return std::ranges::any_of(
        white_space, [rune](Range r) { return r.low <= rune && rune <= r.high; });
}

}  // namespace

// COVERS: FR-3.3 | property
//
// A host's answer is trimmed before it is read, so what counts as white space
// is unicode.IsSpace exactly: every code point, held against Go's table.
TEST_CASE("is_space agrees with Go's White_Space table on every code point") {
    constexpr char32_t max_rune = 0x10FFFF;
    std::size_t disagreements = 0;
    char32_t first = 0;
    for (char32_t rune = 0; rune <= max_rune; ++rune) {
        if (gotext::is_space(rune) != in_white_space_table(rune)) {
            first = disagreements == 0 ? rune : first;
            ++disagreements;
        }
    }
    CAPTURE(static_cast<unsigned>(first));
    CHECK(disagreements == 0);
}

// COVERS: FR-3.3 | positive
//
// trimSpaceTests from strings/strings_test.go, including the invalid bytes that
// stop a trim without being removed.
TEST_CASE("trim_space trims Go's rows") {
    constexpr std::string_view space = "\t\v\r\f\n\u0085\u00A0\u2000\u3000";
    struct Case {
        std::string in;
        std::string_view want;
    };
    for (const auto& c : {
             Case{.in = "", .want = ""},
             Case{.in = "abc", .want = "abc"},
             Case{.in = concat({space, "abc", space}), .want = "abc"},
             Case{.in = " ", .want = ""},
             Case{.in = " \t\r\n \t\t\r\r\n\n ", .want = ""},
             Case{.in = " \t\r\n x\t\t\r\r\n\n ", .want = "x"},
             Case{.in = " \u2000\t\r\n x\t\t\r\r\ny\n \u3000", .want = "x\t\t\r\r\ny"},
             Case{.in = "1 \t\r\n2", .want = "1 \t\r\n2"},
             Case{.in = " x\x80", .want = "x\x80"},
             Case{.in = " x\xc0", .want = "x\xc0"},
             Case{.in = "x \xc0\xc0 ", .want = "x \xc0\xc0"},
             Case{.in = "x \xc0", .want = "x \xc0"},
             Case{.in = "x \xc0 ", .want = "x \xc0"},
             Case{.in = "x \xc0\xc0 ", .want = "x \xc0\xc0"},
             Case{.in = "x ☺\xc0\xc0 ", .want = "x ☺\xc0\xc0"},
             Case{.in = "x ☺ ", .want = "x ☺"},
         }) {
        CAPTURE(c.in);
        CHECK(gotext::trim_space(c.in) == c.want);
    }
}
