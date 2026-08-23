# A rate limit gauge is coloured by pace, not by spend

The context meter and a rate limit window look like the same instrument and are
not asking the same question, so they do not share a colour scale.

## What each colour means

The context bar's colour is its own fill: each cell takes the ramp of the
percentage it stands for, green at the start through to the alarm at 90. Full
is the emergency there, because a full context window is the end of the road.

A rate limit gauge is coloured by **where the window is projected to land when
it resets**: spend divided by how far through the window we are. Its length
still says how much is spent. Full at the moment it resets is the best outcome
available, not the worst: nothing ran out and nothing went unused.

## Why the scale diverges

Green sits in the middle rather than at an end, because both directions away
from landing exactly full are wrong in opposite ways. Above it the window
empties before it resets and dead time follows, so it runs yellow into red.
Below it the allowance goes unspent, which is not a fault but is worth seeing,
so it pales through white into blue.

    PACE_STOPS = 0 blue, 70 white, 100 green, 125 yellow, 150 red

Jeff's calibration example settled the anchor: 80% spent with an hour left of
five projects to exactly 100%, which is the boundary of concern rather than the
middle of it. An earlier version reused the context meter's ramp and made that
case an alarm, which is wrong twice over: it treats efficient use as a crisis,
and it says nothing different when the window will actually run dry.

## Why the verdict fades in

The projection is unreliable early: a percent spent two minutes into five hours
divides by almost nothing and reads as catastrophe. Rather than withhold the
verdict below a threshold, which puts a colour change at an arbitrary moment,
the judged colour is mixed against green in proportion to how far the window has
run, squared so it stays quiet through the middle and arrives late.

Green is the right thing to fade toward because on this scale it is both "on
rate" and "nothing to say", which are the same instruction to the reader.

`PACE_CONFIDENT = 0.6` is where the verdict reaches full strength: three hours
into a five hour window. A fraction rather than a duration, so the seven day
window matures at the same point in its own life instead of after an afternoon.

## What was tried and rejected

**A filled arc glyph** (`○◔◑◕●`) beside the countdown, showing how far through
the window we are. Rejected as too coarse: five steps cannot say "burning 1.4
times faster than this window refills", and it cost a column to say less than
the colour does for free.

**A background wash** under the whole segment, on a separate hue axis from the
bars. It worked, and re-establishing the background after each of the bar's own
resets is the technique if it is ever wanted again. Dropped in favour of tinting
the bar itself, which needs no second colour language: the bar's length is the
spend and its colour is the verdict.
