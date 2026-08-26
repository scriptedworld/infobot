"""What the session's context window is doing, for the bar and for a file.

The bar shows it to a person and the file leaves it where a program can read
it. Both come through `figures` so the two cannot disagree: a bar saying 56%
beside a file saying something else is worse than either alone.

WHY THE FILE EXISTS. An agent has no way to measure its own context. The
payload Claude Code hands the status line carries the numbers, and nothing else
in the session sees them. Reconstructing them means tailing the transcript and
summing usage records by hand, which is what the absence of this file cost once
already.

It sits beside `usage.py`'s offsets, in `state_dir()`: generated state, keyed by
session, outside every repository. One predictable path, so a reader that knows
its own session id knows where to look and needs to know nothing else.

Written in canonical YAML, hand-emitted: block style, one key to a line, keys
sorted, a string quoted and a number bare. Every structured file in the
ecosystem is YAML, and a status line that must render or show nothing does not
take a third-party import to satisfy that. A flat mapping of scalars needs no
library to write correctly.
"""

from __future__ import annotations

import os
import tempfile
from datetime import datetime, timezone
from pathlib import Path

from infobot.usage import state_dir

# The keys used_percentage is computed from: the input side only, never output.
INPUT_KEYS = (
    "input_tokens",
    "cache_creation_input_tokens",
    "cache_read_input_tokens",
)


def figures(cw: dict) -> tuple[float, float, float] | None:
    """The context window as (used, size, percent), or None when it cannot be
    said at all.

    `current_usage` is the authority when present and `total_input_tokens` is
    the fallback, matching `used_percentage`'s own formula. Where the counts are
    absent and a percentage is present, the count is derived from it, so the two
    halves agree rather than reporting a literal zero that reads as a bug.
    """
    size = cw.get("context_window_size")
    if not size:
        return None

    usage = cw.get("current_usage") or {}
    if isinstance(usage, dict) and usage:
        used = sum(float(usage.get(key) or 0) for key in INPUT_KEYS)
    else:
        used = float(cw.get("total_input_tokens") or 0)

    pct = cw.get("used_percentage")
    if pct is None:
        pct = (used / float(size)) * 100
    elif not used:
        used = float(size) * float(pct) / 100

    return used, float(size), float(pct)


def path(session_id: str) -> Path:
    """Where this session's state is left. One path, named by session id."""
    return state_dir() / f"{session_id}.status.yaml"


def write(data: dict) -> None:
    """Leave the last check on disk.

    Best effort in the same sense as `usage._save`: a state file that cannot be
    written costs a reader a measurement it can take another way, and the status
    line's one hard guarantee is that it renders or shows nothing.
    """
    try:
        _write(data)
    except Exception:
        pass


def _write(data: dict) -> None:
    session = data.get("session_id")
    if not session:
        return

    state = _state(data, session)
    directory = state_dir()
    directory.mkdir(parents=True, exist_ok=True)

    # Written whole or not at all. A reader stat-ing this file between a
    # truncate and a write would otherwise see an empty one and conclude the
    # session had no context, which is worse than seeing the previous check.
    target = path(session)
    handle, temporary = tempfile.mkstemp(dir=directory, prefix=f".{session}.", suffix=".yaml")
    try:
        with os.fdopen(handle, "w") as out:
            out.write(_canonical(state))
        os.chmod(temporary, 0o644)
        os.replace(temporary, target)
    except Exception:
        os.unlink(temporary)
        raise


def _state(data: dict, session: str) -> dict:
    workspace = data.get("workspace") or {}
    model = data.get("model") or {}
    effort = data.get("effort") or {}

    state: dict = {
        "session": session,
        "written": datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds"),
        "cwd": workspace.get("current_dir") or "",
        "model": model.get("display_name") or model.get("id") or "",
        "effort": effort.get("level") or "",
    }

    measured = figures(data.get("context_window") or {})
    if measured:
        used, size, pct = measured
        state["context_used"] = int(used)
        state["context_size"] = int(size)
        state["context_remaining"] = max(int(size) - int(used), 0)
        state["context_percent"] = round(pct, 1)

    return {key: value for key, value in state.items() if value != ""}


def _canonical(state: dict) -> str:
    return "".join(f'"{key}": {_scalar(state[key])}\n' for key in sorted(state))


def _scalar(value: object) -> str:
    """A string is quoted and a number is bare, so its type is never in
    question on the way back in."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        text = repr(value)
        return text if "." in text or "e" in text else text + ".0"
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"') + '"'


def forget(session_id: str) -> None:
    """Drop this session's state file.

    Called when the session ends. Nothing will read it again: it reports a
    context window that no longer exists, and a reader finding it later would
    have no way to tell a live session from a finished one.
    """
    try:
        path(session_id).unlink(missing_ok=True)
    except OSError:
        pass
