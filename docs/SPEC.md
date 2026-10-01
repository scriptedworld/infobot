# infobot, the specification

How the status line is built, where `docs/REQUIREMENTS/` says what must be true of
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
it (FR-1.11). This section is the form's normative statement, and
`internal/state/status.schema.json` is its machine-checkable half.

    path     $XDG_STATE_HOME/infobot/<session>.status.yaml, mode 0644
    written  by wrench's Go pack, whole or not at all, through a temporary
             renamed into place; a failed write leaves nothing (FR-1.11b)

A render writes, for example:

    "context_percent": 14.0
    "context_remaining": 861823
    "context_size": 1000000
    "context_used": 138177
    "cwd": "/home/me/src/infobot"
    "effort": "medium"
    "model": "Opus 5.5 (1M context)"
    "session": "9823d31e-4539-4855-9940-706a17967086"
    "written": "2026-09-28T16:44:46-07:00"

The keys (FR-1.11g):

    session             string   always
    written             string   always; ISO 8601, to the second, with offset
    cwd, model, effort  string   where the payload names them
    context_used        integer  the four together, where the window can be
    context_size        integer  measured; remaining is derived and never
    context_remaining   integer  negative, percent is neither floored nor
    context_percent     float    capped (FR-1.11h)

A key whose value would be empty is omitted, never written blank. So an absent
context block and an unmeasured window read the same, which FR-4.10 leaves open.

The spelling, all of it load-bearing for readers that match anchored patterns
(FR-1.11o):

- one key to a line, sorted, quoted, followed by `: ` with one space;
- strings double-quoted, with C0, DEL, C1, U+2028 and U+2029 escaped so a value
  comes back the bytes it went in as (FR-1.11r);
- numbers bare and never with an exponent; a float always carries a decimal
  point and an integer never does (FR-1.11q).

Adding a key is compatible with every current reader. Changing any rule above is
not, and is announced to the readers before it lands (FR-1.11o).

## The offsets file

Private to infobot and read by nothing else (FR-1.7), so its form carries no
promise beyond this program.

    path     $XDG_STATE_HOME/infobot/<session>.json, mode 0600
    written  best effort; a write that fails costs a re-sum (FR-8.10)

    {"files": {"<transcript path>": {"size": <bytes read>,
                                      "totals": {"<model>": {"<field>": <n>}}}}}

One entry per transcript, subagents' included (FR-8.9). `size` is the offset
reached over complete lines (FR-8.7); a transcript now smaller than it is read
from the start (FR-8.8). The fields are `input_tokens`, `output_tokens`,
`cache_read_input_tokens`, `ephemeral_5m_input_tokens` and
`ephemeral_1h_input_tokens`. A file that does not decode, or has no `files`, is
treated as empty (FR-8.10).

## Exits and failures

    bin/infobot           exits 0. With no built binary beside it, it drains
                          stdin and prints one row saying so (FR-1.13), plain
                          under NO_COLOR (FR-3.8).
    bin/statusline        exits 0 whatever it is given (FR-1.2). A failing step
                          costs its own segment (FR-1.4), and nothing is
                          written to stderr.
    bin/forget-session    exits 0. With no built binary it drains stdin and
                          says nothing: a hook has no line to occupy, and the
                          state files left behind are the evidence.
    bin/forget            exits 0 whatever it is given, and refuses a session
                          id carrying a path separator (FR-1.11f).

Both shims resolve their own symlinks before looking for the binary, so they run
from any directory and any link (FR-1.9).

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
which are stated in `docs/REQUIREMENTS/the-meters-and-the-palette/` and
`the-rate-limit-windows/`. The internals of
wrench's Go pack. Any implementation but Go; `docs/PROJECT.md` names the
retired ones.

## Traceability

Forward, every section above cites the rows it rests on. Backward, the rows this
document does not reach:

- FR-4.5 to FR-4.10 are open questions carrying no test, and stay open.
- FR-4.3 and FR-4.4 name Python functions (`time.time`, `limit_segment`,
  `place_context`). What they require, a clock and roots that are parameters, is
  what the Go packages do; the rows need restating without those names, and
  `NEXT_STEPS.md` carries that.
- FR-1.11p asserts the exact bytes emitted, which holds while Go is the only
  writer. Only a second implementation would need it restated, and there is
  none.
- FR-3.2 is refresh interval, set in the harness settings file, and nothing in
  this program implements it.
- FR-1.13 is the shims', which are committed and exec the Go binaries.
