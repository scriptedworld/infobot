# A second implementation is held to byte parity, and is not deployed

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

**And nothing gates it.** Every checker `just checks` runs selects by Go
extension, so the Zig tree is read by none of them, exactly as the two shell
shims are. That is `clank/tasks/infobot/gate/10-the-shims-are-read-by-something`
widened: what is unread is now 12 Zig files as well as 77 lines of shell.
`zig build test` runs its own unit tests and no checker runs `zig build test`.

So deploying it would trade a gated implementation for an ungated one, to gain
about 1.7ms on a render dominated by a file search both of them share.

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
