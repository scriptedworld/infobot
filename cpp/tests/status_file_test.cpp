#include "status_file.hpp"

#include <doctest/doctest.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "environment.hpp"
#include "payload.hpp"
#include "support.hpp"

namespace status_file = infobot::status_file;
using infobot::Environment;
using infobot::payload::Document;
using infobot::test::Scratch;

namespace {

// A code point as UTF-8, so a test names the character by number rather than
// carrying it invisibly in the source.
std::string utf8(char32_t rune) {
    constexpr char32_t one = 0x7F;
    constexpr char32_t two = 0x7FF;
    constexpr char32_t three = 0xFFFF;
    constexpr unsigned lead2 = 0xC0;
    constexpr unsigned lead3 = 0xE0;
    constexpr unsigned lead4 = 0xF0;
    constexpr unsigned tail = 0x80;
    constexpr unsigned mask = 0x3F;
    constexpr int step = 6;
    const auto byte = [](unsigned value) { return static_cast<char>(value); };
    if (rune <= one) {
        return {byte(rune)};
    }
    if (rune <= two) {
        return {byte(lead2 | (rune >> step)), byte(tail | (rune & mask))};
    }
    if (rune <= three) {
        return {byte(lead3 | (rune >> (2 * step))),
                byte(tail | ((rune >> step) & mask)),
                byte(tail | (rune & mask))};
    }
    return {byte(lead4 | (rune >> (3 * step))),
            byte(tail | ((rune >> (2 * step)) & mask)),
            byte(tail | ((rune >> step) & mask)),
            byte(tail | (rune & mask))};
}

// A JSON string literal for text, escaping only what JSON requires.
std::string json_string(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (byte < ' ') {
            constexpr std::string_view hex = "0123456789abcdef";
            constexpr int nibble = 4;
            constexpr unsigned low = 0x0F;
            out += "\\u00";
            out += hex[byte >> nibble];
            out += hex[byte & low];
        } else {
            out += c;
        }
    }
    return out + "\"";
}

// The payload the Go suite's full() builds, with the cwd replaced.
std::string full_payload(std::string_view cwd) {
    return R"({"session_id":"abcd-1234",)"
           R"("model":{"display_name":"Opus 5 \"1M\" \\ context"},)"
           R"("effort":{"level":"xhigh"},)"
           R"("workspace":{"current_dir":)" +
           json_string(cwd) +
           R"(},)"
           R"("context_window":{"context_window_size":1000000,"used_percentage":48.2,)"
           R"("current_usage":{"input_tokens":480000}}})";
}

Environment state_in(const Scratch& scratch) {
    Environment env;
    env.xdg_state_home = scratch.path();
    return env;
}

// Writes a payload into a scratch state directory and returns the file, or
// nullopt when nothing was written.
std::optional<std::string> written(const Scratch& scratch, const std::string& payload) {
    const Document document(payload);
    const Environment env = state_in(scratch);
    status_file::write(document.root(), env, infobot::test::clock());
    const std::string path =
        status_file::path(document.root().str("session_id"), env);
    if (path.empty() || !std::filesystem::exists(path)) {
        return std::nullopt;
    }
    return infobot::test::slurp(path);
}

// The file with its `written` line taken out, which is the only line that
// depends on the time zone this process runs in.
std::string without_written(const std::string& file) {
    std::string out;
    std::string_view rest = file;
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        const std::string_view line = rest.substr(0, end == std::string_view::npos ? rest.size() : end + 1);
        if (!line.starts_with("\"written\"")) {
            out += line;
        }
        rest.remove_prefix(line.size());
    }
    return out;
}

// The cwd line a render writes for text.
std::string cwd_line(std::string_view text) {
    const Scratch scratch;
    const std::string file = written(scratch, full_payload(text)).value_or("");
    const std::size_t at = file.find("\"cwd\"");
    REQUIRE(at != std::string::npos);
    return file.substr(at, file.find('\n', at) - at);
}

std::string percent_line(double pct) {
    const Scratch scratch;
    const std::string payload =
        R"({"session_id":"exponent","context_window":{"context_window_size":100,"used_percentage":)" +
        std::to_string(pct) + "}}";
    const std::string file = written(scratch, payload).value_or("");
    const std::size_t at = file.find("\"context_percent\"");
    REQUIRE(at != std::string::npos);
    return file.substr(at, file.find('\n', at) - at);
}

}  // namespace

// COVERS: FR-1.11g, FR-1.11o, FR-1.11p | property
//
// The exact bytes, because the form is a published interface: silo's board
// matches anchored patterns on the quoted key and the single space after the
// colon, and takes the number bare. These are the bytes TestCanonicalForm pins
// for the Go emitter, so the two implementations are held to one fixture.
TEST_CASE("canonical form") {
    constexpr std::string_view want = R"("context_percent": 48.2
"context_remaining": 520000
"context_size": 1000000
"context_used": 480000
"cwd": "/home/me/.projects/infobot"
"effort": "xhigh"
"model": "Opus 5 \"1M\" \\ context"
"session": "abcd-1234"
)";
    const Scratch scratch;
    const auto file = written(scratch, full_payload("/home/me/.projects/infobot"));
    REQUIRE(file.has_value());
    CHECK(without_written(file.value_or("")) == want);
    CHECK(file.value_or("").find("\"written\": \"") != std::string::npos);
}

// COVERS: FR-1.11g | property
//
// `written` is ISO 8601 to the second in this process's own zone, with the
// offset as +hh:mm, never Z and never +hhmm.
TEST_CASE("written is local time to the second with the offset") {
    const std::string stamp = status_file::timestamp(infobot::test::clock());
    const std::regex shape(R"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d[+-]\d\d:\d\d)");
    REQUIRE(std::regex_match(stamp, shape));
    const auto when = static_cast<std::time_t>(infobot::test::clock_seconds);
    std::tm local{};
    REQUIRE(::localtime_r(&when, &local) != nullptr);
    constexpr long seconds_per_minute = 60;
    constexpr long minutes_per_hour = 60;
    constexpr int year_base = 1900;
    const long offset = local.tm_gmtoff / seconds_per_minute;
    const long magnitude = offset < 0 ? -offset : offset;
    const std::string want = std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}{}{:02}:{:02}",
                                         local.tm_year + year_base,
                                         local.tm_mon + 1,
                                         local.tm_mday,
                                         local.tm_hour,
                                         local.tm_min,
                                         local.tm_sec,
                                         offset < 0 ? '-' : '+',
                                         magnitude / minutes_per_hour,
                                         magnitude % minutes_per_hour);
    CHECK(stamp == want);
}

// COVERS: FR-1.11g | property
//
// A key whose value is empty is omitted rather than written blank, so a reader
// tells "not said" from "said to be nothing". session and written always stay.
TEST_CASE("empty values are omitted") {
    const Scratch scratch;
    const std::string file = written(scratch, R"({"session_id":"bare","model":{"display_name":""},)"
                                              R"("workspace":{"current_dir":""},"effort":{"level":""}})")
                                 .value_or("");
    for (const std::string_view key : {"cwd", "model", "effort", "context_used"}) {
        CAPTURE(key);
        CHECK(file.find("\"" + std::string(key) + "\"") == std::string::npos);
    }
    CHECK(file.find("\"session\"") != std::string::npos);
    CHECK(file.find("\"written\"") != std::string::npos);
}

// COVERS: FR-2.8 | positive
//
// The model is display_name where there is one and id where there is not.
TEST_CASE("the model falls back to its id") {
    const Scratch scratch;
    const std::string file =
        written(scratch, R"({"session_id":"id","model":{"id":"claude-x"}})").value_or("");
    CHECK(file.find("\"model\": \"claude-x\"\n") != std::string::npos);
}

// COVERS: FR-1.11b | property
TEST_CASE("the file is readable by other programs") {
    constexpr auto readable = std::filesystem::perms::owner_read |
                              std::filesystem::perms::owner_write |
                              std::filesystem::perms::group_read |
                              std::filesystem::perms::others_read;
    const Scratch scratch;
    REQUIRE(written(scratch, full_payload("/x")).has_value());
    const auto path = status_file::path("abcd-1234", state_in(scratch));
    CHECK(std::filesystem::status(path).permissions() == readable);
}

// COVERS: FR-1.11b | property
//
// Written whole or not at all, and the temporary is gone either way, so a
// directory of state files never accumulates half-written ones.
TEST_CASE("no temporary is left behind") {
    const Scratch scratch;
    REQUIRE(written(scratch, full_payload("/x")).has_value());
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(scratch.path() + "/infobot")) {
        names.push_back(entry.path().filename().string());
    }
    CHECK(names == std::vector<std::string>{"abcd-1234.status.yaml"});
}

// COVERS: FR-1.11b | negative
//
// A write that fails costs nothing and leaves nothing: a state directory this
// process may not write into, and a state root that is a file.
TEST_CASE("a write that cannot happen leaves nothing") {
    constexpr auto read_only = std::filesystem::perms::owner_read |
                               std::filesystem::perms::owner_exec;
    const Scratch locked;
    std::filesystem::create_directories(locked.path() + "/infobot");
    std::filesystem::permissions(locked.path() + "/infobot", read_only);
    CHECK_FALSE(written(locked, full_payload("/x")).has_value());
    CHECK(std::filesystem::is_empty(locked.path() + "/infobot"));
    std::filesystem::permissions(locked.path() + "/infobot", std::filesystem::perms::owner_all);

    const Scratch blocked;
    (void)blocked.write("infobot", "a file where the directory would be\n");
    CHECK_FALSE(written(blocked, full_payload("/x")).has_value());
}

// COVERS: FR-1.11 | negative
TEST_CASE("no session id writes nothing") {
    const Scratch scratch;
    CHECK_FALSE(written(scratch, R"({"model":{"display_name":"Opus 5"}})").has_value());
    CHECK_FALSE(std::filesystem::exists(scratch.path() + "/infobot"));
}

// COVERS: FR-1.11 | edge
//
// With no state directory to be found, there is no path and nothing is written.
TEST_CASE("no state directory means no path") {
    CHECK(status_file::path("abcd-1234", Environment{}).empty());
    const Document document(full_payload("/x"));
    status_file::write(document.root(), Environment{}, infobot::test::clock());
}

// COVERS: FR-1.11h | edge
//
// context_remaining is never negative, and context_percent is neither floored
// nor capped, so a reader sees an over-full window as over-full.
TEST_CASE("over-full window clamps remaining but not percent") {
    const Scratch scratch;
    const std::string file =
        written(scratch,
                R"({"session_id":"over","context_window":{"context_window_size":100,)"
                R"("used_percentage":130,"current_usage":{"input_tokens":130}}})")
            .value_or("");
    CHECK(file.find("\"context_remaining\": 0\n") != std::string::npos);
    CHECK(file.find("\"context_percent\": 130.0\n") != std::string::npos);
}

// COVERS: FR-1.11 | negative
//
// The schema is checked before anything is written. A negative percentage
// fails its minimum, so no file appears rather than a file no reader trusts.
TEST_CASE("a payload the schema refuses writes nothing") {
    const Scratch scratch;
    CHECK_FALSE(written(scratch,
                        R"({"session_id":"negative","context_window":)"
                        R"({"context_window_size":100,"used_percentage":-5}})")
                    .has_value());
}

// COVERS: FR-1.11q | property
//
// A number is spelled without an exponent, ever. `1e+06` is a legal spelling of
// a million and silo's board matches `[0-9.]+` against the value, so it would
// capture `1`, report a plausible small number, and never fail.
TEST_CASE("numbers are never spelled with an exponent") {
    for (const double pct : {1e6, 1.23456789e8, 1e21, 1e-7, 48.2, 0.0, 100.0}) {
        CAPTURE(pct);
        const std::string line = percent_line(pct);
        const std::string value = line.substr(line.find(": ") + 2);
        CHECK(value.find_first_of("eE") == std::string::npos);
    }
}

// COVERS: FR-1.11q | property
//
// A float keeps its decimal point so a reader gets a float back, and an integer
// does not have one.
TEST_CASE("a float keeps its point and an integer does not") {
    const Scratch scratch;
    const std::string file =
        written(scratch,
                R"({"session_id":"types","context_window":{"context_window_size":1000000,)"
                R"("used_percentage":50,"current_usage":{"input_tokens":500000}}})")
            .value_or("");
    CHECK(file.find("\"context_percent\": 50.0\n") != std::string::npos);
    CHECK(file.find("\"context_size\": 1000000\n") != std::string::npos);
}

// COVERS: FR-1.11q | property
//
// The shortest digits that read back as the same double, placed positionally.
// Past 2^53 the exact binary value has more digits than that, and writing them
// would be a different spelling from the Go emitter's for the same number.
TEST_CASE("floats are spelled with the shortest digits") {
    struct Case {
        double in;
        std::string_view want;
    };
    for (const auto& c : {
             Case{.in = 0.0, .want = "0.0"},
             Case{.in = -0.0, .want = "-0.0"},
             Case{.in = 48.2, .want = "48.2"},
             Case{.in = 1e-7, .want = "0.0000001"},
             Case{.in = 1e21, .want = "1000000000000000000000.0"},
             Case{.in = 123456789.125, .want = "123456789.125"},
             Case{.in = -2.5, .want = "-2.5"},
         }) {
        CAPTURE(c.in);
        CHECK(status_file::float_text(c.in) == c.want);
    }
    constexpr double huge = 1e300;
    constexpr std::size_t zeros = 300;
    CHECK(status_file::float_text(huge) == "1" + std::string(zeros, '0') + ".0");
}

// COVERS: FR-1.11q | edge
//
// NaN and the infinities have no spelling in the form, so encoding refuses them
// rather than writing something a JSON Schema reader cannot represent.
TEST_CASE("a value with no canonical spelling is refused") {
    CHECK_FALSE(status_file::float_text(std::numeric_limits<double>::quiet_NaN()).has_value());
    CHECK_FALSE(status_file::float_text(std::numeric_limits<double>::infinity()).has_value());
    status_file::Fields fields;
    fields.insert_or_assign("context_percent", std::numeric_limits<double>::infinity());
    CHECK_FALSE(status_file::encode(fields).has_value());
    CHECK_FALSE(status_file::valid(fields));
}

// COVERS: FR-1.11g | edge
//
// A count converts to an integer by saturating rather than overflowing, and a
// NaN or a negative count is zero.
TEST_CASE("tokens saturate rather than overflow") {
    constexpr double beyond = 1e21;
    constexpr double some = 12345.9;
    CHECK(status_file::tokens(beyond) == std::numeric_limits<std::int64_t>::max());
    CHECK(status_file::tokens(std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(status_file::tokens(-1) == 0);
    CHECK(status_file::tokens(some) == 12345);
}

// COVERS: FR-1.11r | property
//
// Every C0 control, DEL, every C1 control and the two line separators reach
// the file escaped, never raw, across the whole range rather than a sample.
TEST_CASE("every control code point is escaped") {
    std::vector<char32_t> points;
    constexpr char32_t c0_end = 0x20;
    constexpr char32_t del = 0x7F;
    constexpr char32_t c1_start = 0x80;
    constexpr char32_t c1_end = 0xA0;
    for (char32_t r = 0; r < c0_end; ++r) {
        points.push_back(r);
    }
    points.push_back(del);
    for (char32_t r = c1_start; r < c1_end; ++r) {
        points.push_back(r);
    }
    points.push_back(U'\x2028');
    points.push_back(U'\x2029');
    for (const char32_t r : points) {
        CAPTURE(static_cast<std::uint32_t>(r));
        const std::string line = cwd_line("/a" + utf8(r) + "b");
        CHECK(line.find(utf8(r), 1) == std::string::npos);
        CHECK(line.find("\\") != std::string::npos);
    }
}

// COVERS: FR-1.11r | negative
//
// The line break set, spelled with YAML's own names: a parser accepts these raw
// and hands back a space, which no reader can detect.
TEST_CASE("the line break set is escaped") {
    CHECK(cwd_line("/a\nb") == R"("cwd": "/a\nb")");
    CHECK(cwd_line("/a\rb") == R"("cwd": "/a\rb")");
    CHECK(cwd_line("/a" + utf8(U'\x2028') + "b") == R"("cwd": "/a\Lb")");
    CHECK(cwd_line("/a" + utf8(U'\x2029') + "b") == R"("cwd": "/a\Pb")");
    CHECK(cwd_line("/a" + utf8(U'\x85') + "b") == R"("cwd": "/a\Nb")");
}

// COVERS: FR-1.11r | edge
//
// Two hex digits, uppercase, because `\x9` is a truncated escape rather than a
// tab.
TEST_CASE("short escapes are zero padded") {
    CHECK(cwd_line("/a\x01" "b") == R"("cwd": "/a\x01b")");
    CHECK(cwd_line("/a\x7F" "b") == R"("cwd": "/a\x7Fb")");
    CHECK(cwd_line("/a\tb") == R"("cwd": "/a\tb")");
    CHECK(cwd_line("/a\x1B" "b") == R"("cwd": "/a\eb")");
}

// COVERS: FR-1.11r | positive
//
// Nothing outside the escaped set changes, which is what makes the escaping a
// fix rather than a change to what the form emits for every value it has held.
TEST_CASE("ordinary text is untouched") {
    const std::string accented = "caf" + utf8(U'\xE9') + " " + utf8(U'\x65E5') + utf8(U'\x672C');
    for (const std::string& text : std::initializer_list<std::string>{
             "/home/x/proj", "Opus 5 (1M)", "a b", accented, "/a" + utf8(U'\x200B') + "b", "/a" + utf8(U'\xA0') + "b"}) {
        CAPTURE(text);
        CHECK(cwd_line(text) == "\"cwd\": \"" + text + "\"");
    }
}

// COVERS: FR-1.11r, FR-1.11o | edge
//
// What yaml.v3 does beyond the control set, held because the bytes are the
// interface: a four-byte character is written as a \U escape,
// U+FEFF and the non-characters as \u escapes, and a value that OPENS with a
// byte-order mark has every character escaped, because the emitter tests the
// value's first three bytes wherever in it it is.
TEST_CASE("the emitter's escapes past the control set") {
    CHECK(cwd_line("/a" + utf8(U'\x1F9E0') + "b") == R"("cwd": "/a\U0001F9E0b")");
    CHECK(cwd_line("/a" + utf8(U'\xFEFF') + "b") == R"("cwd": "/a\uFEFFb")");
    CHECK(cwd_line("/a" + utf8(U'\xFFFE') + "b") == R"("cwd": "/a\uFFFEb")");
    CHECK(cwd_line(utf8(U'\xFEFF') + "ab") == R"("cwd": "\uFEFF\x61\x62")");
}
