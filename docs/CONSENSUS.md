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
| SegWit | 481824 on real mainnet; **rebased to 225430 (`BitAIActivationHeight`) on BitAIcoin** | Deliberately rebased, not inherited — see resolved decision below |
| Taproot (BIP340-342, BIP9 bit 2) | 709632+ on real mainnet; **`NEVER_ACTIVE` on BitAIcoin** | Deliberately disabled, not inherited — structurally rejected post-activation regardless, see below |
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

### 4. Historical retarget clamp (new, bugfix)

`CalculateNextWorkRequired`'s (`src/pow.cpp`) internal clamp — the step
that limits how much the raw retarget computation can loosen the target
before returning it — reads `consensus.BitAIHistoricalPowLimit` (real
Bitcoin mainnet's original, strict powLimit) instead of `consensus.powLimit`
(BitAIcoin's loosened, chain-wide ceiling) whenever the retarget being
computed applies to a height below `BitAIActivationHeight`. Every other
chain leaves `BitAIActivationHeight` at `INT_MAX`, so this branch is
unreachable there and mainnet/testnet/regtest behavior is unchanged.

This was discovered as a genuine regression, not designed in advance:
real Bitcoin's very first difficulty retarget (height 2016) computed a
raw target *looser* than mainnet's real powLimit and got clamped back
down to exactly that ceiling — which is why block 2016's recorded
`nBits` equals genesis's. Loosening `consensus.powLimit` chain-wide (see
point 5 below) removed that clamp for the *entire* chain, including for
recomputing this real historical retarget during a full revalidation —
BitAIcoin would then compute a different (looser) target than the one
actually recorded on real block 2016, and `ContextualCheckBlockHeader`
would reject it as `bad-diffbits`. This was latent since the original
`powLimit` change (never exercised until a later change — the SegWit
rebase below — forced a full historical reimport) and is now fixed by
clamping pre-activation retargets against the real, strict, original
ceiling instead. `CheckProofOfWork`/`DeriveTarget` are unaffected by this
fix and continue to use the loosened `consensus.powLimit` unconditionally,
chain-wide: that check only rejects targets *looser* than the ceiling,
and every real historical block's target is already stricter than even
mainnet's own original ceiling, so using a looser ceiling there never
incorrectly rejects genuine historical data.

### 5. Taproot-spend rejection (new, scope boundary — not a sighash fix)

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

Practically, this rule now has a genuine live functional test (not just
code review): a Taproot-shaped output was created via the wallet, mined
in, and a subsequent spend of it was signed successfully (the wallet
itself has no problem constructing a valid Schnorr-signed spend) but
rejected identically by both `testmempoolaccept` and `sendrawtransaction`
with `bitai-taproot-spend-rejected`, confirming the rule is enforced
through `CheckInputScripts` regardless of mempool-policy vs.
block-connection call site (both share the same function) — see
`PHASE1_REPORT.md`.

## Resolved: SegWit / Taproot activation timing on BitAIcoin

This was discovered as a practical blocker during Phase 1 wallet testing
(a `sendtoaddress` to a bech32 address was rejected as `"unexpected-witness"`,
since BitAIcoin originally inherited SegWit's real mainnet activation
height, 481824, which is unreachable at BitAIcoin's own height scale
within any realistic lab timeframe), recorded as an open question rather
than silently resolved, and has since been decided and implemented:

- **`consensus.SegwitHeight` is rebased from mainnet's real 481824 to
  `BitAIActivationHeight` (225430) itself** — SegWit is active from the
  very first BitAIcoin-native block onward. This was chosen over leaving
  it permanently dormant because legacy-only addresses indefinitely was a
  real, ongoing usability cost, and activating from block one (rather
  than some other arbitrary post-fork height) is the simplest rule to
  state and reason about. Verified live: `getdeploymentinfo` reports
  `segwit: active` at height 225430, and a native bech32 `sendtoaddress`
  confirms successfully.
- **Taproot's BIP9 deployment is set to `NEVER_ACTIVE`** (matching how
  the unused `TESTDUMMY` deployment is already handled), rather than left
  on mainnet's real activation window (`min_activation_height=709632`).
  Two reasons: first, at BitAIcoin's trivially-easy lab difficulty,
  reaching absolute height 709632 (~484,000 blocks past the fork point)
  is realistically achievable within a single extended testing session,
  which would let Taproot's deployment ambiguously/accidentally lock in
  without anyone deciding that; `NEVER_ACTIVE` removes that ambiguity
  outright. Second, this changes nothing about the actual security
  guarantee: `CheckInputScripts` already structurally rejects any spend
  of a Taproot-shaped output at height ≥ `BitAIActivationHeight`
  unconditionally (point 5 above), regardless of this deployment's own
  state — Taproot outputs can be created (as still-valid "future
  upgrade" witness programs, exactly as real pre-activation Bitcoin
  treats unknown witness versions) but can never be spent on BitAIcoin.
  Verified live: `getdeploymentinfo` no longer lists a `taproot` entry at
  all post-activation, and the spend-rejection test above confirms the
  practical guarantee holds.

A real fork-ID-aware Taproot sighash design (enabling actual Taproot
spends with replay protection) remains out of scope, deferred to a
future phase, per `docs/REPLAY_PROTECTION.md`.
