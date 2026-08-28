# Implementations of one spec version are one witness, counted several times

2026-08-28. I checked a question against three independent YAML parsers, got the
same answer from all three, decided on it, and was wrong. The specification
settled it in two minutes and said the opposite.

## What happened

infobot escapes control characters in its state file so a value survives the
round trip. The range was C0, DEL and C1. wrench pointed out that U+2028 and
U+2029 sit outside it and that Go escapes both.

I checked rather than adopting, which was right, and I checked the wrong thing.
Three parsers, three implementations, no shared code:

    PyYAML       U+2028 round trips raw
    go-yaml      U+2028 round trips raw
    yaml-rust2   U+2028 round trips raw

So I declined to widen the range, on the stated grounds that I would not make a
change I could not point at a failure for. That standard was correct and I still
hold it. The evidence under it was not.

## What the specification says

YAML 1.1, section 5.4, rule 27:

    b-char ::= b-line-feed | b-carriage-return | b-next-line
             | b-line-separator | b-paragraph-separator

Five characters: LF, CR, U+0085, U+2028, U+2029. YAML 1.2 rule 26 cuts the set
to LF and CR, and gives the reason:

    YAML version 1.1 did support the above non-ASCII line break characters;
    however, JSON does not. Hence, to ensure JSON compatibility, YAML treats
    them as non-break characters as of version 1.2.

So the three characters are one class, and whether they fold is a property of
the reader's version rather than of the character. PyYAML folds U+0085 and
preserves U+2028 and U+2029, which is three fifths of its own version's rule.

My range escaped U+0085 and left its two spec siblings raw. The only thing
separating them was which one PyYAML happens to fold. That is not a range, it is
an artefact of one parser's incompleteness.

## Why three parsers were not three witnesses

They were all asked, and they all answered, and they still corroborated nothing.
They agreed because they share an era rather than an argument: none of them
implements the 1.1 break set completely, so all three inherit the same gap.
**Agreement across implementations of one version is one implementation counted
three times.**

That is wrench's sharpening of a looser thing I had said, and it is better than
mine. The looser version was "three implementations agreeing is not
corroboration when none of them was asked the question", which does not cover
this case, because here all three were asked.

The test that does discriminate: would these sources disagree if the answer were
different? Three parsers of one version would not. A specification would, and it
was one fetch away the whole time.

## What to do

**Ask what would make the sources disagree before counting them.** Sources that
would fail together are one source. Implementations of a common standard,
documents copied from one another, and a claim restated across three files are
all this shape.

**Prefer the primary text when one exists and is reachable.** It cost two
minutes here against an hour of measuring parsers, and it was the only
independent witness available.

**Behaviour answers what is, and a specification answers what is permitted.** A
value that round trips through every parser you have may still be one a
conforming parser is entitled to alter. That gap is exactly where a silent wrong
answer lives.

## What it cost, and what it did not

Nothing shipped wrong: the reversal landed an hour after the first decision and
before anything depended on it. What it cost was a decision defended in writing
to a peer and then withdrawn, which is cheap, and is the reason to write the
reasoning down rather than only the conclusion.
