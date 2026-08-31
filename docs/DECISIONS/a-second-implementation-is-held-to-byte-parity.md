# A second implementation is held to byte parity, and is not deployed

**RETIRED 2026-08-31. The Zig tree is deleted and the parity check with it.**
Go remains the deployed and only implementation.

The out-of-date second implementation is at **`2c19f0b`**, the last commit
holding `zig/`:

    git show 2c19f0b:zig/README.md
    git checkout 2c19f0b -- zig/

The reasoning below is kept because it is why a second implementation was worth
building at all, and because the parity harness is what a third would want.

Why it went, on cost first. `docs/LESSONS/byte-parity-cannot-see-what-a-program-costs.md`
measured it with `poop` and `hyperfine -N` under `taskset` on a 6.3MB
transcript: Go 2.8ms against Zig 13.2ms, 3.08M instructions against 58.0M, and
peak RSS of 8.9-13.6MB against 26-371MB. Identical output, nineteen times the
instructions, and the cause was an arena that never freed.

**The cost and the ecosystem are the same reason, not two.** The Zig build was
hand-spun throughout because there was nothing to reach for: YAML emitted by
hand rather than by a library, and the rest written the same way. An arena that
never frees is what seat-of-the-pants allocation looks like when nobody has
already solved it for you.

So the 19x is not a fact about Zig the language. It is what a hand-rolled
implementation costs, and needing to hand-roll is the ecosystem argument
arriving as a number instead of an opinion.

**Both cost measurements in this repository's history were taken on the same
axis and only one was trustworthy.** A later run using a Python wrapper, ten
iterations, no CPU pinning, on a machine running eight other sessions, reported
Zig ahead by 2x. It was measuring scheduler noise. The instrument that answered
was the one that pinned the CPU and counted instructions.

---

2026-08-30. There is now a Zig implementation of the status line in `zig/`,
built on request. It renders the same two rows and writes the same
`.status.yaml`, and it is checked byte for byte against the Go one rather than
against a description of it.

## What is decided

**The Go build stays the deployed one.** `bin/infobot` execs `bin/statusline`,
which is Go, and nothing in `silo/settings.json` changes. The Zig tree is built
by hand and run by the parity harnesses.

**Parity is the acceptance test, and it is measured rather than asserted.**
Three harnesses, none of which is the Zig tree's own opinion:

    .ephemera/zig-parity.py <corpus>    18 golden cases
    .ephemera/zig-parity-widths.sh      432 renders across width and load
    .ephemera/zig-perf.sh               what a real event costs

The corpus is the better oracle: it was captured from the pre-port Python and
the Go port was checked against it, so agreeing with it agrees with all three.
The sweep covers what the corpus cannot carry, which is a countdown, a cost, and
any width but full.

**A difference is a defect in the Zig tree until shown otherwise.** Two were
found this way and both were real: a percentage of 12.5 printing `13%` against
Go's `12%`, and the transcript grouping falling back to filenames because a
reader left its delimiter in the stream. Neither would have been found by
reading the code.

## Why it is not deployed, which is the part worth writing down

**FR-1.11p pins the emitted form with a fixture at each end**, here and in
wrench. A third emitter is a third statement of one contract, and the reason the
pin exists is that agreement between implementations decays silently. The Zig
tree agrees today because it is checked today.

**FR-1.11o makes a change to the published form an announcement.** Swapping
which program writes it is such a change even when the bytes match, because what
readers depend on would then rest on a different set of measurements.

**And it is only half gated.** Two of the four common-quality checkers read it
and two do not, which was measured rather than assumed:

    lizard          READS IT. 382 functions, 6153 nloc, Zig included
    detect-secrets  READS IT. It scans `git ls-files`, which is language-blind
    traceability    does not. bin/test-traceability.py globs *.go
    suppressions    does not. bin/suppression-register.py:87 globs *.go

`go-std-quality` reads none of it, so there is no build, vet, format, lint or
coverage check over the Zig tree at all. `zig build test` runs its own unit
tests and nothing in the gate runs `zig build test`.

**The first draft of this file said nothing read it**, which was inherited from
`docs/PROJECT.md`'s measurement of the shell shims and generalised to a language
nobody had checked. lizard disproved it within the hour by failing the gate on
11 findings in this tree: four functions over the complexity limit, four over
the argument limit, and three over both. All eleven were fixed rather than
excluded, which is hard rule 4, and the fix is what the parity harnesses then
re-verified.

**And it is far hungrier, which byte parity was never going to show.** Measured
2026-08-30 against a 6.3MB transcript: 368MB peak on a cold read and 26MB warm,
against Go's 13MB and 9MB. The output was identical the whole time.

That is the strongest argument in this file and it arrived last. A parity
harness compares what a program PRODUCES, and two implementations agreeing to
the byte can differ by thirty times in what they cost to produce it. The defect
is this tree's own, in `clank/tasks/infobot/zig-memory/10-*`, and it is fixable;
the point that survives fixing it is that nothing in the acceptance test could
have raised it.

So deploying it would trade a fully gated implementation for a half gated one
that uses thirty times the memory, to gain 1.7ms of system time on a render
whose actual work costs the same in both.

## What it is good for

**It is a second witness to the emitted form**, which is worth something on its
own. `docs/LESSONS/implementations-of-one-version-are-one-witness.md` is about
three parsers agreeing because they shared an era rather than an argument; two
implementations written from the same source in different languages are a
weaker check than that, but the state file is now emitted by two programs that
agree on all 70 escape points and on the canonical bytes.

**It found things about the Go tree by asking questions of it.** The
character-width table is generated from the Go renderer's own `runeWidth`
rather than reimplemented, which is what makes the two agree by construction on
a table nobody wants to maintain twice.

## What would change this

A decision to move, which is not this one. It would need the gate to read Zig
first, and it would need announcing under FR-1.11o whether or not the bytes
moved.
