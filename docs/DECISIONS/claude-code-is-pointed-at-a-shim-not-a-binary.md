# Claude Code is pointed at a shim, not a binary

Claude Code's settings name `bin/infobot`, a committed shell script, and the
script execs `bin/statusline`, which is built in the tree and gitignored. The
committed half is the shim and the built half is not.

## Why

A status line has one tell: it is drawn or it is not. A binary that has not
been built, and a symlink whose target is gone, both fail as a blank line, and a
blank status line is indistinguishable from a quiet session. Nobody is told.

So the thing the settings name has to be something that is always there and can
look. The shim is committed, so a fresh clone has it; it looks for the binary
beside it, and when there is none it prints a row saying so and how to build
it. That is FR-1.13.

The same holds for `bin/forget-session`, the `SessionEnd` hook, with one
difference: a hook has no line to occupy, so an absent binary there says
nothing, and the state files it did not remove are the evidence.

## What was weighed

Other tools in this estate (`bolt`, `converge`, `update`) are reached through a
symlink in `dotfiles/bin`. That works for a command a person types, who sees the
error. For the status line it is the failure above: a symlink cannot report its
own target's absence, and neither can a missing binary.

A binary committed to the tree was not considered: it would go stale against its
source with nothing to say so, which `just install`'s byte comparison exists to
catch.

## What it costs

The shim is one `readlink` and one `exec` per render. How it is built, including
why it resolves its own symlinks first, is
`docs/PATTERNS/a-shim-reports-its-own-binarys-absence.md`.

The shell is read by no checker in the gate, since every checker selects by
file extension. Its behaviour is tested by `cmd/statusline/shim_test.go`, which
runs the committed shim; `gate/10` tracks giving it a reader.
