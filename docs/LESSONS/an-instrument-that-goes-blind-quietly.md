# An instrument that goes blind quietly

2026-08-29. `just clean` removed `bin/statusline`, the status line stopped
across the whole machine, and for 25 minutes every coordination instrument in
the estate reported 25-minute-old numbers as current.

## What happened

The `clean` recipe carried `rm -f bin/statusline bin/forget`, inherited from the
Makefile it replaced, where it had been correct. It is not correct here: Claude
Code executes `bin/infobot` on every event in every live session, so those
binaries are **deployed**, not build output.

Run while testing Justfile recipes, it stopped all 15 state files at 20:31:11.

## FR-1.13 worked, and it made no difference

The shim did exactly what it exists for. Every pane in the estate showed

    ╶─ infobot is not built  go build -o bin/statusline ./cmd/statusline

in red, for 25 minutes, in place of the status line. That is as loud as this
program can be, in the one place a person looks.

Nobody noticed, because the instruments that matter do not read panes. `bin/board`,
the clear-list and the quiet-check all read `~/.local/state/infobot/*.status.yaml`,
and **a writer that stops leaves its last file intact.** Each one kept answering,
confidently, with numbers from before the outage.

It was found by a coordinator noticing that every session in the estate had gone
quiet in the same second, which is a shape a person spots and no instrument
checked for.

## The gap, which is in the form rather than in the scripts

The state file carries `written`. Staleness is therefore **derivable**, and not
one of the three readers reads that key. Nothing in the form makes going blind
loud, and nothing required it to: no requirement says a reader must be able to
tell a current file from a stale one, so nothing made the instruments check.

That is the ordering worth keeping. The scripts are not at fault for a check
nothing asked them to make.

## What to do

**Treat a file's freshness as part of its contract, not as something a reader
may derive.** A published form that carries a timestamp and does not say what a
reader must do with it invites exactly this.

**Where a recipe removes build output, ask whether that output is also
deployed.** `clean` now removes run directories and rebuilds. Any project whose
build output is its deployed artefact has the same trap waiting in the same
recipe; `bolt` is the next one, since `bin/bolt` is installed and gates the
estate.

**Prefer a loud absence to a quiet stale value.** An instrument that cannot
answer should say so. This one answered.

## What it cost

Twenty-five minutes in which nobody could tell who was at what context, who was
clear-ready, or who had stalled, while three tools answered as though they
could. No work was lost. The same failure is recorded in `skills/mc` from an
earlier occurrence, a board run about twenty times against an estate that had
been frozen for thirty-eight minutes, so this is the second instance of one
shape rather than a new one.
