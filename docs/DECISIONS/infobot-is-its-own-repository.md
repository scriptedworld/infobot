# infobot is its own repository

The status line lived in `silo` and moved out into this tree.

## Why

silo holds the standing rules, the settings, the hooks and the written record:
things a person reads. infobot is a program, with its own requirements, its own
gate and its own tests, and it runs on every Claude Code event. Those want a
gate that reads code.

Keeping it in silo showed the gap: silo's gate never read the status line at
all, because lizard selects by file extension and the script had none.

## What it did not fix on its own

The split changed which gate runs, not what the gate can read. A checker that
selects by extension reads neither shell shim, `bin/infobot` or
`bin/forget-session`, so the same gap existed here as in silo until a checker
chose files another way.

toolbox's shell jig is that checker. It picks files by extension, startup-file
name or shebang, so `just checks` runs shellcheck and shfmt over both shims and
the scripts, and the suppression register finds a shell pragma by shebang too.
Their behaviour is also tested: five cases in `cmd/statusline/shim_test.go` run
the committed shim and cover FR-1.13 both ways, FR-3.8, FR-1.9 and FR-1.11f.

## What stays in silo

Claude Code's settings file, which names `bin/infobot` by absolute path. It is
Claude Code's configuration, and infobot does not read it.
