// The suite's failing allocator, checked before any test relies on it.
//
// These cite no requirement: they test a tool the tests use, not the program.
#include "allocation.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <new>

namespace test = infobot::test;

TEST_CASE("an armed allocation throws once and then allocates again") {
    {
        const test::Failure failure(0);
        CHECK_THROWS_AS(test::one_allocation(), std::bad_alloc);
        CHECK(test::Failure::fired());
        CHECK_NOTHROW(test::one_allocation());
    }
    CHECK_NOTHROW(test::one_allocation());
}

TEST_CASE("the armed allocation is the one counted to") {
    const test::Failure failure(1);
    CHECK_NOTHROW(test::one_allocation());
    CHECK_FALSE(test::Failure::fired());
    CHECK_THROWS_AS(test::one_allocation(), std::bad_alloc);
    CHECK(test::Failure::fired());
}

TEST_CASE("a nothrow allocation returns null instead of throwing") {
    const test::Failure failure(0);
    void* got = test::one_nothrow_allocation();
    CHECK(got == nullptr);
    CHECK(test::Failure::fired());
    got = test::one_nothrow_allocation();
    CHECK(got != nullptr);
    ::operator delete(got);
}

TEST_CASE("a sweep fails each allocation of its body in turn") {
    std::size_t runs = 0;
    const test::Sweep sweep = test::fail_each_allocation(
        [] {
            test::one_allocation();
            test::one_allocation();
            test::one_allocation();
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
        [] {
            void* got = test::one_nothrow_allocation();
            ::operator delete(got);
        },
        [](std::size_t /*run*/) {});
    CHECK(sweep.thrown == 0);
    CHECK(sweep.absorbed == 1);
    CHECK(sweep.completed);
}
