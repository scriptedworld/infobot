# infobot's own recipes. No template writes this file.
#
# What is here names this project's binaries and where they are used from, which
# is the part neither the base nor the Go layer can know.

# THIS TREE HOLDS TWO LANGUAGES, so `checks` is overridden here rather than
# taken from the Go layer: the layer can only know its own jig. The Go half is
# the layer's two lines unchanged.
#
# The C++ jig does not build and does not run tests, by its own contract. It
# reads a compilation database and a gcov profile, so `cpp-profile` produces
# both before the jig is asked about them.
#
# FROM AN EMPTY BUILD DIRECTORY, every time. gcov leaves a notes file per object
# at compile time and a counts file per object at run time, and either one
# surviving from an earlier configuration is read as this run's. Measured
# 2026-09-14: after the test sources stopped being instrumented, their notes
# files outlived the change and the jig failed both test files at 0% of lines.

# all the quality tooling, both languages
checks:
    @just _verdict common-quality
    @just _verdict go-std-quality --definitions go-std-quality
    @python3 cpp/tools/check_tables.py
    @just cpp-profile
    @just _verdict cpp-std-quality --definitions cpp-std-quality

# the C++ suite, instrumented, leaving the database and profile the jig reads
cpp-profile:
    rm -rf cpp/build
    cmake -S cpp -B cpp/build -G Ninja -DINFOBOT_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
    cmake --build cpp/build
    ln -sfn cpp/build/compile_commands.json compile_commands.json
    cpp/build/infobot-tests
    @just _cpp-entrypoint

# THE ENTRY POINT IS MEASURED, NOT EXCLUDED, which is hard rule 5. No test
# process reaches main(), so the instrumented binary is run once on a payload
# and its counts join the profile. Everything it could touch is pointed at a
# scratch directory, so it reads no real transcript and leaves no file behind.
_cpp-entrypoint:
    #!/usr/bin/env bash
    set -euo pipefail
    scratch=$(mktemp -d)
    trap 'rm -rf "$scratch"' EXIT
    printf '%s' '{"session_id":"entrypoint","model":{"display_name":"Opus 5"}}' \
        | env HOME="$scratch" XDG_CONFIG_HOME="$scratch" XDG_STATE_HOME="$scratch" \
            TMUX= HERDR_PANE_ID= cpp/build/statusline > /dev/null
    printf '%s' '{"session_id":"entrypoint"}' \
        | env XDG_STATE_HOME="$scratch" cpp/build/forget 2> /dev/null

# Two binaries, and the committed shims beside them are what Claude Code
# invokes. The shim is the committed half and the binary is not, which is
# FR-1.13 rather than a packaging preference: a build that has not run and a
# symlink that dangles both fail as a blank line nobody is told about, so the
# thing `settings.json` names has to be present and able to look.

# the two binaries, beside the shims that exec them
build:
    go build -o bin/statusline ./cmd/statusline
    go build -o bin/forget ./cmd/forget-session

# COMMITTED IS NOT DEPLOYED. Every other check reads the source: the tree is
# clean because the source is committed, the tests pass because they compile the
# source, and the gate is green for the same reason. The built artefact is
# downstream of all of them and inside none.
#
# Not hypothetical. `bin/statusline` ran two commits behind a clean tree for 38
# minutes on 2026-08-28, and the status line has no failure output, so a stale
# binary and a broken one look identical: like a quiet session.
#
# BYTES RATHER THAN MTIMES, because a build sets a fresh mtime whatever it
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

# THE BINARIES ARE NOT BUILD OUTPUT, THEY ARE DEPLOYED. Claude Code runs
# `bin/infobot` on every event in every live session, so removing them stops the
# status line across the whole machine and every session's state file stops
# being written with it.
#
# Measured 2026-08-29: `just clean` during Justfile work removed them, and all
# 15 state files stopped at 20:31:11. FR-1.13 worked exactly as designed, the
# shim rendering "infobot is not built" in every pane, but the instruments that
# read the state files went quiet rather than loud and reported the estate as
# idle for 25 minutes.
#
# So clean removes run directories, which are genuinely disposable, and rebuilds
# rather than leaving a hole. `just clean` must not be the command that takes
# the status line down.

# remove past run directories and tool caches, and leave the deployed binaries built
clean:
    rm -rf .bolt-*/ .bolt*/
    find . -name .cache -type d -prune -exec rm -rf {} +
    @just build
