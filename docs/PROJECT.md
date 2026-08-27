# infobot, the project

The status line Claude Code renders at the bottom of the screen. It reads the
session JSON on stdin and writes the rows that say where you are and how much of
the context window and the rate limit windows you have spent.

The name is the caretaker of the Great Clock, who keeps the machinery running
and tells you what state it is in. It replaces `milton`.

## What it is FOR

One job: turn the session payload into rows. Formatter is a constraint, not a
description. No network, and one subprocess to ask whoever owns the pane how
wide it is, required because every other route to the width fails under Claude
Code. tmux and herdr are both answered; see FR-3.3. The files it opens are the
rate table and the session's own transcripts, both for the cost segment and
both named in this document.

It runs on every Claude Code event, so its startup cost is paid constantly, and
19ms of the current 33ms per render is the Python interpreter starting. That is
why the port to Go is queued.

## Why it is its own repository

It came out of `silo`, which holds the standing rules, the settings, the hooks
and the written record: things a person reads. infobot is a program, with its
own requirements, its own gate and its own tests. Keeping it in silo showed:
silo's gate never read `bin/statusline` at all, because lizard selects by file
extension and the script had none.

## Layout

    bin/infobot        the status line. Python today, Go when the port lands.
    REQUIREMENTS.md    what must be true of it
    NEXT_STEPS.md      what is not done
    docs/PROJECT.md    this file
    START_HERE.md      the session handoff. Untracked, rewritten each session.

## How it is invoked

`silo/settings.json` names it by absolute path:

    "statusLine": { "type": "command", "command": ".../infobot/bin/infobot",
                    "refreshInterval": 10 }

That file stays in silo, because it is Claude Code's configuration rather than
infobot's. infobot does not read it.

## The gate

No jig is adopted yet. `toolbox/bolt.common-quality.yaml` gives traceability,
the suppression register and complexity; `bolt.go-std-quality.yaml` adds the
nine Go checks and is the one that matters, because it judges coverage per file
at 80% through an adapter that exists. The Python equivalent measures coverage
and applies no threshold, which is the main reason the port is to Go.

Adopting them is queued in `clank/tasks/infobot/`.

## Perishable: the pricing table

`~/.config/infobot/pricing.json` carries the API rates the cost segment prices
a session with, and the date they were taken.

    source    https://platform.claude.com/docs/en/about-claude/pricing
    max age   1 day
    on stale  re-read the source and rewrite the file, keeping the `taken` date
              in step. Take whatever is current, promotional rates included:
              the figure is what these prompts would have cost today, so a deal
              running today belongs in it and is gone from it tomorrow.

They drift, and not only on a schedule. Sonnet 5 launched at an introductory
$2/$10 with a rise to $3/$15 booked for September; the rise was then cancelled
and the introductory rate became the standard one.

Nothing refreshes it today, and the status line cannot: it is a formatter that
runs on every event and makes no network call. Filed against agent-support as
`clank/inbox/agent-support/grok-could-refresh-perishable-data`, because `/grok`
runs at the start of most sessions and costs nothing when the file is fresh.

The seed copy in `infobot/pricing.py` is the fallback, so a fresh clone renders
with no config file and no network.

## What is decided, and what is open

Decided: Go, and as of 2026-08-26 the timing too, which was the part that stayed
open long after the language did not. The port runs leaf-first across
`clank/tasks/infobot/status-line/`, one file per task with its tests beside it,
and the Python keeps rendering until the last of them. The argument is in task
05 rather than restated here.

Decided: the binary is built inside this repository and reached through a
symlink in `dotfiles/bin`, which is what `bolt`, `converge` and `update` do.
That choice is what FR-1.13 is about: a build that has not run and a symlink that
dangles fail exactly as a blank line, the same way a missing import would, so the
port must be able to say it is not built.

Decided: two rows, not three. A third for exceptional states, a stale rate table
or a promotion in effect, was declined. A row that appears only when there is
something to say moves the prompt every time it comes and goes, and one always
there spends the space on nothing most of the time. Those states belong on the
segment already present, the way an unpriced model appends a plus to say the
figure is a floor.

Open: section 4 of `REQUIREMENTS.md`, FR-4.1 to FR-4.7. Each open question is a
requirement with an id rather than a line of prose, so closing one is a test or
a decision against a row that exists.
