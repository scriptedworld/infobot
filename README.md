# infobot

The status line Claude Code draws at the bottom of the screen. It reads the
session JSON on stdin and writes two rows: where you are, and how much of the
context window and the rate-limit windows you have spent.

    ╭─ Opus 5 (1M context) - xhigh  ~/.projects/infobot  ⌂ ~/.projects  🧠 ▰▰▰▰▰▱▱▱▱▱ 480k/1.0M (48% consumed)  ⟨c39f9245⟩
    ╰─ ⏳ 5hr ▰▰▰▱▱▱▱▱▱▱  📅 7d ▰▰▰▰▰▰▱▱▱▱                                             💵 $422  🎯 saved $2.4k

The context bar grows to fill whatever the pane leaves after the text, so it is
much longer than this in a real terminal and the sample is shortened to fit a
page. Everything else is what it renders.

One job: turn the session payload into rows. No network. One subprocess, to ask
whichever multiplexer owns the pane how wide it is, because every other route to
the width fails under Claude Code. The only files it opens are the rate table
and the session's own transcripts, both for the cost segment.

It runs on every Claude Code event, so its startup cost is paid constantly, and
that cost is why it is Go rather than the Python it replaced.

**Measure it yourself rather than taking a figure from here.** A render costs
what your transcript makes it cost, and a number with no payload beside it says
nothing:

    python3 bench/bench.py go 10 <a-transcript.jsonl> ./bin/infobot

It reports wall time per render and peak RSS, and its docstring says why it
measures fork to exit rather than the render alone. `docs/PROJECT.md` carries
the figures this project was built against, with the conditions they were taken
under.

## Requirements

Go 1.26.6 or later. Nothing else: no runtime, no libraries to install, no
network at render time.

## Build

    just build

That produces `bin/statusline` and `bin/forget`. Both are gitignored. What is
committed is the pair of shell shims beside them, `bin/infobot` and
`bin/forget-session`, which resolve symlinks, find the binary and exec it.

**The shim is the thing you point Claude Code at, never the binary.** A binary
that has not been built and a symlink that dangles both fail as a blank line
nobody is told about, and a status line has no other tell: it is drawn or it is
not, and a blank one is indistinguishable from a quiet session. The shim turns
that silence into a row saying what to run. That is FR-1.13, and it is why the
committed half is the shim and the built half is not.

## Install

Two entries in `~/.claude/settings.json`, both naming an absolute path to this
repository. Claude Code invokes them from whatever directory a session happens
to be in, so a relative path will not do.

    {
      "statusLine": {
        "type": "command",
        "command": "/absolute/path/to/infobot/bin/infobot",
        "refreshInterval": 10
      },
      "hooks": {
        "SessionEnd": [
          {
            "matcher": "*",
            "hooks": [
              {
                "type": "command",
                "command": "/absolute/path/to/infobot/bin/forget-session",
                "timeout": 10
              }
            ]
          }
        ]
      }
    }

The `SessionEnd` hook is not optional bookkeeping. Each render leaves two files
per session under `~/.local/state/infobot/`, and without the hook they
accumulate for every session that has ever run.

`refreshInterval` is in the block because Claude Code's schema takes it, and it
does not do what its name suggests. The line is drawn on events, not on a timer.
See `docs/LESSONS/the-status-line-is-called-on-updates-not-on-a-timer.md`.

Start a new session to pick it up. If the row reads `infobot is not built`, the
shim is working and `just build` has not been run.

## Configure

Nothing is required. Two paths are used if present.

**`~/.config/infobot/pricing.json`** carries the API rates the cost segment
prices a session with, and the date they were taken. It wins over the copy
compiled into the binary, which exists so that a fresh clone renders with no
config file and no network.

The rates go stale, so the file records when it was read. Refreshing it is a
person's job rather than a scheduled fetch: Claude's pricing page carries
promotional rates and announcements beside the table, and a fetch that reads
only the table cannot tell a cancelled increase from an unapplied one. That has
already happened once, to Sonnet 5.

**`~/.local/state/infobot/`** holds two files per session, written on every
render and removed by the `SessionEnd` hook.

    <session-id>.status.yaml   what the session has spent, for other readers
    <session-id>.json          transcript offsets, so a render tails rather
                               than re-reading

`XDG_CONFIG_HOME` and `XDG_STATE_HOME` are honoured.

**The `.status.yaml` form is a published interface.** It has readers outside
this repository that match anchored patterns against the quoted key, so the
quoting, one key to a line, the single space after the colon, and numbers being
bare are all load-bearing rather than tidy. Adding a key is safe. Changing the
shape is announced before it lands, which is FR-1.11o, and pinned by a test at
each end, which is FR-1.11p.

## Develop

Every project here carries the same ten recipes, so moving between a Go tree and
a Rust one means typing the same words:

    just checks        all the quality tooling, and what to run before landing
    just test          the suite
    just coverage      the suite with the 80% per-file minimum enforced
    just format-check  formatting verified, nothing written
    just format        formatting written
    just build         the two binaries
    just install       rebuild, and prove bin/ matches its source
    just dist          nothing; infobot is used from its own tree
    just clean         past run directories, and the binaries left built
    just leak-scan     the secrets jig alone

`just` on its own lists them.

**`just checks` is the whole gate and contains the others.** It runs two bolt
jigs: `common-quality` for complexity, traceability, suppressions and secrets;
and `go-std-quality` for build, format, lint, tests, tidy, vet and
vulnerabilities. The secrets jig is composed into the first rather than listed
again, so `leak-scan` is for running that piece by itself.

Those jigs and their adapters are symlinks into a sibling repository and are
gitignored, so a clone without that sibling cannot run `just checks`. `just
test`, `just build` and `just format` need only the Go toolchain and work
anywhere.

Both refuse a binary older than its source, because every other check reads the
source and the built artifact is downstream of all of them. A stale binary and a
broken one look identical from the outside, which is to say like a quiet
session.

**Every test names the requirement it discharges**, in a comment directly above
it, and the gate fails a test that cites nothing or cites a requirement
`REQUIREMENTS.md` does not define:

    // COVERS: FR-1.13 | negative

## Where things are written down

    REQUIREMENTS.md    what must be true, and the retired ids with their reasons
    NEXT_STEPS.md      what is not done
    docs/PROJECT.md    the project detail: layout, gate, what is decided
    docs/DECISIONS/    why a choice was made
    docs/LESSONS/      what a mistake cost and how to avoid repeating it

Work is tracked outside this repository, one directory per task, with state
carried as the directory's suffix.

## Known gaps

`just checks` is red on one task, `lint`. The Go jig runs `golangci-lint` with
42 analysers and it reports 152 findings, none of which may be settled with a
suppression pragma, so each is a decision rather than an edit. Most are one of
three rules asking for a house style this project does not keep. The 22 that are
its own are all `gosec`, and each is inherent rather than accidental: a file
opened by computed path, a test fixture that must land executable, or the one
subprocess that asks the terminal how wide it is. Every other task passes:
build, format, tests at 92.7% with the entry points measured, tidy, vet,
vulnerabilities, complexity, traceability, suppressions and secrets.

The two shell shims are read by no checker, because every checker selects by
file extension and the shims have none. Their behaviour is tested five ways;
their text is unread.

## Licence

Apache 2.0. See `LICENSE` and `NOTICE`.
