# infobot, Requirements

What must be true of the status line.

*Derives from:* `bin/infobot`, and the payload shape read out of `claude`
2.1.233.

Requirements are stated as observable properties: what is true of a run, not how
anything is arranged. The reasoning behind a row lives in `docs/DECISIONS/` and
`docs/LESSONS/`, not in the row.

**Status markers.** `[A]` traces to something I said. `[D]` is derived from one.
`[A/D]` is both. `[?]` is an open question carrying no test yet.

**A number is an identifier, not a position.** Nothing is renumbered and nothing
is reused. Section 4 sits at the end while keeping the number it was first
given. Retired ids are listed at the bottom and never come back.

**A lettered id is a facet of the row it hangs off**, and is a requirement in
its own right with its own test.

**Every settled requirement is cited by a test that asserts it**, 110 of 110,
checked by the traceability task of `bolt common-quality`. The six rows marked
`[?]` are open questions and are exempt until they are answered.

## 1. What it reads and writes

| ID | Requirement | |
|---|---|---|
| FR-1.1 | The status line reads the session JSON on standard input and writes complete rows to standard output. | [D] |
| FR-1.2 | It exits 0 whatever it is given, and puts no traceback in the status bar. | [D] |
| FR-1.3 | Every field of the payload is optional. A segment whose data is absent is dropped, never rendered as zero. | [A/D] |
| FR-1.4 | A failure confined to one segment costs only that segment. A field that cannot be read is absent data, so what carried it is dropped and the rest of the row survives. | [D] |
| FR-1.5 | A row with no parts in it is omitted rather than printed blank. | [D] |
| FR-1.6 | No path makes a network call. | [A/D] |
| FR-1.7 | The only files opened are the rate table, the session's own transcripts, the offsets file, and the status file FR-1.11 leaves behind. | [D] |
| FR-1.8 | One subprocess per render at most, and only to ask a host how wide the pane is. The route that answers runs and the others do not. | [D] |
| FR-1.9 | It runs from any working directory, with nothing that can be half present. Claude Code invokes it by absolute path from wherever the session happens to be, so the entry point resolves what it runs from its OWN location rather than from the working directory or from `PATH`. | [D] |
| FR-1.11 | Every render leaves the session's context state on disk, at one path named by session id beside the offsets, for programs other than infobot to read. | [A] |
| FR-1.11a | The bar and the file come through one measurement. | [D] |
| FR-1.11b | The file is written whole or not at all, to a temporary renamed into place. A write that fails costs nothing and leaves nothing behind. | [D] |
| FR-1.11e | Both state files are removed when the session ends, by a `SessionEnd` hook calling `bin/forget-session`. | [A] |
| FR-1.11f | That entry point exits 0 whatever it is given, and refuses a session id carrying a path separator rather than joining it onto a directory it unlinks within. | [D] |
| FR-1.11g | The file carries `session` and `written` always; `cwd`, `model` and `effort` where the payload names them; and `context_used`, `context_size`, `context_remaining` and `context_percent` where the window can be measured. A key whose value is empty is omitted rather than written blank. Keys are quoted, sorted and one to a line. A value's type survives the round trip, so a strict reader gets back a string, an integer and a float. `written` is ISO 8601 carrying an offset, to the second. | [D] |
| FR-1.11h | `context_remaining` is derived rather than read, and never negative. `context_percent` is neither floored nor capped, so an over-full window reads as over-full where the bar can only draw it as full. | [D] |
| FR-1.11o | The emitted form is a published interface with readers outside this repository, so a change to it is announced before it lands rather than discovered by whatever breaks. Today's readers match anchored patterns against the quoted key, so the quoting, one key to a line, the single space after the colon and numbers being bare are all load-bearing and not merely tidy. This replaces the pinning FR-1.11n used to give, which was mutual between two emitters; this one is one-way, from the emitter to its readers. | [A/D] |
| FR-1.11p | The form is pinned independently at both ends: infobot's output is a fixture in wrench, and infobot asserts its own canonical bytes. Neither suite reaches into the other's tree, because a check needing a sibling repository present fails for the wrong reason on a fresh clone. Changing the emitted form is therefore a two-repository change and is meant to feel like one. | [A/D] |
| FR-1.11r | A string value comes back the bytes it went in as, or the write fails loudly. Every code point a strict reader rejects or silently alters is escaped: C0, DEL, C1, and the two line separators U+2028 and U+2029. The separators are in the set because YAML 1.1 makes all five of LF, CR, U+0085, U+2028 and U+2029 line breaks and 1.2 cuts the set to LF and CR, so which of them fold depends on the reader's version rather than on the character. A range derived from one parser's behaviour escaped U+0085 and left its two spec siblings raw. The double-quoted style already carries the full escape set on one line, so this needs no change of form and alters no byte of any value not holding one. Unescaped, `\n` and `\r` and U+0085 each come back as a space, which no reader can detect; the rest make a file no parser will read, which is the loud failure this keeps. | [A/D] |
| FR-1.11q | A number is spelled without an exponent at any magnitude. A float carries a decimal point so a reader gets a float back, and an integer does not. `1e+06` is a legal spelling of a million and a reader matching `[0-9.]+` captures `1` from it, which is a plausible small number rather than a parse failure, so an exponent is a silent wrong answer rather than a formatting preference. | [A/D] |
| FR-1.13 | Whatever the status line is reached through is present or its absence is visible. A build that has not run and a symlink that dangles fail as a blank line nobody is told about, so a missing runtime is a stated, checkable condition rather than silence. | [A/D] |

## 2. The payload

| ID | Requirement | |
|---|---|---|
| FR-2.1 | Field names are those the running Claude Code sends. The payload carries `context_window`, `rate_limits.{five_hour,seven_day}` and `effort.level`, and `resets_at` is a numeric epoch rather than a timestamp string. | [D] |
| FR-2.2 | Token counts are shown only for the window that carries them. `context_window` carries counts and a percentage; `rate_limits` carries a percentage and a reset time and nothing else. | [D] |
| FR-2.3 | A percentage the status line prints is computed by the same formula as the percentage the payload supplies. | [D] |
| FR-2.4 | A countdown already elapsed is omitted rather than printed as `0m`. | [D] |
| FR-2.5 | The context counts are the input side only: input, cache creation and cache read, never output. | [D] |
| FR-2.6 | `current_usage` is the authority for those counts where it is present, and `total_input_tokens` is the fallback where it is not. | [D] |
| FR-2.7 | A percentage arriving without counts derives the counts from it, and counts arriving without a percentage derive the percentage. | [D] |
| FR-2.8 | The model is `display_name` where the payload has one and `id` where it does not, with the effort level appended when one is set. | [D] |
| FR-2.9 | The project root is shown only when it differs from the working directory. | [A/D] |
| FR-2.10 | A path inside home is shown relative to it. | [A/D] |
| FR-2.11 | The session id is shown as its first eight characters. | [A/D] |
| FR-2.12 | The project root, where shown, is marked as a root, and the marker sits outside the colour the path carries. | [A/D] |
| FR-2.13 | A field the payload gains that infobot does not read changes nothing about the output. | [D] |

## 3. The terminal

| ID | Requirement | |
|---|---|---|
| FR-3.1 | Colour is 24-bit. The environment Claude Code's 256-colour cap depends on is set in the harness settings file rather than left to the terminal. | [A/D] |
| FR-3.2 | Refresh is time-based as well as event-based, so a quiet session does not show a stale countdown. | [A/D] |
| FR-3.3 | The terminal width is established or reported as unknown, never guessed. Every ordinary route fails under Claude Code, so whatever owns the pane is asked directly, tmux or herdr, innermost first. A host that cannot be asked yields unknown, which means render the full form. | [D] |
| FR-3.4 | A fabricated default returned by a library in place of an answer is treated as absent data. | [D] |
| FR-3.5 | The width a row is fitted to is the renderer's budget, and the pane is not that budget. The reserve is three columns, two of indent and one at the right, and it belongs to Claude Code rather than to the multiplexer, so it is the same three under tmux and under herdr. | [D] |
| FR-3.6 | What exceeds the budget is cut from the right rather than wrapped. A meter too small to say anything is dropped rather than drawn at any size, and the bar goes before the number it illustrates. | [A/D] |
| FR-3.7 | The width is the one the pane is DRAWN at, which is not always the one its own rectangle reports. A zoomed herdr pane is fitted to the tab's area, and the zoomed pane is the focused one. | [D] |
| FR-3.8 | `NO_COLOR` strips every escape, the separator and the rail included, so the output is either coloured or clean and never half of each. | [A/D] |
| FR-3.9 | A row is measured as the terminal will draw it: escapes cost nothing, combining characters cost nothing, and characters of east-asian width `W` or `F` cost two. | [D] |
| FR-3.10 | Ambiguous-width characters are counted as one column. | [A/D] |
| FR-3.11 | A host that does not answer costs a bounded wait and then counts as unknown. Killing the process is not the bound: a host is a script, and killing the shell leaves any child it spawned holding the inherited stdout pipe, so the read blocks on the grandchild. The bound is the two second timeout plus a short delay after which the pipes are closed regardless. | [D] |

## 5. The rows

| ID | Requirement | |
|---|---|---|
| FR-5.1 | The rows hang off a left rail: a rounded corner opening the block, a rounded corner closing it, a tee for anything between, and a stub when there is only one row. | [A/D] |
| FR-5.2 | The context segment rides the identity row when that row can hold it with no bar at all, and moves to the meter row when it cannot. | [D] |
| FR-5.3 | The context bar takes every column the identity row has left once everything else on it is placed. | [A/D] |
| FR-5.4 | The session id closes the identity row. | [D] |
| FR-5.5 | The rate limit gauges are a fixed ten cells rather than a share of the slack. | [A/D] |
| FR-5.6 | A meter row over budget gives up its gauges for the percentages they were drawing before anything is cut. | [D] |
| FR-5.7 | The cost is pushed to the right of the last row with at least three columns of clear space between, shortened to the total alone when the full form will not fit, and dropped when neither will. | [D] |
| FR-5.8 | The cost goes on the meter row rather than the identity row, and is dropped when there is no meter row. | [D] |
| FR-5.9 | Both cost forms come from one reading of the transcripts. | [D] |
| FR-5.10 | Parts within a row are divided by a separator costing exactly one column, whatever glyph the font provides. | [A/D] |
| FR-5.11 | The rail is part of what a row costs. Fitting measures the parts joined the way they will be joined AND the three columns the rail takes. | [D] |
| FR-5.12 | With the width unknown the bar is a fixed fifty cells. | [A/D] |
| FR-5.13 | Each segment opens with a fixed marker, so which meter is which is read from the shape rather than from the numbers. | [A] |

## 6. The meters and the palette

| ID | Requirement | |
|---|---|---|
| FR-6.1 | The consumption ramp is two straight lines, green to yellow from 0 to 75 and yellow to red from 75 to 90. | [A/D] |
| FR-6.2 | At 90 and above the style inverts rather than merely reddening. | [A] |
| FR-6.2a | The inversion fades in across 90 to 100 rather than switching on at 90. At 90 the foreground is the red the ramp arrives at and the background is the terminal's own; at 100 it is pale yellow on deep red. Both ends move together. Bold cannot fade and is on across the whole band. The starting background follows the desktop palette per FR-6.11. | [A] |
| FR-6.2b | The fade's resolution in the bar is the cell count, because the band is its top tenth: 11 cells of 103, 5 of 40, and 1 at `BAR_MIN`. The gauges are fixed at 10 cells so their band is 2. The printed number is smooth at every width, being coloured from the real percentage rather than from a cell's position. | [D] |
| FR-6.3 | Each filled cell is coloured for the percentage it stands for and not for the bar's total. | [D] |
| FR-6.4 | The last filled cell carries the same colour as the number printed beside it, within one cell of rounding. | [D] |
| FR-6.5 | Every run of cells sharing a style opens with a reset before the style. | [D] |
| FR-6.6 | One escape per run of cells sharing a style, not one per cell. | [D] |
| FR-6.7 | The bar is never cut into. Every cell is a filled or an empty parallelogram and nothing else, and the percentage is printed with the counts. | [A/D] |
| FR-6.8 | A bar of no cells renders nothing at all. A percentage below 0 renders all empty and one above 100 renders all filled. | [D] |
| FR-6.9 | Token counts are compact: below a thousand as they are, thousands as `k` with no decimal, millions as `M` with one. | [A/D] |
| FR-6.10 | The empty cells carry an outline glyph and no background. | [A] |
| FR-6.11 | The colours are taken from the palette the rest of the desktop already uses. The path is the ENCOM teal the i3 bar and claws use, and the bar's empty cells are the dim cyan one step up from that bar's inactive workspace. | [A] |
| FR-6.12 | The rail is drawn in the same colour as the bar's empty cells. | [A] |
| FR-6.13 | A word present to be scanned past rather than read is dimmed. Only the connective words are; the figures beside them are not. | [A/D] |

## 7. The rate limit windows

| ID | Requirement | |
|---|---|---|
| FR-7.1 | A window's gauge carries two readings at once without costing a column: its length is how much is spent, its colour is whether that is a problem. | [A/D] |
| FR-7.2 | The verdict is where the window is projected to land: spend divided by how far through the window it is. | [A/D] |
| FR-7.3 | The pace scale diverges, with green at landing exactly full as the window resets. Above it runs yellow into red; below it pales through white into blue. | [A] |
| FR-7.4 | The verdict fades in against green rather than switching on, reaching full colour at 60% of the window elapsed and squared so it arrives late. | [A/D] |
| FR-7.5 | The fade is a fraction of the window rather than a duration. | [D] |
| FR-7.6 | Green is what the verdict fades toward, rather than grey or nothing. | [A] |
| FR-7.7 | Beyond the ends of the scale the colour clamps. | [D] |
| FR-7.8 | A window with no reset time has no position in its window, so its gauge falls back to the consumption ramp. | [D] |
| FR-7.9 | The countdown is the largest two non-zero units, and it stays a number because a bar cannot carry it. | [A/D] |
| FR-7.10 | The window lengths are five hours and seven days, taken from the payload's own field names. | [D] |
| FR-7.11 | The pace arithmetic cannot divide by nothing and cannot run backwards. How far through the window we are is held between none and all of it, and the divisor carries a floor. The floor is arithmetic only; what stops a wild early projection being believed is FR-7.4. | [D] |

## 8. The cost

| ID | Requirement | |
|---|---|---|
| FR-8.1 | The figure is a counterfactual and not a bill: what the same tokens would have cost through the API at list rates. | [A] |
| FR-8.2 | The session's transcripts are found by globbing for the session id, not by rebuilding the directory slug from the working directory. | [D] |
| FR-8.3 | Subagent transcripts are counted. | [D] |
| FR-8.4 | Transcripts are grouped by root, being a transcript's recorded origin where it has one and its own name where it does not, so the transcripts a `/clear` left behind are counted. | [D] |
| FR-8.5 | The search stays inside the project directory the session belongs to. Only a session id that matches nothing widens it. | [D] |
| FR-8.6 | Only the bytes appended since the last render are parsed. | [D] |
| FR-8.7 | The offset advances only over lines that arrived complete. | [D] |
| FR-8.8 | A transcript that has shrunk is read from the start. | [D] |
| FR-8.9 | State is kept per file rather than as one running sum. | [D] |
| FR-8.10 | State that cannot be read or written costs a re-sum and never a wrong figure. | [D] |
| FR-8.11 | Each model is charged at its own rate and the session is the sum of them, not an average. | [D] |
| FR-8.12 | A model absent from the rate table is left out and the total is flagged incomplete rather than abandoned. The flag is a trailing plus, meaning the figure is a floor. No segment appears only when nothing could be priced. | [A/D] |
| FR-8.13 | Cache reads and cache writes are priced apart rather than lumped together as cache. A read is charged at a tenth of input; a write costs more than a fresh input token. | [D] |
| FR-8.14 | The saving is every cached token, read or written, charged at the plain input rate instead, less what those tokens did cost. | [D] |
| FR-8.15 | The rates come from `~/.config/infobot/pricing.json` when it is there and from the seed in the module when it is not. Both carry the date they were taken. | [D] |
| FR-8.16 | A rate table that is missing, malformed or carrying no rates falls back to the seed rather than raising. | [D] |
| FR-8.17 | Money is printed at the precision the number deserves rather than always two decimals. | [A/D] |
| FR-8.18 | The saving is shown only when there is one. A negative saving drops the clause rather than printing a loss, and the total is still shown. | [D] |
| FR-8.19 | Cache creation counts are read from the nested `cache_creation` object as well as from the top level of a usage record. | [D] |
| FR-8.20 | A usage record naming no model is counted under a placeholder no rate table answers to, so it flags the total incomplete rather than being priced at whatever model was next to it. | [D] |
| FR-8.21 | Only numeric values are added. A usage field carrying anything else is ignored rather than coerced, and does not abandon the record it appeared in. | [D] |
| FR-8.22 | The counted fields are input, output, cache read, and the two ephemeral cache writes. Output is counted here and not in the context percentage, per FR-2.5. | [D] |
| FR-8.23 | The cache multipliers are configurable beside the per-model rates. | [D] |
| FR-8.24 | Looking for the session a transcript belongs to gives up after 40 records. | [D] |

## 4. Being testable, and what is still open

Numbered 4 because that is the number these rows were first given. The first two
are settled requirements about the shape of the code; the rest are questions.

| ID | Requirement | |
|---|---|---|
| FR-4.3 | The clock is a parameter, so FR-2.4, FR-7.4 and FR-7.9 are asserted against a fixed instant. It defaults to `time.time` and reaches `countdown()`, `elapsed_fraction()`, `limit_segment()`, `compose()` and `build()`. | [D] |
| FR-4.4 | The transcript root is a parameter and the rate table and the offsets follow the XDG variables, so section 8 is tested against a fixture tree with nothing patched. A test giving a root must move `XDG_STATE_HOME` too, or the offsets it writes land beside the real ones. | [D] |
| FR-4.5 | Whether the identity row and the meter row should be one row. Two rows is settled against three, which is a different question. | [?] |
| FR-4.6 | Whether the countdown gets its own colour scale, probably inverted: a reset getting closer is good news, which is the opposite direction to consumption. | [?] |
| FR-4.7 | Whether the cost segment carries a staleness marker. The rate table records the date it was taken and nothing reads it. | [?] |
| FR-4.9 | Whether the state file should say what a session is DOING, not only what it has spent. Nothing in it distinguishes a session waiting on a person from one with nothing to do. Raised from silo 2026-08-27. Not a segment: the render sees one payload and has no view of a board. | [?] |
| FR-4.8 | Whether the parameter budget still fits. `limit_segment`, `place_context` and `compose` each carry five parameters, which is exactly what the complexity gate allows. | [?] |
| FR-4.10 | Whether a state file says which KIND of absence its missing context is. FR-1.11g omits an empty value, so a payload carrying no context block writes a file with no context keys, and that is indistinguishable from a session whose window has not been measured yet. Raised by silo 2026-08-28 as the reader: its hand invocation with a stub payload produced a file it could not tell from a real early-session one, and the guard it is adding rejects a malformed value rather than an absent one. The estate met the same shape twice that night, a missing thing and a not-yet thing reading identically at the moment of looking. Adding a key is safe under FR-1.11o where changing the shape is not, so the cheap answer is probably a key rather than a convention. | [?] |

## Retired

An id here is never reused. A reader meeting one in an old commit, a note or
another project's document finds where it went rather than finding it attached
to something unrelated.

| ID | Went | Why, and what replaced it |
|---|---|---|
| FR-1.10 | 2026-08-27 | It wrote no bytecode beside its source. A property of the Python runtime, retired ahead of the Go port that removes the runtime. Nothing replaces it. |
| FR-1.11c | 2026-08-27 | A write that fails costs nothing. Folded into FR-1.11b, which now carries both halves of the atomic write. |
| FR-1.11d | 2026-08-27 | Canonical YAML emitted by hand rather than by importing wrench, justified by the render's runtime staying an answer. Retired 2026-08-27 on a measurement that the Go pack reproduces infobot's bytes, expecting the port to link it. **The port kept the hand-emitter and wrench declined the link on 2026-08-28**, so the emitter this row described is what infobot still does. It is not revived: what it required is now FR-1.11g for the form and FR-1.11q for the spelling of a number. |
| FR-1.11n | 2026-08-27 | The form pinned independently at both ends, infobot's output being a fixture in wrench and infobot asserting its own canonical form. Retired on the same measurement as FR-1.11d, justified by a future state: the port links wrench's pack and there is one emitter. **That state did not arrive.** The port kept the hand-emitter and wrench declined the link on 2026-08-28, so two emitters is the settled answer rather than a temporary one. The guarantee is restored under FR-1.11p, which is a new id because a retired one is never reused, and FR-1.11o carries the half facing the file's readers. **Retiring a row on a future state is the mistake here**, not the reasoning about wrench: it recorded a live gap as a closed one for a day. |
| FR-1.11i | 2026-08-27 | A value's type survives the round trip. Folded into FR-1.11g with the rest of the file's form. |
| FR-1.11j | 2026-08-27 | Keys quoted, sorted and one to a line. Folded into FR-1.11g. |
| FR-1.11k | 2026-08-27 | `written` is ISO 8601 with an offset, to the second. Folded into FR-1.11g. |
| FR-1.11l | 2026-08-27 | The file is readable by other programs. That is why it exists, and FR-1.11 now says so. |
| FR-1.11m | 2026-08-27 | A failed write leaves nothing behind. Folded into FR-1.11b. |
| FR-1.12 | 2026-08-27 | Standard library only, so it ran under whatever `python3` resolved to. A property of the Python runtime, retired ahead of the Go port. The hazard it guarded against does not retire with it; FR-1.13 carries that. |
| FR-1.12a | 2026-08-27 | The interpreter floor, 3.8, set by `Path.unlink(missing_ok=True)`. Retired with FR-1.12, which it qualified. |
| FR-4.1 | 2026-08-28 | Whether infobot has a suite, so the traceability gate means what it says. It does: 109 of 109 settled requirements are cited by a test that asserts them, every package clears 80% per file, and the two entry points are measured by `go build -cover` rather than excluded. The row was a question and the question is answered, so it goes rather than standing as a permanently satisfied assertion. What keeps it true is the gate, not this row. |
| FR-4.2 | 2026-08-28 | Which language, and when. It was a question with an id rather than a line of prose so that closing it would be a change to a row. Answered and closed the same day it was acted on: Go, and the Python is gone. What the port traded is not retired with it and is FR-1.13. |
