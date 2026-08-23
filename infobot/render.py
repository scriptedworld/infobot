#!/usr/bin/env python3
"""statusline -- what Claude Code shows at the bottom of the screen.

Reads the session JSON on stdin and prints one line. Claude Code runs this on
every event, so it stays a formatter: no network, no subprocesses, no file
reads beyond stdin.

WHAT THE INPUT CAN AND CANNOT ANSWER, from the documented schema:

  context_window   gives token counts AND a percentage, so used/total is real.
                   used_percentage counts INPUT ONLY -- input + cache_creation +
                   cache_read, never output -- so the counts shown here use the
                   same formula. Mixing them would print a percentage that does
                   not match its own numerator.

  rate_limits      gives used_percentage and resets_at and NOTHING ELSE. There
                   are no token counts for the 5-hour or 7-day windows, so those
                   segments show a percentage and a countdown. Asking for
                   used/total there is not a missing feature, it is absent data.

EVERY FIELD IS OPTIONAL. rate_limits appears only for Claude.ai subscribers and
only after the first API response; either window can be absent on its own;
used_percentage can be null early in a session. A status line that raises is a
status line that shows nothing at all, so every read is guarded and a missing
segment is dropped rather than printed as zero -- zero is a claim, absence is
not.
"""
from __future__ import annotations

import json
import os
import re
import sys
import time
import unicodedata

BRAIN, HOURGLASS, CALENDAR = "🧠", "⏳", "📅"
# Powerline thin separator from Iosevka Nerd Font, which kitty is configured
# with (font_family "Iosevka Nerd Font", style Light). A plain │ is the fallback
# anywhere the glyph is missing -- it is one character either way, so nothing
# shifts.
SEP_GLYPH = "\ue0b1"
SEP_COLOUR = "\033[38;2;70;80;85m"   # dim, so it divides without competing
DIM = "\033[38;2;120;130;135m"       # connective words -- present, not read

# The left rail, out of oh-my-zsh's multiline prompt: a rounded corner opening
# the block, a rounded one closing it, and a tee for any row between. It does
# one useful thing besides looking like something -- it says the two rows are
# one block rather than two neighbours, which matters when the row above is
# full width and the row below is half of it.
#
# A lone row gets the stub instead. \u256d with no \u2570 under it reads as a block that
# failed to finish, which is a worse thing to say than nothing.
RAIL_TOP, RAIL_MID, RAIL_END, RAIL_ONE = "\u256d\u2500 ", "\u251c\u2500 ", "\u2570\u2500 ", "\u2576\u2500 "

# The glyphs Claude Code draws in its own compaction meter, so the two read as
# one instrument rather than two dialects. They carry no sub-cell steps the way
# the eighth-blocks did, so every bit of resolution now comes from the cell
# count -- which is why the bar takes whatever columns row one has left instead
# of a fixed handful. At 40 cells that is 2.5% each.
FILLED, EMPTY = "▰", "▱"

# Unknown width means tmux could not be asked, so there is nothing to subtract
# from and 50 is a chosen number rather than a fit.
#
# BAR_MIN is the width below which the bar is DROPPED rather than clamped to.
# Clamping was the obvious thing and it is wrong: in a 120-column pane a long
# path leaves six columns, so an eight-cell floor overflows and the row wraps,
# which costs the whole line to save a bar that at 12% a cell was not saying
# much anyway. The counts and the percentage stay either way.
#
# MARGIN is not one column of politeness, it is what Claude Code keeps for
# itself, and the pane width is NOT the budget.
#
# FACT 2026-08-22: a row this script measured at 222 columns, which tmux agrees
# is 222 and does not wrap in a 223-column pane, still came back truncated:
#
#   ...107k/1.0M (11% consumed) ▏ ⟨a5e58a…
#
# The renderer indents the status line two columns and then keeps 220, putting
# its ellipsis in the 220th. So the reserve is three: two for the indent and
# one it leaves at the right. Anything longer is not wrapped, it is cut, and
# the session id is what gets cut because it is last.
BAR_FALLBACK, BAR_MIN, MARGIN = 50, 8, 3

RESET = "\033[0m"

# TRUECOLOR. FACT 2026-08-19: COLORTERM=truecolor and CLAUDE_CODE_TMUX_TRUECOLOR=1
# is set in settings.json, which is exactly what defeats Claude Code's habit of
# capping colour at the 256 palette when it sees $TMUX. A 24-bit escape reaches
# the terminal intact, so the ramp can be continuous instead of stepped.
#
# Two straight lines rather than one. Green to yellow across the long stretch
# where nothing is happening, then yellow to red compressed into 75-90, so the
# colour moves fastest exactly where a glance needs to tell 80 from 88.
GREEN = (60, 200, 90)
YELLOW = (235, 220, 40)
RED = (225, 45, 45)
PIVOT = 75.0
ALARM_AT = 90.0
ALARM = "\033[1;38;2;250;240;120;48;2;180;25;25m"   # bold pale yellow on deep red

# The ENCOM teal, the same value the i3 bar and claws use ($encom_teal,
# #00a595). Taking it from the existing palette rather than picking a cyan means
# the status line reads as part of the desktop instead of beside it.
PATH_COLOUR = "\033[38;2;0;165;149m"

# The unused cells are an outline glyph and NO background. ▱ carries its own
# shape, so it reads as an empty slot unaided, and a background behind it would
# fill the gaps between the parallelograms and turn the tail of the bar into a
# solid slab -- the very thing the outline is there to avoid.
#
# The value comes from the ENCOM palette in the i3 config rather than being
# picked by eye: $encom_dimcyan, one step up from the deepcyan the i3 bar uses
# for `inactive_workspace`.
EMPTY_COLOUR = "\033[38;2;0;95;95m"  # $encom_dimcyan #005f5f


def cyan(text: str) -> str:
    return text if plain() else f"{PATH_COLOUR}{text}{RESET}"


def _mix(a: tuple, b: tuple, t: float) -> tuple:
    t = max(0.0, min(1.0, t))
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def ramp(pct: float) -> str:
    """Just the escape for a percentage, with no text and no reset.

    colour() closes itself with a reset after every call, so a bar built from it
    would emit an open and a close around each of forty cells. The bar needs the
    code alone, so it can open a span once and hold it for every cell that
    shares a colour.
    """
    if pct >= ALARM_AT:
        return ALARM
    if pct <= PIVOT:
        r, g, b = _mix(GREEN, YELLOW, pct / PIVOT)
    else:
        r, g, b = _mix(YELLOW, RED, (pct - PIVOT) / (ALARM_AT - PIVOT))
    return f"\033[38;2;{r};{g};{b}m"


def colour(pct: float, text: str) -> str:
    """Wrap text in a colour interpolated from the percentage consumed."""
    if not text or plain():
        return text
    if pct >= ALARM_AT:
        # Inverted rather than merely red: past this point the message is not
        # "high" but "about to matter", and a hue change alone stops being seen
        # after the twentieth time.
        return f"{ALARM}{text}{RESET}"
    if pct <= PIVOT:
        r, g, b = _mix(GREEN, YELLOW, pct / PIVOT)
    else:
        r, g, b = _mix(YELLOW, RED, (pct - PIVOT) / (ALARM_AT - PIVOT))
    return f"\033[38;2;{r};{g};{b}m{text}{RESET}"


def terminal_width() -> int | None:
    """Columns, or None when it genuinely cannot be known.

    FACT 2026-08-20: every ordinary route fails here. Claude Code captures
    stdout, so fds 0, 1 and 2 all raise OSError; COLUMNS is unset; /dev/tty is
    "No such device or address"; and `shutil.get_terminal_size()` therefore
    returns its fabricated 80x24 fallback.

    THAT FALLBACK IS THE TRAP. Believing it would truncate a 223-column pane to
    80 -- worse than not adapting at all. So the shutil default is never used:
    tmux is asked directly, and anything else returns None, meaning "render the
    full form and let the caller fit it".

    Measured at 3.2ms, which is affordable once per render.
    """
    if not os.environ.get("TMUX"):
        return None
    try:
        import subprocess

        out = subprocess.run(
            ["tmux", "display-message", "-p", "#{pane_width}"],
            capture_output=True, text=True, timeout=2,
        )
        return int(out.stdout.strip()) or None
    except Exception:
        return None


ANSI = re.compile(r"\033\[[0-9;]*m")


def visible_width(text: str) -> int:
    """Columns a rendered string occupies, escapes and all.

    Sizing the bar by subtraction only works if the subtrahend is what the
    terminal will actually draw. `len()` is not that: it counts every byte of
    an escape sequence as a column and the emoji as one column when they take
    two. Both errors run the same way, toward a bar too wide for the line.

    Ambiguous-width characters -- ▰ and ▱ among them -- are counted as one,
    which is what kitty draws them as. A terminal configured to treat ambiguous
    as wide would need this changed, and would double the bar.
    """
    return sum(
        0 if unicodedata.combining(ch)
        else 2 if unicodedata.east_asian_width(ch) in "WF"
        else 1
        for ch in ANSI.sub("", text)
    )


def bar(pct: float, cells: int = BAR_FALLBACK) -> str:
    """A proportional bar whose fill fades along the ramp, cell by cell.

    Each cell is coloured for the percentage IT stands for, not for the bar's
    total: the first is green because it means 2%, and the last filled one
    matches the colour of the number beside it because they mean the same
    thing. So the fade is a fixed property of the bar and only its length moves.

    The bar is never cut into. The percentage is printed with the counts, where
    it can be read as a number rather than found among the parallelograms.
    """
    pct = max(0.0, min(100.0, pct))
    if cells <= 0:
        return ""
    full = round(pct / 100 * cells)
    glyphs = [FILLED] * full + [EMPTY] * (cells - full)

    if plain():
        return "".join(glyphs)

    # One escape per RUN of cells sharing a style, not one per cell. Below the
    # ramp's pivot a forty-cell bar resolves to a handful of distinct shades, so
    # this is most of the escapes saved for nothing given up.
    out: list[str] = []
    held = ""
    for i, glyph in enumerate(glyphs):
        style = ramp((i + 1) / cells * 100) if i < full else EMPTY_COLOUR
        if style != held:
            # RESET first, always. ALARM carries bold and a background as well
            # as a colour, so a bare colour change after it leaves both of them
            # switched on for the rest of the line.
            out.append(RESET + style)
            held = style
        out.append(glyph)
    out.append(RESET)
    return "".join(out)


def plain() -> bool:
    """NO_COLOR is honoured for EVERY escape, not just the obvious ones.

    An earlier version hardcoded the separator and the bar's brackets as
    constants, so `NO_COLOR=1` stripped the segments while leaving those
    behind -- output that was neither coloured nor clean.
    """
    return bool(os.environ.get("NO_COLOR"))


def dim(text: str) -> str:
    # An empty span would still emit an open and a reset with nothing between,
    # which is invisible but wasteful -- and it happens at both ends of the bar,
    # where the fill or the track is empty.
    if not text or plain():
        return text
    return f"{DIM}{text}{RESET}"


def sep() -> str:
    return f" {SEP_GLYPH} " if plain() else f" {SEP_COLOUR}{SEP_GLYPH}{RESET} "


def rail(rows: list[str]) -> list[str]:
    """Hang the rows off a left rail, corners rounded.

    Drawn in the same dimcyan as the bar's empty cells rather than in its own
    colour, so the frame stays one voice with the meter and neither of them
    competes with the numbers.
    """
    if not rows:
        return rows
    if len(rows) == 1:
        leads = [RAIL_ONE]
    else:
        leads = [RAIL_TOP] + [RAIL_MID] * (len(rows) - 2) + [RAIL_END]
    if plain():
        return [lead + row for lead, row in zip(leads, rows)]
    return [f"{EMPTY_COLOUR}{lead}{RESET}{row}" for lead, row in zip(leads, rows)]


def tokens(n: float) -> str:
    """Compact token counts. 142000 -> 142k, 1000000 -> 1.0M."""
    n = float(n)
    if n >= 1_000_000:
        return f"{n / 1_000_000:.1f}M"
    if n >= 1_000:
        return f"{n / 1_000:.0f}k"
    return f"{n:.0f}"


def countdown(resets_at: float | None) -> str:
    """Time until reset, as the largest two units that are non-zero.

    Returns "" when the timestamp is missing or already past -- a countdown
    reading "0m" would suggest a reset is imminent when it has in fact already
    happened and the number is simply stale.
    """
    if not resets_at:
        return ""
    remaining = int(float(resets_at) - time.time())
    if remaining <= 0:
        return ""
    d, remaining = divmod(remaining, 86400)
    h, remaining = divmod(remaining, 3600)
    m = remaining // 60
    if d:
        return f"{d}d{h}h"
    if h:
        return f"{h}h{m:02d}m"
    return f"{m}m"


def home_relative(path: str, home: str) -> str:
    if path == home:
        return "~"
    if home and path.startswith(home + "/"):
        return "~" + path[len(home):]
    return path


def context_segment(cw: dict, cells: int = BAR_FALLBACK) -> str:
    """used/total and a percentage, or nothing if the numbers are not there yet.

    `cells` of 0 drops the bar and keeps the numbers, which is both what a pane
    too narrow to carry a bar gets and how build() measures the room for one.

    PREFERENCE 2026-08-20: the WHOLE span is coloured here, while the rate-limit
    segments colour only their number. That asymmetry is deliberate and is not
    an oversight to tidy up. The context window is the thing watched constantly
    while working; the other two are infrequent details. A wider block of colour
    makes the loud one loud.
    """
    size = cw.get("context_window_size")
    pct = cw.get("used_percentage")
    if not size:
        return ""

    # Match used_percentage's own formula: input side only. current_usage is the
    # authority when present; total_input_tokens is the fallback.
    usage = cw.get("current_usage") or {}
    if usage:
        used = sum(
            float(usage.get(k) or 0)
            for k in ("input_tokens", "cache_creation_input_tokens", "cache_read_input_tokens")
        )
    else:
        used = float(cw.get("total_input_tokens") or 0)

    if pct is None:
        pct = (used / float(size)) * 100 if size else 0
    elif not used:
        # Counts absent but a percentage present. Deriving the count from the
        # percentage keeps the two halves agreeing; printing the literal 0 gave
        # "0/200k 3%", which reads as a bug in the status line rather than as
        # missing data.
        used = float(size) * float(pct) / 100
    pct = float(pct)
    # The percentage sits with the counts, where it reads as one measurement
    # rather than as a number to be found inside the picture of it. The bar is
    # left whole.
    meter = bar(pct, cells)
    counts = colour(pct, f"{tokens(used)}/{tokens(size)} ({pct:.0f}% consumed)")
    # Joined rather than interpolated, so dropping the bar drops its separating
    # space with it instead of leaving a gap the emoji makes conspicuous.
    return " ".join(part for part in (BRAIN, meter, counts) if part)


def row_width(parts: list[str]) -> int:
    """Columns a row costs: its parts joined the way they will be joined, so
    the separators between them count, and the rail it hangs off."""
    return visible_width(sep().join(parts)) + visible_width(RAIL_TOP)


def fitted(cw: dict, others: list[str], at: int, width: int | None) -> str:
    """The context segment with its bar filling whatever the row leaves over.

    `others` is the rest of the row and `at` is where this segment sits in it,
    so what gets measured is the line as it will be printed rather than a sum
    of pieces that forgets the separators.

    One pass, because the rest of the segment is the same width whatever the
    bar does: one more cell is one more column and nothing else moves.
    """
    if not width:
        return context_segment(cw, BAR_FALLBACK)

    # The bar also brings the space that separates it from the counts, which
    # the bar-less form measured here does not have.
    bare = context_segment(cw, 0)
    spare = width - MARGIN - row_width(others[:at] + [bare] + others[at:]) - 1
    cells = spare if spare >= BAR_MIN else 0
    return context_segment(cw, cells) if cells else bare


def limit_segment(emoji: str, label: str, window: dict | None) -> str:
    """Percentage and countdown. No token counts exist for these windows."""
    if not window:
        return ""
    pct = window.get("used_percentage")
    if pct is None:
        return ""
    left = countdown(window.get("resets_at"))
    pct = float(pct)
    # ONLY the percentage takes the ramp -- the same ramp the context segment
    # uses, so 80% is the same shade wherever it appears. The label, the word
    # "consumed" and the countdown stay plain: they are not the measurement.
    #
    # The countdown is deliberately UNCOLOURED rather than colourless by
    # oversight. It wants its own scale and probably an inverted one -- a reset
    # getting closer is good news, so it would run TOWARD green as it nears
    # zero, which is the opposite direction to consumption. Undecided, so left
    # plain rather than guessed at.
    body = f"{label} {colour(pct, f'{pct:.0f}%')} {dim('consumed')}"
    return f"{emoji} {body}" + (f" {dim('resets')} {left}" if left else "")


def model_part(data: dict) -> str:
    """The model, and the effort level when one is set."""
    model = (data.get("model") or {}).get("display_name") or (data.get("model") or {}).get("id")
    effort = (data.get("effort") or {}).get("level")
    if not model:
        return ""
    return f"{model} - {effort}" if effort else model


def path_parts(data: dict, home: str) -> list[str]:
    """Where you are, and where the project root is when it differs.

    Repeating the root is noise on the common case, which is a session started
    where the work is.
    """
    ws = data.get("workspace") or {}
    cwd = ws.get("current_dir") or ""
    root = ws.get("project_dir") or ""
    parts = [cyan(home_relative(cwd, home))] if cwd else []
    if root and root != cwd:
        parts.append("⌂ " + cyan(home_relative(root, home)))
    return parts


def session_part(data: dict) -> str:
    """The first eight of the session id, which is what the harness shows."""
    sid = data.get("session_id")
    return f"⟨{str(sid)[:8]}⟩" if sid else ""


def limit_parts(data: dict) -> list[str]:
    """The two rate limit windows, each dropped when its data is absent."""
    limits = data.get("rate_limits") or {}
    return [
        seg
        for seg in (
            limit_segment(HOURGLASS, "5hr", limits.get("five_hour")),
            limit_segment(CALENDAR, "7d", limits.get("seven_day")),
        )
        if seg
    ]


def place_context(cw: dict, identity: list[str], tail: list[str], usage: list[str],
                  width: int | None) -> None:
    """Put the context segment on whichever row can hold it, bar sized to fit.

    Row one is where it belongs, but only if row one can hold it with no bar at
    all. A narrow split or a long path puts it over the budget before a single
    cell of bar is added, and then it goes to the meter row rather than being
    truncated: what truncation eats is the end of the row, which is the session
    id.

    Both lists are appended to in place, because the caller's two rows are the
    only thing this decides between and returning a pair of them reads worse
    than saying which one it chose.
    """
    if not context_segment(cw, 0):
        return
    if not width or row_width(identity + [context_segment(cw, 0), *tail]) <= width - MARGIN:
        identity.append(fitted(cw, identity + tail, len(identity), width))
    else:
        usage.insert(0, fitted(cw, usage, 0, width))


def build(data: dict, home: str, width: int | None = None) -> list[str]:
    """Two rows, composed from segments that each render themselves.

    Claude Code renders each printed line as its own row. The context window is
    the meter watched constantly while working, so it rides the top row with
    the model and the path, and its bar is given every column the row has left
    over. The rate-limit windows are the infrequent detail and go below.

    The session id closes the top row, which puts the bar between two fixed
    things and makes "the rest of the line" an arithmetic question rather than
    a guess: render the row with an empty bar, measure it, and the bar is the
    difference.

    A row with nothing in it is omitted rather than printed blank: early in a
    session the context and rate-limit fields are all absent, and a stray empty
    row is a visible gap that looks like a fault. Each part above returns "" or
    an empty list when its data is missing, so absence is dropped here by one
    filter rather than by a condition per segment.
    """
    identity = [part for part in (model_part(data), *path_parts(data, home)) if part]
    tail = [part for part in (session_part(data),) if part]
    usage = limit_parts(data)

    place_context(data.get("context_window") or {}, identity, tail, usage, width)
    identity += tail

    return rail([sep().join(row) for row in (identity, usage) if row])


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except Exception:
        # Unparseable input is not worth a traceback in the status bar.
        return 0
    if not isinstance(data, dict):
        return 0
    try:
        for row in build(data, os.path.expanduser("~"), terminal_width()):
            print(row)
    except Exception:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
