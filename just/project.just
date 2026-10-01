# infobot's own recipes. No template writes this file.
#
# What is here names this project's binaries and where they are used from, which
# is the part neither the base nor the Go layer can know.

# scripts/parity.py compares Go's output with any other implementation's,
# given its two binaries. None is kept, so only the checker's own test runs.

# the checker's own test: Go against itself and against two changed copies
parity-self-test:
    @just build
    python3 scripts/parity.py --self-test

# Two binaries, and the committed shims beside them are what Claude Code
# invokes. The shim is the committed half and the binary is not, which is
# FR-1.13 rather than a packaging preference: a build that has not run and a
# symlink that dangles both fail as a blank line nobody is told about, so the
# thing `settings.json` names has to be present and able to look.

# the two binaries, beside the shims that exec them
build:
    go build -o bin/statusline ./cmd/statusline
    go build -o bin/forget ./cmd/forget-session

# Committed is not deployed. Every other check (a clean tree, a passing suite, a
# green gate) reads the source, and the built artefact is downstream of all of
# them and inside none.
#
# The status line has no failure output, so a binary commits behind a clean
# tree, a broken one and a quiet session all look the same.
#
# Bytes, not mtimes, because a build sets a fresh mtime whatever it
# produced, so a timestamp answers "was this written" and never "is this
# current".

# rebuild, and prove the binaries in bin/ match their source
install: build
    @fresh=$(mktemp -d); \
    go build -o "$fresh/statusline" ./cmd/statusline; \
    go build -o "$fresh/forget" ./cmd/forget-session; \
    ok=1; \
    for b in statusline forget; do \
        cmp -s "$fresh/$b" "bin/$b" || { ok=0; echo "bin/$b differs from a fresh build"; }; \
    done; \
    rm -rf "$fresh"; \
    [ "$ok" = 1 ] && echo "bin/statusline and bin/forget are current" || exit 1

# The binaries are built in the tree and reached through the committed shims,
# deliberately rather than by omission: a symlink in dotfiles/bin cannot report
# its own target's absence, which is what FR-1.13 exists to prevent.

# nothing is distributed
dist:
    @echo "infobot is used from its own tree through bin/infobot"

# The binaries are not build output: they are deployed. Claude Code runs
# `bin/infobot` on every event in every live session, so removing them stops the
# status line across the whole machine and every session's state file stops
# being written with it. The shim says so in every pane (FR-1.13), but the
# instruments reading the state files go quiet, not loud;
# `docs/LESSONS/an-instrument-that-goes-blind-quietly.md` has the time it
# happened.
#
# So clean removes run directories, which are genuinely disposable, and rebuilds
# rather than leaving a hole.

# remove past run directories and tool caches, and leave the deployed binaries built
clean:
    rm -rf .bolt-*/ .bolt*/
    find . -name .cache -type d -prune -exec rm -rf {} +
    @just build
