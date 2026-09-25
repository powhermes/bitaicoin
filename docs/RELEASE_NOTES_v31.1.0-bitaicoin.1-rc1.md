# BitAIcoin — first AuxPoW/ASERT activation release (DRAFT)

**Base:** Bitcoin Core v31.1.0 · **Release candidate HEAD:** `46a5c65cea33691d030e22c24d3457055f85f6ef`
**Proposed tag:** `v31.1.0-bitaicoin.1-rc1` (pre-release / release candidate)

> This is the first fully rehearsed BitAIcoin consensus-activation release. It is a **release
> candidate**: install on production-capable nodes and validate per the upgrade runbook, but do not
> treat it as the final production tag until the RC is confirmed in the field.

## Consensus activation

- **Production activation height:** 227808
- **AuxPoW chain ID:** 16969 (`0x4249`)
- **Direct SHA256d mining:** remains valid after activation
- **AuxPoW merge mining:** also becomes valid at activation (both proof types permanently valid)
- **ASERT:** activates at 227808, **6-hour (21600 s) half-life**
- **BIP34Height:** 227931

## Important operator requirement

- **All production-capable nodes must upgrade before the real chain reaches height 227808.**
- Obsolete (pre-activation) binaries demonstrably diverge from and reject the upgraded chain at
  activation. A mixed old/new fleet must not be allowed to cross 227808.

## Recovery fix

- A full `-reindex` AuxPoW disk-deserialization defect (the chain was silently truncated at the first
  AuxPoW block) was discovered during rehearsal and fixed in commit
  `7942285e1175dacb54958b7cde1e4fa19630fde0`. This fix is included in the release candidate.

## Testing

- Activation rehearsal complete (227808 activation, 227931 BIP34 boundary).
- C++ unit suite: **811/811** run pass.
- AuxPoW unit suite (`auxpow_tests`): **52/52**.
- Eight focused release gates pass (createauxblock IBD, auxpow RPC, disabled-chain AuxPoW, prune,
  auxpow reindex, stock reindex, mining_basic, feature_block).
- Full functional suite: **289 tests, zero failures at `-j2`** on a clean Apple Silicon environment.

## No premine / chain history

- Inherits Bitcoin history through height 225429 only.
- The native BitAIcoin chain continues afterward.
- **No production checkpoint or minimum-chainwork value is derived from rehearsal data.**

## Release identity note

This first BitAIcoin RC retains the underlying Bitcoin Core 31.1.0 client/build identity in some
binary/version output (e.g. `bitaicoind -version` still reports `v31.1.0` / "Bitcoin Core"). **Consensus
and BitAIcoin chain identity are unaffected.** BitAIcoin-specific client/version branding may be
normalized in a later non-consensus release-engineering change, which would be separately built and
revalidated through its own release-quality gate. The upstream branding is **not** BitAIcoin's intended
permanent identity — it is simply left untouched here to keep this validated candidate frozen.

## Scope — what is NOT in this release

- A merge-mining **reference coordinator** exists.
- The **Bitcoin Core GBT parent-work adapter is NOT** part of this release.
- **Stratum / pool / accounting / payout** infrastructure is NOT part of this release.

## Validated development RC binary hashes (clean Apple Silicon build)

- `bitaicoind`:    `cba16419790ff2ed94ca83af0529a48838a085e3f16f615c2a45056e7de5cc85`
- `bitaicoin-cli`: `9f1024cc5371ad4ad9d476850994db76913079bc0090b5554d6180a7bf542c02`

These hashes describe the clean Apple Silicon validation build only. If official release binaries are
later rebuilt or reproducibly packaged, record **their** hashes separately — the hashes above do not
apply to future packaging builds.
