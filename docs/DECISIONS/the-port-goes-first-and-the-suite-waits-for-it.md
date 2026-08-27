# The port goes first, and the suite waits for it

Decided 2026-08-26. It reverses a deferral that was recorded, reasoned and
correct when it was made, so what follows is why the ground moved rather than
why the earlier call was wrong.

## What was decided before

Go was settled as the language. The timing was not, and the recommendation in
`clank/tasks/infobot/status-line/05-*` was to defer: two of the four arguments
for porting had not survived checking, the gate read the Python cleanly once the
render moved out of the extensionless entry point, and the display was still
changing shape most days. Porting a moving target means porting twice.

## What changed

**The reason for deferring expired, and it is measurable.** FACT 2026-08-26: the
display last changed shape on 23 August. Six commits that evening brought the
gauges, the pace colour, the fade and the cost segment. Since then there is
documentation on the 24th and two commits on the 26th that changed behaviour
BEHIND the display. The proof is that the golden corpus renders byte-identical
at the width it was captured at:

    python3 .ephemera/check-golden-drift.py 197
    → identical 14, bar only 0, REGRESSED 0

**The port's oracle appeared, incidentally.** That morning the port had 14
requirements and 18 corpus cases covering one pane width, with no cost segment,
no countdown and no state file in any of them. By evening it had 115
requirements written out of the code and stated as observable properties, which
is what makes them language-neutral, plus about 200 assertions across
`.ephemera/check-*.py` whose cases and expected values port even though their
harness does not. Including the two areas most likely to be ported wrong:
transcript tailing, where two obvious implementations were both wrong by about
half, and the pricing arithmetic.

**The cost of waiting was about to spike.** Four `.ready` tasks would have
written a Python suite for 115 requirements, immediately before a port that
throws a suite away. That is the largest waste that was available, and it was
the very next thing anyone would have picked up. It is a much bigger number than
the three interpreter-only requirements that first prompted the re-examination.

## The argument that started it, which is the smallest of the three

Being Python costs requirements, not only milliseconds. FR-1.12 and FR-1.12a
exist for no reason other than the runtime: standard library only so it runs
under whatever `python3` resolves to, and a 3.8 floor set by one keyword. A
compiled binary deletes both rather than satisfying them.

It reaches further. FR-1.11d hand-emits canonical YAML and justifies the
duplication by not gambling on whether three third-party modules resolve; wrench
ships a Go pack, so a Go infobot links a library and the duplication dissolves,
which also retires FR-1.11n's two-repository contract.

## What the port does NOT fix, and why FR-1.13 exists

Go relocates the hazard rather than retiring it. An import that may not resolve
becomes a build that may not have run or a symlink that may dangle, and both
fail identically: a blank line nobody is told about.

FACT 2026-08-26: `~/bin/bolt` dangles on this machine, which is why infobot's own
gate is run by invoking its checkers directly rather than through `bolt`. The
failure mode is not hypothetical and it is in arm's reach.

FR-1.13 is the requirement that trade creates, and task 05 builds the answer to
it before any behaviour is ported.

## The shape that follows from it

Leaf-first, because an ordinal is how dependency is carried in the task tree:
pricing and usage depend on nothing, context needs usage, the render needs all
three. One file per task with its tests beside it, and the Python renders until
the last task swaps the symlink.

## What would overturn this

The display starting to move again. That was the whole reason for the deferral,
it is the one input that has actually changed, and if it changes back the old
reasoning returns intact rather than needing to be rebuilt.
