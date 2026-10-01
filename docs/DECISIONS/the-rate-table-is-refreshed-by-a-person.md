# The rate table is refreshed by a person, through the coordinator

`~/.config/infobot/pricing.json` is the only source of the rates the cost
segment uses (FR-8.27). Nothing in infobot refreshes it. The coordinating
session does, at most three days apart, reading the pricing page itself.

The declaration it follows is the three-line block in `docs/PROJECT.md`,
"Perishable: the pricing table". That block is an interface: the coordinator
runs it as written, so changing it changes somebody else's behaviour and is
announced the way a change to the state file's form is.

## Why a person and not a fetch

The page carries more than the table. Sonnet 5 launched at an introductory
$2/$10 with a rise to $3/$15 booked for September; the rise was then cancelled
and the introductory rate became the standard one. The cancellation was a
sentence beside the table. A fetch reading only the table could not tell a
cancelled increase from one not yet applied, at any interval, so the argument is
about what is read and not about how often.

## Why the coordinator and not every session

Filing the refresh against `/grok` would run it at the start of every session
and after every clear, in every project. That would point a dozen
sessions at one file and one web page at once, none aware of the others and
none holding a lock. There is one coordinator, so it is the single writer.

The status line itself cannot do it: it is a formatter that runs on every event
and makes no network call (FR-1.6).

## Why three days

A one-day window does not hold: under one, the file was found five days stale.
The refresh needs a person, which is what makes a tight window expensive, so
the window is set where it will be kept.

The cost of a stale rate is small and bounded. The figure is a counterfactual on
a subscription, what these tokens would have cost through the API, so a wrong
rate misprices a number nobody is billed for. The file found five days stale
needed no change when it was checked against the page.

## Why nothing is compiled in

A seed compiled into the binary to stand in when the file is missing goes stale
unnoticed, and a price change means a rebuild. With the file as the
only copy, a price change is an edit, and a host with no usable file renders
with no cost segment (FR-8.28), which is visible. The lesson is
`docs/LESSONS/a-fallback-copy-of-perishable-data-goes-stale-unseen.md`.
