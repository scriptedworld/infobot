# A fallback copy of perishable data goes stale unseen

The status line priced sessions from `~/.config/infobot/pricing.json`, and a
copy of the same rates compiled into `internal/pricing/pricing.go` stood in when
the file was missing. The copy was there so a fresh clone would show a cost with
no config file.

On 2026-09-28 the cost segment was blank on every session running Opus 5.5. The
file on lazlo was a month old, oslo had no file at all, and the compiled copy
was exactly as old as the file, because both had been written on the same day
and only one of them had a refresh assigned to it. Neither carried the three
models released since, and a session whose only model has no rate prices to
nothing, so the segment was dropped rather than wrong.

## What made it invisible

A fallback exists to be used when the primary is absent, so its staleness shows
only on the hosts where the primary is absent, and there it looks like the
primary working. Oslo's status line rendered, with no error, from rates nobody
had looked at since August. Nothing compared the two copies, and the refresh
declared in `docs/PROJECT.md` named the file and not the code.

The copy also made the fix expensive: three new prices meant editing Go,
rebuilding, committing and deploying to two hosts, for a change that is data.

## What to do

Keep perishable data in one place, with its refresh owned. Where it is missing,
show nothing rather than a guess: infobot now leaves the cost segment out with
no usable rate file (FR-8.27, FR-8.28, at `0f62594`), and the coordinator's
refresh writes the file on every host.

A fallback is worth keeping for data that does not expire, such as the palette
and the margin, which overlay compiled seeds and stay correct as long as the
code does.
