# BitAIcoin activation rehearsal: crossing 227808 (AuxPoW/ASERT) and 227931 (BIP34)

**Status: IN PROGRESS.** This document is being built incrementally as the rehearsal proceeds.
Sections marked `PENDING` have not happened yet and contain no fabricated data -- per the governing
instruction, no result is recorded before it actually exists. As of this revision: the real
225823->227807 canonical mining run, the 227807 snapshot, Scenario A/B at the real activation
boundary (sec.6-12), and the **full upgraded-node convergence/reorg/branch-local-ASERT phase --
three-node mixed direct/AuxPoW convergence, a same-anchor activation-crossing reorg, a
reverse-composition reorg, a branch-local ASERT anchor test with an anchor-leakage check and a
wrong-bits subtest, and independent chainwork verification (sec.14-21)** -- are all complete, real,
and PASS. Every node used in this phase is stopped; the preserved evidence from earlier phases
(canonical node A, both golden snapshots, both scenario datadirs) was never reused or mutated. Per
instruction, this phase stops here -- BIP34 height 227931, full fresh-node HEADERS sync, pruning, and
stabilization-checkpoint selection (sec.22) remain deliberately not started, pending review of this
report.

## 0. Purpose

Prove that the complete, frozen BitAIcoin consensus + AuxPoW + ASERT + merge-mining RPC package
behaves correctly as a real network at the real production activation heights (227808 for
AuxPoW/ASERT, 227931 for BIP34), using real mining, real validation, real restarts, real headers
sync, and real obsolete-node behavior -- never by manufacturing block-index state, editing heights,
or substituting easier consensus parameters. This is a rehearsal against an **isolated clone** of the
real private chain state; the actual real/private BitAIcoin node and its datadir were never started,
modified, or connected to during this rehearsal.

## 1. Code baseline (frozen before this rehearsal began)

Repository: `powhermes/bitaicoin`, branch `bitaicoin-phase1`.

Two commits, pushed to `origin/bitaicoin-phase1` before any rehearsal mining began:

| commit | subject |
|---|---|
| `d077241af36023f1c6241faf93d6ea7defb81d41` | AuxPoW: createauxblock/submitauxblock RPC milestone + IBD/cooldown fix |
| `2e700d7ad6fe08c840d2e9b4e99ef5e89a2b34f6` | Merge-mining: reference coordinator + pool integration guide |

`git diff --stat HEAD` at rehearsal start: empty (working tree exactly matches
`2e700d7ad6fe08c840d2e9b4e99ef5e89a2b34f6`).

**Upgraded binary** (used for nodes A/B/C): rebuilt from this exact HEAD.
- `bitaicoind` SHA-256: `c7a5c8f5627b3d33d069fc90d760957685f4a0b6dc506c0d38ecc761fe3da93e`
- Size: 15,725,848 bytes. Built: 2026-09-24T03:28 (local).

**Obsolete binary D**: `~/Downloads/bitaicoin-dev/pre-auxpow-binaries-mac/{bitaicoind,bitaicoin-cli}`,
`v31.1.0`. Verified genuinely pre-AuxPoW by real behavior, not by inspection: recognizes
`-chain=bitaicoin` (the chain TYPE predates the AuxPoW work) but `createauxblock` returns
`"unknown command"` -- the entire AuxPoW RPC milestone (and, by construction, everything it depends
on: `fBitAIAuxpowEnabled`, `CheckAuxPowRules`, ASERT wiring) is absent from this binary.

## 2. Consensus parameters (real BitAIcoin `ChainType::BITAICOIN`, from `src/kernel/chainparams.cpp`)

| parameter | value |
|---|---|
| `fBitAIAuxpowEnabled` | `true` |
| `BitAIAuxpowActivationHeight` | `227808` |
| `BitAIASERTActivationHeight` | `227808` |
| `BitAIASERTHalfLife` | `21600` (6 hours) |
| `BIP34Height` | `227931` |
| `powLimit` | `0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff` |
| `nSubsidyHalvingInterval` | `210000` |
| AuxPoW chain ID | `16969` (`0x4249`, "BI") |

## 3. Real chain starting baseline: height 225823

Captured from the real, private BitAIcoin datadir's own `debug.log` (read-only inspection; the
datadir was not started or modified), then independently re-confirmed via RPC against an isolated
clone (sec.4):

| field | value |
|---|---|
| height | 225823 |
| hash | `0000000ad1060dd6b63a31796c4d57977c7f009eb0229b32d0b15dd303ecff57` |
| bits | `1d0fffff` |
| difficulty | `0.06249910592947572` |
| chainwork | `00000000000000000000000000000000000000000000002e45f80f4483f71be0` |
| time | `1789815555` (2026-09-19T10:59:15Z) |
| version | `0x20000000` |
| cumulative tx count | 14,264,498 |
| `initialblockdownload` | `true` (no peers connected; expected and harmless for this isolated clone) |

The real node was already cleanly shut down (`Shutdown done`, no crash) before this rehearsal began;
it was never started during this rehearsal.

## 4. Isolated lab topology

Lab root: `~/Downloads/bitaicoin-rehearsal-lab/` (separate from the real datadir and from the
`bitcoin-core-v31.1` source tree).

- `golden-225823/` -- a clone (APFS `cp -c`, copy-on-write) of the real datadir's `blocks/` +
  `chainstate/` at height 225823, **excluding** all wallets (not needed -- `createauxblock` is
  wallet-independent by design) and excluding `debug.log`/`mempool.dat`/`peers.dat`/`anchors.dat`
  (fresh state). This copy is never mined into directly (sec.6 requirement) and is preserved as the
  reusable source for every node clone.
- `node-A/`, `node-B/`, `node-C/`, `node-D/` -- each its own clone of `golden-225823` (again via
  `cp -c`; confirmed real disk usage did not increase measurably from cloning, since APFS shares
  the underlying blocks copy-on-write). Directory structure: `<node>/bitaicoin/{blocks,chainstate}`
  (matching the real datadir's own layout for a non-`main` chain type).
- A = B = C = the upgraded binary (sec.1). D = the obsolete binary (sec.1).
- Ports: node-A rpcport=28920, p2p port=28820 (B/C/D ports to be assigned when started, sec.14/7).

**Verified**: node A, loaded from its clone, reports height 225823, hash
`0000000ad1060dd6b63a31796c4d57977c7f009eb0229b32d0b15dd303ecff57` -- an exact match with sec.3.
(B/C/D verified identical at 225823 is `PENDING` -- they have not been started yet; only their
datadirs have been prepared, per the instruction to keep the expensive mining work to one canonical
chain during the long pre-activation gap.)

**Network isolation, verified for node A before any mining began**:
```
getpeerinfo         -> []
getnetworkinfo       -> connections: 0, connections_in: 0, connections_out: 0
                        localaddresses: []
```
Node A was started with `-connect=0 -listen=0`, confirmed to have zero peers and no listening
address -- it cannot have reached, and did not reach, the real/private BitAIcoin network.

## 5. Preflight / mining-harness experiments (NOT part of the canonical chain)

Before committing to the long 225823->227807 canonical run, the planned parallel-mining harness was
deliberately stress-tested and audited for correctness rather than assumed to work. This surfaced
three real bugs, each fixed before the canonical run was ever launched. **None of the blocks mined
during this phase are part of the canonical rehearsal chain** -- every experimental node-A copy was
discarded and the canonical run started fresh from the untouched golden 225823 snapshot (sec.6).

**Bug 1 -- identical-header overlap.** Reading `src/node/miner.cpp` showed `include_dummy_extranonce`
appends a **fixed** `OP_0` byte, not a randomized one. Verified concretely
(`~/Downloads/bitaicoin-rehearsal-lab/verify_distinct_templates.py`, real RPC calls): two workers
sharing one payout address produce a **byte-identical** coinbase (and therefore merkle root and
header) at the same height/mempool/second -- confirming that naively racing several
`generatetoaddress` calls with one shared address would have them grind the *same* nonce sequence,
explaining a disappointing ~2.2x speedup from 10 workers in an early timing test (93s for 3 blocks vs.
69s/block single-threaded). **Fix**: each worker gets its own freshly-generated, distinct payout
address every round -- a different `scriptPubKey` guarantees a different coinbase txid, hence a
different merkle root, hence a genuinely independent, non-overlapping header/nonce space
(mathematically certain, re-confirmed via the same script). Measured effect: ~31s/block (flawed) ->
~12.7s/block (fixed, early sample).

**Bug 2 -- server-side work outlives client cancellation.** Killing a losing worker's `bitcoin-cli`
*client* process does not stop the corresponding server-side RPC worker thread's mining loop
(`GenerateBlock()`'s loop is plain computation, holds no lock, and is not tied to the client
connection). A kill-then-immediately-relaunch round design could let losing workers from many rounds
pile up and exhaust the RPC thread pool (default 16) over the ~1900+ rounds needed to reach 227807.
Using this un-audited kill-based harness, a first preflight run mined 112 real (individually valid,
but mixed-provenance) blocks from 225823 to 225935 before being stopped, relabeled
`A-experimental-preflight`, and discarded without being promoted or synced to any other node.

**Bug 3 -- no hard round barrier (found on review, before any further canonical mining).** The
kill-based design could start round N+1 while round N's losers were still grinding server-side,
letting stale work overlap between rounds. **Fix**: a hard round barrier -- every worker's RPC call
(bounded to 100,000,000 tries, ~26s worst case at the measured ~3.88M hashes/sec/thread) is allowed to
return naturally, and only once *every* call from a round has returned does the next round start. A
round is complete only when all server-side work it represents has actually returned. This trades some
raw throughput for a strict correctness guarantee (explicitly the right tradeoff here: reliability over
shaving time off a one-time rehearsal).

**Bug 4 -- fresh clone has no wallet loaded.** The golden snapshot deliberately excludes wallets (not
needed for consensus). The first attempt to validate the round-barrier miner on a freshly-restored
node A silently spun at high CPU with **zero** real progress for over 200 seconds: `getnewaddress`
failed (`RPC_WALLET_NOT_FOUND`, error to stderr, empty stdout) on every call, so every worker's payout
address was an empty string, making every `generatetoaddress` call fail near-instantly on an invalid
address instead of actually grinding -- confirmed directly (`bitaicoin-cli ... getnewaddress` ->
error -18) and confirmed harmless (node A's height was untouched, still exactly 225823, since no
attempted block ever got far enough to be submitted). **Fix**: `createwallet` on every freshly-restored
node before mining, plus an explicit startup/per-round sanity check in `durable_miner.py`
(`ensure_wallet_ready()`) that fails loudly instead of spinning silently if this ever recurs.

With bugs 1, 3, and 4 fixed, a clean 4-block validation (225823->225827) was run and verified: sequential
heights, each block's `previousblockhash` exactly matching the prior accepted hash, `bits=1d0fffff`
throughout, complete per-block logging, the miner stopping exactly at its target height, and (tested
separately, with real 10-way grinding actually running) RPC control calls (`getblockcount`) remaining
responsive at 40-111ms throughout -- 10 workers confirmed safe, no need to reduce to 8. This validation
copy was then also discarded (relabeled `A-validation-preflight-2`) and node A recreated from golden
**one final time** before the canonical run below was launched.

## 6. Canonical durable mining job: 225823 -> 227807 (IN PROGRESS)

This is the authoritative run. It uses the FINAL, fully-audited miner (round barrier + bounded
maxtries + unique-address-per-worker + wallet-readiness check, sec.5) and started from a freshly
re-verified, untouched golden-225823 clone -- no preflight/debug blocks are mixed into this chain.

Per instruction, this is a **durable local OS process**, not a Claude-session background task --
launched via `nohup`+`disown`, reads the node's real current height on startup (safe to kill and
restart at any time; the only durable state is bitcoind's own real chain data), and is hard-coded to
stop at exactly 227807 without ever attempting 227808 (checked before every batch and after every
accepted block; the final round approaching 227807 runs with a single worker specifically to eliminate
any possibility of a race overshooting the target).

Immediately before launch, the freshly-restored node A was re-verified one final time to match sec.3
exactly: height 225823, hash `0000000ad1060dd6b63a31796c4d57977c7f009eb0229b32d0b15dd303ecff57`,
`bits=1d0fffff`, chainwork `...2e45f80f4483f71be0`, time `1789815555` -- all confirmed identical.
Binary re-verified: SHA-256 `c7a5c8f5627b3d33d069fc90d760957685f4a0b6dc506c0d38ecc761fe3da93e`, commit
`2e700d7ad6`. Isolation re-verified: `getpeerinfo` -> `[]`.

| field | value |
|---|---|
| script | `~/Downloads/bitaicoin-rehearsal-lab/durable_miner.py` (final, round-barrier version) |
| datadir | `~/Downloads/bitaicoin-rehearsal-lab/node-A` |
| log path | `~/Downloads/bitaicoin-rehearsal-lab/durable_miner.log` |
| state path | `~/Downloads/bitaicoin-rehearsal-lab/durable_miner_state.json` |
| PID file | `~/Downloads/bitaicoin-rehearsal-lab/durable_miner.pid` |
| launch time | 2026-09-24T07:55:13Z |
| PID at launch | 17930 |
| start height / hash | **225823** / `0000000ad1060dd6b63a31796c4d57977c7f009eb0229b32d0b15dd303ecff57` (the real, untouched baseline -- exact) |
| target stop height | 227807 (never 227808) |
| workers | 10 (one distinct payout address each, per round; reduced to 1 for the final round landing exactly on 227807) |

First canonical block confirmed real: `225824` = `0000000528872e9c9510eaaf9fc194d4e392e9524d1a3a95d3be1996e08f9f0d`,
prevhash exactly `0000000ad1060dd6b63a31796c4d57977c7f009eb0229b32d0b15dd303ecff57` (225823), `bits=1d0fffff`.

**Authoritative provenance statement**: every block from canonical height 225824 onward was mined
after the final restoration from golden 225823 using the final audited round-barrier miner. The
preflight/validation blocks recorded in sec.5 (including their own, different 225824) were
individually consensus-valid but belong to datadirs that were discarded and are **not** ancestry of
the canonical rehearsal tip -- they are preserved there only for audit/debugging history, never as
part of this chain.

**Measured throughput (canonical run only)**: ~203-207 blocks/hour, observed directly from
`durable_miner_state.json` at multiple points during the run. This is the final audited (round-barrier)
miner's own real rate -- the earlier ~283.7 blocks/hour figure belongs only to sec.5's preflight
(kill-based, not round-barrier-correct) harness and must not be read as describing this run; the
round barrier is intentionally more conservative, trading some throughput for the strict no-overlap
guarantee. Recorded here as observed throughput at time of recording, never as a completion-time
prediction.

Every accepted block is logged with height, hash, prevhash, bits, timestamp, and cumulative chainwork,
flushed and fsynced to disk immediately (`durable_miner.log`). Timestamps are recorded exactly as the
node's own real mining pipeline produced them, with no normalization or manipulation -- the first
canonical block's `nTime` (1790236513) reflects a real jump from the inherited private chain's own
historical timestamp (1789815555 at 225823) toward current wall-clock time, which is expected,
correct behavior for real blocks mined now, not a data-quality issue. Later ASERT integration checks
(sec.9 of the rehearsal spec) will compare against these actual recorded 227806/227807 timestamps,
never hypothetical ones.

Recorded heights required by the rehearsal spec (225823, 225824, 227791, 227792, 227806, 227807) --
all now real, captured directly from either `durable_miner.log`'s own accepted-block records or a
live `getblock`/`getblockheader` call against canonical node A before it was stopped. No value below
is a preflight/debug value (sec.5's contaminated provenance never entered this table):

| height | hash | prev hash | nVersion | nBits | target | chainwork | timestamp | subsidy | status |
|---|---|---|---|---|---|---|---|---|---|
| 225823 | `0000000ad1060dd6b63a31796c4d57977c7f009eb0229b32d0b15dd303ecff57` | (real chain, pre-rehearsal) | `0x20000000` | `1d0fffff` | see sec.3 | `...2e45f80f4483f71be0` | 1789815555 | 25 BAIC | real chain baseline |
| 225824 | `0000000528872e9c9510eaaf9fc194d4e392e9524d1a3a95d3be1996e08f9f0d` | `0000000ad1060dd6...` (225823) | `0x20000000` | `1d0fffff` | (== 225823's, same retarget period) | `...2e45f80f4493f71ce0` | 1790236513 | 25 BAIC | ACCEPTED (canonical A), round-barrier miner |
| 227791 | `00000001b8c0950b56bb5ce34fd7fdebe4ee49c4a803e9206073ddc8da087299` | `00000002e37542ef5b7b615d85d9b1db6efaeae447cdaeaebebbf47e7896ce6a` | `0x20000000` | `1d0fffff` | (same retarget period) | `...2e45f80fbf83fecbe0` | 1790268403 | 25 BAIC | ACCEPTED (canonical A) |
| 227792 | `0000000b669266d6ee7bc2d2e09f9f6316711d88bd27e58ffe18cb332388f15b` | `00000001b8c0950b56bb...` (227791) | `0x20000000` | `1d0fffff` | (same retarget period) | `...2e45f80fbf93fecce0` | 1790268420 | 25 BAIC | ACCEPTED (canonical A) |
| 227806 | `00000008908ad22a41f6eb02a7bdefbf9e2cc04150ec09262e129c37607d15f4` | `00000002b9e67d17066edd07182ca61e3c846e808626b11383cd25e72c7b22cc` | `0x20000000` | `1d0fffff` | `0000000fffff0000...0000` | `...2e45f80fc073fedae0` | 1790268637 (mediantime 1790268558) | 25 BAIC | ACCEPTED (canonical A); coinbase `03de790300` -> BIP34 height `0x0379de` = 227806, correct |
| 227807 | `000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f` | `00000008908ad22a41f6eb02a7bdefbf9e2cc04150ec09262e129c37607d15f4` (227806) | `0x20000000` | `1d0fffff` | `0000000fffff0000...0000` | `...2e45f80fc083fedbe0` | 1790268674 (mediantime 1790268576) | 25 BAIC | ACCEPTED (canonical A); coinbase `03df790300` -> BIP34 height `0x0379df` = 227807, correct; **last pre-activation block -- the golden-227807-pre-activation snapshot anchor, sec.7** |

Full verbose `getblock` records for 227806 and 227807 (nonce, merkleroot, full coinbase tx, witness
commitment) were captured before node A was stopped and are preserved verbatim in
`~/Downloads/bitaicoin-rehearsal-lab/blocks_227806_227807.json`; nonces: 227806 = `11512577`,
227807 = `80226489`.

### 6.1 Mid-run audit (read-only, canonical miner left completely untouched)

Performed against the ACTUAL file on disk (not any edit/diff transcript) and the actual running
process/logs, without pausing, restarting, or otherwise touching the canonical run.

**Static file audit of `durable_miner.py`** -- every property below checked by direct inspection and
`grep`, not assumed:
- Exactly one worker-launch mechanism: a single `subprocess.Popen(...)` call site, inside `run_round()`
  only. No `Popen` call exists anywhere in `main()`.
- Zero occurrences of `kill(`, `.terminate(`, or any `signal`/`SIGTERM` use anywhere in the file (the
  one `grep` hit for `kill(` is inside a comment, not code).
- `run_round()` launches `n_workers` processes and then does `for p in procs: p.wait()` -- an
  unconditional, un-timed-out wait on every process -- before returning. Since `run_round()` is called
  synchronously from the single `while True` loop, no next round's `getnewaddress`/`generatetoaddress`
  calls can be issued until the prior round's `run_round()` call has returned, which requires every
  process from that round to have already exited on its own.
- Unique payout address per worker: `worker_addrs = [cli("getnewaddress") for _ in range(n_workers)]`
  -- one fresh call per worker.
- Empty-address/no-wallet failures are fatal (`sys.exit(1)`) in two places: `ensure_wallet_ready()` at
  startup, and again defensively inside `run_round()` before any workers are launched.
- Current height is re-read (`height = get_height()`) as the first statement inside the main loop,
  before every round.
- The running process's actual invocation (confirmed via `ps`) has `stop_height=227807` as its literal
  4th argument.
- `n_workers = 1 if height == STOP_HEIGHT - 1 else WORKERS` -- the round that could produce exactly
  227807 (i.e. starting from height 227806) runs with exactly one worker.
- `if new_height > STOP_HEIGHT: ... write_state({"status": "OVERSHOT_MUST_DISCARD", ...}); return 1` --
  an explicit halt state, traced to confirm `height` can only ever reach `>= STOP_HEIGHT` in this loop
  by having passed through this overshoot check first (i.e. by being observed via a real
  `get_height()` call to equal exactly 227807, never inferred).
- The final "stopped at target" report re-queries `get_height()` one more time rather than assuming
  success from the loop having exited.

**One honest, minor gap found** (not requiring a stop -- does not violate any safety/provenance
property, and the running log shows continuous, gapless real progress with no evidence it has ever
manifested): `run_round()` does not inspect each worker's `p.returncode`, so a genuine RPC/connection
failure on a worker would currently be indistinguishable from a normal exhausted-maxtries round --
both simply result in no height change and a fresh round being tried. Left as a documented item for
post-227807 script hygiene, not fixed now (fixing it would require restarting the running canonical
miner, which is out of scope while it is executing).

**Runtime invariant check** (read-only, parsed directly from `durable_miner.log` and live RPC calls,
94 accepted-block log entries at time of this audit):
- Accepted heights strictly monotonically increasing: confirmed.
- No accepted height greater than 227807: confirmed (max observed height at audit time: 225917).
- Every accepted block's `prevhash` exactly equals the immediately preceding accepted block's `hash`
  (and the very first entry's `prevhash` exactly equals the real 225823 baseline hash): confirmed,
  unbroken.
- `bits` is `1d0fffff` on every single accepted block so far (all pre-activation, as expected):
  confirmed.
- No gaps: every consecutive pair of accepted heights differs by exactly 1.
- No accumulating orphaned `bitaicoin-cli` processes: exactly 10 live worker processes observed at
  audit time (matching one clean, in-flight round), not dozens/hundreds.
- RPC remains responsive: `getblockcount` answered in 10ms during this audit.
- Peer count remains zero: `getpeerinfo` -> `[]`.

### 6.2 ASERT anchor/reference computation and legacy-vs-ASERT comparison for 227808

Computed from the real, real captured 227806/227807 data above (sec.6's table) using the
already-existing, already-differentially-tested `contrib/asert_reference.py` -- no mining performed,
no consensus code touched; this is read-only arithmetic over real chain data, run before Scenario
A/B and used only to state what each branch of `GetNextWorkRequired()` would independently produce.

**ASERT anchor/reference-timestamp convention** (frozen design, `src/pow.cpp:52-74`, exercised here
against real data for the first time): for the first ASERT block (227808), the anchor is
`BitAIASERTActivationHeight - 1` = **227807 itself**.
- `anchorParams.nHeight` = 227807
- `anchorParams.nBits` = 227807's own real nBits = `1d0fffff` (target `431358735298270906413161702650018451440568065076682751302691926835200`)
- `anchorParams.nPrevBlockTime` = the anchor's **PARENT's** timestamp = block 227806's real `nTime`
  = **1790268637** -- explicitly NOT 227807's own time (1790268674) and explicitly NOT median-time-past
  (227807's mediantime is 1790268576, a third, different value; none of the three are interchangeable
  and the code correctly uses only the first).
- For 227808, `pindexLast` = block 227807 = the anchor itself, so `heightDiff` = `227807 - 227807` = **0**.
- `timeDiff` = `pindexLast->GetBlockTime() - nTimeReference` = `1790268674 - 1790268637` = **37 seconds**.

Real computed result: `calculate_asert(ref_target=<227807's target>, pow_target_spacing=600,
time_diff=37, height_diff=0, pow_limit=<BitAIcoin's real powLimit>, half_life=21600)` =
target `423654490308240600993901971007076456240812435801593378127646136729600`, which encodes back to
**nBits `0x1d0fb6d7`** (ratio to the anchor's own target: 0.98213957 -- ASERT *tightens* the target
here: 37 real seconds elapsed for 0 height-delta, i.e. real block 227807 arrived far ahead of the
per-block schedule implied by the anchor's own reference point, so ASERT correctly hardens difficulty
in response, which is expected and correct ASERT behavior, not a bug).

**Legacy 2016-block retarget, computed independently for comparison (never actually applied --
`GetNextWorkRequired()`'s ASERT branch runs first and is unconditionally taken once
`pindexLast->nHeight + 1 >= BitAIASERTActivationHeight`, per `src/pow.cpp:52-53`; this is a
what-if computation only)**: 227808 = 2016 x 113 exactly, so 227808 is also a legitimate legacy
retarget boundary. Using real block 225792 (the real first block of the 2016-block period ending at
227807; hash `0000000d853ab305c262dbf9c67340b7d20d11b564a0e2787a9b05b1f592a5f3`, real time
`1789789080`, fetched read-only from the untouched `scenario-A-direct` clone, which remained at
height 227807 with zero blocks mined into it throughout, verified before and after this query) and
real block 227807's own time (1790268674): `nActualTimespan` = `1790268674 - 1789789080` = `479594`
seconds -- within `[nPowTargetTimespan/4, nPowTargetTimespan*4]` = `[302400, 4838400]`, so **no
clamping applies**. `bnNew = <227807's target> * 479594 / 1209600`, clamped to `powLimit` (no clamp
needed) -> **nBits `0x1d065805`**.

**Corrected interpretation** (an earlier draft of this section had this backwards -- corrected here,
values unchanged): all three nBits share the same compact exponent byte (`0x1d`), so target is
directly proportional to the mantissa, and `0x065805 < 0x0fb6d7 < 0x0fffff` means
**`target(legacy) < target(ASERT) < target(anchor)`** -- the legacy value is a *smaller* target, i.e.
**substantially higher difficulty**, not looser. Quantitatively (verified by
`~/Downloads/bitaicoin-rehearsal-lab/verify_227808_targets.py`, sec.6.2a):
- ASERT target / anchor target ≈ **0.98213957** → ASERT difficulty ≈ **1.018185x** anchor difficulty
- legacy target / anchor target ≈ **0.39648952** → legacy difficulty ≈ **2.522135x** anchor difficulty

The real 225792→227807 legacy retarget window (1,984 real blocks) completed in 479,594 real seconds,
far faster than its nominal 1,209,600-second (2016*600s) schedule. A legacy DAA reacting to that
entire window would therefore have judged the network "much too fast" and **hardened difficulty
sharply (~2.52x)** at 227808 -- the correct legacy-DAA response to blocks arriving faster than
scheduled is to raise difficulty, not lower it. ASERT also hardens at 227808, because the real
227806→227807 interval it actually sees is only 37 seconds (also faster than the 600s/block
schedule), but only **modestly (~1.02x)**, because ASERT's first-block computation is anchored purely
to that one local interval rather than carrying the entire legacy window's cumulative timing history.
The point of the comparison is **large difficulty increase (legacy) vs. small difficulty increase
(ASERT)** -- never "legacy easing off."

### 6.2a Regression assertion (rehearsal/documentation verification only, `pow.cpp` untouched)

`~/Downloads/bitaicoin-rehearsal-lab/verify_227808_targets.py` pins the corrected ordering as a real,
runnable assertion so this specific inversion cannot silently recur: asserts
`target(0x1d065805) < target(0x1d0fb6d7) < target(0x1d0fffff)` and therefore
`difficulty_legacy > difficulty_ASERT > difficulty_anchor`. Run and **PASSED**. This is documentation
tooling only -- it does not touch `src/pow.cpp` or any consensus code, and changes no computed nBits.

**Recorded per instruction, without changing either calculated nBits**: the two branches genuinely
disagree (`0x1d065805` legacy vs. `0x1d0fb6d7` ASERT -- legacy is dramatically *harder*, not looser).
This is exactly the designed behavior, not a discrepancy to fix: `GetNextWorkRequired()` gates on
`BitAIASERTActivationHeight` *before* the legacy `% DifficultyAdjustmentInterval()` check, so real
227808 will unconditionally receive the ASERT value `0x1d0fb6d7`, never the legacy value, regardless
of 227808's coincidental alignment with a legacy retarget boundary. Scenario A (sec.8) mines a real
227808 block and independently confirms the real node's own `GetNextWorkRequired()` output matches
this computed `0x1d0fb6d7` exactly, via real RPC, not by trusting this offline computation alone.

### 6.3 Automated full provenance verification (all 1,984 canonical blocks)

Run against canonical node A (still live at this point, before its clean shutdown in sec.6.4) via
`~/Downloads/bitaicoin-rehearsal-lab/verify_canonical_provenance.py` (preserved verbatim in the lab
directory) -- walks every height from 225824 through 227807 via real `getblockhash`/`getblockheader`
RPC calls, checking, all by real observation and never by sampling:

- Exact block count added: 1,984 (== `227807 - 225823`) -- **PASS**
- No height gaps (every height from 225824..227807 answered a real `getblockhash`) -- **PASS**
- Unbroken `previousblockhash` linkage for all 1,984 blocks, starting from the real 225823 baseline
  hash -- **PASS**, zero breaks
- No AuxPoW-version block anywhere in the range (`nVersion & (1<<8) == 0` for all 1,984) -- **PASS**,
  zero found (expected: AuxPoW only activates at 227808, outside this range)
- All 1,984 blocks direct-mined (same check as above) -- **PASS**
- All 1,984 blocks match the expected legacy-DAA bits `1d0fffff` (the entire 225792-227807 window
  falls within one legacy retarget period, so this value is constant throughout) -- **PASS**, zero
  violations
- Final tip after the walk exactly equals `getblockhash(227807)` -- **PASS**

**OVERALL PROVENANCE VERIFICATION: PASS.** All 1,984 canonical blocks are real, sequential,
direct-mined, correctly-bitted, unbroken descendants of the real 225823 baseline -- no preflight or
debug provenance entered this chain.

### 6.4 Clean shutdown of canonical node A

Canonical node A was stopped via normal RPC (`bitcoin-cli stop`), **not** killed and **not** cloned
while live. `debug.log` confirms `Shutdown in progress...` followed by `Shutdown done` with no crash,
before any snapshot was taken. From this point, canonical node A's own datadir
(`~/Downloads/bitaicoin-rehearsal-lab/node-A`) is treated as read-only evidence; it has not been
started since, except momentarily and read-only via its own untouched clone for later verification
steps (never node A itself).

## 7. Golden 227807 pre-activation snapshot

Created via APFS `cp -Rc` (copy-on-write clonefile) directly from node A's now-cleanly-stopped
datadir -- never from a live node.

`~/Downloads/bitaicoin-rehearsal-lab/golden-227807-pre-activation/` (immutable; `bitaicoind` is never
run directly against this copy -- every scenario clones FROM it first):

| field | value |
|---|---|
| height | 227807 |
| hash | `000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f` |
| prevhash | `00000008908ad22a41f6eb02a7bdefbf9e2cc04150ec09262e129c37607d15f4` (227806) |
| bits | `1d0fffff` |
| chainwork | `00000000000000000000000000000000000000000000002e45f80fc083fedbe0` |
| time | `1790268674` |
| created (filesystem clone time) | `2026-09-25T00:28:42Z` |
| source datadir | `~/Downloads/bitaicoin-rehearsal-lab/node-A/bitaicoin` (post clean-shutdown) |
| source commit | `2e700d7ad6fe08c840d2e9b4e99ef5e89a2b34f6` |
| `bitaicoind` SHA-256 | `c7a5c8f5627b3d33d069fc90d760957685f4a0b6dc506c0d38ecc761fe3da93e` |

**Independent verification** (a separate, discardable `verify-227807` clone of this golden snapshot,
started fresh, checked, then discarded per instruction -- never kept as a second permanent copy):
height, best hash, chainwork, the 227806 hash, and the 227807 hash all matched sec.6/sec.7 exactly;
`getblock(227808)` correctly failed (block not found -- confirms nothing beyond 227807 exists in this
snapshot); a **second restart** of the same `verify-227807` clone reproduced the identical height,
hash, and chainwork with no divergence. **Result: PASS.** The `verify-227807` directory was then
discarded (`rm -rf`), as instructed -- it exists only as a one-time independent check, not as a second
permanent snapshot.

Two separate scenario clones were then made from this same immutable golden snapshot, each untouched
since creation:
- `~/Downloads/bitaicoin-rehearsal-lab/scenario-A-direct/` -- for Scenario A (direct-mined 227808).
  Confirmed still at height 227807 with zero blocks mined into it as of this report (used only for a
  brief, read-only query in sec.6.2 to fetch real block 225792's timestamp; verified unchanged before
  and after that query).
- `~/Downloads/bitaicoin-rehearsal-lab/scenario-B-auxpow/` -- for Scenario B (AuxPoW-mined 227808).
  Never started; reserved untouched for Scenario B.

## 7.1 Obsolete node D: pre-activation compatibility record

Per instruction, D was synced only up to 227807 (not past it) before this report, to record its
pre-activation compatibility honestly before any AuxPoW-only behavior could possibly diverge it.

Procedure (real P2P, no manufactured state): a disposable clone of `golden-227807-pre-activation`
(`sync-source-for-D`, upgraded binary, `-listen=1` so it could accept D's inbound connection) was
started as a sync source; `node-D` was reset to a genuinely empty datadir (height 0) and started with
the **obsolete** binary (`pre-auxpow-binaries-mac`, `-connect=0 -listen=0`), then connected to the
source via `addnode ... add` and left to sync purely via its own real, unmodified P2P/headers-first
code path -- no blocks or state were copied into D directly.

| check | result |
|---|---|
| binary identity preserved | obsolete `pre-auxpow-binaries-mac` throughout; never swapped for the upgraded binary |
| synced via real P2P from genesis to 227807 | confirmed -- `getblockcount` climbed monotonically 0 -> 227807 purely via headers-first sync against the real chain data, no manual state injection |
| best hash == upgraded chain's 227807 hash | **MATCH**: D's `getbestblockhash` = `000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f`, identical to sec.6/sec.7 |
| chainwork matches | **MATCH**: `00000000000000000000000000000000000000000000002e45f80fc083fedbe0`, identical to sec.6/sec.7 |
| no AuxPoW RPC exists on D | confirmed: `createauxblock` -> `"unknown command"`, `submitauxblock` -> `"unknown command"`; `help` output contains no `aux*` commands at all |

**Result: D is a fully compatible, unmodified pre-AuxPoW peer at the pre-activation boundary** -- it
independently validated and converged on the exact real 227807 tip via its own real (unpatched)
validation code, with zero AuxPoW awareness, which is the correct and expected state immediately
before activation. D was then stopped cleanly via normal RPC (`Shutdown done` confirmed in D's own
`debug.log`), and the throwaway `sync-source-for-D` clone was discarded (`rm -rf`) once no longer
needed. D's own datadir (now at real height 227807, obsolete binary) is preserved, stopped, for the
post-activation divergence test in Scenario A/B/convergence (sec.12+), where D is expected to
continue accepting direct-mined blocks normally but be unable to validate an AuxPoW-versioned block.

## 9. Scenario A: first activation block, DIRECT (227808)

Run against `scenario-A-direct/` only -- a pristine, untouched clone of `golden-227807-pre-activation`
(never node A itself, never reused for Scenario B).

**Pre-mining verification** (before any mining): height 227807, best hash
`000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f` (exact match to sec.7), 227807's
own bits `1d0fffff`, `getblockhash(227808)` correctly errored (`-8 Block height out of range`), zero
peers.

**Live integration confirmation, before solving anything**: a real `getblocktemplate` call against
this node returned, on its own, with no consensus code touched and nothing computed offline:
`height=227808`, `bits=1d0fb6d7`, `previousblockhash=<the golden 227807 hash>`,
`target=0000000fb6d70000...0000`. This is the exact ASERT value independently computed in sec.6.2 --
**confirmed live, from the real node's own `GetNextWorkRequired()`, not merely predicted.** It did
**not** return the legacy value `0x1d065805`. Since the template already matched, no "fix" was needed
or performed.

**Mined exactly one block (227808), directly** (`generatetoaddress`, no consensus code touched):

| field | value |
|---|---|
| hash | `000000077fbb8bbec7664628a710a3818caa15238a4408d4247834c8dbd5f715` |
| height | 227808 |
| version | `536870912` / `0x20000000` (VERSION_AUXPOW bit **unset**) |
| previousblockhash | `000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f` (exactly the golden 227807 hash) |
| bits | `1d0fb6d7` |
| target | `0000000fb6d70000000000000000000000000000000000000000000000000000` |
| time | `1790301665` |
| mediantime | `1790268591` |
| nonce | `47956273` |
| chainwork | `00000000000000000000000000000000000000000000002e45f80fc09449597b` |
| difficulty | `0.06363566626887295` |
| coinbase | `03e0790300` (height push `0x0379e0` = 227808, correct) |
| subsidy | 25 BAIC |

**Verified**: accepted; height 227808; prevhash exactly the golden 227807 hash; `bits == 1d0fb6d7`
exactly; VERSION_AUXPOW bit unset; no AuxPoW proof attached (verbosity-2 `getblock` shows a single
plain coinbase tx, no `auxpow` field); PoW valid against the ASERT target (the node's own validation
accepted the block -- it would have rejected an invalid-PoW header outright); `getblock`/`getblockheader`
agree on hash/height/version/bits/target/time/mediantime/nonce/chainwork exactly; chainwork increase
is consistent with the ASERT target (difficulty ratio 227808/227807 = `0.06363566626887295 /
0.06249910592947572` = **1.0181852256**, matching the independently-computed ASERT prediction
**1.018185** to 6 significant figures); a real stop+restart of this node reproduced the identical
227808 tip exactly (same height, hash, bits, chainwork). 227809 was **not** mined -- not required by
the documented checks.

**Comparison against both precomputed possibilities**: the real node used `0x1d0fb6d7`. It did
**not** use `0x1d065805`. **This is real network-level integration evidence** (not a unit test) that
the ASERT branch in `GetNextWorkRequired()` supersedes the legacy 2016-block retarget at the exact
real production activation boundary, exactly as the frozen gating order in `src/pow.cpp:52-53`
specifies.

### 9.1 Obsolete node D vs. Scenario A's DIRECT 227808 block

D's preserved, real, P2P-synced 227807 state (sec.7.1) was reused for this observation, then
preserved again afterward as an immutable `golden-D-227807-obsolete` snapshot (confirmed to have
never advanced past 227807 -- no on-disk chain-state write ever occurred from this observation) so
later observations always restore from a clean copy rather than reusing a possibly-stateful instance.
D was connected **only** to Scenario A's node (never any other peer).

Real, observed result (not assumed):

| check | result |
|---|---|
| header accepted/rejected | **REJECTED** |
| block accepted/rejected | rejected (header rejection means the block itself was never requested/downloaded) |
| debug reject reason (exact log line) | `[validation] AcceptBlockHeader: Consensus::ContextualCheckBlockHeader: 000000077fbb8bbec7664628a710a3818caa15238a4408d4247834c8dbd5f715, bad-diffbits, incorrect proof of work` followed by `[net] Misbehaving: peer=0: invalid header received` |
| D height | unchanged: 227807 |
| D best hash | unchanged: `000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f` |
| D getchaintips | a single tip, unchanged: `{"height": 227807, "hash": "000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f", "status": "active"}` -- the rejected header was not even retained as a headers-only branch |
| last common block | 227807 (the golden hash) |
| peer disconnect | none -- D logs `[warning] Not punishing manually connected peer 0!` (manually-added peers are never banned/disconnected on misbehavior, by design) |

**Real finding, confirmed by exact log line, not assumed**: even though this is a *direct*-mined
block (no AuxPoW involved at all), D still rejects it -- specifically via
`ContextualCheckBlockHeader`'s **`bad-diffbits`** check, because D's own (pre-ASERT) `GetNextWorkRequired()`
independently computes the legacy value `0x1d065805` for height 227808 and compares it against the
block's actual declared `0x1d0fb6d7`, finding a mismatch. This confirms the exact mechanism predicted
in sec.6.2: **the consensus split at 227808 is visible to an obsolete node purely from the difficulty
computation itself, even before AuxPoW's own serialization/versioning is ever considered.**

## 10. Scenario B: first activation block, AuxPoW (227808)

Run independently against `scenario-B-auxpow/` only -- confirmed to have never inherited Scenario A's
227808 state (started completely fresh from the golden snapshot, on its own ports).

**Pre-mining verification**: height 227807, hash `000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f`
(exact match to sec.7), `getblockhash(227808)` correctly errored, zero peers.

**`createauxblock` result, asserted BEFORE any parent work (per instruction -- code would stop, not
"fix," on any mismatch)**:
- returned height = **227808** -- matches
- returned chain ID = **16969** -- matches
- returned bits = **`1d0fb6d7`** -- matches
- returned numeric target (both the raw `target` hex field and the bits-derived target, cross-checked
  against each other) = **exactly** the same ASERT target independently computed in sec.6.2 and live-
  confirmed by Scenario A's own `getblocktemplate` -- matches exactly

All four checks passed; no divergence, so mining proceeded (nothing was "fixed").

**Mined via the frozen reference coordinator** (`createauxblock` -> synthetic parent -> real SHA256d
solve of the parent against the BAIC target -> `CAuxPow` -> `submitauxblock`):

| field | value |
|---|---|
| `submitauxblock` result | `accepted` |
| hash (child) | `551982b3837caf9482f5b85da5c607d808483c3938b96ab0449ac172238ef0cd` |
| height | 227808 |
| version | `1112080640` / `0x42490100` (VERSION_AUXPOW bit **set**) |
| encoded chain ID | 16969 (consistent with `createauxblock`'s own returned chain ID; enforced by the coordinator's real wire encoding, not asserted separately here since `createauxblock`'s value was already checked above and the same job object was used to build the submission) |
| previousblockhash | `000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f` (exactly the golden 227807 hash) |
| bits | `1d0fb6d7` |
| target | `0000000fb6d70000000000000000000000000000000000000000000000000000` |
| time | `1790301957` |
| mediantime | `1790268591` |
| chainwork | `00000000000000000000000000000000000000000000002e45f80fc09449597b` |
| coinbase | `03e0790300` |
| subsidy | 25 BAIC |

**Verified**: `submitauxblock` returned `accepted`; node tip became exactly `createauxblock`'s
predicted child hash; child height 227808; child `bits == 1d0fb6d7`; VERSION_AUXPOW **set**; encoded
chain ID 16969; the proof passed the node's own normal validation (accepted, not merely "not yet
rejected"); `getblock`/`getblockheader` agree; a real stop+restart reproduced the identical 227808
AuxPoW tip exactly.

**Chainwork cross-check against Scenario A (explicit "no work bonus/penalty" requirement)**:
Scenario A (direct) chainwork at 227808 = `...2e45f80fc09449597b`. Scenario B (AuxPoW) chainwork at
227808 = `...2e45f80fc09449597b`. **Identical, exactly.** The proof mechanism (direct vs. AuxPoW)
does not influence chainwork/difficulty in any way.

### 10.1 Obsolete node D vs. Scenario B's AuxPoW 227808 block

Per instruction, D was **not** reused from the 9.1 observation. A fresh clone was made from the
immutable `golden-D-227807-obsolete` snapshot (created in 9.1, confirmed untouched -- still real
height 227807) into a new, separate `D-vs-scenario-B/` directory, then connected **only** to
Scenario B's node.

Real, observed result (not assumed, and explicitly checked for whether it differs from 9.1's
mechanism rather than assumed identical):

| check | result |
|---|---|
| header accepted/rejected | **REJECTED** |
| block accepted/rejected | rejected (header rejection; block never requested) |
| debug reject reason (exact log line) | `[net] Misbehaving: peer=0: header with invalid proof of work` -- notably **no** `AcceptBlockHeader: Consensus::ContextualCheckBlockHeader: ..., bad-diffbits` line this time |
| D height | unchanged: 227807 |
| D best hash | unchanged: `000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f` |
| D getchaintips | a single tip, unchanged, identical to 9.1's | 
| last common block | 227807 (the golden hash) |
| peer disconnect | none (same manual-peer no-punishment behavior as 9.1) |

**Real finding, genuinely distinct from 9.1 -- recorded exactly, not summarized as "same as before"**:
the reject reason's *log-line shape* differs from Scenario A's. Scenario A's direct block was rejected
by the **named** `ContextualCheckBlockHeader` / `bad-diffbits` check (the header's own declared
`nBits` failed D's independent recomputation of the *expected* value). Scenario B's AuxPoW block was
rejected by an **earlier, more generic** check -- `"header with invalid proof of work"`, with no
`bad-diffbits` label and no offending hash printed -- consistent with D's basic, pre-AuxPoW
`CheckProofOfWork()` evaluating the header's *own* hash against its declared target directly (as
every pre-AuxPoW header is validated), which fails for a real AuxPoW block: the child header's own
hash was never engineered to satisfy the target by itself -- only the *synthetic parent's* hash was
solved, per the merge-mining design -- so a binary with no AuxPoW-aware validation code necessarily
sees the child header as carrying "invalid proof of work," independent of and prior to any
diffbits/expected-value comparison. (The received `headers` message was also larger -- 398 bytes vs.
163 bytes for Scenario A's -- consistent with the AuxPoW block's header carrying the serialized
`CAuxPow` proof, which the obsolete binary parses far enough to attempt a proof-of-work check on, but
does not understand.) Both blocks are correctly rejected by D, but for two different, real,
independently-confirmed reasons -- exactly the kind of distinction this rehearsal exists to surface
rather than assume.

## 11. Structural comparison: Scenario A vs. Scenario B

| property | Scenario A (direct) | Scenario B (AuxPoW) | equal? |
|---|---|---|---|
| block hash | `000000077fbb8bbec7664628a710a3818caa15238a4408d4247834c8dbd5f715` | `551982b3837caf9482f5b85da5c607d808483c3938b96ab0449ac172238ef0cd` | **No** (expected -- different coinbase/proof) |
| parent height | 227807 | 227807 | **Yes** |
| prevhash | `000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f` | `000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f` | **Yes** |
| nBits | `1d0fb6d7` | `1d0fb6d7` | **Yes** |
| chainwork increment | `...2e45f80fc09449597b` | `...2e45f80fc09449597b` | **Yes, identical** |

**Confirmed**: the proof mechanism (direct vs. AuxPoW) has zero influence on required difficulty or
resulting chainwork -- both real, independently-mined 227808 blocks, from two completely separate
golden-snapshot clones, received exactly the same ASERT-computed target and produced exactly the same
chainwork increment.

## 12. Result: both Scenario A and Scenario B pass; expected conditions met

| condition | expected | observed |
|---|---|---|
| Scenario A (direct) | 227808 accepted with `1d0fb6d7` | **accepted with `1d0fb6d7`** -- matches |
| Scenario B (AuxPoW) | 227808 accepted with `1d0fb6d7` | **accepted with `1d0fb6d7`** -- matches |

Neither scenario failed or differed from the expected upgraded-node conditions. Per instruction, this
means the rehearsal may proceed to the multi-node convergence/reorg phase in a future session -- not
run automatically in this same pass; report delivered first (see chat).

Both obsolete-node observations (9.1, 10.1) surfaced real, distinct, correctly-rejecting behavior --
consistent with (not merely assumed to be) a genuine consensus fork at 227808 for a node running the
obsolete binary, for two different underlying reasons depending on the proof mechanism used.

## 14. Documentation hygiene check (before this phase began)

Before starting the convergence/reorg phase, the actual on-disk `docs/ACTIVATION_REHEARSAL_227808.md`
was inspected directly (not the edit-transcript) for the specific duplicate/stale rows flagged as a
possible concern (Scenario A/B marked both PENDING and DONE, duplicate ASERT-dynamic-verification
rows, duplicate discrepancies rows). Verified by exact `grep -c` counts on the real file: every such
row-key appeared exactly once (2 occurrences of a given phrase were always one section heading plus
one table row correctly cross-referencing it, e.g. `## 9. Scenario A...` plus a `sec.9` table row --
never two conflicting status rows for the same item). **No duplicates or stale PENDING/DONE conflicts
were found in the actual file; no cleanup was necessary, and no result was altered.** No commit was
made for this check per instruction ("commit only if the actual document required cleanup").

## 15. Preserved evidence (untouched throughout this phase)

Per instruction, the following were never reused or mutated during convergence/reorg testing; every
experiment below used a fresh disposable clone instead:
- canonical node A's evidence (`node-A/`, stopped)
- `golden-227807-pre-activation/` (immutable)
- Scenario A's completed direct-227808 datadir (`scenario-A-direct/`)
- Scenario B's completed AuxPoW-227808 datadir (`scenario-B-auxpow/`)
- `golden-D-227807-obsolete/` (immutable)

New tooling for this phase, all in `~/Downloads/bitaicoin-rehearsal-lab/`: `phase2_common.py` (shared
node lifecycle/RPC helpers, fresh port plan), `parallel_parent_solve.py` (a parallelized version of
the synthetic-parent PoW solver -- rehearsal tooling only, not consensus; cuts real per-AuxPoW-block
wall-clock time from ~24 minutes single-threaded to single-digit seconds to low minutes by splitting
the real 32-bit nonce space across 8 OS processes, same "give each worker independent, disjoint search
space" principle already used and verified for direct mining in `durable_miner.py`), `run_convergence.py`,
`run_reorg_same_anchor.py`, `run_reorg_reverse.py`, `run_branch_local_asert.py`, `wrong_bits_subtest.py`.

**Real infrastructure bug found and fixed before any of this phase's mining began**: `src/init.cpp`
always binds a loopback "onion service target" address at `p2p_port + 1`, **regardless of
`-listenonion`** (confirmed by reading `init.cpp`'s `default_bind_port_onion = default_bind_port + 1`
and by direct observation that passing `-listenonion=0` did not stop the bind). A first attempt at
sequential p2p ports (28830/28831/28832 for three nodes) meant each node's onion-target bind silently
captured the *next* node's real p2p port, so `addnode`-initiated manual connections for two of three
links never established (only the one link whose target port had no `+1` collision worked) --
diagnosed via `getaddednodeinfo` showing `connected: false` indefinitely and via direct comparison of
working vs. non-working nodes' `debug.log` "Bound to" lines. Fixed by spacing every p2p port in this
phase's port plan by 10 (`phase2_common.py`'s `PORTS` dict), not by disabling onion (which does not
actually help). Not a consensus issue; purely a rehearsal-lab port-planning bug in code that did not
exist before this phase.

## 16. Three-upgraded-node mixed direct/AuxPoW convergence (items 2-5)

Three fresh nodes (`conv-A`, `conv-B`, `conv-C`), independently cloned from the same immutable
`golden-227807-pre-activation` snapshot, all running the same upgraded binary/commit. Verified before
connecting: all three at height 227807, all three `bestblockhash ==`
`000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f`, all isolated (zero peers). Connected
in a real full mesh over localhost only (`addnode` both directions between every pair) -- confirmed 2
peers per node, no external peers at any point.

**Mixed sequence actually mined** (real blocks, alternating miner and mechanism):

| height | miner | mechanism | hash | bits | VERSION_AUXPOW | chainwork |
|---|---|---|---|---|---|---|
| 227808 | conv-A | direct | `000000028241d6a20ba5cda85d23a990a63a233c8484873f10490ff0a73e8288` | `1d0fb6d7` | unset | `...2e45f80fc09449597b` |
| 227809 | conv-B | AuxPoW | `566c4f9ca2ac993709ec75bd2be3cfc36b3d447b906b41034f2919d125ddb4e8` | `1d0fffff` | **set** | `...2e45f80fc0a4495a7b` |
| 227810 | conv-C | direct | `000000053e1457a08b0a31de80dda6233e331219bc23d80a536c627cdcae5022` | `1d0fffff` | unset | `...2e45f80fc0b4495b7b` |
| 227811 | conv-A | AuxPoW | `50449791b525aa5d981693c43cfa8ea2ff717129726c6c1a97f66890651968ed` | `1d0fffff` | **set** | `...2e45f80fc0c4495c7b` |
| 227812 | conv-B | direct | `0000000427257e94fa4ce696032c6971b5cd485251f8e31ca49299fd5cbf9192` | `1d0fffff` | unset | `...2e45f80fc0d4495d7b` |
| 227813 | conv-C | AuxPoW | `862f0fa16317a1aa65e801dfc6740ce55374717a5970fda32681128382dc14f1` | `1d0fffff` | **set** | `...2e45f80fc0e4495e7b` |

Real ASERT dynamism observed: 227808's bits (`1d0fb6d7`) tightened slightly from the anchor (real
227806->227807 interval was only 37s); by 227809 the chain had already caught back up to/past its
implied schedule (blocks arriving fast relative to ASERT's 21600s half-life), so ASERT eased back to
its ceiling `1d0fffff` (`powLimit` itself) and stayed there through 227813 -- expected, correct dynamic
behavior, not a bug.

**Item 4 (DAA continuity), checked before every single block, on the actual mining node, before
mining**: `getblocktemplate.bits` (direct) vs. `createauxblock.bits` (AuxPoW) queried together at
every step -- **identical at all 6 steps** (`1d0fb6d7` at the first step, `1d0fffff` thereafter). The
proof mechanism never forks DAA state.

**After every single block** (not just the final height), independently verified on all three nodes:
same best height, same best hash, same chainwork, same bits -- **converged after all 6 blocks, with
zero exceptions**. `assert_converged()` compares real `getblockchaininfo` output across all three
nodes at every step.

### 16.1 Mixed-chain restart test (item 5)

Stopped all three cleanly (`bitcoin-cli stop`, confirmed exit), restarted independently (not yet
reconnected): all three preserved height=227813, identical hash/chainwork/bits **even before
reconnecting** (each node has its own full copy of the chain; convergence does not depend on staying
connected). All 6 blocks (3 direct, 3 AuxPoW) re-read via `getblock` after restart -- every field
(height, bits, VERSION_AUXPOW, chainwork, prevhash) matched their pre-restart record exactly: **no
AuxPoW proof/serialization corruption survives restart**. Reconnected (addnode not auto-persisted
across restart in this setup, so reconnected explicitly) -- remained converged (same height/hash/
chainwork) after reconnect.

## 17. Same-anchor, activation-crossing competing-branch reorg (items 6-8)

Two fresh, independent clones of `golden-227807-pre-activation` (`reorg-X`, `reorg-Y`), kept
disconnected while each built its own competing branch.

- X mined 227808 **DIRECT**: hash `00000006a19c73eb72812d5c1af9a3a7c2a41e268368cc125b770d92027bbb84`,
  bits `1d0fb6d7`.
- Y mined 227808 **AuxPoW**: hash `5481ecf2f8db787901fc418ade15fbb9910e6bd3e12de158690c52eb65327fd4`,
  bits `1d0fb6d7`.
- Both independently required and used **`1d0fb6d7`** (same real ASERT anchor); chainwork at this
  step was exactly equal on both branches (same bits).

**Extended to strictly unequal accumulated work**: X mined 227809 direct (X total: 2 direct blocks,
chainwork `...2e45f80fc0a4495a7b`). Y mined 227809 AuxPoW then 227810 direct (Y total: 2 AuxPoW + 1
direct = 3 blocks, chainwork `...2e45f80fc0b4495b7b`). Before reconnect: **Y strictly greater
chainwork than X by 268,435,712 work units** (confirmed via real `int(chainwork,16)` comparison, not
block count).

**Real P2P reconnect and reorg** (X and Y connected via `addnode`, no restart):
- Fork height: **227807** (`000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f`).
- X's old tip: `0000000377ebd3464a61883a2d9bc4b74dcba6814b9c7707b2ae13ef98a21e2f` (height 227809, chainwork `...2e45f80fc0a4495a7b`).
- Y's winning tip: `00000001b59d0d1e709cc52427ab9719a172dfa41ec49e5fece911ac44e50aa7` (height 227810, chainwork `...2e45f80fc0b4495b7b`).
- Disconnected blocks (X's own): `00000006a19c73eb...` (227808 direct), `0000000377ebd346...` (227809 direct).
- Connected blocks (Y's): `5481ecf2f8db7879...` (227808 AuxPoW), `01ff1e29e0e580e0...` (227809 AuxPoW), `00000001b59d0d1e...` (227810 direct).
- Final: `X.besthash == Y.besthash == 00000001b59d0d1e...`; `X.chainwork == Y.chainwork == ...2e45f80fc0b4495b7b`.

**Real log evidence, captured from X's own `debug.log`** (default log level, no extra `-debug` flags
needed -- these lines are unconditional):
```
New manual peer connected: transport: v2, version: 70016, blocks=227810 peer=0
Saw new header hash=00000001b59d0d1e709cc52427ab9719a172dfa41ec49e5fece911ac44e50aa7 height=227810 peer=0
UpdateTip: new best=00000006a19c73eb... height=227808 version=0x20000000 ...   <- disconnecting X's 227809, back to X's own 227808
UpdateTip: new best=000000029b79bf25... height=227807 version=0x20000000 ...   <- disconnecting X's 227808, back to the shared fork point 227807
UpdateTip: new best=5481ecf2f8db7879... height=227808 version=0x42490100 ...   <- connecting Y's AuxPoW 227808 (VERSION_AUXPOW bit set in the logged version)
UpdateTip: new best=01ff1e29e0e580e0... height=227809 version=0x42490100 ...   <- connecting Y's AuxPoW 227809
UpdateTip: new best=00000001b59d0d1e... height=227810 version=0x20000000 ...   <- connecting Y's direct 227810
```
This is a genuine, real, **activation-crossing** reorg -- it disconnects a direct 227808/227809 pair
and connects an AuxPoW 227808/AuxPoW 227809/direct 227810 triple, stepping back through the exact
227807 legacy/ASERT boundary and forward again, entirely while X kept running (no restart). The `Saw
new header` line confirms X received and accepted Y's AuxPoW-versioned headers directly over the
upgraded P2P path during a real competing-branch event (not simple linear same-branch sync). Honest
scope note: this run did not pass `-debug=net`, so the underlying `getdata`/`block` wire messages
themselves were not separately logged; the unconditional `UpdateTip` sequence is still conclusive
proof that each block was fully downloaded, validated, and connected (a `CBlockIndex`'s tip can only
advance to it after that), just not raw-wire-message-level proof.

### 17.1 Restart-after-reorg (item 17)

Stopped X and Y cleanly, restarted independently: both retained the identical winning tip/chainwork.
`getchaintips` on X shows the active tip at 227810 **and** X's own former losing branch correctly
demoted to `{"height": 227809, ..., "branchlen": 2, "status": "valid-fork"}` (not vanished, not still
"active"). Y's two AuxPoW blocks (227808, 227809) re-read via `getblock` after restart: `VERSION_AUXPOW`
bit still correctly set, both fully readable, `nTx` intact -- no AuxPoW proof/serialization issue
survives restart. (`getchaintips` also lists many `valid-headers` entries at heights 225803-225828 --
these are pre-existing orphaned side-headers inherited from the original 10-way parallel canonical
mining run, sec.5/6, harmless and unrelated to this reorg.)

## 18. Reverse proof-mechanism dominance (item 7)

Fresh clones (`reorgrev-X`, `reorgrev-Y`), disconnected, with the composition **reversed** from sec.17:
this time the eventual *winner* is AuxPoW-heavy and has *more* blocks, and the loser is direct-heavy
with *fewer* blocks -- the opposite pairing from sec.17 (there, the winner Y was also AuxPoW-heavy;
here the letter and the "more blocks" side both flip relative to which one is being tested as
AuxPoW-heavy, decoupling any accidental pattern).

- X2 (winner-to-be): 227808 **AuxPoW**, 227809 **direct**, 227810 **AuxPoW** (3 blocks: 2 AuxPoW + 1 direct).
- Y2 (loser-to-be): 227808 **direct**, 227809 **AuxPoW** (2 blocks: 1 direct + 1 AuxPoW).
- Before reconnect: X2 chainwork `...2e45f80fc0b4495b7b` strictly greater than Y2's `...2e45f80fc0a4495a7b`
  by 268,435,712 work units.
- Reconnected Y2 to X2: **Y2 reorganized onto X2's branch.** Final: both at hash
  `26e9be7317c5832454f1fdc06da4c67f235e49a9a84c223eb070ffc126449099`, both chainwork
  `...2e45f80fc0b4495b7b`.
- Disconnected (Y2's own): `000000037fc9d3ea...` (227808 direct), `bdda442378c43928...` (227809 AuxPoW).
- Connected (X2's): `f9f7739180075ea0...` (227808 AuxPoW), `0000000f3e342c15...` (227809 direct), `26e9be7317c58324...` (227810 AuxPoW).

**Confirms fork choice follows accumulated chainwork regardless of which branch has more direct vs.
AuxPoW blocks** -- reversing which letter/composition wins from sec.17 changes nothing about the
underlying rule. Restart-after-reorg repeated for this pair too: both nodes retained the identical
tip/chainwork after an independent stop+restart.

## 19. Branch-local ASERT anchor test (items 9-12)

**Separate from sec.17's reorg test** -- this proves ASERT does not use a global/cached 227807 anchor,
using isolated branches (never used for any P2P reorg claim, per item 10's explicit caution).

**Constructing ALT** (goal-A construction only, real mining, no fabricated state): a disposable clone
of `golden-227807-pre-activation` had `invalidateblock` called on the real 227807 hash (rolled back to
227806, confirmed via `getblockchaininfo`), then mined a genuine, real, valid **alternative** 227807:
hash `0000000488e73ce9354cc379fb9b6c5483cd67247297c68ac1827ce3a9aa9937`, bits `1d0fffff` (same legacy
value -- same retarget period), real time `1790306794` (mined at real current wall-clock time, ~10.6
hours after the ORIGINAL's real `1790268674` -- a large, natural, undirected difference, not forced).
That construction node was stopped, and its resulting on-disk state was cloned into a **fresh** node
(`branch-alt`) that never itself called `invalidateblock` -- avoiding any ambiguity about residual
invalidity-marking state, per item 10.

| | ORIGINAL | ALT |
|---|---|---|
| 227807 hash | `000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f` | `0000000488e73ce9354cc379fb9b6c5483cd67247297c68ac1827ce3a9aa9937` |
| 227807 time | `1790268674` | `1790306794` |
| 227806 time (shared) | `1790268637` | `1790268637` |
| 227807 bits | `1d0fffff` | `1d0fffff` |
| timeDiff for 227808 ASERT calc | 37s | 38157s |

**Independently computed** (offline, `contrib/asert_reference.py`, before querying either real node):
ORIGINAL-ancestry ASERT nBits for 227808 = **`0x1d0fb6d7`**; ALT-ancestry ASERT nBits for 227808 =
**`0x1d0fffff`** (ALT's much larger real timeDiff, ~1.77 ASERT half-lives, pushes the target all the
way to the `powLimit` ceiling).

**Real node confirmation**: `getblocktemplate.bits` on branch-original = `1d0fb6d7` (matches); on
branch-alt = `1d0fffff` (matches). **The two branches' real nodes independently returned different,
correct, ancestry-specific values -- exactly matching each branch's own offline computation.**

### 19.1 Anchor-leakage check (item 11)

Queried in order A (ALT, then ORIGINAL) and, after independently restarting both nodes, in order B
(ORIGINAL, then ALT): each branch returned the **identical** `bits` value both times, regardless of
query order or the other branch's intervening queries/restarts --
`gbt_orig_1["bits"] == gbt_orig_2["bits"]` and `gbt_alt_1["bits"] == gbt_alt_2["bits"]`, both true.
**No global/cached anchor, no stale `CBlockIndex` reuse, no branch-insensitive ASERT state** -- each
result derives solely from that node's own active branch, exactly as `src/pow.cpp`'s
`pindexLast->GetAncestor(...)` (branch-relative by construction) predicts.

### 19.2 Real differing-anchor blocks mined (item 12)

A real 227808 was mined on **each** branch using **that branch's own** required bits: ORIGINAL ->
hash `0000000e5db822034e9b3e44e099890f369bb101a06f407c29a07eeea6cc1287`, bits `1d0fb6d7`, accepted. ALT
-> hash `000000006c56e227aff7ec146c4103a669e7e896448939f54e0887229f39e764`, bits `1d0fffff`, accepted.

**Wrong-bits subtest (optional, attempted and succeeded -- no invasive Core changes)**: constructed a
real, byte-exact-format segwit coinbase (BIP34 height push + dummy extranonce byte + P2WPKH payout +
the node's own real `default_witness_commitment`, matching the exact layout reverse-engineered from
this rehearsal's own captured real coinbase hex) for a candidate block with **prevhash = ALT's real
227807** but **bits = ORIGINAL's required value (`0x1d0fb6d7`)** instead of ALT's own correct
`0x1d0fffff`. Solved real PoW against that (deliberately wrong-for-this-ancestry, harder) target using
the parallelized solver, then submitted via `submitblock`.

**Result: `submitblock` returned `'bad-diffbits'`, and the node's state was completely unchanged**
(still exactly at ALT's real 227807, height/hash unaffected) -- a clean, unambiguous rejection for
precisely the deliberate defect, with no unrelated structural error, confirming the same `bad-diffbits`
mechanism observed against node D (sec.9.1) also fires correctly for an upgraded node given a
same-height-but-wrong-ancestry-bits block.

## 20. Chainwork verification (item 13)

Independently computed expected per-block chainwork as `2**256 // (target + 1)` from each block's own
real `bits` (the same formula `GetBlockProof()` implements) and compared against every real observed
chainwork delta across the mixed convergence sequence (sec.16):

| height | mechanism | bits | observed delta | independently computed | match |
|---|---|---|---|---|---|
| 227808 | direct | `1d0fb6d7` | 273317275 | 273317275 | **yes** |
| 227809 | AuxPoW | `1d0fffff` | 268435712 | 268435712 | **yes** |
| 227810 | direct | `1d0fffff` | 268435712 | 268435712 | **yes** |
| 227811 | AuxPoW | `1d0fffff` | 268435712 | 268435712 | **yes** |
| 227812 | direct | `1d0fffff` | 268435712 | 268435712 | **yes** |
| 227813 | AuxPoW | `1d0fffff` | 268435712 | 268435712 | **yes** |

**Direct vs. AuxPoW at identical bits**: Scenario A (direct, sec.9) and Scenario B (AuxPoW, sec.10),
both at `1d0fb6d7`, both independently compute to exactly 273,317,275 -- matching their real, identical
`chainwork` field exactly. **Proof mechanism contributes zero difference to work.**

**Harder-target proportionality**: `work(0x1d0fb6d7) / work(0x1d0fffff)` = **1.0181852**, matching the
independently predicted ASERT/anchor target ratio (sec.6.2, ~1.018185) to 6 significant figures.

**Fork choice follows summed chainwork, not block count**: every winner-determination in sec.17/18 was
computed via real `int(chainwork, 16)` comparison, never block count; sec.18 additionally varies which
side has more blocks vs. more work-per-block composition, and the accumulated-chainwork comparison
remains the correct and only predictor of the real reorg outcome in both directions.

## 21. Summary: this phase's expected conditions, all met

| condition | expected | observed |
|---|---|---|
| Mixed-proof A/B/C convergence | converges after every block | **converged after all 6/6 blocks** |
| Same-anchor activation-crossing reorg | more-work branch wins, crosses 227807 cleanly | **PASS** -- Y won, real log-verified disconnect/connect across the boundary |
| Reverse-composition reorg | more-work branch wins regardless of AuxPoW/direct mix | **PASS** -- X2 (AuxPoW-heavy) won |
| Direct/AuxPoW fork choice follows chainwork | never block count alone | **PASS** -- verified mathematically, sec.20 |
| Branch-local ASERT anchor selection | each branch computes its own anchor, no leakage | **PASS** -- differing nBits per branch, no leakage either query order |
| Restart-after-reorg (both reorg tests) | tip/chainwork persist, losing branch demoted not vanished, no AuxPoW corruption | **PASS** |

No consensus or RPC code was changed at any point in this phase. The one real bug found (sec.15's
onion-port collision) is rehearsal-lab infrastructure, not BitAIcoin consensus code. Per instruction,
this phase stops here: **not** proceeding to BIP34 height 227931, full fresh-node HEADERS sync,
pruning, or stabilization-checkpoint selection until this report is reviewed.

## 22. Pending sections (will be completed once BIP34/HEADERS/pruning/checkpoint work begins)

All tooling below is written and ready in `~/Downloads/bitaicoin-rehearsal-lab/` -- each script is a
real, runnable implementation (not a placeholder), verified to import/parse correctly. Everything
through sec.6-21 (the canonical 225823->227807 run, the 227807 snapshot, Scenario A/B at the real
activation boundary, and the full upgraded-node convergence/reorg/branch-local-ASERT phase) is now
real, complete, and PASS. **BIP34 height 227931, full fresh-node HEADERS sync, pruning, and
stabilization-checkpoint selection remain deliberately not started**, pending review of this report.
No result for any of the following exists yet; none will be fabricated or assumed:

| item | script | status |
|---|---|---|
| shared node/RPC config | `lab_common.py` / `phase2_common.py` | ready |
| 227807 snapshot capture + verification | sec.7 | **DONE -- PASS** |
| D pre-activation compatibility record | sec.7.1 | **DONE -- PASS** |
| ASERT anchor computation + legacy-vs-ASERT comparison for 227808 (corrected interpretation) | sec.6.2/6.2a | **DONE -- PASS** (computed, then live-confirmed by real `getblocktemplate` in sec.9 and again independently in sec.19) |
| Automated 1,984-block provenance verification | sec.6.3 | **DONE -- PASS** |
| Scenario A: first activation block, DIRECT | sec.9 | **DONE -- PASS** |
| Obsolete node D vs. Scenario A | sec.9.1 | **DONE** -- real rejection, `bad-diffbits` |
| Scenario B: first activation block, AuxPoW | sec.10 | **DONE -- PASS** |
| Obsolete node D vs. Scenario B | sec.10.1 | **DONE** -- real rejection, distinct reason (`header with invalid proof of work`) |
| Structural A vs. B comparison | sec.11 | **DONE -- PASS** |
| Three-upgraded-node mixed direct/AuxPoW convergence (A/B/C) | sec.16 (`run_convergence.py`) | **DONE -- PASS**, converged after all 6/6 blocks |
| Mixed-chain restart test | sec.16.1 | **DONE -- PASS**, no AuxPoW corruption |
| Same-anchor activation-crossing reorg | sec.17 (`run_reorg_same_anchor.py`) | **DONE -- PASS**, real log-verified disconnect/connect |
| Restart-after-reorg | sec.17.1 | **DONE -- PASS** |
| Reverse proof-mechanism-dominance reorg | sec.18 (`run_reorg_reverse.py`) | **DONE -- PASS** |
| Branch-local ASERT anchor test (incl. anchor-leakage check + wrong-bits subtest) | sec.19-19.2 (`run_branch_local_asert.py`, `wrong_bits_subtest.py`) | **DONE -- PASS**, including the optional wrong-bits `bad-diffbits` confirmation |
| Chainwork mathematical verification | sec.20 | **DONE -- PASS**, matches to 6+ significant figures |
| ASERT dynamic verification (real heights, independent reference) | `scenario_asert_compare.py` | superseded by sec.9's and sec.19's live confirmations; the original script remains available for a future standalone run if wanted |
| BIP34 boundary (227931) crossing | `scenario_bip34.py` | ready, PENDING run |
| Restart/reindex matrix | `scenario_restart_matrix.py` | ready, PENDING run at each required height |
| HEADERS-first synchronization (fresh-node milestone) | `scenario_headers_sync.py` | ready, PENDING run -- explicitly deferred (item 14 of this phase's instructions): sec.17's reorg already exercised real AuxPoW header propagation during a competing-branch event, but not yet the dedicated fresh-node HEADERS-first milestone |
| Pruning rehearsal | `scenario_pruning_plan.py` | ready; includes an honest feasibility check against the real 550 MiB prune floor, with `feature_auxpow_prune.py` as the documented fallback authority if infeasible at these heights |
| Obsolete-node (D) further divergence | `scenario_obsolete_node_D.py` | D's behavior at the pre-activation boundary (sec.7.1) and against both proof mechanisms at the activation boundary (sec.9.1/10.1) is now recorded; per instruction (item 16 of this phase), D was deliberately not involved further in the upgraded-node convergence/reorg experiments -- that evidence was judged sufficient for now |
| Upgrade gate statement | (sec.15 of the original spec) | PENDING |
| Post-activation stabilization checkpoint candidate | (sec.16 of the original spec) | PENDING |
| Discrepancies found, if any | | none found in consensus code; mining-harness/rehearsal-lab bugs found and fixed are documented in sec.5 and sec.15 (onion-port collision); the legacy-vs-ASERT divergence at 227808 (sec.6.2) and both obsolete-node rejections (sec.9.1/10.1) are expected designed behavior, not discrepancies |

Note on sec.9/10: the existing `scenario_A_direct.py`/`scenario_B_auxpow.py` scripts (written earlier
in the rehearsal, sec.7 of the pending table before this revision) mine extra continuity blocks
(227809/227810) and do not perform a pre-mining `getblocktemplate`/`createauxblock` assertion gate.
Per explicit instruction for this specific run, sec.9/10 were executed via new, narrower scripts
(`run_scenario_b.py` for B; direct RPC calls for A) that perform exactly the specified pre-mining
assertions and stop after exactly one block. The original two scripts remain available, unmodified,
for a future full continuity/multi-block run if wanted.

**No result for any item still marked PENDING above exists yet. None will be fabricated or assumed.**
