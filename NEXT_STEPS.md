# What is not done

## Queued

    gate/10          the shims are read by something       .blocked
    jig-adoption/10  adopt the Go jig                      .planning
    one-emitter/10   link wrench's pack so there is one    .cancelled
    published-form/10 a schema for the status file         .ready
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

**Lint is the only red task and it is reached**, which took two fixes on
2026-08-30 and neither was about the findings. `config/go-std-quality.golangci.yml`
was never linked, so the task exited 3 on a missing file for as long as the jig
had been adopted and the count below came from running the tool by hand. And
`common-quality` failed ahead of it on a recipe name, so nothing after it ran at
all.

**The 124 this file used to quote was a capped number.** golangci-lint
truncates its own output by default, `max-issues-per-linter` at 50 and
`max-same-issues` at 3, and neither the shared config nor the jig turns it off.
Uncapped, the same tree reported 187. Filed as
`clank/inbox/toolbox/the-lint-task-reports-a-capped-count`, with both runs as
evidence, because it reaches every adopter and the fix is one flag pair on the
jig's command line.

Measure it uncapped or the figure is a floor:

    golangci-lint run --config config/go-std-quality.golangci.yml \
        --max-issues-per-linter 0 --max-same-issues 0

**152 now**, after the pass of 2026-08-30 cleared everything that needed no
decision. 130 are escalated to toolbox, whose shared config has no per-project
override:

    paralleltest      89   tests were never meant to run in parallel
    mnd               28   none is truly magic; four were never printed
    gochecknoglobals  13   all are types Go's const cannot hold, none mutated

Three entries under `clank/inbox/toolbox/`, each corrected for the cap.

**The remaining 22 are infobot's own and NOT ONE IS AN EDIT.** All gosec, and
rule 4 admits no pragma without a person having answered first:

    G304  10   a file opened by computed path: the transcripts, the rate
               table, and temporary paths a test has just built
    G306   7   a WriteFile that must land executable. 0600 is not a mode a
               script runs from, so the rule and the fixture cannot both be
               satisfied. Any project whose tests write a script meets this
    G204   3   a subprocess with a variable: the width query the design
               requires, and two tests invoking the shim
    G301   1   and G302 1, both the state file. See below

**The state file's mode is a question, not a finding.** gosec wants 0600 where
it is written 0644, and `TestFileIsReadableByOtherPrograms` pins 0644 while
citing FR-1.11b, which is about writing whole or not at all and says nothing
about a mode. The test asserts something no requirement states, and its name
says "other programs" where the assertion only bites for other users. silo's
board reads these files as the same user, so 0600 would not break the consumer
we know about. Tightening it would drop an intent recorded nowhere else.

## The secrets jig gates, since toolbox fixed it

Fixed upstream at toolbox `adc8d00`. `detect-secrets scan --baseline` is a
builder rather than a checker, so it absorbed a newly committed credential and
exited 0; the task now runs `detect-secrets-hook` over the tracked file list,
which gates. `common-quality` composes it, so `just checks` reaches it.

It then flagged this repository's own `secrets:` recipe, because
`detect-secrets` reads the name as an assignment. **silo had already renamed the
recipe to `leak-scan` at `12a5d4e`**, forty-eight minutes before this repository
filed the collision as somebody else's problem, and the copy here was the only
pre-rename one left. Taken at `d33e0c1`, and the pack brought `_verdict` with
it.

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

## The form is described in prose and gets a schema

`published-form/10` is `.ready`. Raised from silo, which reads the form with
anchored greps and wanted a machine-checkable contract, and settled by a change
to the estate decision at silo `c4ef97a`: a component running once per event has
its schema enforced by its suite and a check on the artifact, where a long-lived
one validates on write.

**The render path is untouched, and the measurement is why.** Validating nine
keys is 10 microseconds, but linking the validator costs 6.2ms of package init
in a process that starts once per event, so the rule read literally was 56% on
top of an 11.8ms render. `.ephemera/schema-cost/` regenerates it and
`docs/LESSONS/a-difference-with-two-explanations-is-not-a-measurement-yet.md`
carries how the number was got wrong first.

**A schema does not retire FR-1.11p.** `1e+06` and `1000000` decode to the same
number, so nothing a schema states about the decoded structure reaches
FR-1.11q's rule about the spelling. The schema and the byte fixture cover
different halves.

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
