# What cxx is built from

**RETIRED 2026-09-25, with the C++ rebuild.** `cxx/` is deleted, last at
`e1e99b1`, and `go-stays-deployed-while-cpp-clears-its-gate.md` says why. The
library measurements below are kept for anyone choosing C++ dependencies
again.

---

The C++ rebuild in `cxx/` takes a maintained library where one meets the
requirement, and plain standard or POSIX C++ where none does. Nothing is
translated from the Go implementation; `REQUIREMENTS.md` is the specification
and Go is only the check that real renders come out the same. Every library is
reached through CMake FetchContent pinned to a commit and linked static.

Each choice below was measured on an i7-14700KF pinned to one P-core with
`hyperfine -N`, GCC 14.2 at -O2. The probes are in clank
`tasks/infobot/cpp-clean/20-a-library-for-each-concern`, `evidence/`.

## JSON and YAML: wrench's pack, and simdjson for the transcripts

Two parsers, deliberately. Everything that is a document goes through wrench's
C++ pack (clank `tasks/wrench/library/cpp/`): the payload, the rate table, the
palette and layout config, herdr's reply, and the state file both written and
validated. The transcript lines do not.

The pack binds jsoncons, and it is built to be right rather than quick. On the
same corpus, 33 files and 61,317,795 bytes summed to identical totals, jsoncons
takes 259.3 ms where simdjson takes 42.5 ms (measured by wrench; infobot's own
earlier reading of simdjson on that corpus was 33.3 ms, against glaze at 35.8
and nlohmann/json at 140.8). A first render reads every transcript the session
has, so that difference is paid at the start of every session, and later renders
read only what was appended (FR-8.6).

So the transcript scan reads with simdjson directly, pinned like every other
dependency. It costs 177,872 bytes of text, 18.6% of a statically linked binary.
FR-8.21 keeps a record whose usage field is not a number, which reading a field
at a time does naturally.

**The cost of two parsers is that nobody holds them level**, which is the reason
wrench declines to own the second one. What limits it here is where each is
used: no file infobot writes, and no document another program reads, goes
through simdjson.

## Display width: utf8proc

`utf8proc_charwidth` from utf8proc 2.11.0 (commit `d7bf128`, Unicode 17.0),
used as the library gives it. It measures a character as a terminal draws it
(FR-3.9) and counts ambiguous-width characters as one (FR-3.10). It is static
data, 345KB of object code, with no cost at startup.

ICU 76 was measured and declined. It adds 0.37 ms to every process start,
against a render that starts in under a millisecond, and brings a 31MB data
library.

## The host query: posix_spawn and a pidfd

No library. The width query spawns the host with `posix_spawn`, then waits in
one `poll` over the output pipe and a pidfd for the child: two seconds for the
host, then a quarter second for the pipe once it has exited (FR-3.11). An answer
counts when the host exited 0 and the pipe closed.

    host                  answers    hangs     child holds the pipe
    posix_spawn + pidfd   0.74 ms    2.0 s     0.25 s, unknown
    reproc 14.2.5         31.3 ms    30 s      30 s

reproc's deadline did not bound the drain of a hung host, so it fails FR-3.11
as well as costing 30 ms on a host that answers.

## The written stamp: localtime_r and strftime

`written` is local time with its offset (FR-1.11g). `std::chrono::current_zone`
parses the time zone database on first use, 1.9 ms per process. `localtime_r`
and `strftime("%FT%T%z")` with the colon inserted give the same stamp in the
time the process takes to start.

## Finding transcripts: std::filesystem

A `directory_iterator` over the project directories finds the session's
transcript among 201 entries in about half a millisecond after process start.
FR-8.2 to FR-8.5 need a match on a file name, not a glob language.

## What this leaves open

The wrench pack's own JSON and YAML libraries are wrench's choice, and the
rebuild's modules that read JSON wait for the pack to exist. The modules that
do not (width, host, pricing arithmetic, rows) can be built first.
