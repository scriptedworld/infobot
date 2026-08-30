# A difference with two explanations is not a measurement yet

2026-08-30. I timed two binaries, got a clean 4.8ms gap with tight variance
across repeated runs, and had the wrong answer. The number was right. What I
would have concluded from it was not, and nothing in the number said so.

## What was being decided

The estate requires a component to validate a structure against its schema
before writing it, so it cannot emit what it would refuse to read back. infobot
runs once per Claude Code event, and a real event is 11.8ms, so the question was
what that rule costs here and whether it needs an exception.

silo asked for the number before either of us traded on it, which was the right
instinct and is not the lesson. The lesson is what the first number was worth.

## The first measurement, which was clean and insufficient

Two binaries writing the same nine-key file, one linking the schema library and
one not, 400 runs each, interleaved:

    nodep     2.36 MB    5.143 ms/run
    bare      4.70 MB   11.387 ms/run

Repeatable, and the gap held while the absolute figures moved by a third as the
machine got busier. Every property I ask of a measurement, and **two
explanations fit it exactly**:

    the binary is 2.34MB larger, so the process pages in more before main
    the library does work in its package init, whichever binary carries it

Those want opposite decisions. If it is size, any schema library costs this and
the rule is expensive here in principle. If it is init, it is one library's
choice, another might cost nothing, and the answer is about linkage rather than
about validating.

**I could not tell which from the number, and the number looked finished.** That
is the whole failure mode: a difference with tight variance reads as a result,
and its ambiguity is invisible because the ambiguity is not in the data.

## The control

A third binary, `padded`: no library at all, plus a blob of exactly the size the
library adds, referenced so the linker keeps it.

    nodep     2.36 MB    5.143 ms/run   no library
    padded    4.70 MB    5.177 ms/run   library-sized, no library
    bare      4.70 MB   11.387 ms/run   library linked, never called
    checked   4.71 MB   11.795 ms/run   linked, compiled and validated

`padded` costs what `nodep` costs. **The size is not the cost**, 0.034ms of it,
and the 6.21ms is the library initialising. A fourth binary then separates being
linked from being used: calling it adds 0.408ms on top.

Confirmed by a second instrument that agrees to two decimals:

    GODEBUG=inittrace=1 ./checked out.yaml
    init github.com/santhosh-tekuri/jsonschema/v6 @1.3 ms, 6.2 ms clock,
         1708792 bytes, 21285 allocs

## Why the right answer needed the control and not more runs

More runs of `nodep` against `bare` would have tightened a figure that was
already tight. Precision is not the axis the error was on.

The decision that followed turns entirely on which explanation was true. It came
out as **process lifetime**: a component running once per event enforces its
schema in its suite, and a long-lived one validates on write because it pays the
linkage once. That sentence is unavailable from the two-binary version. Had the
answer been size, the amendment would have been about hot paths, which is what
was drafted before the number arrived and would have been wrong.

## What to do

**Before running the comparison, write down every explanation that fits a
difference.** If more than one does, the comparison is not the experiment yet
and adding runs will not make it one.

**Build the control that separates them, and prefer one that isolates a single
variable you can set by hand.** A blob of a chosen size is cheap and it answers
size on its own. The instinct to skip it is strongest exactly when the first
result already agrees with what you expected.

**Then confirm with an instrument that works differently.** Timing and
`inittrace` share no machinery, so their agreeing is evidence in a way that two
timing runs agreeing is not.

**Cite the control when reporting.** Without `padded` in the table the result
reads as "the binary got bigger", and a reader has no way to see the conclusion
is unsupported. That is what makes it a measurement somebody else can act on
rather than a number they have to trust.

## Where it sits

The harness is `.ephemera/schema-cost/`, self-contained: `./run.sh 400` builds
what it times, blob included, and diffs the four outputs to prove they did the
same work. The decision it fed is silo's `c4ef97a`; the task is
`clank/tasks/infobot/published-form/10-*`.
