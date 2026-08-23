# A session is more than one transcript

Reading a session's token usage looks like reading one file named after the
session id. It is not, and the two ways it is not each cost about half the
figure.

## Subagents bill in their own transcripts

FACT 2026-08-23: a session's own transcript is
`~/.claude/projects/<slug>/<session-id>.jsonl`, and each subagent it spawns
gets `<session-id>/subagents/agent-*.jsonl` beside it. Measured on one silo
session:

    main transcript    output  2,173,056   cache read 1,028,259,817
    26 subagent files  output  2,283,057   cache read   508,464,060

The subagents were 51% of the output tokens and 33% of the cache reads.
Counting only the file named after the session halves the bill.

## `/clear` opens a new transcript under a new id

FACT 2026-08-23: it does not continue the existing one. Measured on the bolt
session that was cleared at 19:53Z:

    5d7ac158-...jsonl   4011 records, last at 2026-08-23T19:51:10Z
    e0332753-...jsonl     58 records, first at 2026-08-23T19:53:47Z

The new transcript's first user record is the `/clear` command itself. Its
`sessionId` is the new id on every record.

**This is not what compaction does**, and the two are easy to conflate. A
compaction boundary sits INSIDE one transcript: silo's `ee068845` carries
`{"type": "system", "subtype": "compact_boundary"}` at record 3970 with 1,335
usage records before it and 968 after, all under one `sessionId`. Usage carries
straight through a compaction and does not carry through a clear.

## Two ids, pointing opposite ways

FACT 2026-08-23: every record carries `sessionId`, the transcript's OWN id.
Records after a clear ALSO carry a snake_case `session_id` naming the session
they came from. In `e0332753` that field is absent until record 18 and then
holds `5d7ac158` for the rest of the file.

So the link exists but points backwards, and which id you are handed decides
which half you can see. Grouping transcripts by ROOT solves it: a transcript's
recorded origin where it has one, its own name where it does not. Every
transcript sharing a root is one session's spending, whichever id was asked
about. `infobot/usage.py` does this.

CLAIM 2026-08-23, unverified: the status line payload's `session_id` after a
clear is the new session's id rather than the original. Not measured, because
no cleared session was rendering at the time. The root grouping makes it moot,
which is why it was left unmeasured rather than guessed at.

## What it cost

Two rewrites of the same function. The first counted one file. The second
counted subagents but followed only one side of a clear. Both looked correct in
testing, because a session with no subagents and no clear is the case you reach
for first.

The lesson under the lesson: a transcript is a file, and a session is a set of
them. Ask what the set is before reading any of it.
