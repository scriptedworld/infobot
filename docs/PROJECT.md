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
fresh clone re-derives it.

The first row is the corpus payload, with no cost segment; the second adds a
pane width and a live session id. Why the gap between them is file search:
`docs/LESSONS/the-port-removed-the-interpreter-not-the-file-search.md`.

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
    scripts/           the entry-point coverage run and the render probe
    README.md          the front door: what it is, how to build and wire it
    LICENSE, NOTICE    Apache 2.0
    docs/REQUIREMENTS/ what must be true of it, one file per requirement
    NEXT_STEPS.md      what is not done, and why
    docs/PROJECT.md    this file
    docs/SPEC.md       how it is built, and the published forms
    docs/DECISIONS/    why a choice was made
    docs/LESSONS/      what a mistake cost
    docs/PATTERNS/     techniques that carry to other projects

`README.md` is for somebody arriving at the repository, and this file is for
somebody working in it. Where they cover the same ground the README states the
instruction and the reason lives in `docs/`.

## How it is invoked

Claude Code's settings file names it by absolute path:

    "statusLine": { "type": "command", "command": ".../infobot/bin/infobot",
                    "refreshInterval": 10 }

That file stays in silo, because it is Claude Code's configuration, not
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

bolt exits 0 whenever a run completed, whatever the tools concluded, so every
call goes through `_verdict` in `just/base.just`, which reads `success` from the
`result.yaml` the run names. How the recipes are layered is
`docs/PATTERNS/the-two-word-interface.md`.

Coverage is judged per file at 80%, and the two entry points are measured, not
excluded, which is hard rule 5: `docs/PATTERNS/measuring-an-entry-point-with-go-build-cover.md`.

What is red, and why: `NEXT_STEPS.md`, "The gate is red".

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

The file is the only copy, and without a usable one the cost segment is left
out (FR-8.27, FR-8.28), so the coordinator's refresh is what keeps the segment
alive.

## What is decided, and what is open

Done, not merely decided: Go. The port landed on 2026-08-28 and the Python is
gone. It reads 17 of 18 identical against the golden corpus the Python
produced, and the eighteenth differs on purpose.

The corpus was captured from the Python, which had no suite of its own:
`docs/LESSONS/an-absent-suite-is-not-an-absent-oracle.md`. It leaves out the
countdown and the cost by construction, so no case is a representative render:
`docs/LESSONS/a-golden-corpus-measures-the-cheapest-render.md`.

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

Open: `docs/REQUIREMENTS/testable-and-open/`, each question a row with an id, so
closing one is a test or a decision against a row that exists. What is not done
is `NEXT_STEPS.md`.
