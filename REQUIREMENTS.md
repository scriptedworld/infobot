# infobot, Requirements

What must be true of the status line. Moved out of `silo/REQUIREMENTS.md`
section 4 when infobot became its own repository, and renumbered as this
document's own.

*Derives from:* `bin/infobot`, and the payload shape read out of `claude`
2.1.233.

Requirements are stated as observable properties. Each says what is true of a
run, not how anything is arranged.

**Status markers.** `[A]` traces to a statement of Jeff's. `[D]` is derived from
one. `[A/D]` is both. `[?]` is an open question, recorded so it is not lost and
carrying no test yet.

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
| FR-1.4 | A failure confined to one segment costs only that segment. This is not held today: `main()` wraps the whole render in a bare `except` and emits nothing, so a failure costs every row rather than one line. A malformed `resets_at` reproduces it. The payload Claude Code actually sends does not reach it, so it is not urgent; but the file states this property and does not hold it, and the cost of the gap grew with the second row. | [D] |

## 2. The payload

| ID | Requirement | |
|---|---|---|
| FR-2.1 | Field names are those the running Claude Code sends, established by reading it rather than by inferring from documentation. The payload carries `context_window`, `rate_limits.{five_hour,seven_day}` and `effort.level`, and `resets_at` is `Math.round(Number(i))`, a numeric epoch rather than a timestamp string. | [D] |
| FR-2.2 | Token counts are shown only for the window that carries them. `context_window` carries counts and a percentage, so used-of-total is real; `rate_limits` carries a percentage and a reset time and nothing else, so counts there are absent data rather than a missing feature. | [D] |
| FR-2.3 | A percentage the status line prints is computed by the same formula as the percentage the payload supplies, so the two halves of a segment agree. | [D] |
| FR-2.4 | A countdown already elapsed is omitted rather than printed as `0m`, which would suggest a reset is imminent when it has in fact happened and the number is stale. | [D] |

## 3. The terminal

| ID | Requirement | |
|---|---|---|
| FR-3.1 | Colour is 24-bit, and the environment that Claude Code's 256-colour cap under `$TMUX` depends on is set in `silo/settings.json` rather than left to the terminal. | [A/D] |
| FR-3.2 | Refresh is time-based as well as event-based. Without it the countdowns move only on Claude Code events, so a quiet session shows a "resets in" that is minutes stale, which is worse than showing none. | [A/D] |
| FR-3.3 | The terminal width is established or reported as unknown, never guessed. Every ordinary route fails under Claude Code: stdout is captured so fds 0/1/2 raise, `COLUMNS` is unset, `/dev/tty` is unopenable, and `shutil.get_terminal_size()` therefore returns its fabricated 80x24. Believing that fallback truncates a 223-column pane to 80, which is worse than not adapting at all. tmux is asked directly; anything else yields "unknown", which means render the full form. | [D] |
| FR-3.4 | A fabricated default returned by a library in place of an answer is treated as absent data, not as data. This is FR-1.3 applied to the environment rather than to the payload, and the rule is the same: a confident wrong number is worse than a missing one. | [D] |
| FR-3.5 | The width a row is fitted to is the renderer's budget, and the pane is not that budget. A row measured at 222 columns, which tmux agrees is 222 and which does not wrap in a 223-column pane, came back as `...(11% consumed) ▏ ⟨a5e58a…`. Claude Code indents the status line two columns and then keeps 220, putting its ellipsis in the 220th, so the reserve is three columns of the 223. This is the other half of FR-3.3: establishing the width is not the same as being allowed to use it. | [D] |
| FR-3.6 | What exceeds the budget is cut from the right rather than wrapped, so the whole cost of a row being too long falls on whatever sits at its end. A meter too small to say anything is therefore dropped rather than drawn at any size: the bar is dropped before the number it illustrates is, and a segment that cannot fit at all moves to another row instead of overflowing. | [A/D] |

## 4. Open

| ID | Requirement | |
|---|---|---|
| FR-4.1 | infobot has a test suite, so the requirements above are held to something and the traceability gate means what it says. The oracle already exists: `clank/tasks/infobot/status-line/05-*/golden/` holds 18 captured cases, and `test-plan.md` beside it lists the properties each requirement wants asserted. | [?] |
| FR-4.2 | The implementation language. Go is decided and the reasoning is in the task; this row closes when the port lands and the Python is gone. | [?] |
