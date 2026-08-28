# What is not done

## The port landed

Go, 2026-08-28. The Python is gone, `bin/infobot` is a committed shim that execs
the built binary, and a render went from 30.6ms to 4.5ms.

What checked it, because a rewrite is only as good as what it was held against:

    golden corpus     17 of 18 identical, at the width it was captured
    parity probes     the state file, the countdown and the cost, Python to Go
    the wrench fixture  byte for byte, from both ends
    go test           78 of 107 requirements cited by a test that asserts them

The eighteenth corpus case is `malformed-resets` and it differs on purpose. The
Python wrapped the whole render in a bare `except`, so one unreadable field
blanked both rows; that was FR-1.4's known gap, captured deliberately so fixing
it would show as a diff. It does.

## Queued

    clank/tasks/infobot/status-line/   05 through 20, all superseded by the
                                       single pass that landed the port
    clank/tasks/infobot/state-readers/ 10  say what a session is DOING  .questions

**The `status-line` tasks want closing out rather than doing.** They were
written as a leaf-first sequence, one file per task, and the work was done in
one pass instead. Each needs its `.complete` rename and the SHA that did it, or
`.cancelled` with the reason, which is that the sequence was mis-sized rather
than wrong.

## Not done, and honest about it

**29 settled requirements carry no test.** `python3 bin/test-traceability.py
--requirements REQUIREMENTS.md .` names them. They are mostly the row-assembly
rules in section 5 and the payload-reading rules in section 2, which are
reachable through `render.Build` with a fixed width and a fixed clock and want
no fixture at all. FR-4.1 closes when the gate does.

**The Go jig is not adopted.** `bolt.go-std-quality.yaml` is the one that
matters, because it judges coverage per file at 80% through an adapter that
exists. `make cover` applies that bar by hand meanwhile, and every package
clears it: payload 100, state 94, render 91, num 89, pricing 88, forget 86,
usage 82. The two entry points are 100 when measured the way the rule requires,
built with `go build -cover` and run rather than excluded.

**No remote.** The repository is local, `clone = false` in the roster. Creating
`github.com/scriptedworld/infobot` is not something a session does on its own.

**`PACE_CONFIDENT` at 0.6 and the squared fade** were tuned by eye against one
evening's numbers. Expect them to move; only normal use answers whether the
middle of a window feels too quiet.

## Open questions

Section 4 of `REQUIREMENTS.md` holds them, each with an id so closing one is a
change to a row. What is left: the suite, one row against two, colouring the
countdown, a staleness marker on the cost, the parameter budget, and whether the
state file should say what a session is DOING rather than only what it spent.

That last one is filed as a task in `clank/tasks/infobot/state-readers/`, in
`.questions`, because the first question is whether it is infobot's work at all.
infobot is a formatter: one payload, one session, no view of a board.

## Two things that constrain changes to the state file

**It has a reader outside this repository.** silo's coordination board pulls
`context_percent` and `cwd` out of every session's file with patterns anchored
on the quoted key and the single space after the colon, and takes the number
bare. FR-1.11o makes announcing a change to that form a requirement rather than
a courtesy. Adding a key is safe; changing the shape is not.

**Nothing pins it against wrench any more.** FR-1.11n used to make a form change
a two-repository change, and it retired on the measurement that wrench's Go pack
emits the same bytes. The port did not then link that pack, so there are still
two emitters and nothing holding them together. Linking it is the open half.
