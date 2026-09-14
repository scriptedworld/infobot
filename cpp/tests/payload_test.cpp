#include "payload.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <optional>
#include <string_view>

using infobot::payload::Document;
using infobot::payload::Map;

// COVERS: FR-1.3 | property
//
// An absent Map answers like an empty one, so a caller never has to check
// before reaching through it. Every field of the payload is optional.
TEST_CASE("an absent map answers like an empty one") {
    const Map absent;
    CHECK_FALSE(absent.present());
    CHECK_FALSE(absent.obj("anything").present());
    CHECK(absent.str("anything").empty());
    CHECK_FALSE(absent.num("anything").has_value());
    CHECK(absent.count("anything") == 0);
    CHECK_FALSE(absent.has("anything"));
    // And reaching two levels through nothing is still nothing.
    CHECK(absent.obj("a").obj("b").str("c").empty());
}

// COVERS: FR-1.3 | property
//
// Absence and zero stay distinguishable: num says whether one was there, count
// says what to add.
TEST_CASE("num distinguishes absent from zero") {
    const Document doc(R"({"there":0,"null":null,"text":"x","big":1.5e300})");
    const Map data = doc.root();
    constexpr double big = 1.5e300;
    REQUIRE(data.present());
    CHECK(data.num("there") == 0.0);
    CHECK(data.num("big") == big);
    CHECK_FALSE(data.num("missing").has_value());
    CHECK_FALSE(data.num("null").has_value());
    CHECK_FALSE(data.num("text").has_value());
    // has tells an explicit null from an absent key, which num cannot.
    CHECK(data.has("null"));
    CHECK_FALSE(data.has("missing"));
}

// COVERS: FR-1.3 | property
//
// Every JSON number reads as a double, integers and unsigned integers included,
// because Go's decoder puts every number into an interface as float64.
TEST_CASE("an integer reads as a number") {
    const Document doc(R"({"small":42,"negative":-7,"unsigned":18446744073709551615})");
    const Map data = doc.root();
    constexpr double small = 42;
    constexpr double negative = -7;
    constexpr double largest_unsigned = 18446744073709551615.0;
    CHECK(data.num("small") == small);
    CHECK(data.num("negative") == negative);
    CHECK(data.num("unsigned") == largest_unsigned);
}

// COVERS: FR-1.3 | negative
TEST_CASE("wrong types read as absent") {
    const Document doc(R"({"obj":"not an object","str":42,"num":"not a number"})");
    const Map data = doc.root();
    CHECK_FALSE(data.obj("obj").present());
    CHECK(data.str("str").empty());
    CHECK(data.count("num") == 0);
}

// COVERS: FR-1.3 | positive
TEST_CASE("a nested object and its string are read") {
    const Document doc(R"({"model":{"display_name":"Opus","id":"claude-opus-5"}})");
    const Map model = doc.root().obj("model");
    REQUIRE(model.present());
    CHECK(model.str("display_name") == "Opus");
    CHECK(model.count("missing") == 0);
}

// COVERS: FR-1.3 | edge
//
// A repeated key is valid JSON. Go's decoder keeps the last value it reads, so
// reading the first would render a different payload from the same bytes.
TEST_CASE("a repeated key reads as its last value") {
    const Document doc(R"({"model":"first","model":"last"})");
    CHECK(doc.root().str("model") == "last");
}

// COVERS: FR-1.4 | negative
//
// Text that does not parse, and JSON that is not an object, both leave no root.
// Go's Main makes the same test before it renders anything.
TEST_CASE("a document that is not an object has no root") {
    for (const auto* text : {"", "not json", "null", "[1,2]", "42", R"({"open":)"}) {
        CAPTURE(text);
        const Document doc(text);
        CHECK_FALSE(doc.root().present());
    }
}
