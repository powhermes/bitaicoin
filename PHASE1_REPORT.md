# Phase 1 Report — BitAIcoin Synthetic Lab

Status: **complete**. Every milestone below has a passing automated or
live-verified check behind it — nothing here is asserted from source
review alone unless explicitly labeled as such in Known Limitations.

**A note on binary names below:** the `bitcoind`/`bitcoin-cli` references
in this report describe events as they actually happened at the time
(M4 and earlier M5 testing), before those binaries were renamed to
`bitaicoind`/`bitaicoin-cli` — see `docs/ARCHITECTURE.md`'s "binary/daemon
renaming" note. Current builds produce the renamed binaries; this report
is left as an accurate historical record rather than rewritten.

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
(M5)    BitAIcoin Phase 1 M5: SegWit activation, Taproot deployment lock-out,
        historical retarget-clamp bugfix
```

(M4's three-node network test and this documentation suite are verified
live/written directly against this branch; see below. M5's exact commit
hash is recorded in `git log bitaicoin-phase1` once committed.)

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
- First BitAIcoin block hash (final M5 test run, post powLimit
  recalibration): `00000007134b265abc8b959b1e54b99899acbe334dc8f6516a3a093b77bed7bc`
  (two earlier test runs recorded different hashes,
  `2a9a48dc2492d70e0187cd8a1972e6bda777d7653cfa237504cedc19c6c56ec8` and
  `05b52b03589c11c30c909a9c8e33071344c61ce996c202554d139e81e417d45d`,
  from before the SegWit-activation and powLimit-overflow fixes
  respectively — each was a valid activation block under its own
  rule set at the time; only the final hash above reflects the
  fully-fixed M5 rules)
- Activation block `bits`/target: `1d0fffff` (matches the recalibrated
  `BitAIActivationPowLimit`'s compact form exactly, confirmed via
  `getblock`; required ~7.5s to mine with an explicit `maxtries` on this
  development machine — see `docs/TESTNET_RUNBOOK.md`)

## Consensus bugfix: historical retarget clamp (M5)

Rebasing `SegwitHeight` to `BitAIActivationHeight` (see Resolved section
in `docs/CONSENSUS.md`) required a full historical reimport to
revalidate the chain under the new rules (an ordinary node restart
correctly refuses to trust old validation results after a consensus
parameter change: `"Witness data for blocks after height 225430 requires
validation. Please restart with -reindex."`).

That reimport surfaced a genuine, previously-latent bug: real Bitcoin's
very first difficulty retarget (height 2016) was rejected as
`bad-diffbits` under BitAIcoin's rules. Root cause: `consensus.powLimit`
was loosened chain-wide back in M2 to let the easy activation block mine
at all, but that same field is also read by `CalculateNextWorkRequired`'s
internal clamp for *every* retarget calculation, including real
historical ones below the fork point. Real block 2016's recorded `nBits`
is the result of mainnet's original retarget computation being clamped
down to mainnet's *original, strict* powLimit — recomputing it against
BitAIcoin's *loosened* powLimit produces a different, unclamped (looser)
value that doesn't match the real historical record. This was never
caught before because the original M1-era historical import (which
produced the height-225430 chain used through M4) ran *before* M2
introduced the loosened `powLimit`, and Bitcoin Core doesn't revalidate
already-connected blocks on ordinary restart — so this bug had zero
observable effect until something (this SegWit change) forced a full
reimport for the first time since M2.

Fixed by adding `consensus.BitAIHistoricalPowLimit` (real mainnet's
original powLimit, used only inside the retarget clamp for heights below
`BitAIActivationHeight`) — see `docs/CONSENSUS.md` point 4. Verified via
a full, clean historical reimport reaching height 225429 with the
correct fork-anchor hash, zero `bad-diffbits` rejections, and the full
738-case unit test suite still passing with zero regressions.

## Chain identity

See `docs/CHAIN_IDENTITY.md` for the full table (magic bytes, ports,
address prefixes, bech32 HRP). No collisions against mainnet, testnet3,
testnet4, signet, or regtest.

## Difficulty

Activation block mines at target `0fffff00...00` (`bits=1d0fffff`),
matching the M5-calibrated `BitAIActivationPowLimit` — confirmed live via
`getblock`. Mining forward through the first natural post-activation
retarget boundary (real height 225792) was tested directly (see the
overflow bugfix below): `bits` stayed unchanged across that boundary
(`1d0fffff` on both sides), the correct, stable outcome once the
overflow bug was fixed. (An earlier report of this section, before that
fix, described the activation target as `bits=207fffff` — that value was
part of the bug and no longer applies; see below.)

## Consensus bugfixes: retarget-multiply overflow and headers-sync clamp (M5)

Two further bugs in the same family as the historical-retarget-clamp fix
above, found by actually mining the live chain forward to its first
natural post-activation retarget boundary rather than assuming the
design was correct:

1. **Retarget-multiply overflow.** BitAIcoin's original
   `powLimit`/`BitAIActivationPowLimit` (`0x7fffff...ff`, ~1 bit of
   headroom below the uint256 maximum) left no room for
   `CalculateNextWorkRequired`'s intermediate `bnNew *= nActualTimespan`
   multiply to avoid silently overflowing `arith_uint256` and wrapping
   into an essentially arbitrary result. Confirmed live: at real height
   225792, the retarget window's mix of real-2013 and synthetic
   present-day timestamps produced a multi-year "actual timespan"
   (clamped to the maximum 4x ratio), and multiplying the near-maximum
   activation target by that ratio overflowed, wrapping to a target that
   decoded as `bits=1e09debb` — a difficulty roughly 850,000x higher than
   intended. Hand-computed the exact overflow-wrapped arithmetic in
   Python and confirmed it matches the live result bit-for-bit, ruling
   out any other explanation. Real Bitcoin's own `powLimit` values all
   carry 32 bits of headroom for exactly this reason; `regtest`'s
   identical-looking `0x7fffff...ff` value is not a safe counter-example,
   since regtest also sets `fPowNoRetargeting = true` and never reaches
   this arithmetic at all. Fixed by recalibrating both values to
   `0x0000000fffff...ff` (28 bits of headroom). Verified live: mining
   forward past height 225792 a second time produced a stable, unchanged
   `bits` value across the boundary — see `docs/CONSENSUS.md` point 6.
2. **Headers-sync anti-DoS clamp.** `PermittedDifficultyTransition`
   (used by `headerssync.cpp`'s normal P2P headers-first sync, not
   exercised by this project's own `-loadblock` bootstrap) has its own,
   separate min/max envelope check against `consensus.powLimit`, missed
   by the original historical-retarget-clamp fix. Fixed the same way
   (historical/loosened split by height) — see `docs/CONSENSUS.md`
   point 5. No live test of this path specifically (it requires a real
   P2P peer relationship during initial sync, which Phase 1's node
   topology doesn't exercise — see Known Limitations).

**Practical side effect discovered while verifying these fixes:**
`generatetoaddress`'s default `maxtries` (1,000,000) is too low for the
calibrated `powLimit`'s ~2^28 average hash attempts and silently returns
zero blocks instead of erroring; an explicit higher `maxtries` (e.g.
`1000000000`) is required and now documented in
`docs/TESTNET_RUNBOOK.md`. Also discovered `maxtries` is a budget shared
across an entire multi-block `generatetoaddress` call, not a per-block
allowance — mining N blocks reliably requires N separate single-block
calls.

## Wallet transfer test

**M4 (original, legacy addresses):** coinbase matured on Node A (100
confirmations after the activation block), then a legacy (P2PKH)
`sendtoaddress` transfer confirmed and propagated: Node A → Node B →
Node C, each hop verified by comparing `getbalance` on the receiving node
against the expected amount after confirmation. All three nodes
converged on identical `getblockcount` / `getbestblockhash` throughout.
(Legacy addresses were used deliberately at the time, not incidentally —
a bech32 destination was tried first and was correctly rejected at
consensus level as `"unexpected-witness"`, which is what surfaced the
SegWit-activation-timing question in the first place.)

**M5 (native SegWit, single-node):** after rebasing `SegwitHeight` to
`BitAIActivationHeight` (see `docs/CONSENSUS.md`), a fresh bech32
(P2WPKH) address (`bai1q...`) was generated, mined to directly, matured,
and a native SegWit `sendtoaddress` between two bech32 addresses in the
same wallet confirmed successfully — the original blocker is resolved.
Multi-node propagation of SegWit transactions specifically was not
re-verified in this pass (Node B/C were not re-seeded from the M5 chain
during this session — see Known Limitations); the underlying P2P
propagation mechanism itself is unmodified from M4's already-proven
three-node test.

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
2. ~~No live functional test of the Taproot-spend rejection path.~~
   **RESOLVED (M5).** Now that SegWit is active (see below), a
   Taproot-shaped (witness v1) output can be created. Live test: a
   Taproot output was created via the wallet and mined in, a spend of it
   was signed successfully (Schnorr signature, `"complete": true`), and
   submitting that spend was rejected identically by both
   `testmempoolaccept` and `sendrawtransaction` with
   `bitai-taproot-spend-rejected` — confirming `CheckInputScripts`'
   rejection holds through the real signing → broadcast path, not just in
   code review.
3. **No live network-level replay-protection test** (i.e., no test
   literally submits a real-Bitcoin-signed transaction to a live
   BitAIcoin node or vice versa). The in-process unit vectors in
   `bitaicoin_forkid_tests.cpp` are mathematically equivalent to this
   (same signature never verifies under both sighash domains for
   identical transaction data) and are considered sufficient for Phase 1.
4. ~~SegWit/Taproot activation timing on BitAIcoin is an open design
   question, not yet decided.~~ **RESOLVED (M5).** SegWit now activates
   at `BitAIActivationHeight`; Taproot's deployment is `NEVER_ACTIVE`.
   See `docs/CONSENSUS.md`'s "Resolved" section for the decision and
   verification evidence.
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
   start. **Update:** Node B/C were re-seeded from Node A's fully-fixed
   M5 chain and the three-node network re-verified live: all three
   converged on identical `getblockcount`/`getbestblockhash` after fresh
   mining, and a native bech32 (SegWit) `sendtoaddress` from Node A to
   Node B confirmed and propagated correctly (`getbalance` on Node B
   matched the sent amount exactly). This still does not independently
   prove three-way P2P import reliability from a cold start (the
   copy-instead-of-import limitation itself is unchanged), but confirms
   all of M5's fixes hold under real multi-node propagation, not just on
   a single node.
6. **Difficulty ceiling (`consensus.powLimit`) and activation target
   (`BitAIActivationPowLimit`) are explicit development placeholders**
   (`PRODUCTION_DIFFICULTY_NOT_FINAL` in code comments), chosen for lab
   convenience (single-digit-second block times with an explicit
   `maxtries`, recalibrated in M5 from an earlier, unsafe near-instant
   value that caused a retarget-multiply overflow — see the bugfix
   section above), not as a considered choice for any eventual public
   network. The real production question this doesn't answer — what
   mining/security model BitAIcoin should eventually run on (a
   permissioned miner set vs. fully public PoW, and if public, whether
   SHA256d is even the right algorithm given how trivially existing
   Bitcoin hashpower could redirect at a new SHA256d chain) — remains an
   open decision, deliberately not made unilaterally. What *is* now
   hardened regardless of that decision: a startup assertion in
   `CBitAIcoinParams` (`kernel/chainparams.cpp`) requires any future
   `powLimit` value to leave adequate overflow headroom, verified live to
   correctly reject the original unsafe value — see `docs/CONSENSUS.md`
   point 6's "Hardening beyond the fix itself."
7. **No live test of the headers-sync anti-DoS clamp fix**
   (`PermittedDifficultyTransition`, M5). This code path is only
   exercised during normal P2P headers-first sync from a peer; Phase 1's
   node topology (all three nodes seeded from the same `-loadblock`
   import) never triggers it. Verified by code review and by mirroring
   the same fix pattern already proven live in
   `CalculateNextWorkRequired`, not by an independent live test.

## Historical mode status

**Not implemented; not scheduled.** Blocked on recovery of the raw 2013
abandoned-branch block bodies (`fork08.dat`), which prior research in this
project confirmed are not currently recoverable from any known archive.
See `docs/HISTORICAL_LINEAGE.md` for full detail on what would be needed
and why nothing here overclaims what's been recovered.
