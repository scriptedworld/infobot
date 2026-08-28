# What is not done

## The port landed

Go, 2026-08-28. The Python is gone, `bin/infobot` is a committed shim that execs
the built binary, and a real render went from 40.4ms to 11.8ms. `docs/PROJECT.md`
carries the full figures and says why the render measured alone is a different
and larger number.

What checked it, because a rewrite is only as good as what it was held against:

    golden corpus     17 of 18 identical, at the width it was captured
    parity probes     the state file, the countdown and the cost, Python to Go
    the wrench fixture  byte for byte, from both ends
    go test           109 of 109 requirements cited by a test that asserts them

The first three predate the Go. They were captured from the Python, which had no
suite in this repository and was not therefore unchecked: the corpus was built
to be the port's oracle, and about 200 assertions in `.ephemera/check-*.py` went
with it. That harness was throwaway and is gone, and its cases are now in
`go test`.

The eighteenth corpus case is `malformed-resets` and it differs on purpose. The
Python wrapped the whole render in a bare `except`, so one unreadable field
blanked both rows; that was FR-1.4's known gap, captured deliberately so fixing
it would show as a diff. It does.

## Queued

    one-emitter/10   link wrench's pack so there is one    .cancelled
    state-readers/10 say what a session is DOING           .questions
    status-line/     05 through 20                         .complete

**Ask the task tree rather than this file.** Its state is a rename and a
document's state is prose, so where the two disagree the tree is right.

    ( setopt null_glob; print -l ~/.projects/clank/tasks/infobot/**/*.blocked )

## Not done, and honest about it

**The Go jig is not adopted.** `bolt.go-std-quality.yaml` is the one that
matters, because it judges coverage per file at 80% through an adapter that
exists. `make cover` applies that bar by hand meanwhile, and every package
clears it: payload 100, render 96, state 94, num 89, pricing 88, forget 86,
usage 82. The two entry points are 100 when measured the way the rule requires,
built with `go build -cover` and run rather than excluded.

**No remote.** The repository is local, `clone = false` in the roster. Creating
`github.com/scriptedworld/infobot` is not something a session does on its own.

**`PACE_CONFIDENT` at 0.6 and the squared fade** were tuned by eye against one
evening's numbers. Expect them to move; only normal use answers whether the
middle of a window feels too quiet.

## Open questions

Section 4 of `REQUIREMENTS.md` holds them, each with an id so closing one is a
change to a row. What is left: one row against two, colouring the countdown, a
staleness marker on the cost, the parameter budget, whether the state file
should say what a session is DOING rather than only what it spent, and which
kind of absence a missing context block is.

The suite is no longer among them. FR-4.1 retired on 2026-08-28 when the gate
started passing, because a question that is answered goes rather than standing
as a permanently satisfied assertion. What keeps it true is the gate.

That last one is filed as a task in `clank/tasks/infobot/state-readers/`, in
`.questions`, because the first question is whether it is infobot's work at all.
infobot is a formatter: one payload, one session, no view of a board.

## Two things that constrain changes to the state file

**It has a reader outside this repository.** silo's coordination board pulls
`context_percent` and `cwd` out of every session's file with patterns anchored
on the quoted key and the single space after the colon, and takes the number
bare. FR-1.11o makes announcing a change to that form a requirement rather than
a courtesy. Adding a key is safe; changing the shape is not.

**It is pinned against wrench by a fixture at each end, not by shared code.**
FR-1.11p, restoring what FR-1.11n gave. wrench declined the link on 2026-08-28,
with reasons rather than a preference: it cannot promise an import path while
its own `gate/05` is blocked, and `YAML.Encode` is not a surface it supports for
an outside consumer because its float behaviour is unspecified and disagrees
with its other two packs. Its recommendation was to keep the emitter that
already works, and that is what infobot does.

So there are two emitters permanently, and two tests that never meet.
**Editing `TestCanonicalForm` to make it pass is how the pin comes undone**,
which is the only way it can.

`clank/tasks/infobot/one-emitter/10-*.cancelled` carries the full answer.

**A number is never spelled with an exponent.** FR-1.11q. `1e+06` is a legal
spelling of a million and a reader matching `[0-9.]+` captures `1` from it, so
it is a silent wrong answer rather than a parse failure. infobot had that defect
and it is fixed; wrench found the same one across all three of its packs, where
four of six values diverge. The canonical spelling for the ecosystem is with our
user.
