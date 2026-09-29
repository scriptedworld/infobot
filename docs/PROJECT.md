# infobot, the project

The status line Claude Code renders at the bottom of the screen. It reads the
session JSON on stdin and writes the rows that say where you are and how much of
the context window and the rate limit windows you have spent.

The name is the caretaker of the Great Clock, who keeps the machinery running
and tells you what state it is in. It replaces `milton`.

**Paths here that begin with another repository's name will not resolve for
you.** `clank`, `silo`, `toolbox`, `wrench` and `bolt` are private siblings:
clank holds the task tree, silo the settings that invoke this, toolbox the
adopted checkers, and wrench and bolt the tooling two requirements are pinned
against. They are named rather than hidden because a decision loses its reason
when the thing it was weighed against is edited out, and a reader who cannot
open them can still see what the argument was. Nothing in this repository needs
any of them to build or to run; `README.md` says what it does need.

## What it is FOR

One job: turn the session payload into rows. Formatter is a constraint, not a
description. No network, and one subprocess to ask whoever owns the pane how
wide it is, required because every other route to the width fails under Claude
Code. tmux and herdr are both answered; see FR-3.3. The files it opens are the
rate table and the session's own transcripts, both for the cost segment and
both named in this document.

It runs on every Claude Code event, so its startup cost is paid constantly. That
is what the Go port was for. Two figures, because they measure different things
and only the second is what a session pays:

    render alone          Python 25.2ms   Go  4.0ms   6.3x
    a real event          Python 40.4ms   Go 11.8ms   3.4x
    peak RSS              Python 17.5MB   Go   ~9MB

Medians over 40 to 50 renders of one payload, interleaved, 2026-08-28. To
re-measure, the pre-port Python comes back out of git and runs beside the
binary:

    git show cd24fb4^:infobot/render.py    and its four siblings

Any working copy of that lives in `.ephemera/perf/`, which is gitignored, so a
fresh clone re-derives it rather than finding it.

The first row is the corpus payload, which carries no cost segment, and it is
the 30.6 to 4.5 this document used to quote on its own. The second row adds the
two things a real render does and the corpus omits: a pane width to fit to, and
a live session id, which sends the cost segment to find that session's
transcript under `~/.claude/projects`. That search is about 15ms in either
language, it was ported as it stood, and it is now roughly 60% of what a Go
render spends. Most of what the port removed was the interpreter starting, and
what is left is dominated by file search rather than by language.

Go's RSS is polled from `/proc`, and a process this short-lived can be missed at
its peak, so 9MB is a floor. Python's is exact because it lives long enough to
sample.

## Why it is its own repository

`docs/DECISIONS/infobot-is-its-own-repository.md`, including the gap the split
left: no checker in the gate reads the two shell shims.

## Layout

    bin/infobot        a shell shim. Committed, and execs bin/statusline.
    bin/forget-session the same, for the SessionEnd hook, execs bin/forget.
    bin/statusline     the built binary. Gitignored.
    bin/forget         the built cleanup. Gitignored.
    cmd/               two entry points, one delegating call each
    internal/          the program: render, state, usage, pricing, payload, num
    Justfile           the two-word interface; recipes in just/
    README.md          the front door: what it is, how to build and wire it
    LICENSE, NOTICE    Apache 2.0
    REQUIREMENTS.md    what must be true of it
    NEXT_STEPS.md      what is not done
    docs/PROJECT.md    this file

`README.md` is for somebody arriving at the repository, and this file is for
somebody working in it. Where they cover the same ground the README states the
instruction and this file states the reason, so the install steps live there and
why the shim is the committed half lives here.

The committed half is the shim and the built half is not:
`docs/DECISIONS/claude-code-is-pointed-at-a-shim-not-a-binary.md`.

## How it is invoked

Claude Code's settings file names it by absolute path:

    "statusLine": { "type": "command", "command": ".../infobot/bin/infobot",
                    "refreshInterval": 10 }

That file stays in silo, because it is Claude Code's configuration rather than
infobot's. infobot does not read it.

## Two machines run it, and both stay on the latest commit

lazlo holds this tree and oslo holds a clone of it. Every Claude Code session
on either machine runs the status line, so a commit is not finished until both
are on it and built. oslo counts whenever it is reachable, and a commit made
while it is down goes over the next time it is up. A tree with no built binary
shows "infobot is not built" in every session on that machine.

The two share no remote, so a commit goes over as a bundle, fast-forward only:

    git bundle create .ephemera/infobot.bundle <oslo HEAD>..main
    scp .ephemera/infobot.bundle oslo:.projects/infobot/.ephemera/
    ssh oslo 'cd ~/.projects/infobot &&
              git pull --ff-only .ephemera/infobot.bundle main && just install'

Then check that both agree: `git log --oneline -1` matches on the two machines,
and `just install` on each prints that the binaries are current. To see the
render itself, `scripts/render-probe.sh PAYLOAD` on each host prints the rows
for a payload naming one of that host's sessions; the cost segment is there
only when the host's rate table is usable.

## The gate

    just checks

Two jigs, both adopted from toolbox as symlinks. `common-quality` gives
complexity, traceability, suppressions and secrets; `go-std-quality` gives
build, format, lint, tests, tidy, vet and vulnerabilities.

**bolt exits 0 whenever the run completed, whatever the tools concluded**, so
every call passes `--result-to-exitcode` and the verdict reaches the shell. The
authority is `success` in the `result.yaml` a run names.

**The entry points are measured, not excluded**, which is hard rule 5. They are
one delegating call each and no test process reaches them, so
`scripts/cover-entrypoint.sh` builds both with `go build -cover`, runs them, and
merges the profile. Coverage is judged per file at 80%.

One task is red: `lint`. 152 issues, of which 130 are escalated to toolbox as
config decisions and 22 are infobot's own, every one of them a gosec finding
that cannot be settled by an edit. Rule 4 admits no pragma without a person
having answered first, so they stay red. The task that adopts the Go jig
carries them.

**Measure that uncapped or it is a floor.** golangci-lint truncates its own
output by default, at 50 per linter and 3 per repeated message, and neither the
shared config nor the jig turns it off. The same tree reads 124 capped and 187
uncapped, and which findings are hidden shifts as the tree changes, so a fix can
look like a regression. `--max-issues-per-linter 0 --max-same-issues 0`, filed
for toolbox as `the-lint-task-reports-a-capped-count`.

## Perishable: the pricing table

`~/.config/infobot/pricing.json` carries the API rates the cost segment prices
a session with, and the date they were taken.

    source    https://platform.claude.com/docs/en/about-claude/pricing
    max age   3 days
    on stale  re-read the source and rewrite the file, keeping the `taken` date
              in step. Take whatever is current, promotional rates included:
              the figure is what these prompts would have cost today, so a deal
              running today belongs in it and is gone from it tomorrow.

**Those three lines are an interface, not a description.** The coordinator reads
them and runs the declaration rather than inventing one, so restructuring this
block changes somebody else's behaviour and gets announced the way FR-1.11o's
form does.

Why a person refreshes it, why the coordinator, and why three days:
`docs/DECISIONS/the-rate-table-is-refreshed-by-a-person.md`.

Each rate is `[input, output]` in dollars per million tokens. A model whose
cache reads are not a tenth of input carries its own multiplier as a third
number: Opus 5.5 at 0.05, Fable 5.1 and Mythos 5.1 at 0.025. Leave it off and
the model reads at `cache_read`, which doubles or quadruples the largest line
on those sessions. A model missing from the table leaves the cost segment blank
on any session that ran only that model.

**The file is the only copy.** Nothing is compiled in, so a price change is an
edit to the file and never a rebuild. A host with no file, or one missing
`rates`, `cache_read` or `cache_write`, renders with no cost segment. That makes
the coordinator's refresh the thing that keeps the segment alive, on every host
the status line runs on (see "Two machines run it").

## What is decided, and what is open

Done rather than decided: Go. The port landed on 2026-08-28 and the Python is
gone. It was checked against the 18-case golden corpus the Python produced,
which reads 17 of 18 identical;
the eighteenth is `malformed-resets`, and it differs because the Python wrapped
the whole render in a bare `except` so one bad field blanked both rows. That was
FR-1.4's known gap, captured deliberately so that fixing it would show up here
as a diff. It does.

**The corpus predates the port and was captured from the Python**, by
`capture-golden.py` beside it, whose first line calls itself the oracle for the
port. The Python carried no suite in this repository: no test file, no test
target, nothing the gate ran. It was not unvalidated, and reading the absent
suite as an absent oracle is the mistake the corpus exists to prevent. What it
had was that corpus, a drift checker whose clean run at width 197 on 2026-08-26
is what proved the display had stopped moving, and about 200 assertions across
`.ephemera/check-*.py` covering transcript tailing and the pricing arithmetic.
The harness was throwaway by design and is gone; the cases and their expected
values came across into the Go tests, which is what capturing them was for.

**Two things are absent from every corpus case by construction**, and both are
in `capture-golden.py`'s header. No `resets_at`, because the countdown is
computed from the wall clock and any case carrying one drifts by a minute and
stops being golden. No cost segment, because the session id names a transcript
whose totals grow as a session runs, so the corpus uses an id no transcript
answers to. A case is therefore the cheapest render available and not a
representative one. Timing the binary against it measures the render with the
segment that reads files left out.

Decided: Claude Code reaches the binary through the committed shim.
`docs/DECISIONS/claude-code-is-pointed-at-a-shim-not-a-binary.md`.

Decided: Go is the only implementation. Zig and two generations of C++ were
built beside it and are gone; C++ went on 2026-09-25 with wrench's C++ pack, which
it would have taken its structured files from.
`docs/DECISIONS/go-stays-deployed-while-cpp-clears-its-gate.md` has the figures
and the reasons.

Decided: two rows, not three. `docs/DECISIONS/two-rows-not-three.md`.

Decided: the state file is written through wrench's pack.
`docs/DECISIONS/the-state-file-is-emitted-by-wrenchs-pack.md`.

Open: section 4 of `REQUIREMENTS.md`. Each open question is a requirement with
an id rather than a line of prose, so closing one is a test or a decision
against a row that exists. FR-4.2, which language and when, closed on 2026-08-28
and is in the Retired table; the rest stand.
