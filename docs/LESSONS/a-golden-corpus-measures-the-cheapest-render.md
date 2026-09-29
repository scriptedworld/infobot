# A golden corpus measures the cheapest render

The golden corpus the port was checked against leaves out two things by
construction, and both are stated in `capture-golden.py`'s header.

No case carries `resets_at`. The countdown is computed from the wall clock, so
a case carrying one drifts by a minute and stops being golden.

No case has a cost segment. The session id names a transcript whose totals grow
as the session runs, so the corpus uses an id no transcript answers to.

Both omissions are right for a byte-for-byte oracle. They also make every case
the cheapest render there is: no countdown arithmetic, and none of the
transcript search and tailing that the cost segment does, which is the most
expensive thing a real render does.

## What it costs to forget

Timing the binary against the corpus measures a render with its file-reading
segment left out. The render-alone figure in `docs/PROJECT.md` is that
measurement, and the real-event figure beside it is about three times larger
because it adds a pane width and a live session id.
`docs/LESSONS/the-port-removed-the-interpreter-not-the-file-search.md` is what
the difference turned out to be.

## What to do

Before quoting a timing from a test corpus, say what the corpus leaves out and
whether the thing being measured lives there. A corpus built for determinism
removes exactly the parts that vary, and in a program that reads files, those
are often the parts that cost.
