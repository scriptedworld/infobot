# What is not done

## Queued

The work is in `clank/tasks/infobot/`:

    status-line/05  rewrite in Go        .questions -- the language is my call
    status-line/10  write the test suite .ready
    status-line/20  a failing segment costs one segment, not the row (FR-1.4)
    session-cost/10 what the session would have cost  .complete

Task 10 was drafted against the Python and still says `# COVERS:`. If the port
happens it becomes `//`; if it does not, it is already right.

## The decision waiting on me

**Go or stay on Python.** Written up in full in `status-line/05`. Short form: two
of the four arguments for the port did not survive checking, the gate now reads
the Python cleanly, and the display is still changing shape most days. The
recommendation is to defer rather than abandon.

## Not queued, and waiting on nothing but a decision

**No tests.** FR-4.1. The oracle exists: 18 captured cases in
`clank/tasks/infobot/status-line/05-*/golden/`, and `test-plan.md` beside it
lists the properties each requirement wants asserted.

**No remote.** The repository is local, `clone = false` in the roster. Creating
`github.com/scriptedworld/infobot` is not something a session does on its own.

**The Python jig is not adopted.** `bolt.common-quality.yaml` and
`bolt.secrets.yaml` are linked and their checks pass; the nine Python checks in
`bolt.python-std-quality.yaml` are not. Expect ruff, mypy, pylint, vulture and
interrogate to have opinions the first time they run.

**bolt itself is being rewritten**, so the gate is run by invoking the checkers
directly rather than through `bolt -c bolt.common-quality.yaml`. FACT
2026-08-23: `~/.projects/bolt` tracks three files and no Go source, and
`~/bin/bolt` dangles.

## Known and deliberately unfixed

FR-1.4: `main()` wraps the whole render in a bare `except`, so one bad field
blanks both rows rather than one segment. `golden/malformed-resets.txt` captures
that behaviour, so task 20 fixing it shows up as a deliberate diff.

The pricing table has no staleness signal. It carries `taken`, nothing reads it,
and a figure priced from three week old rates looks exactly like a correct one.
A marker on the cost segment is the intended fix, beside the plus that already
means "a model here has no rate".

## Open questions

- Should the identity row and the meter row be one row? They were split when the
  meter row arrived, and the bar now fills row one, so the reason may have
  expired. Two rows is settled against three, which is a different question.
- The countdown is uncoloured. It wants its own scale, probably inverted: a
  reset getting closer is good news, opposite to consumption.
- PREFERENCE 2026-08-23, expect this to move: `PACE_CONFIDENT` at 0.6 and the
  squared fade were tuned by eye against one evening's numbers. Whether the
  middle of a window feels too quiet is a question only normal use answers.
