#!/usr/bin/env python3
# Copyright (c) 2026 The BitAIcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""MergeMiningCoordinator: the reference coordinator's core API.

Deliberately just two operations (item 4 of the merge-mining milestone
spec): `create_job()` and `submit_parent_solution()`. This split matters --
a real pool performs the actual SHA256d work OUTSIDE this API, on real
hardware, between those two calls. The CPU-mining loop used by this
repository's own demo/test client (wire.solve_parent_header) is NOT part of
this class and is never called by it.

This module never re-implements any AuxPoW consensus rule. Every acceptance
or rejection decision comes from bitaicoind's own real submitauxblock
response -- this file only builds the proof bytes and interprets that
response (item 8: "the coordinator should construct proofs; bitaicoind
remains authoritative about validity").
"""

from __future__ import annotations

import random
from dataclasses import dataclass, field
from enum import Enum
from typing import List, Optional

from provider import ParentCandidate, ParentSolution, ParentWorkProvider
from rpc import BitAIcoinRPC, RPCError
from wire import (
    AuxPow,
    build_merge_mining_tag,
    build_single_chain_commitment,
    compact_to_target,
    hash_from_hex,
    hash_hex,
    target_from_createauxblock_hex,
)


@dataclass
class Job:
    """The complete, machine-readable work package (merge-mining milestone
    item 3) a parent pool needs to go do real SHA256d work, plus everything
    this reference coordinator itself needs later to convert a solution into
    a CAuxPow. Every hash/target field's byte-order convention is documented
    at the point it is produced (see wire.py's own module docstring for the
    full byte-order reference) and repeated in `field_byte_order` below so
    nothing here requires the reader to guess.
    """
    # --- from createauxblock, verbatim / lightly parsed ---
    child_hash: str                 # DISPLAYED hex convention (hash_hex/hash_from_hex)
    chain_id: int                   # always 16969
    previousblockhash: str          # DISPLAYED hex convention
    coinbasevalue: int
    child_bits: str                 # compact target, 8 hex chars
    child_height: int
    child_target_createauxblock_hex: str   # createauxblock's OWN raw byte-order convention (NOT displayed, NOT GBT)

    # --- derived / cross-checked locally, never trust one representation alone ---
    child_target_numeric: int       # decoded once, cross-checked two independent ways (see create_job())

    # --- the merge-mining commitment this job's tag commits to ---
    commitment_tag_hex: str         # fabe6d6d || aux_merkle_root || tree_size || nonce, ready to embed verbatim
    tree_size: int                  # this reference: always 1 (item 6)
    merkle_nonce: int
    chain_index: int                # this reference: always 0
    chain_branch: List[str]         # this reference: always [] -- DISPLAYED hex convention per entry

    # --- internal state, not part of the "what a pool needs" contract ---
    parent_candidate: ParentCandidate  # opaque to an external pool; used only by this reference's own synthetic path

    field_byte_order: dict = field(default_factory=lambda: {
        "child_hash": "displayed (reversed) hex -- same convention as getbestblockhash/getblockhash",
        "previousblockhash": "displayed (reversed) hex, same as child_hash",
        "child_bits": "compact 8-hex-char target, wire/display convention (identical either way for this 4-byte field)",
        "child_target_createauxblock_hex": "RAW internal bytes, NO reversal -- the Namecoin/Dogecoin AuxPoW RPC "
                                            "convention. Reverse its bytes to get getblocktemplate's own target hex.",
        "child_target_numeric": "plain Python int -- the actual number being compared against a candidate parent hash",
        "commitment_tag_hex": "ready-to-embed raw bytes (fabe6d6d magic + 32 raw LE bytes + 2x uint32 LE) -- insert "
                               "this exact byte string into your own parent coinbase's scriptSig verbatim",
        "chain_branch": "each entry: displayed (reversed) hex, matching child_hash's own convention",
    })

    def to_dict(self) -> dict:
        """The machine-readable representation from item 3, with explicit
        byte-order documentation inlined so nothing requires guessing."""
        return {
            "child_hash": self.child_hash,
            "chain_id": self.chain_id,
            "previousblockhash": self.previousblockhash,
            "coinbasevalue": self.coinbasevalue,
            "child_bits": self.child_bits,
            "child_height": self.child_height,
            "child_target": self.child_target_createauxblock_hex,
            "child_target_numeric_hex": format(self.child_target_numeric, "064x"),
            "commitment": self.commitment_tag_hex,
            "tree_size": self.tree_size,
            "merkle_nonce": self.merkle_nonce,
            "chain_index": self.chain_index,
            "chain_branch": self.chain_branch,
            "_byte_order": self.field_byte_order,
        }


class SubmitStatus(Enum):
    ACCEPTED = "accepted"
    STALE = "stale"
    REJECTED = "rejected"


@dataclass
class SubmitResult:
    status: SubmitStatus
    detail: str


class MergeMiningCoordinator:
    """Orchestrates create_job()/submit_parent_solution() against one
    bitaicoind RPC endpoint and one ParentWorkProvider. Holds no consensus
    logic of its own (item 8)."""

    def __init__(self, rpc: BitAIcoinRPC, provider: ParentWorkProvider, payout_address: str):
        self._rpc = rpc
        self._provider = provider
        self._payout_address = payout_address

    def create_job(self, merkle_nonce: Optional[int] = None) -> Job:
        """Calls createauxblock, builds the (tree_size=1, item 6) merge-mining
        commitment explicitly, and asks the configured ParentWorkProvider for
        a parent candidate with that commitment already embedded. Returns a
        complete Job: everything a parent pool needs, byte-order-documented,
        with nothing left implicit."""
        result = self._rpc.createauxblock(self._payout_address)

        child_hash = hash_from_hex(result["hash"])
        target_from_field = target_from_createauxblock_hex(result["target"])
        target_from_bits = compact_to_target(int(result["bits"], 16))
        # Cross-check, every time, that createauxblock's two independent
        # representations of the same target agree -- never trust one
        # representation alone (docs/AUXPOW_MILESTONE.md sec.11.1).
        if target_from_field != target_from_bits:
            raise RuntimeError(
                "createauxblock's target/bits fields disagree -- this would be a real bug in the "
                f"frozen RPC contract, not a coordinator bug: target={result['target']} bits={result['bits']}"
            )
        child_target = target_from_field

        # Explicit, not hidden in a helper's defaults (item 2): the current,
        # real single-aux-chain mode.
        if merkle_nonce is None:
            merkle_nonce = random.randrange(0, 2**32)
        aux_merkle_root, tree_size, chain_branch, chain_index = build_single_chain_commitment(child_hash, merkle_nonce)
        assert tree_size == 1 and chain_branch == [] and chain_index == 0

        tag = build_merge_mining_tag(aux_merkle_root, tree_size, merkle_nonce)

        template = self._provider.get_parent_candidate()
        candidate = self._provider.insert_coinbase_commitment(template, tag)

        return Job(
            child_hash=result["hash"],
            chain_id=result["chainid"],
            previousblockhash=result["previousblockhash"],
            coinbasevalue=result["coinbasevalue"],
            child_bits=result["bits"],
            child_height=result["height"],
            child_target_createauxblock_hex=result["target"],
            child_target_numeric=child_target,
            commitment_tag_hex=tag.hex(),
            tree_size=tree_size,
            merkle_nonce=merkle_nonce,
            chain_index=chain_index,
            chain_branch=[hash_hex(h) for h in chain_branch],
            parent_candidate=candidate,
        )

    def submit_parent_solution(self, job: Job, solution: ParentSolution) -> SubmitResult:
        """Converts solved parent evidence into the exact CAuxPow
        submitauxblock expects, and submits it. Implements the stale-job
        semantics from item 7:
          - if the BitAIcoin tip has moved away from this job's own
            previousblockhash AND is not this job's own child_hash, the job
            is stale: reported cleanly, WITHOUT calling submitauxblock (a
            courtesy pre-check only -- never a consensus decision, item 8).
          - the job is NEVER mutated or recycled; a stale job must be
            discarded and a fresh one requested via create_job() again.
          - a resubmission whose child_hash is ALREADY the accepted active
            AuxPoW block still succeeds -- that case is deliberately let
            through to bitaicoind's own real, already-frozen idempotent-retry
            check (docs/AUXPOW_MILESTONE.md sec.11.2/12.1), never decided
            here.
        """
        current_tip = self._rpc.getbestblockhash()
        if current_tip != job.previousblockhash and current_tip != job.child_hash:
            return SubmitResult(
                SubmitStatus.STALE,
                f"BitAIcoin tip is {current_tip}, but this job was built against "
                f"previousblockhash={job.previousblockhash}; request a fresh job.",
            )

        auxpow = self._build_auxpow(job, solution)

        self._provider.receive_solved_parent_work(solution)

        try:
            accepted = self._rpc.submitauxblock(job.child_hash, auxpow.hex())
        except RPCError as e:
            return SubmitResult(SubmitStatus.REJECTED, f"submitauxblock RPC error: {e}")

        if accepted:
            note = " (idempotent resubmission of an already-accepted block)" if current_tip == job.child_hash else ""
            return SubmitResult(SubmitStatus.ACCEPTED, f"accepted{note}")
        return SubmitResult(SubmitStatus.REJECTED, "bitaicoind rejected the submitted proof (submitauxblock returned false)")

    @staticmethod
    def _build_auxpow(job: Job, solution: ParentSolution) -> AuxPow:
        chain_branch_ints = [hash_from_hex(h) for h in job.chain_branch]
        return AuxPow(
            coinbase_tx=solution.coinbase,
            coinbase_branch=solution.coinbase_branch,
            coinbase_index=solution.coinbase_index,
            chain_branch=chain_branch_ints,
            chain_index=job.chain_index,
            parent_block=solution.header,
        )
