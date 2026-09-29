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

The refresh was first filed against `/grok`, which runs at the start of every
session and after every clear, in every project. That would point a dozen
sessions at one file and one web page at once, none aware of the others and
none holding a lock. There is one coordinator, so it is the single writer.

The status line itself cannot do it: it is a formatter that runs on every event
and makes no network call (FR-1.6).

## Why three days

It was one day, and the file was found five days stale, so the figure had never
been honoured. A window that is kept beats a shorter one that is not, and the
refresh needs a person, which is what makes a tight window expensive.

The cost of a stale rate is small and bounded. The figure is a counterfactual on
a subscription, what these tokens would have cost through the API, so a wrong
rate misprices a number nobody is billed for. When the five-day-stale file was
re-read, nothing in it had changed.

## Why nothing is compiled in

A seed compiled into the binary used to stand in when the file was missing. It
went stale unnoticed, and a price change meant a rebuild. With the file as the
only copy, a price change is an edit, and a host with no usable file renders
with no cost segment (FR-8.28), which is visible. The lesson is
`docs/LESSONS/a-fallback-copy-of-perishable-data-goes-stale-unseen.md`.
