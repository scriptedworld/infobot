# An optimised build removes the allocation a test arms

The C++ suite, deleted with `cxx/` and last at `e1e99b1`, had an allocator that
failed one named allocation, so a test could take the exception edges gcov
counts. Its own tests asked for the next allocation to fail and then made one:

    const test::Failure failure(0);
    CHECK_THROWS_AS((void)std::make_unique<int>(1), std::bad_alloc);

That passes under the gate and fails in a clone built Release. The standard lets
a compiler remove an allocation whose lifetime it can see, and at -O2 GCC 14.2
removes this one: nothing reaches the armed failure, the arming survives the
test case, and the failure goes off later inside doctest instead. Three cases
failed and the report named the wrong one.

## What made it invisible

The gate builds instrumented at -O0, because that is what gcov reads. So the one
build the gate judges is the one build where the elision does not happen. Every
check was green: the suite, the coverage minima, clang-tidy, cppcheck,
include-what-you-use.

What found it was building a fresh clone to check a task's own done condition,
that a clone builds with nothing installed. The build was Release because that
is what a person building this by hand would type.

## What to do

**Allocate through a function in another translation unit** when a test is about
allocation itself. The call is then opaque and no optimiser can elide it:

    void one_allocation() {
        constexpr std::size_t size = 32;
        void* block = ::operator new(size);
        ::operator delete(block, size);
    }

**Run the suite in an optimised build as well as the instrumented one.**
The `cxx-clang` recipe built Release with Clang, so the second run varied the
optimisation level as well as the compiler. Both of those are the sort of
difference that decides whether a test is measuring what it says.

## What it generalises to

A suite that only ever runs in one configuration is evidence about that
configuration. Anything the compiler is permitted to do differently, elision of
allocations, of copies, of a read the optimiser can prove is unused, is a place
where a passing test can be reporting on a program the user never runs.
