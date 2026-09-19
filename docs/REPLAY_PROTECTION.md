# Replay Protection

Because BitAIcoin shares real Bitcoin history through height 225429, a
transaction signed for BitAIcoin could in principle also be valid on real
Bitcoin (or vice versa) unless something specifically prevents it — a
"replay attack." This document specifies BitAIcoin's replay-protection
design, why it was built this way, and how to verify it.

## Design: fork-ID sighash domain separation

This is the same family of technique used by other Bitcoin-history-sharing
forks (e.g. Bitcoin Cash's `SIGHASH_FORKID`): fold a fixed,
chain-specific, nonzero constant into bits of the signature hash that real
Bitcoin never uses, so that a valid BitAIcoin signature hash can never
equal a valid Bitcoin signature hash for the same transaction, inputs, and
keys.

### Why this is collision-free by construction, not by luck

Bitcoin's serialized signature hash-type (`nHashType`, aka `SIGHASH_ALL` /
`SIGHASH_NONE` / `SIGHASH_SINGLE`, optionally OR'd with
`SIGHASH_ANYONECANPAY`) is transmitted as a **single byte** appended to
every signature (`vchSig.back()`), and is deserialized into a wider
`int32_t nHashType` that has bits 8-31 always zero for every real
signature that has ever existed or ever can exist on Bitcoin, because
nothing in Bitcoin's protocol can produce a hash type outside `[0, 255]`.

`ApplyForkId` (`src/script/interpreter.h`):

```cpp
constexpr int32_t ApplyForkId(int32_t nHashType, uint32_t fork_id)
{
    return (nHashType & 0xff) | (fork_id << 8);
}
```

With `fork_id = BitAIForkId = 0x424149` (ASCII `"BAI"`), this sets bits
8-31 to a fixed nonzero pattern. Since real Bitcoin signatures can never
have those bits set, **every post-activation BitAIcoin sighash preimage is
guaranteed to differ from every possible real-Bitcoin sighash preimage**
for the same transaction data — not because a hash happens not to collide,
but because the set of possible values is disjoint by construction.

### Where it's applied

`ApplyForkId(nHashType, fork_id)` is called at exactly the two places in
`SignatureHash()` (`src/script/interpreter.cpp`) that serialize the hash
type into the preimage before hashing — one on the cache-hit fast path,
one on the cache-miss/store path — for **both** sighash algorithms
BitAIcoin currently supports signing under:

- **Legacy** (pre-SegWit) sighash
- **BIP143** (SegWit v0) sighash

The internal SIGHASH_SINGLE / SIGHASH_ANYONECANPAY flag-masking logic
elsewhere in `SignatureHash()` is deliberately untouched — it must
continue operating on the raw, un-folded `nHashType` to correctly decide
which inputs/outputs to include in the preimage; only the byte that gets
serialized into the hash itself is transformed.

**Taproot (BIP341/342) is explicitly out of scope** — see
`docs/CONSENSUS.md` point 7. Rather than build an incorrect or partial
Schnorr-sighash fork-ID scheme, BitAIcoin structurally rejects any
transaction spending a Taproot output post-activation, closing the gap by
elimination.

### Unconditional by height, not by signature content

`fork_id` is resolved once per validation/signing context from chain
height vs. `BitAIActivationHeight` — never read from the transaction or
signature itself:

- **Validation** (`CheckInputScripts`, `src/validation.cpp`): computed
  once per call from the block/mempool-entry height, threaded into every
  `CScriptCheck` and into the script-execution cache key (so a
  fork-id=0 and fork-id=BAI check of the identical script never share a
  cache hit).
- **Wallet signing** (`CWallet::SignTransaction`, `CWallet::FillPSBT`,
  `src/wallet/wallet.cpp`): both resolve fork_id via the shared
  `CWallet::ResolveForkId()` helper, from `m_last_block_processed_height +
  1` (0 if the wallet has never been attached to any chain).
- **RPC signing** (`signrawtransactionwithkey` and `descriptorprocesspsbt`'s
  `ProcessPSBT`, `src/rpc/rawtransaction.cpp`): computed from the active
  chain's height + 1.

Because it's derived from consensus-visible height rather than a bit an
attacker controls, there is no way to construct a transaction that
"opts out" of replay protection once past the activation height — this
is a hard behavioral guarantee, not a default.

## Test vectors

`src/test/bitaicoin_forkid_tests.cpp` (Boost unit tests, part of
`test_bitcoin`):

1. **`forkid_domain_separation_legacy`** — builds a P2PK spend, computes
   the legacy sighash both with `fork_id=0` and `fork_id=BAI`, signs each,
   and proves: (a) the two sighashes differ; (b) each signature verifies
   under its own sighash; (c) a BitAIcoin-domain signature does **not**
   verify under the plain-Bitcoin sighash for the identical
   transaction/inputs/outputs/keys, and vice versa.
2. **`forkid_domain_separation_bip143`** — the same four checks, using a
   P2WPKH-style scriptCode and `SigVersion::WITNESS_V0` (BIP143) instead
   of legacy.
3. **`forkid_collision_freedom`** — an executable proof, not just an
   assertion in prose: for every real Bitcoin hashtype byte value
   (`SIGHASH_ALL`/`NONE`/`SINGLE`, each with and without
   `SIGHASH_ANYONECANPAY`) crossed with several fork-ID values (`0`,
   `BitAIForkId`, and an arbitrary other nonzero value), no two distinct
   `(hashtype, fork_id)` pairs ever produce the same `ApplyForkId` output,
   and `fork_id=0` is confirmed to be a true no-op (reproduces the
   original hashtype exactly), so mainnet/testnet/regtest behavior is
   provably unchanged.

Run with:

```
ctest --test-dir build -R bitaicoin_forkid_tests
# or, if built without ctest wiring:
build/bin/test_bitcoin --run_test=bitaicoin_forkid_tests
```

All three cases pass as part of the full `test_bitcoin` suite (see
`PHASE1_REPORT.md` for the full-suite run).

## Fixed defect: the PSBT signing path never threaded fork_id (found during BitAI Payment implementation)

While implementing BitAI Payment's live deposit test (see
`docs/BITAI_PAYMENT_DESIGN.md` §8), a real BAIC_TEST transaction built via
the standard `walletcreatefundedpsbt` → `walletprocesspsbt` →
`finalizepsbt` sequence was consistently rejected at broadcast with
`mempool-script-verify-flag-failed (Signature must be zero for failed
CHECK(MULTI)SIG operation)`, even for a plain, freshly-verified P2WPKH
input (confirmed via `gettxout`) — while a plain `sendtoaddress` from the
same wallet, spending the same UTXO pool, broadcast successfully.

Root cause: `SignPSBTInput` (`src/psbt.cpp`) built its
`MutableTransactionSignatureCreator` without a `fork_id` argument,
defaulting to 0 — every PSBT-signed input was signed against plain
Bitcoin's sighash, never BitAIcoin's fork-ID-folded one. This gap existed
only on the PSBT path: `CWallet::SignTransaction` (used by
`sendtoaddress`) already resolved and threaded `fork_id` correctly, but
nothing threaded it into `CWallet::FillPSBT` → `DescriptorScriptPubKeyMan::FillPSBT`
→ `SignPSBTInput`. A second, related instance of the same class of bug was
in `PSBTInputSignedAndVerified` (used both by `SignPSBTInput`'s own
early-exit check and by `CWallet::FillPSBT`'s post-signing completeness
computation): it built its `MutableTransactionSignatureChecker` without
`fork_id` too, so even after the first bug was fixed, `walletprocesspsbt`
still reported `complete: false` for a correctly-signed, broadcastable
transaction, because it was re-verifying the signature against the wrong
sighash.

Fix: threaded an explicit `fork_id` parameter (default `0`, so every
non-BitAIcoin chain and every caller that only assembles already-present
signatures via a keyless dummy provider is unaffected) through
`SignPSBTInput` and `PSBTInputSignedAndVerified`
(`src/psbt.h`/`src/psbt.cpp`), through the `ScriptPubKeyMan`/`DescriptorScriptPubKeyMan`/
`ExternalSignerScriptPubKeyMan::FillPSBT` chain, and resolved it in
`CWallet::FillPSBT` via a new shared `CWallet::ResolveForkId()` helper
(also now used by `CWallet::SignTransaction`, replacing its own inline
copy of the same logic) and in `ProcessPSBT`
(`descriptorprocesspsbt`'s RPC handler) from the active chain height.

`CWallet::ResolveForkId()` exists specifically because the first draft of
this fix called `GetLastBlockHeight()` directly in `FillPSBT`, which
asserts if the wallet has never been attached to any chain
(`m_last_block_processed_height == -1`) — true of `WalletTestingSetup`'s
default fixture, and caught by `psbt_wallet_tests` failing after the
change. `ResolveForkId()` checks `m_last_block_processed_height` directly
and conservatively resolves to `fork_id=0` when it's unknown, rather than
crashing.

Regression test: `psbt_signing_and_verification_use_fork_id` in
`src/test/bitaicoin_forkid_tests.cpp` signs a P2WPKH PSBT input with an
explicit nonzero `fork_id` and asserts `PSBTInputSignedAndVerified`
agrees only when given that same `fork_id`, disagreeing at `fork_id=0` —
directly exercising the mismatch that let this hide (the two halves of
the bug independently defaulting to 0 made them consistent with each
other, just consistently wrong once a real broadcast checked the result
against the correct, height-derived `fork_id`).

`AnalyzePSBT` (`src/node/psbt.cpp`, backing the `analyzepsbt` RPC's
diagnostic "what's missing" output) still calls `PSBTInputSignedAndVerified`
without a `fork_id` — it has no chain-height context available in its
current call signature. This is a known, narrow, diagnostic-only
inaccuracy (it may report a correctly-signed post-activation input as
not-yet-final) and does not affect signing, broadcast, or the completeness
field `walletprocesspsbt` and `finalizepsbt` actually rely on.

## Known limitation

There is no live network-level replay-protection functional test beyond
the unit-test vectors above (i.e., no test literally submits a
Bitcoin-mainnet-signed transaction to a live BitAIcoin node and confirms
rejection, or vice versa) — doing so meaningfully would require a real,
separately-signed Bitcoin-mainnet-valid transaction as a fixture, which
is out of scope for an in-repo unit test. The unit vectors above are
mathematically equivalent to that test (they prove the same signature
never verifies under both domains for identical inputs), and are
considered sufficient verification for Phase 1.
