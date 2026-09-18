BitAIcoin
=========

BitAIcoin (ticker: **BAIC**) is an independent proof-of-work altcoin,
forked from [Bitcoin Core](https://bitcoincore.org) v31.1, built as a
settlement-layer foundation for future autonomous AI-agent commerce
(L402-style machine-to-machine payments). It shares real Bitcoin ledger
history through block height 225429, then diverges onto its own
consensus rules at height 225430.

**This is Phase 1: a private "Synthetic Lab" network** — real, tested,
running on real mining and real wallet transfers, but not publicly
launched, seeded, or announced. See `docs/HISTORICAL_LINEAGE.md` for
exactly what "shares real Bitcoin history" does and does not mean, and
`PHASE1_REPORT.md` for what has actually been built and verified.

This is a source fork of Bitcoin Core; the original upstream README is
preserved at [README-BITCOIN-CORE.md](README-BITCOIN-CORE.md), and
`UPSTREAM.md` records the exact tag/commit this was forked from and every
BitAIcoin-specific commit on top of it.

## What makes BitAIcoin different from Bitcoin

| | |
|---|---|
| Shared history | Genesis through height 225429 — byte-identical to real Bitcoin mainnet |
| Diverges at | Height 225430 |
| No premine | Coinbase schedule is Bitcoin's original 50-coin/210,000-block halving schedule, unmodified |
| Replay protection | Consensus-level fork-ID sighash folding (legacy + BIP143); Taproot spends structurally rejected post-activation — see `docs/REPLAY_PROTECTION.md` |
| Network identity | Own P2P magic, ports, address prefixes, bech32 HRP — see `docs/CHAIN_IDENTITY.md` |
| Consensus deviations | Fully enumerated in `docs/CONSENSUS.md`, including open questions |

Full design rationale: `docs/ARCHITECTURE.md`.

## Documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — design overview and rationale
- [`docs/HISTORICAL_LINEAGE.md`](docs/HISTORICAL_LINEAGE.md) — exact relationship to real Bitcoin history
- [`docs/CHAIN_IDENTITY.md`](docs/CHAIN_IDENTITY.md) — ports, magic bytes, address prefixes
- [`docs/CONSENSUS.md`](docs/CONSENSUS.md) — every consensus rule that differs from Bitcoin, and every one that doesn't
- [`docs/REPLAY_PROTECTION.md`](docs/REPLAY_PROTECTION.md) — fork-ID sighash design and test vectors
- [`docs/AGENT_PAYMENTS.md`](docs/AGENT_PAYMENTS.md) — future L402 agent-payment architecture (not implemented yet)
- [`docs/TESTNET_RUNBOOK.md`](docs/TESTNET_RUNBOOK.md) — exact steps to bootstrap history and run a private multi-node network
- [`PHASE1_REPORT.md`](PHASE1_REPORT.md) — what was built, tested, and verified in Phase 1, and current known limitations
- [`UPSTREAM.md`](UPSTREAM.md) — upstream repo/tag/commit and this fork's own commit history

## Building

Same build system as upstream Bitcoin Core — see
[README-BITCOIN-CORE.md](README-BITCOIN-CORE.md) and `doc/build-*.md` for
full platform instructions. Phase 1 was built and verified with:

```bash
cmake -B build -DBUILD_GUI=OFF -DENABLE_IPC=OFF
cmake --build build -j4
ctest --test-dir build
```

## Running

Select the BitAIcoin chain with `-chain=bitaicoin`, exactly like
`-chain=test`/`-chain=signet`/`-chain=regtest`:

```bash
bitcoind -chain=bitaicoin -datadir=<your datadir> ...
bitcoin-cli -chain=bitaicoin -datadir=<your datadir> ...
```

A fresh datadir has no chain data until it's seeded with real Bitcoin
history through height 225429 — see `docs/TESTNET_RUNBOOK.md` for the
exact bootstrap procedure (there is no shortcut; a `bitaicoin`-chain node
cannot simply resync history from a `main`-chain datadir, since block-file
framing and genesis validation are chain-specific from block 0).

## License

BitAIcoin is released under the same MIT license as upstream Bitcoin
Core. See [COPYING](COPYING).
