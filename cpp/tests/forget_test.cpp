#include "forget.hpp"

#include <doctest/doctest.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "environment.hpp"
#include "goquote.hpp"
#include "support.hpp"

namespace forget = infobot::forget;
namespace goquote = infobot::goquote;
using infobot::Environment;
using infobot::test::Scratch;

namespace {

Environment state_in(const Scratch& scratch) {
    Environment env;
    env.xdg_state_home = scratch.path();
    return env;
}

// forget's log for a payload, run over real descriptors: the payload from a
// scratch file, the log into another.
std::string logged(const Scratch& scratch, std::string_view payload, const Environment& env) {
    const std::string in_path = scratch.write("payload.json", payload);
    const std::string log_path = scratch.file("log");
    const int in = ::open(in_path.c_str(), O_RDONLY | O_CLOEXEC);
    constexpr int mode = 0600;
    const int log = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    REQUIRE(in >= 0);
    REQUIRE(log >= 0);
    CHECK(forget::run({.input = in, .log = log}, env) == 0);
    ::close(in);
    ::close(log);
    return infobot::test::slurp(log_path);
}

}  // namespace

// COVERS: FR-1.11f | negative
//
// A session id names one file each. Anything else, including a path separator
// smuggled through the payload, is refused rather than joined onto a directory
// this walks with unlink.
TEST_CASE("a session id carrying a path is refused") {
    for (const std::string_view bad :
         {"", "..", "../../etc/passwd", "a/b", "a\\b", "..%2f", "ok/../../.."}) {
        CAPTURE(bad);
        CHECK_FALSE(forget::named(bad));
    }
    for (const std::string_view good :
         {"abcd-1234", "a5e58a4d-2a4e-4774-aa6c-1e7745721df6", "plain"}) {
        CAPTURE(good);
        CHECK(forget::named(good));
    }
}

// COVERS: FR-1.11e | positive
TEST_CASE("remove takes both files and says which") {
    const Scratch scratch;
    const std::string status = scratch.write("infobot/s.status.yaml", "x");
    const std::string offsets = scratch.write("infobot/s.json", "x");
    const std::vector<std::string> removed = forget::remove("s", state_in(scratch));
    CHECK(removed == std::vector<std::string>{status, offsets});
    CHECK(std::filesystem::is_empty(scratch.path() + "/infobot"));
}

// COVERS: FR-1.11e | edge
//
// With no state directory to be found there is nothing to name, so nothing is
// removed and nothing fails.
TEST_CASE("remove with no state directory removes nothing") {
    CHECK(forget::remove("s", Environment{}).empty());
}

// COVERS: FR-1.11f | edge
//
// A cleanup that exits 0 having removed nothing is the same shape as a gate
// that passes having checked nothing, so it says which it was.
TEST_CASE("nothing to remove is said rather than implied") {
    const Scratch scratch;
    const Scratch state;
    CHECK(logged(scratch, R"({"session_id":"never-existed"})", state_in(state)) ==
          "forget-session: never-existed had nothing to remove\n");
}

// COVERS: FR-1.11e | positive
TEST_CASE("the log names every file removed") {
    const Scratch scratch;
    const Scratch state;
    const std::string status = state.write("infobot/s.status.yaml", "x");
    const std::string offsets = state.write("infobot/s.json", "x");
    CHECK(logged(scratch, R"({"session_id":"s"})", state_in(state)) ==
          "forget-session: s removed " + status + " " + offsets + "\n");
}

// COVERS: FR-1.11f | negative
TEST_CASE("forget exits zero whatever it is given") {
    const Scratch state;
    for (const std::string_view in : {"",
                                      "not json",
                                      "[]",
                                      "null",
                                      "{}",
                                      R"({"session_id":123})",
                                      R"({"session_id":"../escape"})"}) {
        CAPTURE(in);
        const Scratch scratch;
        (void)logged(scratch, in, state_in(state));
    }
    const std::array<int, 2> closed = {-1, -1};
    CHECK(forget::run({.input = closed[0], .log = closed[1]}, state_in(state)) == 0);
}

// COVERS: FR-1.11f | negative
//
// A refused id is said out loud, quoted as Go's %q quotes it, and reaches no
// file.
TEST_CASE("a refused id is reported and removes nothing") {
    const Scratch scratch;
    const Scratch state;
    const std::string keep = state.write("infobot/innocent.status.yaml", "x");
    CHECK(logged(scratch, R"({"session_id":"../infobot/innocent"})", state_in(state)) ==
          "forget-session: refused session id \"../infobot/innocent\"\n");
    CHECK(std::filesystem::exists(keep));
    CHECK(logged(scratch, R"({"session_id":123})", state_in(state)) ==
          "forget-session: refused session id \"\"\n");
}

// COVERS: FR-1.11f | property
//
// strconv.Quote: printable runes as they are, the usual backslash escapes, \x
// for a control character or a stray byte, and \u or \U for a rune that is not
// printable, lowercase hex throughout.
TEST_CASE("a session id is quoted as Go quotes it") {
    struct Case {
        std::string_view in;
        std::string_view want;
    };
    for (const auto& c : {
             Case{.in = "plain", .want = R"("plain")"},
             Case{.in = "a\"b\\c", .want = R"("a\"b\\c")"},
             Case{.in = "\a\b\f\n\r\t\v", .want = R"("\a\b\f\n\r\t\v")"},
             Case{.in = "\x01\x7f", .want = R"("\x01\x7f")"},
             Case{.in = "\xff", .want = R"("\xff")"},
             Case{.in = "\xc2\x85", .want = R"("\u0085")"},
             Case{.in = "\xe2\x80\xa8", .want = R"("\u2028")"},
             Case{.in = "\xf3\xa0\x80\x81", .want = R"("\U000e0001")"},
             Case{.in = "caf\xc3\xa9", .want = "\"caf\xc3\xa9\""},
         }) {
        CAPTURE(c.in);
        CHECK(goquote::quote(c.in) == c.want);
    }
}

// COVERS: FR-1.11f | edge
//
// What strconv.IsPrint counts as printable, at the edges of its table.
TEST_CASE("printable follows strconv.IsPrint") {
    constexpr char32_t space = 0x20;
    constexpr char32_t tilde = 0x7E;
    constexpr char32_t del = 0x7F;
    constexpr char32_t nbsp = 0xA0;
    constexpr char32_t inverted_bang = 0xA1;
    constexpr char32_t max = 0x10FFFF;
    CHECK(goquote::is_print(space));
    CHECK(goquote::is_print(tilde));
    CHECK_FALSE(goquote::is_print(del));
    CHECK_FALSE(goquote::is_print(nbsp));
    CHECK(goquote::is_print(inverted_bang));
    CHECK_FALSE(goquote::is_print(0));
    CHECK_FALSE(goquote::is_print(max));
}
