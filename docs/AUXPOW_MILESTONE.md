# AuxPoW activation — pre-production consensus milestone

Status: **Planning + baseline recorded. Implementation not yet started.** This document exists so the
decision, the reasoning, and the exact pre-change chain state are on record before any consensus code
is touched, per the explicit instruction that opened this milestone (2026-09-23).

Do not rewrite or invalidate any existing BitAIcoin block. This milestone only ever adds a
height-gated branch at a height strictly above the current tip.

## 1. Pre-AuxPoW baseline (recorded 2026-09-23, before any consensus change)

- **Tip height:** 225823
- **Tip hash:** `0000000ad1060dd6b63a31796c4d57977c7f009eb0229b32d0b15dd303ecff57`
- **BitAIForkAnchorHeight:** 225429 / **BitAIForkAnchorHash:** `0000000000000366ce98ca28338900094e8cbf445776253181749f782546d006`
- **BitAIActivationHeight:** 225430 (== SegwitHeight)
- **BitAIActivationPowLimit:** `0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff`
- **consensus.powLimit:** `0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff` (28 bits headroom, M5 fix)
- **consensus.BitAIHistoricalPowLimit:** `00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff` (real mainnet's, for pre-activation retarget reproduction)
- **nSubsidyHalvingInterval:** 210000 (unmodified)
- **nPowTargetTimespan:** 1209600 (two weeks) / **nPowTargetSpacing:** 600 — retarget interval = 2016 blocks, on **absolute height**, continuing real Bitcoin's own epoch boundaries (confirmed: height 225792, the M5 bug height, is an exact multiple of 2016 — the fork does not reset retarget windows to zero at the activation point)
- **BitAIForkId:** `0x424149` ("BAI"), folded into sighash at height >= BitAIActivationHeight for replay protection — a **separate mechanism** from any AuxPoW chain ID (see §5)
- **pchMessageStart:** `b7 78 d8 11` / **nDefaultPort:** 28333 / **bech32_hrp:** "bai"
- **Difficulty at tip and at block 225430:** identical, 0.06249910592947572 (`bits` 0x1d0fffff).
  **Correction (2026-09-23) to an earlier, wrong characterization in this doc:** this is *not*
  because "no retarget has happened yet." A native, absolute-height 2016-block retarget already
  occurred at height 225792 (225792 / 2016 = 112 exactly — see the epoch-boundary note above) i.e.
  well within the 225430-225823 span already covered by this baseline. That retarget genuinely ran
  and genuinely recomputed a new target from the preceding 2016-block window — it simply reproduced
  the identical target value, because the chain has been mining at a stable, low rate relative to
  `consensus.powLimit` throughout that window, not because the retarget was skipped or didn't fire.
  The original text here ("no retarget yet... well under the 2016-block retarget window") was
  factually wrong and is corrected here rather than silently edited out.

## 2. The five points requested before implementation

### (1) Proposed activation height: **227808**

227808 is the **next 2016-block retarget boundary after the current tip** (225823 → next boundary
225792 is already past; the one after that is 227808 = 2016 × 113). Reasoning:

- The retarget windows run on absolute height (confirmed above), so 227808 is a real, load-bearing
  boundary the existing DAA code already treats specially — not an arbitrary number.
- Activating AuxPoW *and* any DAA change (§3) at the same point the code already recomputes
  difficulty means the epoch immediately before activation is homogeneous (pure legacy SHA256d, as
  today) and the epoch immediately after can run entirely under the new rules from its first block.
  A boundary that fell mid-epoch would need the retarget math to reason about a window straddling
  two different mining/difficulty regimes, which is exactly the kind of edge case that produced the
  M5 overflow bug.
- Runway: 227808 − 225823 = **1985 blocks** from today's tip. Given the plan is to mine through this
  deliberately with real hashpower rather than wait out lab-pace block times, this is calendar
  runway to build and test against, not a constraint on how fast you could reach it if you chose to.
  If more calendar time is wanted for the 3-node upgrade, the next candidate is 229824 (one epoch
  later); I'd only move to it if 1985 blocks turns out to be too little runway in practice.

### (2) Proposed AuxPoW chain ID: **16969** (0x4249, the two ASCII bytes "BI" read as a big-endian u16)

This is a *different field, for a different purpose*, from `BitAIForkId` (0x424149, "BAI", folded
into transaction sighash for replay protection). The AuxPoW chain ID is a small integer committed
into the *parent block's* coinbase merge-mining tag and merkle structure, used purely to bind a
parent-chain block to *this specific* auxiliary chain and to reject AuxPoW proofs built for a
different chain. Keeping the two constants visibly related (BAI-derived) but numerically distinct
avoids implying they're interchangeable.

Caveat: chain-ID collision only matters against other chains actually being merge-mined on the same
parent network. There's an informal community registry of chain IDs used by real merge-mined
alts (Namecoin, Syscoin, and others historically assigned low integers). Since BitAIcoin is a
private lab network today, there's no live collision risk — this becomes relevant only if BitAIcoin
is ever merge-mined against a public parent chain, at which point the chosen value should be
checked against that registry before going live, not before.

### (3) Proposed DAA behavior: **switch to a fast-reacting algorithm (ASERT) exactly at the activation height**

The existing 2016-block retarget is a well-documented poor fit for a sudden merge-mining hashrate
change: real Bitcoin miners turning on AuxPoW support can add orders of magnitude of hashrate
instantly, and a classic 2016-block window won't correct for up to two weeks of real time (or,
worse, a burst of near-instant blocks on this low-difficulty lab chain), which is exactly the
adversarial scenario the milestone asks to be tested. Proposal:

- At heights < 227808: **unchanged** — today's `CalculateNextWorkRequired`, byte-for-byte.
- At heights >= 227808: an **ASERT**-style algorithm (the approach Bitcoin Cash adopted in 2020 for
  this same class of problem), retargeting every block rather than every 2016, converging toward the
  correct difficulty within a small number of blocks after a hashrate shock instead of up to a full
  epoch.
- This needs its own overflow/precision safety proof, mirroring the M5 lesson (`docs/CONSENSUS.md`
  point 6): ASERT's math is exponent-based fixed-point, a different arithmetic shape from the
  existing linear retarget, and needs its own explicit headroom argument and a startup assert, not
  an assumption that "it's a well-known algorithm so it's safe here."

### (4) Reference implementation: **Namecoin's original AuxPoW design**, cross-checked against **Syscoin's** more modern maintained fork of it

Namecoin's AuxPoW is the foundational, SHA256d-native merge-mining design (this matters: Dogecoin's
AuxPoW is scrypt/Litecoin-oriented and not the right shape here). Namecoin's own tree targets a much
older Bitcoin Core codebase than this one (v31.1, CMake, C++20), so it's a *design* reference, not
something to vendor directly. Syscoin maintains a more actively updated SHA256d AuxPoW
implementation closer to modern Bitcoin Core internals, useful as a porting reference for how the
same validation logic reads against a current codebase. I'll port the *logic* (serialization shape,
validation order, the specific rejection cases the milestone lists) rather than copy either tree
verbatim, since neither is a drop-in fit for this fork's CMake build or its existing height-gated
patterns.

### (5) Compatibility risk against existing 225430+ history: **none, by construction**

AuxPoW validation only ever applies to blocks at height >= 227808. Every block from 225430 through
the current tip, and everything mined between now and 227808 under legacy rules, keeps validating
exactly as it does today — `CheckProofOfWork` on the block's own header hash, no AuxPoW structure
present or required. This mirrors the pattern already used for `BitAIActivationHeight` itself
(SegWit/Taproot/replay-protection all branch cleanly on a height check with no retroactive
reinterpretation of earlier blocks), so there's direct precedent in this codebase for doing this
kind of gate safely. The one thing that must be implemented as a *pure height branch with no shared
mutable state* is the DAA change in (3), for the same reason.

**No genuine design conflict found.** Proceeding, per instruction.

## Next concrete step

This is a genuine multi-part consensus implementation (serialization, validation, a new
DAA branch, new RPCs, and the full adversarial/activation-boundary/reorg/multi-node test matrix
listed in the milestone). It will be built and tested incrementally, not delivered as one
untested block of code — starting with the AuxPoW header/proof serialization and validation core,
since every other piece (the RPCs, the tests, the multi-node upgrade) depends on that being correct
first.

**Status update (2026-09-23, commit `800c533c3a`): the serialization/validation core is done.**
`src/auxpow.h`/`src/auxpow.cpp` implement `CAuxPow` (parent header + coinbase tx + both merkle
branches), the version-bit helpers, `CheckMerkleBranch()`, the Namecoin-compatible
index-grinding-resistant `GetExpectedMerkleTreeIndex()`, and `CAuxPow::Check()` covering every
rejection case listed in sec.4/5 above (parent PoW vs. the auxiliary chain's own target -- not the
parent chain's, a real bug caught and fixed before any test was written around the wrong version --
parent-not-itself-AuxPoW, coinbase merkle inclusion, exactly-one-tag ambiguity defense, chain-merkle
commitment, index-grinding defense). `src/test/auxpow_tests.cpp` has 9 real end-to-end tests, each
building an actual fake parent block and running it through `Check()`; all 9 pass, and the full
existing 749-case `test_bitcoin` suite still passes with zero regressions (both verified by an actual
`cmake --build` + test run this session, not assumed).

**Explicitly NOT yet done, next slice:** wiring this into `CheckProofOfWorkImpl`,
`ContextualCheckBlockHeader`, net_processing's header/block relay, and GBT/mining. Splicing new
block-acceptance-path serialization into the live consensus code is being kept as its own separate,
reviewed change rather than folded into the same commit as the core logic -- consistent with "built
and tested incrementally" above. The DAA branch (ASERT, sec.3/A) is validated as a standalone module
(`contrib/asert_reference.py`) but likewise not yet wired into `pow.cpp`'s real `GetNextWorkRequired`
dispatch. New RPCs, the obsolete-node fork test (protocol frozen in sec.C), and the stabilization
checkpoint (sec.D) all remain after that wiring lands.

## Addendum (2026-09-23): frozen details, before the first consensus commit

Adds precision on top of §1-5 above per explicit follow-up instruction. Nothing in §1-5 above is
changed; this section makes each item exact and adds three new required items.

### A. ASERT specification, exact (BCH/BCHN `aserti3-2d`, not "ASERT-style")

Reference: the BCH `aserti3-2d` specification (adopted via CHIP-2020-05, implemented in BCHN's
`pow.cpp`), kept as a *separate* reference from the Namecoin/Syscoin AuxPoW references in §4 above
-- these are two unrelated pieces of prior art (a difficulty algorithm and a merge-mining proof
format) and should not be conflated.

- **Target block interval:** 600 seconds -- reuses BitAIcoin's existing `nPowTargetSpacing`
  unchanged. No new constant introduced for this.
- **Anchor block:** height 227807, the last block validated under the legacy (pre-activation)
  retarget algorithm.
- **CORRECTION (2026-09-23) to the anchor/time-reference convention, found by reading BCHN's actual
  `pow.cpp` rather than continuing to derive it from prose, per explicit instruction:** my first-pass
  text above said `anchor_target` and `anchor_time` are read from the anchor block's own header, and
  the exact-formula bullet (kept below, now corrected) said `time_diff` = current block's time minus
  *anchor block's* time. **Both were wrong in the same way.** The real BCHN convention, confirmed
  from `GetNextASERTWorkRequired`/`CalculateASERT` in
  [bitcoin-cash-node/bitcoin-cash-node `src/pow.cpp`](https://github.com/bitcoin-cash-node/bitcoin-cash-node/blob/master/src/pow.cpp)
  and the [official upgrade spec](https://upgradespecs.bitcoincashnode.org/2020-11-15-asert/):
    - `time_diff` = the **tip block's** time (i.e. `pindexPrev`, the most recently connected block
      when computing the *next* block's target -- never the being-mined block's own time, since
      that's unknown at target-computation time) minus the **anchor block's PARENT's** time, not the
      anchor block's own time.
    - `height_diff` = the tip block's height minus the anchor height.
    - `anchor_target` = the anchor block's own `nBits`, which *is* read from its own header (this
      part was right).
  For BitAIcoin, concretely: `anchorParams = {nHeight: 227807, nBits: <227807's own bits>,
  nPrevBlockTime: <block 227806's time>}`. When computing the target for block N (N > 227807), the
  tip is block N-1, so `time_diff = time(N-1) - time(227806)` and `height_diff = height(N-1) -
  227807`. This must be implemented exactly this way, not "from the anchor's own timestamp" -- it is
  the kind of off-by-one-block, easy-to-get-wrong-from-memory detail the instruction to "test the
  timestamp-reference convention rather than deriving it from prose" was specifically about, and
  reading the real source (not just the spec prose, which is easy to mis-paraphrase on this exact
  point) is what caught it.
- **Exact formula (corrected to match the real convention above):**
  `next_target = anchor_target * 2^((time_diff - target_interval*(height_diff+1)) / halflife)`,
  with `time_diff`/`height_diff` as just defined (tip-vs-anchor's-parent, tip-vs-anchor). The
  `height_diff + 1` (not `height_diff`) is deliberate and matches the reference spec: it accounts for
  the anchor block itself already having "used up" one interval's worth of schedule, so a block
  arriving exactly on schedule produces exponent 0 (no change) rather than a small permanent bias.
- **Integer algorithm, transcribed line-for-line from the real source** (not reconstructed from
  memory of the spec prose) into `contrib/asert_reference.py::calculate_asert()`, including the
  actual polynomial coefficients (`195766423245049`, `971821376`, `5127`, rounding constant `2^47`,
  `>> 48`, radix `65536`), the integer-shift/fractional-remainder split, and the left-shift overflow
  clamp. **Validated in this session** against the algorithm's own defining algebraic properties
  (exact identity on-schedule, exact doubling at +halflife, exact halving at -halflife, monotonicity,
  correct clamping) rather than against literal third-party numeric test-vector rows -- disclosed
  limitation: the GitLab `qa-assets` CSV test vectors
  ([bchn-sw/qa-assets](https://gitlab.com/bitcoin-cash-node/bchn-sw/qa-assets/-/tree/master/test_vectors/aserti3-2d))
  returned HTTP 403 to automated fetch this session, and the available web-fetch tool paraphrases
  fetched pages rather than passing through raw bytes, so literal upstream row values could not be
  independently byte-confirmed here. The algebraic self-consistency checks are a real, defensible
  validation (they're exact mathematical consequences of the formula, not something a wrong
  transcription would pass by luck), but obtaining the literal CSV and diffing it byte-for-byte
  remains the stronger check and should be done from a normal browser/`git clone` before this ships
  as consensus code, not asserted as already done here.
- **Real, concrete finding from this validation, not present in the first-pass addendum:** BCHN's
  `CalculateASERT` hard-asserts `(powLimit >> 224) == 0`. BitAIcoin's actual `consensus.powLimit`
  (`src/kernel/chainparams.cpp:289`, confirmed by reading the source directly) has only 28 leading
  zero bits (`bit_length() == 228`), 4 bits short of the 32-bit margin real Bitcoin/BCH mainnet's own
  powLimit has, so it **fails** this precondition outright. Because this is a plain C `assert()`,
  which is compiled out under `NDEBUG` in a release build, this would not crash a release binary --
  the actual overflow protection is the separate, unconditional runtime clamp at the left-shift step
  (`if ((nextTargetShifted >> shifts) != nextTarget) nextTarget = powLimit;`), which does not depend
  on this assert. But a debug build would abort on the very first ASERT retarget after activation,
  and copying the assert verbatim would be silently wrong for BitAIcoin's own, intentionally wider,
  powLimit. **Decision applied in `asert_reference.py`, flagged here for the eventual C++ port to
  carry forward, not resolved unilaterally as final:** relax the precondition to the property this
  code actually relies on -- `powLimit >> 240 == 0` (16 bits of multiply headroom, since the
  fixed-point `factor` is always `< 2*RADIX = 2^17`) -- which BitAIcoin's powLimit satisfies
  (228 < 240), instead of copying BCH mainnet's tighter, coincidental 224-bit figure verbatim.
- **powLimit / clamping behavior, corrected from my first pass:** the ceiling that matters is
  `next_target <= consensus.powLimit` (targets can't get *easier* than the chain-wide floor
  difficulty) -- **not** a floor preventing targets from getting *harder*; there is no consensus
  minimum on the hard side other than the practical limits of the 256-bit representation. I had
  this backwards in my first internal pass at simulating it; caught it before committing anything
  by deriving the target/hashrate relationship analytically rather than trusting a first buggy
  simulation run (details in §B).
  **Concretely relevant to BitAIcoin today:** verified from the live node that the chain has been
  running at exactly `consensus.powLimit` (bits `0x1d0fffff`) since block 225430, unchanged across
  the one retarget that's already happened -- so relative to the anchor block, the powLimit ceiling
  is *already binding*. Any future hashrate withdrawal cannot ease the target further from where it
  already sits; it can only slow blocks down, with no consensus-level relief available. This is
  correct, expected behavior for a chain already at its easiest allowed setting, not a bug.
- **Compact-target (`nBits`) rounding:** Bitcoin's compact format keeps only ~3 significant bytes of
  mantissa, so converting a full 256-bit target to `nBits` always rounds toward the nearest
  representable compact value (in practice, rounds the true target down slightly). ASERT's design
  avoids this compounding block-over-block *because it always recomputes from the anchor's original,
  full-precision target*, not from a previously-rounded `nBits` value re-expanded -- the rounding
  error doesn't accumulate the way it can in an EMA-based algorithm. This must be implemented as
  "recompute from the anchor every time," not "carry forward the last computed target," or that
  property is lost.

#### Half-life: NOT frozen. Expanded study below; the earlier "36x" claim is retracted.

**The earlier claim in this doc that a 6-hour half-life is "36x harder to whipsaw" than 1 hour is
retracted outright, per explicit instruction.** It was arithmetically wrong with no defensible
source (6h / 1h = 6x, not 36x -- there is no calculation that produces 36 from the numbers actually
in play). It should never have been written without being derived and shown. It is not being
"substantiated" after the fact; it's dropped.

**Replacement metric, precisely defined and reproduced, from BitAIcoin's real, unmodified consensus
timestamp constants** (confirmed via `grep` against `src/chain.h`/`src/validation.cpp`:
`MAX_FUTURE_BLOCK_TIME = 2*60*60` seconds, `nMedianTimeSpan = 11` blocks, hard rejection at
`nTime > now + MAX_FUTURE_BLOCK_TIME` and at `nTime <= GetMedianTimePast()` of the previous 11
blocks): the maximum inflation a single block's own target can receive from one miner maximally
lying about that one block's timestamp (shifting it the full allowed +7200s into the future) is
exactly `2^(MAX_FUTURE_BLOCK_TIME / halflife)`:

| half-life | max single-block target inflation via a +7200s timestamp lie |
|---|---|
| 1 hour (3600s) | **4.0000x** |
| 6 hours (21600s) | **1.2599x** |
| 1 day (86400s) | **1.0595x** |
| 2 days (172800s) | **1.0293x** |

This is a single-block, one-shot metric (one miner, one lied timestamp, immediate next-block
target), not a model of a sustained multi-block manipulation campaign -- stated as a limitation of
the metric itself, not hidden.

#### Expanded study: surge/recovery, pool-hopping, stochastic, and MTP-floor-aware

The prior version of this section used a floating-point behavioral stand-in for the DAA, permanent
shocks only, deterministic arrivals only, and no timestamp-consensus constraints -- explicitly
called out as insufficient ("the current withdrawal simulation only establishes that a hashrate drop
... cannot be compensated because the chain is already at powLimit") and redone from scratch per
instruction. The new study (`contrib/asert_halflife_simulation.py`, built on the bit-exact
`calculate_asert()` port validated in §A above) adds:

1. **Surge-then-revert** (not permanent-only): 1x baseline -> {10x, 100x, 1000x} held for
   {20, 100, 500} blocks -> back to 1x, with recovery measured *after* the surge ends, not from t=0.
2. **Repeated on/off pool-hopping cycles** at several on/off block-count combinations.
3. **A stochastic (Poisson-arrival) variant** alongside the deterministic (expected-value) model, 5
   seeds per half-life.
4. **BitAIcoin's real MTP-floor and future-time-ceiling constraints** applied to every simulated
   timestamp (a block's recorded time cannot go below `MTP(previous 11 blocks) + 1`).

**Surge-then-revert (selected rows; full table in the script's own output, reproducible via
`python3 contrib/asert_halflife_simulation.py`):**

| half-life | surge | held | blocks to recover post-surge | wall-clock to recover | worst instant rate multiple in transition |
|---|---|---|---|---|---|
| 1 hour | 1000x | 20 blk | 18 | 9.5h | 120.0x |
| 1 hour | 1000x | 500 blk | 2 | 169.9h (~7.1d) | 999.2x |
| 6 hours | 1000x | 20 blk | 61 | 16.1h | 600.0x |
| 6 hours | 1000x | 500 blk | 2 | 160.0h (~6.7d) | 939.7x |
| 1 day | 1000x | 20 blk | 1 | 3.7h | 600.0x |
| 1 day | 1000x | 500 blk | 458 | 159.2h (~6.6d) | 85.7x |
| 2 days | 1000x | 20 blk | 1 | 3.5h | 600.0x |
| 2 days | 1000x | 500 blk | 809 | 214.3h (~8.9d) | 300.0x |

Real finding, not previously visible under the permanent-shock-only model: for **short** surges
(held only 20 blocks), longer half-lives barely react at all before the surge ends, so "recovery"
looks nearly instantaneous (1-2 blocks) simply because there was nothing to recover *from* -- the
DAA never moved. For **long, sustained** surges (held 500 blocks), the ordering flips: the fast
half-lives (1h, 6h) have already eased the target most of the way toward the new equilibrium by the
time the surge ends and recover almost immediately (2 blocks), while the slow half-lives (1 day, 2
days) are still mid-adjustment when the shock ends and take hundreds of blocks and many days of
wall-clock time to finish settling. **A short half-life is not strictly "worse" here -- it's better
adapted to long sustained shocks and worse adapted to resisting brief manipulation; a long half-life
is the reverse.** This is the real tradeoff the half-life choice has to make, and it only shows up
once surge duration is varied, which the original permanent-only model could not reveal.

**Pool-hopping (10x/50-on/50-off, 6 cycles; 100x/20-on/100-off, 4 cycles; 1000x/10-on/200-off, 3
cycles):** worst instantaneous rate multiple during any cycle scales with the on-hashrate multiplier
roughly as expected (10.0x / 100.0x / 600.0x respectively, consistent across all four half-lives --
the *ceiling* of the swing is set by the multiplier itself, not by the half-life). Whether difficulty
fully re-settles to 1x by the *end* of each off-phase is more sensitive to the specific on/off block
counts chosen than to half-life alone (full table in script output) -- worth re-running with
production-realistic on/off durations once real pool behavior is observed, rather than reading too
much into the specific numbers from these illustrative cycle lengths.

**Stochastic vs. deterministic -- a real methodological finding, not just an alternative run:**
applying the deterministic report's strict metric ("20 consecutive individual blocks each within 10%
of target") to the Poisson-arrival logs returned **no settling point for any seed at any half-life**.
Investigated rather than silently switched away from: a Poisson arrival process has per-block
coefficient of variation of 1.0 (stdev == mean for an exponential distribution), so individual
blocks routinely land 2-5x off the mean purely from honest statistical noise, making "20 individual
blocks in a row within 10%" essentially unsatisfiable regardless of half-life or DAA quality. That is
a real property of the *metric*, not of the DAA, once arrivals are genuinely random rather than
expected-value-only -- disclosed as a limitation of the original metric definition, now fixed with a
rolling-window metric (mean rate over a trailing 20-block window, checked for persistence over the
following window) that is actually meaningful under stochastic noise:

| half-life | deterministic recovery (windowed) | stochastic recovery (windowed), 5 seeds |
|---|---|---|
| 1 hour | 21 blocks | 20-110 blocks (mean ~49) |
| 6 hours | 99 blocks | 70-138 blocks (mean ~96) |
| 1 day | 203 blocks | 32-104 blocks (mean ~66) |
| 2 days | 154 blocks | 20-149 blocks (mean ~61) |

The half-life ordering is not perfectly monotonic under either metric at this scenario (100x/100blk
surge) -- both the deterministic and stochastic windowed numbers show 1 day recovering *faster* than
6 hours here, which is a genuine artifact of exactly how far a 100x/100-block surge pushes each
half-life's target before reverting (a 6-hour half-life has moved further from anchor by block 100
than a 1-day one has, so has more distance to unwind) rather than a general "slower half-lives always
recover faster" rule -- the surge-then-revert table above, which varies surge duration, is the more
complete picture. Under real stochastic noise, recovery times for any single half-life vary by roughly
2-5x across seeds, which any half-life decision needs to tolerate as normal variance, not treat as a
DAA malfunction.

**MTP-floor impact -- a genuine, disclosed constraint, but a narrow one:** the real MTP floor (nTime
must exceed the median of the previous 11 blocks) essentially never binds for the 10x-1000x, ≤500-block
scenarios above (0 blocks clamped in every case checked). It only starts to bind under a much more
extreme, sustained combination -- a 10,000x surge held for 500 blocks -- and even then only for a
minority of blocks: 5/500 at 1-hour half-life, 20/500 at 6 hours, 75/500 at 1 day, 85/500 at 2 days.
**Counterintuitive but explainable finding:** the MTP floor binds *more*, not less, at longer
half-lives under an extreme sustained surge, because a slower-reacting DAA leaves the target closer
to its pre-surge (harder-to-mine-for-the-new-hashrate) value for longer, which is exactly the
condition (target still "too easy" relative to the surged hashrate) that drives the algorithm's own
*expected* block interval below one second, at which point the integer-second timestamp floor -- not
the DAA -- becomes the binding constraint. This is a second, independent point in favor of a shorter
half-life for resisting extreme sustained arrival shocks specifically, to be weighed against the
single-block timestamp-manipulation metric above, which favors a *longer* half-life.

**No half-life is selected here.** Per instruction, this section compares {1h, 6h, 1d, 2d} on the
full expanded scenario set and leaves the consensus value open. The tradeoffs now visible: short
half-lives adapt faster to long sustained shocks and resist the MTP-floor interaction better, but are
more exposed to single-block timestamp manipulation and to whipsaw during pool-hopping-style cycling;
long half-lives are the reverse. Full reproducible output: `python3
contrib/asert_halflife_simulation.py` (uses `contrib/asert_reference.py`, the validated bit-exact
ASERT port from §A).

### B. AuxPoW chain ID 16969 -- checked now, real search, sources included

Searched actual chainparams source and the Bitcoin/BCH merged-mining wiki for every real assigned
AuxPoW chain ID I could find. Confirmed values, with sources:

| Chain | nAuxpowChainId |
|---|---|
| Namecoin | 1 (`0x0001`) |
| IXCoin | 3 (`0x0003`) mainnet; 1 on testnet/regtest (shared with Namecoin -- accepted there because strict chain-ID checking is normally relaxed on test networks, not something to copy for BitAIcoin's own mainnet-equivalent) |
| Bunkercoin | 73 (`0x0042`) |
| Myriadcoin | 90 (`0x005A`) |
| Dogecoin | 98 (`0x0062`) |
| Elastos (ELA, actively merge-mined with real Bitcoin today) | 1224 |

**16969 collides with none of these.** Phrased honestly, as instructed: this is **"no known
collision,"** not a guarantee of global uniqueness -- there is no single authoritative registry, and
smaller or now-dead merge-mined alts (I0Coin, Devcoin, and others) may have used values I couldn't
find a documented source for. `BitAIForkId` (0x424149, "BAI", a sighash replay-protection value) and
the AuxPoW chain ID (16969, "BI") remain **completely separate constants used for unrelated
purposes** -- confirmed unchanged from the original proposal.

Sources: [Merged mining specification](https://en.bitcoin.it/wiki/Merged_mining_specification),
[IXCoin chainparams.cpp](https://github.com/IXCore/IXCoin/blob/master/src/chainparams.cpp),
[Dogecoin chainparams.cpp](https://github.com/dogecoin/dogecoin/blob/master/src/chainparams.cpp),
[Myriadcoin chainparams.cpp](https://github.com/myriadcoin/myriadcoin/blob/master/src/chainparams.cpp),
[Bunkercoin chainparams.cpp](https://github.com/bunkercoin/bunkercoin/blob/master/src/chainparams.cpp),
[Elastos merged-mining guide](https://github.com/elastos/Elastos.ELA/wiki/Merged-mining-guide).

### C. Obsolete-node fork test -- protocol frozen now, run once implementation exists

**Preserved today, before any consensus code changes,** so the test later uses a byte-identical,
guaranteed-authentic pre-AuxPoW binary rather than a rebuild that could drift:

- Mac (arm64): `~/Downloads/bitaicoin-dev/pre-auxpow-binaries-mac/{bitaicoind,bitaicoin-cli}`,
  sha256 `f76a767b...` / `e901817e...`
- BITAISERVER3 (x86_64): `/opt/pre-auxpow-binaries/{bitaicoind,bitaicoin-cli}`,
  sha256 `f692667b...` / `e927276c...`
- Both built from `bitaicoin-phase1` @ `2734adbd25` (docs-only; no consensus code exists on top of
  this yet).

**Test protocol, to run once AuxPoW is implemented and before crossing 227808 for real:**
1. Stand up a 4th node instance running the preserved old binary, on its own datadir, `addnode`'d to
   the three upgraded nodes, synced to the same pre-activation tip.
2. Mine/advance the upgraded three-node network across height 227808 using real AuxPoW-validated
   blocks.
3. Record explicitly: the old node's final tip height and hash (expected: stalls at 227807 and never
   advances, since it should either fail to deserialize the new block format's AuxPoW-flagged
   version bit or reject a header it can't validate -- the *exact* failure mode, not just "it
   stalls," is the point of running this for real rather than assuming); any error/log output it
   produces; whether it crashes versus cleanly rejects.
4. Confirm all three upgraded nodes converge on the same tip hash as each other after the boundary.
5. Document old-node-versus-upgraded-node behavior explicitly in the milestone doc, with the actual
   recorded heights/hashes/log excerpts -- this is a deliberate hard fork, proven while the network
   is still private, exactly as instructed.

### D. Post-activation stabilization checkpoint -- placeholder only, no hash yet

Not chosen now, deliberately. Criteria to be met before a specific block is picked and recorded as
the checkpoint/baseline: the chain must have (1) crossed 227808 under real AuxPoW validation, (2)
survived a restart of all three nodes from that state, (3) survived at least one real reorg crossing
the activation boundary, and (4) run under sustained, substantial SHA256d hashpower for long enough
to demonstrate the DAA settling behavior in practice, not just in simulation. Once all four hold,
the specific height/hash/chainwork of a block chosen from that proven run gets recorded here.

### Confirmed unchanged
Existing blocks 225430-225823 remain untouched -- nothing above alters how any existing block
validates. Proceeding to the AuxPoW serialization/validation core next.
