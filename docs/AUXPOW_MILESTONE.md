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
- **Difficulty at tip and at block 225430 (verified directly from both, identical, no retarget yet):** 0.06249910592947572 (`bits` 0x1d0fffff) — expected, since only 394 blocks have passed since activation, well under the 2016-block retarget window

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
  retarget algorithm. `anchor_target` and `anchor_time` are read from that block's own header --
  not from a running EMA, not from the previous block once ASERT is live. Every subsequent block's
  target is computed **fresh from this fixed anchor**, which is the core property that makes ASERT
  immune to the cumulative-rounding-drift problem an EMA-style algorithm has.
- **Exact formula:**
  `next_target = anchor_target * 2^((time_diff - target_interval*(height_diff+1)) / halflife)`
  where `time_diff` = current block's timestamp minus anchor block's timestamp, `height_diff` =
  current block's height minus anchor height. The `height_diff + 1` (not `height_diff`) is
  deliberate and matches the reference spec: it accounts for the anchor block itself already having
  "used up" one interval's worth of schedule, so a block arriving exactly on schedule produces
  exponent 0 (no change) rather than a small permanent bias.
- **Integer algorithm:** BCHN's reference implementation computes this with **fixed-point
  arithmetic, scale factor 2^16** (not floating point) -- `exponent = ((time_diff - target_interval
  *(height_diff+1)) << 16) / halflife`, then splits `exponent` into an integer number of bit-shifts
  (`exponent >> 16`) plus a fractional remainder in `[0, 65536)`, applies the integer shifts
  directly to the 256-bit target, and approximates `2^(fractional/65536)` with a validated
  polynomial. **I will implement and validate this integer path against BCHN's own published test
  vectors before it is committed as consensus code** -- reproducing fixed-point polynomial
  coefficients from memory is exactly the kind of thing that must be checked against a known-good
  reference, not trusted on recall, given what "exactly, not merely ASERT-style" is asking for.
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

#### Half-life: simulated, not guessed

Built a deterministic (expected-value) simulation of the exact formula above, at BitAIcoin's real
`nPowTargetSpacing` of 600s, against the requested 10x/100x/1000x arrival (hashrate increase) and
withdrawal (hashrate decrease) shocks, across six half-life candidates. Two real bugs were caught
and fixed while building it before trusting any output: (1) the target/hashrate relation was
initially inverted (fixed by deriving the steady-state condition analytically: for a permanent
hashrate multiplier k, the correct equilibrium is `relative_target = 1/k`, not `k`); (2) the
powLimit ceiling above had to be added, or withdrawal scenarios produce a nonsensical unbounded
blow-up in the model.

| half-life | arrival 10x | arrival 100x | arrival 1000x | withdrawal (any factor) |
|---|---|---|---|---|
| 1 hour | 38 blk / 6.4h | 59 blk / 6.5h | 79 blk / 6.6h | clamped at powLimit, no recovery possible |
| 2 hours | 77 blk / 9.6h | 119 blk / 10.0h | 159 blk / 10.0h | same |
| 6 hours | 233 blk / 22.8h | 358 blk / 23.7h | 478 blk / 23.8h | same |
| 1 day | 934 blk / 82.3h | 1432 blk / 85.6h | 1913 blk / 86.0h | same |
| 2 days (BCH mainnet) | 1869 blk / 161.7h | 2865 blk / 168.2h | 3826 blk / 168.9h | same |
| 4 days | 3739 blk / 320.5h | 5731 blk / 333.6h | 7652 blk / 334.9h | same |

"Settle" = the ratio of actual to target block-arrival rate stays within 10% for at least 20
consecutive blocks. Withdrawal scenarios never show a settling time because, given the chain's
*current* anchor-adjacent state (already at powLimit), there is nothing for the algorithm to ease
toward -- it correctly holds at the ceiling and blocks simply arrive slower, with the deviation
exactly equal to the withdrawal factor for as long as the withdrawal persists. This is a genuine,
useful finding, not a gap in the study: it means the arrival direction is the one with real dynamics
to tune for right now, and the withdrawal direction is a *clamp-is-honored* test, not a
*settling-speed* test.

**Recommendation: half-life = 6 hours (21600 seconds).** Settles a full 10x-1000x arrival shock
within about a day of dedicated testing (22.8-23.8 hours), which fits a deliberate, hands-on stress
test session, while being 36x longer than the 1-hour candidate -- long enough that ordinary
block-timestamp noise, or a short deliberate timestamp-shading attempt, can't whipsaw the difficulty
the way an extremely short half-life would let it. This is a recommendation with the evidence
attached, not a final decision on my authority alone -- happy to move to 1-2 hours for faster
test iteration, or 1+ day for more manipulation resistance, if either is preferred once the table is
in front of you.

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
