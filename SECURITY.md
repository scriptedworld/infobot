# Security

## What this program touches

infobot is a status line. Claude Code runs it on every event, hands it a JSON
payload on stdin, and prints the two rows it writes to stdout.

It reads:

- the session payload on stdin, which carries the working directory, the model,
  context usage and, for subscribers, rate-limit windows
- the session's transcripts under `~/.claude/projects/`, its subagents'
  included, to count what a session has spent
- its own configuration under `~/.config/infobot/`: the rate table, the palette
  and the layout
- its own state files under `~/.local/state/infobot/`

It writes, per session, under `~/.local/state/infobot/`:

- `<session>.status.yaml`, mode 0644, the context state other programs read
- `<session>.json`, mode 0600, its offsets into the transcripts

It makes no network calls and opens no ports. It runs one subprocess, `tmux` or
`herdr`, to ask the multiplexer that owns the pane how wide it is, and only when
that multiplexer's environment variable is set.

## What that means for you

**The transcript it reads is your conversation.** infobot parses it for token
counts and does not copy its content anywhere, but a bug that logged what it
read would leak whatever you had been discussing. That is the failure to report
fastest.

The status file is readable by other users on this machine. It carries the
working directory, the model name, context numbers and a session id. Nothing it
holds is a credential, and the session id is not one. Whether it should be 0600
is an open question in `NEXT_STEPS.md`.

Nothing sanitises the payload before it is rendered. The strings it prints come
from the harness. A terminal-escape sequence arriving in a field would be
printed, which is a real class of problem for any status line; this one has
fixtures pinning its escape handling, not an argument that it is safe.

## Reporting

Open an issue. If it is the transcript-leak class above, or anything that
causes infobot to execute something other than the width query, say so in the
title and leave the detail out of a public issue until it is fixed.

There is no release process and no advisory feed, so a fix is a commit on
`main` and the issue is where it is explained.

## What is not defended

**Anything that already has your permissions.** infobot reads files you can
read and writes files you own. It is not a boundary, and a process that could
tamper with its state file could already read your transcripts directly.
