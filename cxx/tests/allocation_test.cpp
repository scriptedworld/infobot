// The suite's failing allocator, checked before any test relies on it.
//
// These cite no requirement: they test a tool the tests use, not the program.
#include "allocation.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <memory>
#include <new>

namespace test = infobot::test;

TEST_CASE("an armed allocation throws once and then allocates again") {
    {
        const test::Failure failure(0);
        CHECK_THROWS_AS((void)std::make_unique<int>(1), std::bad_alloc);
        CHECK(test::Failure::fired());
        CHECK_NOTHROW((void)std::make_unique<int>(2));
    }
    CHECK_NOTHROW((void)std::make_unique<int>(3));
}

TEST_CASE("the armed allocation is the one counted to") {
    const test::Failure failure(1);
    const auto first = std::make_unique<int>(1);
    CHECK_FALSE(test::Failure::fired());
    CHECK_THROWS_AS((void)std::make_unique<int>(2), std::bad_alloc);
    CHECK(test::Failure::fired());
}

TEST_CASE("a nothrow allocation returns null instead of throwing") {
    const test::Failure failure(0);
    const std::unique_ptr<int> got(new (std::nothrow) int(1));
    CHECK(got == nullptr);
    CHECK(test::Failure::fired());
}

TEST_CASE("a sweep fails each allocation of its body in turn") {
    std::size_t runs = 0;
    const test::Sweep sweep = test::fail_each_allocation(
        [] {
            const auto a = std::make_unique<int>(1);
            const auto b = std::make_unique<int>(2);
            const auto c = std::make_unique<int>(3);
        },
        [&runs](std::size_t run) {
            CHECK(run == runs);
            ++runs;
        });
    CHECK(sweep.thrown == 3);
    CHECK(sweep.absorbed == 0);
    CHECK(sweep.completed);
    CHECK(runs == 4);
}

TEST_CASE("a sweep counts a failure the body absorbs") {
    const test::Sweep sweep = test::fail_each_allocation(
        [] { const std::unique_ptr<int> got(new (std::nothrow) int(1)); },
        [](std::size_t /*run*/) {});
    CHECK(sweep.thrown == 0);
    CHECK(sweep.absorbed == 1);
    CHECK(sweep.completed);
}
