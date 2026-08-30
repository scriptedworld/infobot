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

Measured 2026-08-30, 300 runs interleaved, a real event with a live session id
and warm offsets, on this machine:

    go     7.5 ms/run    3.9 MB
    zig    5.8 ms/run    4.4 MB

**Read that as "the same order", not as a win.** Both are dominated by the
transcript search the cost segment does, which `docs/PROJECT.md` measures at
about 15ms cold and which was ported as it stood into both. The gap moves by
more than its own size between runs on a machine carrying a dozen agent
sessions.

**The mode matters more than the language.** Debug was 17.7ms and a 17.1MB
binary, so it is not a slower build of the same program. ReleaseSafe is the
default here and ReleaseFast bought nothing measurable, 5.4ms against 5.8ms,
inside the run-to-run spread. ReleaseSmall is worth knowing about at 275KB, one
fourteenth of the ReleaseSafe binary, for a cost of about 0.3ms.

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
