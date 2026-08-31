# Contributing

This is a personal tool that happens to be readable. Issues and patches are
welcome; there is no roadmap to fit into and no response-time promise.

## Build and check

Go 1.26 and `just`. The gate also needs `bolt` and a checkout of `toolbox`
beside this one, which is what supplies the shared jigs.

    just build          the binary into bin/
    just test           the suite
    just coverage       the suite with a coverage profile
    just checks         everything the gate runs
    just format         gofmt

`just checks` is what has to pass. It runs the shared common-quality and
go-std-quality jigs through bolt, and reads bolt's result file rather than its
exit status, because bolt exits 0 on a run that wrote no result.

**Without toolbox beside this repository, `just checks` cannot run.** `just
test` and `just build` do not need it.

## What a change has to carry

**A test that names the requirement it discharges**, as a comment directly
above it:

    // COVERS: FR-1.11o | regression

Kinds are `positive`, `negative`, `edge`, `property`, `regression`. The gate
fails a test citing a requirement `REQUIREMENTS.md` does not define, and fails
a settled requirement no test cites.

**A requirement first, if the change adds behaviour.** `REQUIREMENTS.md` is
what the tests are written against, and a change that alters what the status
line emits also needs `docs/PROJECT.md` to still be true afterwards.

## The one thing to know before changing the output

**The emitted form is a published interface.** `bin/board` in another
repository greps the state file this writes, one key to a line with quoted
keys, and that dependency is recorded here as FR-1.11o. Changing the shape of
the state file is an announcement, not a refactor.

The rendered rows are freer, but the escape handling is pinned by fixtures at
both ends: a width table generated from the renderer's own measurement rather
than reimplemented, and a golden corpus captured before the Go port that both
the port and everything after it are checked against.

## Style

`gofmt`, and comments that say why rather than what. A comment that restates
the line below it is noise; a comment naming the bug that produced the line is
the reason the line survives.

Commit messages are conventional commits and say what changed, not how it was
decided.
