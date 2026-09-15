#include "gojson.hpp"

#include <doctest/doctest.h>
#include <simdjson.h>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gojson = infobot::gojson;

namespace {

// Every expected value below is what encoding/json printed for the same struct,
// seeded the same way, handed the same text. The probe is
// .ephemera/cpp/agent-foundations/gojson/main.go.

struct Inner {
    double x = 0;
    double y = 0;
};

bool operator==(const Inner& a, const Inner& b) { return a.x == b.x && a.y == b.y; }

constexpr double seed_rate = 1.5;
constexpr std::int64_t seed_size = 7;
constexpr double seed_opt = 1;
constexpr std::int64_t seed_opt_int = 2;
constexpr double seed_list_first = 5;
constexpr double seed_list_second = 6;
constexpr Inner seed_nested{.x = 3, .y = 4};
constexpr Inner seed_inner{.x = 1, .y = 2};

using Rates = gojson::Map<std::optional<std::vector<double>>>;

// Go's struct, with every field holding something before the decode, so a
// field the decode leaves alone is told apart from one it resets.
struct Wire {
    std::string name = "kept";
    double rate = seed_rate;
    std::int64_t size = seed_size;
    bool on = true;
    std::optional<double> opt = seed_opt;
    std::optional<std::int64_t> opt_int = seed_opt_int;
    std::optional<gojson::Map<double>> totals = gojson::Map<double>{{"a", 1}, {"b", 2}};
    std::optional<gojson::Map<Inner>> structs = gojson::Map<Inner>{{"a", seed_inner}};
    std::optional<std::vector<double>> list =
        std::vector<double>{seed_list_first, seed_list_second};
    Inner nested = seed_nested;
    std::optional<Rates> rates;
};

void decode_inner(gojson::Decode& d, simdjson::dom::element value, Inner& out) {
    d.as_struct(value, [&](simdjson::dom::object object) {
        d.fields(object, "x", [&](auto v) { d.float_into(v, out.x); });
        d.fields(object, "y", [&](auto v) { d.float_into(v, out.y); });
    });
}

void decode_number(gojson::Decode& d, simdjson::dom::element value, double& out) {
    d.float_into(value, out);
}

void decode_numbers(gojson::Decode& d,
                    simdjson::dom::element value,
                    std::optional<std::vector<double>>& out) {
    d.slice_into(value, out, decode_number);
}

void decode_wire(gojson::Decode& d, simdjson::dom::object top, Wire& w) {
    d.fields(top, "name", [&](auto v) { d.string_into(v, w.name); });
    d.fields(top, "rate", [&](auto v) { d.float_into(v, w.rate); });
    d.fields(top, "size", [&](auto v) { d.int_into(v, w.size); });
    d.fields(top, "on", [&](auto v) { d.bool_into(v, w.on); });
    d.fields(top, "opt", [&](auto v) { d.optional_float_into(v, w.opt); });
    d.fields(top, "opt_int", [&](auto v) { d.optional_int_into(v, w.opt_int); });
    d.fields(top, "totals", [&](auto v) { d.map_into(v, w.totals, decode_number); });
    d.fields(top, "structs", [&](auto v) { d.map_into(v, w.structs, decode_inner); });
    d.fields(top, "list", [&](auto v) { d.slice_into(v, w.list, decode_number); });
    d.fields(top, "nested", [&](auto v) { decode_inner(d, v, w.nested); });
    d.fields(top, "rates", [&](auto v) { d.map_into(v, w.rates, decode_numbers); });
}

// json.Unmarshal(text, &w) == nil, with w seeded. Text that does not parse is
// an error before any field is touched, as it is in Go.
bool unmarshal(std::string_view text, Wire& w) {
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (parser.parse(simdjson::padded_string(text)).get(root) != simdjson::SUCCESS) {
        return false;
    }
    gojson::Decode decode;
    decode.as_struct(root,
                     [&](simdjson::dom::object top) { decode_wire(decode, top, w); });
    return decode.ok();
}

// Whether w still holds every seeded value.
bool untouched(const Wire& w) {
    const Wire seeded;
    return w.name == seeded.name && w.rate == seeded.rate && w.size == seeded.size &&
           w.on == seeded.on && w.opt == seeded.opt && w.opt_int == seeded.opt_int &&
           w.totals == seeded.totals && w.structs == seeded.structs &&
           w.list == seeded.list && w.nested == seeded.nested &&
           w.rates == seeded.rates;
}

}  // namespace

// COVERS: FR-8.16 | property
//
// The second rule in gojson.hpp: null leaves a plain value as it was, a nested
// struct included, and resets a pointer, a map and a slice to nil.
TEST_CASE("null leaves plain values and resets pointers, maps and slices") {
    Wire w;
    REQUIRE(unmarshal(R"({"name":null,"rate":null,"size":null,"on":null,)"
                      R"("opt":null,"opt_int":null,"totals":null,"structs":null,)"
                      R"("list":null,"nested":null})",
                      w));
    const Wire seeded;
    CHECK(w.name == seeded.name);
    CHECK(w.rate == seeded.rate);
    CHECK(w.size == seeded.size);
    CHECK(w.on == seeded.on);
    CHECK(w.nested == seeded.nested);
    CHECK_FALSE(w.opt.has_value());
    CHECK_FALSE(w.opt_int.has_value());
    CHECK_FALSE(w.totals.has_value());
    CHECK_FALSE(w.structs.has_value());
    CHECK_FALSE(w.list.has_value());
}

// COVERS: FR-8.16 | negative
//
// The first rule: a wrong type is an error and does not stop the decode, so a
// field after it is still set, and the caller testing the error throws the whole
// file away. A malformed rate table falls back to the seed on exactly this.
TEST_CASE("one wrong type rejects the decode and the decode carries on") {
    Wire w;
    CHECK_FALSE(unmarshal(R"({"rate":"x","name":"after"})", w));
    CHECK(w.name == "after");
    CHECK(w.rate == seed_rate);

    Wire nested;
    CHECK_FALSE(
        unmarshal(R"({"unknown":1,"nested":{"x":"bad"},"name":"after"})", nested));
    CHECK(nested.name == "after");
    CHECK(nested.nested == seed_nested);
}

// COVERS: FR-8.16 | negative
//
// Every plain kind refuses every other kind, and a struct refuses anything but
// an object or null, the root included.
TEST_CASE("each kind refuses a value of another type") {
    for (const std::string_view text : {
             R"({"name":1})",
             R"({"rate":"1"})",
             R"({"size":"1"})",
             R"({"on":1})",
             R"({"opt":"x"})",
             R"({"opt_int":1.5})",
             R"({"opt_int":"x"})",
             R"({"totals":[1]})",
             R"({"totals":{"a":"x"}})",
             R"({"list":{"a":1}})",
             R"({"nested":5})",
             R"([1])",
             R"({"open":)",
         }) {
        CAPTURE(text);
        Wire w;
        CHECK_FALSE(unmarshal(text, w));
    }
}

// COVERS: FR-8.10 | negative
//
// An offset is an int64, and Go refuses a fraction, an exponent or a literal past
// int64 for one even where the value is whole. A state file carrying one is
// unreadable, which costs a re-sum rather than a wrong offset.
TEST_CASE("an int refuses a fraction, an exponent and a literal past int64") {
    for (const std::string_view text : {
             R"({"size":1.0})",
             R"({"size":1e2})",
             R"({"size":9223372036854775808})",
         }) {
        CAPTURE(text);
        Wire w;
        CHECK_FALSE(unmarshal(text, w));
        CHECK(w.size == seed_size);
    }
    Wire w;
    REQUIRE(unmarshal(R"({"size":-9223372036854775808})", w));
    CHECK(w.size == INT64_MIN);
}

// COVERS: FR-8.15 | positive
//
// A float takes any number, an unsigned literal past int64 included, and a
// pointer takes its value when one is there.
TEST_CASE("values of the right type are set") {
    Wire w;
    REQUIRE(unmarshal(R"({"rate":18446744073709551615,"opt":2.5,"opt_int":9,)"
                      R"("on":false,"name":"set"})",
                      w));
    constexpr double largest_unsigned = 18446744073709551615.0;
    constexpr double opt = 2.5;
    constexpr std::int64_t opt_int = 9;
    CHECK(w.rate == largest_unsigned);
    CHECK(w.opt == opt);
    CHECK(w.opt_int == opt_int);
    CHECK_FALSE(w.on);
    CHECK(w.name == "set");
}

// COVERS: FR-8.15 | property
//
// An object decodes into an existing map, keeping the keys it does not name, and
// each key it does name starts from a fresh zero value, so a null entry is zero
// and a struct entry loses the fields the object leaves out.
TEST_CASE("a map is merged and each entry starts from zero") {
    Wire merged;
    REQUIRE(unmarshal(R"({"totals":{"b":3,"c":4}})", merged));
    const gojson::Map<double> want_merged{{"a", 1}, {"b", 3}, {"c", 4}};
    CHECK(merged.totals == want_merged);

    Wire null_entry;
    REQUIRE(unmarshal(R"({"totals":{"a":null}})", null_entry));
    const gojson::Map<double> want_null{{"a", 0}, {"b", 2}};
    CHECK(null_entry.totals == want_null);

    Wire fresh;
    REQUIRE(unmarshal(R"({"structs":{"a":{"y":5}}})", fresh));
    const gojson::Map<Inner> want_fresh{{"a", Inner{.x = 0, .y = 5}}};
    CHECK(fresh.structs == want_fresh);
}

// COVERS: FR-8.15 | property
//
// A nil map is created by the first object, and the rate table's map of slices
// keeps a null entry as a present key holding nil.
TEST_CASE("a nil map is created and holds a nil slice for null") {
    Wire w;
    REQUIRE(unmarshal(R"({"rates":{"m":[1,2],"n":null}})", w));
    const Rates want{{"m", std::vector<double>{1, 2}}, {"n", std::nullopt}};
    CHECK(w.rates == want);

    Wire repeated;
    REQUIRE(unmarshal(R"({"rates":{"m":[1,2],"m":[null]}})", repeated));
    const Rates want_repeated{{"m", std::vector<double>{0}}};
    CHECK(repeated.rates == want_repeated);
}

// COVERS: FR-8.15 | property
//
// A struct decodes into what is there, so a field the object leaves out keeps
// its value, and a repeated key is read to its last value.
TEST_CASE("a struct is merged and a repeated key keeps its last value") {
    Wire nested;
    REQUIRE(unmarshal(R"({"nested":{"y":9}})", nested));
    CHECK(nested.nested == Inner{.x = seed_nested.x, .y = 9});

    Wire repeated;
    REQUIRE(unmarshal(R"({"name":"first","name":"last"})", repeated));
    CHECK(repeated.name == "last");

    Wire root_null;
    REQUIRE(unmarshal("null", root_null));
    CHECK(untouched(root_null));
}

// COVERS: FR-3.3 | property
//
// An array replaces the slice's length: herdr's pane list is read whole, and a
// shorter list leaves no pane from before behind.
TEST_CASE("an array sets the slice's length") {
    Wire w;
    REQUIRE(unmarshal(R"({"list":[7]})", w));
    CHECK(w.list == std::vector<double>{7});

    Wire empty;
    REQUIRE(unmarshal(R"({"list":[]})", empty));
    CHECK(empty.list == std::vector<double>{});
}

// COVERS: FR-3.3 | edge
//
// encoding/json decodes an array INTO the slice's existing elements
// (decode.go, array: `if i < v.Len() { d.value(v.Index(i)) }`), so null over an
// element that was already there leaves it, where slice_into starts every
// element from zero. Go printed [5] for this input.
TEST_CASE("null over an existing slice element leaves it, as Go does") {
    Wire w;
    REQUIRE(unmarshal(R"({"list":[null]})", w));
    CHECK(w.list == std::vector<double>{seed_list_first});
}
