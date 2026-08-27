# A measurement can be correct and about the wrong thing

2026-08-26. A true number, verified before use, became a false requirement. The
check that would have caught it was one line and was never run, because
verifying the claim felt like the whole job.

## What happened

Another session measured that importing wrench's Python pack fails on this
machine:

    /usr/bin/python3 -c "import wrench"
    → ModuleNotFoundError: No module named 'jsonschema'

That was offered as the reason infobot hand-emits canonical YAML rather than
using the library. Rather than take it on trust it was re-run here, and it
reproduced exactly. It then went into FR-1.11d as: *it is not imported because
it cannot be.*

**infobot does not run on `/usr/bin/python3`.** Both entry points say
`#!/usr/bin/env python3`, which on this machine reaches a mise-managed 3.14.7,
and the two interpreters do not carry the same packages:

    /usr/bin/python3         3.13.5   yaml yes   jsonschema NO    referencing NO
    env python3              3.14.7   yaml yes   jsonschema yes   referencing yes

Under the interpreter infobot actually uses, the import would have succeeded.
The requirement was true about a runtime nothing here runs on.

## Why verifying did not catch it

The measurement was checked for TRUTH and never for SUBJECT. Re-running someone
else's command confirms the command does what they said; it says nothing about
whether the thing measured is the thing the argument needs. `head -1 bin/infobot`
was the missing step and it is one line.

The failure survives good practice. Both sessions were deliberately verifying
rather than trusting, and both verified the same wrong thing, because the second
inherited the first's framing along with its number.

## What to ask of a measurement before building on it

Alongside "is this true", ask "is this about the thing my argument is about". For
a runtime claim that means: which interpreter, resolved how, at the moment the
program actually starts. For a filesystem claim, which path and whose HOME. For
a timing claim, warm or cold, and on which host.

## The better reason, which does not depend on a package being absent

FR-1.12 replaced it: infobot imports the standard library and nothing else, so
it runs under whatever `python3` resolves to rather than under one particular
interpreter. A dependency that resolves or not depending on which interpreter
wins a PATH race is what FR-1.9 already calls something that can be half
present, and for a status line it fails as a blank line rather than as an error
anyone sees.

That reason holds whether or not `jsonschema` is installed anywhere, which the
original did not. FACT 2026-08-26, measured from wrench: `python3-yaml` is an
apt package nothing manually installed, held on a dependency edge and an
autoremove candidate. The package state that made the first measurement true was
itself in motion.

## The general shape

A reason that rests on the current state of a machine expires when the machine
changes. A reason that rests on a property of the design does not. When a
measurement is doing the work of a requirement, that is usually a sign the
requirement has not been found yet.
