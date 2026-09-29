# A shim reports its own binary's absence

## The problem

Some programs fail silently when they are missing. A status line is drawn or it
is not, and a blank one looks like a quiet session. A binary that has not been
built and a symlink whose target is gone both produce that blank, and nobody is
told.

## How

Point the caller at a committed script, never at the binary. The script:

1. Resolves its own path through every symlink (`readlink -f "$0"`) before
   looking for anything beside it. Without this, a shim reached through a link
   finds the link, which is itself, and execs itself forever.
2. Execs the binary beside it when it is present and is not the script itself.
3. Otherwise drains stdin, so the caller writing into it gets no broken pipe,
   and prints one line in the place the output would have been, saying what is
   missing and what to run. It honours `NO_COLOR` for that line too.
4. Exits 0 either way, since a caller that treats a failure as an error has
   nobody to show it to.

Where there is no line to occupy, as in a hook run at session end, step 3
prints nothing, and what the program would have cleaned up is left as the
evidence.

## Why not a symlink or a committed binary

A symlink cannot report that its target is gone. A committed binary goes stale
against its source with nothing to say so.

## Here

`bin/infobot` and `bin/forget-session`, tested by running the committed shim in
`cmd/statusline/shim_test.go`. The requirement is FR-1.13, and the decision is
`docs/DECISIONS/claude-code-is-pointed-at-a-shim-not-a-binary.md`.
