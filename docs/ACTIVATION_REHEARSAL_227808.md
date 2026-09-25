# BitAIcoin activation rehearsal: crossing 227808 (AuxPoW/ASERT) and 227931 (BIP34)

**Status: IN PROGRESS.** This document is being built incrementally as the rehearsal proceeds.
Sections marked `PENDING` have not happened yet and contain no fabricated data -- per the governing
instruction, no 227807/227808+ result is recorded before it actually exists. As of this revision: the
real 225823->227807 canonical mining run, its full provenance verification, the immutable
golden-227807-pre-activation snapshot and its independent verification, the obsolete node D
pre-activation compatibility record, and the ASERT-vs-legacy computation for 227808 are all complete
and real (sec.6-7.1). Canonical node A is now stopped and treated as read-only evidence. **227808 has
not been mined by anything yet** -- Scenario A and Scenario B (sec.8) remain deliberately not started.

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

## 8. Pending sections (will be completed once Scenario A/B begin)

All tooling below is written and ready in `~/Downloads/bitaicoin-rehearsal-lab/` -- each script is a
real, runnable implementation (not a placeholder), verified to import/parse correctly. The
225823->227807 canonical run, its provenance verification, the golden-227807 snapshot, its
independent verification, D's pre-activation record, and the ASERT-vs-legacy computation for 227808
(sec.6-7.1) are now all real, complete, and PASS. Everything below sec.6-7.1 has not been RUN against
real 227808+ data yet -- that is Scenario A/B's own job, deliberately not started until this report is
delivered and reviewed, per instruction. No result for any of the following exists yet; none will be
fabricated or assumed:

| item | script | status |
|---|---|---|
| shared node/RPC config | `lab_common.py` | ready |
| 227807 snapshot capture + verification | sec.7 | **DONE -- PASS** |
| D pre-activation compatibility record | sec.7.1 | **DONE -- PASS** |
| ASERT anchor computation + legacy-vs-ASERT comparison for 227808 | sec.6.2 | **DONE** (computed, not mined) |
| Automated 1,984-block provenance verification | sec.6.3 | **DONE -- PASS** |
| Scenario A: first activation block, DIRECT | `scenario_A_direct.py` | ready, PENDING run |
| Scenario B: first activation block, AuxPoW | `scenario_B_auxpow.py` | ready, PENDING run |
| Three-upgraded-node convergence (A/B/C) | `scenario_convergence.py` | ready, PENDING run |
| Competing-branch/reorg test | `scenario_reorg.py` | ready (base case; differing-anchor variant is a documented follow-up), PENDING run |
| ASERT dynamic verification (real heights, independent reference) | `scenario_asert_compare.py` | ready, PENDING run (sec.6.2's offline computation still needs on-node RPC confirmation at real 227808) |
| BIP34 boundary (227931) crossing | `scenario_bip34.py` | ready, PENDING run |
| Restart/reindex matrix | `scenario_restart_matrix.py` | ready, PENDING run at each required height |
| HEADERS-first synchronization | `scenario_headers_sync.py` | ready, PENDING run |
| Pruning rehearsal | `scenario_pruning_plan.py` | ready; includes an honest feasibility check against the real 550 MiB prune floor, with `feature_auxpow_prune.py` as the documented fallback authority if infeasible at these heights |
| Obsolete-node (D) post-activation divergence | `scenario_obsolete_node_D.py` | ready, PENDING run (D's pre-activation state is now recorded, sec.7.1) |
| Upgrade gate statement | (sec.15 of the spec) | PENDING |
| Post-activation stabilization checkpoint candidate | (sec.16 of the spec) | PENDING |
| Discrepancies found, if any | | none found in consensus code so far; mining-harness bugs found and fixed are documented in sec.5; the legacy-vs-ASERT divergence at 227808 (sec.6.2) is expected designed behavior, not a discrepancy |

**No result for any item still marked PENDING above exists yet. None will be fabricated or assumed.**
