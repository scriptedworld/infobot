# Every measurement was of my tree, the conclusion about everybody else's

2026-08-29. A finding was filed against another repository, carefully measured,
correctly reasoned, and wrong by forty-eight minutes. Nothing in it was
inaccurate. Its load-bearing premise was about eight repositories, and every
experiment in it was run on one.

## What was filed

`just checks` failed because `detect-secrets` read the recipe name `secrets:` as
an assignment. The finding isolated it properly, in a scratch repository, one
file and one commit each:

    secrets:      -> Secret Keyword, at the command line
    leak-scan:    -> clean, identical command
    secrets: leak-scan -> still trips

Three probes, a control, and the right conclusion about the mechanism: it is the
name, in any position the scanner reads, and no arrangement of the recipe body
avoids it.

Then it listed three fixes and rejected all three. The rejection is the sentence
that matters:

> Renaming locally would give infobot a green gate and a different interface
> from every other project, which is worse than the red.

## What was true at the time it was written

`leak-scan` had been the estate's recipe name for forty-eight minutes. silo
renamed it at `12a5d4e`, 21:05, for exactly this reason and with exactly this
reasoning. The finding was written at 21:53.

So renaming was not a local divergence. It was the only way back into line, and
this repository was holding the last pre-rename copy of a file that is meant to
be byte-identical in eight trees. The finding recommended keeping the gate red
in order to preserve a convention that had already changed.

**The control that would have caught it was the one already in the experiment.**
`leak-scan:` was tested, as the clean case. Nobody asked why that name and not
any other, or whether anyone else was using it.

## Why the care did not help

Every measurement was sound. The scratch repository, the three probes, the
isolation of the mechanism: all of it holds up and none of it was the problem.

The conclusion had two premises. The first was about `detect-secrets`, measured
three ways. The second was **that the estate's recipe name is `secrets`**, and
that was inherited from this repository's own file, which is the one artifact in
the world that could not disconfirm it.

A premise taken from the thing under test is not evidence. It reads as evidence
because it is specific, checkable and true of the tree in front of you.

## The cost, which was paid by the next session

The finding became a line in `START_HERE.md`: *`just checks` is red and it is
not your code.* The next session read that, verified the collision was real,
confirmed it was attributed elsewhere, and moved on to other work. The
verification succeeded and confirmed the wrong half.

One command would have settled it, and it is the same command whichever
direction the drift ran:

    grep -n '^leak-scan:\|^secrets:' ~/.projects/*/just/base.just

Two of three adopters answered `leak-scan` that night. All three do now, this
one having been the third, so run it rather than reading the count here.

## What to do

**Before filing against another tree, read that tree.** Not the decision that
governs it, not your copy of the shared file, the tree. Filing is allowed
anywhere and costs nothing; the entry is where a wrong premise gets preserved in
somebody else's queue.

**Name the premise that is not about your repository, and check that one
first.** A finding usually has exactly one, and it is usually the one the
recommendation turns on rather than the one the experiments cover.

**When a fix is rejected because it would diverge from a convention, verify the
convention as of today.** That is a claim about other people's files with a
timestamp on it, and it is the most perishable thing a finding can contain.

**Ask whoever owns it, which is cheaper than any of this.** The rename had an
owner, a commit and a reason, and a message would have returned all three.

## What it is not

Not an argument against filing. The mechanism the finding established was right
and is still right, and it is why the rename exists at all. The failure was
narrow: one premise, about somebody else's tree, inherited from mine.
