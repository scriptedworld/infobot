# An absent suite is not an absent oracle

Before the Go port, the Python status line had no test suite in this
repository: no test file, no test target, nothing the gate ran. Read quickly,
that says the port had nothing to be checked against.

It had. What it had was a golden corpus: 18 cases captured from the Python by
`capture-golden.py`, a script whose first line calls itself the oracle for the
port. Beside it were a drift checker, whose clean run at width 197 is what
showed the display had stopped moving, and about 200 assertions in throwaway
scripts covering transcript tailing and the pricing arithmetic.

The port was checked against that corpus and read 17 of 18 identical. The
eighteenth, `malformed-resets`, differs on purpose: the Python wrapped the
whole render in a bare `except`, so one bad field blanked both rows. That was
FR-1.4's known gap, captured so that fixing it would show up as a diff, and it
does.

## What to do

Before concluding that something was never validated, look for the oracle in
the shape the work left it: a corpus, a checker, a harness in a working
directory, a task's evidence. The corpus lives with the task that captured it,
`clank/tasks/infobot/status-line/05-rewrite-the-status-line-in-go.complete/`.

The throwaway harness is gone by design. Its cases and expected values moved
into the Go tests, which was the point of capturing them.
