# infobot in C++

The status line, rebuilt. `docs/SPEC.md` says what it does and what each module
owns; `docs/DECISIONS/what-cxx-is-built-from.md` says what it is built from.

**Not deployed.** `bin/infobot` execs the Go build. This tree is finished when
`just parity cxx` agrees with Go and its gate is green; whether it then replaces
Go is open in
`docs/DECISIONS/go-stays-deployed-while-cpp-clears-its-gate.md`.

## Where it starts

    cmd/statusline.cpp   main, one call: statusline::run(stdin, stdout)
    cmd/forget.cpp       main, one call: forget::run(stdin, stderr)

A render is one process per Claude Code event. `statusline::run` reads the
session payload, asks whoever owns the pane how wide it is, reads what the
session's transcripts appended since the last render, writes the state file, and
prints the rows. The modules that do each of those arrive task by task, in
`clank/tasks/infobot/cpp-clean/rebuild/`; today a render reads its input and
prints nothing.

## The tree

    src/       one module per concern, a header saying what it owns
    cmd/       the two entry points, one call each, no logic
    tests/     doctest, one file per module, plus the failing allocator
    build/     CMake's, gitignored

## Build and test

    just cxx-profile     configure, build instrumented, run the suite
    just cxx-clang       the same suite built and run with Clang
    just checks          the gate, both languages
    just parity cxx      rows and state files against the Go build

By hand, without the recipes:

    cmake -S cxx -B cxx/build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build cxx/build
    cxx/build/infobot-tests

Dependencies come through CMake FetchContent pinned to a commit and link static:
utf8proc for display width, doctest for the suite. A first configure fetches
them, so it needs the network once; `just` keeps them in `.cache/fetchcontent`
afterwards.

## What the suite does that a reader should know about

`tests/allocation.hpp` replaces the global `operator new` for the test binary
only, so a test can fail one allocation and take the exception edges gcov counts.
It is registered as S-3 in `SUPPRESSIONS`, with the question that was asked and
the answer given. The shipped binaries never link it.
