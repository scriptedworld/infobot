# The status line is called on updates, not on a timer

Claude Code runs the status line command once per transcript entry, about a
third of a second after the entry lands. It runs it no other time. An idle
session renders nothing at all.

`refreshInterval` in `settings.json` does not drive it. Treat the render rate as
a consequence of session activity, and read `written` in the state file when you
need to know how old a number is.

## What was measured

**Measured 2026-08-26.** Sampled the mtime of
`~/.local/state/infobot/<session>.status.yaml` every 0.5s for 76 seconds, across
a deliberate idle window, then paired each write against the session transcript.

Seven renders. Each one trails a transcript entry:

    render 11:47:54.790  <- assistant 11:47:54.430  +0.36s
    render 11:47:56.394  <- user      11:47:56.050  +0.34s
    render 11:48:06.606  <- assistant 11:48:06.253  +0.35s
    render 11:48:07.522  <- assistant 11:48:07.125  +0.40s
    render 11:49:07.133  <- assistant 11:49:06.785  +0.35s
    render 11:49:07.837  <- assistant 11:49:07.496  +0.34s
    render 11:49:10.685  <- assistant 11:49:10.330  +0.36s

    lag: min 0.34s  max 0.40s  spread 0.06s

A spread of 0.06s across seven samples is the cost of spawning the command. A
timer would not track another clock that closely.

**The idle window settles it.** Between 11:48:07 and 11:49:07 the session did
nothing and the file was not written once:

    largest gap: 59.6s
    a 10-second timer would have put 5 writes inside it. There were 0.

## `refreshInterval` says it does this and did not

The settings schema documents it plainly:

    refreshInterval: Re-run the status line command every N seconds in
                     addition to event-driven updates.  minimum: 1

So `10` means ten seconds, and it is additional to the event-driven calls rather
than a floor under them. That reading is consistent with the sub-second gaps
during activity, which are the event-driven half.

**It is not consistent with the idle window.** `"refreshInterval": 10` was set
throughout the measurement, and 59.6 seconds of idle should have carried five
timer ticks. It carried none.

Either the key is not honoured in this build, or the timer does not run while
the session is idle, which would make it additional to activity rather than
additional to events. Not resolved. What is measured is that **an idle session
renders nothing regardless of what the key says**, so nothing should be built on
the timer firing.

A guess that it meant milliseconds, and a change to `10000`, was made and
reverted before the schema was read. It was wrong twice over: the unit is
seconds, so the value would have meant nearly three hours, and the cadence was
never a timer to set in the first place.

## The corollary, which is the useful half

**The transcript is a log of render calls**, offset by about a third of a
second. So the state file in `state_dir()` is exactly as fresh as the last
transcript entry: it cannot go stale while the session is doing anything, and
when the session is idle the numbers it holds are not changing.

That answers the question that started this, which was how often the file should
be refreshed. It refreshes when there is something to refresh, and a timer would
be strictly worse: staleness during activity, and pointless writes during idle.

**One case renders nothing: a queued message.** A message arriving while the
session is idle is parked, and the transcript records it, but nothing renders
until the turn resumes and the model produces an entry.

    11:49:00.836  user       message arrives, queued
    11:49:06.785  assistant  turn resumes
    11:49:07.133  render

Six seconds where the file said the session was where it had been. Correct, and
worth knowing before treating the file as a liveness signal. It reports context,
not activity.

## Reproducing it

    f=~/.local/state/infobot/<session>.status.yaml
    for i in $(seq 1 150); do stat -c '%y' "$f"; sleep 0.5; done | uniq

Do nothing for a minute in the middle of it. The gap is the finding.

## Nothing removes the file when the session ends

The status line is the only thing that writes it, and it is never called again
after the last entry. Fifteen offsets files had accumulated in `state_dir()`
before anyone noticed, one per session since the offsets were introduced.

A `SessionEnd` hook removes both, calling `bin/forget-session`. It cannot be
proven from inside a session, because it fires as the session goes.

    ls ~/.local/state/infobot/

Run that after closing a session. Its two files should be gone and every other
session left alone.
