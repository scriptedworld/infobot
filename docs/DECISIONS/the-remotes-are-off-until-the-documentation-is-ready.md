# The remotes are off until the documentation is ready

No repository in this estate has a git remote. That is deliberate, it is not an
obstacle, and it ends on a condition that has nothing to do with hosting.

## What happened, in order

The commit histories were scrubbed, to clean up the commit messages. Rewriting a
history changes every SHA in it, and the rewrite removed the remotes along the
way.

They have been left off since, on purpose. **While a repository is offline, no
copy anywhere can hold a reference to the history that was rewritten away.** A
remote that still exists is a place old refs can survive, in a clone, a fork, a
cache or a CI checkout, and any of those can put the scrubbed messages back in
front of somebody.

## What ends it

The documentation. These projects go up once their documentation is clean and
appropriate: the right content in the right tier, no stream of consciousness in
`NEXT_STEPS.md` or `docs/PROJECT.md`, and nothing a stranger arriving at a
public repository would have to decode.

So the absence of a remote is downstream of the documentation being finished. It
is not a decision waiting to be taken, not a permission to be granted, and not
something a session unblocks by asking.

## Why this is written down

**It had been explained repeatedly and recorded nowhere**, so every session that
met a repository with no remote reported it as an open question, and it was
explained again. A fact that has to be re-given on request is a fact that is not
written down.

The failure is worth naming because it is not about remotes. A state with a
reason and an end condition read, to every reader who found only the state, as a
gap. `NEXT_STEPS.md` here said "no remote, a history rewrite stripped them",
which is the mechanism and carries neither the intent nor the condition that
clears it, so it invited exactly the question it was meant to answer.

## What follows from it for a session

**Do not report a missing remote as a blocker, an open decision, or work
waiting on an answer.** It is a known state with a known end.

**Do not create a remote,** and do not treat the existence of a private remote
elsewhere as licence: three repositories in the estate were found still to have
private GitHub repos holding pre-rewrite history, which is the case this decision
exists to prevent rather than an exception to it.

**Work on the documentation instead**, which is the thing that actually moves
this.
