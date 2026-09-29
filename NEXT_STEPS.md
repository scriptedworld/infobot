# What is not done

The task tree in clank is the queue, and a directory's suffix is its state.
This file holds what the tasks do not: open problems, and why each is open.

    ( setopt null_glob; print -l <task-tree>/**/*.ready )

## The gate is red

`just checks` stops at `common-quality`, so `go-std-quality` is not reached:

- **suppressions**: four pragmas in `internal/render` disagree with
  `SUPPRESSIONS`, below.
- **wording**: `bin/voice-tells.py` finds the prose findings that
  `documentation/30` owns, which waits on `documentation/filing/`.

Behind them, `lint` in the Go jig is red on 152 findings when measured
uncapped. golangci-lint caps its own output at 50 per linter and 3 per repeated
message, and neither the shared config nor the jig turns that off, so measure
it this way or the count is a floor that shifts as the tree changes:

    golangci-lint run --config config/go-std-quality.golangci.yml \
        --max-issues-per-linter 0 --max-same-issues 0

130 of them are questions for toolbox's shared config, which has no
per-project override, and are filed there:

    paralleltest      89   the tests were never meant to run in parallel
    mnd               28   none is truly magic
    gochecknoglobals  13   all are types Go's const cannot hold, none mutated

22 are infobot's own, all gosec, and none is settled by an edit:

    G304  10   a file opened by computed path: the transcripts, the rate
               table, and temporary paths a test has just built
    G306   7   a WriteFile that must land executable; 0600 is not a mode a
               script runs from, so rule and fixture cannot both hold
    G204   3   a subprocess with a variable: the width query the design
               requires, and two tests invoking the shim
    G301   1   and G302 1, both the state file's mode, below

`jig-adoption/10` carries them.

## Twenty-four pragmas were added without anyone being asked

Merged at `9e92fa9`: fourteen `gochecknoglobals` and ten `gosec`, with a
`SUPPRESSIONS` file asserting answers to questions nobody had been asked. Hard
rule 4 admits no pragma until a person has answered why. `palette_config.go`
and `width_tty.go` were built on top, so reverting is possible and not a clean
revert.

It needs a decision, not an edit. Keeping them adopts answers nobody gave.
Dropping them returns 24 findings and the position above. The four
`internal/render` files `suppressions` fails on are part of the same set.

## The state file's mode is a question, not a finding

gosec wants 0600 where the file is written 0644.
`TestFileIsReadableByOtherPrograms` pins 0644 while citing FR-1.11b, which is
about writing whole or not at all and says nothing of a mode, so the test
asserts what no requirement states. silo's board reads the files as the same
user, so 0600 would not break the one known reader. Tightening it drops an
intent recorded nowhere else.

## This repository has a remote the decision says it should not

`origin` is `git@github.com:scriptedworld/infobot.git`, and the last commit
known to be there is `de7d2de`. `docs/DECISIONS/the-remotes-are-off-until-the-documentation-is-ready.md`
opens by saying no repository here has a remote, because the absence is what
keeps the scrubbed history from surviving in a clone or a cache. Here that
protection has already lapsed.

`clone = false` in `dotfiles/repos.live.yaml` still holds; it governs whether
the estate clones this tree, not whether a remote is configured in it.

This needs a person: either the decision is spent and gets rewritten, or the
remote is there by accident and the question is what it already published.
Nothing is pushed until it is answered.

## Requirement rows that disagree with the code

**FR-5.12** reads "with the width unknown the bar is a fixed fifty cells", and
`docs/SPEC.md`, "The width", repeats it. The code draws no bar at all (`fitted`
in `internal/render/segments.go`), and `TestUnknownWidthDrawsNoBar` asserts that
while citing FR-5.12. The comment beside the code says why: a bar is a claim
about room, and with no width any length is a guess the host truncates. Which
is intended needs a person. If it is the code, FR-5.12 is retired for a new row
and the test and SPEC follow.

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

**The pin names a commit wrench's remote no longer carries.** The history
rewrite replanted `f34be14` as `8e9d199`, with the same tree, and only
`8e9d199` is on wrench's `origin/main`. A build the module cache cannot serve
has nothing to fetch. Re-pin once, after wrench rewrites its commit messages
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
evening's numbers. Only normal use says whether the middle of a window is too
quiet.
