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

**wrench re-measured with its own probe and all eight reproduce, and it supplied
the two things my probe could not.**

First, **the agreement is a requirement rather than a coincidence.** At wrench
`d62f755` this morning its encoder used `strconv.FormatFloat(v, 'g', -1, 64)`
and `1000000.0` encoded as `1e+06`, so my case 7 would have failed yesterday.
FR-4.8 landed at wrench `7076801` today: positional decimal, never an exponent,
in every codec of every pack. That is the upgrade its declination was waiting
on, and "unspecified, and disagreeing with my other two packs" is retired.

Second, **key order, which a fixture match cannot show.** wrench sorts
unconditionally; so does infobot, at `state.go:221`, `sort.Strings` over the key
set before emitting. Checked rather than assumed, against wrench's own example
inserted out of order plus every field this file might plausibly gain and three
cases that break a naive sort. They agree exactly, `Agent` and
`_leading_underscore` included. Adding a key is therefore safe from reordering
at both ends.

**FACT 2026-08-28: a defect of infobot's turned up in wrench's list of edges it
thought were not a problem.** wrench escapes `\ " \n \t \r`. infobot escapes
`\` and `"` only, at `state.go:237`, so a value carrying a newline is emitted
raw. Measured with a payload whose `current_dir` holds one:

    "cwd": "/home/x/a
    b"

Eight lines for seven keys, so FR-1.11g's one-key-to-a-line is broken, and
silo's board reading `grep -oP '"cwd": "\K[^"]+'` returns `/home/x/a` and stops.
**That is FR-1.11q's shape exactly**, a silent wrong answer rather than a parse
failure, arriving through a different door. A newline is legal in a Linux path,
so it is reachable, and nothing has reached it.

**The newline is one member of a class, and the rest is worse.** FACT
2026-08-28, `.ephemera/ctrl-probe.py`, a control character in `current_dir`:

    ESC BEL DEL   raw, and no YAML parser will read the file back
    U+0085 CR     parses, and comes back as a space
    TAB           survives

So infobot writes files it cannot read, and silently corrupts two characters. It
is FR-1.11q's shape twice over, and the same class wrench found in its own packs
the same day. A path may hold any byte but NUL and `/`, so all of it is legal.

I told wrench its escaping was "strictly better than mine" before measuring
this. Wrong in both directions: wrench corrected that its other two packs
disagree with its Go one, and this shows mine is worse than I had assumed rather
than merely thinner.

**Linking still repairs it.** wrench's Go pack escapes ESC and round trips it,
measured at wrench `67d843a`, and the Go pack is the one that would be linked.
Its caveat is precise and worth keeping: its Python and Rust packs diverge from
its Go one on these inputs, so a conformance check comparing infobot's bytes
against a non-Go pack would inherit a divergence rather than escape one. That is
`clank/tasks/wrench/parity/50-*.questions`.

**The fix stays inside the form, because the form was never the limit.** FACT
2026-08-28, `.ephemera/yaml-styles.py`: YAML's double-quoted scalar carries the
full C-style escape set and stays on one line, so it is JSON's string with more
in it, and it is already the style this file emits. Every control character
round trips as `\xNN`. YAML even names U+0085 as `\N`, which JSON cannot spell.

So this is an emitter implementing two escapes out of roughly twenty, not a
format that cannot express the value. Widening the set changes no byte of any
file rendered so far.

**The characters that parse raw are the dangerous ones.** ESC, BEL, NUL and DEL
are refused, which is a loud failure. NEL, CR, LF and TAB are accepted raw and
two of them come back wrong, which is the silent one. A stricter emitter is
therefore safer than a stricter parser here.

The block styles `|` and `>` are YAML's multi-line forms and are not wanted:
they span lines by definition and FR-1.11g is one key to a line.

Fixing it is still an FR-1.11o announcement, because the form's definition
changes even though its current output does not.

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
