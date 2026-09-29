# Measuring an entry point with go build -cover

## The problem

Coverage is judged per file, and `main.go` is one delegating call that no test
reaches: nothing in a test process calls `main()`. It reads 0% and fails the
floor. Excluding the file would settle the number and drop the guarantee that
one well-tested file cannot carry an untested one, so it is measured instead.

## How

Build the command with coverage instrumentation, run it once the smallest way
that walks `main()`, and merge its profile with the test profile.

    go build -cover -covermode=atomic -coverpkg=./... -o "$work/cmd" ./cmd/cmd
    printf '<payload>' | GOCOVERDIR="$covdata" "$work/cmd" >/dev/null
    go tool covdata textfmt -i="$covdata" -o="$profile"

Four details matter:

- `-covermode` must match the test run's, or the merge fails.
- Every binary built this way writes into the same `GOCOVERDIR`, and one
  `textfmt` covers them all.
- The run must not touch real state. Move every directory the program writes,
  here `XDG_STATE_HOME`, into the work directory, or the run leaves files
  behind that name sessions which never existed.
- An empty conversion leaves a profile with no mode line, which merges as
  broken. Write `mode: atomic` alone in that case.

## Where it runs

`scripts/cover-entrypoint.sh` does this for both binaries. The Go jig calls it
through its `{entrypoint}` placeholder, which `bolt.go-std-quality.definitions.yaml`
fills, and the adapter merges the result with the suite's profile.
