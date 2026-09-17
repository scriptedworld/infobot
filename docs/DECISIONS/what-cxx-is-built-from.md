# What cxx is built from

The C++ rebuild in `cxx/` takes a maintained library where one meets the
requirement, and plain standard or POSIX C++ where none does. Nothing is
translated from the Go implementation; `REQUIREMENTS.md` is the specification
and Go is only the check that real renders come out the same. Every library is
reached through CMake FetchContent pinned to a commit and linked static.

Each choice below was measured on an i7-14700KF pinned to one P-core with
`hyperfine -N`, GCC 14.2 at -O2. The probes are in clank
`tasks/infobot/cpp-clean/20-a-library-for-each-concern`, `evidence/`.

## JSON and YAML: wrench's C++ pack, and nothing else

The payload, the rate table, the palette and layout config, the transcript
lines and the state file all go through wrench's pack (clank
`tasks/wrench/library/cpp/`). infobot links no JSON or YAML library of its own.

The transcript scan is the one hot path, so its cost was measured for wrench's
library choice: 61MB of transcripts, 7,735 usage records summed by model, three
libraries producing identical totals.

    simdjson 4.6.11 on-demand   33.3 ms
    glaze 8.4.0                 35.8 ms
    nlohmann/json 3.11.3       140.8 ms

A first render pays this once per session; later renders read only what was
appended (FR-8.6). FR-8.21 keeps a record whose usage field is not a number,
which field-at-a-time access does naturally and decoding into fixed structs
does not.

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
