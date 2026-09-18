// The two entry points' contract: exit 0 whatever they are given.
#include <doctest/doctest.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <initializer_list>
#include <string_view>

#include "forget.hpp"
#include "statusline.hpp"

namespace {

// Runs an entry point with body on its input, and a pipe it may write to.
int with_input(int (*entry)(int, int) noexcept, std::string_view body) {
    std::array<int, 2> in{};
    std::array<int, 2> out{};
    REQUIRE(::pipe(in.data()) == 0);
    REQUIRE(::pipe(out.data()) == 0);
    REQUIRE(::write(in[1], body.data(), body.size()) ==
            static_cast<ssize_t>(body.size()));
    ::close(in[1]);
    const int status = entry(in[0], out[1]);
    for (const int end : {in[0], out[0], out[1]}) {
        ::close(end);
    }
    return status;
}

constexpr std::array<std::string_view, 5> inputs{"", "not json", "{}", "[]", "null"};

}  // namespace

// COVERS: FR-1.2 | negative
TEST_CASE("the status line exits 0 whatever it is given") {
    for (const std::string_view input : inputs) {
        CAPTURE(input);
        CHECK(with_input(infobot::statusline::run, input) == 0);
    }
}

// COVERS: FR-1.11f | negative
TEST_CASE("the cleanup exits 0 whatever it is given") {
    for (const std::string_view input : inputs) {
        CAPTURE(input);
        CHECK(with_input(infobot::forget::run, input) == 0);
    }
}
