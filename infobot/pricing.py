"""What a session would have cost through the API, at list rates.

Jeff is on a subscription, so nothing here is a bill. It is the counterfactual:
what the same tokens would have come to had they gone through the API, which is
the only figure that makes the cache worth anything visible.

THE RATES ARE A CACHED COPY AND THEY DRIFT. They are taken from the `claude-api`
skill's own table, itself stamped as cached, because a status line that runs on
every event cannot make a network call to ask. `TAKEN` below is the date they
were read; anything relying on them being current should re-read them rather
than trust this file.

A model absent from the table is priced at nothing and reported as tokens, not
as a figure. A cost computed from a guessed rate is worse than no cost.
"""

from __future__ import annotations

TAKEN = "2026-08-23"

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
    spent = saved = 0.0
    for model, counts in totals.items():
        rates = RATES.get(model)
        if not rates:
            return None
        rate_in, rate_out = rates
        cached = 0
        spent += counts.get("input_tokens", 0) / 1e6 * rate_in
        spent += counts.get("output_tokens", 0) / 1e6 * rate_out
        for field, multiplier in CACHE_WRITE.items():
            tokens = counts.get(field, 0)
            cached += tokens
            spent += tokens / 1e6 * rate_in * multiplier
        reads = counts.get("cache_read_input_tokens", 0)
        cached += reads
        spent += reads / 1e6 * rate_in * CACHE_READ
        saved += cached / 1e6 * rate_in
    return spent, saved - _cache_cost(totals)


def _cache_cost(totals: dict) -> float:
    """What the cached tokens actually cost, reads and writes together."""
    cost = 0.0
    for model, counts in totals.items():
        rate_in = RATES[model][0]
        for field, multiplier in CACHE_WRITE.items():
            cost += counts.get(field, 0) / 1e6 * rate_in * multiplier
        cost += counts.get("cache_read_input_tokens", 0) / 1e6 * rate_in * CACHE_READ
    return cost


def money(dollars: float) -> str:
    """Dollars, at the precision the number deserves rather than always two."""
    if dollars >= 1000:
        return f"${dollars / 1000:.1f}k"
    if dollars >= 100:
        return f"${dollars:.0f}"
    return f"${dollars:.2f}"
