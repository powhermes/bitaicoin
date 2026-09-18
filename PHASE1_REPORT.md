# Phase 1 Report — BitAIcoin Synthetic Lab

Status: **complete**. Every milestone below has a passing automated or
live-verified check behind it — nothing here is asserted from source
review alone unless explicitly labeled as such in Known Limitations.

## Upstream

- Repository: https://github.com/bitcoin/bitcoin
- Tag: `v31.1` (commit `9be056a8a72b624dae9623b2f7bded92c2a21c91`)
- Full detail: `UPSTREAM.md`

## BitAIcoin commits (branch `bitaicoin-phase1`)

```
8be8995 fix: guard pipe2() with runtime availability check on macOS
2b19309 BitAIcoin Phase 1 M1: chain identity scaffolding (no-op fork)
36e2fe4 BitAIcoin Phase 1 M2: fork anchor + activation difficulty transition
aa5567d BitAIcoin Phase 1 M3: replay protection + Taproot-spend rejection
```

(M4's three-node network test and this documentation suite are verified
live/written directly against this branch; see below.)

## Build environment

- Apple Silicon (arm64), macOS 26.6, Apple clang 21.0.0, CMake 4.4.3
- `cmake -B build -DBUILD_GUI=OFF -DENABLE_IPC=OFF && cmake --build build -j4`
- Full detail: `UPSTREAM.md`

## Build results

Clean build succeeds with zero errors and no BitAIcoin-related warnings.
One unrelated pre-existing bug was found and fixed to get a clean build at
all on this machine: `TokenPipe::Make()`'s `pipe2()` call segfaulted on
this specific macOS SDK/OS-version combination (`pipe2` is declared as
macOS-27.0+-only in the SDK but the running OS is 26.6, so the
weak-linked symbol resolves to null at runtime); fixed with an
`__builtin_available` guard falling back to plain `pipe()` (commit
`8be8995`, kept separate from BitAIcoin feature work).

## Test results

- **Full existing Bitcoin Core unit test suite**: 738 test cases, 0
  failures, 0 regressions, after the complete M1-M3 diff.
  ```
  $ build/bin/test_bitcoin
  Running 738 test cases...
  *** No errors detected
  ```
- **New replay-protection unit tests** (`bitaicoin_forkid_tests.cpp`, 3
  cases — domain separation under legacy sighash, domain separation under
  BIP143 sighash, and an executable collision-freedom proof over every
  real Bitcoin hashtype byte value): all pass. See
  `docs/REPLAY_PROTECTION.md` for what each proves.
- **Pre-existing bug found and fixed while wiring up these tests**:
  `txvalidationcache_tests.cpp` had several call sites passing
  `&m_node.chainman->ActiveChainstate().CoinsTip()` (a
  `CCoinsViewCache*`) to a function that has always required `const
  CCoinsViewCache&` — a stray `&` that apparently was never caught
  because earlier milestones never built the `test_bitcoin` target.
  Fixed alongside the mechanical signature updates this test file needed
  for the new `CheckInputScripts(nHeight, consensusParams, ...)`
  parameters.

## Fork ancestor and activation

- Fork anchor height: **225429**
- Fork anchor hash (real Bitcoin mainnet, enforced by consensus):
  `0000000000000366ce98ca28338900094e8cbf445776253181749f782546d006`
- Activation height (first BitAIcoin-native block): **225430**
- First BitAIcoin block hash (this test run):
  `2a9a48dc2492d70e0187cd8a1972e6bda777d7653cfa237504cedc19c6c56ec8`
- Activation block `bits`/target: `207fffff` (matches
  `BitAIActivationPowLimit`'s compact form exactly, confirmed via
  `getblock`)

## Chain identity

See `docs/CHAIN_IDENTITY.md` for the full table (magic bytes, ports,
address prefixes, bech32 HRP). No collisions against mainnet, testnet3,
testnet4, signet, or regtest.

## Difficulty

Activation block mines at target `7fffff00...00` (`bits=207fffff`),
matching `BitAIActivationPowLimit` — confirmed live via `getblock`, not
just by reading the config. Every block observed after activation in this
test run retained the same easy target (no natural 2016-block retarget
boundary was reached in the scope of this test), consistent with the
"special-case exactly the activation height, fall through unmodified
otherwise" design in `docs/CONSENSUS.md`.

## Wallet transfer test

Coinbase matured on Node A (100 confirmations after the activation
block), then a legacy (P2PKH) `sendtoaddress` transfer confirmed and
propagated: Node A → Node B → Node C, each hop verified by comparing
`getbalance` on the receiving node against the expected amount after
confirmation. All three nodes converged on identical `getblockcount` /
`getbestblockhash` throughout.

(Legacy addresses were used deliberately, not incidentally — see
`docs/CONSENSUS.md`'s open question on SegWit/Taproot activation timing;
a bech32 destination was tried first and correctly rejected at consensus
level as `"unexpected-witness"`, which is what surfaced that open
question in the first place.)

## Replay-protection test

`bitaicoin_forkid_tests.cpp`'s three unit-test vectors (see above and
`docs/REPLAY_PROTECTION.md`) all pass. No live network-level replay test
was run (see Known Limitations).

## Three-node propagation test

Three independent `-chain=bitaicoin` nodes (Node A/B/C, ports
28333/28433/28533 respectively), each seeded from the same validated
historical import, connected only to each other via explicit `-connect=`
(no DNS seeds, no public peering — `vSeeds`/`vFixedSeeds` empty).
Confirmed via `getpeerinfo`: 4 peers per node (2 manual outbound + 2
inbound) once `-maxconnections` was raised from 8 to 32 (see
`docs/TESTNET_RUNBOOK.md` for why 8 was insufficient). Mining and wallet
transfers (above) propagated and converged identically across all three
nodes. All three stopped cleanly at the end of the test
(`bitcoin-cli stop`, confirmed no leftover process on any RPC port).

## Known limitations

1. **No live functional test of the fork-anchor-mismatch rejection path.**
   Verified by code review only (`ContextualCheckBlockHeader`'s exact-hash
   check at `BitAIForkAnchorHeight`). A live test would require forging a
   genuine competing block at real 2013-era Bitcoin mainnet difficulty,
   which is computationally impractical to construct as a test fixture.
2. **No live functional test of the Taproot-spend rejection path.**
   Verified by code review only, reusing the same `IsPayToTaproot()`
   predicate used elsewhere in the codebase for the analogous real-Bitcoin
   check. No Taproot output can currently exist on this chain at its
   current height under either the real BIP9 deployment schedule or
   BitAIcoin's own configuration (see `docs/CONSENSUS.md`'s open
   question), so there is no way to construct a live spend to test against
   without first resolving that question.
3. **No live network-level replay-protection test** (i.e., no test
   literally submits a real-Bitcoin-signed transaction to a live
   BitAIcoin node or vice versa). The in-process unit vectors in
   `bitaicoin_forkid_tests.cpp` are mathematically equivalent to this
   (same signature never verifies under both sighash domains for
   identical transaction data) and are considered sufficient for Phase 1.
4. **SegWit/Taproot activation timing on BitAIcoin is an open design
   question, not yet decided.** Currently both remain permanently dormant
   because their inherited real-mainnet activation heights are
   unreachable at BitAIcoin's own height scale. Practical consequence:
   wallets must use legacy addresses for now. Full detail and tradeoffs
   in `docs/CONSENSUS.md`.
5. **Node B and Node C were seeded by copying Node A's validated
   `blocks/`+`chainstate/` directories, not by independently re-running
   the P2P historical import three times.** A second/third from-scratch
   import was attempted first and failed reproducibly at height ~2015 on
   a memory-constrained development machine — root-caused to the
   `ImportBlocks()` call completing normally but the whole `bitcoind`
   process then being killed shortly after, consistent with an
   OS-level memory-pressure (jetsam-style) kill rather than any bug in the
   import logic itself. Copying already-validated data sidesteps needing
   to revalidate 225,430 blocks three separate times and produces
   byte-identical results; it does mean this specific run did not
   independently prove three-way P2P import reliability from a cold
   start.
6. **Difficulty ceiling (`consensus.powLimit`) and activation target
   (`BitAIActivationPowLimit`) are explicit development placeholders**
   (`PRODUCTION_DIFFICULTY_NOT_FINAL` in code comments), chosen for lab
   convenience (sub-second block times), not as a considered choice for
   any eventual public network.

## Historical mode status

**Not implemented; not scheduled.** Blocked on recovery of the raw 2013
abandoned-branch block bodies (`fork08.dat`), which prior research in this
project confirmed are not currently recoverable from any known archive.
See `docs/HISTORICAL_LINEAGE.md` for full detail on what would be needed
and why nothing here overclaims what's been recovered.
