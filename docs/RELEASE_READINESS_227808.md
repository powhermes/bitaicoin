# Release readiness: BitAIcoin 227808 (AuxPoW/ASERT) / 227931 (BIP34) activation

Concise, operational record. Full narrative evidence lives in
`docs/ACTIVATION_REHEARSAL_227808.md`; this document is the release gate summary.

## Source

- **Release-candidate branch HEAD (pre-manifest-update)**: `0d82992ccd32d5e9ac3f87cd96e8572fe96bc355`
  (`powhermes/bitaicoin`, branch `bitaicoin-phase1`). This is the tree that was built and validated in
  the final clean-environment gate below. Committing this manifest update creates one further commit on
  top; that commit's hash is the true final candidate HEAD and is recorded at the bottom of this file
  once known.
- **Composition of the candidate HEAD**:
  - `c5e1c62482e910428604ee373f24b798eddd8233` -- this release-readiness manifest (initial version)
  - `0d82992ccd32d5e9ac3f87cd96e8572fe96bc355` -- `.gitignore` hygiene fix restoring the historical
    ignore patterns for `test/config.ini`, `test/cache/`, `test/.mypy_cache/` so that
    `git status --porcelain` is genuinely empty (build artifacts no longer show as untracked).
- **Reindex-fix commit included**: `7942285e1175dacb54958b7cde1e4fa19630fde0`
  (confirmed ancestor of the candidate HEAD via `git merge-base --is-ancestor`).
- **Functional-test REPO_ROOT/symlink fix included**: `448ad36f53031a5994bd772e8d16c114e807a040`
  (confirmed ancestor via `git merge-base --is-ancestor`).
- **Previous activation/AuxPoW milestone commits included**: `d077241af36023f1c6241faf93d6ea7defb81d41`
  (createauxblock/submitauxblock RPC + IBD/cooldown fix), `2e700d7ad6fe08c840d2e9b4e99ef5e89a2b34f6`
  (merge-mining reference coordinator)
- **Working tree**: **genuinely clean** -- `git status --porcelain` produces no output at all (not
  "clean except untracked"; the `.gitignore` hygiene commit above is what closed that gap).

**Release-source requirement, stated unambiguously**: the minimum production release must contain the
complete activation package **and** the reindex fix, as one continuous history -- not two independently
selectable commits. **Production release source must be a descendant of the complete AuxPoW/ASERT
activation implementation and must include reindex fix commit `7942285e1175dacb54958b7cde1e4fa19630fde0`.**
The exact final release-candidate HEAD recorded above (`0d82992ccd...`, plus the manifest-update commit
on top of it) is the concrete instance of that requirement as verified in this rehearsal; any later
commit built on top of it, without reverting either the activation implementation or the reindex fix,
continues to satisfy it.

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
| C++ full result | **PASS**, 811/811 test cases run (816 total, 5 platform-skipped; one benign
  `DIR_UNIT_TEST_DATA`-unset skip-warning in `script_assets_tests`, standard upstream behavior), EXIT 0.
  Re-confirmed on the fresh post-reboot clean build. |
| C++ `auxpow_tests` | **PASS**, 52/52 test cases (re-confirmed on the fresh clean build) |
| Focused AuxPoW functional suite | **PASS**, all of: `feature_auxpow_createauxblock_ibd.py`,
  `feature_auxpow_rpc.py`, `feature_auxpow_rpc_disabled_chains.py`, `feature_auxpow_prune.py`,
  `feature_auxpow_reindex.py` |
| Stock reindex / mining / block tests | **PASS**: `feature_reindex.py`, `mining_basic.py`,
  `feature_block.py` (each confirmed individually against the release-candidate HEAD) |
| Full functional suite (`-j2`, post-reboot clean machine) | **PASS -- ✓ ALL PASSED**, 289 tests,
  runtime 872 s. Every non-pass is a legitimate platform/config skip (not-on-Linux USDT/bind tests,
  IPC/bench not compiled, previous-releases-not-available backward-compat tests, python3-zmq absent);
  zero failures; runner EXIT 0. See "Full functional suite" below. |

### Final clean-environment validation (build environment + binaries)

This gate was executed on a **freshly rebooted machine** specifically to obtain the authoritative,
fully-green full-suite result that the earlier heavily-used session could not (see historical note
below). Environment verified clean before building: no stray `bitaicoind`/`test_runner`/`bitcoind`
processes, **zero swap in use**, ample free disk.

- **Toolchain**: Apple clang 21.0.0 (`clang-2100.3.34.2`), target `arm64-apple-darwin25.6.0`;
  CMake 4.4.3; Xcode 27.0 (`27A266a`); macOS 26.6 (`25G72`); 10 cores / 16 GB RAM.
- **Build directory**: brand-new `build-rc/` (the old, heavily-exercised `build/` tree was **not**
  reused).
- **Configure**: `RelWithDebInfo`; `BUILD_TESTS=ON`, `BUILD_CLI=ON`, `BUILD_DAEMON=ON`,
  `BUILD_UTIL=ON`; `BUILD_GUI=OFF`, `BUILD_BENCH=OFF`, `ENABLE_IPC=OFF`; system `cc`/`c++`.
- **Build command**: `cmake --build build-rc -j10` -- completed with no errors.
- **Binary SHA-256** (the daemon/CLI are emitted as `bitaicoind`/`bitaicoin-cli`; the functional test
  harness requires `bitcoind`/`bitcoin-cli` names, provided as symlinks to these exact binaries -- a
  harness-path accommodation only, no source or binary altered):
  - `bitaicoind`: `cba16419790ff2ed94ca83af0529a48838a085e3f16f615c2a45056e7de5cc85`
  - `bitaicoin-cli`: `9f1024cc5371ad4ad9d476850994db76913079bc0090b5554d6180a7bf542c02`

### Full functional suite: clean-environment result

The full functional suite was run **once at `-j2`** against the exact release-candidate tree on the
post-reboot clean machine and **passed with zero failures** (`ALL | ✓ Passed`, 289 tests, runtime
872 s, runner EXIT 0). No `-j1` fallback was needed. Every test not marked passed is a documented
platform/configuration skip (not-on-Linux USDT/bind interfaces, IPC/bench binaries not compiled,
previous-release-dependent backward-compat tests, python3-zmq module absent) -- none are failures.

The eight focused functional gates were additionally each run individually against this same clean
build first and all passed: `feature_auxpow_createauxblock_ibd.py`, `feature_auxpow_rpc.py`,
`feature_auxpow_rpc_disabled_chains.py`, `feature_auxpow_prune.py`, `feature_auxpow_reindex.py`,
`feature_reindex.py`, `mining_basic.py`, `feature_block.py`.

### Historical note: earlier heavily-used-session flakiness (now superseded)

Before the reboot, full-suite attempts at `-j2`/`-j1` on the same tree did **not** complete zero-green.
That was investigated at the time and attributed to session-accumulated machine-resource pressure, not
a code defect:

- **One real, deterministic, 100%-reproducible bug was found and fixed** during that investigation:
  `feature_auxpow_createauxblock_ibd.py` failed every time under `test_runner.py`/`ctest` (which invoke
  tests via a symlinked build-tree copy) because its `REPO_ROOT` computation used `os.path.abspath()`,
  which does not resolve symlinks. Fixed in commit `448ad36f53031a5994bd772e8d16c114e807a040`
  (`os.path.realpath()` instead) -- a test-infrastructure-only change, no consensus/RPC/serialization
  code touched.
- **Every other failure was individually re-run in isolation and passed**; the failing set changed on
  every full-suite run (no two failed on the same tests), the same pattern was reproduced on the
  **pre-fix** binary during the rehearsal (`ACTIVATION_REHEARSAL_227808.md` sec.26.6), and the failure
  signature was always a generic transport-level error (`ConnectionResetError`, RPC timeout, occasional
  `SIGKILL`) in tests unrelated to AuxPoW/ASERT/BIP34/reindex. Direct measurement showed very low free
  memory and 11.9 GB of swap in use at the time.
- This clean-environment gate is exactly the "authoritative, fully-green result from a fresh
  environment" that the earlier pass recommended obtaining before tagging. It has now been obtained.

## Known non-consensus notes

- Functional-suite flakiness observed late in an extremely long pre-reboot session was traced to
  machine-resource pressure (11.9 GB swap in use), not the release-candidate source; it did **not**
  recur in the post-reboot clean-environment gate, which passed the full suite zero-green at `-j2`. See
  the historical note above.
- The laboratory port/onion-service-bind collision found during the convergence/reorg rehearsal phase
  (`ACTIVATION_REHEARSAL_227808.md` sec.15) was rehearsal infrastructure only (a port-numbering choice
  in throwaway test scripts), not a BitAIcoin or Bitcoin Core defect.
- **No production checkpoint hash comes from rehearsal data.** Every post-225823 hash produced during
  this entire rehearsal belongs to a disposable laboratory fork and must never be used as an
  `nMinimumChainWork` value, a checkpoint, or any other production parameter.

## Consensus freeze

**Consensus implementation is frozen following completion of the 227808/227931 activation rehearsal.
Any subsequent consensus-affecting change requires explicit review and a targeted activation rehearsal
before production.**

This does **not** mean ordinary bug fixes, UI/RPC changes, or release engineering are forbidden. It
means no casual modification of: activation height, chain ID, ASERT, direct/AuxPoW coexistence, block
serialization, PoW validation, BIP34 semantics, replay rules, or the fork anchor.

## Release-gate confirmations (final)

- **Reindex fix included**: yes -- `7942285e1175dacb54958b7cde1e4fa19630fde0` is an ancestor of the
  candidate HEAD (verified by `git merge-base --is-ancestor`).
- **Working tree clean**: yes -- `git status --porcelain` produces no output.
- **Activation rehearsal complete**: yes -- full 227808/227931 rehearsal COMPLETE and consensus-frozen
  (`docs/ACTIVATION_REHEARSAL_227808.md`).
- **Consensus freeze active**: yes -- see "Consensus freeze" above.
- **C++ unit gate**: 811/811 run PASS, `auxpow_tests` 52/52, EXIT 0, on the fresh clean build.
- **Full functional suite**: ✓ ALL PASSED at `-j2` on the post-reboot clean machine (289 tests, 872 s,
  zero failures).
- **Final candidate HEAD**: the commit that lands this manifest update **is** the final release
  candidate. Because a commit cannot embed its own hash, the authoritative value is whatever
  `git rev-parse HEAD` / `git log -1 --format=%H -- docs/RELEASE_READINESS_227808.md` reports for this
  commit on branch `bitaicoin-phase1`. It is a direct child of `0d82992ccd32d5e9ac3f87cd96e8572fe96bc355`
  and, like its parent, includes the reindex fix and the full activation implementation.

## Explicitly not done as part of this manifest

- The real/private production chain was not mined, advanced, or otherwise touched.
- No checkpoint or `nMinimumChainWork` value was inserted or changed.
- No production node was upgraded; no production peer configuration was changed.
- The `BitcoinCoreGBTParentWorkProvider` (next architecture phase) was not started -- deliberately
  deferred until after this release-candidate/source baseline is frozen and recorded, per instruction,
  to avoid mixing integration expansion with final consensus release preparation.
- No release was tagged or deployed.
