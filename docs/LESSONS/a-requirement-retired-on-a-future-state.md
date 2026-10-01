# A requirement retired on a future state records a live gap as closed

FR-1.11n required the state file's form to be pinned at both ends: infobot's
output was a fixture in wrench, and infobot asserted its own canonical form. It
was retired on 2026-08-27, on a measurement that wrench's Go pack reproduced
infobot's bytes, and on the expectation that the port would link the pack and
leave one emitter.

That state did not arrive on time. The port kept its hand emitter, and wrench
declined the link on 2026-08-28, so for a fortnight there were two emitters and
the requirement that pinned them was listed as retired. The guarantee came back
under a new id, FR-1.11p, since a retired id is never reused, with FR-1.11o
carrying the half that faces the file's readers. The link was taken at infobot
`1b96c85`, and the state the row had been retired on finally existed.

## Why the arrival does not redeem it

For those two weeks the requirements table said the pin was not needed, while
two emitters of one published form ran with nothing holding them together.
The link landing later does not change what the table said in between: anybody
reading it was told a live gap was closed.

## What to do

Retire a requirement on what is true, not on what a plan expects to make true.
Where the reason is a future state, leave the row live and note what would
retire it, then retire it when that state has landed and can be pointed at.
