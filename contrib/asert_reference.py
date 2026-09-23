#!/usr/bin/env python3
"""
Bit-exact port of BCHN's CalculateASERT (aserti3-2d), transcribed line-for-line
from src/pow.cpp in bitcoin-cash-node/bitcoin-cash-node (GitHub mirror of the
GitLab canonical repo), retrieved 2026-09-23. Source function reproduced below
this docstring for side-by-side comparison with this port.

    arith_uint256 CalculateASERT(const arith_uint256 &refTarget,
                                 const int64_t nPowTargetSpacing,
                                 const int64_t nTimeDiff,
                                 const int64_t nHeightDiff,
                                 const arith_uint256 &powLimit,
                                 const int64_t nHalfLife) noexcept {
      assert(refTarget > 0 && refTarget <= powLimit);
      assert((powLimit >> 224) == 0);
      assert(nHeightDiff >= 0);
      assert(llabs(nTimeDiff - nPowTargetSpacing * nHeightDiff) < (1ll << (63 - 16)));
      const int64_t exponent = ((nTimeDiff - nPowTargetSpacing * (nHeightDiff + 1)) * 65536) / nHalfLife;
      int64_t shifts = exponent >> 16;
      const auto frac = uint16_t(exponent);
      const uint32_t factor = 65536 + ((
          195766423245049ull * frac
        + 971821376ull * frac * frac
        + 5127ull * frac * frac * frac
        + (1ull << 47)
      ) >> 48);
      arith_uint256 nextTarget = refTarget * factor;
      shifts -= 16;
      if (shifts <= 0) {
          nextTarget >>= -shifts;
      } else {
          const auto nextTargetShifted = nextTarget << shifts;
          if ((nextTargetShifted >> shifts) != nextTarget) nextTarget = powLimit;
          else nextTarget = nextTargetShifted;
      }
      if (nextTarget == 0) nextTarget = arith_uint256(1);
      else if (nextTarget > powLimit) nextTarget = powLimit;
      return nextTarget;
    }

CRITICAL correction versus my own first-pass prose description (caught by
reading this source, not by trusting memory of the spec prose): time_delta is
NOT "current block time minus anchor block time." It is the tip block's
(pindexPrev's) time minus the ANCHOR BLOCK'S PARENT's time, and height_delta
is the tip's height minus the anchor height:

    GetNextASERTWorkRequired(pindexPrev, ...):
        nTimeDiff  = pindexPrev->GetBlockTime() - anchorParams.nPrevBlockTime
        nHeightDiff = pindexPrev->nHeight - anchorParams.nHeight
        nextTarget = CalculateASERT(refTarget=anchor.nBits, spacing, nTimeDiff,
                                     nHeightDiff, powLimit, halfLife)

For BitAIcoin with anchor block = 227807 (last block validated under the
legacy retarget, per the milestone doc's existing framing), this means:
  - anchorParams.nHeight       = 227807
  - anchorParams.nBits         = bits of block 227807 (its own, real target)
  - anchorParams.nPrevBlockTime = time of block 227806 (anchor's PARENT), not
    block 227807's own time.
  - When computing the target for block N (N > 227807), use pindexPrev =
    block N-1: nTimeDiff = time(N-1) - time(227806), nHeightDiff =
    height(N-1) - 227807.

This file provides `calculate_asert(...)`, a direct Python port with the same
integer semantics (arbitrary-precision Python ints stand in for arith_uint256;
explicit masking to 256 bits reproduces the fixed-width overflow-to-powLimit
behavior), for use both as documentation and as the fixed-point engine behind
the half-life simulation study.
"""

RADIX = 65536  # 2**16


def calculate_asert(ref_target: int, pow_target_spacing: int, time_diff: int,
                     height_diff: int, pow_limit: int, half_life: int) -> int:
    assert 0 < ref_target <= pow_limit
    # BCHN's own source asserts `(powLimit >> 224) == 0`, a precondition tied
    # to BCH mainnet's own powLimit (224-bit, 32 bits of headroom) that
    # BitAIcoin's real, intentionally-wider powLimit (228-bit, 28 bits of
    # headroom -- see _bitaicoin_powlimit_finding() below) does not satisfy.
    # Per the finding/recommendation there (option (a)): the property this
    # precondition actually needs to protect is that `ref_target * factor`
    # (factor < 2*RADIX = 2^17) plus the fixed-point left-shift step have
    # room to be computed without silently wrapping a fixed-width type before
    # the explicit overflow-to-powLimit clamp can catch it. A 16-bit-headroom
    # bound (`powLimit >> 240 == 0`) is the precondition BitAIcoin's port
    # actually relies on; it is satisfied (228 < 240) and is asserted here
    # instead of copying BCH's tighter, coincidental 224-bit figure verbatim.
    assert pow_limit >> 240 == 0, "powLimit must leave 16 bits of multiply headroom"
    assert height_diff >= 0
    assert abs(time_diff - pow_target_spacing * height_diff) < (1 << (63 - 16))

    # Python's // floors toward -inf; C++ integer division truncates toward 0.
    # Match C++ truncating-toward-zero division exactly (exact for the
    # magnitudes used here -- a production C++ port uses real int64 division,
    # not this Python stand-in):
    numer = (time_diff - pow_target_spacing * (height_diff + 1)) * RADIX
    q, r = divmod(numer, half_life)
    exponent = q + 1 if (r != 0 and (numer < 0) != (half_life < 0)) else q

    shifts = exponent >> 16  # NB: Python >> is arithmetic (floor) shift on ints, same as C++ on int64 for negative numbers here since exponent already floor-consistent with C++'s arithmetic right shift (C++ >> on signed is implementation-defined but universally arithmetic on real platforms, matching Python)
    frac = exponent & 0xFFFF  # uint16_t(exponent) truncates to low 16 bits, unsigned

    factor = 65536 + (
        (195766423245049 * frac
         + 971821376 * frac * frac
         + 5127 * frac * frac * frac
         + (1 << 47)) >> 48
    )

    next_target = ref_target * factor
    shifts -= 16
    MASK256 = (1 << 256) - 1
    if shifts <= 0:
        next_target >>= -shifts
    else:
        shifted = (next_target << shifts) & MASK256
        # overflow check: does shifting back down recover the original?
        if (shifted >> shifts) != next_target:
            next_target = pow_limit
        else:
            next_target = shifted

    if next_target == 0:
        next_target = 1
    elif next_target > pow_limit:
        next_target = pow_limit
    return next_target


def _self_check():
    """
    Validate against the algorithm's own defining mathematical properties --
    NOT against externally-sourced numeric test-vector rows (the GitLab
    qa-assets CSVs returned HTTP 403 to automated fetch in this session, and
    the available web-fetch tool summarizes/paraphrases fetched pages rather
    than passing through raw bytes, so literal third-party numeric rows
    could not be independently confirmed byte-for-byte this session -- this
    limitation is disclosed rather than hidden). These properties follow
    directly from the formula next_target = ref_target * 2^((time_diff -
    spacing*(height_diff+1))/halflife) and are exact algebraic consequences
    of it, so an implementation that satisfies all of them to within the
    documented fixed-point rounding error (< 0.013%, per the BCH spec's own
    stated bound on the polynomial approximation) is a strong correctness
    signal independent of trusting any single memorized row of numbers.
    """
    # Use a powLimit shaped like real Bitcoin/BCH mainnet's (32 leading zero
    # bits, i.e. < 2^224) so the algebraic properties are checked under the
    # same precondition BCHN's own assert(s) require. BitAIcoin's ACTUAL
    # powLimit does not satisfy this -- see _bitaicoin_powlimit_finding()
    # below, which documents that as a separate, explicit finding rather
    # than silently using a substitute value here.
    pow_limit = 0xFFFF << 208  # real Bitcoin/BCH mainnet powLimit (nBits 0x1d00ffff), < 2^224
    ref_target = pow_limit >> 3
    spacing = 600
    half_life = 172800  # BCH mainnet value, used only for this generic self-check

    # Property 1: arriving exactly on schedule (time_diff == spacing*height_diff...
    # but formula uses height_diff+1, so "on schedule" for height_diff=0, time_diff=0
    # is NOT exponent 0 -- exponent = (0 - spacing*1)/halflife, a small negative
    # value. Exponent is exactly 0 when time_diff == spacing*(height_diff+1).
    for height_diff in (0, 1, 10, 2016):
        time_diff = spacing * (height_diff + 1)
        t = calculate_asert(ref_target, spacing, time_diff, height_diff, pow_limit, half_life)
        rel_err = abs(t - ref_target) / ref_target
        assert rel_err < 0.0002, f"on-schedule identity failed at height_diff={height_diff}: rel_err={rel_err}"

    # Property 2: +halflife seconds of extra time_diff (relative to on-schedule)
    # doubles the target; -halflife halves it.
    height_diff = 1000
    on_schedule_time = spacing * (height_diff + 1)
    t0 = calculate_asert(ref_target, spacing, on_schedule_time, height_diff, pow_limit, half_life)
    t_plus = calculate_asert(ref_target, spacing, on_schedule_time + half_life, height_diff, pow_limit, half_life)
    t_minus = calculate_asert(ref_target, spacing, on_schedule_time - half_life, height_diff, pow_limit, half_life)
    assert abs(t_plus / t0 - 2.0) < 0.0002, f"+halflife doubling failed: ratio={t_plus/t0}"
    assert abs(t0 / t_minus - 2.0) < 0.0002, f"-halflife halving failed: ratio={t0/t_minus}"

    # Property 3: monotonicity -- more elapsed time (easier conditions implied)
    # strictly increases the target, all else equal.
    prev = None
    for extra in range(0, 20000, 1000):
        t = calculate_asert(ref_target, spacing, on_schedule_time + extra, height_diff, pow_limit, half_life)
        if prev is not None:
            assert t >= prev, "monotonicity violated"
        prev = t

    # Property 4: clamping -- an extreme positive time_diff clamps at powLimit,
    # not overflow garbage; refTarget already at powLimit stays at powLimit
    # going further easy.
    t_extreme = calculate_asert(ref_target, spacing, on_schedule_time + 10**8, height_diff, pow_limit, half_life)
    assert t_extreme == pow_limit, "failed to clamp to powLimit on extreme easy shift"
    t_at_limit = calculate_asert(pow_limit, spacing, on_schedule_time + 10**8, height_diff, pow_limit, half_life)
    assert t_at_limit == pow_limit, "failed to stay clamped at powLimit"

    print("All self-consistency checks passed:")
    print("  - on-schedule arrival reproduces ref_target (within 0.02% fixed-point rounding)")
    print("  - +halflife doubles target, -halflife halves target (within 0.02%)")
    print("  - target is monotonic increasing in elapsed time")
    print("  - clamps correctly at powLimit, does not overflow")


def _bitaicoin_powlimit_finding():
    """
    Real finding, not a hypothetical: BCHN's CalculateASERT hard-asserts
    `(powLimit >> 224) == 0`, i.e. powLimit must fit in the low 224 bits
    (< 2^224). Real Bitcoin/BCH mainnet's powLimit (nBits 0x1d00ffff) has 32
    leading zero bits and satisfies this with 32 bits to spare.

    BitAIcoin's actual powLimit (bits 0x1d0fffff, hex
    0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffff) has only
    28 leading zero bits -- 4 bits short of the 32-bit margin BCHN's own
    mainnet value has, and it FAILS the `< 2^224` precondition outright
    (it's >= 2^224, so `powLimit >> 224 != 0`).
    """
    bitaicoin_pow_limit = int("0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 16)  # src/kernel/chainparams.cpp:289, verified 2026-09-23
    assert bitaicoin_pow_limit.bit_length() == 228, bitaicoin_pow_limit.bit_length()
    violates = (bitaicoin_pow_limit >> 224) != 0
    print(f"\nBitAIcoin powLimit bit_length = {bitaicoin_pow_limit.bit_length()} "
          f"(real Bitcoin/BCH mainnet's is 224)")
    print(f"BCHN's CalculateASERT precondition `(powLimit >> 224) == 0` "
          f"{'FAILS' if violates else 'holds'} for BitAIcoin's powLimit.")
    print("Consequence: this is a plain C assert(), which Bitcoin Core release "
          "builds compile with NDEBUG (assert() compiled out) -- so this would "
          "NOT abort a release binary. The actual overflow protection in "
          "CalculateASERT is the separate, unconditional runtime check at the "
          "left-shift step (`if ((nextTargetShifted >> shifts) != nextTarget) "
          "nextTarget = powLimit;`), which does not depend on this assert and "
          "remains in effect regardless. So functional correctness is not lost, "
          "but the precondition is real, and a debug build of BitAIcoin's port "
          "would abort on the very first ASERT retarget after activation.")
    print("DECISION applied in this port (flagged for the C++ implementation to "
          "carry forward, not resolved silently): relaxed the precondition to "
          "the property BitAIcoin's port actually relies on -- `powLimit >> 240 "
          "== 0` (16 bits of multiply headroom, since factor < 2*RADIX = 2^17) "
          "-- which BitAIcoin's 28-bit-leading-zero powLimit satisfies, instead "
          "of copying BCH mainnet's tighter, coincidental 224-bit figure "
          "verbatim. calculate_asert() above now asserts this relaxed bound.")
    holds_relaxed = (bitaicoin_pow_limit >> 240) == 0
    print(f"Relaxed precondition `(powLimit >> 240) == 0` holds for BitAIcoin: {holds_relaxed}")
    assert holds_relaxed
    return violates


if __name__ == "__main__":
    _self_check()
    _bitaicoin_powlimit_finding()
