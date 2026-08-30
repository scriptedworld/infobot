#!/bin/sh
# Measure the statements in the two main() functions that no test can reach.
#
# Hard rule 5: a coverage failure is never settled by excluding the file. Each
# entry point is one delegating call, unreachable from `go test` because nothing
# in a test calls it, so it is measured instead: built with -cover, run once,
# and its profile merged with the test one by the adapter.
#
# Called by the `tests` task of bolt.go-std-quality.yaml through the {entrypoint}
# placeholder, which is one argument by design, so this takes the work directory
# and nothing else. The task has already written cover-entry.out holding a bare
# mode line; this overwrites it.
#
# TWO BINARIES, ONE PROFILE. Both write into the same GOCOVERDIR and a single
# conversion covers them, because covdata merges whatever runs it finds there.
#
# XDG_STATE_HOME IS MOVED FOR BOTH. The status line writes a state file named
# for the session id on every render, and the cleanup removes one. Left alone,
# this run would drop a file into the real state directory naming a session that
# never existed, which no SessionEnd will ever be handed.
set -eu

work="${1:?the work directory is the only argument}"

profile="$work/cover-entry.out"
covdata="$work/covdata"
state="$work/state"

mkdir -p "$covdata" "$state"

go build -cover -covermode=atomic -coverpkg=./... -o "$work/statusline" ./cmd/statusline
go build -cover -covermode=atomic -coverpkg=./... -o "$work/forget" ./cmd/forget-session

# The smallest safe invocation of each. The status line needs a payload it can
# measure a window from, or it takes an early return and leaves main() partly
# unmeasured. The cleanup needs only a session id.
printf '{"session_id":"cover-entrypoint","context_window":{"context_window_size":200000,"used_percentage":40}}' |
    GOCOVERDIR="$covdata" XDG_STATE_HOME="$state" "$work/statusline" >/dev/null

printf '{"session_id":"cover-entrypoint"}' |
    GOCOVERDIR="$covdata" XDG_STATE_HOME="$state" "$work/forget" >/dev/null 2>&1

# The binaries write one coverage file per run into GOCOVERDIR, in a binary
# format, so it is converted to the text profile the adapter merges.
go tool covdata textfmt -i="$covdata" -o="$profile"

# An empty conversion leaves a file with no mode line, which merges as a broken
# profile rather than as nothing. Restore the no-op form instead.
if [ ! -s "$profile" ]; then
    printf 'mode: atomic\n' > "$profile"
fi
