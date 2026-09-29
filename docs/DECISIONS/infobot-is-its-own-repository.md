# infobot is its own repository

The status line lived in `silo` and moved out into this tree.

## Why

silo holds the standing rules, the settings, the hooks and the written record:
things a person reads. infobot is a program, with its own requirements, its own
gate and its own tests, and it runs on every Claude Code event. Those want a
gate that reads code.

Keeping it in silo showed the gap: silo's gate never read the status line at
all, because lizard selects by file extension and the script had none.

## What it did not fix

The split changed which gate runs, not what the gate can read. Every checker
`just checks` invokes still selects by extension, so the two shell shims,
`bin/infobot` and `bin/forget-session`, are read by none of them: lizard reads
every `.go` file and neither shim, and the suppression register globs `*.go`.

Their behaviour is tested even so. Five cases in `cmd/statusline/shim_test.go`
run the committed shim and cover FR-1.13 both ways, FR-3.8, FR-1.9 and
FR-1.11f. What is unread is the text: a suppression pragma in shell would go
unregistered, and a defect on a path those five do not walk would go unseen.
`gate/10` tracks it, waiting on toolbox's shell jig.

## What stays in silo

Claude Code's settings file, which names `bin/infobot` by absolute path. It is
Claude Code's configuration, and infobot does not read it.
