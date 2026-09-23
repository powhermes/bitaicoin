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
