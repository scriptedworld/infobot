# Classify, then count

## The problem

A count of files, processes or records compared against an expectation looks
like evidence, and a surprising count invites a conclusion. The count cannot say
why any one item is there, so the conclusion is about the expectation as much
as about the items.

## How

1. Fix an instant, `AS_OF`, and print it beside the result. Decide every class
   against absolute timestamps, never against "within the last hour", so two
   runs naming the same instant agree whenever they are made.
2. Give every item a class from questions it can answer about itself: when was
   it made, relative to the change being judged; is the thing it belongs to
   still running; did the measurement itself create it.
3. Count the classes. The finding is the class nobody can explain. If it is
   empty, there is nothing to find, whatever the total.

Three traps the classes exist to catch:

- **A boundary inside the set.** Items made before the change being judged
  cannot be evidence about it.
- **The wrong denominator.** The total means nothing until it is set against
  the right population, and an estimate from memory is not one.
- **The observer's own items.** A probe that writes state leaves some of what
  it then counts. Isolate every directory a probe writes, and audit that by
  listing the real directory before and after, not by searching the probe's
  text.

## Here

`docs/LESSONS/a-pile-of-files-is-not-evidence-of-a-broken-cleanup.md` is the
case this came from: 27 state files read as a cleanup that never ran, and the
classes were 10 older than the hook, 7 live, 3 fixtures and 0 unexplained.
