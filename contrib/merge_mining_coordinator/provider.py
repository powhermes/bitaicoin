#!/usr/bin/env python3
# Copyright (c) 2026 The BitAIcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""ParentWorkProvider: the one adapter boundary this slice defines but does
NOT fully build out (docs/AUXPOW_MILESTONE.md merge-mining milestone, item 9).

Only SyntheticParentWorkProvider is implemented here. A real
BitcoinCoreGBTParentWorkProvider (talking to an actual Bitcoin Core node via
getblocktemplate/submitblock, handling longpoll, coinbaseaux, witness
commitments, etc.) is deliberately left as documented future work -- see
docs/MERGE_MINING_INTEGRATION.md sec.8. Building that now would drag this
slice into real Bitcoin GBT semantics, which is explicitly out of scope.
"""

from __future__ import annotations

import time
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import List

from wire import BlockHeader, Transaction, TxIn, TxOut, make_coinbase


@dataclass
class ParentTemplate:
    """A parent-chain block candidate BEFORE the merge-mining commitment has
    been inserted -- i.e. before the coinbase (and therefore the header's
    hashMerkleRoot) is final. `coinbase.vin[0].script_sig` at this point
    contains only provider-specific filler (extranonce, height push, etc.),
    never a merge-mining tag yet."""
    coinbase: Transaction
    header: BlockHeader  # hash_merkle_root is NOT yet final at this stage


@dataclass
class ParentCandidate:
    """A parent-chain block candidate AFTER the merge-mining commitment has
    been inserted: the coinbase now carries the tag, and the header's
    hashMerkleRoot is final. This is what actually gets mined (only the
    header's `time`/`nonce` may still change)."""
    coinbase: Transaction
    header: BlockHeader
    coinbase_branch: List[int]   # this reference implementation: always [] (single-tx parent)
    coinbase_index: int          # this reference implementation: always 0


@dataclass
class ParentSolution:
    """The exact evidence CAuxPow needs on the parent side, as handed back
    by whoever actually did the SHA256d work (item 4: this is what a real
    external pool would return to the coordinator -- the coordinator's core
    API never performs this work itself)."""
    header: BlockHeader          # final, solved (nonce set, block_hash() <= target)
    coinbase: Transaction
    coinbase_branch: List[int]
    coinbase_index: int


class ParentWorkProvider(ABC):
    """The parent-chain adapter boundary (item 9). Three conceptual
    operations, matching a real pool's actual workflow against ANY
    SHA256d parent chain:
      1. get a parent candidate/work (before any BitAIcoin commitment)
      2. insert/update the coinbase commitment (after MergeMiningCoordinator
         has built the merge-mining tag)
      3. receive solved parent work (hand the final, solved evidence back to
         whatever owns the real parent chain -- for a real Bitcoin adapter,
         this is where a real `submitblock` call to Bitcoin Core would go)
    """

    @abstractmethod
    def get_parent_candidate(self) -> ParentTemplate:
        raise NotImplementedError

    @abstractmethod
    def insert_coinbase_commitment(self, template: ParentTemplate, commitment_tag: bytes) -> ParentCandidate:
        raise NotImplementedError

    @abstractmethod
    def receive_solved_parent_work(self, solution: ParentSolution) -> None:
        """Hand the final, solved parent evidence to whatever owns the real
        parent chain. A real BitcoinCoreGBTParentWorkProvider would call
        Bitcoin Core's own `submitblock` here; the synthetic provider has no
        real parent chain to submit to, so it only validates/logs."""
        raise NotImplementedError


class SyntheticParentWorkProvider(ParentWorkProvider):
    """Deterministic, self-contained parent-work source requiring no real
    Bitcoin Core node at all (docs/MERGE_MINING_INTEGRATION.md sec.3): a
    single-transaction (coinbase-only) synthetic parent block. Its own
    `bits`/proof-of-work are NOT checked against any real Bitcoin difficulty
    by anything in this reference implementation or by bitaicoind's
    CAuxPow::Check() (which only ever checks the parent header's HASH
    against BitAIcoin's own required target, never the parent's own claimed
    `bits` field) -- this is a deliberate, documented simplification, not an
    oversight (see docs/MERGE_MINING_INTEGRATION.md sec.3)."""

    def __init__(self, extranonce_prefix: bytes = b"BAIC-coordinator-synthetic-parent"):
        self._extranonce_prefix = extranonce_prefix
        self._counter = 0

    def get_parent_candidate(self) -> ParentTemplate:
        self._counter += 1
        # Filler scriptSig content only -- an arbitrary, distinguishing
        # extranonce so repeated calls don't accidentally build identical
        # coinbases. Real consensus-relevant content (BIP34 height push,
        # real extranonce entropy) does not apply to this synthetic parent,
        # since nothing validates it as a real Bitcoin block.
        script_sig = self._extranonce_prefix + self._counter.to_bytes(8, "little")
        coinbase = make_coinbase(script_sig)
        header = BlockHeader(
            version=1,
            hash_prev_block=0,       # synthetic: no real parent chain history
            hash_merkle_root=0,      # not yet final -- set in insert_coinbase_commitment
            time=int(time.time()),
            bits=0x1D00FFFF,         # a plausible-looking value; never checked by CAuxPow::Check()
            nonce=0,
        )
        return ParentTemplate(coinbase=coinbase, header=header)

    def insert_coinbase_commitment(self, template: ParentTemplate, commitment_tag: bytes) -> ParentCandidate:
        coinbase = Transaction(
            version=template.coinbase.version,
            vin=[TxIn(
                prev_txid=template.coinbase.vin[0].prev_txid,
                prev_vout=template.coinbase.vin[0].prev_vout,
                script_sig=template.coinbase.vin[0].script_sig + commitment_tag,
                sequence=template.coinbase.vin[0].sequence,
            )],
            vout=list(template.coinbase.vout),
            locktime=template.coinbase.locktime,
        )
        header = BlockHeader(**template.header.__dict__)
        # Single-transaction parent block: the merkle root of a one-leaf
        # tree is simply that leaf's own hash -- the coinbase's txid
        # directly, with an EMPTY branch and index 0. This is the same
        # simplification build_valid_auxpow() uses throughout BitAIcoin
        # Core's own functional-test suite (test/functional/test_framework/
        # auxpow.py), carried over here deliberately rather than
        # reinvented, since it is already the established, correct minimal
        # case. A provider for a REAL Bitcoin parent chain (with many other
        # transactions) would instead compute a real, non-trivial
        # coinbase_branch here.
        header.hash_merkle_root = coinbase.txid()
        return ParentCandidate(coinbase=coinbase, header=header, coinbase_branch=[], coinbase_index=0)

    def receive_solved_parent_work(self, solution: ParentSolution) -> None:
        # No real parent chain exists in synthetic mode -- nothing to
        # submit anywhere. A real BitcoinCoreGBTParentWorkProvider would
        # call Bitcoin Core's submitblock with `solution` here instead.
        pass
