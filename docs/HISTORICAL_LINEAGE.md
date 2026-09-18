# Historical Lineage

This document states, plainly and without overclaiming, exactly how
BitAIcoin relates to real Bitcoin history. It exists so nobody — including
future maintainers of this project — mistakes what was actually
accomplished for something it is not.

## What is true

- BitAIcoin's ledger from genesis (block 0, 2009-01-03) through height
  **225429** is **byte-for-byte real Bitcoin mainnet history**, imported
  from an independent sync against the real public Bitcoin P2P network
  (see `docs/TESTNET_RUNBOOK.md` for the exact bootstrap procedure).
- Block 225429's hash is hardcoded into `CBitAIcoinParams` as
  `BitAIForkAnchorHash` and enforced by consensus
  (`ContextualCheckBlockHeader` rejects any competing block at that
  height) — a BitAIcoin node cannot be made to accept a different history
  below the fork point.
- Height 225429 was chosen because it is the block immediately preceding
  the real-world March 2013 Bitcoin v0.7/v0.8 BDB-lock-limit consensus
  split (BIP50): at height 225430, the network split into two competing
  chains for several hours before the v0.8 branch was abandoned by
  developer/miner consensus and the v0.7-compatible branch became the
  permanent, canonical Bitcoin history that all present-day nodes,
  including this project's own reference against the live network, agree
  on.
- That abandoned v0.8 branch (roughly 24 orphaned blocks starting at
  225430) was the subject of prior research in this project: its headers
  were recovered and independently verified as valid proof-of-work under
  current Bitcoin Core consensus rules, sourced from public archives
  (bitcoin-data/stale-blocks and equivalent). Its **raw block bodies**
  (full transaction data, referred to internally as `fork08.dat`) were
  **not recovered** — they were never widely archived at the time (the
  branch was economically worthless within hours of the split) and are
  presumed lost. This matches the experience of the public archaeology
  projects that have tried the same recovery.

## What is NOT true, and why this document says so explicitly

- **BitAIcoin does not "continue" or "resurrect" the abandoned 2013
  branch.** Continuing it would require the actual orphaned block bodies
  (transactions, merkle roots that check out, coinbase claims, etc.),
  which do not exist in any recoverable form. Claiming otherwise would be
  a factual misrepresentation of what this project has and has not
  recovered.
- **Block 225430 onward on BitAIcoin is synthetic**, not historical. It is
  freshly mined under BitAIcoin's own consensus rules (see
  `docs/CONSENSUS.md`), using the real block 225429 purely as its parent.
  This mode is called **Synthetic Lab mode** throughout this project's
  documentation, and it is the *only* mode implemented in Phase 1.

## Historical mode (deferred, not implemented)

The architecture leaves room for a future **Historical mode**, in which
the synthetic block(s) at/after 225430 would instead be replaced by the
genuine recovered 2013 orphan-branch blocks, if their raw bodies are ever
recovered from some currently-unknown archive. Concretely, this would mean:

- `BitAIActivationHeight` would be raised to cover however many real
  historical orphan blocks are recovered (each validated under the exact
  historical difficulty and rules those blocks were originally subject
  to, since they precede BitAIcoin's own consensus changes).
- The synthetic activation-height difficulty transition
  (`BitAIActivationPowLimit`) would move to apply only after the last
  genuinely recovered historical block, not at 225430.
- No other architectural change is needed — the fork-anchor/activation
  mechanism was deliberately built height-parameterized for exactly this
  reason.

**Status: PENDING RAW HISTORICAL BLOCK RECOVERY.** This is not scheduled
work; it is blocked on an artifact that does not currently exist. Nothing
in Phase 1 depends on it, and nothing in Phase 1 pretends it has already
happened.

## Summary table

| Height range | Content | Status |
|---|---|---|
| 0 – 225429 | Real Bitcoin mainnet history | Recovered and verified; enforced by consensus |
| 225430+ (Phase 1) | Synthetic BitAIcoin-native blocks | Implemented (Synthetic Lab mode) |
| 225430+ (future, optional) | Genuine recovered 2013 orphan-branch blocks | Not implemented; blocked on unrecovered `fork08.dat` |
