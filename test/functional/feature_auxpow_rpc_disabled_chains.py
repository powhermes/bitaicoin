#!/usr/bin/env python3
# Copyright (c) 2026 The BitAIcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""createauxblock/submitauxblock must not expose functioning merge-mining
work merely because the RPC is registered (docs/AUXPOW_MILESTONE.md sec.10
item 2): on an ordinary chain (fBitAIAuxpowEnabled == false --
MAIN/TESTNET/TESTNET4/SIGNET), createauxblock must fail clearly and
immediately, regardless of height.

Run on testnet4 as the representative "ordinary chain" (an alternate real
PoW chain type with no signet-challenge/mainnet-checkpoint complications) --
the underlying per-chain-type fBitAIAuxpowEnabled/BitAIAuxpowActivationHeight
values themselves are already exhaustively unit-tested for every chain type
in src/test/auxpow_tests.cpp (auxpow_enabled_flag_matches_the_desired_per_chain_state);
this test only proves the RPC actually consults that real flag rather than
assuming activity.
"""
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_raises_rpc_error


class AuxPowRPCDisabledChainsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.chain = "testnet4"
        self.setup_clean_chain = True

    def add_options(self, parser):
        pass

    def run_test(self):
        node = self.nodes[0]
        assert_raises_rpc_error(
            -1, "AuxPoW is not enabled on this chain",
            node.createauxblock, "mkHS9ne12qx9pS9VojpwU5xtRd4T7X7ZUt",
        )
        # submitauxblock must be gated the same way, independent of
        # createauxblock -- it re-checks EnsureAuxPowActiveOrThrow itself
        # (docs/AUXPOW_MILESTONE.md sec.10: "so they can't drift apart"),
        # so this must fail on the chain-awareness check, never reaching the
        # (nonexistent) candidate-cache lookup.
        assert_raises_rpc_error(
            -1, "AuxPoW is not enabled on this chain",
            node.submitauxblock, "00" * 32, "00",
        )
        self.log.info("createauxblock/submitauxblock correctly refuse to operate on an ordinary chain")


if __name__ == "__main__":
    AuxPowRPCDisabledChainsTest(__file__).main()
