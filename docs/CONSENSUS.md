# Consensus Rules

This document lists every point at which BitAIcoin's consensus rules
differ from real Bitcoin mainnet, and — just as importantly — every point
where they deliberately do **not** differ. Anything not listed here is
unmodified Bitcoin Core v31.1 consensus code running unconditionally.

## Inherited unmodified (below and around the fork point)

`CBitAIcoinParams` copies these directly from `CMainParams`, verbatim,
including below the fork point where they apply to real historical blocks
exactly as they always have:

| Rule | Height | Status on BitAIcoin |
|---|---|---|
| BIP34 (coinbase height) | 227931 | Inherited, real mainnet value |
| BIP65 (CHECKLOCKTIMEVERIFY) | 388381 | Inherited, real mainnet value |
| BIP66 (strict DER) | 363725 | Inherited, real mainnet value |
| CSV (BIP68/112/113) | 419328 | Inherited, real mainnet value |
| SegWit | 481824 | Inherited, real mainnet value — **see Open Question below** |
| Taproot (BIP340-342, BIP9 bit 2) | activation window 709632+ | Inherited, real mainnet value — **structurally rejected post-activation regardless, see below** |
| Subsidy schedule (50 coin, halving every 210,000 blocks) | — | Inherited unmodified, including through the fork point |
| `nPowTargetTimespan` / `nPowTargetSpacing` | — | Inherited (14 days / 10 minutes), governs retargeting cadence both before and after the fork |
| Signature/script verification rules (opcodes, standardness at consensus level, sigop limits, block/tx size limits) | — | Entirely unmodified |

These are copied rather than deleted specifically so that pre-fork history
(0–225429) validates under exactly the rules real Bitcoin nodes have
always enforced for those blocks — a chain that silently reinterpreted its
own imported history would not actually be validating it.

## BitAIcoin-specific deviations

### 1. Fork anchor enforcement (new)

`ContextualCheckBlockHeader` (`src/validation.cpp`) rejects any block at
height `BitAIForkAnchorHeight` (225429) whose hash does not equal
`BitAIForkAnchorHash`. This is not a Bitcoin mainnet rule — mainnet has no
concept of a required hash at a specific height baked into consensus code
this way (`assumevalid`/checkpoints are soft, operator-configurable hints,
not always-enforced consensus). On BitAIcoin, this one height is
non-negotiable by design: it is the entire mechanism that ties BitAIcoin's
ledger to real Bitcoin's.

### 2. Activation-height difficulty transition (new)

`GetNextWorkRequired` (`src/pow.cpp`) special-cases exactly one height —
when the next block is `BitAIActivationHeight` (225430) — and returns
`BitAIActivationPowLimit`'s compact form directly, bypassing the normal
retarget calculation for that one block only. Every other height, before
and after activation, falls through unmodified to Bitcoin's standard
2016-block `CalculateNextWorkRequired` retargeting. The retarget-boundary
modulus is **not** rebased to the activation height — the first
post-activation retarget window is simply a short/partial one, which the
existing algorithm already handles correctly.

`consensus.powLimit` itself (the hard ceiling `CheckProofOfWork`/
`DeriveTarget` enforce against *any* accepted block, not just the
retargeting floor) was loosened from mainnet's real value to match
`BitAIActivationPowLimit`, since a ceiling stricter than the activation
target would make the easy activation block un-minable and rejected as
"high-hash." This is safe for historical (pre-225430) blocks because their
real recorded difficulty was always far stricter than even this loosened
ceiling — loosening the ceiling does not retroactively change what
historical blocks look like, it only changes what's accepted going
forward. **This value (`0x7fff...ff`, roughly 2 bits below the maximum
representable target) is a development convenience — real blocks mine in
well under a second at this difficulty — and is explicitly a `PRODUCTION_
DIFFICULTY_NOT_FINAL` placeholder, not a considered choice for any
eventual public network.**

### 3. Replay protection: fork-ID sighash folding (new)

See `docs/REPLAY_PROTECTION.md` for full detail. Summary: for every input
spent at height ≥ `BitAIActivationHeight`, the legacy and BIP143 (SegWit
v0) sighash algorithms fold a fixed nonzero `BitAIForkId` (`0x424149`,
ASCII `"BAI"`) into unused high bits of the serialized hash-type word
before hashing. This is unconditional by block height, not a
signature-embedded flag an attacker could omit — there is no downgrade
path.

### 4. Taproot-spend rejection (new, scope boundary — not a sighash fix)

Real Bitcoin's Taproot (BIP341/342) sighash algorithm uses a
domain-separated tagged hash with Schnorr signatures, architecturally
different from legacy/BIP143 ECDSA sighashing, and folding a fork-ID into
it correctly would be substantially more involved than the legacy/BIP143
case. Rather than ship a partial or incorrect Taproot replay-protection
scheme, Phase 1 makes the honest, conservative choice: **any transaction
that spends a Taproot (witness v1) output at height ≥
`BitAIActivationHeight` is rejected outright**, in `CheckInputScripts`
(`src/validation.cpp`), before signature checking is ever reached. This
is enforced at consensus level (both mempool policy and block
connection), not just as wallet/RPC policy. It closes the replay-protection
gap by elimination rather than by mitigation. A real fork-ID-aware
Taproot sighash design is left for a future phase.

Practically, this rule has no observable effect on BitAIcoin today: since
`SegwitHeight`/Taproot's BIP9 deployment are inherited dormant at
mainnet's real (very high) activation heights (see Open Question below),
no Taproot output can exist on BitAIcoin's chain yet under any current
configuration. The rejection code exists and is exercised by code review
against the same `IsPayToTaproot()` predicate used elsewhere in the
codebase, but has no live functional test for exactly this reason — see
`PHASE1_REPORT.md`'s Known Limitations.

## Open question: SegWit / Taproot activation timing on BitAIcoin

This was discovered as a practical blocker during Phase 1 wallet testing
(see `PHASE1_REPORT.md`), not decided in advance, and is recorded here
rather than silently resolved:

BitAIcoin currently inherits SegWit's and Taproot's real mainnet
activation heights (481824 and 709632-ish respectively) verbatim from
`CMainParams`. Since BitAIcoin's own chain height starts at 225430 and
grows slowly in a private lab (no 481824+ blocks will ever be mined at
that literal height under any realistic Phase-1 testing), **SegWit and
Taproot are permanently dormant on BitAIcoin as currently configured** —
not because a decision was made to disable them, but because their
inherited activation heights are unreachable in practice.

This has an immediate practical consequence: wallets must use legacy
(P2PKH) addresses for now. A `sendtoaddress` to a bech32 (P2WPKH) address
produces a witness-carrying transaction that `CheckWitnessMalleation`
correctly rejects as `"unexpected-witness"` at this chain height, and (as
discovered during testing) a wallet will keep auto-rebroadcasting such an
unconfirmed transaction on every load, re-poisoning the mempool.

This is left as an explicit open question rather than resolved here
because it is a real design decision with tradeoffs, not a bug:

- Lowering `SegwitHeight`/Taproot's deployment window to something reachable
  on BitAIcoin's own height scale would enable native SegWit/Taproot
  addresses, but reopens the Taproot replay-protection gap described in
  point 4 above (which would then need a real fix, not a rejection rule).
  It would also mean BitAIcoin's early post-fork blocks are governed by
  different soft-fork timing than the equivalent real Bitcoin heights,
  which is a bigger consensus-identity decision than Phase 1 was scoped
  to make unilaterally.
- Leaving it as-is (permanently dormant) is simplest and keeps Phase 1's
  diff minimal, at the cost of legacy-only addresses indefinitely.

No production or user-facing decision has been made either way. This
should be resolved explicitly, with the user, before any phase that
depends on native SegWit/Taproot support (e.g. most realistic L402
agent-payment designs — see `docs/AGENT_PAYMENTS.md`).
