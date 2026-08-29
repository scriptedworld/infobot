# The build. `bin/infobot` and `bin/forget-session` are committed shell shims
# that exec the binaries this produces beside them; the binaries are gitignored.
#
# The shim exists so an absent binary SAYS SO rather than rendering a blank line
# (FR-1.13). That is the trade the Go port makes: an import that might not
# resolve became a build that might not have run, and both fail identically
# unless something is watching for it.

GOFLAGS ?= -mod=mod

.PHONY: build test check gate leakcheck clean

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

# What runs in ANY clone, needing nothing but the Go toolchain. A contributor
# without this machine's adopted tooling runs this and gets a real answer.
#
# gofmt is here rather than assumed. It lists unformatted files on stdout and
# exits 0 whatever it finds, so `test -z` on its output is what makes it a gate
# rather than a report. It caught a file that had been sitting in a green tree,
# which is the whole argument for it being a step instead of a habit.
check: build-current
	@out=$$(gofmt -l .); \
	if [ -n "$$out" ]; then echo "not gofmt-clean:"; echo "$$out"; exit 1; fi
	go vet ./...
	go test ./...

# The full gate. Everything in `check`, plus the checkers adopted from toolbox.
#
# THOSE CHECKERS ARE SYMLINKS INTO A SIBLING REPOSITORY and are gitignored: in a
# built image the same files arrive from the anvil layer, so a committed copy
# would be a third statement of one thing. A clone without that sibling
# therefore has no checkers at all, and the failure it used to give was a Python
# traceback naming a path, which reads as a broken repository rather than as a
# missing adoption.
#
# So it is reported. Same principle as FR-1.13 one level up: a thing that is not
# there says so, in terms that name what would fix it.
gate: check
	@for c in bin/test-traceability.py bin/suppression-register.py; do \
	  if [ ! -r "$$c" ]; then \
	    echo "$$c is missing."; \
	    echo "It is adopted from toolbox as a symlink and is gitignored, so a"; \
	    echo "clone without that sibling repository does not have it."; \
	    echo "Run 'make check' for everything that needs only the Go toolchain."; \
	    exit 1; \
	  fi; \
	done
	lizard -C 15 -a 5 -L 60 .
	python3 bin/test-traceability.py --requirements REQUIREMENTS.md .
	python3 bin/suppression-register.py --register SUPPRESSIONS .
	$(MAKE) leakcheck

# NAME THE CORPUS, DO NOT LET THE TOOL CHOOSE IT.
#
# `bolt secrets .` cannot run this: its detect-secrets task declares a baseline
# no adopter has, so it exits 2 on a usage error and scans nothing, and giving
# it the baseline is worse. `detect-secrets scan --baseline` is a baseline
# BUILDER by its own help text: measured 2026-08-28, it absorbs a newly
# committed credential into the file and exits 0. That is toolbox's own-gate/30
# and the file is a symlink, so it is not fixed here. This runs beside it.
#
# `detect-secrets-hook` is the half that gates. It takes filenames, which is the
# point: the corpus is stated here rather than inferred by the tool. `scan` with
# no path reads what git tracks, which is a narrower question than its name
# suggests, and an untracked fixture makes it look like it found nothing.
#
# Both commands print what they read, so a run that scanned nothing says so
# instead of passing quietly.
leakcheck:
	@files=$$(git ls-files -z | tr '\0' '\n' | wc -l); \
	if [ "$$files" -eq 0 ]; then \
	  echo "no tracked files; this check read nothing"; exit 1; fi; \
	echo "the hook over $$files tracked file(s)"
	@git ls-files -z | xargs -0 detect-secrets-hook
	gitleaks detect --no-banner --redact

clean:
	rm -f bin/statusline bin/forget
