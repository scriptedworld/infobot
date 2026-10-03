# What is not done

The task tree in clank is the queue, and a directory's suffix is its state.
This file holds what the tasks do not: open problems, and why each is open.

    ( setopt null_glob; print -l <task-tree>/**/*.ready )

## The gate is red on lint alone

`common-quality` passes, wording and suppressions included. `go-std-quality`
fails on `lint` and nothing else: format, build, vet, tidy, vulnerabilities,
the suite and per-file coverage all pass. Measure lint uncapped, because
golangci-lint caps its own output at 50 per linter and 3 per repeated message,
and neither the shared config nor the jig turns that off:

    golangci-lint run --config config/go-std-quality.golangci.yml \
        --max-issues-per-linter 0 --max-same-issues 0

All 126 findings are questions for toolbox's shared config, which has no
per-project override, and wait on toolbox's `go-jig/10`. They are decided
there, not worked around here:

    paralleltest      94   the tests were never meant to run in parallel
    mnd               32   none is truly magic

gosec finds nothing. A file opened by a computed path is read through an
`os.Root` on the directory that holds it, and an executable test fixture is
written through `os.CopyFS`, which keeps its execute bits.

## Twelve pragmas still carry answers nobody gave

S-1 and S-2 in `SUPPRESSIONS` hold twelve pragmas, from `9e92fa9`, whose
answers nobody gave. Hard rule 4 admits no pragma until a person has answered
why, so each needs a decision: keeping it adopts an answer nobody gave, and
dropping it returns its finding to the count above. S-4's one mark was asked
and answered. The three G304 marks on the transcript reads could be removed
instead, by reading through an `os.Root` on the transcript directory as the
config reads do.

## The state file's mode is a question, not a finding

gosec wants 0600 where the file is written 0644.
`TestFileIsReadableByOtherPrograms` pins 0644 while citing FR-1.11b, which is
about writing whole or not at all and says nothing of a mode, so the test
asserts what no requirement states. silo's board reads the files as the same
user, so 0600 would not break the one known reader. Tightening it drops an
intent recorded nowhere else.

## A push waits on the gate

`origin` is `git@github.com:scriptedworld/infobot.git`, and the last commit
there is `de7d2de`. A push waits on the four conditions in
`docs/DECISIONS/a-push-waits-on-history-and-documentation-review.md`, and of
those the gate is what is left: lint is red, as above.

`clone = false` in `dotfiles/repos.live.yaml` still holds; it governs whether
the estate clones this tree, not whether a remote is configured in it.

## Requirement rows that disagree with the code

**FR-3.3** says an unknown width "means render the full form", and
`docs/SPEC.md`, "The width", repeats it. The context bar is drawn at FR-5.12's
fifty cells there, but `build` takes the compact form for the rate-limit gauges
at width 0, printing their percentages without bars. Which is intended needs a
person.

**FR-6.11, FR-3.5 and FR-1.7** predate the configuration files. FR-6.11 names
the ENCOM teal the seed no longer uses, FR-3.5 fixes the reserve at three where
`layout.json` sets it, and FR-1.7's list of files opened leaves out
`palette.json` and `layout.json`. The tests for the two files cite FR-6.11 and
FR-3.5 because SPEC does; the rows want restating under new ids.

**FR-4.3 and FR-4.4** name Python functions (`time.time`, `limit_segment`,
`place_context`, `compose`). What they require survives in Go: the clock, the
transcript root and the XDG paths are parameters, so sections 7 and 8 are
tested against a fixture tree with nothing patched. Each needs restating under a
new id against that property.

## The layout is not monotonic as the pane narrows

`Build` composes both rows, measures the meter row, and when it is over budget
recomposes both rows compacted. Row one then has more slack than it had, so the
cost can take back a form it gave up at a wider width, which FR-5.7 forbids.
Compaction is a decision about row two and should not hand row one a budget.

It holds today by where two thresholds sit, not by construction:
`TestCostShortensThenDropsAsTheRowNarrows` passes with ten gauge cells and a
margin of 3, and fails at eleven, twelve or thirteen cells, or at a margin of 8
(at width 79). The gauge's percentage reading is declined below `readingMin`
columns for the same reason.

The obvious repair, keeping row one from the first composition and row two from
the second, is unsafe: `placeContext` can move the context segment between the
rows, so one row from each composition can duplicate or drop it.

**It blocks raising the default margin.** At 3 the line renders 309 columns in
a 313-column terminal and Claude Code cuts both rows with its own ellipsis; at
8 both render complete. What eats the columns varies: the Remote Control
indicator sits to the right of the status line when it is on, and neither the
environment nor the payload says whether it is, so the margin cannot be
derived. This machine sets 8 in `~/.config/infobot/layout.json`, and the
default moves once the layout is monotonic.

## The rows want describing in configuration

Not scheduled. The palette and the margin already live in
`~/.config/infobot/palette.json` and `layout.json`, each overlaying a compiled
seed and falling back on anything malformed. They are separate on purpose: the
palette follows the theme, and the margin is a fact about the terminal. The
shape of each row would be next, a file naming the segments and their order.

Every segment is already a function from the payload to a string, and
`compose` only picks and orders them, so a template could drive that loop. What
does not fall out is the fitting: the context bar takes what the row leaves, and
the meter row compacts as a unit. A template has to say which segment absorbs
the slack, which may be dropped and in what order, and that vocabulary does not
exist. It follows the monotonicity fix above, or it encodes the defect in a file
format.

## The state file

Its form is stated in `docs/SPEC.md`, "The state file". Two things constrain
changing it.

It has a reader outside this repository. silo's `bin/board` matches `session`,
`written` and `context_percent` with patterns anchored at the line start,
parses `written` with `date -d`, and treats a file older than 15 minutes as a
stopped session. It joins to herdr by session id, since `cwd` follows the tool
shell. FR-1.11o makes announcing a change to the form a requirement: adding a
key is safe, and changing the shape is not.

Its exact bytes are asserted here (FR-1.11p) by `TestCanonicalForm`, against the
pack that emits them. Editing that test to make it pass is the only way the
check comes undone.

**A schema covers the other half.** `published-form/10` is `.questions`: the
render already validates against the schema through wrench's pack and pays 1.8
to 3.2 ms of the validator's package init per event, against the shape silo
`c4ef97a` settled. The schema cannot replace the byte check: `1e+06` and
`1000000` decode to the same number, so no schema reaches FR-1.11q's rule about
the spelling.

**A fixture carries this machine's username.** `internal/state/state_test.go`
asserts an absolute home path as `cwd` twice. Not a secret, and untidy for a
published repository. wrench keeps a fixture of these bytes too, so change both
together.

## The wrench pack

Why the state file goes through it:
`docs/DECISIONS/the-state-file-is-emitted-by-wrenchs-pack.md`.

**The announcement FR-1.11o obliges has not been made.** Linking changed three
escape spellings: U+2028 from ` ` to `\L`, U+2029 from ` ` to `\P`,
U+0085 from `\x85` to `\N`. Both spellings escape and round-trip, and only a
value carrying one of those characters is affected, which a `cwd` or a model
name does not. The board's patterns match keys, so the expected impact is none,
but saying so to its owner is the announcement, and it has not been sent.

**The pin names a commit wrench's remote does not carry.** The pin is
`f34be14`; wrench's `origin/main` holds the same tree as `8e9d199`. A build the
module cache cannot serve has nothing to fetch. Re-pin once, after wrench rewrites its commit messages
(`clank/tasks/wrench/prose-cleanup/50`), since that changes every SHA again.
A tag on the Go pack would make this a version bump; that is open in wrench.

To upgrade: wrench pushes; `go get github.com/scriptedworld/wrench/go@<commit>`;
the suite passes; and the rebuilt `bin/statusline` is checked for something the
new pack changed, which is how the upgrade is known to have happened. The last
time, that was the string `WRENCH_ALLOW_EXTERNAL_SCHEMA_REFS` disappearing.

## Open questions

`docs/REQUIREMENTS/testable-and-open/` holds them, each with an id, so closing
one is a change to a row. `state-readers/10` is the one with a task, in `.questions`.
Its first question, whose a reader over every session's file would be, is
partly answered already: silo's board is that reader and has an owner. What
remains is where a session's intent comes from, and whose vocabulary it uses.

`PACE_CONFIDENT` at 0.6 and the squared fade were tuned by eye against one
evening's numbers. Whether the middle of a window reads too quiet is open until
normal use shows it.
