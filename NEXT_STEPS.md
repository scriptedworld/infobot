# What is not done

## Queued

The work is in `clank/tasks/infobot/`:

    status-line/05  the Go skeleton, the build, FR-1.13    .ready
    status-line/10  port pricing.py                        .ready
    status-line/12  port usage.py                          .ready
    status-line/14  port context.py and forget-session     .ready
    status-line/16  port render.py's plumbing, and FR-1.4  .ready
    status-line/18  port render.py's display               .ready
    status-line/20  swap the symlink, retire the Python    .ready
    session-cost/10 what the session would have cost       .complete

**The port is decided and started.** Ordinals run leaf-first because that is how
dependency is carried here: pricing and usage depend on nothing, context needs
usage, the render needs all three. Each task ports one file and writes its tests
beside it, in Go. No suite is written in Python.

Until 2026-08-26 those ordinals held four tasks that would have written that
suite in Python, for 112 requirements, immediately before a port that throws a
suite away. That is what tipped the decision, not speed.

## The decision that was waiting, and is not any more

**Go, and now.** Settled 2026-08-26. The full argument is in `status-line/05`,
kept beside what it overturned, because the answer only reads correctly next to
the deferral it replaced.

What changed, in the order it weighed:

**The reason for deferring expired, and it is measurable.** It was that the
display kept changing shape, so porting would mean porting twice. The display
last changed shape on 23 August: six commits that evening, then documentation,
then two commits on the 26th that changed behaviour BEHIND it. The golden corpus
rendering byte-identical at its captured width is the proof.

**The port's oracle appeared, by accident.** That morning it had 14 requirements
and 18 corpus cases covering one pane width, no cost, no countdown, no state
file. By evening it had 113 requirements stated as observable properties, which
is what makes them language-neutral, and about 200 assertions whose cases and
expected values are portable data. Including the two areas most likely to be
ported wrong: transcript tailing, where two obvious implementations were both
wrong by about half, and the pricing arithmetic.

**The cost of waiting was about to spike.** Four ready tasks would have written a
Python suite for 113 requirements, immediately before a port that throws a suite
away. That is far more waste than the three interpreter requirements, and it was
the very next thing anyone would have picked up.

**What the port does NOT do, which FR-1.13 exists to catch.** Go retires FR-1.12
and FR-1.12a and does not retire the class behind them. An import that may not
resolve becomes a build that may not have run or a symlink that may dangle, and
both fail identically, as a blank line nobody is told about. `~/bin/bolt` is the
worked example: the symlink was laid on 20 August, its target was absent through
26 August, and a binary appeared under it at 11:51 on the 27th. Nothing
announced either transition. That is a real trade, not a free win, and task 05
builds the answer to it before anything else.

If the display starts moving again, this is wrong and the old reasoning returns
intact.

## Not queued, and waiting on nothing but a decision

**No tests, and none are coming in Python.** FR-4.1. The suite lands one file at
a time as each port task does, in Go, which is the whole reason the port went
first. Three oracles feed it: 18 captured cases in
`clank/tasks/infobot/status-line/05-*/golden/`, the seams already in the code so
the clock and the transcript root are parameters rather than calls, and about
200 assertions across `.ephemera/check-*.py` whose cases and expected values
port even though their harness does not.

Those probes are gitignored working files. They are the only oracle for
everything the corpus cannot reach, which is the cost segment, the countdown and
the state file, all absent from all eighteen cases by construction. Each port
task promotes the ones it needs; task 20 accounts for whatever is left.

**No remote.** The repository is local, `clone = false` in the roster. Creating
`github.com/scriptedworld/infobot` is not something a session does on its own.

**The Python jig is not adopted.** `bolt.common-quality.yaml` and
`bolt.secrets.yaml` are linked and their checks pass; the nine Python checks in
`bolt.python-std-quality.yaml` are not. Expect ruff, mypy, pylint, vulture and
interrogate to have opinions the first time they run.

**The gate runs through bolt again**, and its command line changed. The jig is a
positional argument and flags come before it:

    bolt --output-dir .ephemera/<dir> common-quality .

**bolt exits 0 whenever the run completed**, whatever the tools concluded, so
the verdict is `success` in the `result.yaml` it names on the last line. The
summary line above it counts executions rather than failures: a run with one
failing check of three prints `failed: 3 execution(s)`. Read the artifact.

Invoking the three checkers directly still works and is the fallback while bolt
is being rebuilt.

## Known and deliberately unfixed

FR-1.4: `main()` wraps the whole render in a bare `except`, so one bad field
blanks both rows rather than one segment. `golden/malformed-resets.txt` captures
that behaviour, so task 20 fixing it shows up as a deliberate diff.

The golden corpus is bound to the pane it was captured in. Every case that gets
a width bakes those columns into its bar, so `check` is green in the 191-column
herdr pane it was last captured in and red in any other, and a wholesale DIFFERS
means "captured somewhere else" before it means a regression. Re-capture with
`capture-golden.py` when the pane changes, and read the individual diffs to be
sure it is only the bar that moved. `.ephemera/check-golden-drift.py 191` sorts
them into bar length, styling only and REGRESSED, which is the distinction that
matters: a pure colour change leaves the glyph count untouched, so a check that
strips glyphs files it under "the pane moved" and a reader stops looking.

Pinning the width would need a route that takes it from the environment, which
was weighed and declined: the case for it is the corpus, and the corpus is meant
to be replaced by the suite in tasks 16 and 18, which can pass a width to
`build()` and needs nothing from the environment at all.

## Open questions

Section 4 of `REQUIREMENTS.md` holds them, as FR-4.1 to FR-4.7, so each carries
an id a test or a decision can close. They are the suite, the language, the two
seams the suite needs, one row against two, colouring the countdown, and a
staleness marker on the cost.

One thing there is not a requirement and belongs here instead: `PACE_CONFIDENT`
at 0.6 and the squared fade were tuned by eye against one evening's numbers, and
expect them to move. Whether the middle of a window feels too quiet is a
question only normal use answers.
