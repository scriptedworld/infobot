# infobot in Zig

A second implementation of the status line, byte-identical to the Go one.

**This is an experiment, not the deployed program.** `bin/infobot` still execs
the Go build, and nothing here is wired into Claude Code. Read
`../docs/DECISIONS/a-second-implementation-is-held-to-byte-parity.md` before
changing that.

## Build and check

    zig build                  both binaries, ReleaseSafe
    zig build test             the unit tests
    zig build -Doptimize=Debug when a stack trace is wanted

Zig 0.16.0. It is not on PATH here and is not registered globally; the harnesses
reach it with `mise exec zig@0.16.0 --`.

## What it is measured against

Parity is the whole point, so it is checked three ways and none of them is this
tree's own opinion:

    ../.ephemera/zig-parity.py <corpus>    the 18-case golden corpus
    ../.ephemera/zig-parity-widths.sh      432 renders across width and load
    ../.ephemera/zig-perf.sh               what a real event costs

All three build both binaries before comparing. That is not tidiness: a stale
executable against a fresh one reads as a porting bug, and it cost one run here
when a rounding fix was compiled into the tests and not into the binary.

**The corpus is the better oracle and the sweep is the wider one.** The corpus
was captured from the pre-port Python and the Go port was checked against it, so
agreeing with it agrees with all three implementations. It cannot carry a
countdown or a cost, though, because both move while it is being read, and it
renders with no host so every case is full width. The sweep covers what it
cannot: nine pane widths, sixteen percentages, three gauge loads, in colour.

Last run 2026-08-30: 18 of 18, 432 of 432, and the two `forget` binaries agree
on five session ids including the three that must be refused.

## What the port had to preserve, and what it cost

**Rounding, twice.** `num.round` breaks a tie to the even neighbour, because
Python's `round()` does and the Go port kept it: the bar's filled-cell count is
`round(pct/100*cells)` and every colour channel is rounded out of an
interpolation, so a tie is reachable on both.

`num.fixed` is the second half and it was found by measurement rather than
foresight. Zig's `{d:.N}` rounds a tie AWAY from zero, and rounds the shortest
round-trip representation rather than the exact value, so a percentage of 12.5
printed `13% consumed` against Go's `12%`. It was 27 of 432 cases in the sweep
and nothing else.

**The character-width table is generated, not written.** `src/eaw.zig` comes out
of `../.ephemera/eawgen`, which walks every code point and asks the SAME
`runeWidth` the Go renderer uses. A table written by hand from the Unicode data
would agree almost everywhere, and "almost" is a bar one cell wrong on a payload
nobody tried. Regenerate it when `golang.org/x/text` moves.

**The subprocess timeout is on the read, not the process**, which is a
simplification the Go implementation could not reach. Go needed `WaitDelay`
because killing a host script leaves a grandchild holding the inherited stdout
pipe, so a two second timeout waited thirty against `sleep 30`. Polling the pipe
with a deadline sidesteps it: when the deadline passes this stops reading, and
whether the grandchild ever closes the pipe stops being the program's problem.

## What it costs to run

**Faster and much hungrier.** Measured 2026-08-30, warm offsets, a 6.3MB
transcript, `hyperfine -N` for time and a small `wait4` supervisor for memory:

    time     go  4.0 ms ± 1.3    [user 1.0, system 3.2]
             zig 2.3 ms ± 1.0    [user 1.1, system 1.1]

    peak RSS      cold        warm
    go       11.6-13.6 MB   8.9-9.1 MB
    zig      368-371 MB     25.8-27.1 MB

**The time is not the interesting half.** User time is the same to within noise,
1.0 against 1.1 ms, so the render costs what it costs in either language. The
whole gap is system time, which is Go's runtime setting itself up in the kernel.

**THE MEMORY IS A DEFECT IN THIS TREE, not a property of Zig**, and it is
`clank/tasks/infobot/zig-memory/10-*`. Three faults compound: `scan` allocates
the whole delta as one slice, so a cold read holds the entire transcript;
`originOf` takes a 4MB buffer per call and is called once per transcript in the
pool; and every parsed line goes into an arena that never frees. One arena for
one render was a reasonable choice for a program that draws a line, and it is
the wrong one for a program that parses six megabytes on the way there.

**Two earlier figures here were wrong and are worth knowing about.** They said
7.5 and 5.8 ms, from a bash loop timing its own subprocesses, and 1.2MB against
6.8MB of RSS, which was the reverse of the truth. The memory number came from a
payload carrying no `rate_limits`: with one row there is no second row for
`alignCost` to place a cost segment on, so the transcript was never read and
both binaries were measured doing no work. **Byte parity cannot catch that.**
The output was identical while one implementation used thirty times the memory.

**The build mode matters more than the language for size.** Debug was 17.7ms and
a 17.1MB binary, so it is not a slower build of the same program. ReleaseSafe is
the default and ReleaseFast bought nothing measurable. ReleaseSmall is 275KB,
one fourteenth of ReleaseSafe, for about 0.3ms.

## What reads this tree, which is less than reads the Go one

Measured 2026-08-30 rather than assumed, because assuming it was wrong once:

    lizard          READS IT, and failed the gate on 11 findings the first time
    detect-secrets  READS IT; it scans `git ls-files` and is language-blind
    traceability    does not; it globs *.go
    suppressions    does not; it globs *.go
    go-std-quality  reads none of it, so no build, vet, format, lint or coverage

`zig build test` is the only thing that runs these unit tests, and nothing in
the gate runs `zig build test`. Run it yourself.

## Layout

    src/ctx.zig       what every part needs: allocator, Io, environment
    src/num.zig       the rounding, both halves
    src/payload.zig   the session JSON, every field optional
    src/eaw.zig       generated character widths
    src/width.zig     visible columns, and asking a host how wide the pane is
    src/palette.zig   the colours and the two ramps
    src/segments.zig  the bar, the rail, the individual segments
    src/rows.zig      composing and fitting the two rows
    src/state.zig     the published `.status.yaml`, which must match byte for byte
    src/usage.zig     tailing the transcripts, and the offsets file
    src/pricing.zig   the rate table and the counterfactual cost
    src/forget.zig    what SessionEnd removes
    src/main.zig, src/forget_main.zig   the two entry points

One arena per process, taken in the entry point. The program renders one line
and exits, so nothing frees and the whole allocation goes back at once. That is
also why the tests that touch these helpers run on an arena: threading ownership
through them to satisfy a leak checker would be ceremony no real caller performs.
