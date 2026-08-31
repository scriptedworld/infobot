# Security

## What this program touches

infobot is a status line. Claude Code runs it on every event, hands it a JSON
payload on stdin, and prints the two rows it writes to stdout.

**It reads:**

- the session payload on stdin, which carries the working directory, the model,
  context usage and, for subscribers, rate-limit windows
- the session transcript named in that payload, to count what a session has
  spent
- its own state files under `~/.local/state/infobot/`

**It writes:**

- two rows on stdout
- one state file per session under `~/.local/state/infobot/`, mode 0600

**It makes no network calls**, opens no ports, and executes nothing.

## What that means for you

**The transcript it reads is your conversation.** infobot parses it for token
counts and does not copy its content anywhere, but a bug that logged what it
read would leak whatever you had been discussing. That is the failure worth
reporting fastest.

**The state file is on your disk and readable by you.** It carries the working
directory, the model name, context numbers and a session id. Nothing it holds
is a credential, and the session id is not one.

**Nothing sanitises the payload before it is rendered.** The strings it prints
come from the harness. A terminal-escape sequence arriving in a field would be
printed, which is a real class of problem for any status line, and this one has
fixtures pinning its escape handling rather than an argument that it is safe.

## Reporting

Open an issue. If it is the transcript-leak class above, or anything that
causes infobot to execute something, say so in the title and leave the detail
out of a public issue until it is fixed.

There is no release process and no advisory feed, so a fix is a commit on
`main` and the issue is where it is explained.

## What is not defended

**Anything that already has your permissions.** infobot reads files you can
read and writes files you own. It is not a boundary, and a process that could
tamper with its state file could already read your transcripts directly.
