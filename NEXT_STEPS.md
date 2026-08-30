# What is not done

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

## The gate is red on one task, lint

The Go jig is adopted and `just checks` runs it. Six of its seven tasks pass,
along with all four of common-quality. Coverage is 92.8%, per file at the 80%
bar, with both entry points measured by building with `go build -cover` and
running them.

`golangci-lint` reports 124 issues, and rule 4 forbids settling any with a
pragma, so each is a decision. 89 are escalated to toolbox, whose shared config
has no per-project override:

    paralleltest      50   tests were never meant to run in parallel
    mnd               26   all 26 checked; none is truly magic
    gochecknoglobals  13   all are types Go's const cannot hold, none mutated

Three entries under `clank/inbox/toolbox/`. The remaining 35 are infobot's own,
workable now, and `build.go:230` is the one to start with: it writes the
program's entire output through an unchecked `Fprintln`.

## The secrets jig gates, since toolbox fixed it

Fixed upstream at toolbox `adc8d00`. `detect-secrets scan --baseline` is a
builder rather than a checker, so it absorbed a newly committed credential and
exited 0; the task now runs `detect-secrets-hook` over the tracked file list,
which gates. `common-quality` composes it, so `just checks` reaches it.

## Smaller things

**A fixture carries this machine's username.** `internal/state/state_test.go`
asserts `"cwd": "/home/ancient/.projects/infobot"` twice. Not a secret, untidy
for a published repository, and **not a one-line change**: FR-1.11p pins those
bytes at both ends, so it needs wrench in the loop. Do it in the same pass as
the link decision below.

**No remote, deliberately, and it is not a blocker.** `clone = false` in the
roster. The commit histories were scrubbed to clean up their messages, which
removed the remotes, and they are being left off on purpose: while a repository
is offline no copy anywhere can hold a reference to the rewritten-away history.

**What ends it is the documentation, not a decision about hosting.** These
projects go up once their documentation is clean and appropriate. So a document
reporting the missing remote as an open question or an obstacle is wrong, and
`docs/DECISIONS/the-remotes-are-off-until-the-documentation-is-ready.md` is the
long form.

**`PACE_CONFIDENT` at 0.6 and the squared fade** were tuned by eye against one
evening's numbers. Expect them to move; only normal use answers whether the
middle of a window feels too quiet.

## Open questions

Section 4 of `REQUIREMENTS.md` holds six, each with an id so closing one is a
change to a row: one row against two, colouring the countdown, a staleness
marker on the cost, the parameter budget, whether the state file should say what
a session is DOING, and which kind of absence a missing context block is.

The last is `clank/tasks/infobot/state-readers/`, in `.questions`. **Its premise
has since been settled and the task file has not caught up:** the board is
silo's dispatch tooling, so a reader over every session's file already exists
and has an owner. What remains is narrower, being where intent comes from and
whose vocabulary it uses.

## Two things that constrain changes to the state file

**It has a reader outside this repository.** silo's coordination board pulls
`context_percent` and `cwd` from every session's file with patterns anchored on
the quoted key and the single space after the colon, taking the number bare.
FR-1.11o makes announcing a change to that form a requirement rather than a
courtesy. Adding a key is safe; changing the shape is not.

**It is pinned against wrench by a fixture at each end, not by shared code.**
FR-1.11p. Editing `TestCanonicalForm` to make it pass is how that pin comes
undone, and it is the only way it can.

## The wrench link is decided by evidence and waiting on a decision

wrench declined on 2026-08-28 for two reasons and both have expired: its gate is
green, and its float behaviour is fixed by its own FR-4.8 rather than being
unspecified. Measured since, `.ephemera/wrench-parity`:

    the TestCanonicalForm fixture      byte-identical
    every edge the fixture misses      agrees, whole-number floats included
    key order                          both sort, and agree on the hard cases

**Two costs, both real and both small.** The import path moves to
`github.com/scriptedworld/wrench/go` when wrench's `gate/05` lands, on no date
and in two trees now that `bolt.go` consumes it. And linking would change
infobot's bytes on about fourteen escape spellings, where Go names what this
spells `\xNN`; `TestCanonicalForm` holds none of them, so the fixture would pass
without noticing.

Taking the link turns FR-1.11p from two emitters pinned by two fixtures into one
emitter with a conformance check, which is better than either option available
when wrench declined. It is a change to how a published form is produced, so it
belongs to wrench as much as here, and FR-1.11o makes announcing it an
obligation.

`clank/tasks/infobot/one-emitter/10-*.cancelled` carries the answer as it stood.
Its premise is what changed rather than its reasoning.
