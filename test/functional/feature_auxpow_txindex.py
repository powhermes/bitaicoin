#!/usr/bin/env python3
# Copyright (c) 2026 The BitAIcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression test: -txindex / -txospenderindex lookups of transactions that
sit in an AuxPoW block.

BUG (fixed): TxIndex::FindTx() and TxoSpenderIndex::ReadTransaction() read
the block header back from blk*.dat with plain `file >> header` (80 bytes),
then seek CDiskTxPos::nTxOffset. On disk an AuxPoW block is
header + CAuxPow proof + vtx, and nTxOffset is relative to the END of that
header record (proof included), so the seek landed inside the proof and the
lookup failed with "txid mismatch" (getrawtransaction <txid> without a
blockhash -> "No such mempool or blockchain transaction").

Read-path only: no consensus, block-file or index-database format change;
offsets already stored in an existing index stay valid (no -reindex).
"""

from test_framework.auxpow import (
    BITAI_AUXPOW_CHAIN_ID,
    CBlockWithAuxPow,
    build_valid_auxpow,
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
from test_framework.wallet import MiniWallet


class FeatureAuxpowTxindexTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-txindex", "-txospenderindex"]]

    def submit_auxpow_block(self, node, peer, extra_txs=()):
        prev_hash_hex = node.getbestblockhash()
        height = node.getblockcount() + 1
        required_bits = int(node.getblockheader(prev_hash_hex)["bits"], 16)

        block = CBlockWithAuxPow()
        block.nVersion = make_auxpow_version(BITAI_AUXPOW_CHAIN_ID, 4)
        block.hashPrevBlock = int(prev_hash_hex, 16)
        block.nTime = node.getblockheader(prev_hash_hex)["time"] + 1
        block.nBits = required_bits
        block.nNonce = 0
        block.vtx = [create_coinbase(height=height)] + list(extra_txs)
        add_witness_commitment(block)
        block.auxpow = build_valid_auxpow(block.hash_int, uint256_from_compact(required_bits), BITAI_AUXPOW_CHAIN_ID)

        block_hash_hex = f"{block.hash_int:064x}"
        peer.send_and_ping(msg_block(block))
        self.wait_until(lambda: node.getbestblockhash() == block_hash_hex)
        return block_hash_hex

    def wait_indexes_synced(self, node):
        self.wait_until(lambda: all(i["synced"] for i in node.getindexinfo().values()))

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)
        peer = node.add_p2p_connection(P2PInterface())

        self.log.info("Direct block, then mature MiniWallet coins")
        direct_hash = self.generate(wallet, 1, sync_fun=self.no_op)[0]
        self.generate(node, 100, sync_fun=self.no_op)

        self.log.info("AuxPoW block carrying a coinbase and a spend")
        spend = wallet.create_self_transfer()
        spent_outpoint = spend["tx"].vin[0].prevout
        aux_hash = self.submit_auxpow_block(node, peer, extra_txs=[spend["tx"]])
        assert node.getblockheader(aux_hash)["versionHex"].startswith("4249")
        aux_block = node.getblock(aux_hash)
        aux_coinbase, aux_spend = aux_block["tx"][0], aux_block["tx"][1]
        assert_equal(aux_spend, spend["txid"])

        self.generate(node, 1, sync_fun=self.no_op)  # direct descendant
        self.wait_indexes_synced(node)

        self.log.info("txindex: direct-block tx (control)")
        direct_cb = node.getblock(direct_hash)["tx"][0]
        assert_equal(node.getrawtransaction(direct_cb, True)["blockhash"], direct_hash)

        self.log.info("txindex: AuxPoW-block coinbase and spend, no blockhash hint")
        for txid in (aux_coinbase, aux_spend):
            res = node.getrawtransaction(txid, True)
            assert_equal(res["txid"], txid)
            assert_equal(res["blockhash"], aux_hash)

        self.log.info("txospenderindex: spender sitting in the AuxPoW block")
        res = node.gettxspendingprevout(
            [{"txid": f"{spent_outpoint.hash:064x}", "vout": spent_outpoint.n}],
            {"mempool_only": False, "return_spending_tx": True},
        )[0]
        assert_equal(res["spendingtxid"], aux_spend)
        assert_equal(res["blockhash"], aux_hash)

        self.log.info("Restart: stored offsets still resolve (no reindex needed)")
        self.restart_node(0)
        self.wait_indexes_synced(node)
        assert_equal(node.getrawtransaction(aux_spend, True)["blockhash"], aux_hash)


if __name__ == '__main__':
    FeatureAuxpowTxindexTest(__file__).main()
