# infobot, the specification

How the status line is built, where `REQUIREMENTS.md` says what must be true of
it. Every claim here cites the rows it rests on, and the last section traces
those rows back. A reader who wants the reasoning behind a row goes to
`docs/DECISIONS/`; a reader who wants to run the thing goes to `README.md`.

Go, in `internal/` and `cmd/`, is the implementation it describes, and the only
one kept.

## The program

Two entry points, each a shell shim that execs a binary it reports the absence
of (FR-1.13, FR-1.9):

    bin/infobot         -> the status line. Payload on stdin, rows on stdout.
    bin/forget-session  -> the SessionEnd cleanup (FR-1.11e).

A render is one process per Claude Code event, so everything below is paid on
every event. It makes no network call (FR-1.6) and at most one subprocess, to
ask a host how wide the pane is (FR-1.8).

## What a render reads

    stdin              the session payload, JSON, every field optional
                       (FR-1.1, FR-1.3, FR-2.1)
    the environment    HOME, XDG_CONFIG_HOME, XDG_STATE_HOME, NO_COLOR,
                       TMUX, TMUX_PANE, HERDR_PANE_ID, HERDR_BIN_PATH, PATH
    the rate table     $XDG_CONFIG_HOME/infobot/pricing.json (FR-8.27)
    the palette        $XDG_CONFIG_HOME/infobot/palette.json (FR-6.11)
    the layout         $XDG_CONFIG_HOME/infobot/layout.json, the margin (FR-3.5)
    the transcripts    ~/.claude/projects/**, found by session id (FR-8.2-8.5)
    the offsets        $XDG_STATE_HOME/infobot/<session>.json (FR-8.6-8.10)

Those are the only files opened (FR-1.7). A field the payload gains that nothing
here reads changes no output (FR-2.13).

What the payload means is FR-2.2 to FR-2.12: which window carries counts, how a
percentage and its counts derive each other, that the context counts are the
input side only, how the model name and the path are built, and that the session
id shows as eight characters.

## What a render writes

    stdout    one or two complete rows (FR-1.1, FR-1.5)
    the state $XDG_STATE_HOME/infobot/<session>.status.yaml (FR-1.11)
    the offsets, advanced over the lines that arrived complete (FR-8.7)

## The order of a render

1. Decode the payload. Absent is absent, never zero (FR-1.3).
2. Ask the pane its width, or establish that it is unknown (FR-3.3).
3. Load the palette and the margin, each falling back to its seed (FR-6.11,
   FR-3.5), and the rate table, which has no seed: without a usable one the
   cost segment is left out (FR-8.28).
4. Read what the transcripts appended since the last render and price it
   (FR-8.6, FR-8.11).
5. Measure the context window once. The bar and the state file come from that
   one measurement (FR-1.11a), and both cost forms from one reading of the
   transcripts (FR-5.9).
6. Compose the rows, fit them to the budget, write the state file, print.

A failure inside any step costs that step's segment and nothing else (FR-1.4),
and the process exits 0 whatever it was given (FR-1.2).

## The width

Whoever owns the pane is asked directly, innermost first, because every ordinary
route fails under Claude Code (FR-3.3). tmux is asked when `TMUX` is set, herdr
when `HERDR_PANE_ID` is; the route that answers runs and the others do not
(FR-1.8). A herdr pane that is zoomed is fitted to its tab's area (FR-3.7), and
a reported rectangle is trimmed to what can be drawn into.

A host that does not answer costs a bounded wait and then counts as unknown
(FR-3.11). The bound covers the whole query, including a grandchild holding the
pipe open after the host itself has gone, so it is a deadline on the read rather
than a signal to the child. Unknown is not a guess: the rows render in full form
with a fixed fifty-cell bar (FR-3.4, FR-5.12).

The width the rows are fitted to is the pane less three columns, which belong to
Claude Code (FR-3.5), and the rail is part of what a row costs (FR-5.11).

## The rows

Two rows hanging off a left rail (FR-5.1). The identity row carries the model,
the path, the context bar and the session id; the meter row carries the rate
limit windows and the cost. Where the context segment sits depends on what fits
(FR-5.2, FR-5.3), and the cost rides the meter row only (FR-5.7, FR-5.8).

Fitting measures a row as the terminal draws it: escapes cost nothing,
combining marks cost nothing, east-asian `W` and `F` cost two columns, and
ambiguous characters count as one (FR-3.9, FR-3.10). What exceeds the budget is
cut from the right, and a meter too small to say anything is dropped instead of
drawn small (FR-3.6). A row left with no parts is omitted (FR-5.5). The session
id closes the identity row (FR-5.4), parts are divided by a separator costing
one column (FR-5.10), each segment opens with a fixed marker (FR-5.13), and a
meter row over budget gives up its gauges before anything is cut (FR-5.6).

Colour is 24-bit (FR-3.1), and `NO_COLOR` strips every escape including the rail
and the separators (FR-3.8). The consumption ramp, the inversion band above 90
and the pace scale are specified in FR-6.1 to FR-6.13 and FR-7.1 to FR-7.11;
this document adds nothing to them.

## The cost

The figure is what the session's tokens would have cost through the API at list
rates, a counterfactual rather than a bill (FR-8.1). Transcripts are found by
globbing for the session id, grouped by recorded origin so that what a `/clear`
left behind still counts, and the search stays inside the session's own project
directory unless the id matches nothing at all (FR-8.2 to FR-8.5).

Only the bytes appended since the last render are parsed, offsets are kept per
file, a file that shrank is re-read from the start, and state that cannot be
read or written costs a re-sum and never a wrong figure (FR-8.6 to FR-8.10).
Each model is priced at its own rate, cache reads and writes apart, with an
unpriced model flagging the total a floor (FR-8.11 to FR-8.24).

## The state file

Every render leaves the session's context state where other programs can read
it (FR-1.11). Its keys, their types and the ISO 8601 `written` stamp are
FR-1.11g and FR-1.11h; the escaping of any string that could fold is FR-1.11r,
and numbers carry no exponent at any magnitude (FR-1.11q).

The file is written whole or not at all, through a temporary renamed into place,
and a write that fails leaves nothing behind (FR-1.11b).

The form is a published interface. Its readers match anchored patterns against
the quoted key, so quoting, one key to a line, the single space after the colon
and bare numbers are load-bearing, and a change to any of them is announced
before it lands (FR-1.11o).

**Implementations are held to content identity, not byte identity.** Two
implementations of infobot must print the same rows and write state files that
decode to the same content. The bytes may differ, as they do between wrench's
packs, which hold their own packs to the same standard.

## The cleanup

`bin/forget-session` takes a payload carrying a session id and removes the two
state files named by it (FR-1.11e). It exits 0 whatever it is given and refuses a
session id carrying a path separator rather than joining it onto a directory it
unlinks within (FR-1.11f).

## What this does not specify

The palette's values, the glyphs, the ramp arithmetic and the pace scale, all of
which are stated in `REQUIREMENTS.md` sections 6 and 7. The internals of
wrench's Go pack. The Python original, the Zig experiment and two C++ ports,
all retired.

## Traceability

Forward, every section above cites the rows it rests on. Backward, the rows this
document does not reach:

- FR-4.5 to FR-4.10 are open questions carrying no test, and stay open.
- FR-4.3 and FR-4.4 name Python functions (`time.time`, `limit_segment`,
  `place_context`). What they require, a clock and roots that are parameters, is
  what the Go packages do; the rows need restating without those names, and
  `NEXT_STEPS.md` carries that.
- FR-1.11p asserts the exact bytes emitted, which holds while Go is the only
  writer. It wanted restating only for a second implementation, and none is
  kept.
- FR-3.2 is refresh interval, set in the harness settings file, and nothing in
  this program implements it.
- FR-1.13 is the shims', which are committed and exec the Go binaries.
