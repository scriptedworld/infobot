# infobot, Requirements

What must be true of the status line. One file per requirement,
`<category>/FR-<id>-<slug>.md`, each holding its row.

Derived from `bin/infobot` and the payload shape read out of `claude` 2.1.233.

Requirements are stated as observable properties: what is true of a run, not
how anything is arranged. The reasoning behind a row lives in `docs/DECISIONS/`
and `docs/LESSONS/`, and how the program meets it in `docs/SPEC.md`.

A status marker says where a row comes from. `[A]` traces to something I said,
`[D]` is derived from one, and `[A/D]` is both. `[?]` is an open question
carrying no test yet.

A number is an identifier, not a position: nothing is renumbered and nothing is
reused, which is why section 4 keeps the number it was first given. A lettered
id is a facet of the row it hangs off, and a requirement in its own right with
its own test.

## Retirement is a filename

    what-it-reads-and-writes/FR-1.12-standard-library-only.retired

A retired requirement keeps its category, and the row inside it keeps the id
registered, so declaring it again is caught. The file says when it went and
what replaced it.

## Every settled row carries a test

Every test names the requirement it discharges, and the gate's `traceability`
task fails a settled row no test cites, or a test citing an id this tree does
not declare:

    python3 bin/test-traceability.py --requirements docs/REQUIREMENTS .

`just checks` passes the path through toolbox's
`bolt.requirements-directory.definitions.yaml`, linked in beside the jigs.
The `[?]` rows are reported as context and not failed.
