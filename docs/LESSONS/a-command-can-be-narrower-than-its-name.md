# A command can be narrower than its name, and carrying it does not help

2026-08-28. Earlier the same day I wrote that a relayed finding should carry the
command rather than the conclusion, because a conclusion has no seam to check.
That is right and it is not sufficient. A command carries its own claim in its
name, and the name can be wider than what it does.

## What happened

A shared secrets jig declares `detect-secrets scan --baseline .secrets.baseline`
and no adopter has that file, so it exits 2 on a usage error every run. The
question was whether committing a baseline would fix it or make it worse.

Two sessions measured it. Three probes between them. Every one was flawed.

    probe 1  mine       credential written, never committed
                        `detect-secrets scan app.py` to check it was detectable
                        result: plain scan 1, jig command 0 absorbed
    probe 2  dispatch   credential never committed
                        `app.py` passed explicitly on both runs
                        result: plain scan 3, jig command absorbed 3
    probe 3  mine       credential committed, jig's command exactly
                        result: exit 0, baseline grows 1 -> 2

`detect-secrets scan` **scans what git tracks**. The jig passes no path. So an
untracked fixture is not scanned at all, and passing a path by hand is a
different command from the one under test.

Probes 1 and 2 measured neither the tree nor the jig. They produced numbers, the
numbers disagreed with each other, and both supported the same conclusion, which
is how they survived.

## Why carrying the command did not save it

Both of us published our commands. Both were checkable. The other side read
them, and neither of us noticed, because `detect-secrets scan` reads as *scan
this project* and means *scan the files git knows about*. The seam was there and
it pointed at the wrong thing.

So the earlier rule needs its boundary written next to it: **carrying the
command makes a claim checkable against what the command does, not against what
the question was.** Where the two differ, a published command is a conclusion
with extra steps.

## What actually caught it

The two numbers disagreeing. Mine said the baseline recorded nothing, dispatch's
said it absorbed everything. Both conclusions matched; only the mechanism
differed, and chasing that difference is what exposed that neither run had
scanned anything.

**A disagreement in a detail nobody's argument depends on is worth more than
agreement on the conclusion.** It is the only signal available when both parties
are wrong in the same direction.

## What to do

**Check what the tool read, not only what it returned.** `gitleaks` says
`56 commits scanned`; `detect-secrets` says nothing about its corpus. A count of
what was examined is the cheapest guard against a check that answered a narrower
question, and a tool that does not print one deserves suspicion.

**Run the command under test, character for character.** Adding a path to prove
the fixture is detectable is a second experiment, and its result does not
transfer to the first.

**When two measurements agree on the conclusion and differ on a detail, chase
the detail.** That is the case where agreement is least informative.

## What it cost, and what it did not

Nothing was acted on. The recommendation reached from three bad probes happened
to be the right one, and it was re-derived properly before it went anywhere. The
cost was two sessions' measurement time and a corrected figure sent twice.

The rate is not a fact about either session. Both of us hit the same trap, and
one of dispatch's flawed runs was inside the command it used to catch mine. That
is evidence about how well this failure hides, which argues for the measurement
rather than against the measurer.
