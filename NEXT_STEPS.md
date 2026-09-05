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

    ( setopt null_glob; print -l <task-tree>/**/*.blocked )

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
filed against toolbox, with both runs as
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

Three entries filed against toolbox, each corrected for the cap.

**HARD RULE 4 WAS BROKEN HERE ON 2026-09-04, by a session that had not read
this section.** Twenty-four pragmas were added — fourteen `gochecknoglobals` and
ten `gosec` — and a `SUPPRESSIONS` file was written asserting answers to
questions nobody had been asked. The paragraph below says no pragma goes in
without a person having answered first, and that is exactly what happened.

Two things it pre-empted, both decided above and neither by a person at the time:

- The 130 escalated findings are a question for toolbox's shared config, not a
  case for 130 local pragmas. Fourteen of them now carry one.
- The state file's mode is recorded below as **a question, not a finding**. It
  now carries `//nolint:gosec` and a written justification.

The measurement in that session was also capped: it reported 93 findings from a
plain `golangci-lint run`, where the figure above is 152 measured uncapped.

**Decide before building on it.** The pragmas are in `main` on both machines —
merged at `9e92fa9` with `palette_config.go` and `width_tty.go` built on top —
so reverting is possible and is not a clean revert. Keeping them means adopting
answers nobody gave; dropping them returns 24 findings and restores the position
this section records.

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
asserts an absolute home path as `cwd` twice. Not a secret, untidy
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

The last is the task tracker, in `.questions`. **Its premise
has since been settled and the task file has not caught up:** the board is
silo's dispatch tooling, so a reader over every session's file already exists
and has an owner. What remains is narrower, being where intent comes from and
whose vocabulary it uses.

## The cost recovers a form it already surrendered, at width 79

FACT 2026-09-05. `TestCostShortensThenDropsAsTheRowNarrows` sweeps the width
down and asserts the cost never returns to a fuller form as the pane narrows.
With `margin` at 3 it passes. At 8 it fails at width 79, which went back to a
fuller form than 80 had.

**Latent, not introduced.** The margin change exposed it; the non-monotonicity
is in the layout. It was not chased because the session was out of context, and
the default was left at 3 rather than editing the test to accommodate it, which
would have hidden the bug the test exists to catch.

**This blocks raising the default margin**, which wants to happen: 3 truncates
against a real terminal. See below.

## The margin default is known wrong and cannot move yet

FACT 2026-09-05, measured by screenshotting the terminal at 313 columns. At
`margin = 3` the line renders 309 and Claude Code cuts BOTH rows with its own
ellipsis, losing the end of the session id and the saved figure. At 8 both
render complete.

**What eats the columns is not a fixed quantity.** The Remote Control indicator
renders to the RIGHT of the status line when it is on. Nothing in the
environment or the payload reports whether it is, so the margin cannot be
derived. FACT: no `CLAUDE_*` variable carries it, and the payload keys are
`context_window`, `display_name`, `effort`, `id`, `level`, `model`,
`rate_limits`, `session_id`, `workspace`.

This machine sets 8 in `~/.config/infobot/layout.json`. The compiled default
moves once the monotonicity above is fixed.

## The rows want describing in configuration rather than in code

Wanted, not scheduled, and not started.

**Two pieces of it landed 2026-09-05**, both following `internal/pricing`: the
palette is `~/.config/infobot/palette.json` and the margin is
`~/.config/infobot/layout.json`. Both overlay a compiled seed and fall back on
anything malformed, because a status line that fails shows nothing at all. They
are separate files deliberately: the palette is a symlink into `g0bl1n.theme`
and is the same wherever the theme is adopted, while the margin is a fact about
the terminal and the host build in front of it. That split is the one to keep
when more of the construction moves to configuration. The shape of each row would be lines in
a configuration file naming the segments and their order, so changing what the
status line shows stops being a code change.

**Most of the way there already.** Every segment is a function taking the
payload and returning a string, and `compose` does nothing but pick them and put
them in order. A template naming `model`, `path`, `context`, `session` would
drive that loop with no segment needing to change.

**The width fitting is what does not fall out of it, and it is the whole
difficulty.** Fitting is not a property of any segment: the context bar takes
whatever the row has left, which means measuring every other segment first, and
the meter row compacts as a unit when it is over budget. So a template has to
say more than an order. It has to say which segment absorbs the slack, which
may be dropped, and in what order things give way, and that vocabulary does not
exist yet.

Worth doing after the compaction coupling below is fixed rather than before.
Describing the current fitting behaviour in configuration would be encoding a
defect in a file format.

## Compaction hands row one a budget it did not have

`Build` composes both rows, measures the meter row, and recomposes BOTH rows
compacted when it is over budget. Row one then has more slack than it did, so
the cost segment can take back detail it had already given up as the pane
narrows, which FR-5.7 forbids.

**Compaction is a decision about row two and should not hand row one a budget
it did not have.**

It is latent rather than firing: the monotonicity holds at ten gauge cells by
where two thresholds happen to sit, not by construction. Measured 2026-08-31 at
`9bfbfdf`, with no other change, widening the gauge to eleven cells breaks
`TestCostShortensThenDropsAsTheRowNarrows` and twelve and thirteen break it too.
Nine and ten pass.

The reading beside each gauge is therefore declined below `readingMin` columns,
which is not a weaker claim but a refusal to make the claim where it cannot be
held. Fixing the coupling is what would let that restriction go.

The obvious repair, keeping row one from the first composition and taking only
row two from the second, is unsafe as it stands: `placeContext` moves the
context segment between the rows, so one row from each composition can duplicate
or drop it.

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

## The wrench link is taken, and the announcement it obliges is not made

**Landed 2026-09-03.** `internal/state/state.go` imports
`github.com/scriptedworld/wrench/go`, writes through `wrench.SaveYAMLFile`, and
the 133-line hand emitter is deleted. `gate/05` was named here as the blocker
and was never the blocker: `go get github.com/scriptedworld/wrench/go@latest`
resolves, and `go.mod` carries the dependency at a pseudo-version because the Go
pack has no tags, which is a different problem and still open.

FR-1.11p is rewritten: one emitter and a conformance check, rather than two
emitters pinned by two fixtures. That is the better of the two, and it is what
this section predicted when it was still a proposal.

**The announcement FR-1.11o obliges has not been made.** Linking changed the
emitted bytes on three escape spellings — U+2028 from `\u2028` to `\L`, U+2029
from `\u2029` to `\P`, U+0085 from `\x85` to `\N`. Both spellings escape and
both round-trip, and the change reaches only a value carrying one of those three
characters, which a `cwd` or a model name does not. That makes it small, not
exempt: FR-1.11o says a change to the published form is announced **before it
lands rather than discovered by whatever breaks**, and it landed first.

The reader is silo's `bin/board`, which matches anchored patterns against the
quoted key. Those patterns are unaffected by an escape inside a value, so the
expected impact is none — but "we checked and it is none" is the announcement,
and nobody has sent it.

**This repository does not build against the wrench beside it.** `go.mod` names
`github.com/scriptedworld/wrench/go` at a pseudo-version fetched from GitHub and
there is no `replace`, so the local checkout is invisible here: a rebuilt
`bin/statusline` on 2026-09-04 still contained `WRENCH_ALLOW_EXTERNAL_SCHEMA_REFS`,
a string wrench had removed that morning, because the published pack still has
it.

That is the right default and it has an ordering consequence worth writing down:
**infobot is verified against the wrench that is pushed, not the one that is
written.** Every green run here between 2026-09-03 and wrench landing was a run
against the older pack.

**Done 2026-09-04, in that order.** wrench pushed at `f34be14`;
`go get github.com/scriptedworld/wrench/go@latest` took the pseudo-version
`v0.0.0-20260904181338-f34be142d905`; the suite passes; the rebuilt
`bin/statusline` no longer contains `WRENCH_ALLOW_EXTERNAL_SCHEMA_REFS`, which
is how the upgrade is checkable rather than assumed. None of it is optional next
time either.

The schema infobot compiles carries no `$ref`, so wrench's reference change
cannot reach it. That is a reason to expect the upgrade to be quiet, not a reason
to skip it.

**wrench's decision document is spent.**
`docs/DECISIONS/infobots-hand-emitted-yaml-is-a-considered-duplicate.md` resolved
a duplication that this change ended. It is rewritten there rather than here, and
the fixture it defended stays on its own merits.
