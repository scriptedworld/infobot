"""What a session would have cost through the API, at list rates.

Jeff is on a subscription, so nothing here is a bill. It is the counterfactual:
what the same tokens would have come to had they gone through the API, which is
the only figure that makes the cache worth anything visible.

THE RATES ARE A CACHED COPY AND THEY DRIFT. A status line that runs on every
event cannot make a network call to ask, so they are read from a table on disk
and refreshed by something else.

`~/.config/infobot/pricing.json` is that table, and it wins when it is there.
Config rather than state: the offsets under `~/.local/state/infobot` are
disposable machine bookkeeping, while this is a file worth editing by hand.
Sonnet 5 carrying an introductory rate with an expiry is exactly the case that
wants a person, not a fetch.

The table below is the seed and the fallback, so a fresh clone renders with no
config file and no network. Both carry the date they were taken.

A model absent from the table is priced at nothing and reported as tokens, not
as a figure. A cost computed from a guessed rate is worse than no cost.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

TAKEN = "2026-08-23"
SOURCE = "https://platform.claude.com/docs/en/pricing.md"


# Input and output, dollars per million tokens.
RATES = {
    "claude-fable-5": (10.00, 50.00),
    "claude-mythos-5": (10.00, 50.00),
    "claude-opus-5": (5.00, 25.00),
    "claude-opus-4-8": (5.00, 25.00),
    "claude-opus-4-7": (5.00, 25.00),
    "claude-opus-4-6": (5.00, 25.00),
    "claude-sonnet-5": (3.00, 15.00),
    "claude-sonnet-4-6": (3.00, 15.00),
    "claude-haiku-4-5": (1.00, 5.00),
}

# Multipliers on the input rate. A cache read is CHARGED, at a tenth: it is 90%
# off, not free, and on a long session it is the largest single line. A write
# costs more than a fresh input token, which is why the two are priced apart
# rather than lumped together as "cache".
CACHE_READ = 0.1
CACHE_WRITE = {"ephemeral_5m_input_tokens": 1.25, "ephemeral_1h_input_tokens": 2.0}


def table_path() -> Path:
    root = os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config"
    return Path(root) / "infobot" / "pricing.json"


def table() -> dict:
    """The rates on disk, or the seed below when there are none to be had.

    Anything malformed falls back rather than raising. A status line that
    raises shows nothing at all, and a stale rate is a smaller wrong than a
    blank row.
    """
    try:
        loaded = json.loads(table_path().read_text())
        if loaded.get("rates"):
            return loaded
    except (OSError, ValueError, AttributeError):
        pass
    return {"taken": TAKEN, "source": SOURCE, "rates": RATES,
            "cache_read": CACHE_READ, "cache_write": CACHE_WRITE}


def priced(totals: dict) -> tuple[float, float] | None:
    """Dollars spent, and dollars the cache took off, or None if unpriceable.

    `totals` is keyed by model, each holding the token counts as the transcript
    records them. A model with no rate makes the whole figure a guess, so the
    answer is None rather than a total that quietly omits part of the session.

    The saving is the honest counterfactual: every cached token, read or
    written, charged at the plain input rate instead. That is what the session
    would have cost with no caching at all, less what it did cost.
    """
    if not totals:
        return None
    rate_table = table()
    read_rate = rate_table.get("cache_read", CACHE_READ)
    write_rates = rate_table.get("cache_write", CACHE_WRITE)
    spent = saved = 0.0
    for model, counts in totals.items():
        rates = rate_table["rates"].get(model)
        if not rates:
            return None
        rate_in, rate_out = rates
        cached = 0
        spent += counts.get("input_tokens", 0) / 1e6 * rate_in
        spent += counts.get("output_tokens", 0) / 1e6 * rate_out
        for field, multiplier in write_rates.items():
            tokens = counts.get(field, 0)
            cached += tokens
            spent += tokens / 1e6 * rate_in * multiplier
        reads = counts.get("cache_read_input_tokens", 0)
        cached += reads
        spent += reads / 1e6 * rate_in * read_rate
        saved += cached / 1e6 * rate_in
    return spent, saved - _cache_cost(totals, rate_table)


def _cache_cost(totals: dict, rate_table: dict) -> float:
    """What the cached tokens actually cost, reads and writes together."""
    cost = 0.0
    for model, counts in totals.items():
        rate_in = rate_table["rates"][model][0]
        for field, multiplier in rate_table.get("cache_write", CACHE_WRITE).items():
            cost += counts.get(field, 0) / 1e6 * rate_in * multiplier
        cost += (counts.get("cache_read_input_tokens", 0) / 1e6 * rate_in
                 * rate_table.get("cache_read", CACHE_READ))
    return cost


def money(dollars: float) -> str:
    """Dollars, at the precision the number deserves rather than always two."""
    if dollars >= 1000:
        return f"${dollars / 1000:.1f}k"
    if dollars >= 100:
        return f"${dollars:.0f}"
    return f"${dollars:.2f}"
