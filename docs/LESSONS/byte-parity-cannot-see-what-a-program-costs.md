# Byte parity cannot see what a program costs

2026-08-30. A second implementation was checked against the first by comparing
what it printed, in four harnesses and 547 cases, all identical. It used thirty
times the memory and five times the CPU, and nothing in the acceptance test
could have said so.

## What was built and how it was checked

A Zig implementation of the status line, held to the Go one's output:

    18 of 18    the golden corpus, which predates both
    432 of 432  renders across nine widths and sixteen percentages, in colour
    67 of 67    control points through the published state file
    30 of 30    the state file, tmux, the rate table, the offsets round trip
    5 of 5      the cleanup binary, refusals included

Every one passed. The implementations agree on every byte either produces.

## What that missed

FACT 2026-08-30, a 6.3MB transcript, `poop` and `hyperfine -N` under `taskset`:

    wall time     go 2.8 ms       zig 13.2 ms      4.7x
    instructions  go 3.08 M       zig 58.0 M      19x
    peak RSS      go 8.9-13.6 MB  zig 26-371 MB   up to 30x

Nineteen times the instructions for identical output. The cause was ordinary
and local: an arena that never frees, holding every line parsed out of a file
read whole into memory.

## Why no harness caught it, which is the point

**A parity harness compares what a program PRODUCES.** Cost is not in the
output. Two implementations can agree to the byte and differ by an order of
magnitude in what producing the bytes took, and no amount of comparing output
closes that gap. It is not a gap in the harness; it is the wrong axis.

The Go implementation had a budget written down, in `docs/PROJECT.md`: a render
is paid on every Claude Code event, so its cost is the whole reason the port
happened. **That budget was never a requirement, so it was never inherited.** A
second implementation was written against the observable behaviour, which was
recorded, and not against the constraint, which was prose in a file about
something else.

## The measurement was wrong three times first

Worth its own space, because the cost figures were published twice before they
were right.

**A bash loop over `date +%s%N`** timed its own subprocesses and reported 7.5ms
against 5.8ms. `hyperfine -N` said 4.0 against 2.3, and pinned with `taskset`
said 2.8 against 13.2.

**A Python harness measured Python.** `os.fork` hands the child the
interpreter's address space and `ru_maxrss` counts from process creation, so it
reported 11,716 KB for both implementations and both payloads. Four figures
identical to the kilobyte is the tell. Python's own RSS was 13,628 KB.

**And the payload measured nothing.** It carried no `rate_limits`, so the render
had one row, so `alignCost` had no second row to place a cost segment on, so
`costForms` never ran and NEITHER BINARY READ A TRANSCRIPT. Both were timed
doing no work, and the numbers looked plausible enough to publish. That one
error produced both a reversed memory figure and a reversed timing figure, and
the timing half survived a round of corrections because fixing the memory
measurement did not prompt re-checking the clock.

## What to do

**Write the resource budget as a requirement, not as prose.** If a program has
a cost constraint, a reimplementation cannot inherit it from a paragraph in a
document about something else. `docs/SPEC.md` is where this one belongs and it
does not exist yet.

**Measure a workload that does the work.** Before trusting a comparison, check
that the expensive path ran: an offsets file written, a cost segment printed, a
counter that moved. A figure that does not change when the input changes by six
megabytes is measuring something else.

**Use the instrument the machine already has.** `hyperfine -N` with `taskset`,
and `poop` where hardware counters are permitted, which give instruction counts
and cache misses that wall time hides. A hand-rolled loop measures the loop.

**And keep the parity harness.** It found two real defects the day it was
written, and it remains the right check for the axis it covers. The mistake was
reading "identical output" as "equivalent implementation".
