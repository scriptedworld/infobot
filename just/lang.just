# The Go layer. Everything true of a Go CLI and nothing true of one project.
#
# A LANGUAGE TEMPLATE OWNS THIS FILE and writes nothing else. It is here because
# no template exists yet, and it is written to be lifted into one.
#
# WHAT IS AND IS NOT GENERIC. `./...` and `./cmd/...` are Go's own spellings and
# hold for any module, so build, test, format and format-check need no project
# values. `install` and `dist` do: they name binaries, and a project's binaries
# and where they are used from are its own, so those two are left to the base
# form here and a project overrides them in `just/project.just`.
#
# THE JIG IS RUN THROUGH BOLT rather than by listing its tools here, so a change
# to the shared standard reaches this project without editing this file.
#
# `--definitions` IS NOT OPTIONAL AND NOT IMPLIED BY THE FILE EXISTING. Without
# it the shared jig's default stands, `entrypoint: 'true'`, and the coverage
# task measures a no-op where the project's script belongs. It fails rather than
# passing, since the entry points then read 0.0%, but the message names coverage
# rather than the invocation.

# Cheapest first, so a failure surfaces before the slow checks run.
#
# TWO JIGS AND NOT THREE. common-quality composes the secrets jig as a task of
# its own, so `checks` reaches it by construction. Listing `bolt secrets .` here
# as well would run detect-secrets and gitleaks over the whole history twice for
# one verdict. The `secrets` recipe stays for running that piece alone.

# all the quality tooling
checks:
    bolt common-quality . --result-to-exitcode
    bolt --definitions go-std-quality go-std-quality . --result-to-exitcode

# Race and shuffle are the jig's, and are here too so that running the piece
# alone asks the same question the gate does.

# the suite
test:
    CGO_ENABLED=1 go test -race -shuffle=on ./...

# The jig is what enforces the minimum: per file at 80%, through an adapter,
# including the entry points a test process cannot reach. `go test -cover` here
# instead would print numbers and fail on nothing.

# the suite with coverage, and the minimum enforced
coverage:
    bolt --definitions go-std-quality go-std-quality . --result-to-exitcode

# gofmt lists unformatted files on stdout and exits 0 whatever it finds, so its
# exit status answers "did gofmt run" and never "is this formatted". `test -z`
# on its output is what makes this a check.

# formatting verified, nothing written
format-check:
    @out="$(gofmt -l .)"; \
    if [ -n "$out" ]; then echo "not gofmt-clean:"; echo "$out"; exit 1; fi

# formatting written
format:
    gofmt -w .

# every command in the module
build:
    go build ./cmd/...
