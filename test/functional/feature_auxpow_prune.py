#!/usr/bin/env python3
# Copyright (c) 2026 The BitAIcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test AuxPoW's on-demand proof storage design under real node restarts and
real block pruning (docs/AUXPOW_MILESTONE.md sec.5, amendment 3, 2026-09-23).

This is the honest, node-level counterpart to the in-process
ChainstateManager tests in src/test/auxpow_tests.cpp: it exercises the same
validation/storage code through a REAL running bitaicoind, across REAL
process restarts and REAL blk-file pruning, which an in-process C++ unit
test fixture cannot do.

Sequence:
  1. Mine a short real chain, then submit one real AuxPoW-flagged block via
     real P2P (the wire format actually used by net_processing.cpp today,
     since CheckAuxPowRules() is not yet spliced into the live path -- see
     the HONEST SCOPE note in src/test/auxpow_tests.cpp for why this is
     accepted at an otherwise-arbitrary low height).
  2. Restart the node; confirm the AuxPoW block's chain state survives and
     its proof is still readable on demand (LoadBlockIndexGuts's
     readAuxPowHeader callback re-derives PoW validity from the still-present
     blk file, exactly as intended by the on-demand design).
  3. Mine enough further blocks and prune, so the blk file holding the
     AuxPoW proof is deleted. NOT identical to stock pruning's own
     trade-off: a stock pruned node still keeps every 80-byte header
     resident in CBlockIndex forever; a BitAIcoin pruned node additionally
     loses the AuxPoW PROOF once its blk file goes, while the lean
     CBlockIndex metadata (including IsAuxpowVersion()) is, like stock
     headers, never pruned.
  4. Restart again; confirm the node comes up cleanly (LoadBlockIndexGuts
     does not require the now-unavailable proof to revalidate startup) and
     the chain/index state remains correct.
  5. Confirm a live peer's getheaders request spanning the pruned AuxPoW
     block gets the REAL, intended fallback (net_processing.cpp: an
     unreadable on-demand proof means the HEADERS loop stops before that
     entry) rather than a crash, a hang, or a header sent without its proof.
"""

from test_framework.auxpow import (
    BITAI_AUXPOW_CHAIN_ID,
    CBlockWithAuxPow,
    build_valid_auxpow,
    make_auxpow_version,
)
from test_framework.blocktools import (
    MIN_BLOCKS_TO_KEEP,
    add_witness_commitment,
    create_coinbase,
)
from test_framework.messages import (
    msg_block,
    msg_getheaders,
    uint256_from_compact,
)
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


class FeatureAuxpowPruneTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # -fastprune: tiny blk files, so a modest number of blocks after the
        # AuxPoW block is enough to roll it into a separate, prunable file.
        # -prune=1: manual pruning mode (pruneblockchain RPC available,
        # no automatic background pruning to race against).
        self.extra_args = [["-fastprune", "-prune=1"]]

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

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Mining a short real chain, then submitting one real AuxPoW block via P2P")
        self.generate(node, 3, sync_fun=self.no_op)
        tip = node.getbestblockhash()
        tip_height = node.getblockcount()

        auxblock = self.build_auxpow_block(node, tip, tip_height + 1)
        auxblock_hash_hex = f"{auxblock.hash_int:064x}"

        peer = node.add_p2p_connection(P2PInterface())
        peer.send_and_ping(msg_block(auxblock))
        self.wait_until(lambda: node.getbestblockhash() == auxblock_hash_hex)
        assert_equal(node.getblockcount(), tip_height + 1)
        node.disconnect_p2ps()

        self.log.info("Restarting the node; the AuxPoW block's proof must still be readable on demand")
        self.restart_node(0, extra_args=self.extra_args[0])
        assert_equal(node.getbestblockhash(), auxblock_hash_hex)
        # The lean index metadata survives any restart regardless of pruning;
        # this alone doesn't prove the ON-DEMAND proof read path specifically,
        # but LoadBlockIndex's real PoW-recheck already exercised
        # ReadBlockHeaderWithAuxPow() during this restart (it re-verifies
        # every historical AuxPoW header's PoW at startup) -- if that read had
        # failed or misbehaved, the node would not have come back up with the
        # correct tip at all.
        assert_equal(node.getblockheader(auxblock_hash_hex)["height"], tip_height + 1)

        # -fastprune rolls blk files at 64KiB; empirically that's roughly
        # 200-250 small regtest blocks per file. pruneblockchain() also
        # silently clamps any requested height down to
        # (chain_height - MIN_BLOCKS_TO_KEEP), so the chain needs to be long
        # enough that even the CLAMPED height still lands past the AuxPoW
        # block's own (early, small) blk file -- not just past the AuxPoW
        # block's literal height. 300 extra blocks of margin on top of
        # MIN_BLOCKS_TO_KEEP comfortably covers that file boundary.
        EXTRA_BLOCKS_FOR_FILE_ROLLOVER = 300
        self.log.info(f"Mining {MIN_BLOCKS_TO_KEEP + EXTRA_BLOCKS_FOR_FILE_ROLLOVER} further blocks and pruning past the AuxPoW block")
        self.generate(node, MIN_BLOCKS_TO_KEEP + EXTRA_BLOCKS_FOR_FILE_ROLLOVER, sync_fun=self.no_op)
        prune_target_height = node.getblockcount() - MIN_BLOCKS_TO_KEEP
        assert prune_target_height > tip_height + 1, "test needs the AuxPoW block to be safely prunable"
        actual_pruned_to = node.pruneblockchain(prune_target_height)
        assert actual_pruned_to >= 0, (
            "pruneblockchain() pruned nothing (-1) -- the AuxPoW block's blk file "
            "was not yet eligible; increase EXTRA_BLOCKS_FOR_FILE_ROLLOVER"
        )

        # Direct proof the blk file holding the AuxPoW block's raw data
        # (including its proof) is genuinely gone -- NOT the same as stock
        # pruning's own trade-off (see module docstring): getblock (which
        # needs the raw block, and therefore the auxpow bytes for this
        # specific block) must now fail, while getblockheader (lean,
        # never-pruned CBlockIndex metadata only) must keep working.
        assert_raises_rpc_error(-1, "Block not available (pruned data)", node.getblock, auxblock_hash_hex)
        header_after_prune = node.getblockheader(auxblock_hash_hex)
        assert_equal(header_after_prune["height"], tip_height + 1)
        pruneheight_before_restart = node.getblockchaininfo()["pruneheight"]
        assert pruneheight_before_restart > tip_height + 1
        # Sanity: pruneblockchain()'s own returned height and
        # getblockchaininfo()'s separately-computed GetPruneHeight() need not
        # be byte-identical (different internal conventions for "how far
        # pruned" -- confirmed by direct observation, off by one in this
        # run), but must be close and on the same side of the AuxPoW block.
        assert abs(pruneheight_before_restart - actual_pruned_to) <= 1

        self.log.info("Restarting again with the AuxPoW proof's blk file gone")
        chain_height_before_restart = node.getblockcount()
        self.restart_node(0, extra_args=self.extra_args[0])
        assert_equal(node.getblockcount(), chain_height_before_restart)
        assert_equal(node.getblockheader(auxblock_hash_hex)["height"], tip_height + 1)
        # The real invariant that matters: pruneheight survives a restart
        # unchanged (the pruned blk file does not come back, and the node
        # does not need to re-derive this from scratch).
        assert_equal(node.getblockchaininfo()["pruneheight"], pruneheight_before_restart)

        self.log.info("A getheaders request spanning the pruned AuxPoW block must fall back cleanly, not crash")
        peer2 = node.add_p2p_connection(P2PInterface())
        getheaders_msg = msg_getheaders()
        getheaders_msg.locator.vHave = [int(node.getblockhash(tip_height), 16)]  # last block BEFORE the AuxPoW block
        getheaders_msg.hashstop = 0
        peer2.send_and_ping(getheaders_msg)
        # The real, intended fallback (net_processing.cpp's getheaders-response
        # loop): GetHeaderForAnnounce() returns nullopt for the pruned
        # AuxPoW-flagged entry, so the loop `break`s BEFORE appending it --
        # the node responds with a HEADERS message that is empty (or, if any
        # non-AuxPoW headers existed in between, stops short of the pruned
        # entry), never a crash, a hang, or a header sent without its proof.
        peer2.wait_until(lambda: "headers" in peer2.last_message)
        received = peer2.last_message["headers"].headers
        assert_equal(len(received), 0)
        # The node is still alive and answers ordinary RPCs normally after
        # serving that response -- the fallback did not corrupt any state.
        assert_equal(node.getblockcount(), chain_height_before_restart)
        node.disconnect_p2ps()


if __name__ == "__main__":
    FeatureAuxpowPruneTest(__file__).main()
