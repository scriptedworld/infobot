# infobot, Requirements

What must be true of the status line. Moved out of `silo/REQUIREMENTS.md`
section 4 when infobot became its own repository, and renumbered as this
document's own.

*Derives from:* `bin/infobot`, and the payload shape read out of `claude`
2.1.233.

Requirements are stated as observable properties: what is true of a run, not how
anything is arranged.

**Status markers.** `[A]` traces to something I said. `[D]` is derived from
one. `[A/D]` is both. `[?]` is an open question, recorded so it is not lost and
carrying no test yet.

**A number is an identifier, not a position.** Nothing is renumbered and nothing
is reused, because the ids are cited from the code, from `docs/`, and from the
tasks in `clank`. Section 4 sits at the end of the document while keeping the
number it was first given, and holds both the requirements about being testable
and the questions still open.

**A lettered id is a facet of the row it hangs off.** `FR-1.11a` is one property
of the state file `FR-1.11` requires, and it is a requirement in its own right
with its own test. The traceability checker keys every segment as a number and a
suffix, so the letters sort and cite exactly as the numbers do. Use the shape
where one decision has several separately testable consequences; a row that
merely sits near another one gets its own number.

> The traceability gate does not pass, and that is the honest state. Every
> settled requirement below is uncovered, because infobot has no test suite.
> Marking them `[?]` would make the gate green by lying about what is settled.
> Closing the gap is `clank/tasks/infobot/status-line/`.

## 1. What it reads and writes

| ID | Requirement | |
|---|---|---|
| FR-1.1 | The status line reads the session JSON on standard input and writes complete rows to standard output. Today that is two rows: identity, then meters. The count is a rendering decision and not a requirement; that each row is complete is. | [D] |
| FR-1.2 | It exits 0 whatever it is given. Unparseable input, a non-object payload and an internal error each produce no traceback in the status bar. | [D] |
| FR-1.3 | Every field of the payload is optional and is treated as optional. A segment whose data is absent is **dropped**, never rendered as zero, because zero is a claim and absence is not. | [A/D] |
| FR-1.4 | A failure confined to one segment costs only that segment. Not held today: `main()` wraps the whole render in a bare `except` and emits nothing, so a failure costs every row. A malformed `resets_at` reproduces it, and the payload Claude Code actually sends does not reach it. | [D] |
| FR-1.5 | A row with no parts in it is omitted rather than printed blank. Early in a session the context and rate-limit fields are all absent, and an empty row hanging off the rail reads as a fault rather than as nothing to say. | [D] |
| FR-1.6 | No path makes a network call. The rates it prices with come off disk and the width comes from a host already running, because a status line rendering on every event cannot wait on anything. | [A/D] |
| FR-1.7 | The only files opened are the rate table, the session's own transcripts, the offsets file that records how far into each transcript it has read, and the status file FR-1.11 leaves behind. Everything else it prints comes from the payload it was handed. | [D] |
| FR-1.11 | Every render leaves the session's context state on disk, at one path named by session id beside the offsets. The payload carries numbers nothing else in the session can see, and an agent has no other way to learn how much of its own context is gone: without this it tails its own transcript and sums usage records by hand. | [A] |
| FR-1.11a | The bar and the file come through one measurement. Two formulas would let a bar reading 56% sit beside a file saying something else, and a reader has no way to tell which is wrong. | [D] |
| FR-1.11b | It is written whole or not at all, to a temporary beside the target and renamed into place. A reader between a truncate and a write would otherwise see an empty file and conclude the session had no context, which is worse than seeing the previous check. | [D] |
| FR-1.11c | A write that fails costs nothing. It is guarded like the offsets file: the status line's one hard guarantee is that it renders or shows nothing, and a state file is not worth spending that on. | [D] |
| FR-1.11d | It is YAML in canonical form, emitted by hand, and the hand-emission is a considered duplicate of wrench rather than an alternative to it. wrench owns the form of the ecosystem's structured files and its Python pack round-trips these bytes unchanged, so there is one dialect and not two. It is not imported because it would make the status line's runtime a question rather than an answer: the pack takes `yaml`, `jsonschema` and `referencing` at module level, and whether those resolve depends on which interpreter wins the `env python3` race. That is FR-1.12 applied to a specific temptation, and FR-1.11c is kept rather than traded. | [D] |
| FR-1.11e | Both state files are removed when the session ends, by a `SessionEnd` hook calling `bin/forget-session`. The status line is the only thing that writes them and it is never called again after the last transcript entry, so nothing else would: fifteen offsets files had accumulated before anyone looked. | [A] |
| FR-1.11f | That entry point exits 0 whatever it is given, and refuses a session id carrying a path separator rather than joining it onto a directory it unlinks within. A cleanup that raises is a hook failure reported to somebody closing their terminal. | [D] |
| FR-1.11g | The file carries `session` and `written` always; `cwd`, `model` and `effort` where the payload names them; and `context_used`, `context_size`, `context_remaining` and `context_percent` where the window can be measured at all. A key whose value is empty is omitted rather than written blank, so a reader tells "not said" from "said to be nothing". | [D] |
| FR-1.11h | `context_remaining` is derived rather than read, and never negative: a payload reporting more used than the window holds gives zero. `context_percent` is neither floored nor capped, so a reader sees an over-full window as over-full where the bar can only draw it as full. The two are consistent because the clamp is a drawing limit, not a second measurement. | [D] |
| FR-1.11i | A value's type survives the round trip. Strings are quoted, numbers are bare, a float keeps its decimal point, booleans are `true` and `false`, and a quote or backslash inside a string is escaped. A strict YAML reader gets back a string, an integer and a float, not nine strings. | [D] |
| FR-1.11j | Keys are quoted, sorted, and one to a line, so two consecutive checks differ only on the lines whose numbers moved and a reader diffing them sees the change rather than the reordering. | [D] |
| FR-1.11k | `written` records when the check was taken, ISO 8601 carrying an offset, to the second. A reader has no other way to tell a fresh file from one a killed session left behind. | [D] |
| FR-1.11l | The file is left readable by programs other than infobot, which is the whole reason it exists. | [D] |
| FR-1.11m | A write that fails leaves nothing behind. The temporary is removed on the way out, so a directory of state files never accumulates half-written ones beside the real ones. | [D] |
| FR-1.11n | The form is pinned independently at both ends. infobot's output is a fixture in wrench, so wrench diverging fails its own suites; infobot asserts its own canonical form, so infobot diverging fails infobot's. Neither test suite reaches into the other's tree, because a check that needs a sibling repository present is a check that fails for the wrong reason on a fresh clone. Changing the emitted form is therefore a two-repository change and is meant to feel like one. | [D] |
| FR-1.8 | One subprocess per render at most, and only to ask a host how wide the pane is. The route that answers runs and the others do not. | [D] |
| FR-1.9 | It runs from any working directory with no install step, no virtualenv and nothing that can be half present. Claude Code invokes it by absolute path from wherever the session happens to be, so the entry point resolves the module from its OWN location rather than from the working directory. | [D] |
| FR-1.10 | It writes no bytecode beside its source. Compilation is disabled before the first import rather than after it, because after it is too late: the first run has already left a `__pycache__` in the tree. | [A/D] |
| FR-1.12 | It imports the standard library and nothing else, so it runs under whatever `python3` resolves to rather than under one particular interpreter. Both entry points say `#!/usr/bin/env python3`, which on this machine reaches a mise-managed 3.14 and not the system 3.13, and the two do not carry the same packages: `jsonschema` and `referencing` are present under one and absent under the other. A dependency that resolves or not depending on which interpreter wins a PATH race is what FR-1.9 means by something that can be half present, and here it would mean a blank status line rather than a failed import somebody sees. | [D] |
| FR-1.12a | "Whatever `python3` resolves to" carries a floor, or it is a promise with nothing behind it. The floor is 3.8, and one keyword sets it: `Path.unlink(missing_ok=True)`, in both `forget` functions. Everything else parses as far back as 3.7. The floor is stated so it can be raised deliberately rather than by someone reaching for a newer convenience and moving it without noticing. | [D] |
| FR-1.13 | Whatever the status line is reached through is present or its absence is visible. A compiled binary does not retire FR-1.12's hazard, it relocates it: an import that may not resolve becomes a build that may not have run or a symlink that may dangle, and both fail the same way, as a blank line nobody is told about. `~/bin/bolt` dangles on this machine right now, which is why infobot's own gate is run by invoking its checkers directly. Whichever runtime this ends up on, a missing one is a stated, checkable condition rather than silence. | [A/D] |

## 2. The payload

| ID | Requirement | |
|---|---|---|
| FR-2.1 | Field names are those the running Claude Code sends, established by reading it rather than by inferring from documentation. The payload carries `context_window`, `rate_limits.{five_hour,seven_day}` and `effort.level`, and `resets_at` is `Math.round(Number(i))`, a numeric epoch rather than a timestamp string. | [D] |
| FR-2.2 | Token counts are shown only for the window that carries them. `context_window` carries counts and a percentage, so used-of-total is real; `rate_limits` carries a percentage and a reset time and nothing else, so counts there are absent data rather than a missing feature. | [D] |
| FR-2.3 | A percentage the status line prints is computed by the same formula as the percentage the payload supplies, so the two halves of a segment agree. | [D] |
| FR-2.4 | A countdown already elapsed is omitted rather than printed as `0m`, which would suggest a reset is imminent when it has in fact happened and the number is stale. | [D] |
| FR-2.5 | The context counts are the input side only: input, cache creation and cache read, never output. That is `used_percentage`'s own formula, and including output prints a percentage that disagrees with its own numerator. | [D] |
| FR-2.6 | `current_usage` is the authority for those counts where it is present and `total_input_tokens` is the fallback where it is not. | [D] |
| FR-2.7 | A percentage arriving without counts derives the counts from it, and counts arriving without a percentage derive the percentage. Printing the literal zero gave `0/200k (3% consumed)`, which reads as a fault in the status line rather than as data that has not arrived. | [D] |
| FR-2.8 | The model is `display_name` where the payload has one and `id` where it does not, with the effort level appended when one is set. | [D] |
| FR-2.9 | The project root is shown only when it differs from the working directory, because repeating it is noise on the common case of a session started where the work is. | [A/D] |
| FR-2.10 | A path inside home is shown relative to it. | [A/D] |
| FR-2.11 | The session id is shown as its first eight characters, which is what the harness shows. | [A/D] |
| FR-2.12 | The project root, where it is shown at all, is marked as a root rather than left to read as a second path. The marker is outside the colour the path itself carries, so what is tinted is the path and not the annotation. | [A/D] |
| FR-2.13 | A field the payload gains that infobot does not read changes nothing about the output. Only the named fields are read, so Claude Code adding to the payload is not a change here. | [D] |

## 3. The terminal

| ID | Requirement | |
|---|---|---|
| FR-3.1 | Colour is 24-bit, and the environment that Claude Code's 256-colour cap under `$TMUX` depends on is set in `silo/settings.json` rather than left to the terminal. A host that sets no `$TMUX` never triggers the cap and needs nothing: herdr keeps the 24-bit escapes in its own grid. | [A/D] |
| FR-3.2 | Refresh is time-based as well as event-based. Without it the countdowns move only on Claude Code events, so a quiet session shows a "resets in" that is minutes stale, which is worse than showing none. | [A/D] |
| FR-3.3 | The terminal width is established or reported as unknown, never guessed. Every ordinary route fails under Claude Code: stdout is captured so fds 0/1/2 raise, `COLUMNS` is unset, `/dev/tty` is unopenable, and `shutil.get_terminal_size()` therefore returns its fabricated 80x24. Believing that fallback truncates a 223-column pane to 80. Whatever owns the pane is asked directly, tmux or herdr, innermost first, because tmux inside a herdr pane is the one this line is drawn in. A host that cannot be asked yields "unknown", which means render the full form. | [D] |
| FR-3.4 | A fabricated default returned by a library in place of an answer is treated as absent data. This is FR-1.3 applied to the environment rather than to the payload. | [D] |
| FR-3.5 | The width a row is fitted to is the renderer's budget, and the pane is not that budget. A row measured at 222 columns, which tmux agrees is 222 and which does not wrap in a 223-column pane, came back as `...(11% consumed) ▏ ⟨a5e58a…`. Claude Code indents the status line two columns and then keeps 220, putting its ellipsis in the 220th, so the reserve is three columns of the 223. Establishing the width is not the same as being allowed to use it. The reserve belongs to Claude Code rather than to the multiplexer, so it is the same three under herdr: a row fitted to 194 in a 197-column herdr pane reads back at 196 with its last segment intact. | [D] |
| FR-3.6 | What exceeds the budget is cut from the right rather than wrapped, so the whole cost of a row being too long falls on whatever sits at its end. A meter too small to say anything is dropped rather than drawn at any size: the bar goes before the number it illustrates, and a segment that cannot fit at all moves to another row instead of overflowing. | [A/D] |
| FR-3.7 | The width is the one the pane is DRAWN at, which is not always the one its own rectangle reports. A herdr pane zoomed out of a two-way split is drawn across the whole tab while its rectangle still gives the half it will return to, so the tab's area is what the zoomed pane is fitted to. The zoomed pane is the focused one, and a pane that is not it is not on screen at all. | [D] |
| FR-3.8 | `NO_COLOR` strips every escape, the separator and the rail included, so the output is either coloured or clean and never half of each. A hardcoded escape survives the stripping of the segments around it. | [A/D] |
| FR-3.9 | A row is measured as the terminal will draw it: escapes cost nothing, combining characters cost nothing, and characters of east-asian width `W` or `F` cost two. `len()` is wrong in both directions and both errors run toward a row too wide for the line. | [D] |
| FR-3.10 | Ambiguous-width characters are counted as one column, which is what kitty draws them as. A terminal configured to treat ambiguous as wide would draw every bar at twice its measured width. | [A/D] |
| FR-3.11 | A host that does not answer costs a bounded wait and then counts as unknown. The width query carries a two second timeout, because a hung multiplexer must not hang a line that renders on every event. | [D] |

## 5. The rows

| ID | Requirement | |
|---|---|---|
| FR-5.1 | The rows hang off a left rail: a rounded corner opening the block, a rounded corner closing it, a tee for anything between, and a stub when there is only one row. An opening corner with no closing corner under it reads as a block that failed to finish. | [A/D] |
| FR-5.2 | The context segment rides the identity row when that row can hold it with no bar at all, and moves to the meter row when it cannot. Truncation eats the end of the row, which is the session id, so relocating costs less than overflowing. | [D] |
| FR-5.3 | The context bar takes every column the identity row has left once everything else on it is placed. | [A/D] |
| FR-5.4 | The session id closes the identity row, which puts the bar between two fixed things and makes the room left for it an arithmetic question rather than a guess. | [D] |
| FR-5.5 | The rate limit gauges are a fixed ten cells rather than a share of the slack, so the row below does not move under them as the bar above grows, and ten cells resolves to 10% a cell, which is what a window checked occasionally wants. | [A/D] |
| FR-5.6 | A meter row over budget gives up its gauges for the percentages they were drawing before anything is cut, which costs six columns less per window. | [D] |
| FR-5.7 | The cost is pushed to the right of the last row with at least three columns of clear space between, shortened to the total alone when the full form will not fit, and dropped when neither will. | [D] |
| FR-5.8 | The cost goes on the meter row rather than the identity row, because the identity row has already given its slack to the bar. With no meter row there is nowhere for it that is not somewhere else's space, so it is dropped. | [D] |
| FR-5.9 | Both cost forms come from one reading of the transcripts. Deciding which of two strings fits must not double the only expensive thing on the row. | [D] |
| FR-5.10 | Parts within a row are divided by a separator costing exactly one column, whatever glyph the font provides. The powerline separator and the plain vertical bar it falls back to are one column each, so a font without the glyph shifts nothing. | [A/D] |
| FR-5.11 | The rail is part of what a row costs. Fitting measures the parts joined the way they will be joined AND the three columns the rail takes, because a bar sized against the parts alone overflows by exactly the rail. | [D] |
| FR-5.12 | With the width unknown the bar is a fixed fifty cells. There is nothing to subtract from, so fifty is a chosen number rather than a fit, and it is what "render the full form" means in FR-3.3. | [A/D] |
| FR-5.13 | Each segment opens with a fixed marker, so which meter is which is read from the shape rather than from the numbers: a brain for the context window, an hourglass for the five hour window, a calendar for the seven day one, a note for what the session would have cost and a target for what the cache took off. | [A] |

## 6. The meters and the palette

| ID | Requirement | |
|---|---|---|
| FR-6.1 | The consumption ramp is two straight lines, green to yellow from 0 to 75 and yellow to red from 75 to 90, so the colour moves fastest exactly where a glance needs to tell 80 from 88. | [A/D] |
| FR-6.2 | At 90 and above the style inverts rather than merely reddening. Past that point the message is not "high" but "about to matter", and a hue change alone stops being seen after the twentieth time. | [A] |
| FR-6.2a | The inversion FADES IN across 90 to 100 rather than switching on at 90. At 90 the foreground is the red the ramp beneath it arrives at and the background is the TERMINAL'S OWN, so there is nothing to see at the boundary; at 100 it is pale yellow on deep red. Both ends move together, so the background filling in and the foreground brightening are one movement. Bold is the one part that cannot fade and is on across the whole band. Black is not the right starting background and was the first attempt: the palette here is Tokyo Night at `#1a1b26`, against which pure black is a dark notch, which is the seam the fade exists to remove. It follows the desktop's palette per FR-6.11, so a theme change moves it. | [A] |
| FR-6.3 | Each filled cell is coloured for the percentage IT stands for and not for the bar's total, so the fade is a fixed property of the bar and only its length moves. | [D] |
| FR-6.4 | The last filled cell carries the same colour as the number printed beside it, within one cell of rounding, because they mean the same thing. | [D] |
| FR-6.5 | Every run of cells sharing a style opens with a reset before the style. The alarm style carries bold and a background as well as a foreground, so a bare colour change after it leaves both switched on for the rest of the line. | [D] |
| FR-6.6 | One escape per run of cells sharing a style, not one per cell. Below the ramp's pivot a long bar resolves to a handful of distinct shades, so this is most of the escapes saved for nothing given up. | [D] |
| FR-6.7 | The bar is never cut into. Every cell is a filled or an empty parallelogram and nothing else, and the percentage is printed with the counts where it reads as a number rather than as something to be found among the parallelograms. | [A/D] |
| FR-6.8 | A bar of no cells renders nothing at all. A percentage below 0 renders all empty and one above 100 renders all filled, rather than either running off the end. | [D] |
| FR-6.9 | Token counts are compact: below a thousand as they are, thousands as `k` with no decimal, millions as `M` with one. | [A/D] |
| FR-6.10 | The empty cells carry an outline glyph and no background. A background would fill the gaps between the parallelograms and turn the tail of the bar into a solid slab, which is what the outline is there to avoid. | [A] |
| FR-6.11 | The colours are taken from the palette the rest of the desktop already uses rather than picked to suit this line, so the status line reads as part of the desktop instead of beside it. The path is the ENCOM teal the i3 bar and claws use, and the bar's empty cells are the dim cyan one step up from that bar's inactive workspace. | [A] |
| FR-6.12 | The rail is drawn in the same colour as the bar's empty cells rather than in one of its own, so the frame stays one voice with the meter and neither competes with the numbers. | [A] |
| FR-6.13 | A word that is present to be scanned past rather than read is dimmed. Only the connective words are: the figures beside them are not. | [A/D] |

## 7. The rate limit windows

| ID | Requirement | |
|---|---|---|
| FR-7.1 | A window's gauge carries two readings at once without costing a column: its LENGTH is how much is spent, its COLOUR is whether that is a problem. A glyph beside it would cost a column and would quantise a continuous number into a handful of steps. | [A/D] |
| FR-7.2 | The verdict is where the window is projected to LAND: spend divided by how far through the window it is. A plain ratio puts its neutral point at arriving exactly full, which is running out rather than being fine. | [A/D] |
| FR-7.3 | The pace scale DIVERGES, with green at landing exactly full as the window resets, because that is the best outcome available rather than an alarm. Above it the window empties early and runs yellow into red; below it the allowance goes unspent and pales through white into blue. | [A] |
| FR-7.4 | The verdict fades in against green rather than switching on, reaching full colour at 60% of the window elapsed and squared so it arrives late. A percent spent two minutes into five hours divides by almost nothing and projects a catastrophe. | [A/D] |
| FR-7.5 | The fade is a fraction of the window rather than a duration, so the seven day window matures at the same point in its own life instead of after an afternoon. | [D] |
| FR-7.6 | Green is what the verdict fades toward, rather than grey or nothing, because green is this scale's "no comment" as well as its "on rate" and both mean there is nothing to act on. | [A] |
| FR-7.7 | Beyond the ends of the scale the colour clamps. A window projected to land at 400% is the same red as one landing at 150: once it will not last, by how much it will not last stops changing what to do about it. | [D] |
| FR-7.8 | A window with no reset time has no position in its window, so its gauge falls back to the consumption ramp and means what the context meter's colour means. | [D] |
| FR-7.9 | The countdown is the largest two non-zero units, and it stays a number because a bar cannot carry it: half spent with four hours to go reads very differently from half spent with ten minutes to go. | [A/D] |
| FR-7.10 | The window lengths are five hours and seven days, taken from the payload's own field names rather than configured separately. | [D] |
| FR-7.11 | The pace arithmetic cannot divide by nothing and cannot run backwards. How far through the window we are is held between none of it and all of it, so a reset further out than the window is long reads as the start rather than as a negative; and the divisor carries a floor, so a projection made a moment into a window is large rather than infinite. The floor is arithmetic only. What stops a wild early projection being BELIEVED is the fade in FR-7.4. | [D] |

## 8. The cost

| ID | Requirement | |
|---|---|---|
| FR-8.1 | The figure is a counterfactual and not a bill. It is what the same tokens would have cost through the API at list rates, which is what makes the cache worth anything visible. | [A] |
| FR-8.2 | The session's transcripts are found by globbing for the session id, not by rebuilding the directory slug from the working directory, because a session may have been started somewhere other than where it now is. | [D] |
| FR-8.3 | Subagent transcripts are counted. On one measured session they were 51% of output tokens and 33% of cache reads, so leaving them out is wrong by about half. | [D] |
| FR-8.4 | Transcripts are grouped by ROOT, being a transcript's recorded origin where it has one and its own name where it does not, so the transcripts a `/clear` left behind are counted. A clear opens a new transcript under a NEW session id and the two ids point opposite ways, so matching on the id alone follows one side of it. | [D] |
| FR-8.5 | The search stays inside the project directory the session belongs to, so its cost is proportional to one project's sessions rather than to every session on the machine. Only a session id that matches nothing widens the search. | [D] |
| FR-8.6 | Only the bytes appended since the last render are parsed. A full re-sum of a 2.7MB transcript measured 21ms to 29ms against a render budget of 33ms, and it grows for the life of the session. | [D] |
| FR-8.7 | The offset advances only over lines that arrived complete, because the transcript is being appended to by the very session that is rendering and can be read mid-line. | [D] |
| FR-8.8 | A transcript that has shrunk is read from the start. It was rotated or replaced, so the offset held for it means nothing against the new one. | [D] |
| FR-8.9 | State is kept per file rather than as one running sum, because subagent transcripts appear part way through a session and a single offset cannot say which of them a total already includes. | [D] |
| FR-8.10 | State that cannot be read or written costs a re-sum and never a wrong figure, so the failure is not worth reporting. | [D] |
| FR-8.11 | Each model is charged at its own rate and the session is the sum of them, not an average. | [D] |
| FR-8.12 | A model absent from the rate table is left out and the total is flagged incomplete rather than abandoned, so one unknown model costs the exactness of the figure and not the figure. The flag is shown as a trailing plus, meaning the figure is a floor. No segment at all appears only when nothing could be priced. | [A/D] |
| FR-8.13 | Cache reads and cache writes are priced apart rather than lumped together as "cache". A read is CHARGED, at a tenth of input: 90% off is not free, and on a long session it is the largest single line. A write costs more than a fresh input token. | [D] |
| FR-8.14 | The saving is every cached token, read or written, charged at the plain input rate instead, less what those tokens did cost. That is what the session would have cost with no caching at all. | [D] |
| FR-8.15 | The rates come from `~/.config/infobot/pricing.json` when it is there and from the seed in the module when it is not, so a fresh clone renders with no config file and no network. Both carry the date they were taken. | [D] |
| FR-8.16 | A rate table that is missing, malformed or carrying no rates falls back to the seed rather than raising. A stale rate is a smaller wrong than a blank row. | [D] |
| FR-8.17 | Money is printed at the precision the number deserves rather than always two decimals. | [A/D] |
| FR-8.18 | The saving is shown only when there is one. A session that wrote cache blocks and never read them back spent MORE than it would have with no caching, so the figure is negative and the clause is dropped rather than printed as a loss. The total is still shown. | [D] |
| FR-8.19 | Cache creation counts are read from the nested `cache_creation` object as well as from the top level of a usage record, because that is where the transcript puts the per-duration breakdown. Reading only the top level counts the writes as nothing. | [D] |
| FR-8.20 | A usage record naming no model is counted under a placeholder that no rate table answers to, so it flags the total incomplete rather than being priced at whatever model happened to be next to it. | [D] |
| FR-8.21 | Only numeric values are added. A usage field carrying anything else is ignored rather than coerced, and it does not abandon the record it appeared in. | [D] |
| FR-8.22 | The counted fields are input, output, cache read, and the two ephemeral cache writes. Output is counted here and NOT in the context percentage, which is FR-2.5: what a window holds and what a session is billed are different questions. | [D] |
| FR-8.23 | The cache multipliers are configurable beside the per-model rates, so a change to how caching is priced needs no code change, just as a change to a model's rate does not. | [D] |
| FR-8.24 | Looking for the session a transcript belongs to gives up after a bounded number of records. The field first appeared on record 18 of a transcript opened by a clear, and the bound is 40, so a large transcript that never carries one costs a bounded read rather than a full scan. | [D] |

## 4. Being testable, and what is still open

Numbered 4 because that is the number these rows were first given, and cited
from `docs/PROJECT.md`, `NEXT_STEPS.md` and the code under it. The first two
rows are settled requirements about the shape of the code; the rest carry `[?]`
and are questions.

| ID | Requirement | |
|---|---|---|
| FR-4.3 | The clock is a parameter, so FR-2.4, FR-7.4 and FR-7.9 are asserted against a fixed instant rather than against a shape. It defaults to `time.time` and reaches `countdown()`, `elapsed_fraction()`, `limit_segment()`, `compose()` and `build()`, so a whole row can be rendered at a chosen moment. Nothing but a test passes anything else. | [D] |
| FR-4.4 | The transcript root is a parameter and the rate table and the offsets follow the XDG variables, so section 8 is tested against a fixture tree with nothing patched. `usage.transcripts()`, `usage.totals()` and `render.cost_forms()` take the root; `XDG_CONFIG_HOME` moves the rate table and `XDG_STATE_HOME` the offsets. A test giving a root must move `XDG_STATE_HOME` too, or the offsets it writes land beside the real ones and a later render skips bytes it never counted. | [D] |
| FR-4.1 | infobot has a test suite, so the requirements above are held to something and the traceability gate means what it says. Two oracles exist already: `clank/tasks/infobot/status-line/05-*/golden/` holds 18 captured cases, and `test-plan.md` beside it lists properties to assert. The work is split across tasks 10, 12, 14 and 16, by section, because 112 settled requirements is more than one session. | [?] |
| FR-4.2 | The implementation language, and the timing, which stayed open long after the language did not. Both settled 2026-08-26: Go, starting now, leaf-first across tasks 05 to 20 with the Python rendering until the last of them. This row closes when the Python is gone. FR-1.12 and FR-1.12a are DELETED rather than marked done when it is: they are about an interpreter that will no longer run anything. | [?] |
| FR-4.5 | Whether the identity row and the meter row should be one row. They were split when the meter row arrived and the bar now fills row one, so the reason may have expired. Two rows is settled against three, which is a different question. | [?] |
| FR-4.6 | Whether the countdown gets its own colour scale, probably inverted: a reset getting closer is good news, which is the opposite direction to consumption. Left uncoloured rather than guessed at. | [?] |
| FR-4.7 | Whether the cost segment carries a staleness marker. The rate table records the date it was taken, nothing reads it, and a figure priced from three week old rates looks exactly like a correct one. The intended place is beside the plus that already means "a model here has no rate". | [?] |
| FR-4.8 | Whether the parameter budget still fits. `limit_segment`, `place_context` and `compose` each carry five parameters, which is exactly what the complexity gate allows, so the next one added anywhere in that chain fails the gate rather than merely reading badly. A context object is the usual answer and the Go port is where it would land. | [?] |
