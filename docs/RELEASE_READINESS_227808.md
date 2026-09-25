# Release readiness: BitAIcoin 227808 (AuxPoW/ASERT) / 227931 (BIP34) activation

Concise, operational record. Full narrative evidence lives in
`docs/ACTIVATION_REHEARSAL_227808.md`; this document is the release gate summary.

## Source

- **Release-candidate branch HEAD**: `448ad36f53031a5994bd772e8d16c114e807a040`
  (`powhermes/bitaicoin`, branch `bitaicoin-phase1`)
- **Reindex-fix commit included**: `7942285e1175dacb54958b7cde1e4fa19630fde0`
- **Previous activation/AuxPoW milestone commits included**: `d077241af36023f1c6241faf93d6ea7defb81d41`
  (createauxblock/submitauxblock RPC + IBD/cooldown fix), `2e700d7ad6fe08c840d2e9b4e99ef5e89a2b34f6`
  (merge-mining reference coordinator)
- **Working tree**: clean (no tracked-file modifications; only pre-existing untracked build artifacts
  -- `.DS_Store`, `test/cache/`, `test/config.ini`)

**Release-source requirement, stated unambiguously**: the minimum production release must contain the
complete activation package **and** the reindex fix, as one continuous history -- not two independently
selectable commits. **Production release source must be a descendant of the complete AuxPoW/ASERT
activation implementation and must include reindex fix commit `7942285e1175dacb54958b7cde1e4fa19630fde0`.**
The exact final release-candidate HEAD recorded above (`448ad36f53...`) is the concrete instance of
that requirement as verified in this rehearsal; any later commit built on top of it, without reverting
either the activation implementation or the reindex fix, continues to satisfy it.

## Consensus parameters

| parameter | value |
|---|---|
| Fork anchor (real chain shares Bitcoin history only through this height) | 225429 |
| Current real/private production baseline | height 225823, hash `0000000ad1060dd6b63a31796c4d57977c7f009eb0229b32d0b15dd303ecff57` |
| Production activation height | 227808 |
| AuxPoW chain ID | 16969 (`0x4249`, "BI") |
| ASERT half-life | 21600 seconds (6 hours) |
| Direct + AuxPoW validity | both permanently valid after activation |
| BIP34Height | 227931 |

## Test evidence

| item | result |
|---|---|
| Activation rehearsal | **COMPLETE** (`docs/ACTIVATION_REHEARSAL_227808.md`) |
| Obsolete-node divergence | confirmed (two distinct real rejection reasons, direct vs. AuxPoW) |
| Direct activation (227808) | **PASS** |
| AuxPoW activation (227808) | **PASS** |
| Mixed-node convergence (A/B/C, direct+AuxPoW) | **PASS** |
| Activation-crossing reorg (same-anchor, reverse-composition, direct-heavy-wins) | **PASS** |
| Branch-local ASERT (anchor-leakage check, wrong-bits subtest) | **PASS** |
| BIP34 boundary (227931) | **PASS** (wrong-height rejection, pre-activation control, boundary reorg) |
| Fresh-node IBD (real HEADERS-first, full sync) | **PASS** |
| Full `-reindex` | **PASS after fix** (see Known Non-Consensus Notes) |
| `-reindex-chainstate` | **PASS** (both before and after the reindex fix -- never affected) |
| Pruning mechanism | `feature_auxpow_prune.py` functional test **PASS**; real rehearsal chain's own physical
  pruning limitation (tiny native blocks share one blk file with the always-unprunable tip) documented,
  not a defect in the mechanism itself |
| C++ full result | **PASS**, 811/811 test cases |
| C++ `auxpow_tests` | **PASS**, 52/52 test cases |
| Focused AuxPoW functional suite | **PASS**, all of: `feature_auxpow_createauxblock_ibd.py`,
  `feature_auxpow_rpc.py`, `feature_auxpow_rpc_disabled_chains.py`, `feature_auxpow_prune.py`,
  `feature_auxpow_reindex.py` |
| Stock reindex / mining / block tests | **PASS**: `feature_reindex.py`, `mining_basic.py`,
  `feature_block.py` (each confirmed individually against the release-candidate HEAD) |
| Full functional suite (`-j2`, then `-j1`) | **NOT fully green on this development machine -- see below.
  No deterministic regression found beyond the one bug already fixed.** |

### Full functional suite: honest result, not overstated

Two full-suite attempts at `-j2` and one at `-j1` were run against the exact release-candidate tree.
None completed with zero failures. Investigated per instruction, not dismissed:

- **One real, deterministic, 100%-reproducible bug was found and fixed** during this investigation:
  `feature_auxpow_createauxblock_ibd.py` failed every time under `test_runner.py`/`ctest` (which invoke
  tests via a symlinked build-tree copy) because its `REPO_ROOT` computation used `os.path.abspath()`,
  which does not resolve symlinks. Fixed in commit `448ad36f53031a5994bd772e8d16c114e807a040`
  (`os.path.realpath()` instead) -- a test-infrastructure-only change, no consensus/RPC/serialization
  code touched. Confirmed passing under `test_runner.py` after the fix.
- **Every other failure observed across all three full-suite attempts, and across an earlier -j6
  attempt during the rehearsal itself, was individually re-run in isolation and passed** -- either
  immediately, or on one retry. The specific set of failing tests **changed every single time** (no
  two full-suite runs failed on the same set of tests), and the same broad pattern was independently
  reproduced on the **pre-fix** binary during the rehearsal itself (`ACTIVATION_REHEARSAL_227808.md`
  sec.26.6), confirming this is not caused by the reindex fix or any other change in this
  release-candidate tree.
- The failure signature is consistently a generic transport-level error (`ConnectionResetError`, RPC
  timeout, occasional `SIGKILL` on a test node) arising during ordinary P2P/RPC setup in tests that
  have no relationship to AuxPoW, ASERT, BIP34, or reindex logic (wallet, mempool, generic P2P, mining
  template tests). This is consistent with real, session-accumulated resource pressure on this
  particular development machine (very low free memory and high open-file-descriptor counts were
  observed directly) after an extraordinarily long, continuous test/rehearsal session, not a code
  defect.
- **Recommendation**: obtain one authoritative, fully-green full-suite result from a fresh environment
  (a clean CI run, or this same machine after a full restart) before tagging a release, specifically
  because this local machine could not produce one in its current, heavily-used state. The evidence
  above supports that the release-candidate tree itself has no known deterministic functional
  regression; it does not substitute for that clean confirmation.

## Known non-consensus notes

- High-parallel (and even single-threaded, `-j1`) functional-suite flakiness was observed on this
  specific development machine late in an extremely long session; see above. Not relevant to the
  release-candidate source itself.
- The laboratory port/onion-service-bind collision found during the convergence/reorg rehearsal phase
  (`ACTIVATION_REHEARSAL_227808.md` sec.15) was rehearsal infrastructure only (a port-numbering choice
  in throwaway test scripts), not a BitAIcoin or Bitcoin Core defect.
- **No production checkpoint hash comes from rehearsal data.** Every post-225823 hash produced during
  this entire rehearsal belongs to a disposable laboratory fork and must never be used as an
  `nMinimumChainWork` value, a checkpoint, or any other production parameter.

## Consensus freeze

**Consensus implementation is frozen following completion of the 227808/227931 activation rehearsal.
Any subsequent consensus-affecting change requires a new review and targeted activation rehearsal
before production.**

This does **not** mean ordinary bug fixes, UI/RPC changes, or release engineering are forbidden. It
means no casual modification of: activation height, chain ID, ASERT, direct/AuxPoW coexistence, block
serialization, PoW validation, BIP34 semantics, replay rules, or the fork anchor.

## Explicitly not done as part of this manifest

- The real/private production chain was not mined, advanced, or otherwise touched.
- No checkpoint or `nMinimumChainWork` value was inserted or changed.
- No production node was upgraded; no production peer configuration was changed.
- The `BitcoinCoreGBTParentWorkProvider` (next architecture phase) was not started -- deliberately
  deferred until after this release-candidate/source baseline is frozen and recorded, per instruction,
  to avoid mixing integration expansion with final consensus release preparation.
- No release was tagged or deployed.
