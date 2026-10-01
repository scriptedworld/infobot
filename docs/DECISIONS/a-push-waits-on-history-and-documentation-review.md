# A repository pushes once its history and documentation pass review

A repository in this estate has a git remote, and a push to it waits until all
four of these hold:

1. Its gate passes, wording included, read from `result.yaml`.
2. Every document, comment and source file has had a voice review, and the
   findings are cleared rather than scored.
3. Every unpushed commit message has had the same review and is rewritten where
   it fails, `git filter-repo` included, before anything is pushed.
4. A reader opening its history or documentation would not take it for machine
   output: no em-dashes, no narration of sessions, no attribution, no label
   badges, no recaps.

The session holding the repository checks these and reports the evidence to the
coordinator, which reads it before the push goes ahead. The ruling is Q5 in
clank `tasks/silo/human-voice/20-decide-what-needs-a-human-voice`.

## Why pushes wait on the history review

The commit histories were rewritten to clean up their messages, and a rewrite
changes every SHA it reaches. Holding pushes back until the review has passed
keeps every rewrite on commits no remote holds, so none of them needs a
force-push. A remote holding old refs is a place rewritten history survives, in
a clone, a fork, a cache or a CI checkout, and any of those can put the old
messages back in front of somebody.

So a push that would need `--force`, or a rewrite that reaches a commit already
on the remote, stops and comes to me.

## Why documentation is part of the condition

These projects go up once their documentation is clean and appropriate: the
right content in the right tier, no stream of consciousness in `NEXT_STEPS.md`
or `docs/PROJECT.md`, and nothing a stranger arriving at a public repository
would have to decode.

## What follows from it for a session

An unpushed repository is a known state with a known end, and a session does
not report it as a blocker, an open decision, or work waiting on an answer. What
moves it is work on the four conditions.

Do not push before they hold, and do not take a remote that already holds
pre-rewrite history as licence to push over it. That is the force-push case,
and it comes to me.
