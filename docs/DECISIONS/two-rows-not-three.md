# Two rows, not three

The status line is two rows: identity (model, path, context, session) and
meters (the rate-limit windows and the cost). A third row for exceptional
states, such as a stale rate table or a promotion in effect, was declined.

## Why

A row that appears only when there is something to say moves the prompt every
time it comes and goes, which is a distraction on every event. A row that is
always there spends the space on nothing most of the time.

## Where those states go instead

On the segment already present. The model for it is the cost's trailing `+`,
which says the session ran a model the rate table has no price for, so the
figure is a floor (FR-8.12). A stale table or a promotion would mark the cost
the same way, as a character on it, not a line of its own.
