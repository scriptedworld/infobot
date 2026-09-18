# infobot's own recipes. No template writes this file.
#
# What is here names this project's binaries and where they are used from, which
# is the part neither the base nor the Go layer can know.

# This tree holds two languages, so `checks` is overridden here instead of
# taken from the Go layer, which can only know its own jig. The Go half is the
# layer's two lines unchanged.
#
# The C++ gate reads cxx/, the rebuild. cpp/, the first port, is ungated and is
# deleted once cxx/ reaches parity with Go.
#
# The C++ jig neither builds nor runs tests. It reads a compilation database
# and a gcov profile, so `cxx-profile` produces both first, from an empty build
# directory: a gcov notes or counts file surviving an earlier configuration is
# read as this run's.

# all the quality tooling, both languages
checks:
    @just _verdict common-quality
    @just _verdict go-std-quality --definitions go-std-quality
    @just cxx-profile
    @just cxx-clang
    @just _verdict cpp-std-quality --definitions cpp-std-quality

fetched := justfile_directory() / ".cache/fetchcontent"

# The jig lints and checks headers across every file in the database with no
# way to exclude one, so the database the root carries holds infobot's own files
# and not the libraries FetchContent built beside them.
#
# gcovr reads every gcov profile under the repository, .ephemera included, and
# its --exclude filters the report rather than the search. A profile left by any
# other build fails the tests task, so every one outside cxx/build is removed
# first, with the stray .gcov files a failed gcovr run leaves at the root.

# the cxx suite, instrumented, leaving the database and profile the jig reads
cxx-profile:
    rm -rf cpp/build cxx/build compile_commands.json
    find . \( -name '*.gcda' -o -name '*.gcno' \) -not -path './.git/*' -delete
    find . -maxdepth 1 -name '*.gcov' -delete
    cmake -S cxx -B cxx/build -G Ninja -DINFOBOT_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug -DFETCHCONTENT_BASE_DIR={{fetched}}
    cmake --build cxx/build
    jq '[.[] | select(.file | contains("/.cache/fetchcontent/") | not)]' cxx/build/compile_commands.json > compile_commands.json
    cxx/build/infobot-tests
    @just _cxx-entrypoint

# the cxx suite built with Clang and run, beside the GCC build the jig reads
cxx-clang:
    rm -rf cxx/build-clang
    cmake -S cxx -B cxx/build-clang -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DFETCHCONTENT_BASE_DIR={{fetched}}
    cmake --build cxx/build-clang
    cxx/build-clang/infobot-tests

# No test process reaches main(), so the instrumented binaries run once on a
# payload and their counts join the profile, which is hard rule 5. Everything
# they could touch points at a scratch directory.
_cxx-entrypoint:
    #!/usr/bin/env bash
    set -euo pipefail
    scratch=$(mktemp -d)
    trap 'rm -rf "$scratch"' EXIT
    printf '%s' '{"session_id":"entrypoint","model":{"display_name":"Opus 5"}}' \
        | env HOME="$scratch" XDG_CONFIG_HOME="$scratch" XDG_STATE_HOME="$scratch" \
            TMUX= HERDR_PANE_ID= cxx/build/statusline > /dev/null
    printf '%s' '{"session_id":"entrypoint"}' \
        | env XDG_STATE_HOME="$scratch" cxx/build/forget 2> /dev/null

# Two binaries, and the committed shims beside them are what Claude Code
# invokes. The shim is the committed half and the binary is not, which is
# FR-1.13 rather than a packaging preference: a build that has not run and a
# symlink that dangles both fail as a blank line nobody is told about, so the
# thing `settings.json` names has to be present and able to look.

# content parity with Go for a CMake tree's binaries, `just parity cpp` for the
# first port
parity tree="cxx" *flags="":
    @just build
    cmake -S {{tree}} -B .ephemera/parity/build-{{tree}} -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
    cmake --build .ephemera/parity/build-{{tree}} --target statusline forget > /dev/null
    python3 scripts/parity.py --other .ephemera/parity/build-{{tree}}/statusline --other-forget .ephemera/parity/build-{{tree}}/forget {{flags}}

# the checker's own test: Go against itself and against two changed copies
parity-self-test:
    @just build
    python3 scripts/parity.py --self-test

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
