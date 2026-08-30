# The ten recipes, in the form that applies to any project in any language.
#
# A TEMPLATE OWNS THIS FILE. It is here because no template exists yet, and it
# is written to be lifted into one unchanged.
#
# EVERY RECIPE EXISTS AND SUCCEEDS. `build` and `dist` are genuine no-ops for a
# repository holding only documents, and `just build` must never fail for the
# wrong reason, so the recipe is present and does nothing rather than absent. A
# language layer overrides whichever of these it has something real to say
# about, by being imported ahead of this file.
#
# THE BOLT CALLS ARE THE POINT. A person types `just checks` and does not need
# to know which jigs run in what order.
#
# A BLANK LINE SEPARATES RATIONALE FROM THE DESCRIPTION. `just --list` takes the
# last contiguous comment line before a recipe, so without the break it prints
# whatever fragment a paragraph happened to end on, and that list is the whole
# discovery surface of a two-word interface.

# Quality, tests, coverage, secrets and requirements coverage. Everything.
#
# The biggest full gate that can be run, and what is run before anything lands.
# The other nine recipes exist so a piece can be run alone while working; this
# one is not their sibling, it contains them.
#
# EXPECT IT RED WHERE THE TREE CANNOT YET DISCHARGE A REQUIREMENT. That is
# honest rather than broken.

# all the quality tooling
checks:
    bolt common-quality . --result-to-exitcode

# the suite
test:
    @echo "no test process in this project"

# Enforcement is the half that matters: a coverage number nothing fails on is a
# report rather than a gate.

# the suite with coverage, and the minimum enforced
coverage:
    @echo "no coverage in this project"

# Kept separate from `format` so a gate can ask the question without changing
# the tree.

# formatting verified, nothing written
format-check:
    @echo "nothing to format in this project"

# formatting written
format:
    @echo "nothing to format in this project"

# may be a no-op
build:
    @echo "nothing to build in this project"

# whatever this language or runtime distributes
dist:
    @echo "nothing to distribute in this project"

# AN INSTALLED ARTEFACT IS DOWNSTREAM OF EVERY CHECK. Nothing rebuilds it, so a
# forgotten reinstall gates green on an old binary. Where a language layer
# implements this it compares BYTES rather than mtimes, because a copy sets a
# fresh mtime whatever it copied.

# put the built artefact where it is used from
install:
    @echo "nothing to install in this project"

# A STALE TREE IS A LIVE FAILURE rather than untidiness: bolt refuses an output
# directory that already holds a run, and `.bolt-*` accumulates one per run. The
# stamp format changed with the Rust build, so both spellings go.

# remove build output and past run directories
clean:
    rm -rf .bolt-*/ .bolt*/

# common-quality is settled to run this too, so `checks` reaches it by
# construction rather than by every project remembering a second call. This
# recipe stays for running the piece by itself.

# the secrets jig
secrets:
    bolt secrets . --result-to-exitcode
