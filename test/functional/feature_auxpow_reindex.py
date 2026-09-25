#!/usr/bin/env python3
# Copyright (c) 2026 The BitAIcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression test for a real full-`-reindex` AuxPoW disk-deserialization
defect found during the BitAIcoin activation rehearsal (documented in
docs/ACTIVATION_REHEARSAL_227808.md's discrepancy section).

BUG (fixed): `ChainstateManager::LoadExternalBlockFile()` (src/validation.cpp,
used by both full `-reindex` and `-loadblock=<file>` external import --
node::ImportBlocks calls it for both) deserialized blocks read back from
blk*.dat using plain, non-AuxPoW-aware `CBlock` deserialization
(`blkdat >> TX_WITH_WITNESS(*pblock)`), unlike every other block-read call
site in the codebase (BlockManager::ReadBlock via AuxPowBlockForRecv,
net_processing's P2P receive, rest.cpp, core_io.cpp), which are all
chain-aware. A real, valid, already-on-disk AuxPoW block would therefore be
reconstructed in memory WITHOUT its CAuxPow proof, fail BitAIcoin's own
"auxpow-missing" consensus check, and the reindexed chain would silently
truncate at the block immediately before the first AuxPoW block -- with the
node reporting itself healthy afterward (no crash, no fatal error).

Confirmed NOT to affect (all separately verified against the real rehearsal
fixtures): ordinary restart, live P2P sync/validation, or `-reindex-chainstate`
(which rebuilds only the chainstate from the already-indexed block index via
the correct `ReadBlock` path, never calling `LoadExternalBlockFile` at all).

This test builds a real chain where the very first block after genesis is
itself AuxPoW (the exact real failing shape -- "AuxPoW as the first
post-activation block"), interleaved with direct blocks (proving the disk
scanner correctly returns to the next block boundary after a variable-length
AuxPoW record, not just when AuxPoW happens to be last), plus a real
descendant after the final AuxPoW block (so a regression shows up as
truncation, not merely "failed to extend"). Runs both full `-reindex` and
`-reindex-chainstate` and requires byte-identical final state either way.
"""

from test_framework.auxpow import (
    BITAI_AUXPOW_CHAIN_ID,
    CBlockWithAuxPow,
    build_valid_auxpow,
    is_auxpow_version,
    make_auxpow_version,
)
from test_framework.blocktools import (
    add_witness_commitment,
    create_coinbase,
)
from test_framework.messages import (
    msg_block,
    uint256_from_compact,
)
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class FeatureAuxpowReindexTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # -blocksxor=0 on node0 only: its blk00000.dat is then stored raw/
        # unobfuscated, so it can be reused directly as an external
        # bootstrap-style file for node1's -loadblock test (item 11) --
        # -loadblock's LoadExternalBlockFile call reads the given file
        # completely raw, with no XOR-deobfuscation step of its own (that is
        # purely an internal on-disk storage feature for a node's own
        # managed blocksdir, confirmed by reading the function: it never
        # touches the datadir's own xor.dat key at all). node1 keeps its
        # own default (random) obfuscation for its own internal storage --
        # irrelevant to how it reads the externally-supplied file.
        self.extra_args = [["-blocksxor=0"], []]

    def build_auxpow_block(self, node, prev_hash_hex, height):
        prev_hash_int = int(prev_hash_hex, 16)
        required_bits = int(node.getblockheader(prev_hash_hex)["bits"], 16)

        block = CBlockWithAuxPow()
        block.nVersion = make_auxpow_version(BITAI_AUXPOW_CHAIN_ID, 4)
        block.hashPrevBlock = prev_hash_int
        block.nTime = node.getblockheader(prev_hash_hex)["time"] + 1
        block.nBits = required_bits
        block.nNonce = 0  # never checked for an AuxPoW-flagged block
        block.vtx = [create_coinbase(height=height)]
        add_witness_commitment(block)  # segwit is active from genesis on regtest; also sets hashMerkleRoot

        target_aux = uint256_from_compact(required_bits)
        block.auxpow = build_valid_auxpow(block.hash_int, target_aux, BITAI_AUXPOW_CHAIN_ID)
        return block

    def submit_auxpow_block(self, node, peer, prev_hash_hex, height):
        block = self.build_auxpow_block(node, prev_hash_hex, height)
        block_hash_hex = f"{block.hash_int:064x}"
        peer.send_and_ping(msg_block(block))
        self.wait_until(lambda: node.getbestblockhash() == block_hash_hex)
        assert_equal(node.getblockcount(), height)
        return block_hash_hex

    def check_state(self, node, label):
        info = node.getblockchaininfo()
        self.log.info(f"[{label}] height={info['blocks']} hash={info['bestblockhash']} "
                       f"chainwork={info['chainwork']}")
        return {"height": info["blocks"], "hash": info["bestblockhash"], "chainwork": info["chainwork"]}

    def run_test(self):
        node = self.nodes[0]

        self.log.info("=== Building: AuxPoW-first, then direct/AuxPoW/direct/AuxPoW/direct, then a plain descendant ===")
        peer = node.add_p2p_connection(P2PInterface())

        genesis_hash = node.getbestblockhash()
        assert_equal(node.getblockcount(), 0)

        # Item 7: AuxPoW as the literal FIRST post-genesis/post-activation block
        # (BitAIAuxpowActivationHeight=1 on regtest -- see chainparams.cpp).
        h1 = self.submit_auxpow_block(node, peer, genesis_hash, 1)
        self.log.info(f"height 1 (AuxPoW, first post-genesis block): {h1}")

        # Item 6: interleaved mixed chain -- direct, AuxPoW, direct, AuxPoW, direct.
        self.generate(node, 1, sync_fun=self.no_op)          # height 2, direct
        h3 = self.submit_auxpow_block(node, peer, node.getbestblockhash(), 3)   # height 3, AuxPoW
        self.generate(node, 1, sync_fun=self.no_op)          # height 4, direct
        h5 = self.submit_auxpow_block(node, peer, node.getbestblockhash(), 5)   # height 5, AuxPoW
        self.generate(node, 1, sync_fun=self.no_op)          # height 6, direct descendant -- proves
                                                              # a regression shows as truncation, not
                                                              # merely "chain failed to extend"
        node.disconnect_p2ps()

        auxpow_hashes = {1: h1, 3: h3, 5: h5}
        before = self.check_state(node, "BEFORE reindex")
        assert_equal(before["height"], 6)

        for h, expected_hash in auxpow_hashes.items():
            bh = node.getblockhash(h)
            assert_equal(bh, expected_hash)
            block = node.getblock(bh, 2)
            assert is_auxpow_version(int(block["versionHex"], 16)), f"height {h} should carry VERSION_AUXPOW"

        self.log.info("=== Stopping cleanly, restarting with full -reindex ===")
        self.stop_node(0)
        self.start_node(0, extra_args=self.extra_args[0] + ["-reindex"])
        # NOTE: deliberately do NOT wait on `not initialblockdownload` --
        # on a genuinely bugged binary, the AuxPoW block at height 1 fails its
        # own "auxpow-missing" consensus check, LoadExternalBlockFile hits
        # real EOF immediately after (having imported only genesis), and
        # "Reindexing finished" logs within milliseconds; a chain stuck at
        # genesis's placeholder 2011 timestamp never satisfies the IBD-exit
        # heuristic, so that wait would just time out uninformatively rather
        # than surface the real, fast failure. A short, generous, bounded
        # wait on the real target height, tolerating (not requiring) a
        # timeout, means a truncation shows up immediately below as an exact,
        # informative height/hash assertion failure rather than a bare
        # wait_until timeout exception.
        try:
            self.wait_until(lambda: node.getblockcount() == 6, timeout=30)
        except AssertionError:
            pass  # fall through -- the explicit assert below reports the real, exact stuck height

        after_reindex = self.check_state(node, "AFTER full -reindex")
        assert_equal(after_reindex, before)

        self.log.info("Verifying every AuxPoW block is readable, correctly flagged, and its proof intact after reindex")
        for h, expected_hash in auxpow_hashes.items():
            bh = node.getblockhash(h)
            assert_equal(bh, expected_hash)
            block = node.getblock(bh, 2)
            assert is_auxpow_version(int(block["versionHex"], 16)), f"height {h}: AuxPoW bit lost after reindex"
            assert_equal(block["height"], h)

        # The literal regression symptom: without the fix, this would be
        # truncated at height 0 (the block before the first AuxPoW block,
        # which here is genesis itself).
        assert_equal(node.getblockcount(), 6)
        assert_equal(node.getbestblockhash(), node.getblockhash(6))
        self.log.info("PASS: full -reindex crosses the first-post-genesis AuxPoW block and all "
                       "interleaved AuxPoW blocks correctly; no silent truncation.")

        self.log.info("=== Re-running -reindex-chainstate on the same final state (must also match, no regression) ===")
        self.stop_node(0)
        self.start_node(0, extra_args=self.extra_args[0] + ["-reindex-chainstate"])
        try:
            self.wait_until(lambda: node.getblockcount() == 6, timeout=30)
        except AssertionError:
            pass
        after_reindex_chainstate = self.check_state(node, "AFTER -reindex-chainstate")
        assert_equal(after_reindex_chainstate, before)
        for h, expected_hash in auxpow_hashes.items():
            block = node.getblock(node.getblockhash(h), 2)
            assert is_auxpow_version(int(block["versionHex"], 16))
        self.log.info("PASS: -reindex-chainstate still matches exactly (unaffected by the read-path fix, as expected).")

        self.log.info("=== Item 11: external block-file import (-loadblock) shares the same LoadExternalBlockFile "
                       "code path as -reindex -- confirming the same fix also corrects that path ===")
        node.stop_node()
        bootstrap_file = self.nodes[0].blocks_path / "blk00000.dat"
        assert bootstrap_file.exists(), "expected node0's raw (unobfuscated, -blocksxor=0) blk00000.dat to exist"

        node1 = self.nodes[1]
        self.log.info(f"Importing {bootstrap_file} into a completely fresh node via -loadblock")
        # node1 was already auto-started (empty, genesis-only) by the framework's
        # own setup_nodes() -- stop it and wipe its chain data so it is
        # genuinely, verifiably fresh before this import, not merely unused.
        self.stop_node(1)
        import shutil
        shutil.rmtree(node1.chain_path)
        self.start_node(1, extra_args=[f"-loadblock={bootstrap_file}"])
        try:
            self.wait_until(lambda: node1.getblockcount() == 6, timeout=30)
        except AssertionError:
            pass
        after_loadblock = self.check_state(node1, "AFTER -loadblock import")
        assert_equal(after_loadblock, before)
        for h, expected_hash in auxpow_hashes.items():
            bh = node1.getblockhash(h)
            assert_equal(bh, expected_hash)
            block = node1.getblock(bh, 2)
            assert is_auxpow_version(int(block["versionHex"], 16)), f"height {h}: AuxPoW bit lost after -loadblock import"
        self.log.info("PASS: -loadblock external import reaches the identical tip, with every AuxPoW block intact -- "
                       "the same read-path fix corrects both -reindex and -loadblock (node::ImportBlocks calls "
                       "ChainstateManager::LoadExternalBlockFile for both).")


if __name__ == "__main__":
    FeatureAuxpowReindexTest(__file__).main()
