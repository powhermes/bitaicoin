#!/usr/bin/env python3
# Copyright (c) 2026 The BitAIcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Adversarial regression matrix for the merge-mining coordinator reference
implementation, run against a real, live bitaicoind regtest node.

This file constructs deliberately-malformed proofs at the wire level (using
this coordinator's own wire.py primitives directly, the same ones
MergeMiningCoordinator itself uses) and submits them via the real
submitauxblock RPC -- it never re-implements or asserts what "should" be
valid from first principles. bitaicoind's own real AuxPoW validation
(already exhaustively covered by src/test/auxpow_tests.cpp and
test/functional/feature_auxpow_rpc.py) is the sole authority on every
verdict here (item 8 of the merge-mining milestone spec: "Do not duplicate
consensus validation logic in the coordinator").

Run directly:
    python3 contrib/merge_mining_coordinator/test_negative_matrix.py
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from coordinator import MergeMiningCoordinator, SubmitStatus  # noqa: E402
from provider import ParentSolution, SyntheticParentWorkProvider  # noqa: E402
from rpc import BitAIcoinRPC, RPCError  # noqa: E402
from wire import (  # noqa: E402
    AuxPow,
    MERGE_MINING_MAGIC,
    Transaction,
    TxIn,
    build_merge_mining_tag,
    build_single_chain_commitment,
    hash_from_hex,
    make_coinbase,
    solve_parent_header,
)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN_DIR = os.path.join(REPO_ROOT, "build", "bin")
RPC_USER = "x"
RPC_PASSWORD = "y"
RPC_PORT = 19901

FAILURES = []
PASSES = []


def check(condition, name, detail=""):
    if condition:
        PASSES.append(name)
        print(f"[PASS] {name}")
    else:
        FAILURES.append((name, detail))
        print(f"[FAIL] {name} -- {detail}")


def start_node(datadir):
    os.makedirs(datadir, exist_ok=True)
    subprocess.run([
        os.path.join(BIN_DIR, "bitaicoind"),
        f"-datadir={datadir}", "-regtest", "-daemon", "-fallbackfee=0.0001",
        f"-rpcuser={RPC_USER}", f"-rpcpassword={RPC_PASSWORD}", f"-rpcport={RPC_PORT}",
    ], check=True, cwd=BIN_DIR)
    rpc = BitAIcoinRPC("127.0.0.1", RPC_PORT, RPC_USER, RPC_PASSWORD, timeout=10.0)
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            rpc.getblockchaininfo()
            return rpc
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("node did not become ready")


def stop_node(rpc):
    try:
        rpc.call("stop")
    except Exception:
        pass
    time.sleep(1)


def find_failing_nonce(header, target):
    """Returns a header whose hash does NOT satisfy `target` -- needed
    because regtest's own default target (0x207fffff) is extremely easy
    (roughly half of all possible hashes already satisfy it by chance), so
    "an unsolved header" cannot be assumed to already fail on its own."""
    from wire import BlockHeader
    h = BlockHeader(**header.__dict__)
    for nonce in range(1000):
        h.nonce = nonce
        if h.block_hash() > target:
            return h
    raise RuntimeError("could not find a nonce failing this (very easy) target -- unexpected")


def build_raw_auxpow_submission(tag_builder, child_hash, target, satisfy_pow=True, corrupt_auxpow=None):
    """Builds a fresh single-tx synthetic parent committing (via
    `tag_builder`, a callable returning the raw scriptSig tag bytes to
    embed) to `child_hash`, mines it (or deliberately does not, if
    satisfy_pow=False), and returns the exact hex `submitauxblock` would
    receive -- with `corrupt_auxpow` (a callable taking the constructed
    AuxPow and mutating it in place) applied last, if given. This mirrors
    MergeMiningCoordinator's own internal construction exactly, just with
    adversarial hooks exposed for this test file only. Does not itself talk
    to the node -- the caller submits the returned hex via its own rpc
    handle, keeping this a pure, reusable builder."""
    tag = tag_builder()
    coinbase = make_coinbase(tag)
    from wire import BlockHeader
    header = BlockHeader(version=1, hash_prev_block=0, hash_merkle_root=coinbase.txid(),
                          time=int(time.time()), bits=0x1D00FFFF, nonce=0)
    if satisfy_pow:
        header = solve_parent_header(header, target)
    else:
        header = find_failing_nonce(header, target)

    proof = AuxPow(
        coinbase_tx=coinbase,
        coinbase_branch=[],
        coinbase_index=0,
        chain_branch=[],
        chain_index=0,
        parent_block=header,
    )
    if corrupt_auxpow is not None:
        corrupt_auxpow(proof)
    return proof.hex()


def main():
    tmp = tempfile.mkdtemp(prefix="baic_mmc_negmatrix_")
    rpc = start_node(tmp)
    try:
        rpc.createwallet("negmatrix")
        payout_addr = rpc.getnewaddress()
        provider = SyntheticParentWorkProvider()
        coordinator = MergeMiningCoordinator(rpc, provider, payout_addr)

        # -----------------------------------------------------------
        # Positive case 1: a fully valid proof, via the real public API.
        # -----------------------------------------------------------
        job_valid = coordinator.create_job()
        solved = solve_parent_header(job_valid.parent_candidate.header, job_valid.child_target_numeric)
        result = coordinator.submit_parent_solution(job_valid, ParentSolution(
            header=solved,
            coinbase=job_valid.parent_candidate.coinbase,
            coinbase_branch=job_valid.parent_candidate.coinbase_branch,
            coinbase_index=job_valid.parent_candidate.coinbase_index,
        ))
        check(result.status == SubmitStatus.ACCEPTED, "valid proof accepted", str(result.detail))
        check(rpc.getbestblockhash() == job_valid.child_hash, "tip advanced to the accepted child hash")

        # -----------------------------------------------------------
        # Positive case 2: idempotent resubmission of that same, now-
        # accepted, AuxPoW child.
        # -----------------------------------------------------------
        result2 = coordinator.submit_parent_solution(job_valid, ParentSolution(
            header=solved,
            coinbase=job_valid.parent_candidate.coinbase,
            coinbase_branch=job_valid.parent_candidate.coinbase_branch,
            coinbase_index=job_valid.parent_candidate.coinbase_index,
        ))
        check(result2.status == SubmitStatus.ACCEPTED, "idempotent resubmission of accepted AuxPoW child",
              str(result2.detail))

        # -----------------------------------------------------------
        # Negative case: stale BAIC candidate. Create job A, then let a
        # DIFFERENT job (B, same tip) actually advance the chain; A is now
        # stale and must be rejected cleanly, without mutating/recycling it.
        # -----------------------------------------------------------
        job_a = coordinator.create_job()
        # A DIFFERENT payout address, so job_b's coinbase (and therefore its
        # own BAIC child hash) is guaranteed distinct from job_a's, even
        # though both are built at the exact same tip in the same second --
        # otherwise two calls this close together can legitimately produce
        # byte-identical candidates (same tip, same empty mempool, same
        # second), which would make this "stale" setup accidentally
        # re-test idempotency instead (caught by a real run: this exact
        # collision happened on the first version of this test).
        coordinator_b = MergeMiningCoordinator(rpc, provider, rpc.getnewaddress())
        job_b = coordinator_b.create_job()
        check(job_a.child_hash != job_b.child_hash, "setup: job A and job B are genuinely distinct candidates")
        solved_b = solve_parent_header(job_b.parent_candidate.header, job_b.child_target_numeric)
        rb = coordinator.submit_parent_solution(job_b, ParentSolution(
            header=solved_b, coinbase=job_b.parent_candidate.coinbase,
            coinbase_branch=job_b.parent_candidate.coinbase_branch, coinbase_index=job_b.parent_candidate.coinbase_index))
        check(rb.status == SubmitStatus.ACCEPTED, "setup: sibling job B accepted to make job A stale", str(rb.detail))

        solved_a = solve_parent_header(job_a.parent_candidate.header, job_a.child_target_numeric)
        ra = coordinator.submit_parent_solution(job_a, ParentSolution(
            header=solved_a, coinbase=job_a.parent_candidate.coinbase,
            coinbase_branch=job_a.parent_candidate.coinbase_branch, coinbase_index=job_a.parent_candidate.coinbase_index))
        check(ra.status == SubmitStatus.STALE, "stale BAIC candidate reported cleanly as STALE, not mutated/recycled",
              str(ra.detail))

        # -----------------------------------------------------------
        # Remaining negative cases: constructed at the wire level directly
        # (this file's own build_raw_auxpow_submission), submitted via the
        # raw RPC, verdict taken entirely from bitaicoind's real response.
        # -----------------------------------------------------------
        def fresh_target_and_hash():
            j = coordinator.create_job()
            return j, j.child_target_numeric, hash_from_hex(j.child_hash)

        # 1. parent hash above BAIC target (deliberately unsolved).
        j, target, child_hash = fresh_target_and_hash()
        aux_root, tree_size, chain_branch, chain_index = build_single_chain_commitment(child_hash, 12345)
        hexdata = build_raw_auxpow_submission(
            lambda: build_merge_mining_tag(aux_root, tree_size, 12345), child_hash, target, satisfy_pow=False)
        try:
            r = rpc.submitauxblock(j.child_hash, hexdata)
            check(r is False, "parent hash above BAIC target rejected", f"got {r!r}")
        except RPCError as e:
            check(True, "parent hash above BAIC target rejected", f"(via RPC error {e.code}: {e.message})")

        # 2. wrong child commitment (tag commits to a different hash).
        j, target, child_hash = fresh_target_and_hash()
        wrong_hash = child_hash ^ 1
        aux_root, tree_size, chain_branch, chain_index = build_single_chain_commitment(wrong_hash, 999)
        hexdata = build_raw_auxpow_submission(
            lambda: build_merge_mining_tag(aux_root, tree_size, 999), child_hash, target)
        r = rpc.submitauxblock(j.child_hash, hexdata)
        check(r is False, "wrong child commitment rejected", f"got {r!r}")

        # 3. corrupted parent coinbase (mutate coinbase AFTER the header's
        # hashMerkleRoot was fixed against the ORIGINAL coinbase).
        j, target, child_hash = fresh_target_and_hash()
        aux_root, tree_size, chain_branch, chain_index = build_single_chain_commitment(child_hash, 42)
        tag = build_merge_mining_tag(aux_root, tree_size, 42)
        coinbase = make_coinbase(tag)
        from wire import BlockHeader
        header = BlockHeader(version=1, hash_prev_block=0, hash_merkle_root=coinbase.txid(),
                              time=int(time.time()), bits=0x1D00FFFF, nonce=0)
        header = solve_parent_header(header, target)
        corrupted_coinbase = Transaction(version=coinbase.version,
                                          vin=[TxIn(prev_txid=0, prev_vout=0xFFFFFFFF,
                                                    script_sig=tag + b"\x00\x01\x02", sequence=0xFFFFFFFF)],
                                          vout=list(coinbase.vout), locktime=coinbase.locktime)
        proof = AuxPow(coinbase_tx=corrupted_coinbase, coinbase_branch=[], coinbase_index=0,
                        chain_branch=[], chain_index=0, parent_block=header)
        r = rpc.submitauxblock(j.child_hash, proof.hex())
        check(r is False, "corrupted parent coinbase rejected", f"got {r!r}")

        # 4. wrong parent (coinbase) merkle branch -- a single-tx block
        # should have an EMPTY branch; a bogus non-empty one must fail.
        j, target, child_hash = fresh_target_and_hash()
        aux_root, tree_size, _, _ = build_single_chain_commitment(child_hash, 7)
        tag = build_merge_mining_tag(aux_root, tree_size, 7)
        hexdata = build_raw_auxpow_submission(
            lambda: tag, child_hash, target,
            corrupt_auxpow=lambda p: setattr(p, "coinbase_branch", [0xDEADBEEF]),
        )
        r = rpc.submitauxblock(j.child_hash, hexdata)
        check(r is False, "wrong parent coinbase merkle branch rejected", f"got {r!r}")

        # 5. wrong coinbase index (must be 0).
        j, target, child_hash = fresh_target_and_hash()
        aux_root, tree_size, _, _ = build_single_chain_commitment(child_hash, 8)
        tag = build_merge_mining_tag(aux_root, tree_size, 8)
        hexdata = build_raw_auxpow_submission(
            lambda: tag, child_hash, target,
            corrupt_auxpow=lambda p: setattr(p, "coinbase_index", 1),
        )
        r = rpc.submitauxblock(j.child_hash, hexdata)
        check(r is False, "wrong coinbase index rejected", f"got {r!r}")

        # 6. malformed merged-mining tag (truncated payload -- magic
        # present, but not enough bytes after it).
        j, target, child_hash = fresh_target_and_hash()
        bad_tag = MERGE_MINING_MAGIC + b"\x01\x02\x03"  # far short of the required 40-byte payload
        hexdata = build_raw_auxpow_submission(lambda: bad_tag, child_hash, target)
        r = rpc.submitauxblock(j.child_hash, hexdata)
        check(r is False, "malformed (truncated) merged-mining tag rejected", f"got {r!r}")

        # 7. duplicate merged-mining tags (ambiguous -- must be rejected
        # even though the FIRST one is perfectly well-formed and correct).
        j, target, child_hash = fresh_target_and_hash()
        aux_root, tree_size, _, _ = build_single_chain_commitment(child_hash, 55)
        good_tag = build_merge_mining_tag(aux_root, tree_size, 55)
        hexdata = build_raw_auxpow_submission(lambda: good_tag + good_tag, child_hash, target)
        r = rpc.submitauxblock(j.child_hash, hexdata)
        check(r is False, "duplicate merged-mining tags rejected", f"got {r!r}")

        # 8. incorrect tree size (0 -- explicitly rejected by
        # CAuxPow::Check's own tree-size sanity bound).
        j, target, child_hash = fresh_target_and_hash()
        hexdata = build_raw_auxpow_submission(
            lambda: build_merge_mining_tag(child_hash, 0, 3), child_hash, target)
        r = rpc.submitauxblock(j.child_hash, hexdata)
        check(r is False, "incorrect (zero) tree size rejected", f"got {r!r}")

        # 9. incorrect chain index (nChainIndex must equal the
        # deterministically-expected slot -- 0, for tree_size=1).
        j, target, child_hash = fresh_target_and_hash()
        aux_root, tree_size, _, _ = build_single_chain_commitment(child_hash, 9)
        tag = build_merge_mining_tag(aux_root, tree_size, 9)
        hexdata = build_raw_auxpow_submission(
            lambda: tag, child_hash, target,
            corrupt_auxpow=lambda p: setattr(p, "chain_index", 1),
        )
        r = rpc.submitauxblock(j.child_hash, hexdata)
        check(r is False, "incorrect chain index rejected", f"got {r!r}")

        # 10. truncated proof (well-formed up to a point, then cut off).
        j, target, child_hash = fresh_target_and_hash()
        aux_root, tree_size, _, _ = build_single_chain_commitment(child_hash, 10)
        tag = build_merge_mining_tag(aux_root, tree_size, 10)
        good_hex = build_raw_auxpow_submission(lambda: tag, child_hash, target)
        try:
            r = rpc.submitauxblock(j.child_hash, good_hex[:-8])
            check(False, "truncated proof rejected", f"expected an error, got {r!r}")
        except RPCError as e:
            check(e.code == -22, "truncated proof rejected", f"decode error as expected: {e.message}")

        print(f"\n{len(PASSES)} passed, {len(FAILURES)} failed")
        if FAILURES:
            print("FAILURES:")
            for name, detail in FAILURES:
                print(f"  - {name}: {detail}")
            return 1
        print("ALL NEGATIVE-MATRIX TESTS PASSED")
        return 0
    finally:
        stop_node(rpc)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
