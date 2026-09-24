#!/usr/bin/env python3
"""
BitAIcoin ASERT activation-boundary "schedule reset" comparison
(docs/AUXPOW_MILESTONE.md sec.9, item 3).

227808 is BOTH the first ASERT block AND what would otherwise have been a
legacy 2016-block retarget boundary (227808 / 2016 = 113 exactly). Because
the ASERT branch in GetNextWorkRequired() runs first, the legacy retarget
that would normally compute block 227808's target from the FULL preceding
2016-block window (225792..227807) is intentionally never performed. ASERT
instead computes block 227808's target from ONLY block 227807's own nBits
and the single 227806->227807 solvetime (heightDiff=0 for the very first
ASERT block) -- a much more LOCAL view than the legacy algorithm's
2016-block average.

This script computes, for a range of realistic and shock scenarios, BOTH:
  A. what the IMPLEMENTED code (ASERT-at-227808) actually produces, and
  B. what the LEGACY 2016-block retarget would have produced for the same
     hypothetical chain, had it still been in effect at that boundary.

This does NOT change anything. It is read-only analysis to inform an
explicit, documented decision about whether skipping the legacy retarget at
227808 is acceptable given BitAIcoin's real powLimit and expected
merge-mining hash shocks -- per explicit instruction, not to auto-select a
different design.

Real starting point (not synthetic): BitAIcoin's actual chain, queried
2026-09-24 --
    height 225792 (the current epoch's start): nBits 0x1d0fffff, time 1789789080
    height 225823 (real tip):                  nBits 0x1d0fffff, time 1789815555
Both legacy epoch bound (225792) and the projected-forward simulation start
(225823) use these real, observed values -- not placeholders.
"""
import sys
sys.path.insert(0, "contrib")
from asert_reference import calculate_asert, compact_to_target, target_to_compact

# --- Real, observed chain state (2026-09-24) ---
REAL_EPOCH_START_HEIGHT = 225792
REAL_EPOCH_START_TIME = 1789789080
REAL_EPOCH_START_NBITS = 0x1d0fffff

REAL_TIP_HEIGHT = 225823
REAL_TIP_TIME = 1789815555

# --- Real BitAIcoin consensus constants ---
POW_LIMIT = int("0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 16)
SPACING = 600
ASERT_ACTIVATION = 227808
ASERT_ANCHOR = 227807  # ASERT_ACTIVATION - 1
LEGACY_TIMESPAN = 1209600  # two weeks, 2016 * 600
LEGACY_INTERVAL_BLOCKS = 2016
ASERT_HALFLIFE = 21600  # frozen, sec.8.1


def legacy_retarget(old_nbits, actual_timespan):
    """Byte-for-byte the same clamp/scale logic as CalculateNextWorkRequired
    (src/pow.cpp), using BitAIcoin's current-era consensus.powLimit (not the
    historical clamp, since this whole epoch is post-activation)."""
    if actual_timespan < LEGACY_TIMESPAN // 4:
        actual_timespan = LEGACY_TIMESPAN // 4
    if actual_timespan > LEGACY_TIMESPAN * 4:
        actual_timespan = LEGACY_TIMESPAN * 4
    old_target, _neg, _over = compact_to_target(old_nbits)
    new_target = (old_target * actual_timespan) // LEGACY_TIMESPAN
    if new_target > POW_LIMIT:
        new_target = POW_LIMIT
    return target_to_compact(new_target)


def simulate_block_times(start_height, start_time, end_height, shock_start_height=None,
                          shock_multiplier=1, shock_ends_height=None):
    """Deterministic (no randomness -- this is about the STRUCTURAL
    schedule-reset property, not stochastic variance, which the half-life
    study already covers separately) block-by-block time projection.
    Returns {height: time}. Baseline spacing is exactly SPACING seconds;
    while `shock_start_height <= height` (and, if given,
    `height < shock_ends_height`), blocks arrive `shock_multiplier` times
    faster (spacing/shock_multiplier seconds apart) -- modeling a real,
    sustained external hashrate change, not per-block noise."""
    times = {start_height: start_time}
    t = start_time
    for h in range(start_height + 1, end_height + 1):
        in_shock = (shock_start_height is not None and h >= shock_start_height and
                    (shock_ends_height is None or h < shock_ends_height))
        # Integer arithmetic throughout -- real block timestamps are
        # integers, and calculate_asert() requires integer inputs.
        spacing = max(1, SPACING // shock_multiplier) if in_shock else SPACING
        t += spacing
        times[h] = t
    return times


def compare_scenario(name, shock_start_height=None, shock_multiplier=1, shock_ends_height=None):
    times = simulate_block_times(REAL_TIP_HEIGHT, REAL_TIP_TIME, ASERT_ANCHOR,
                                  shock_start_height, shock_multiplier, shock_ends_height)
    t_225792 = REAL_EPOCH_START_TIME
    t_227806 = times[ASERT_ANCHOR - 1]
    t_227807 = times[ASERT_ANCHOR]

    # A. Implemented: ASERT-at-227808 (heightDiff=0, refTarget=block 227807's
    # own nBits -- which is REAL_EPOCH_START_NBITS unchanged, since the
    # legacy DAA never retargets mid-epoch).
    anchor_target, _, _ = compact_to_target(REAL_EPOCH_START_NBITS)
    asert_target = calculate_asert(anchor_target, SPACING,
                                    t_227807 - t_227806, 0, POW_LIMIT, ASERT_HALFLIFE)
    asert_nbits = target_to_compact(asert_target)

    # B. What the legacy 2016-block retarget would have produced for block
    # 227808, using the FULL 225792..227807 window.
    legacy_nbits = legacy_retarget(REAL_EPOCH_START_NBITS, t_227807 - t_225792)

    asert_final_target, _, _ = compact_to_target(asert_nbits)
    legacy_final_target, _, _ = compact_to_target(legacy_nbits)
    ratio = asert_final_target / legacy_final_target
    print(f"{name:<58} ASERT=0x{asert_nbits:08x}  legacy-would-be=0x{legacy_nbits:08x}  "
          f"ASERT/legacy target ratio={ratio:.4f}")


print("=" * 130)
print("ACTIVATION-BOUNDARY SCHEDULE-RESET COMPARISON: ASERT-at-227808 (implemented, LOCAL: sees only")
print("block 227806->227807) vs. what the legacy 2016-block retarget (GLOBAL: sees 225792->227807)")
print("would have produced for the same hypothetical chain. Read-only analysis; no design change.")
print("=" * 130)

compare_scenario("Steady 1x throughout 225792->227808")

for k in (10, 100, 1000):
    compare_scenario(f"{k}x arriving 20 blocks before 227808, continuing after",
                      shock_start_height=ASERT_ANCHOR - 20 + 1, shock_multiplier=k)
for k in (10, 100, 1000):
    compare_scenario(f"{k}x arriving 100 blocks before 227808, continuing after",
                      shock_start_height=ASERT_ANCHOR - 100 + 1, shock_multiplier=k)

# Large hash arriving before 227808 and disappearing EXACTLY at activation:
# the shock is already OVER by the time block 227807 (the anchor) is mined --
# ASERT's single-block view sees a normal, un-shocked solvetime and is
# structurally blind to the shock that just happened; the legacy average
# still carries the shock's real (if partial) contribution.
for k in (10, 100, 1000):
    compare_scenario(f"{k}x for 100 blocks, ending exactly at 227807 (gone by the anchor)",
                      shock_start_height=ASERT_ANCHOR - 100, shock_multiplier=k,
                      shock_ends_height=ASERT_ANCHOR)

# Large hash arriving exactly at 227808: no shock has happened yet by the
# time block 227808's OWN target is computed (the shock affects block 227808
# itself and onward, not the decision made for it) -- both approaches see an
# identical, un-shocked 225792..227807 history, so they must agree here;
# included as the "boundary sanity" control case.
compare_scenario("1000x arriving exactly AT 227808 (no effect on 227808's own target)",
                  shock_start_height=ASERT_ACTIVATION, shock_multiplier=1000)

print()
print("No half-life or activation height is being changed by this script. Comparison only,")
print("per explicit instruction -- see docs/AUXPOW_MILESTONE.md sec.9 item 3 for the recorded decision.")
