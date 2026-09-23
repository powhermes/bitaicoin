#!/usr/bin/env python3
"""
ASERT half-life selection study for BitAIcoin.

Deterministic (expected-value) simulation of the aserti3-2d relation:

    relative_target(h) = 2 ** ((time_diff - ibt*(height_diff+1)) / halflife)

where time_diff/height_diff are measured from a fixed anchor block, ibt is the
target block interval (600s, matching BitAIcoin's existing nPowTargetSpacing).

This is NOT the bit-exact fixed-point consensus algorithm (that needs its own
integer implementation validated against BCHN's published test vectors before
it's committed as code). This is a floating-point behavioral study, used only
to compare candidate half-life values against the shock scenarios requested,
using expected block-arrival time rather than stochastic Poisson arrivals --
a stated limitation, not a hidden one: real variance would make settling noisier,
not systematically faster or slower, so relative comparisons between half-life
candidates still hold.
"""

IBT = 600.0
POW_LIMIT_CEILING = 1.0  # anchor is already at consensus.powLimit; see comment above  # target block interval, seconds (== consensus.nPowTargetSpacing)

def simulate(halflife, k, tolerance=0.10, max_blocks=500000):
    """
    k = hashrate multiplier relative to anchor, applied from height 1 onward,
    permanently, starting from an already-settled chain (relative_target=1 at h=0).
    Returns dict with blocks/time to settle and the worst transient rate multiple.
    """
    t = 0.0  # elapsed wall-clock time since anchor
    h = 0
    worst_ratio = 1.0
    worst_h = 0
    settle_h = None
    settle_t = None
    consecutive_ok = 0
    NEEDED_CONSECUTIVE = 20  # must actually stay settled, not just touch the band once

    while h < max_blocks:
        h += 1
        exponent = (t - IBT * h) / halflife
        try:
            rel_target = 2.0 ** exponent
        except OverflowError:
            rel_target = float('inf')
        # A real integer implementation must clamp the target to >= 1 (never zero) and
        # to <= powLimit -- this floor exists precisely because an extreme, fast-reacting
        # half-life can otherwise drive the computed target to literal zero, which is the
        # kind of edge case this simulation is meant to surface.
        rel_target = max(rel_target, 1e-300)  # avoid literal float underflow only; not a consensus clamp
        # REAL consensus ceiling: target must never exceed consensus.powLimit. Verified from the
        # live node that the anchor-adjacent chain is ALREADY running exactly at powLimit (bits
        # 0x1d0fffff, unchanged from block 225430 through the current tip, across one already-
        # occurred retarget) -- so relative to THIS anchor, the ceiling is 1.0, not a distant limit.
        rel_target = min(rel_target, POW_LIMIT_CEILING)
        # expected block time is inversely proportional to (relative_target * hashrate_multiplier):
        # bigger target = easier = each hash attempt more likely to succeed = shorter expected time;
        # more hashrate = more attempts/sec = shorter expected time.
        expected_block_time = IBT / (rel_target * k)
        t += expected_block_time

        actual_rate_ratio = IBT / expected_block_time  # >1 means blocks coming faster than target
        deviation = abs(actual_rate_ratio - 1.0)
        instant_multiple = actual_rate_ratio if actual_rate_ratio >= 1 else 1.0 / actual_rate_ratio
        if instant_multiple > worst_ratio:
            worst_ratio = instant_multiple
            worst_h = h

        if deviation <= tolerance:
            consecutive_ok += 1
            if consecutive_ok == NEEDED_CONSECUTIVE and settle_h is None:
                settle_h = h - NEEDED_CONSECUTIVE + 1
                settle_t = t  # approx; good enough for a wall-clock comparison
        else:
            consecutive_ok = 0
            settle_h = None
            settle_t = None

        if settle_h is not None and h > settle_h + NEEDED_CONSECUTIVE + 5:
            break

    return {
        "settle_blocks": settle_h,
        "settle_wall_clock_h": (settle_t / 3600.0) if settle_t is not None else None,
        "worst_instant_multiple": worst_ratio,
        "worst_at_block": worst_h,
    }


half_lives = {
    "1 hour": 3600,
    "2 hours": 7200,
    "6 hours": 21600,
    "1 day": 86400,
    "2 days (BCH mainnet)": 172800,
    "4 days": 345600,
}
scenarios = [
    ("arrival 10x", 10.0),
    ("arrival 100x", 100.0),
    ("arrival 1000x", 1000.0),
    ("withdrawal 10x", 0.1),
    ("withdrawal 100x", 0.01),
    ("withdrawal 1000x", 0.001),
]

header = f"{'half-life':<22}{'scenario':<18}{'blocks to settle':>18}{'wall-clock (h)':>16}{'worst instant mult.':>22}"
print(header)
print("-" * len(header))
for hl_name, hl in half_lives.items():
    for sc_name, k in scenarios:
        r = simulate(hl, k)
        sb = str(r["settle_blocks"]) if r["settle_blocks"] is not None else "n/a"
        wc = f"{r['settle_wall_clock_h']:.2f}" if r["settle_wall_clock_h"] is not None else "n/a"
        print(f"{hl_name:<22}{sc_name:<18}{sb:>18}{wc:>16}{r['worst_instant_multiple']:>21.1f}x")
    print()
