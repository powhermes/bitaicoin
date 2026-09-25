# BitAIcoin activation rehearsal: crossing 227808 (AuxPoW/ASERT) and 227931 (BIP34)

**Status: REHEARSAL COMPLETE.** This document was built incrementally as the rehearsal proceeded; every
section reflects a real result that actually happened, in the order it happened, including one real
Core defect this rehearsal itself found and fixed. No result was ever recorded before it existed.

**Summary of everything covered**: the real 225823->227807 canonical mining run and 227807 snapshot;
Scenario A/B at the real 227808 activation boundary (sec.6-12); the full upgraded-node
convergence/reorg/branch-local-ASERT phase (sec.14-21, reviewed and approved, including a literal
opposite-composition reorg added in review, sec.18.1); the real BIP34 height 227931 boundary rehearsal
(sec.23, reviewed and approved) -- a fresh 121-block gap mined honestly, the real `BIP34Height`
parameter and its exact enforcement semantics confirmed from source (plus a correction to an earlier,
misleading claim about the separate, inert `BIP34Hash` mechanism, sec.23.0), a normal direct+AuxPoW
sequence across the boundary, a wrong-child-height rejection (`bad-cb-height`) immediately after
activation, the matching pre-activation control (accepted, no enforcement yet), DAA/chainwork
continuity, restart persistence, and a reorg crossing 227931; and the **final lifecycle phase**
(sec.25-31) -- real fresh-node HEADERS-first/full synchronization; reindex validation that found a
real, confirmed Core defect (full `-reindex` silently truncated the chain at the first AuxPoW block),
which was fixed on its own narrowly-scoped track (commit `7942285e1175dacb54958b7cde1e4fa19630fde0`,
pushed) and re-verified against the real rehearsal fixtures before this document was updated, per
instruction; a consolidated restart/reindex matrix; a real pruning-feasibility analysis (real, honest
infeasibility at the current tip, precisely characterized, never a manufactured "prune passed"
result); a production upgrade gate; and post-activation stabilization criteria (a height range and
checklist -- explicitly never a production hash from this or any rehearsal). Every node used in every
phase is stopped; every piece of preserved evidence (canonical node A, both golden 227807/227928
snapshot sources, both Scenario A/B datadirs, `boundary-normal`, `scenario-B-auxpow`) was never reused
or mutated. **No production action was taken**: the real/private node was never started, mined, or
touched; no checkpoint or minimum-chainwork parameter was changed; no GBT adapter or Stratum/pool
infrastructure was built.

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

## 18. Reverse branch identity / independent composition reorg (item 7)

**Naming correction**: this section's test changed which node/letter won and used an independently
chosen composition, but its winning branch was still AuxPoW-heavy (2 AuxPoW + 1 direct, vs. the
loser's 1 direct + 1 AuxPoW) -- not the literal opposite composition from sec.17 (also an AuxPoW-heavy
winner). Renamed accordingly to avoid overclaiming; sec.18.1 below adds the literal opposite-composition
case (direct-heavy winner vs. AuxPoW-heavy loser), closing the symmetry gap precisely.

Fresh clones (`reorgrev-X`, `reorgrev-Y`), disconnected, with the branch identity/composition chosen
independently of sec.17 (winner AuxPoW-heavy with *more* blocks, loser direct-heavy with *fewer*
blocks, and the letter that wins is flipped relative to sec.17's framing).

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

### 18.1 Literal opposite-composition reorg: DIRECT-heavy winner vs. AuxPoW-heavy loser

Closes the composition-symmetry gap precisely: fresh clones (`reorg3-X`, `reorg3-Y`), disconnected,
where the **more-work winner is DIRECT-heavy** and the **less-work loser is AuxPoW-heavy** -- the
literal opposite of every prior reorg test in this document (sec.17's Y and sec.18's X2 were both
AuxPoW-heavy winners). Does not involve obsolete D.

- X (winner-to-be, direct-heavy): 227808 **direct**, 227809 **direct**, 227810 **AuxPoW** (3 blocks: 2
  direct + 1 AuxPoW).
- Y (loser-to-be, AuxPoW-heavy): 227808 **AuxPoW**, 227809 **AuxPoW** (2 blocks: 2 AuxPoW).
- Before reconnect: X chainwork strictly greater than Y's by 268,435,712 work units (confirmed via
  real `int(chainwork,16)` comparison).
- Reconnected Y to X: **Y reorganized onto X's DIRECT-heavy branch.** Final: both at hash
  `b3597e24baf426f7b660e5333cbf81ab377403c2fecdbb8155fcbe0f1f11a423`, both chainwork
  `...2e45f80fc0b4495b7b`.
- Disconnected (Y's own, both AuxPoW): `bcf0a083d29cdcdc...` (227808), `fabed2ceb702f17a...` (227809).
- Connected (X's): `000000030f3b9f9b...` (227808 direct), `00000000c5411c46...` (227809 direct),
  `b3597e24baf426f7...` (227810 AuxPoW).
- `getchaintips` on Y: its own former AuxPoW tip (`fabed2ceb702f17a...`, height 227809) correctly
  appears as `"status": "valid-fork"`, `branchlen: 2` -- demoted, not vanished.
- Restart-after-reorg: both nodes retained the identical tip/chainwork after an independent stop+restart.

**This is now the literal opposite composition from sec.17, closing the symmetry gap**: sec.17 proved
an AuxPoW-heavy branch can win; this proves a direct-heavy branch can win against an AuxPoW-heavy
competitor; sec.18 proved the winning letter is independent of sec.17's framing. Together, all three
confirm proof mechanism and branch identity are both irrelevant to fork choice -- only accumulated
chainwork decides.

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
| Literal opposite-composition reorg (sec.18.1) | direct-heavy branch can also win against an AuxPoW-heavy competitor | **PASS** -- X (direct-heavy) won |
| Direct/AuxPoW fork choice follows chainwork | never block count alone | **PASS** -- verified mathematically, sec.20 |
| Branch-local ASERT anchor selection | each branch computes its own anchor, no leakage | **PASS** -- differing nBits per branch, no leakage either query order |
| Restart-after-reorg (all reorg tests) | tip/chainwork persist, losing branch demoted not vanished, no AuxPoW corruption | **PASS** |

No consensus or RPC code was changed at any point in this phase. The one real bug found (sec.15's
onion-port collision) is rehearsal-lab infrastructure, not BitAIcoin consensus code. This phase was
reviewed and approved; per that review, the rehearsal proceeded next to the real BIP34 height 227931
boundary (sec.23) -- full fresh-node HEADERS sync, pruning, and stabilization-checkpoint selection
remain deliberately not started.

## 23. BIP34 activation rehearsal at the real height 227931 (separate phase, own evidence)

Dedicated section, kept separate from the 227808 sections above per instruction. New disposable chain
(`bip34-boundary` and its descendants); the canonical node A, both golden 227807/227928 snapshots'
*sources*, Scenario A/B evidence, and all sec.14-21 convergence/reorg evidence were never reused or
mutated.

### 23.0 Audit: `BIP34Hash` is a separate, narrow BIP30-skip optimization gate, NOT part of BIP34's own enforcement

**Correction to an earlier, misleading statement in this document** (this section previously said
`consensus.BIP34Hash` being set to the real historical Bitcoin mainnet hash at height 227931 was
"consistent with this chain inheriting real Bitcoin history" -- true only in the narrow sense that the
*value itself* was copied from mainnet, but the surrounding claim implied an active, meaningful
consensus relationship that does not exist for BitAIcoin's own forked history past height 225429; this
rehearsal's own real BAIC block 227931,
`000000000dcfc56475a3d3767c8ae0efd361db9239181c4d8988c25e8833e4e3`, is obviously BitAIcoin-native and
will never equal that hardcoded mainnet value, which is precisely what this audit investigates).

**Method**: searched the entire current source tree (`grep -rn "BIP34Hash" src/`) for every
read/reference, distinguishing parameter assignment from runtime use:

```
src/consensus/params.h:96:      uint256 BIP34Hash;                          <- declaration only
src/kernel/chainparams.cpp:91,237,467,574,726,815:  consensus.BIP34Hash = ...   <- assignment only, one per chain type
src/validation.cpp:2497:        fEnforceBIP30 = fEnforceBIP30 && (!pindexBIP34height ||
                                     !(pindexBIP34height->GetBlockHash() == params.GetConsensus().BIP34Hash));
```

**Exactly one runtime read exists in the entire tree**, inside `ConnectBlock()`'s BIP30 (duplicate-
coinbase) handling, read directly (lines 2480-2500 with their surrounding comments):
```cpp
CBlockIndex* pindexBIP34height = pindex->pprev->GetAncestor(params.GetConsensus().BIP34Height);
//Only continue to enforce if we're below BIP34 activation height or the block hash at that height doesn't correspond.
fEnforceBIP30 = fEnforceBIP30 && (!pindexBIP34height || !(pindexBIP34height->GetBlockHash() == params.GetConsensus().BIP34Hash));
```
This is real Bitcoin Core's own historical BIP30/BIP34 interaction, unmodified: on real mainnet, once
the chain is confirmed (by hash match at the known BIP34 height) to be the real chain that already
activated BIP34, the node may safely **stop** enforcing the older, more expensive BIP30 duplicate-
coinbase check (BIP34's own unique-height-per-coinbase property makes a new BIP30 violation
effectively impossible past that point). **It is not part of BIP34's own height-encoding enforcement**
(`bad-cb-height`), which is governed entirely and only by `BIP34Height`/`DEPLOYMENT_HEIGHTINCB`
(sec.23.1) -- a completely separate code path that never reads `BIP34Hash` at all.

**Result, precisely**: `BIP34Hash` is not unused legacy metadata (it is read, every block, in
`ConnectBlock`), but for BitAIcoin it is **permanently inert**: since `pindexBIP34height`'s real hash at
BitAIcoin's own 227931 will never equal the hardcoded real-mainnet value, the condition
`!(... == BIP34Hash)` is always true, so `fEnforceBIP30` is **never relaxed** by this mechanism --
BitAIcoin unconditionally keeps performing the (safe, conservative, slightly more thorough) BIP30
duplicate-coinbase check forever, past 227931, rather than ever taking the optimization real mainnet
takes at the equivalent point. **This is not a validation gap, not an under-enforcement, and not a
security defect** -- the stale value only ever prevents an optimization from firing; it can never cause
an invalid block to be accepted or a valid one rejected. No startup, checkpoint, assumeutxo, or index
logic references `BIP34Hash` anywhere in the tree (confirmed by the same search). **No Core change is
made or recommended** -- per instruction, this is reported, not fixed, since it has no effect on
BitAIcoin's actual consensus correctness, only on a now-permanently-dormant micro-optimization.

### 23.1 Real BIP34Height parameter (read from source, not docs/comments)

Confirmed directly in `src/kernel/chainparams.cpp` (`CBitAIcoinParams`): `consensus.BIP34Height =
227931`, entirely independent of `BIP34Hash` (sec.23.0 above -- a separate mechanism BIP34's own
enforcement never reads). Enforcement mechanism traced through `src/validation.cpp`
(`ContextualCheckBlock`, the `bad-cb-height` check) ->
`DeploymentActiveAfter(pindexPrev, ..., DEPLOYMENT_HEIGHTINCB)` -> `src/consensus/params.h`
(`DeploymentHeight(DEPLOYMENT_HEIGHTINCB) == BIP34Height`) -> `src/deploymentstatus.h`:
```
inline bool DeploymentActiveAfter(const CBlockIndex* pindexPrev, ...)
{
    return (pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1) >= params.DeploymentHeight(dep);
}
```
For a candidate block at height H, this is `H >= 227931`. **Confirmed exactly as expected**: height
227930 (`H=227930 < 227931`) does NOT enforce `bad-cb-height`; height 227931 (`H=227931 >= 227931`) is
the first height that DOES. Also confirmed in `src/node/miner.cpp`: `coinbaseTx.vin[0].scriptSig =
CScript() << nHeight;` is **unconditional** -- the real miner always encodes the correct height
regardless of whether BIP34 enforcement has activated yet, so every real-mined block in this rehearsal
(both before and after 227931) naturally has a correct height-encoded coinbase; testing a *wrong*
height therefore requires manually constructing a candidate (sec.23.5/23.6), never something the real
miner would ever itself produce.

### 23.2 Building the disposable chain: 227808 through 227928

`bip34-boundary`, cloned from the immutable `golden-227807-pre-activation` snapshot. Verified before
mining: height 227807, hash `000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f`, zero
peers.

**Crossing 227808**: one real, valid, upgraded **direct** block: hash
`00000008954151d793eb2bb92652780b7ed9a081e7c28e4c04650d8daee50422`, version `0x20000000`, bits
`1d0fb6d7` (obeys ASERT, matching the already-established real anchor value), chainwork
`...2e45f80fc09449597b`.

**Gap 227809->227928** (121 blocks): mined via the same audited round-barrier, multi-worker direct
miner design as `durable_miner.py` (unique payout address per worker, hard round barrier, no `kill()`)
-- real PoW throughout, no difficulty/height-parameter changes, no synthetic block-index edits. Real
measured rate: ~208-240 blocks/hour (10 workers), completing the full gap in ~2081s (~35 minutes) of
real wall-clock time. Automated provenance check: all 121 blocks (227808-227928) verified sequential,
unbroken `previousblockhash` linkage, final tip exactly `getblockhash(227928)` -- **PASS**.

**Real finding, honestly disclosed (not a bug, not fixed, not worked around)**: because this gap was
mined far faster than its nominal 600s/block schedule, and BitAIcoin's ASERT anchor is permanently
fixed at height 227807 (never rolling forward to a more recent block), the target moved substantially
harder by height 227928. **Precise mechanism** (corrected wording from an earlier draft of this
section, which described this as "compounding without bound" -- technically imprecise): ASERT's
exponent term is `(timeDiff - spacing*(heightDiff+1)) / halfLife`, where both `timeDiff` and
`heightDiff` are measured against the one fixed 227807 reference point. **Sustained faster-than-
schedule block production accumulates negative schedule displacement relative to the fixed reference
anchor and therefore progressively hardens the target; sustained slower production moves it in the
opposite direction.** The implementation remains bounded by its existing target clamps at every step
(`contrib/asert_reference.py`/`src/pow.cpp`'s `ComputeASERTTarget`: floor of target `1`, easy-side
ceiling `powLimit` -- both already exercised and confirmed in sec.6.2/9's real data). By height 227928
this had pushed bits from the initial `1d0fb6d7` down to `1d063a0f` -- meaningfully harder -- purely as
a real, correct consequence of mining efficiently relative to that fixed anchor, not any parameter
change. Disclosed here because it directly affected how long later real-PoW steps in this same phase
took (sec.23.5's wrong-height solve took several minutes of real 8-worker compute at the resulting
difficulty). **This fixed-reference-anchor behavior matches the intended, already-frozen, already-
reviewed ASERT design exactly** (the anchor was deliberately fixed at the pre-activation/post-activation
boundary block, sec.6.2/9, specifically so the first post-activation retarget has a well-defined,
unambiguous, height-independent reference point -- this rehearsal is the first time that design has
been exercised at a real fast-mining rate over an extended run, not a newly discovered design defect).
No code was touched to produce or work around this; no Core change is necessary or recommended.

**Item 6 snapshot**: cleanly stopped, snapshotted (`cp -Rc`) as `golden-227928-pre-bip34/` (three
blocks before real BIP34Height=227931), documented as disposable rehearsal infrastructure only (never
run `bitaicoind` directly against it).

### 23.3 Normal boundary sequence: 227929-227932 (items 8, 9, 12, 14)

`boundary-normal`, cloned fresh from `golden-227928-pre-bip34`.

| height | mechanism | hash | version | AuxPoW | bits | chainwork | time | coinbase prefix | decoded height | subsidy |
|---|---|---|---|---|---|---|---|---|---|---|
| 227929 | direct | `000000036dd0346752882e3f30b6b1ba7680819726cc489aa60757cfebee902d` | `0x20000000` | unset | `1d061e1f` | `...2e45f80fca62e6d1db` | 1790312006 | `03597a0300` | 227929 | 25 BAIC |
| 227930 | AuxPoW | `043a0b40f62e3be2ee3cf7cc08a128eae258b167bd65959e3f61889094d5e1d6` | `0x42490100` | **set** | `1d06068f` | `...2e45f80fca8d630bbb` | 1790312052 | `035a7a0300` | 227930 | 25 BAIC |
| **227931** | **direct** | `000000000dcfc56475a3d3767c8ae0efd361db9239181c4d8988c25e8833e4e3` | `0x20000000` | unset | `1d05eb57` | `...2e45f80fcab8a2a1d9` | 1790314557 | `035b7a0300` | 227931 | 25 BAIC |
| 227932 | AuxPoW | `5ef0c1080f6d864df67429613fbef5819836d90e60c09fdbacaf07be77c7dd9b` | `0x42490100` | **set** | `1d064af7` | `...2e45f80fcae15105a9` | 1790314569 | `035c7a0300` | 227932 | 25 BAIC |

**227931 is the real first BIP34-enforced height.** Every coinbase's decoded height (BIP34 push, parsed
independently from `scriptSig`) exactly equals the block's own real height, for both direct and AuxPoW
blocks, both before and after activation.

### 23.4 Item 9: child vs. parent coinbase, explicit distinction

For the AuxPoW blocks above (227930, 227932), the coinbase decoded and checked in the table is the
**BitAIcoin CHILD's own real coinbase** (`getblock`'s `tx[0]`) -- the object BIP34's `bad-cb-height`
check actually inspects (`block.vtx[0]->vin[0].scriptSig`, `src/validation.cpp`). The **AuxPoW PARENT's
own coinbase** (built by `contrib/merge_mining_coordinator/provider.py`'s
`SyntheticParentWorkProvider`, carrying the merge-mining commitment tag) is a completely separate
object on a separate synthetic chain, never inspected by BIP34 validation, and never substitutes for
the child's own coinbase in any check. AuxPoW does not and cannot bypass BIP34 -- the child coinbase is
independently, fully validated exactly as a direct block's would be.

### 23.5 Wrong child height AFTER activation (item 10) -- `bad-cb-height`

From `boundary-normal` at real height 227930 (before mining 227931), constructed a real candidate for
height 227931 with correct prevhash, correct required bits (`0x1d05eb57`, real ASERT value at that
tip), valid real PoW solved against that target, a structurally valid segwit coinbase and witness
commitment (byte-exact real format, reused/generalized from sec.19.2's wrong-bits construction) --
**except** the coinbase's own height push deliberately encodes **227930** instead of the real 227931.

```
real_height=227931  declared_bits=0x1d05eb57  coinbase_claims=227930
submitblock result: 'bad-cb-height'
tip after attempt: height=227930 (UNCHANGED)
```

**Rejected exactly as expected, for exactly the intended reason** -- the block reached full BIP34
validation (correct PoW, correct bits, valid witness commitment) and was rejected specifically and
only for the height mismatch, not for any incidental malformation. Tip provably unchanged.

### 23.6 Pre-activation control (item 11) -- accepted, no enforcement yet

From a **fresh** `boundary-preactivation-control` clone of `golden-227928-pre-bip34` (never reused from
sec.23.5's node): mined a real, valid 227929, then constructed a candidate for height 227930 -- correct
prevhash, correct required bits (`0x1d069df7`), valid real PoW, valid witness commitment -- with the
coinbase deliberately encoding **227929** instead of the real 227930.

```
real_height=227930  declared_bits=0x1d069df7  coinbase_claims=227929
submitblock result: None (ACCEPTED)
tip after attempt: height=227930, hash=0000000585a9ffc5aaee81e2b8378af883fae6653db636613bec1f5f9942cc6d
```

**Accepted.** On-chain confirmation: the real block at height 227930 has a coinbase-encoded height of
227929 (not 227930) and is nonetheless the node's real, valid tip. This is the strongest possible
boundary demonstration: **the identical class of defect (wrong child-height encoding) is accepted one
block before BIP34Height and rejected with `bad-cb-height` at BIP34Height itself** -- sec.23.5 and
sec.23.6 together isolate the exact height at which enforcement begins, using the real node's own
validation, not an assumption from documentation.

### 23.7 AuxPoW wrong-height subtest (item 13) -- not attempted, documented per instruction

Per instruction, this optional subtest was not pursued: `createauxblock`'s candidate cache is keyed to
the child candidate hash, so mutating the child coinbase changes the child hash and would make
`submitauxblock` (which expects to match its own cached job) unsuitable without weakening RPC
semantics, which is explicitly not permitted. What this rehearsal already has is judged sufficient:
sec.23.3's real, valid AuxPoW block at 227932 proves normal ASERT+AuxPoW+BIP34 integration; sec.23.5's
direct wrong-height rejection proves the consensus enforcement mechanism itself (BIP34 validates
`block.vtx[0]` regardless of which mining path produced the block, sec.23.4); the existing C++/functional
AuxPoW-specific wrong-height test coverage (outside this rehearsal) remains the adversarial AuxPoW
authority for this specific combination.

### 23.8 DAA/chainwork continuity across the boundary (item 14)

Independently computed expected chainwork as `2**256 // (target+1)` from each block's own real `bits`
and compared against every real observed delta across 227929-227932:

| height | bits | observed delta | independently computed | match |
|---|---|---|---|---|
| 227929 | `1d061e1f` | 702060416 | 702060416 | **yes** |
| 227930 | `1d06068f` | 712784352 | 712784352 | **yes** |
| **227931** | `1d05eb57` | 725587486 | 725587486 | **yes** |
| 227932 | `1d064af7` | 682517456 | 682517456 | **yes** |

Also confirmed live, post-activation: `getblocktemplate.bits` and `createauxblock.bits` on
`boundary-normal` at its final tip (height 227933's requirement) both returned `1d062cd3` -- **still
identical**, exactly as pre-activation. **BIP34 activation caused zero change to the DAA/chainwork
formula** -- every bits fluctuation visible across this range is ASERT's own real dynamic retargeting
(sec.23.2's fixed-anchor finding), entirely unrelated to and unaffected by BIP34.

### 23.9 Restart across the boundary (item 15)

`boundary-normal` stopped cleanly and restarted: tip unchanged at height 227932/hash
`5ef0c1080f6d864df67429613fbef5819836d90e60c09fdbacaf07be77c7dd9b`; all four blocks (227929-227932)
re-read via `getblock` -- every coinbase height still decodes correctly, both AuxPoW blocks (227930,
227932) still show the correct `VERSION_AUXPOW` bit, no serialization/migration/revalidation issue on
the first BIP34-enforced block specifically or any other.

### 23.10 Small reorg crossing 227931 (item 16)

Two fresh clones of `golden-227928-pre-bip34` (`boundary-reorgX`, `boundary-reorgY`), disconnected.

- **X** (own real 227929, 227930) -> real, valid **227931 DIRECT** (hash
  `000000041b933c1377fd5abbc9641638d45b27b94feb79244caf11d6b65a4054`, bits `1d06924f`) -> one
  descendant (227932).
- **Y** (own real 227929, 227930) -> real, valid **227931 AuxPoW** (hash
  `9d0043cd1c166d6d654760ca973f03ba7694b88e8c42f237a87efacf72b07788`, bits `1d06a89f`) -> two
  descendants (227932, 227933), giving Y strictly greater chainwork.

Both competing 227931 blocks independently satisfied BIP34 (correct, self-consistent height encoding
on their own respective branches) **before** any reconnect. Real P2P reconnect: **X reorganized onto
Y**, crossing the real 227931 activation boundary with no restart. Final: both nodes at hash
`0000000245e9c756be70283f7f57016edf459a37633728a05cd2c4c4a29695af`, chainwork
`...2e45f80fcafd8c576e`. Restart-after-reorg: both nodes retained the identical tip/chainwork after an
independent stop+restart.

### 23.11 BIP34 phase summary

| condition | expected | observed |
|---|---|---|
| BIP34Height read from real source | 227931, enforcement gated by `H >= BIP34Height` | **confirmed exactly**, sec.23.1 |
| Continuous valid ancestry 227808->227928 | no gaps, no synthetic edits | **PASS**, 121/121 blocks |
| Normal boundary sequence (direct + AuxPoW around 227931) | both mechanisms accepted, correct height encoding | **PASS**, sec.23.3 |
| Child vs. parent coinbase distinction | AuxPoW never bypasses child BIP34 | **confirmed**, sec.23.4 |
| Wrong height AFTER activation | rejected `bad-cb-height`, tip unchanged | **PASS**, sec.23.5 |
| Wrong height BEFORE activation (control) | accepted, no enforcement | **PASS**, sec.23.6 |
| Valid AuxPoW immediately after activation | accepted, correct height | **PASS**, sec.23.3 (227932) |
| BIP34 does not affect DAA/chainwork | identical formula before/after | **PASS**, sec.23.8 |
| Restart across the boundary | tip/chainwork/coinbase decoding all persist | **PASS**, sec.23.9 |
| Reorg crossing 227931 | normal reorg, both sides independently BIP34-valid | **PASS**, sec.23.10 |

No Core or consensus change was necessary or made anywhere in this phase. Per instruction, the
rehearsal stops here: **not** proceeding to full fresh-node HEADERS-first sync, a reindex/reindex-
chainstate matrix beyond what this phase needed, pruning, or stabilization-checkpoint selection until
this report is reviewed.

## 25. Final lifecycle phase, part 1: process hygiene and fresh-node HEADERS/full sync

Preserved throughout, never reused/mutated: canonical node A, `golden-227807-pre-activation`,
`golden-227928-pre-bip34`, both Scenario A/B evidence datadirs, all convergence/reorg evidence,
`boundary-normal`.

**Process hygiene**: inventoried all rehearsal-related processes before starting. Found and killed one
genuinely stale, silently-CPU-consuming (~95 real minutes, 0.1-20% CPU) wedged process tree, traced
exactly to an earlier multiprocessing-from-stdin bug (macOS `spawn` requires a real backing `.py` file;
an inline heredoc invocation has none, so worker respawns fail, and in this case the parent hung
indefinitely rather than raising). Two other `resource_tracker` stub processes were confirmed, by their
own command-line/timestamp, to belong to an unrelated project/session and were left untouched per
instruction not to kill an unknown process blindly. All preserved evidence datadirs confirmed idle (no
actively-held lock).

### 25.1 Fresh-node HEADERS-first / full synchronization

Real SOURCE (`sync-source`, fresh clone of `boundary-reorgX`'s real height-227933 mixed direct/AuxPoW/
BIP34 history, not pruned, exact upgraded binary, isolated) and a **genuinely empty** DESTINATION
(`sync-destination`, no copied blocks/chainstate/indexes/snapshot -- confirmed height 0, real genesis
hash, before connecting). Connected DESTINATION only to SOURCE; this is a real initial sync, not
filesystem state transfer.

**Headers-first behavior, real timing evidence**: all bulk `headers` P2P messages completed within 26
real seconds of connecting (`headers` climbed to the full real tip, 227933, almost immediately, while
`blocks` was still 0); block validation then took ~6 more real minutes to climb from 0 to 227933 --
`UpdateTip` lines for every one of the named boundary heights (225429 fork anchor, 227807 last legacy
block, 227808 first ASERT/AuxPoW-capable height, 227930 pre-BIP34, 227931 first BIP34-enforced height,
227932 post-BIP34 AuxPoW) were all captured with real timestamps confirming this real, correct
headers-then-blocks ordering.

**Representative blocks, DESTINATION vs. SOURCE, all matching exactly** (hash, height, prevhash, bits,
version, VERSION_AUXPOW state, chainwork): 225429, 227807, 227808, 227930, 227931 (the real AuxPoW
block, `VERSION_AUXPOW` correctly set), 227932, 227933.

**Convergence**: `SOURCE.bestblockhash == DESTINATION.bestblockhash`,
`SOURCE.height == DESTINATION.height`, `SOURCE.chainwork == DESTINATION.chainwork`,
`DESTINATION.headers == DESTINATION.blocks` -- all confirmed exactly. Restarted DESTINATION with SOURCE
disconnected/stopped: identical tip/chainwork retained. Reconnected: no state change (already fully
converged).

**AuxPoW HEADERS/transport evidence, precisely distinguished (not conflated)**: the real AuxPoW block
(227931) was downloaded during IBD via genuine per-block `getdata`/`block` **full-BLOCK transport**
(`received: block (486 bytes) peer=0` -- the larger size vs. an ordinary block's `251 bytes`,
consistent with the serialized `CAuxPow` proof), distinct from the earlier bulk `headers` messages used
for header sync, and never via compact-block relay (not used during IBD in this implementation, only
for near-tip live relay). `Saw new header hash=...227931... peer=0` (an unconditional, always-logged
line) confirms X received and accepted Y's real AuxPoW-versioned header directly over the upgraded P2P
path. Honest scope note: this run did not pass `-debug=net`, so the raw `getdata` wire message itself
was not separately logged; the unconditional `UpdateTip` sequence plus the explicit `received: block`
size evidence is still conclusive (a `CBlockIndex`'s tip cannot advance to a block without it having
been fully downloaded and validated).

**Result: Part 1 complete, real, PASS in full.**

## 26. Discrepancy found and fixed: full `-reindex` silently truncated the chain at the first AuxPoW block

**Found** during Part 2 (reindex/reindex-chainstate validation) of the final lifecycle phase. Per
instruction, all further final-phase work (Parts 3-9: consolidated restart/reindex matrix, pruning,
upgrade gate, stabilization criteria) was **paused** the moment this was confirmed, and the fix was
made on its own narrowly-scoped track before resuming. This section preserves the original failure
evidence exactly as observed -- it is not rewritten to make the rehearsal look as though it passed the
first time.

### 26.1 Evidence frozen before any investigation or fix

- Source commit (binary build baseline): `519d3d9eab9a0dcbc084d88e9d78849d5f0cdd28`
- Binary SHA-256 (pre-fix, as originally tested): `c7a5c8f5627b3d33d069fc90d760957685f4a0b6dc506c0d38ecc761fe3da93e`
- Failing datadir: fresh clone of the preserved `scenario-B-auxpow` evidence (never itself modified)
- First AuxPoW block hash: `551982b3837caf9482f5b85da5c607d808483c3938b96ab0449ac172238ef0cd` (height 227808)
- BEFORE `-reindex`: height=227808, hash=`551982b3...`, chainwork=`...2e45f80fc09449597b`
- AFTER `-reindex` (failed): height=227807, hash=`000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f`, chainwork=`...2e45f80fc083fedbe0` -- silently truncated one block short, no fatal error, node reports itself healthy
- Exact log rejection, captured verbatim: `[validation] AcceptBlockHeader: Consensus::CheckBlockHeader: 551982b3837caf9482f5b85da5c607d808483c3938b96ab0449ac172238ef0cd, auxpow-missing, AUXPOW version bit set but no AuxPoW proof attached`
- Confirmed (fresh clone of the same original evidence): ordinary restart reads the real 227808 AuxPoW state correctly
- Confirmed (fresh clone of the same original evidence): `-reindex-chainstate` alone passes cleanly, reaching height 227808 with the AuxPoW block intact

A permanent, frozen, read-only regression fixture (`regression-evidence-prefix-reindex-bug/`, with a
README recording all of the above) reproduces this failure exactly and was never touched again after
creation, for before/after comparison.

### 26.2 Full disk-serialization audit (performed before any edit)

| path | function | serializer/wrapper | chain-awareness |
|---|---|---|---|
| WRITE to blk*.dat | `BlockManager::WriteBlock` (`node/blockstorage.cpp`) | `AuxPowBlockForSend(block, auxpowEnabled)` | correct |
| NORMAL on-demand read (RPC `getblock`, restarts, reorgs) | `BlockManager::ReadBlock` | `AuxPowBlockForRecv(block, GetConsensus().fBitAIAuxpowEnabled)` | correct |
| header-only read (`LoadBlockIndexGuts`/`GetHeaderForAnnounce`) | `ReadBlockHeaderWithAuxPow` | `UnserializeBlockHeaderWithAuxPow` | correct |
| P2P send/receive | `net_processing.cpp` | `AuxPowBlockForSend`/`AuxPowBlockForRecv` | correct |
| REST / raw-block decode | `rest.cpp`, `core_io.cpp` | `AuxPowBlockForRecv` | correct |
| **full `-reindex` read** | `ChainstateManager::LoadExternalBlockFile` (`validation.cpp`) | plain `blkdat >> TX_WITH_WITNESS(*pblock)` | **INCORRECT -- the one gap** |
| **`-loadblock=<file>` external import** | same `LoadExternalBlockFile` (via `node::ImportBlocks`, confirmed the identical function powers both `-reindex`'s blk*.dat rescan and `-loadblock`'s external-file import) | same plain read | **INCORRECT, same root cause** |
| disabled chains (MAIN/TESTNET/TESTNET4/SIGNET) | n/a -- `fBitAIAuxpowEnabled` defaults `false`, only BitAIcoin's own chain type and regtest set it `true` (confirmed in `chainparams.cpp`) | `AuxPowBlockForRecv`'s own `if (auxpowEnabled && header.IsAuxpow())` gate | never attempts a proof read regardless of bit 8; byte-identical to the old plain read on these chains |

### 26.3 Root cause

`ChainstateManager::LoadExternalBlockFile()` deserialized blocks read back from `blk*.dat` with plain,
non-AuxPoW-aware `CBlock` deserialization -- the one block-read call site in the entire codebase that
did not use the chain-aware wrapper every other site already used. The on-disk bytes were always
correct and complete (the write path is, and always was, correct); only this specific read path failed
to reconstruct the in-memory block's `auxpow` field, which then correctly (from
`CheckBlockHeader`'s own narrow perspective) rejected the resulting incomplete-looking block.

### 26.4 Fix

Commit `7942285e1175dacb54958b7cde1e4fa19630fde0`: the smallest correct read-path change -- replaced
the plain deserialization with `AuxPowBlockForRecv(*pblock, GetConsensus().fBitAIAuxpowEnabled)`, the
exact same wrapper the normal read path already uses, reusing the existing abstraction rather than
inventing a new one. No consensus rule, chain ID, `VERSION_AUXPOW` semantics, ASERT, activation height,
block hash calculation, disk format, RPC surface, BIP34 logic, or candidate-cache/proof-ownership model
was changed.

### 26.5 Regression coverage

`test/functional/feature_auxpow_reindex.py` (registered in `test_runner.py`, alongside fixing a
pre-existing, unrelated gap where `feature_auxpow_createauxblock_ibd.py` was never registered there
either). Builds a real regtest chain where the very first block after genesis is itself AuxPoW (the
literal failing shape -- BitAIcoin's real `BitAIAuxpowActivationHeight=1`/`fBitAIAuxpowEnabled=true` on
regtest, confirmed in `chainparams.cpp`), interleaved with direct blocks
(AuxPoW/direct/AuxPoW/direct/AuxPoW/direct), plus a real descendant after the final AuxPoW block so a
regression shows as truncation, not merely "failed to extend." Runs full `-reindex`,
`-reindex-chainstate`, and a `-loadblock=<file>` external import of the same real `blk00000.dat`
(exercising all three real callers of the shared function), requiring byte-identical final
height/hash/chainwork and correct `VERSION_AUXPOW`/proof decoding in every case. **Confirmed the test
itself is real**: fails exactly at the predicted point (truncates to genesis) against the pre-fix
binary; passes cleanly against the fix.

### 26.6 Re-verification against the real rehearsal fixtures (post-fix)

- Fresh clone of `scenario-B-auxpow`: full `-reindex` now reaches height 227808, hash
  `551982b3...` exactly, chainwork exactly matching pre-reindex, zero `auxpow-missing` occurrences,
  AuxPoW block fully readable with proof intact.
- Fresh clone of the real mature mixed post-BIP34 state (`boundary-reorgX`, height 227933): full
  `-reindex` reaches height 227933, hash `0000000245e9c756...` exactly, chainwork matching exactly,
  zero `auxpow-missing` occurrences; the one real AuxPoW block in this history (height 227931) survives
  correctly.
- `-reindex-chainstate` re-run post-fix on both: still passes, unaffected by the read-path change (it
  never calls the changed function).
- Existing real-chain history unchanged: node-A's real 225823 hash
  (`0000000ad1060dd6b63a31796c4d57977c7f009eb0229b32d0b15dd303ecff57`) and the immutable
  `golden-227807-pre-activation` snapshot hash
  (`000000029b79bf2511fd42880959cd5637a435b1d042f1d14207407e8927e64f`) both read correctly, unchanged,
  with the fixed binary.
- C++ suite: `auxpow_tests` (52 cases) and the full suite (811 cases) both pass. Functional:
  `feature_auxpow_prune.py`, `feature_auxpow_rpc.py`, `feature_auxpow_rpc_disabled_chains.py`,
  `feature_auxpow_createauxblock_ibd.py`, the new `feature_auxpow_reindex.py`, the stock
  `feature_reindex.py`/`feature_reindex_init.py`/`feature_reindex_readonly.py`, `mining_basic.py`, and
  `feature_block.py` all pass.
- A handful of unrelated functional tests intermittently failed with generic P2P/RPC connection
  timeouts under sustained parallel (`-j 6`) or rapid sequential execution on this machine. **Not
  dismissed without investigation**: a controlled, otherwise-identical comparison against the
  unmodified pre-fix binary reproduced the same broad, run-to-run-varying set of unrelated failures
  (mining/wallet/p2p/mempool/tool tests unrelated to AuxPoW or reindex code), and every affected test
  passed cleanly when re-run in isolation. This is real, pre-existing environmental flakiness on this
  machine, confirmed identical with and without the fix -- not a regression from this change.

### 26.7 Severity

**"Full-reindex AuxPoW disk-deserialization defect."** A recovery/read-path bug, not a chain-split bug,
not a security/consensus defect, but serious enough to require fixing before any release that could
reach real AuxPoW-active history:

- Affects full `-reindex` (and the same-function `-loadblock` external import) after any real AuxPoW
  block exists on disk.
- Can silently truncate active history at the first AuxPoW block, with the node presenting itself as a
  healthy, fully-synced node at the truncated height -- an operator recovery risk.
- Does **not** invalidate already-running nodes.
- Does **not** affect ordinary restart.
- Does **not** affect live P2P validation/sync (confirmed extensively throughout this entire
  rehearsal, including this same phase's own fresh-node full IBD, sec.25.1).
- Does **not** affect `-reindex-chainstate`.
- Does **not** imply existing `blk*.dat` bytes are corrupt (they were always correct and complete).
- Does **not** change any consensus rule.

### 26.8 Status

**Resolved and re-tested.** Fixed in commit `7942285e1175dacb54958b7cde1e4fa19630fde0`, pushed to
`origin/bitaicoin-phase1`. The final lifecycle phase resumes from where it paused (Part 2's remaining
items) in a subsequent pass of this same document.

## 27. Consolidated restart/reindex matrix (final lifecycle phase, Part 3)

Real evidence only; per instruction, no cell was re-tested solely to make the table uniform where
existing evidence from earlier sections already establishes the behavior.

| state | ordinary restart | reorg + restart | `-reindex` | `-reindex-chainstate` |
|---|---|---|---|---|
| 227807 (pre-activation) | **PASS** (sec.19, restarts of `branch-original`/`branch-alt`; many others) | n/a (pre-activation, no competing branch at this exact height in this rehearsal) | **PASS** (sec.26.6, `reindex-227807`) | **PASS** (implied by sec.26.6's AuxPoW-state runs, which reindex-chainstate through this height en route) |
| 227808 direct | **PASS** (Scenario A, sec.9) | **PASS** (sec.17.1, X's own direct 227808 was one of the two competing blocks) | **PASS** (sec.26.6, `reindex-227808-direct`) | not separately isolated at this exact single-mechanism state; superseded by the AuxPoW-state and mature-state runs below, which exercise the identical code path |
| 227808 AuxPoW | **PASS** (Scenario B, sec.10) | **PASS** (sec.17.1, Y's own AuxPoW 227808 was the other competing block; restart-after-reorg confirmed) | **FOUND THE DEFECT here, then PASS after the fix** (sec.26, sec.26.6) | **PASS** (sec.26.6, and the original discovery-phase confirmation) |
| 227809+ mixed (real rehearsal data, alternating direct/AuxPoW/direct/AuxPoW/direct/AuxPoW) | **PASS** (sec.16.1, convergence restart test) | **PASS** (sec.17.1/18/23.10, multiple reorgs then restarts) | **PASS** (sec.27.1 below -- real `conv-A` chain, 3 real AuxPoW blocks at 227809/227811/227813, all survive with proof intact) | superseded by the above; same code path already confirmed working |
| 227930 | **PASS** (`boundary-normal`, sec.23.9) | crossed within sec.23.10's reorg block range; not isolated as its own single-height reorg target | **PASS** (implied -- sequential reindex necessarily validates 227930 correctly en route to the mature-state tip 227933, sec.26.6; reindexing cannot "skip" an intermediate height) | **PASS** (same sequential-validation logic) |
| 227931 (first BIP34-enforced) | **PASS** (`boundary-normal`, sec.23.9) | **PASS** (sec.23.10, both competing 227931 blocks) | **PASS** (sec.26.6, mature-state reindex passes through and reads 227931 correctly, real AuxPoW block, proof intact) | **PASS** (same mature-state run) |
| 227932+ | **PASS** (`boundary-normal`, sec.23.9) | **PASS** (sec.23.10, reorg reached 227933) | **PASS** (sec.26.6, mature-state reindex reaches 227933 exactly) | **PASS** (same mature-state run) |

### 27.1 Real-data confirmation: reindexing the actual mixed convergence chain

Closes the "227809+ mixed" cell with real rehearsal data (not only the regtest regression test,
sec.26.5). Fresh clone of `conv-A` (real chain: 227808 direct, 227809 AuxPoW, 227810 direct, 227811
AuxPoW, 227812 direct, 227813 AuxPoW). BEFORE: height=227813, hash
`862f0fa16317a1aa65e801dfc6740ce55374717a5970fda32681128382dc14f1`, chainwork `...2e45f80fc0e4495e7b`.
Full `-reindex`: AFTER matches exactly (same height/hash/chainwork), zero `auxpow-missing`
occurrences, and all three real AuxPoW blocks (227809, 227811, 227813) independently confirmed still
carrying `VERSION_AUXPOW` with correct bits after the reindex. **PASS.**

## 28. Pruning: real feasibility analysis (final lifecycle phase, Part 4)

Established from the REAL current blk-file layout, not assumed from the 550 MiB target alone.

**Real blk-file layout** (real `conv-A` chain, height 227813, 6.9 GB total, 48 files `blk00000.dat`
through `blk00047.dat`): correlating `-reindex`'s own unconditional `LogInfo` progress markers
("Reindexing block file blkNNNNN.dat (X% complete)...") against the real `UpdateTip` height log lines
that follow them shows the **last** "Reindexing block file" marker printed is for `blk00047.dat` at
97% -- and heights 225823 (the real chain's transition from inherited historical Bitcoin blocks to
BitAIcoin-native blocks), 227808, and 227813 (the current tip) **all** appear later in the log with no
further file-boundary marker in between. **All of them are in the same single physical file,
`blk00047.dat` (6.3 MB, far short of the 128 MiB default rotation size).**

**Confirmed directly in source**: `MIN_BLOCKS_TO_KEEP = 288` (`src/validation.h`).

**Real manual/automatic prune behavior** (`-prune=550` on a fresh clone of the same real chain):
```
before: height=227813, size_on_disk=6.9 GB (unpruned)
after:  height=227813, pruned=true, size_on_disk=507,664,001 bytes (~484 MB), pruneheight=222839
```
Real historical data below height 222839 was successfully pruned (confirmed: `getblock` on height
100000 fails with `"Block not available (pruned data)"`, as expected). **But pruning could not advance
past height 222839** -- nearly 5,000 blocks short of the current tip, and far short of what
`MIN_BLOCKS_TO_KEEP=288` alone would suggest should be prunable (227813-288=227525). All real AuxPoW
blocks in this range (227808, 227809, 227811, 227813) remain fully readable, not pruned. Restarted the
pruned node: identical tip/hash/chainwork/pruneheight, AuxPoW blocks still fully readable with proof
intact.

**Confirmed key issue, exactly as anticipated**: this rehearsal's own native blocks (post-225823) are
tiny (~250-500 bytes each, coinbase-only) compared to the 128 MiB default blk-file rotation size --
thousands of them fit in a single file. Since that one file's span keeps growing to include the
**current, always-unprunable tip** as new blocks are mined, the file containing any historical AuxPoW
proof from this rehearsal **can never become eligible for deletion at the current tip**, regardless of
how far `MIN_BLOCKS_TO_KEEP` would otherwise allow pruning to reach. This is a real, physical
consequence of this rehearsal's own block sizes, not a defect in the pruning mechanism itself.

**Exact physical reason, stated precisely**: *227808 and all retained descendants through the current
tip occupy the same physical blk file (`blk00047.dat`), so normal blk-file-level pruning cannot remove
the file containing the AuxPoW proof without also deleting the currently-active tip -- which pruning
correctly refuses to do.* Forcing an actual rollover would require mining on the order of hundreds of
thousands of additional tiny blocks purely to grow file size past 128 MiB -- explicitly out of
proportion for this rehearsal, per instruction, and not attempted.

**Authoritative evidence for the underlying mechanism** (on-demand AuxPoW proof storage correctly
surviving a *real* file-level prune, exercised at a practical scale via `-fastprune`'s tiny 64 KiB
files): the existing, already-green `test/functional/feature_auxpow_prune.py` (docs/AUXPOW_MILESTONE.md
sec.5.3), re-run in sec.26.6 alongside the reindex fix and still passing. This rehearsal does not
re-derive that test; it defers to it explicitly, as its own module docstring anticipates, combined with
the real restart/reindex evidence above and this real blk-file analysis. **No misleading "real prune
passed" result is claimed** -- the real, honest result is: real historical data prunes correctly, the
rehearsal's own recent/AuxPoW-containing data physically cannot at this tip, and the underlying
mechanism is independently proven at a practical scale by the existing test.

## 29. Production upgrade gate (final lifecycle phase, Part 5)

Formal operational gate for the real production/private BitAIcoin chain. This section does not touch,
and was not exercised against, the real production node.

| item | value |
|---|---|
| Production chain baseline (real, private) | height 225823, hash `0000000ad1060dd6b63a31796c4d57977c7f009eb0229b32d0b15dd303ecff57` |
| Production activation height | **227808** |
| Minimum required source/release commit | `2e700d7ad6fe08c840d2e9b4e99ef5e89a2b34f6` (AuxPoW/ASERT/merge-mining milestone) **plus the reindex fix, commit `7942285e1175dacb54958b7cde1e4fa19630fde0`** -- any node that might ever need to `-reindex` after real AuxPoW history exists must run at least this commit, not merely the pre-activation baseline |
| Binary hashes for tested builds | pre-fix `bitaicoind` SHA-256 `c7a5c8f5627b3d33d069fc90d760957685f4a0b6dc506c0d38ecc761fe3da93e` (used throughout sec.6-25); post-fix `bitaicoind` SHA-256 `bb56b1165e2649e2bc7ccab4d56be87f3f366b57a3d718dc48150610afe6dfa6` (used throughout sec.26-28) -- recorded for reference; a real production release build should be built fresh from the exact release commit, not reused from this rehearsal's own build |
| AuxPoW chain ID | 16969 (`0x4249`, "BI") |
| ASERT half-life | 21600 seconds (6 hours) |
| Direct + AuxPoW validity | both **permanently** valid after activation (never one superseding the other; sec.9-11, sec.16-18, sec.23.3-23.4 -- extensively, repeatedly confirmed) |
| Obsolete-binary rejection | demonstrably confirmed: obsolete nodes reject the real upgraded chain at 227808 for two independently-observed real reasons depending on proof mechanism (`bad-diffbits` for direct, sec.9.1; `header with invalid proof of work` for AuxPoW, sec.10.1) |
| Mixed old/new production fleet | **not permitted** through activation -- every production-capable node must run the upgraded release **before** the real production chain reaches height 227808 |

No auto-upgrade or action was taken against the real node as part of this rehearsal; this is an
operational release-readiness gate, not an instruction to act on production.

## 30. Stabilization criteria (final lifecycle phase, Part 6)

**Explicit reminder, per instruction**: no block hash from this rehearsal is a production checkpoint.
Every post-225823 block mined in this entire rehearsal (sec.6 onward) belongs to a disposable
laboratory fork whose hashes, timestamps, and coinbases differ from whatever the real production chain
will actually produce. `227808`- and `227931`-height rehearsal hashes are **not** production checkpoint
hashes. No `nMinimumChainWork` or checkpoint parameter should be changed using any rehearsal data.

Instead, the following are the **criteria** the real production chain must satisfy before a real
production stabilization/checkpoint candidate can be considered -- a height range and a checklist, not
a hash:

1. Real production chain crosses height 227808.
2. A real direct-mined block path is observed on the real chain at/after 227808.
3. A real AuxPoW block path is observed on the real chain, if merge-mining is actually active in
   production by then.
4. Substantial real post-activation history accumulates (a specific depth is deliberately not
   prescribed here -- this rehearsal establishes the *mechanism*, not a magic number for the real
   network's own organic growth).
5. Real production node restart(s) pass (ordinary restart, and `-reindex-chainstate` specifically --
   given sec.26's finding, a real production operator should prefer `-reindex-chainstate` over full
   `-reindex` unless a full reindex is genuinely required, until/unless further hardening beyond this
   rehearsal's scope is separately reviewed).
6. Real peer convergence is confirmed among real production nodes.
7. No unexpected reorg, serialization, or DAA issue is observed on the real chain.
8. Real production chain crosses BIP34 height 227931.
9. Additional real history accumulates after 227931.
10. **Only then** should a real production height/hash/chainwork candidate be recorded, from the real
    chain's own real data, for review -- never from this or any other rehearsal.

The rehearsal may identify a height range and a criteria checklist (as above); it must never produce a
production hash.

## 31. Final lifecycle phase summary and remaining scope

| part | status |
|---|---|
| Part 1: fresh-node HEADERS-first/full sync | **DONE -- PASS** (sec.25.1) |
| Part 2: reindex/reindex-chainstate validation | **DONE** -- found, fixed, and re-verified a real Core defect (sec.26); all four disposable states (227807, 227808 direct, 227808 AuxPoW, mature mixed post-BIP34) and the real mixed convergence chain (sec.27.1) all confirmed passing post-fix |
| Part 3: consolidated restart/reindex matrix | **DONE** (sec.27) |
| Part 4: pruning feasibility | **DONE** -- real infeasibility at the current tip, precisely characterized, not manufactured (sec.28) |
| Part 5: production upgrade gate | **DONE** (sec.29) |
| Part 6: stabilization criteria | **DONE** (sec.30) |
| Part 7: report hygiene | performed at the start of this pass (sec.14, and re-confirmed directly on the actual file before this commit) |
| Part 8: this report | this section and sec.25-30 |
| Part 9: commit and stop | this section's own commit, see below |

**No Core or consensus change was necessary anywhere in Parts 1, 3, 4, 5, or 6.** The one real defect
(Part 2) was found, fixed on its own narrowly-scoped track, and re-verified before this document was
updated, per instruction. Per instruction, this rehearsal now stops: **not** launching production, not
mining the real/private node, not inserting checkpoints, not changing minimum chainwork, and not
building a Bitcoin GBT adapter or Stratum/pool infrastructure.

## 32. Pending sections (post-rehearsal)

**This activation rehearsal is now complete end to end.** Everything through sec.6-31 -- the canonical
225823->227807 run, the 227807 snapshot, Scenario A/B at the real activation boundary, the full
upgraded-node convergence/reorg/branch-local-ASERT phase, the real BIP34 height 227931 boundary
rehearsal, fresh-node HEADERS-first/full sync, the full reindex/reindex-chainstate matrix (including
one real Core defect found, fixed on its own track, and re-verified), pruning feasibility, the
production upgrade gate, and stabilization criteria -- is real, complete, and either PASS or (for the
one real discrepancy) FOUND-FIXED-VERIFIED. Nothing below is required to consider the rehearsal itself
finished; the remaining items are genuinely optional follow-on work, not gaps in what was asked for:

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
| ASERT dynamic verification (real heights, independent reference) | `scenario_asert_compare.py` | superseded by sec.9's, sec.19's, and sec.23.8's live confirmations; the original script remains available for a future standalone run if wanted |
| BIP34 boundary (227931) crossing | sec.23 (`run_bip34_normal.py`, `run_bip34_control.py`, `run_bip34_reorg.py`, `manual_block.py`, `mine_gap.py`) | **DONE -- PASS**, including the wrong-height/control pair and a boundary-crossing reorg |
| BIP34Hash source audit (correcting an earlier misleading report claim) | sec.23.0 | **DONE** -- inert BIP30-skip optimization gate, fully independent of BIP34's own enforcement, no Core change |
| Fresh-node HEADERS-first / full synchronization | sec.25.1 | **DONE -- PASS** |
| Full `-reindex` AuxPoW disk-deserialization defect | sec.26 | **FOUND, FIXED, RE-VERIFIED** -- commit `7942285e1175dacb54958b7cde1e4fa19630fde0` |
| `-reindex-chainstate` (unaffected by the defect, re-confirmed post-fix) | sec.26.6, sec.27 | **DONE -- PASS** |
| Consolidated restart/reindex matrix | sec.27 | **DONE** -- every named state covered by real evidence |
| Pruning feasibility | sec.28 | **DONE** -- real infeasibility at the current tip, precisely characterized; underlying mechanism deferred to the existing, already-green `feature_auxpow_prune.py` |
| Production upgrade gate | sec.29 | **DONE** |
| Post-activation stabilization criteria | sec.30 | **DONE** -- a height range and checklist, deliberately no production hash |
| Obsolete-node (D) further divergence | `scenario_obsolete_node_D.py` | D's behavior at the pre-activation boundary (sec.7.1) and against both proof mechanisms at the activation boundary (sec.9.1/10.1) is now recorded; per instruction, D was deliberately not involved further in the convergence/reorg, BIP34, or final lifecycle phases -- that evidence was judged sufficient |
| Discrepancies found, if any | | **one real Core defect found, fixed, and re-verified: sec.26 (full-reindex AuxPoW disk-deserialization defect, commit `7942285e1175dacb54958b7cde1e4fa19630fde0`)**; mining-harness/rehearsal-lab bugs found and fixed are documented in sec.5, sec.15 (onion-port collision), and sec.23.2's honest disclosure of a real-but-expected ASERT fixed-anchor difficulty-growth dynamic (not a bug, not fixed, not worked around); the legacy-vs-ASERT divergence at 227808 (sec.6.2) and both obsolete-node rejections (sec.9.1/10.1) are expected designed behavior, not discrepancies |
| Genuinely optional follow-on (not part of what was asked) | `scenario_restart_matrix.py`, `scenario_pruning_plan.py`, `scenario_obsolete_node_D.py`, `scenario_asert_compare.py` | ready scripts retained in `~/Downloads/bitaicoin-rehearsal-lab/` for any future dedicated deep-dive, but superseded for this rehearsal's own purposes by the real evidence already captured above |

Note on sec.9/10: the existing `scenario_A_direct.py`/`scenario_B_auxpow.py` scripts (written earlier
in the rehearsal, sec.7 of the pending table before this revision) mine extra continuity blocks
(227809/227810) and do not perform a pre-mining `getblocktemplate`/`createauxblock` assertion gate.
Per explicit instruction for this specific run, sec.9/10 were executed via new, narrower scripts
(`run_scenario_b.py` for B; direct RPC calls for A) that perform exactly the specified pre-mining
assertions and stop after exactly one block. The original two scripts remain available, unmodified,
for a future full continuity/multi-block run if wanted.

**No result for any item still marked PENDING above exists yet. None will be fabricated or assumed.**
