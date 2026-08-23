# infobot, the project

The status line Claude Code renders at the bottom of the screen. It reads the
session JSON on stdin and writes the rows that say where you are and how much of
the context window and the rate limit windows you have spent.

The name is the caretaker of the Great Clock, who keeps the machinery running
and tells you what state it is in. It replaces `milton`.

## What it is FOR

One job: turn the session payload into rows. It is a formatter, and that is a
constraint rather than a description. No network, no file reads beyond stdin,
and one subprocess, which is `tmux display-message` for the pane width and is
required because every other route to the width fails under Claude Code. See
FR-3.3.

It runs on every Claude Code event, so its startup cost is paid constantly.
That is the reason the port to Go is queued: 19ms of the current 33ms per render
is the Python interpreter starting.

## Why it is its own repository

It came out of `silo`, which is the standing rules, the settings, the hooks and
the written record: things a person reads. infobot is a program, with its own
requirements, its own gate and its own tests. Keeping it in silo meant one
repository holding both, and it showed: silo's gate reads its own documents and
never read `bin/statusline` at all, because lizard selects by file extension and
the script had none.

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
a session with, and the date they were taken. They drift: Sonnet 5's rate has
an introductory price with an expiry, which is drift measured in days rather
than releases.

    source    https://platform.claude.com/docs/en/pricing.md
    max age   1 day
    on stale  re-read the source and rewrite the file, keeping the `taken` date
              in step. A rate a person set deliberately is a preference and
              stays; note it rather than overwriting it.

Nothing refreshes it today. A session cannot: the status line is a formatter
that runs on every event and cannot make a network call. Filed against
agent-support as `clank/inbox/agent-support/grok-could-refresh-perishable-data`,
because `/grok` runs at the start of most sessions and is the natural place for
a check that has to happen often and costs nothing when the file is fresh.

The seed copy in `infobot/pricing.py` is the fallback, so a fresh clone renders
with no config file and no network.

## What is decided, and what is open

Decided: Go, and the reasoning is in the task rather than restated here.
Decided: the binary is built inside this repository and reached through a
symlink in `dotfiles/bin`, which is what `bolt`, `converge` and `update` do.

Open: whether the two rows should be one, whether the countdown wants its own
colour scale running toward green as a reset nears, and FR-4.1, the test suite.
