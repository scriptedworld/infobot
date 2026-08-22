# What is not done

## Queued

The work is in `clank/tasks/sigmund/status-line/`, ordered by its ordinals:

    05  rewrite in Go, with the golden corpus beside it as the oracle
    10  write the test suite, which 05 makes possible
    20  a failing segment should cost one segment, not the whole line (FR-1.4)

Task 10 was written against the Python and still says `# COVERS:`. Correcting it
is in 05's acceptance.

## Not queued, and waiting on nothing but a decision

**The gate is not adopted.** No `bolt.*.yaml` is linked here, so nothing reads
this repository at all. The Go jig is the one worth having and it wants a Go
module to read, so this follows 05 rather than leading it.

**No remote.** The repository is local. Every other project on the roster is at
`github.com/scriptedworld`, and creating that is not something a session does on
its own.

## Known and deliberately unfixed

`bin/sigmund` renders two rows and holds every requirement in
`REQUIREMENTS.md` except FR-1.4, which it states about itself and does not hold:
`main()` wraps the whole render in a bare `except`, so one bad field blanks both
rows rather than one segment. `golden/malformed-resets.txt` in the task tree
captures that behaviour, so when task 20 fixes it the diff is the evidence.

## Open questions

- Should the identity row and the meter row be one row? They were split when the
  meter row was added, and the bar now fills row one, so the reason may have
  expired.
- The countdown is uncoloured. It wants its own scale, probably inverted: a
  reset getting closer is good news, which runs opposite to consumption.
- Does the rate limit segment want token counts? It cannot have them. The
  payload carries a percentage and a reset time and nothing else, which is
  FR-2.2, so the question is whether to keep saying so.
