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

It runs on every Claude Code event, so its startup cost is paid constantly. That
is what the Go port was for: 30.6ms per render became 4.5ms, measured over 30
renders of the same payload in a herdr pane on 2026-08-28. Most of what went was
the interpreter starting.

## Why it is its own repository

It came out of `silo`, which holds the standing rules, the settings, the hooks
and the written record: things a person reads. infobot is a program, with its
own requirements, its own gate and its own tests. Keeping it in silo showed:
silo's gate never read `bin/statusline` at all, because lizard selects by file
extension and the script had none.

## Layout

    bin/infobot        a shell shim. Committed, and execs bin/statusline.
    bin/forget-session the same, for the SessionEnd hook, execs bin/forget.
    bin/statusline     the built binary. Gitignored.
    bin/forget         the built cleanup. Gitignored.
    cmd/               two entry points, one delegating call each
    internal/          the program: render, state, usage, pricing, payload, num
    Makefile           build, test, cover, gate
    REQUIREMENTS.md    what must be true of it
    NEXT_STEPS.md      what is not done
    docs/PROJECT.md    this file

**The committed half is the shim and the built half is not**, which is FR-1.13
rather than a packaging preference. Claude Code names `bin/infobot` in
`settings.json`, and a binary that has not been built is a blank line nobody is
told about. The shim is what turns that silence into a row saying so.

## How it is invoked

`silo/settings.json` names it by absolute path:

    "statusLine": { "type": "command", "command": ".../infobot/bin/infobot",
                    "refreshInterval": 10 }

That file stays in silo, because it is Claude Code's configuration rather than
infobot's. infobot does not read it.

## The gate

`make gate` runs it: the tests, then complexity, traceability and the
suppression register. `bolt common-quality .` runs the last three through the
jig, and note that bolt exits 0 whenever the RUN completed, so the verdict is
`success` in the `result.yaml` it names rather than the exit status.

`bolt.go-std-quality.yaml` is not adopted yet. It is the one that matters,
because it judges coverage per file at 80% through an adapter that exists.
`make cover` applies that bar by hand meanwhile. **The entry points are measured
rather than excluded**: they are one delegating call each and the test process
cannot reach them, so they are built with `go build -cover`, run, and their
profile read. Both are at 100% that way.

Adopting the jig is queued in `clank/tasks/infobot/`.

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

The seed copy in `internal/pricing/pricing.go` is the fallback, so a fresh clone
renders with no config file and no network.

## What is decided, and what is open

Done rather than decided: Go. The port landed on 2026-08-28 and the Python is
gone. It was checked against the 18-case golden corpus in
`clank/tasks/infobot/status-line/05-*/golden/`, which reads 17 of 18 identical;
the eighteenth is `malformed-resets`, and it differs because the Python wrapped
the whole render in a bare `except` so one bad field blanked both rows. That was
FR-1.4's known gap, captured deliberately so that fixing it would show up here
as a diff. It does.

Decided: the binary is built inside this repository and Claude Code reaches it
through the committed shim, rather than through a symlink in `dotfiles/bin`
which is what `bolt`, `converge` and `update` do. The difference is FR-1.13. A
symlink cannot report its own target's absence, and neither can a missing
binary, so the thing `settings.json` names has to be something that is always
there and can look.

Decided: two rows, not three. A third for exceptional states, a stale rate table
or a promotion in effect, was declined. A row that appears only when there is
something to say moves the prompt every time it comes and goes, and one always
there spends the space on nothing most of the time. Those states belong on the
segment already present, the way an unpriced model appends a plus to say the
figure is a floor.

Open: section 4 of `REQUIREMENTS.md`. Each open question is a requirement with
an id rather than a line of prose, so closing one is a test or a decision
against a row that exists. FR-4.2, which language and when, closed on 2026-08-28
and is in the Retired table; the rest stand.
