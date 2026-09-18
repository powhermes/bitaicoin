# Architecture

This document is the technical design overview of BitAIcoin Phase 1: what
was built, why it's structured this way, and how the pieces fit together.
For the exact list of consensus differences see `docs/CONSENSUS.md`; for
network/address constants see `docs/CHAIN_IDENTITY.md`; for the
replay-protection mechanism in detail see `docs/REPLAY_PROTECTION.md`; for
the real-history relationship see `docs/HISTORICAL_LINEAGE.md`.

## Goal

Produce a correct, tested, privately-run PoW altcoin ("Synthetic Lab"
mode) that:

1. Shares real Bitcoin ledger history through height 225429.
2. Diverges onto independent BitAIcoin-native consensus rules at height
   225430, with no ability to be reorganized back onto a competing
   history below that point.
3. Has genuine consensus-level replay protection, not cosmetic
   network/prefix changes alone.
4. Has zero premine — the only coins that exist are the ones the original
   Bitcoin block-reward schedule would have produced.
5. Never touches or depends on the user's existing real Bitcoin Core
   installation.
6. Runs as a real, working multi-node network with real mining and real
   wallet transfers, verified live rather than asserted from source
   reading alone.

## High-level design

BitAIcoin is implemented as a new `ChainType` inside an otherwise-stock
Bitcoin Core v31.1 tree (see `UPSTREAM.md`), selected via `-chain=bitaicoin`
exactly like `-chain=test`/`-chain=signet`/`-chain=regtest`. This was
chosen over three alternatives considered and rejected:

- **Reusing signet**: signet's genesis block is fixed by upstream Bitcoin
  Core and cannot be made to carry 225,430 blocks of real history.
- **Reusing regtest**: universally treated as ephemeral/throwaway
  elsewhere in the codebase (instant-mine, no real difficulty), which
  fails the "persistent, collision-free chain identity" requirement.
- **Hacking `ChainType::MAIN` in place**: would silently inherit every
  "this is real Bitcoin mainnet" fast-path or assumption elsewhere in the
  codebase (assumed-valid checkpoints, minimum chainwork thresholds,
  hardcoded seed nodes, etc.) rather than making BitAIcoin's identity
  explicit and auditable.

A genuine new enum value touches a small number of *exhaustive* compiler-
enforced switches (`chainparamsbase.cpp`, `chainparams.cpp`,
`bitcoin-cli.cpp`) that cannot ship with a silently-unhandled case, plus a
few non-exhaustive `if`-chains reviewed by hand (GUI code, out of scope
since Phase 1 builds with `BUILD_GUI=OFF`).

## The fork point: a height field, not a soft fork

The pre/post-activation split is modeled as new plain height fields on
`Consensus::Params` (`BitAIForkAnchorHeight`/`Hash`, `BitAIActivationHeight`,
`BitAIActivationPowLimit`, `BitAIForkId`) rather than a BIP9 versionbits
deployment, because this is a **hard fork by design**, not a soft,
miner-signaled, opt-in change — there is no scenario where BitAIcoin
"doesn't activate." All of these fields default to inert values
(`INT_MAX` height, zero hash/fork-id) on every other chain
(main/test/testnet4/signet/regtest), which is what makes it possible to
assert real Bitcoin mainnet/testnet/regtest behavior is provably
unchanged — the new fields are simply never read on those chains.

Three consensus-level mechanisms key off these fields:

1. **Fork-anchor enforcement** (`ContextualCheckBlockHeader`) — see
   `docs/CONSENSUS.md` §1.
2. **Activation-height difficulty transition** (`GetNextWorkRequired`) —
   see `docs/CONSENSUS.md` §2.
3. **Replay protection + Taproot rejection** (`CheckInputScripts` and the
   sighash algorithms) — see `docs/REPLAY_PROTECTION.md` and
   `docs/CONSENSUS.md` §3-4.

## Historical bootstrap mechanism

You cannot start a node as `-chain=main`, sync to 225429, and relaunch the
*same datadir* as `-chain=bitaicoin` — block-file magic framing
(`pchMessageStart`) and the hardcoded genesis-hash assertion are fixed per
chain-type from block 0, and a `-chain=bitaicoin` node run against a
`-chain=main`-framed datadir simply won't recognize it. The bootstrap
instead works as an explicit, one-time import:

1. **Independent scratch sync**: an unmodified `bitcoind -chain=main`
   instance, in its own throwaway datadir, syncs the real public Bitcoin
   P2P network up to `-stopatheight=225429`. This is completely separate
   from, and never touches, the user's existing (mid-IBD, external SSD)
   Bitcoin Core installation.
2. **Linearize + re-magic**: `contrib/linearize/` (stock, already in the
   v31.1 tree) reassembles the scratch chain's block files into one
   linear file in height order, then a small purpose-written script
   (`remagic.py`, not part of stock tooling) rewrites each
   length-prefixed record's 4-byte magic from Bitcoin's `f9beb4d9` to
   BitAIcoin's `b778d811` — without touching any payload bytes. This step
   is load-bearing: a `-loadblock` import under `-chain=bitaicoin` rejects
   any file still framed with Bitcoin's magic.
3. **Fresh BitAIcoin datadir import**: `bitcoind -chain=bitaicoin
   -loadblock=<remagicked file>` validates the entire 225,430-block file
   cleanly, because every rule governing those heights under
   `CBitAIcoinParams` is byte-identical to mainnet's (same genesis, same
   dormant-but-defined BIP34/65/66/CSV/SegWit heights, same subsidy
   schedule) — see `docs/CONSENSUS.md`'s "Inherited unmodified" table.
4. **Verification before mining anything**: `getblockhash 225429` must
   equal the hardcoded `BitAIForkAnchorHash`, and `getblockcount` must
   report `225430`.
5. From that point on, the same datadir continues natively under ordinary
   BitAIcoin P2P/mining/wallet operation — there is no further identity
   switch, ever.

See `docs/TESTNET_RUNBOOK.md` for the literal commands.

## Node/network topology (Phase 1)

Three independent `bitcoind -chain=bitaicoin` nodes (Node A/B/C), each
with a datadir seeded from the same bootstrap import (B and C were seeded
by directly copying Node A's already-validated `blocks/`+`chainstate/`
directories rather than re-running the P2P import a second and third
time — see `PHASE1_REPORT.md`'s Known Limitations for why). Nodes connect
to each other only via explicit `-connect=`, with no DNS seeds and no
public-network peering (`vSeeds`/`vFixedSeeds` are empty in
`CBitAIcoinParams` — see `docs/CHAIN_IDENTITY.md`). Verified: block
propagation and coinbase-funded wallet transfers converge identically
across all three nodes (see `PHASE1_REPORT.md`).

## Wallet/RPC integration

Replay protection required threading a `fork_id` value through the
signing path without changing its public shape for callers that don't
need to know about it:

- `CWallet::SignTransaction` resolves `fork_id` internally from
  `Params().GetConsensus()` and the wallet's last-seen block height + 1 —
  callers like `CreateTransaction` and the fee bumper need zero changes.
- `signrawtransactionwithkey` resolves it the same way from the active
  chainstate height.
- The resolved value flows through
  `ScriptPubKeyMan::SignTransaction` → free `SignTransaction()` →
  `MutableTransactionSignatureCreator` → `SignatureHash()`.

This keeps the fork-id computation in exactly two places (wallet, RPC)
rather than threading a new parameter through every intermediate call
site that doesn't need to make a decision about it.

## What Phase 1 deliberately does not include

Explicitly deferred, not forgotten (per the original project directive
and this plan's scope boundary):

- Binary/daemon renaming — `bitcoind`/`bitcoin-cli` are unchanged names,
  selected via `-chain=bitaicoin`.
- GUI/Qt branding (Phase 1 builds with `BUILD_GUI=OFF`).
- BIP44 HD wallet coin-type registration.
- Taproot-domain-separated signing (structurally rejected instead, see
  `docs/CONSENSUS.md` §4).
- Any L402/agent-payment implementation (architecture notes only, see
  `docs/AGENT_PAYMENTS.md`).
- Any public launch, seed node, or external network exposure of any kind.
- Historical mode (splicing in genuinely recovered 2013 orphan-branch
  blocks) — blocked on an unrecovered artifact, see
  `docs/HISTORICAL_LINEAGE.md`.
