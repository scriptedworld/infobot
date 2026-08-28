# The build. `bin/infobot` and `bin/forget-session` are committed shell shims
# that exec the binaries this produces beside them; the binaries are gitignored.
#
# The shim exists so an absent binary SAYS SO rather than rendering a blank line
# (FR-1.13). That is the trade the Go port makes: an import that might not
# resolve became a build that might not have run, and both fail identically
# unless something is watching for it.

GOFLAGS ?= -mod=mod

.PHONY: build test gate clean

build:
	go build -o bin/statusline ./cmd/statusline
	go build -o bin/forget ./cmd/forget-session

test:
	go test ./...

# Per-file coverage, which is the bar the Go jig applies. The entry points are
# one delegating call each and the test process cannot reach them, so they are
# MEASURED rather than excluded: built with -cover, run, and their profile read.
cover:
	go test -cover ./...
	@dir=$$(mktemp -d); out=$$(mktemp -d); state=$$(mktemp -d); \
	go build -cover -o $$out/statusline ./cmd/statusline; \
	go build -cover -o $$out/forget ./cmd/forget-session; \
	printf '{"session_id":"cover","context_window":{"context_window_size":200000,"used_percentage":40}}' \
	  | GOCOVERDIR=$$dir XDG_STATE_HOME=$$state $$out/statusline >/dev/null; \
	printf '{"session_id":"cover"}' | GOCOVERDIR=$$dir XDG_STATE_HOME=$$state $$out/forget 2>/dev/null; \
	echo "entry points, measured by running them:"; \
	go tool covdata percent -i=$$dir | grep '/cmd/'; \
	rm -rf $$dir $$out $$state

# COMMITTED IS NOT DEPLOYED. Every other check reads the SOURCE: git status is
# clean because the source is committed, the tests pass because they compile the
# source, and the gate is green for the same reason. The built artifact is
# downstream of all of them and inside none.
#
# That is not hypothetical here. bin/statusline ran two commits behind a clean
# tree for 38 minutes on 2026-08-28, and the status line has no failure output,
# so a stale binary and a broken one look identical: like a quiet session.
build-current:
	@if [ ! -x bin/statusline ]; then \
	  echo "bin/statusline is not built; run make build"; exit 1; fi
	@newest=$$(find cmd internal -name '*.go' ! -name '*_test.go' -newer bin/statusline \
	    2>/dev/null; find go.mod go.sum -newer bin/statusline 2>/dev/null); \
	if [ -n "$$newest" ]; then \
	  echo "bin/statusline is older than $$(echo "$$newest" | head -1); run make build"; \
	  exit 1; fi

gate: test build-current
	lizard -C 15 -a 5 -L 60 .
	python3 bin/test-traceability.py --requirements REQUIREMENTS.md .
	python3 bin/suppression-register.py --register SUPPRESSIONS .

clean:
	rm -f bin/statusline bin/forget
