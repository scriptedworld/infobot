#!/usr/bin/env python3
"""statusline: what Claude Code shows at the bottom of the screen.

Reads the session JSON on stdin and prints two rows. Claude Code runs this on
every event, so it stays cheap: no network, one subprocess for the pane width,
and one file tailed for the session's token usage. Nothing here waits on
anything, and nothing here raises.

WHAT THE INPUT CAN AND CANNOT ANSWER, from the documented schema:

  context_window   gives token counts AND a percentage, so used/total is real.
                   used_percentage counts INPUT ONLY: input + cache_creation +
                   cache_read, never output, so the counts shown here use the
                   same formula. Mixing them would print a percentage that does
                   not match its own numerator.

  rate_limits      gives used_percentage and resets_at and NOTHING ELSE. There
                   are no token counts for the 5-hour or 7-day windows, so those
                   segments show a percentage and a countdown. Asking for
                   used/total there is not a missing feature, it is absent data.

EVERY FIELD IS OPTIONAL. rate_limits appears only for Claude.ai subscribers and
only after the first API response; either window can be absent on its own;
used_percentage can be null early in a session. A status line that raises shows
nothing at all, so every read is guarded and a missing segment is dropped rather
than printed as zero. Zero is a claim, absence is not.
"""
from __future__ import annotations

import json
import os
import re
import sys
import time
import unicodedata

from infobot import context, pricing, usage

BRAIN, HOURGLASS, CALENDAR = "🧠", "⏳", "📅"
MONEY, BULLSEYE = "💵", "🎯"
# Powerline thin separator from Iosevka Nerd Font, which kitty is configured
# with (font_family "Iosevka Nerd Font", style Light). A plain │ is the fallback
# anywhere the glyph is missing, one character either way, so nothing shifts.
SEP_GLYPH = "\ue0b1"
SEP_COLOUR = "\033[38;2;70;80;85m"   # dim, so it divides without competing
DIM = "\033[38;2;120;130;135m"       # connective words: present, not read

# The left rail, out of oh-my-zsh's multiline prompt: a rounded corner opening
# the block, a rounded one closing it, and a tee for any row between. It says
# the two rows are one block rather than two neighbours, which matters when the
# row above is full width and the row below is half of it.
#
# A lone row gets the stub instead. \u256d with no \u2570 under it reads as a block that
# failed to finish.
RAIL_TOP, RAIL_MID, RAIL_END, RAIL_ONE = "\u256d\u2500 ", "\u251c\u2500 ", "\u2570\u2500 ", "\u2576\u2500 "

# The glyphs Claude Code draws in its own compaction meter, so the two read as
# one instrument. They carry no sub-cell steps the way the eighth-blocks did, so
# every bit of resolution comes from the cell count, which is why the bar takes
# whatever columns row one has left. At 40 cells that is 2.5% each.
FILLED, EMPTY = "▰", "▱"

# Unknown width means tmux could not be asked, so there is nothing to subtract
# from and 50 is a chosen number rather than a fit.
#
# BAR_MIN is the width below which the bar is DROPPED rather than clamped to.
# Clamping overflows: in a 120-column pane a long path leaves six columns, so an
# eight-cell floor wraps the row, costing the whole line to save a bar that at
# 12% a cell was not saying much. The counts and the percentage stay either way.
#
# MARGIN is what Claude Code keeps for itself, so the pane width is NOT the
# budget. A row measured at 222 columns, which tmux agrees is 222 and which does
# not wrap in a 223-column pane, still came back truncated:
#
#   ...107k/1.0M (11% consumed) ▏ ⟨a5e58a…
#
# The renderer indents two columns and then keeps 220, putting its ellipsis in
# the 220th, so the reserve is three. Anything longer is cut rather than
# wrapped, and the session id goes first because it is last.
BAR_FALLBACK, BAR_MIN, MARGIN = 50, 8, 3

# The rate limit windows get a fixed small gauge rather than a share of the
# slack. They are checked occasionally, not watched, so 10% a cell is enough
# resolution, and a fixed width keeps the row from moving under them as the
# context bar above grows. Under a narrow pane the gauge gives way to the
# percentage it was drawing, which costs six columns less.
WINDOW_CELLS = 10

# PACE is where a window LANDS: spend divided by how far through the window you
# are, projecting the percentage it arrives at when it resets. It colours the
# gauge rather than adding a glyph, so the bar carries two readings without
# costing a column, its LENGTH the spend and its COLOUR the verdict. The scale
# diverges with green in the middle, and the reasoning is in
# docs/DECISIONS/a-rate-limit-gauge-is-coloured-by-pace.md.
#
# The window lengths come from the payload's own field names.
FIVE_HOUR, SEVEN_DAY = 5 * 3600, 7 * 86400

# How much of a window has to elapse before its verdict is shown in full. A
# percent spent two minutes into five hours divides by almost nothing and
# projects a catastrophe, so the judgement fades in against green rather than
# switching on. A fraction rather than a duration, so the seven day window
# matures at the same point in its own life instead of after an afternoon.
PACE_CONFIDENT = 0.6

# An arithmetic floor only, to divide by. The fade above stops a wild early
# projection from being believed; this stops it being infinite.
PACE_MIN_ELAPSED = 0.01

RESET = "\033[0m"

# TRUECOLOR. COLORTERM=truecolor and CLAUDE_CODE_TMUX_TRUECOLOR=1 are set in
# settings.json, which is what defeats Claude Code's habit of capping colour at
# the 256 palette when it sees $TMUX. A 24-bit escape reaches the terminal
# intact, so the ramp can be continuous instead of stepped.
#
# Two straight lines rather than one. Green to yellow across the long stretch
# where nothing is happening, then yellow to red compressed into 75-90, so the
# colour moves fastest exactly where a glance needs to tell 80 from 88.
GREEN = (60, 200, 90)
YELLOW = (235, 220, 40)
RED = (225, 45, 45)
PIVOT = 75.0
ALARM_AT = 90.0
ALARM_TOP = 100.0            # where the alarm has arrived in full

# The alarm FADES IN across 90 to 100 rather than switching on at 90.
#
# Its foreground starts at RED, which is exactly where the ramp below it
# arrives, so nothing jumps at the boundary: the last yellow-to-red cell and the
# first alarm cell are the same colour. It then runs to a pale yellow.
#
# Its background starts at BLACK and fills to a deep red. Against a dark
# terminal an all-black background reads as no background at all, so the
# inversion arrives gradually instead of slamming on, and by 100 it is the full
# yellow-on-red that cannot be mistaken for part of the ramp.
#
# Bold is the one part that cannot fade, so it is on across the whole band. It
# is also the least of the three: the background is what carries the message.
BLACK = (0, 0, 0)
ALARM_FG = (250, 240, 120)   # pale yellow, the far end of the foreground fade
ALARM_BG = (180, 25, 25)     # deep red, the far end of the background fade

# The ENCOM teal, the same value the i3 bar and claws use ($encom_teal,
# #00a595), so the status line reads as part of the desktop instead of beside it.
PATH_COLOUR = "\033[38;2;0;165;149m"

# The unused cells are an outline glyph and NO background. ▱ carries its own
# shape, and a background behind it would fill the gaps between the
# parallelograms and turn the tail of the bar into a solid slab.
#
# The value is $encom_dimcyan from the i3 config, one step up from the deepcyan
# the i3 bar uses for `inactive_workspace`.
EMPTY_COLOUR = "\033[38;2;0;95;95m"  # $encom_dimcyan #005f5f

# The pace stops, here because they are built from the palette above.
PACE_STOPS = (
    (0.0, (70, 140, 235)),      # blue: the window is barely being touched
    (70.0, (220, 225, 230)),    # white: under-spending it
    (100.0, GREEN),             # lands exactly full as it resets
    (125.0, YELLOW),            # empties a fifth of the way early
    (150.0, RED),               # empties a third of the way early
)


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
        return alarm(pct)
    if pct <= PIVOT:
        r, g, b = _mix(GREEN, YELLOW, pct / PIVOT)
    else:
        r, g, b = _mix(YELLOW, RED, (pct - PIVOT) / (ALARM_AT - PIVOT))
    return f"\033[38;2;{r};{g};{b}m"


def alarm(pct: float) -> str:
    """The alarm style at `pct`, faded in from the top of the ramp.

    At ALARM_AT it is RED on black, which is the colour the ramp beneath it
    arrives at and a background a dark terminal does not show, so the boundary
    has nothing to see. At ALARM_TOP it is pale yellow on deep red.

    Both ends interpolate together, so the background filling in and the
    foreground brightening are one movement rather than two.
    """
    into = min(1.0, max(0.0, (pct - ALARM_AT) / (ALARM_TOP - ALARM_AT)))
    r, g, b = _mix(RED, ALARM_FG, into)
    back = _mix(BLACK, ALARM_BG, into)
    return f"\033[1;38;2;{r};{g};{b};48;2;{back[0]};{back[1]};{back[2]}m"


def colour(pct: float, text: str) -> str:
    """Wrap text in a colour interpolated from the percentage consumed."""
    if not text or plain():
        return text
    if pct >= ALARM_AT:
        # Inverted rather than merely red: past this point the message is not
        # "high" but "about to matter", and a hue change alone stops being seen
        # after the twentieth time. Faded in across the band rather than
        # switched on, so the counts and the bar's last cells move together.
        return f"{alarm(pct)}{text}{RESET}"
    if pct <= PIVOT:
        r, g, b = _mix(GREEN, YELLOW, pct / PIVOT)
    else:
        r, g, b = _mix(YELLOW, RED, (pct - PIVOT) / (ALARM_AT - PIVOT))
    return f"\033[38;2;{r};{g};{b}m{text}{RESET}"


def terminal_width() -> int | None:
    """Columns, or None when it genuinely cannot be known.

    Every ordinary route fails here. Claude Code captures stdout, so fds 0, 1
    and 2 all raise OSError; COLUMNS is unset; /dev/tty is "No such device or
    address"; and `shutil.get_terminal_size()` therefore returns its fabricated
    80x24 fallback.

    THAT FALLBACK IS THE TRAP. Believing it would truncate a 223-column pane to
    80, worse than not adapting at all. So the shutil default is never used:
    whatever owns the pane is asked directly, and a host that cannot be asked
    returns None, meaning "render the full form and let the caller fit it".

    The hosts are tried INNERMOST FIRST. tmux running inside a herdr pane draws
    this line in the tmux pane, which is the narrower of the two, so tmux
    answers whenever it is there and herdr answers when it is not. Each route
    costs one subprocess of a few milliseconds and only the winning one runs.
    """
    return tmux_width() or herdr_width()


def ask(argv: list[str]) -> str:
    """Put a question to a host and hand back its stdout, stripped.

    It RAISES on a missing binary or a timeout, and each caller catches, because
    what a failure means is the caller's to say: for a width route it means the
    width is unknown, which is a rendering decision rather than an error.

    subprocess is imported here rather than at the top so a render with no host
    to ask does not pay for it, which is every render outside a multiplexer.
    """
    import subprocess

    out = subprocess.run(argv, capture_output=True, text=True, timeout=2)
    return out.stdout.strip()


def tmux_width() -> int | None:
    """Pane columns from tmux, measured at 3.2ms and affordable once per render."""
    if not os.environ.get("TMUX"):
        return None
    try:
        return int(ask(["tmux", "display-message", "-p", "#{pane_width}"])) or None
    except Exception:
        return None


def herdr_width() -> int | None:
    """Pane columns from herdr, measured at 2-4ms, the same order as tmux.

    `pane layout` is the only command carrying a rectangle: `pane current` and
    `pane get` describe the pane in full and never say how wide it is. It
    answers for the CALLING PANE'S WHOLE TAB, so the pane has to be picked out
    of the list it returns, and HERDR_PANE_ID is what names it.

    A tab holding exactly one pane answers whatever id that pane carries. It is
    the one case where not matching the id costs nothing, because there is only
    one rectangle it could be, and it covers an id in the environment that no
    longer names the pane the process now sits in. Every other mismatch is
    reported unknown rather than guessed at, which is FR-3.3 again.

    A ZOOMED PANE'S RECTANGLE IS THE UNZOOMED ONE. `zoomed` goes true and every
    rect in the reply stays exactly where it was, so a pane zoomed out of a
    two-way split reports half the columns it is drawn in and the row comes out
    half the width it could be. The tab's `area` is the width to use, and the
    zoomed pane is the focused one: zooming the neighbour moved `focused_pane_id`
    to it and left this pane hidden, which is the case where the unzoomed
    rectangle is the right answer because it is what unzooming restores.

    HERDR_BIN_PATH is preferred over the name because it pins the version that
    owns this pane, and because PATH in a status line subprocess is whatever
    Claude Code inherited rather than whatever a shell would have built.
    """
    pane = os.environ.get("HERDR_PANE_ID")
    if not pane:
        return None
    try:
        binary = os.environ.get("HERDR_BIN_PATH") or "herdr"
        layout = json.loads(ask([binary, "pane", "layout", "--current"]))["result"]["layout"]
        panes = layout["panes"]
        mine = [p for p in panes if p.get("pane_id") == pane]
        if not mine and len(panes) == 1:
            mine = panes
        if layout.get("zoomed") and mine[0]["pane_id"] == layout.get("focused_pane_id"):
            return int(layout["area"]["width"]) or None
        return int(mine[0]["rect"]["width"]) or None
    except Exception:
        return None


ANSI = re.compile(r"\033\[[0-9;]*m")


def visible_width(text: str) -> int:
    """Columns a rendered string occupies, escapes and all.

    Sizing the bar by subtraction only works if the subtrahend is what the
    terminal will actually draw. `len()` is not that: it counts every byte of
    an escape sequence as a column and the emoji as one column when they take
    two. Both errors run toward a bar too wide for the line.

    Ambiguous-width characters, ▰ and ▱ among them, are counted as one, which is
    what kitty draws them as. A terminal configured to treat ambiguous as wide
    would need this changed, and would double the bar.
    """
    return sum(
        0 if unicodedata.combining(ch)
        else 2 if unicodedata.east_asian_width(ch) in "WF"
        else 1
        for ch in ANSI.sub("", text)
    )


def bar(pct: float, cells: int = BAR_FALLBACK, tint: str = "") -> str:
    """A proportional bar whose fill fades along the ramp, cell by cell.

    Each cell is coloured for the percentage IT stands for, not for the bar's
    total: the first is green because it means 2%, and the last filled one
    matches the colour of the number beside it because they mean the same
    thing. So the fade is a fixed property of the bar and only its length moves.

    The bar is never cut into. The percentage is printed with the counts, where
    it can be read as a number rather than found among the parallelograms.

    A tint overrides the fade and paints every filled cell one colour. The rate
    limit gauges use it to mean something the fade cannot: not where each cell
    sits, but whether the whole reading is a problem.
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
        style = (tint or ramp((i + 1) / cells * 100)) if i < full else EMPTY_COLOUR
        if style != held:
            # RESET first, always. The alarm style carries bold and a
            # background as well as a colour, so a bare colour change after it
            # leaves both of them switched on for the rest of the line. It also
            # changes on EVERY cell through the fade, so this runs per cell up
            # there rather than per run.
            out.append(RESET + style)
            held = style
        out.append(glyph)
    out.append(RESET)
    return "".join(out)


def plain() -> bool:
    """NO_COLOR is honoured for EVERY escape, not just the obvious ones.

    A hardcoded separator or bracket survives `NO_COLOR=1` stripping the
    segments around it, giving output that is neither coloured nor clean.
    """
    return bool(os.environ.get("NO_COLOR"))


def dim(text: str) -> str:
    # An empty span would still emit an open and a reset with nothing between,
    # which happens at both ends of the bar where the fill or the track is
    # empty.
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


def countdown(resets_at: float | None, now=time.time) -> str:
    """Time until reset, as the largest two units that are non-zero.

    Returns "" when the timestamp is missing or already past. A countdown
    reading "0m" suggests a reset is imminent when it has already happened and
    the number is simply stale.

    `now` is the clock, taken as an argument so a test can fix the instant and
    assert a string rather than a shape. FR-4.3. Nothing calls it with anything
    but the default outside a test.
    """
    if not resets_at:
        return ""
    remaining = int(float(resets_at) - now())
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

    The WHOLE span is coloured here, while the rate-limit segments colour only
    their number. That asymmetry is deliberate: the context window is watched
    constantly while working and the other two are infrequent details, so a
    wider block of colour makes the loud one loud.
    """
    # One formula, shared with the file `context.write` leaves behind, so a
    # bar reading 56% cannot sit beside a file saying something else. What it
    # does with an absent count or an absent percentage is described there.
    measured = context.figures(cw)
    if not measured:
        return ""
    used, size, pct = measured
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


def limit_segment(label: str, window: dict | None, span: int,
                  compact: bool = False, now=time.time) -> str:
    """A gauge and a countdown. No token counts exist for these windows.

    `label` carries the emoji and the name together because they are one fixed
    string at both call sites, and because the clock has to be a parameter
    (FR-4.3) and the complexity gate allows five of them.

    The percentage is drawn rather than printed. It is the same bar as the
    context window's, on the same ramp, so 80% is the same shade wherever it
    appears and one glance reads all three meters. At WINDOW_CELLS it resolves
    to 10% a cell, which is what a window checked occasionally wants.

    The countdown stays as a number because a bar cannot carry it, and because
    it is what makes the gauge actionable: half spent with four hours to go
    reads very differently from half spent with ten minutes to go.

    It is deliberately UNCOLOURED. It wants its own scale and probably an
    inverted one, running TOWARD green as it nears zero, since a reset getting
    closer is good news. Undecided, so left plain rather than guessed at.
    """
    if not window:
        return ""
    pct = window.get("used_percentage")
    if pct is None:
        return ""
    pct = float(pct)
    resets_at = window.get("resets_at")
    # Concern where it can be worked out, raw spend where it cannot: with no
    # reset time there is no window position, so the gauge falls back to
    # meaning what the context meter's colour means.
    elapsed = elapsed_fraction(resets_at, span, now)
    tint = ramp(pct) if elapsed is None else pace_tint(pct, elapsed)
    gauge = (tinted(f"{pct:.0f}%", tint) if compact
             else bar(pct, WINDOW_CELLS, tint=tint))
    parts = [label, gauge, countdown(resets_at, now)]
    return " ".join(part for part in parts if part)


def elapsed_fraction(resets_at: float | None, span: int, now=time.time) -> float | None:
    """How far through the window we are, or None when that is unknowable."""
    if not resets_at or not span:
        return None
    remaining = float(resets_at) - now()
    if remaining <= 0:
        return None
    return max(0.0, min(1.0, 1 - remaining / span))


def pace_tint(pct: float, elapsed: float) -> str:
    """The window's verdict, faded toward green by how much it can say yet.

    Two independent readings in one colour. Where the window is projected to
    land decides the hue; how far through the window we are decides how much of
    that hue is shown, against green for the rest.

    Green rather than grey or nothing, because green is this scale's "no
    comment" as well as its "on rate", and both mean there is nothing to act on.
    """
    projected = pct / max(elapsed, PACE_MIN_ELAPSED)
    # Squared, so the verdict stays quiet through the middle of the window and
    # arrives late. Running hot with most of the window still ahead is not
    # something to act on, and a linear fade is already half shouting at the
    # halfway mark.
    trust = min(1.0, elapsed / PACE_CONFIDENT) ** 2
    r, g, b = _mix(GREEN, pace_rgb(projected), trust)
    return f"\033[38;2;{r};{g};{b}m"


def pace_rgb(projected: float) -> tuple:
    """Walk the diverging stops and interpolate between the two that bracket it.

    Outside the ends it clamps, so a window projected to land at 400% is the
    same red as one landing at 150: once it will not last, by how much it will
    not last stops changing what to do about it.
    """
    lo_at, lo = PACE_STOPS[0]
    for hi_at, hi in PACE_STOPS[1:]:
        if projected <= hi_at:
            return _mix(lo, hi, (projected - lo_at) / (hi_at - lo_at))
        lo_at, lo = hi_at, hi
    return PACE_STOPS[-1][1]


def tinted(text: str, escape: str) -> str:
    """One colour over a whole span, closed with a reset."""
    return text if plain() else f"{escape}{text}{RESET}"


# Columns of clear space kept between the meter row and the cost pushed to its
# right, so the two read as separate things rather than one long line.
COST_GUTTER = 3


def cost_forms(data: dict, root=None) -> tuple[str, str]:
    """The cost segment at full length and shortened, from one read.

    Both forms come from the same totals because working them out means
    tailing a file, and doing that twice to decide which of two strings fits
    would double the only expensive thing on the row.

    `root` reaches usage.totals() so a test can price a fixture tree. FR-4.4.
    """
    figures = pricing.priced(usage.totals(data.get("session_id") or "", root))
    if not figures:
        return "", ""
    spent, saved, complete = figures
    # A trailing plus says the session ran a model the table has no rate for, so
    # the figure is a floor rather than a total.
    total = f"{MONEY} {pricing.money(spent)}" + ("" if complete else "+")
    if saved <= 0:
        return total, total
    return f"{total}{sep()}{BULLSEYE} {dim('saved')} {pricing.money(saved)}", total


def align_cost(lines: list[str], data: dict, width: int | None) -> list[str]:
    """Push the cost to the right of the meter row, shortening it or dropping it.

    It goes on the meter row rather than the identity row because the identity
    row has already given its slack to the context bar. With no meter row there
    is nowhere for it that is not somewhere else's space, so it is dropped.
    """
    if len(lines) < 2 or not width:
        return lines
    room = width - MARGIN - visible_width(lines[-1])
    for segment in cost_forms(data):
        gap = room - visible_width(segment)
        if segment and gap >= COST_GUTTER:
            lines[-1] += " " * gap + segment
            break
    return lines


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


def limit_parts(data: dict, compact: bool = False, now=time.time) -> list[str]:
    """The two rate limit windows, each dropped when its data is absent."""
    limits = data.get("rate_limits") or {}
    return [
        seg
        for seg in (
            limit_segment(f"{HOURGLASS} 5hr", limits.get("five_hour"), FIVE_HOUR, compact, now),
            limit_segment(f"{CALENDAR} 7d", limits.get("seven_day"), SEVEN_DAY, compact, now),
        )
        if seg
    ]


def place_context(cw: dict, identity: list[str], tail: list[str], usage: list[str],
                  width: int | None) -> None:
    """Put the context segment on whichever row can hold it, bar sized to fit.

    Row one is where it belongs, but only if row one can hold it with no bar at
    all. A narrow split or a long path puts it over the budget before a single
    cell of bar is added, and then it goes to the meter row rather than being
    truncated: what truncation eats is the end of the row, the session id.

    Both lists are appended to in place.
    """
    if not context_segment(cw, 0):
        return
    if not width or row_width(identity + [context_segment(cw, 0), *tail]) <= width - MARGIN:
        identity.append(fitted(cw, identity + tail, len(identity), width))
    else:
        usage.insert(0, fitted(cw, usage, 0, width))


def build(data: dict, home: str, width: int | None = None, now=time.time) -> list[str]:
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
    row looks like a fault. Each part above returns "" or an empty list when its
    data is missing, so one filter here handles absence for every segment.
    """
    rows = compose(data, home, width, False, now)
    # The gauges give way to the percentages they draw rather than letting the
    # row run past the budget, which is the rule the context bar follows too.
    # Measured after composing rather than before, because the context segment
    # relocates onto this row when row one cannot hold it, and that is exactly
    # the case where the row is too long.
    if width and rows[1] and row_width(rows[1]) > width - MARGIN:
        rows = compose(data, home, width, True, now)

    lines = rail([sep().join(row) for row in rows if row])
    return align_cost(lines, data, width)


def compose(data: dict, home: str, width: int | None, compact: bool,
            now=time.time) -> list[list[str]]:
    """The two rows as lists of parts, before they are joined."""
    identity = [part for part in (model_part(data), *path_parts(data, home)) if part]
    tail = [part for part in (session_part(data),) if part]
    usage = limit_parts(data, compact, now)

    place_context(data.get("context_window") or {}, identity, tail, usage, width)
    identity += tail
    return [identity, usage]


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except Exception:
        # Unparseable input is not worth a traceback in the status bar.
        return 0
    if not isinstance(data, dict):
        return 0

    # Guarded inside `write`, and called before the render so a payload the bar
    # cannot draw still leaves its numbers on disk.
    context.write(data)

    try:
        for row in build(data, os.path.expanduser("~"), terminal_width()):
            print(row)
    except Exception:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
