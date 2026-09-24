#!/usr/bin/env python3
# Copyright (c) 2026 The BitAIcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test createauxblock / submitauxblock, the two-call AuxPoW mining RPC
interface (docs/AUXPOW_MILESTONE.md sec.10).

This is an RPC-surface test, not a consensus test: AuxPoW consensus
correctness itself (CAuxPow::Check, CheckAuxPowRules, chain-ID/activation
enforcement, ASERT) is covered exhaustively by src/test/auxpow_tests.cpp and
src/test/pow_tests.cpp. This file only proves the RPC wiring around that
already-frozen consensus logic: schemas, caching, staleness, and that no
consensus rule is silently duplicated or bypassed at the RPC layer.
"""
import copy
import threading
from decimal import Decimal

from test_framework.auxpow import (
    BITAI_AUXPOW_CHAIN_ID,
    CAuxPow,
    _coinbase_txin,
    _null_txout,
    _solve_header_for_target,
    build_merge_mining_tag,
    build_valid_auxpow,
    get_expected_merkle_tree_index,
    reverse_hex_bytes,
    target_from_auxpow_rpc_hex,
)
from test_framework.authproxy import AuthServiceProxy
from test_framework.messages import (
    CBlockHeader,
    CTransaction,
    hash256,
    ser_uint256,
    uint256_from_compact,
    uint256_from_str,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)
from test_framework.wallet import MiniWallet

REGTEST_SUBSIDY_SATS = 50 * 100_000_000


class AuxPowRPCTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def check_merkle_branch(self, h, branch, index):
        """Python port of CheckMerkleBranch (src/auxpow.cpp), needed here
        (rather than reusing build_valid_auxpow) to hand-construct a
        tree_size > 1 chain-merkle proof for the wrong-chain-ID test below."""
        for other in branch:
            if index & 1:
                combined = ser_uint256(other) + ser_uint256(h)
            else:
                combined = ser_uint256(h) + ser_uint256(other)
            h = uint256_from_str(hash256(combined))
            index >>= 1
        return h

    def build_wrong_chain_id_auxpow(self, aux_hash_int, target):
        """Builds a well-formed, self-consistent AuxPoW proof for a
        SIMULATED multi-chain merge-mining tree (tree_size=4) whose slot
        assignment is correct for a DIFFERENT chain ID
        (BITAI_AUXPOW_CHAIN_ID + 1), not for BITAI_AUXPOW_CHAIN_ID itself.
        Real validation always supplies the fixed, non-attacker-controlled
        BITAI_AUXPOW_CHAIN_ID as `nChainId` to CAuxPow::Check() (chain ID is
        never parsed from the submitted proof -- see docs/AUXPOW_MILESTONE.md
        sec.10 report), so this proof's slot mismatches the REAL expected
        slot and must be rejected via "auxpow-wrong-chain-index".

        Note this is a DIFFERENT mechanism than the plain
        "wrong chain-merkle branch/index" test elsewhere in this file (which
        hand-corrupts nChainIndex directly, independent of any chain ID):
        this constructs an otherwise entirely internally-consistent proof
        that is only wrong because it was assembled for the wrong chain.
        """
        tree_size = 4
        wrong_chain_id = BITAI_AUXPOW_CHAIN_ID + 1
        # Find a nonce for which the two chain IDs actually disagree on the
        # expected slot (tree_size=4 gives only a 25% collision chance per
        # nonce, so this always succeeds quickly).
        nonce = None
        for candidate_nonce in range(0, 64):
            idx_real = get_expected_merkle_tree_index(candidate_nonce, BITAI_AUXPOW_CHAIN_ID, tree_size)
            idx_wrong = get_expected_merkle_tree_index(candidate_nonce, wrong_chain_id, tree_size)
            if idx_real != idx_wrong:
                nonce = candidate_nonce
                break
        assert nonce is not None, "no nonce found where the two chain IDs' expected slots differ"

        idx_for_wrong_chain = get_expected_merkle_tree_index(nonce, wrong_chain_id, tree_size)
        # Arbitrary but fixed sibling hashes for a 2-level (tree_size=4) branch.
        branch = [0x1111111111111111111111111111111111111111111111111111111111111111,
                  0x2222222222222222222222222222222222222222222222222222222222222222]
        commitment = self.check_merkle_branch(aux_hash_int, branch, idx_for_wrong_chain)
        tag = build_merge_mining_tag(commitment, tree_size, nonce)

        coinbase = CTransaction()
        coinbase.vin = [_coinbase_txin(tag)]
        coinbase.vout = [_null_txout()]

        parent = CBlockHeader()
        parent.nVersion = 1
        parent.hashPrevBlock = 0
        parent.hashMerkleRoot = coinbase.txid_int
        parent.nTime = 1700000000
        parent.nBits = 0x1d0fffff
        parent.nNonce = 0
        _solve_header_for_target(parent, target)

        proof = CAuxPow()
        proof.coinbaseTx = coinbase
        proof.vMerkleBranch = []
        proof.nIndex = 0
        proof.vChainMerkleBranch = branch
        proof.nChainIndex = idx_for_wrong_chain
        proof.parentBlock = parent
        return proof

    def build_and_submit(self, node, cand, *, chain_id=BITAI_AUXPOW_CHAIN_ID, corrupt=None):
        """Builds a valid AuxPoW proof for `cand` (optionally passing it
        through `corrupt`, a callable that mutates the CAuxPow before
        serialization) and submits it. Returns submitauxblock's result.

        Derives the numeric target from `cand["target"]` itself (the
        conventional Namecoin/Dogecoin AuxPoW RPC byte order -- see
        target_from_auxpow_rpc_hex), not from `cand["bits"]`, so this
        (the primary proof-building path most tests in this file go
        through) genuinely exercises the real field a pool would read,
        rather than silently re-deriving the same number a different way.
        Cross-checked once, explicitly, against the bits-derived value in
        the schema test below.
        """
        target = target_from_auxpow_rpc_hex(cand["target"])
        proof = build_valid_auxpow(int(cand["hash"], 16), target, chain_id)
        if corrupt is not None:
            corrupt(proof)
        return node.submitauxblock(cand["hash"], proof.serialize().hex())

    def run_test(self):
        node = self.nodes[0]
        self.wallet = MiniWallet(node)
        self.generate(self.wallet, 101)  # past coinbase maturity, real spendable UTXOs

        # createauxblock does not need or use a wallet; decode/derive the
        # payout script directly from a plain address (item 4). Use the
        # MiniWallet's own address (default ADDRESS_OP_TRUE mode) so we can
        # independently verify the real accepted coinbase output script
        # without needing a wallet loaded on `node` at all.
        payout_addr = self.wallet.get_address()

        # -------------------------------------------------------------
        # item 1: exact JSON field names/types + target byte order
        # -------------------------------------------------------------
        self.log.info("createauxblock: field names, types, and target/bits agreement")
        cand = node.createauxblock(payout_addr)
        expected_keys = {"hash", "chainid", "previousblockhash", "coinbasevalue", "bits", "height", "target"}
        assert_equal(set(cand.keys()), expected_keys)
        assert isinstance(cand["hash"], str) and len(cand["hash"]) == 64
        int(cand["hash"], 16)  # must be valid hex
        assert_equal(cand["chainid"], BITAI_AUXPOW_CHAIN_ID)  # item 3
        assert isinstance(cand["previousblockhash"], str) and len(cand["previousblockhash"]) == 64
        assert isinstance(cand["coinbasevalue"], int)
        assert isinstance(cand["bits"], str) and len(cand["bits"]) == 8
        int(cand["bits"], 16)
        assert isinstance(cand["height"], int)
        assert isinstance(cand["target"], str) and len(cand["target"]) == 64
        # Target byte order (fixed 2026-09-24, docs/AUXPOW_MILESTONE.md
        # sec.10.1/10.8): createauxblock's own "target" follows the
        # CONVENTIONAL Namecoin/Dogecoin AuxPoW RPC byte order -- raw
        # internal bytes, hex-encoded with NO reversal -- which is the exact
        # byte-reversal of getblocktemplate's own big-endian/"natural"
        # "target" convention, never directly equal to it. Two independent
        # checks: (1) the two representations describe the SAME numeric
        # target, derived two different ways (from "target" itself, and
        # separately from "bits"); (2) reversing "target"'s bytes reproduces
        # getblocktemplate's "target" string exactly.
        target_from_field = target_from_auxpow_rpc_hex(cand["target"])
        target_from_bits = uint256_from_compact(int(cand["bits"], 16))
        assert_equal(target_from_field, target_from_bits)
        gbt = node.getblocktemplate({"rules": ["segwit"]})
        assert_equal(cand["previousblockhash"], gbt["previousblockhash"])
        assert_equal(cand["bits"], gbt["bits"])
        assert_equal(reverse_hex_bytes(cand["target"]), gbt["target"])  # NOT direct equality
        assert_equal(cand["height"], gbt["height"])

        # Payout-address correctness (item 4) is checked below on the REAL
        # accepted coinbase, once a candidate is actually submitted and
        # mined -- a stronger check than inspecting the unsubmitted
        # candidate's raw block bytes.

        self.log.info("createauxblock: coinbasevalue includes subsidy + fees (item 9)")
        fee = Decimal("0.00010000")
        self.wallet.send_self_transfer(from_node=node, fee=fee)
        cand_with_fee = node.createauxblock(payout_addr)
        assert_equal(cand_with_fee["coinbasevalue"], REGTEST_SUBSIDY_SATS + int(fee * 100_000_000))

        # -------------------------------------------------------------
        # item 9: repeated same-tip work remains submittable after mempool
        # changes; a newer candidate for the same tip does not invalidate
        # an older one.
        # -------------------------------------------------------------
        self.log.info("same-tip candidate stays valid across mempool changes and newer candidates")
        cand_a = node.createauxblock(payout_addr)
        self.wallet.send_self_transfer(from_node=node)  # mempool changes
        cand_b = node.createauxblock(payout_addr)  # newer candidate, same tip
        assert_equal(cand_a["previousblockhash"], cand_b["previousblockhash"])

        # -------------------------------------------------------------
        # Duplicate-vs-stale resolution, exact tests A/B (docs/AUXPOW_MILESTONE.md
        # sec.10.12): (A) a valid candidate is accepted; (B) immediately
        # resubmitting the EXACT SAME hash/proof afterward still returns
        # true (idempotent retry), even though the tip has by then already
        # moved past cand_a's own prev_hash -- proving the idempotent-retry
        # check really does run before, and win over, the stale-prev check.
        # -------------------------------------------------------------
        target_a = target_from_auxpow_rpc_hex(cand_a["target"])
        proof_a_hex = build_valid_auxpow(int(cand_a["hash"], 16), target_a, BITAI_AUXPOW_CHAIN_ID).serialize().hex()

        result_a = node.submitauxblock(cand_a["hash"], proof_a_hex)  # test A
        assert_equal(result_a, True)
        assert_equal(node.getbestblockhash(), cand_a["hash"])

        result_a_retry = node.submitauxblock(cand_a["hash"], proof_a_hex)  # test B
        assert_equal(result_a_retry, True)
        assert_equal(node.getbestblockhash(), cand_a["hash"])  # tip did not move again/duplicate

        # Verify the payout landed on the requested address (item 4), on the
        # REAL accepted block.
        accepted_block = node.getblock(cand_a["hash"], 2)
        coinbase_tx = accepted_block["tx"][0]
        payout_vout = coinbase_tx["vout"][0]
        assert_equal(payout_vout["scriptPubKey"]["address"], payout_addr)
        assert_equal(payout_vout["value"], Decimal(cand_a["coinbasevalue"]) / 100_000_000)

        # -------------------------------------------------------------
        # item 9 / duplicate-vs-stale exact test C: candidate A's own
        # sibling, cand_b (same original tip, never itself submitted), is
        # now stale -- a DIFFERENT block (cand_a) advanced the tip, not a
        # resubmission of cand_b itself, so no idempotent-retry exception
        # applies and this must still be a hard stale error.
        # -------------------------------------------------------------
        self.log.info("stale candidate rejected once the tip has moved (cand_b's tip is gone)")
        assert_raises_rpc_error(-8, "stale", self.build_and_submit, node, cand_b)  # test C

        # -------------------------------------------------------------
        # item 9: unknown candidate hash rejected
        # -------------------------------------------------------------
        self.log.info("unknown/evicted candidate hash rejected")
        fake_hash = "ab" * 32
        # submitauxblock decodes the auxpow payload BEFORE looking up the
        # candidate (so malformed encodings are rejected uniformly,
        # regardless of whether the hash happens to be known -- item 6); use
        # a well-formed, arbitrary proof here so the request actually
        # reaches the candidate-cache lookup this test means to exercise.
        arbitrary_proof = build_valid_auxpow(int(fake_hash, 16), uint256_from_compact(0x207fffff), BITAI_AUXPOW_CHAIN_ID)
        assert_raises_rpc_error(-8, "Unknown or evicted", node.submitauxblock, fake_hash, arbitrary_proof.serialize().hex())

        # -------------------------------------------------------------
        # item 9: malformed auxpow encodings rejected (item 6)
        # -------------------------------------------------------------
        self.log.info("malformed auxpow payloads are rejected with a clear decode error, not a crash")
        cand_c = node.createauxblock(payout_addr)
        target_c = target_from_auxpow_rpc_hex(cand_c["target"])
        good_proof = build_valid_auxpow(int(cand_c["hash"], 16), target_c, BITAI_AUXPOW_CHAIN_ID)
        good_hex = good_proof.serialize().hex()

        assert_raises_rpc_error(-22, None, node.submitauxblock, cand_c["hash"], "not_hex")
        assert_raises_rpc_error(-22, None, node.submitauxblock, cand_c["hash"], good_hex[:-4])  # truncated
        assert_raises_rpc_error(-22, None, node.submitauxblock, cand_c["hash"], good_hex + "ff")  # trailing garbage

        # Oversized merkle branch vector: MAX_MERKLE_BRANCH_LENGTH is 32
        # (src/auxpow.h); re-serializing a proof with more entries than that
        # via the real (de)serializer exercises our own explicit post-decode
        # length check (item 6 -- no second, hand-rolled AuxPoW parser).
        oversized = copy.deepcopy(good_proof)
        oversized.vMerkleBranch = [0] * 40  # MAX_MERKLE_BRANCH_LENGTH is well under 32 in this tree
        assert_raises_rpc_error(-22, None, node.submitauxblock, cand_c["hash"], oversized.serialize().hex())

        # -------------------------------------------------------------
        # item 9: consensus-rejected proofs -- wrong chain ID, wrong child
        # commitment, wrong parent coinbase merkle proof, wrong chain-merkle
        # index -- must each come back cleanly as `false`, not an exception,
        # since these are real, well-formed-but-invalid submissions that go
        # through the full validation pipeline (item 7).
        # -------------------------------------------------------------
        self.log.info("wrong chain ID rejected (false, not an exception)")
        cand_d = node.createauxblock(payout_addr)
        target_d = target_from_auxpow_rpc_hex(cand_d["target"])
        wrong_chain_proof = self.build_wrong_chain_id_auxpow(int(cand_d["hash"], 16), target_d)
        result_d = node.submitauxblock(cand_d["hash"], wrong_chain_proof.serialize().hex())
        assert_equal(result_d, False)
        assert_equal(node.getbestblockhash(), cand_a["hash"])  # tip unchanged

        self.log.info("wrong child commitment rejected")
        cand_e = node.createauxblock(payout_addr)
        target_e = target_from_auxpow_rpc_hex(cand_e["target"])
        wrong_commitment_proof = build_valid_auxpow(int(cand_e["hash"], 16) ^ 1, target_e, BITAI_AUXPOW_CHAIN_ID)
        result_e = node.submitauxblock(cand_e["hash"], wrong_commitment_proof.serialize().hex())
        assert_equal(result_e, False)

        self.log.info("wrong parent coinbase merkle proof rejected")
        cand_f = node.createauxblock(payout_addr)
        result_f = self.build_and_submit(node, cand_f, corrupt=lambda p: setattr(p, "vMerkleBranch", [0xdeadbeef]))
        assert_equal(result_f, False)

        self.log.info("wrong chain-merkle branch/index rejected")
        cand_g = node.createauxblock(payout_addr)
        result_g = self.build_and_submit(node, cand_g, corrupt=lambda p: setattr(p, "nChainIndex", 1))
        assert_equal(result_g, False)

        # -------------------------------------------------------------
        # item 9 / duplicate-vs-stale exact tests D/E (docs/AUXPOW_MILESTONE.md
        # sec.10.12): (D) an invalid proof for the current candidate returns
        # false and the candidate remains reusable (not burned/evicted); (E)
        # a valid proof afterward for that SAME candidate still succeeds.
        # -------------------------------------------------------------
        self.log.info("invalid submission does not burn the candidate; a later valid submission still succeeds")
        cand_h = node.createauxblock(payout_addr)
        bad_result = self.build_and_submit(node, cand_h, corrupt=lambda p: setattr(p, "nChainIndex", 1))  # test D
        assert_equal(bad_result, False)
        good_result = self.build_and_submit(node, cand_h)  # test E
        assert_equal(good_result, True)
        assert_equal(node.getbestblockhash(), cand_h["hash"])

        # -------------------------------------------------------------
        # sec.11.7 exact tests A-D: the idempotent-retry shortcut must fire
        # ONLY for an already-accepted, ACTUAL AuxPoW block -- not for any
        # already-valid active-chain block whatsoever (a real gap found and
        # fixed before this milestone was frozen: the original check would
        # have incorrectly returned true for submitauxblock(<any accepted
        # block's hash>, <anything>), including an ordinary direct-mined
        # block that was never an AuxPoW submission at all).
        # -------------------------------------------------------------
        self.log.info("idempotent-retry shortcut is narrowed to real AuxPoW blocks only")

        # Test A: accepted AuxPoW block hash + the SAME proof -> true.
        cand_idem = node.createauxblock(payout_addr)
        target_idem = target_from_auxpow_rpc_hex(cand_idem["target"])
        proof_idem_hex = build_valid_auxpow(int(cand_idem["hash"], 16), target_idem, BITAI_AUXPOW_CHAIN_ID).serialize().hex()
        assert_equal(node.submitauxblock(cand_idem["hash"], proof_idem_hex), True)  # test A
        assert_equal(node.getbestblockhash(), cand_idem["hash"])

        # Test B: accepted AuxPoW block hash + a malformed/irrelevant proof
        # -> STILL true, because the idempotent check runs before the
        # auxpow-hex decode: the already-valid active AuxPoW block is
        # authoritative for the retry, regardless of what was resubmitted.
        assert_equal(node.submitauxblock(cand_idem["hash"], "not_hex_at_all"), True)  # test B
        assert_equal(node.submitauxblock(cand_idem["hash"], "aabbcc"), True)  # test B, structurally-truncated too

        # Test C: an accepted, post-activation, DIRECT-mined block's hash +
        # an arbitrary well-formed AuxPoW proof -> must NOT return true via
        # the idempotent shortcut (IsRealAuxpow() is false for this block's
        # real nVersion -- it never carries VERSION_AUXPOW). It must instead
        # fall through to the normal candidate lookup and fail as
        # unknown/evicted, since this hash was never an outstanding AuxPoW
        # candidate.
        direct_block_hash = self.generatetoaddress(node, 1, payout_addr)[0]
        direct_target = uint256_from_compact(int(node.getblock(direct_block_hash)["bits"], 16))
        arbitrary_proof_for_direct = build_valid_auxpow(int(direct_block_hash, 16), direct_target, BITAI_AUXPOW_CHAIN_ID)
        assert_raises_rpc_error(-8, "Unknown or evicted", node.submitauxblock,
                                 direct_block_hash, arbitrary_proof_for_direct.serialize().hex())  # test C
        assert_equal(node.getbestblockhash(), direct_block_hash)  # unaffected either way

        # Test D: a historical/pre-activation active-chain block hash (the
        # genesis block, height 0 -- regtest AuxPoW activation height is 1)
        # must never qualify for the idempotent shortcut either, even though
        # it is trivially "valid" and "on the active chain": the explicit
        # height >= BitAIAuxpowActivationHeight guard rejects it before
        # IsRealAuxpow() is even consulted.
        genesis_hash = node.getblockhash(0)
        genesis_target = uint256_from_compact(int(node.getblock(genesis_hash)["bits"], 16))
        arbitrary_proof_for_genesis = build_valid_auxpow(int(genesis_hash, 16), genesis_target, BITAI_AUXPOW_CHAIN_ID)
        assert_raises_rpc_error(-8, "Unknown or evicted", node.submitauxblock,
                                 genesis_hash, arbitrary_proof_for_genesis.serialize().hex())  # test D
        assert_equal(node.getbestblockhash(), direct_block_hash)  # unaffected

        # -------------------------------------------------------------
        # item 9: attaching a proof never changes the candidate's child hash
        # -------------------------------------------------------------
        self.log.info("attaching a proof does not change the candidate's child hash")
        # Implicit throughout: every accepted submission above resulted in
        # getbestblockhash() == the ORIGINAL createauxblock "hash" value,
        # even though a full auxpow (with real transaction/header bytes) was
        # attached in between. Also enforced as a live server-side invariant
        # (CHECK_NONFATAL(block_copy->GetHash() == hash) in rpc/auxpow.cpp).

        # -------------------------------------------------------------
        # item 9: simultaneous submissions against one cached candidate
        # cannot corrupt cache state.
        # -------------------------------------------------------------
        self.log.info("concurrent submissions for the same still-current candidate do not corrupt state")
        cand_i = node.createauxblock(payout_addr)
        target_i = target_from_auxpow_rpc_hex(cand_i["target"])
        proof_i_hex = build_valid_auxpow(int(cand_i["hash"], 16), target_i, BITAI_AUXPOW_CHAIN_ID).serialize().hex()

        results = []
        errors = []

        def submit_once():
            # A dedicated RPC connection per thread: AuthServiceProxy's
            # single persistent HTTP connection is not itself safe for
            # concurrent calls from multiple threads (that would be a test
            # harness artifact, not a real node-side race) -- the real
            # concurrency this test means to exercise is inside the node's
            # own multi-threaded RPC server (rpcthreads) and
            # AuxBlockCandidateCache, not in this Python client.
            thread_rpc = AuthServiceProxy(node.url, timeout=60)
            try:
                results.append(thread_rpc.submitauxblock(cand_i["hash"], proof_i_hex))
            except Exception as e:  # noqa: BLE001 -- deliberately broad: we only assert no crash/hang below
                errors.append(e)

        threads = [threading.Thread(target=submit_once) for _ in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=60)
        assert all(not t.is_alive() for t in threads), "a concurrent submitauxblock call hung"
        assert_equal(node.getbestblockhash(), cand_i["hash"])
        self.log.info(f"race outcome: results={results} errors={[str(e) for e in errors]}")
        assert True in results, f"no concurrent submission succeeded: results={results} errors={errors}"
        # With the idempotent-retry fix (docs/AUXPOW_MILESTONE.md sec.10.12),
        # every racer submitting this SAME (hash, proof) pair is expected to
        # see `true` in the common case -- either the first-time real
        # acceptance, ProcessNewBlock's own "already known and valid"
        # duplicate recognition, or the new early idempotent check finding
        # the hash already fully valid on the active chain. A "stale"
        # RPC_INVALID_PARAMETER (-8) remains possible only in the narrow
        # window between a racer's own idempotent-check lock acquisition and
        # its own later staleness-check lock acquisition, if another racer's
        # acceptance lands in between -- also an acceptable, clean outcome
        # (never a crash, a hang, or a wrong tip).
        for e in errors:
            assert "-8" in str(e) or "stale" in str(e).lower() or "Unknown or evicted" in str(e), \
                f"unexpected error from a concurrent submission: {e}"
        # The cache itself must still be fully functional afterward.
        cand_after_race = node.createauxblock(payout_addr)
        assert_equal(self.build_and_submit(node, cand_after_race), True)

        self.log.info("all createauxblock/submitauxblock RPC-surface adversarial tests passed")


if __name__ == "__main__":
    AuxPowRPCTest(__file__).main()
