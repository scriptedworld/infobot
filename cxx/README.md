# infobot in C++

The status line, rebuilt. `docs/SPEC.md` says what it does and what each module
owns; `docs/DECISIONS/what-cxx-is-built-from.md` says what it is built from.

**Not deployed.** `bin/infobot` execs the Go build. This tree is finished when
`just parity cxx` agrees with Go and its gate is green; whether it then replaces
Go is open in
`docs/DECISIONS/go-stays-deployed-while-cpp-clears-its-gate.md`.

## Where it starts

`main` is in `cmd/`, never in `src/`. Each entry point is one call and no logic:

    cmd/statusline.cpp   main -> render::statusline(stdin, stdout)   src/render.cpp
    cmd/forget.cpp       main -> forget::run(stdin, stderr)          src/forget.cpp

A render is one process per Claude Code event. Finished, `render::statusline`
reads the session payload, asks whoever owns the pane how wide it is, reads what
the session's transcripts appended since the last render, writes the state file,
and prints the rows.

**Today it reads the payload and prints nothing.** The modules that turn a
payload into rows arrive task by task in
`clank/tasks/infobot/cpp-clean/rebuild/`, starting with `20`. The reading is
real: a writer is never left holding a pipe, and an allocation failing anywhere
in it still exits 0, which is FR-1.2 and is what `tests/entry_test.cpp` checks.

## The tree

    src/       one module per concern, a header saying what it owns
    cmd/       the two entry points, one call each, no logic
    tests/     doctest, one file per module, plus the failing allocator
    build/     CMake's, gitignored

A module's file is named for what it owns, and no two files in the tree share a
name. `src/render.cpp` is the status line; `cmd/statusline.cpp` is the binary
that calls it.

    src/render.cpp     the payload to the rows
    src/forget.cpp     the SessionEnd cleanup
    src/platform.cpp   what the program needs from the operating system

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
