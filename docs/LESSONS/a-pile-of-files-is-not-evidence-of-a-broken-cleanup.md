# A pile of files is not evidence of a broken cleanup

2026-08-26. Two sessions spent an evening on a conclusion that a count made
obvious and a classification dissolved. Nothing was broken.

## What happened

A `SessionEnd` hook was installed at 11:54 to remove the two state files each
session leaves under `~/.local/state/infobot`. By evening the directory held 27
files. A second session counted them, compared against an estimate of how many
sessions had ever run, and concluded: *this is not a cleanup that sometimes
misses, it is a cleanup that has never removed anything.*

That went into a task as its premise. It was wrong.

## What the classification showed

Every id accounted for, against the hook's install time rather than against a
count of anything:

    10   predate the hook          could never have been handed to it
     7   live at the as-of         sessions still running
     3   test fixtures             probes that did not move XDG_STATE_HOME
    --
     0   unexplained

Nothing was a missed cleanup. `ps` put six of ten live claude processes at over
a day old and one at over four days, so almost nothing had ENDED since the hook
landed. The hook had had no OPPORTUNITY to run, which is a different condition
from running and doing nothing, and it has a different fix.

## The three ways the count misled

**It aggregated over a boundary that mattered.** Ten of the files were older than
the hook. No cleanup can remove a file created before it existed, so those ten
were never evidence about anything.

**It counted files against the wrong denominator.** Twenty-seven files against
"about seven sessions" is damning. Against ten live processes and 185 transcripts
it is unremarkable. The denominator was an aside from a person, hardened into a
premise because of who said it.

**Three of the files were the observer's own.** Probes that render write an
offsets file for whatever session id their payload carries. Four of them did not
move `XDG_STATE_HOME`, so fixture ids landed in the real directory naming
sessions that never existed. The measurement was polluted by the act of
measuring.

## What to do instead

Classify, then count the classes. For this directory that means, for each id:
does a transcript exist, is its newest file older than the hook, and was it
written recently enough to belong to something still running.

It was done by a `.ephemera/classify-state.py`, which was working space and is
gone. The three questions above are the whole of it, so it is cheaper to rewrite
than to have kept. A cleanup is suspect only when the unexplained class is
non-empty.

## And pin the classification to an absolute instant

The first version bucketed by "written within the last hour", which is a
property of when it ran and not of the file. The same directory gave 7 live and
0 unexplained at 22:00, and 12 live and 5 unexplained an hour later. A later
reader would have filed five anomalies that were never there.

A classification that moves under you is worse than a count, because it looks
like a finding. Take an `AS_OF`, print it beside the result, and decide every
bucket against absolute timestamps, so two runs naming the same instant agree
whenever they are made.

## The related trap, which is what put the fixtures there

A test that renders writes state for the session id in its payload. Any test
that touches `usage.totals`, directly or through `build()`, must move
`XDG_STATE_HOME` as well as any transcript root it passes. Otherwise it writes
offsets beside the real ones and the next real render skips bytes it never
counted, and the failure lands on whoever ran the tests, in their own status
line, later.

Audit that by behaviour, not by grep. Checking for the string `XDG_STATE_HOME`
in a probe reported one as isolated when the only match was an assertion LABEL.
Run everything, list the real directory before and after, and compare.
