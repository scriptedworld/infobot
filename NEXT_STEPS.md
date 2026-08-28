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

    gate/10          the shims are read by something       .blocked
    jig-adoption/10  adopt the Go jig                      .planning
    one-emitter/10   link wrench's pack so there is one    .cancelled
    session-cost/10  show what the session would have cost .complete
    state-readers/10 say what a session is DOING           .questions
    status-line/     05 through 20                         .complete

**Ask the task tree rather than this file.** Its state is a rename and a
document's state is prose, so where the two disagree the tree is right.

    ( setopt null_glob; print -l ~/.projects/clank/tasks/infobot/**/*.blocked )

## Not done, and honest about it

**The Go jig is not adopted**, and it is
`clank/tasks/infobot/jig-adoption/10-adopt-the-go-jig.planning`.
`bolt.go-std-quality.yaml` is the one that matters, because it judges coverage
per file at 80% through an adapter that exists. `make cover` applies that bar by
hand meanwhile.

Every file clears it, which is a stronger statement than the per-package one
this section used to make. Statement-weighted from the profile, 2026-08-28:

    payload 100.0  palette 97.2  segments 97.1  build 96.7  state 94.0
    width    92.7  num     88.9  pricing 88.3  forget 85.7  usage  81.6

The two entry points are 100 when measured the way the rule requires, built with
`go build -cover` and run rather than excluded.

Coverage is therefore not what adoption is waiting on. `golangci-lint` under the
jig's config is: 123 issues, none of which may be settled with a pragma, so they
are decisions rather than edits. Running the rest of the jig's checks by hand
found one real thing, a file that was not gofmt-clean, fixed at `7de208a`.

**A fixture carries this machine's username, and removing it is a
two-repository change.** `internal/state/state_test.go` asserts
`"cwd": "/home/ancient/.projects/infobot"` twice, in `TestCanonicalForm`. It is
not a secret and it is untidy for a repository meant to be published.

It was left alone deliberately. FR-1.11p pins that form as a fixture at each
end, infobot's bytes here and the same bytes in wrench, and the comment above
the test says editing it to make it pass is how the pin comes undone. If wrench's
fixture holds the same literal string, changing it here silently unpins the two,
and FR-1.11o makes announcing a change to the form a requirement. So it is
wrench's session in the loop or it stays.

Worth doing in the same pass as the wrench YAML measurement above, since both
touch the same test and the same counterpart.

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

**Both of those reasons have since expired, so "permanently" was wrong.** Told
on 2026-08-28 that wrench's YAML is implemented and can be relied on, and the
surface is there: `github.com/scriptedworld/wrench` exposes
`yamlCodec.Encode(any) ([]byte, error)` in `yaml.go`, and `488e723 feat(gate):
wrench is green` retires the blocked `gate/05` that was the reason it could not
promise an import path.

**FACT 2026-08-28: the measurement is taken, and `Encode` reproduces the
canonical form exactly.** The probe is `.ephemera/wrench-parity`, a module with
a `replace` onto the local wrench, run with `go run .`:

    the whole TestCanonicalForm fixture   byte-identical

That covers quoted keys, one key to a line, the single space after the colon,
key ordering, `48.2` unrounded, `1000000` with no exponent, and `"` and `\`
escaped as infobot escapes them.

**The edges the fixture never reaches were checked too, and they are the ones
that mattered.** `context_percent` is the only float emitted, it is rounded to
one place, and the fixture's 48.2 never exercises a whole number. infobot's
`decimal()` formats with `'f'`, which never uses an exponent, then appends `.0`
where there is no decimal point, so a percentage landing on 100 stays a float
for whoever reads it back. wrench agrees on every one:

    0 -> 0.0        100 -> 100.0      7 -> 7.0        0.1 -> 0.1
    1000000 -> 1000000                10000000 -> 10000000
    1000000.0 -> 1000000.0            9007199254740992 unchanged

So the second objection is answered rather than assumed away. wrench's float
behaviour was called unspecified when it declined; on the values this form
actually carries it is specified and it agrees.

**That settles the measurement and not the decision.** Linking changes FR-1.11p
from two emitters pinned by two fixtures into one emitter with a conformance
check, which is a change to how the published form is produced and belongs to
wrench's session as much as this one. FR-1.11o makes announcing it an obligation.

**Editing `TestCanonicalForm` to make it pass is still how the pin comes
undone.** If the link is taken, that test stops being a second emitter's
assertion and becomes the conformance check on a linked one, which is a better
outcome than either option that was on the table. Until then it is load-bearing
exactly as it stands.

`clank/tasks/infobot/one-emitter/10-*.cancelled` carries the answer as it stood,
and its premise is what changed rather than its reasoning.

**A number is never spelled with an exponent.** FR-1.11q. `1e+06` is a legal
spelling of a million and a reader matching `[0-9.]+` captures `1` from it, so
it is a silent wrong answer rather than a parse failure. infobot had that defect
and it is fixed; wrench found the same one across all three of its packs, where
four of six values diverge. The canonical spelling for the ecosystem is with our
user.
