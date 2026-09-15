# infobot in C++

A second implementation of the status line, built to measure what it costs in
speed and footprint against the Go one.

**Not deployed.** `bin/infobot` execs the Go binary and nothing here is wired
into Claude Code. The reasons are the retired Zig decision's, which still hold:
`../docs/DECISIONS/a-second-implementation-is-held-to-byte-parity.md`.

## Build and check

    just cpp-profile     fresh instrumented build, the suite, and the database
    just checks          both languages' gates, this tree's through cpp-std-quality

C++23 on g++ 14, CMake and Ninja. Three libraries, from Debian 13:

    simdjson 3.12.3    reads every JSON input; linked static
    jsoncons 1.3.2     validates the state file against its draft 2020-12 schema
    doctest 2.4.11     the suite

`compile_commands.json` at the repository root links into `cpp/build/`, because
the jig's lint and headers tasks read `-p .`.

## Parity with Go

Held to the Go binary's bytes, not to a description of them. The harnesses are
in `.ephemera/cpp/`, gitignored, and each builds both binaries before comparing:

    parity.py <golden-dir>    the 18-case golden corpus
    parity-widths.sh          432 renders across nine widths and 48 loads
    parity-cost.sh            every session under ~/.claude/projects, cold and warm

`cpp/src/width_table.cpp` is generated from the Go renderer's own width rule by
`.ephemera/cpp/widthgen`, and `TestCppWidthTableMatchesGo` in `internal/render`
checks it against Go for every code point.

## Where it can differ from Go, and why that is accepted

Parity is held on the inputs this program is really handed. Go's
`encoding/json` and simdjson disagree on inputs nothing hands it:

    input                             Go                     C++
    an integer past 64 bits           read as a float        the document is rejected
    invalid UTF-8, a lone \ud800      replaced with U+FFFD   the document is rejected
    a leading byte-order mark         rejected               accepted
    -0                                a float, prints -0     the integer 0, prints 0
    a struct key differing in case    matched                not matched

Measured 2026-09-14 with `.ephemera/cpp/probe`. The real-transcript harness is
the evidence that no input in use reaches them: 148 sessions, 0 differences.

## Coverage

Per file at 80% for lines and for branches, from gcov. The library is
instrumented and the test sources are not, which is the boundary `go test
-cover` draws: an instrumented doctest file reads about 40% of branches taken in
a passing suite, because each assertion expands to branches taken only on
failure. `CMakeLists.txt` carries the measurement.

## Not yet read by common-quality

toolbox's traceability checker knows Go, Python and Rust tests, and its
suppression register has no C++ suffix. The `// COVERS:` marks here are written
as the Go tests write them, but nothing checks them until toolbox reads C++.
Filed as `clank/inbox/toolbox/common-quality-does-not-read-cpp`.
