"""Token totals for the running session, read by tailing its transcript.

The status line's payload carries no cumulative usage, but Claude Code writes a
usage record per assistant message into the session transcript, and the payload
carries the session id that names it.

THIS IS THE ONE PLACE INFOBOT READS A FILE IT WAS NOT HANDED. It is deliberate
and it is bounded: a stat on every render, a parse only of what has been
appended since the last one. A full re-sum of a 2.7MB transcript measured 21ms
to 29ms against a render budget of 33ms, and it grows for the life of the
session, so re-summing every time is not affordable and tailing is.

Every read is guarded. A missing transcript, an unreadable state file, a half
written line: each returns no totals rather than raising, because a status line
that raises shows nothing at all.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

PROJECTS = Path.home() / ".claude" / "projects"

# How far into a transcript to look for the session it belongs to. A transcript
# opens with bookkeeping that carries no session at all: measured on one opened
# by a clear, the field first appears on record 18.
CLAIM_LINES = 40
COUNTS = (
    "input_tokens",
    "output_tokens",
    "cache_read_input_tokens",
    "ephemeral_5m_input_tokens",
    "ephemeral_1h_input_tokens",
)


def state_dir() -> Path:
    """Where the offsets live: generated state, so outside the repository."""
    root = os.environ.get("XDG_STATE_HOME") or Path.home() / ".local" / "state"
    return Path(root) / "infobot"


def transcripts(session_id: str) -> list[Path]:
    """Every transcript the session bills for: its own, and its subagents'.

    Found by name rather than by rebuilding a path. The directory is a slug of
    the working directory, but a session may have been started somewhere other
    than where it now is, so the id is globbed for instead of the slug being
    reconstructed from `workspace.current_dir`.

    THE SUBAGENTS ARE NOT OPTIONAL. Each runs in its own transcript under
    `<session>/subagents/`, and on a session that delegates they are not a
    rounding error: measured on one, they were 51% of output tokens and 33% of
    cache reads. Counting only the main transcript halves the bill.

    NEITHER ARE THE TRANSCRIPTS A CLEAR LEFT BEHIND. `/clear` closes one
    transcript and opens another under a NEW `sessionId`, so a name match alone
    would follow only one side of it. Measured on a cleared session: the old
    transcript's last record and the new one's first are two minutes apart, and
    the new one's first user record is the `/clear` command itself.

    Two ids run through a cleared session and they point opposite ways. Every
    record carries `sessionId`, which is the transcript's OWN id and changes at
    the clear. Records after the clear also carry a snake_case `session_id`
    holding the id of the session it came from. Since the status line's payload
    may hand over either one, transcripts are grouped by ROOT: a transcript's
    recorded origin where it has one, its own name where it does not. Every
    transcript sharing a root is one session's spending, whichever id was
    asked about.
    """
    if not session_id:
        return []
    try:
        named = list(PROJECTS.glob(f"*/{session_id}.jsonl"))
        # Scoped to the project the session belongs to. A clear opens its new
        # transcript beside the old one, so the search never leaves that
        # directory, and the cost is proportional to one project's sessions
        # rather than to every session ever recorded on the machine.
        pool = list(named[0].parent.glob("*.jsonl")) if named else list(PROJECTS.glob("*/*.jsonl"))
        root = _root(named[0]) if named else session_id
        found = [p for p in pool if _root(p) == root or p.stem == root]
        return found + [sub for p in found
                        for sub in p.parent.glob(f"{p.stem}/subagents/*.jsonl")]
    except OSError:
        return []


def _root(path: Path) -> str:
    """The session a transcript belongs to: its recorded origin, else its name."""
    try:
        with path.open("rb") as handle:
            for count, raw in enumerate(handle):
                if count >= CLAIM_LINES:
                    break
                try:
                    claimed = (json.loads(raw) or {}).get("session_id")
                except ValueError:
                    continue
                if claimed:
                    return claimed
    except OSError:
        pass
    return path.stem


def totals(session_id: str) -> dict:
    """Token counts for the session, keyed by model. Empty when unknowable.

    State is per file rather than one running sum, because subagent transcripts
    appear part way through a session and a single offset cannot say which of
    them a total already includes.
    """
    state = _load(session_id).get("files", {})
    seen: dict = {}
    changed = False
    for path in transcripts(session_id):
        key = str(path)
        try:
            size = path.stat().st_size
        except OSError:
            continue
        known = state.get(key, {})
        if known.get("size") == size:
            seen[key] = known
            continue
        # A file that shrank was rotated or replaced, so its offset means
        # nothing against the new one and it is read from the start.
        start = known.get("size", 0) if size > known.get("size", 0) else 0
        counted = dict(known.get("totals", {})) if start else {}
        read, consumed = _scan(path, start)
        _merge(counted, read)
        seen[key] = {"size": start + consumed, "totals": counted}
        changed = True

    if changed or len(seen) != len(state):
        _save(session_id, {"files": seen})
    summed: dict = {}
    for entry in seen.values():
        _merge(summed, entry["totals"])
    return summed


def _scan(path: Path, start: int) -> tuple[dict, int]:
    """Usage from `start` onward, and how many bytes of it were whole lines.

    A transcript being appended to by the session that is rendering can end
    mid-line, so the offset advances only over lines that arrived complete.
    """
    found: dict = {}
    consumed = 0
    try:
        with path.open("rb") as handle:
            handle.seek(start)
            for raw in handle:
                if not raw.endswith(b"\n"):
                    break
                consumed += len(raw)
                _take(found, raw)
    except OSError:
        return {}, 0
    return found, consumed


def _take(found: dict, raw: bytes) -> None:
    """Add one transcript line's usage, ignoring anything that is not usage."""
    try:
        message = (json.loads(raw) or {}).get("message") or {}
    except (ValueError, TypeError):
        return
    usage = message.get("usage") or {}
    if not usage:
        return
    counts = found.setdefault(message.get("model") or "?", {})
    creation = usage.get("cache_creation") or {}
    for field in COUNTS:
        value = usage.get(field, creation.get(field, 0))
        if isinstance(value, (int, float)):
            counts[field] = counts.get(field, 0) + value


def _merge(into: dict, more: dict) -> None:
    for model, counts in more.items():
        target = into.setdefault(model, {})
        for field, value in counts.items():
            target[field] = target.get(field, 0) + value


def _load(session_id: str) -> dict:
    try:
        return json.loads((state_dir() / f"{session_id}.json").read_text())
    except (OSError, ValueError):
        return {}


def _save(session_id: str, state: dict) -> None:
    """Best effort. A state file that cannot be written costs a re-sum, and a
    re-sum is slow rather than wrong, so the failure is not worth reporting."""
    try:
        directory = state_dir()
        directory.mkdir(parents=True, exist_ok=True)
        (directory / f"{session_id}.json").write_text(json.dumps(state))
    except OSError:
        pass
