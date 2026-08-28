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

It came out of `silo`, which holds the standing rules, the settings, the hooks
and the written record: things a person reads. infobot is a program, with its
own requirements, its own gate and its own tests. Keeping it in silo showed:
silo's gate never read `bin/statusline` at all, because lizard selects by file
extension and the script had none.

**That gap is still open here, and the split did not close it.** What moved was
which gate runs, not what it can read. All three checkers `make gate` invokes
select by Go extension, so the 77 lines of shell in `bin/infobot` and
`bin/forget-session` are read by none of them. Measured 2026-08-28: lizard read
26 of 26 `.go` files and 0 of 2 shims; `suppression-register.py:87` globs
`*.go`.

**Their behaviour is tested even so**, and the distinction matters. Five cases in
`cmd/statusline/shim_test.go` execute the committed shim and carry `COVERS:`
marks for FR-1.13 both ways, FR-3.8, FR-1.9 and FR-1.11f. What is unread is the
text, so what is genuinely exposed is a suppression pragma in shell going
unregistered, and any defect on a path those five do not walk.

It is `clank/tasks/infobot/gate/10-*`, blocked on toolbox's shell jig, and
neither `shellcheck` nor `shfmt` is installed on this machine yet.

## Layout

    bin/infobot        a shell shim. Committed, and execs bin/statusline.
    bin/forget-session the same, for the SessionEnd hook, execs bin/forget.
    bin/statusline     the built binary. Gitignored.
    bin/forget         the built cleanup. Gitignored.
    cmd/               two entry points, one delegating call each
    internal/          the program: render, state, usage, pricing, payload, num
    Makefile           build, test, cover, gate
    README.md          the front door: what it is, how to build and wire it
    LICENSE, NOTICE    Apache 2.0
    REQUIREMENTS.md    what must be true of it
    NEXT_STEPS.md      what is not done
    docs/PROJECT.md    this file

`README.md` is for somebody arriving at the repository, and this file is for
somebody working in it. Where they cover the same ground the README states the
instruction and this file states the reason, so the install steps live there and
why the shim is the committed half lives here.

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

Adopting it is `clank/tasks/infobot/jig-adoption/10-adopt-the-go-jig.planning`,
which carries what running each of its checks by hand turned up: coverage
already clears the per-file bar, and `golangci-lint` reports 123 issues that
have to be decided rather than silenced.

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
