# Upstream

BitAIcoin is a source fork of Bitcoin Core. This file records exactly where
it started and how the fork is organized, per the project's own requirement
to never obscure that provenance.

## Upstream repository

- Repository: https://github.com/bitcoin/bitcoin
- Tag: `v31.1`
- Tag commit (dereferenced): `9be056a8a72b624dae9623b2f7bded92c2a21c91`
- Tag date: 2026-07-06

## Local fork

- Branch: `bitaicoin-phase1`, based directly on the tag commit above with no
  intervening rebases or history rewrites.
- Commits (oldest first):
  1. `8be8995` — `fix: guard pipe2() with runtime availability check on macOS`
     — a local build-environment portability fix, unrelated to BitAIcoin.
     Stock v31.1 segfaults on start on a macOS SDK/OS-version combination
     where `pipe2()` is declared but not actually linked at runtime; guarded
     with `__builtin_available`.
  2. `2b19309` — Phase 1 M1: chain identity scaffolding (`ChainType::BITAICOIN`,
     `CBitAIcoinParams`, network/address identity). Deliberately a no-op fork
     below the activation height.
  3. `36e2fe4` — Phase 1 M2: fork-anchor enforcement and the one-time
     activation-height difficulty transition.
  4. `aa5567d` — Phase 1 M3: replay protection (fork-ID sighash domain
     separation) and Taproot-spend rejection.

Each is its own milestone-sized commit with its own verification recorded in
its commit message — see `git log bitaicoin-phase1` for full detail, and
`PHASE1_REPORT.md` for the consolidated results.

## Build environment this was developed and verified against

- Machine: Apple Silicon (arm64), macOS 26.6 (Darwin 25.6.0)
- Compiler: Apple clang 21.0.0 (clang-2100.3.34.2)
- Build system: CMake 4.4.3, Ninja-less default generator, `-j4`
- Dependencies: Homebrew `boost`, `libevent`, `sqlite`, `pkgconf`
- Multiprocess/IPC (Cap'n Proto) and GUI (Qt) were not available/requested
  and are disabled for Phase 1 (`-DENABLE_IPC=OFF -DBUILD_GUI=OFF`); neither
  is required by anything in this phase.

## License

Bitcoin Core's original MIT license (`COPYING`) is preserved unmodified.
BitAIcoin-specific additions are licensed the same way; no copyright
notices have been removed from files this fork modifies.

## What "fork" means here

BitAIcoin is a *source* fork (a modified build of the Bitcoin Core
software) that also happens to *share ledger history* with real Bitcoin
through a specific historical block (see `docs/HISTORICAL_LINEAGE.md`).
These are two different kinds of "fork" and neither implies the other:
plenty of Bitcoin Core source forks (e.g. alternative implementations)
share no history with Bitcoin at all, and plenty of history-sharing forks
are maintained as patches against a different codebase. BitAIcoin does
both deliberately: same code lineage, same ledger up to the documented
divergence point.
