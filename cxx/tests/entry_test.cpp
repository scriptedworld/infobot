// What both entry points promise: exit 0 whatever they are given, and whatever
// happens inside them.
#include <doctest/doctest.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string_view>

#include "allocation.hpp"
#include "forget.hpp"
#include "render.hpp"

namespace test = infobot::test;

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
        CHECK(with_input(infobot::render::statusline, input) == 0);
    }
}

// COVERS: FR-1.11f | negative
TEST_CASE("the cleanup exits 0 whatever it is given") {
    for (const std::string_view input : inputs) {
        CAPTURE(input);
        CHECK(with_input(infobot::forget::run, input) == 0);
    }
}

// COVERS: FR-1.2 | edge
//
// An allocation failing anywhere in a render is still an exit of 0 and no
// traceback in the status bar. The suite's allocator fails each allocation a
// render makes, one run at a time.
TEST_CASE("an allocation failing anywhere still exits 0") {
    std::size_t runs = 0;
    const test::Sweep sweep = test::fail_each_allocation(
        [] {
            CHECK(with_input(infobot::render::statusline, R"({"session_id":"s"})") ==
                  0);
        },
        [&runs](std::size_t /*run*/) { ++runs; });
    CHECK(sweep.completed);
    CHECK(sweep.thrown == 0);
    CHECK(sweep.absorbed > 0);
    CHECK(runs > 1);
}

// COVERS: FR-1.11f | edge
TEST_CASE("an allocation failing in the cleanup still exits 0") {
    const test::Sweep sweep = test::fail_each_allocation(
        [] { CHECK(with_input(infobot::forget::run, R"({"session_id":"s"})") == 0); },
        [](std::size_t /*run*/) {});
    CHECK(sweep.completed);
    CHECK(sweep.thrown == 0);
    CHECK(sweep.absorbed > 0);
}
