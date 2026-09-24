#!/usr/bin/env python3
# Copyright (c) 2026 The BitAIcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Standalone, dependency-free (stdlib only) reference implementation of the
BitAIcoin AuxPoW (merged-mining) wire format.

This module is deliberately self-contained and does NOT import anything from
BitAIcoin Core's own test framework (test/functional/test_framework/). The
whole point of this coordinator is to be readable by an external SHA256d
pool integrator who has never looked at BitAIcoin Core's C++ source -- every
byte-order decision below is documented at the point it matters, and every
struct layout is a direct, literal transcription of the real, frozen C++
serialization it must match (src/auxpow.h, src/primitives/block.h), not a
reused test helper.

============================================================================
BYTE ORDER, IN ONE PLACE (read this before touching anything else here)
============================================================================
Every "hash" in this module is a Python `int` internally (arbitrary
precision, easy to compare/shift), and every serialization function turns
that int into 32 raw bytes via `int.to_bytes(32, "little")` --
**little-endian**, matching a real `uint256`'s internal storage order,
which is what actually gets hashed/serialized on the wire (merkle
branches, the merge-mining tag's commitment field, block headers).

This is DELIBERATELY THE OPPOSITE of the familiar big-endian-looking hex
string a block explorer shows you (e.g. bitaicoind's own `getblockhash`
output, or `getbestblockhash`) -- that display convention is produced by
REVERSING the raw bytes before hex-encoding. This module's `hash_hex()`
helper does that reversal, matching `uint256::GetHex()`; its
`hash_from_hex()` does the inverse. Every place in this file that touches a
hash is commented with which convention applies.

createauxblock's own "target" field follows a THIRD, separate convention:
the conventional Namecoin/Dogecoin AuxPoW RPC byte order (raw internal
bytes, hex-encoded with NO reversal -- i.e. the same raw byte order as a
hash used inside the wire format, NOT the same as a displayed block hash).
See `target_from_createauxblock_hex()` below, and
docs/MERGE_MINING_INTEGRATION.md sec.2 for a worked example.
============================================================================
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

# ---------------------------------------------------------------------------
# Hashing primitives
# ---------------------------------------------------------------------------


def sha256(b: bytes) -> bytes:
    return hashlib.sha256(b).digest()


def sha256d(b: bytes) -> bytes:
    """Double SHA-256, the hash function used for every block/tx hash in
    both Bitcoin and BitAIcoin. Returns 32 raw bytes in the SAME internal
    (little-endian-relative-to-the-displayed-hash) order as a real
    `uint256`'s storage -- callers wanting the human-displayed hex must
    reverse these bytes first (see `hash_hex()`)."""
    return sha256(sha256(b))


def hash_to_int(raw_le_bytes: bytes) -> int:
    """Raw 32 little-endian bytes (as produced by sha256d, or as stored in
    a uint256) -> Python int. This int is what every function in this
    module that takes a "hash" parameter expects."""
    return int.from_bytes(raw_le_bytes, "little")


def hash_to_raw_le(h: int) -> bytes:
    """Python int -> raw 32 little-endian bytes, i.e. the exact bytes that
    get serialized on the wire (merkle branches, the merge-mining tag's
    commitment field, a block header's hashPrevBlock/hashMerkleRoot)."""
    return h.to_bytes(32, "little")


def hash_hex(h: int) -> str:
    """Python int -> the DISPLAYED hex convention (same as bitaicoin-cli's
    getblockhash/getbestblockhash/getblock output): raw little-endian bytes,
    REVERSED, then hex-encoded. This is what a human, or createauxblock's
    own `hash`/`previousblockhash` fields, shows you."""
    return hash_to_raw_le(h)[::-1].hex()


def hash_from_hex(hex_str: str) -> int:
    """Inverse of hash_hex(): a displayed hex hash string (e.g. from
    createauxblock's "hash" field, or getbestblockhash) -> the Python int
    every function in this module expects."""
    return int.from_bytes(bytes.fromhex(hex_str)[::-1], "little")


def reverse_hex_bytes(hex_str: str) -> str:
    """Reverses the BYTE order of a hex string (not its characters) -- e.g.
    'aabbcc' -> 'ccbbaa'. Converts between createauxblock's own "target"
    byte order (target_from_createauxblock_hex, below) and
    getblocktemplate's big-endian/"natural" convention -- the two are exact
    byte-reversals of each other, never directly equal. See
    docs/MERGE_MINING_INTEGRATION.md sec.2."""
    return bytes.fromhex(hex_str)[::-1].hex()


def target_from_createauxblock_hex(target_hex: str) -> int:
    """createauxblock's own "target" field uses the conventional
    Namecoin/Dogecoin AuxPoW RPC byte order: raw internal bytes, hex-encoded
    with NO reversal (HexStr(BEGIN(target), END(target)) in those
    codebases). This is NOT the displayed-hash convention (hash_from_hex)
    and NOT getblocktemplate's own big-endian "target" convention -- it is
    the exact byte-reversal of the latter. See
    docs/MERGE_MINING_INTEGRATION.md sec.2 for the full explanation and a
    worked numeric example."""
    return int.from_bytes(bytes.fromhex(target_hex), "little")


def compact_to_target(bits: int) -> int:
    """Decodes a compact-format "bits" value (e.g. from createauxblock's
    own "bits" field, or a block header's nBits) into the full 256-bit
    numeric target, matching arith_uint256::SetCompact() exactly."""
    size = bits >> 24
    word = bits & 0x007FFFFF
    if size <= 3:
        word >>= 8 * (3 - size)
        return word
    return word << (8 * (size - 3))


# ---------------------------------------------------------------------------
# CompactSize (Bitcoin's variable-length integer encoding)
# ---------------------------------------------------------------------------


def ser_compact_size(n: int) -> bytes:
    if n < 253:
        return struct.pack("<B", n)
    elif n <= 0xFFFF:
        return struct.pack("<BH", 253, n)
    elif n <= 0xFFFFFFFF:
        return struct.pack("<BI", 254, n)
    else:
        return struct.pack("<BQ", 255, n)


def ser_hash_vector(hashes: List[int]) -> bytes:
    """Serializes a list of hashes (Python ints) the same way CAuxPow's own
    vMerkleBranch/vChainMerkleBranch fields are serialized: CompactSize
    count, then each hash as 32 raw little-endian bytes."""
    out = ser_compact_size(len(hashes))
    for h in hashes:
        out += hash_to_raw_le(h)
    return out


class _Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def read(self, n: int) -> bytes:
        if self.pos + n > len(self.data):
            raise ValueError("truncated data: tried to read past end")
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def read_compact_size(self) -> int:
        first = self.read(1)[0]
        if first < 253:
            return first
        elif first == 253:
            return struct.unpack("<H", self.read(2))[0]
        elif first == 254:
            return struct.unpack("<I", self.read(4))[0]
        else:
            return struct.unpack("<Q", self.read(8))[0]

    def read_hash_vector(self) -> List[int]:
        n = self.read_compact_size()
        return [hash_to_int(self.read(32)) for _ in range(n)]

    def remaining(self) -> int:
        return len(self.data) - self.pos


# ---------------------------------------------------------------------------
# Minimal legacy (non-segwit) transaction: enough to build a coinbase.
# This mirrors CTransaction's non-witness serialization exactly -- the
# coinbase inside a CAuxPow is always serialized WITHOUT witness data
# (TX_NO_WITNESS in src/auxpow.h's own SERIALIZE_METHODS), so this module
# never implements segwit serialization at all: a real merge-mining parent
# coinbase does not need one for this purpose.
# ---------------------------------------------------------------------------


@dataclass
class TxIn:
    prev_txid: int = 0       # 0 for a coinbase input
    prev_vout: int = 0xFFFFFFFF
    script_sig: bytes = b""
    sequence: int = 0xFFFFFFFF

    def serialize(self) -> bytes:
        return (
            hash_to_raw_le(self.prev_txid)
            + struct.pack("<I", self.prev_vout)
            + ser_compact_size(len(self.script_sig))
            + self.script_sig
            + struct.pack("<I", self.sequence)
        )


@dataclass
class TxOut:
    value: int = 0
    script_pubkey: bytes = b""

    def serialize(self) -> bytes:
        return (
            struct.pack("<q", self.value)
            + ser_compact_size(len(self.script_pubkey))
            + self.script_pubkey
        )


@dataclass
class Transaction:
    version: int = 1
    vin: List[TxIn] = field(default_factory=list)
    vout: List[TxOut] = field(default_factory=list)
    locktime: int = 0

    def serialize(self) -> bytes:
        out = struct.pack("<i", self.version)
        out += ser_compact_size(len(self.vin))
        for txin in self.vin:
            out += txin.serialize()
        out += ser_compact_size(len(self.vout))
        for txout in self.vout:
            out += txout.serialize()
        out += struct.pack("<I", self.locktime)
        return out

    def txid(self) -> int:
        return hash_to_int(sha256d(self.serialize()))


def make_coinbase(script_sig: bytes) -> Transaction:
    """Builds the simplest possible coinbase transaction carrying
    `script_sig` in its single input -- exactly where
    FindMergeMiningTag()/CAuxPow::Check() looks for the merge-mining tag
    (src/auxpow.cpp: `coinbaseTx->vin[0].scriptSig`). The single, zero-value,
    empty-scriptPubKey output is a placeholder: it is never inspected by
    CAuxPow::Check() (a real Bitcoin parent coinbase's actual payout logic
    is completely independent of this)."""
    return Transaction(
        version=1,
        vin=[TxIn(prev_txid=0, prev_vout=0xFFFFFFFF, script_sig=script_sig, sequence=0xFFFFFFFF)],
        vout=[TxOut(value=0, script_pubkey=b"")],
        locktime=0,
    )


# ---------------------------------------------------------------------------
# Merkle branch construction/verification -- a direct, literal port of
# CheckMerkleBranch (src/auxpow.cpp), used for BOTH the parent coinbase
# branch and the aux-chain branch (the same function, on both sides of a
# CAuxPow, per the real C++ code).
# ---------------------------------------------------------------------------


def check_merkle_branch(leaf_hash: int, branch: List[int], index: int) -> int:
    """Recomputes the merkle root `leaf_hash` proves inclusion in, given a
    branch (sibling hashes, bottom to top) and its bit-indexed position.
    Byte-for-byte identical algorithm to CheckMerkleBranch: at each level,
    bit 0 of `index` selects concatenation order (left=other,right=hash if
    set, else left=hash,right=other), then index is shifted right one bit."""
    h = leaf_hash
    idx = index
    for other in branch:
        if idx & 1:
            combined = hash_to_raw_le(other) + hash_to_raw_le(h)
        else:
            combined = hash_to_raw_le(h) + hash_to_raw_le(other)
        h = hash_to_int(sha256d(combined))
        idx >>= 1
    return h


# ---------------------------------------------------------------------------
# Merge-mining tag (the bytes an external pool inserts into its own parent
# coinbase's scriptSig) -- matches src/auxpow.h's MERGE_MINING_HEADER and
# src/auxpow.cpp's FindMergeMiningTag exactly.
# ---------------------------------------------------------------------------

MERGE_MINING_MAGIC = bytes([0xFA, 0xBE, 0x6D, 0x6D])  # "\xfa\xbe mm"


def build_merge_mining_tag(aux_merkle_root: int, tree_size: int, merkle_nonce: int) -> bytes:
    """Builds the exact bytes an external pool must insert somewhere in its
    own parent coinbase's first input's scriptSig:

        fabe6d6d || aux_merkle_root (32 bytes, LE) || tree_size (4 bytes, LE) || nonce (4 bytes, LE)

    `aux_merkle_root` here is the CHAIN merkle root -- for BitAIcoin's
    current tree_size=1 reference mode this is simply the child block hash
    itself (see build_single_chain_commitment() below); for a future
    multi-aux-chain tree it would be the root of that tree instead.
    All three trailing fields are little-endian, matching
    FindMergeMiningTag's own explicit "fields are wire little-endian"
    comment (src/auxpow.cpp)."""
    if not (0 <= tree_size <= 0xFFFFFFFF):
        raise ValueError("tree_size must fit in a uint32")
    if not (0 <= merkle_nonce <= 0xFFFFFFFF):
        raise ValueError("merkle_nonce must fit in a uint32")
    return (
        MERGE_MINING_MAGIC
        + hash_to_raw_le(aux_merkle_root)
        + struct.pack("<I", tree_size)
        + struct.pack("<I", merkle_nonce)
    )


def find_merge_mining_tag(script_sig: bytes) -> Tuple[int, int, int]:
    """Reference (decode-side) counterpart of FindMergeMiningTag, provided
    here for the coordinator's own negative-test matrix (to verify what it
    just built, and to construct deliberately-malformed cases) -- NOT used
    by bitaicoind at all, which has and uses its own real C++ implementation
    independently. Raises ValueError with the same real rejection reasons
    src/auxpow.cpp uses (tag not found / multiple tags / truncated
    payload), for test-matrix clarity."""
    tag_len = len(MERGE_MINING_MAGIC)
    payload_len = 32 + 4 + 4
    matches = [i for i in range(len(script_sig) - tag_len + 1)
               if script_sig[i:i + tag_len] == MERGE_MINING_MAGIC]
    if not matches:
        raise ValueError("merge-mining tag not found in coinbase")
    if len(matches) > 1:
        raise ValueError("multiple merge-mining tags found in coinbase (ambiguous, rejected)")
    pos = matches[0] + tag_len
    if pos + payload_len > len(script_sig):
        raise ValueError("merge-mining tag found but truncated payload")
    commitment = hash_to_int(script_sig[pos:pos + 32])
    tree_size = struct.unpack("<I", script_sig[pos + 32:pos + 36])[0]
    nonce = struct.unpack("<I", script_sig[pos + 36:pos + 40])[0]
    return commitment, tree_size, nonce


# ---------------------------------------------------------------------------
# Chain-merkle-tree helpers (item 6: separable now so a future multi-aux
# -chain pool -- BAIC + DOGE + NMC etc., sharing one parent -- can use
# tree_size > 1 without rewriting the coordinator). BitAIcoin's OWN current
# reference path (build_single_chain_commitment) always uses tree_size=1,
# chain_index=0, chain_branch=[] -- deliberately the simplest possible case
# of the same general machinery, not a separate code path.
# ---------------------------------------------------------------------------

# Matches GetExpectedMerkleTreeIndex (src/auxpow.cpp) exactly: same LCG
# constants, same uint32 wraparound (masked explicitly here since Python
# ints don't wrap on their own).
_MASK32 = 0xFFFFFFFF
_LCG_A = 1103515245
_LCG_C = 12345


def expected_merkle_tree_index(nonce: int, chain_id: int, tree_size: int) -> int:
    rand = nonce & _MASK32
    rand = (rand * _LCG_A + _LCG_C) & _MASK32
    rand = (rand + (chain_id & _MASK32)) & _MASK32
    rand = (rand * _LCG_A + _LCG_C) & _MASK32
    return rand % tree_size


def build_single_chain_commitment(child_hash: int, merkle_nonce: int) -> Tuple[int, int, List[int], int]:
    """BitAIcoin's current, real single-aux-chain reference path:
    tree_size=1, chain_index=0, chain_branch=[] -- explicit, not hidden in
    a helper's default arguments. Returns
    (aux_merkle_root, tree_size, chain_branch, chain_index). For
    tree_size=1, aux_merkle_root == child_hash directly (a one-leaf "tree"
    has no internal nodes) and expected_merkle_tree_index() collapses to 0
    for ANY chain_id/nonce (see docs/MERGE_MINING_INTEGRATION.md sec.6 for
    why this means BitAIcoin's own chain_id plays no role in this specific
    computation while tree_size stays 1)."""
    tree_size = 1
    chain_branch: List[int] = []
    chain_index = 0
    assert expected_merkle_tree_index(merkle_nonce, 0, tree_size) == 0  # true for any chain_id when tree_size==1
    aux_merkle_root = child_hash
    return aux_merkle_root, tree_size, chain_branch, chain_index


def build_multi_chain_commitment(chain_id_to_hash: dict, tree_size: int, merkle_nonce: int, this_chain_id: int
                                  ) -> Tuple[int, List[int], int]:
    """Future-facing (NOT exercised by BitAIcoin's own current reference
    test, which stays tree_size=1 -- item 6): builds a real binary merkle
    tree over `tree_size` slots, placing each chain's aux block hash at its
    own expected_merkle_tree_index() slot (empty slots are zero-hash
    padding, matching the real Namecoin/Dogecoin convention), and returns
    (aux_merkle_root, chain_branch_for_this_chain_id, chain_index_for_this_chain_id).
    Left undocumented/unexercised beyond this reference implementation
    deliberately -- BitAIcoin's current milestone only claims tree_size=1
    (see docs/MERGE_MINING_INTEGRATION.md sec.6)."""
    leaves = [0] * tree_size
    for cid, h in chain_id_to_hash.items():
        idx = expected_merkle_tree_index(merkle_nonce, cid, tree_size)
        if leaves[idx] != 0:
            raise ValueError(f"slot collision at index {idx} building the chain merkle tree")
        leaves[idx] = h

    this_index = expected_merkle_tree_index(merkle_nonce, this_chain_id, tree_size)

    # Standard bottom-up binary tree, matching Bitcoin's own convention of
    # duplicating the last node at each level when the level's node count is
    # odd (BlockMerkleRoot's own convention, mirrored here since
    # CheckMerkleBranch's verification side assumes this construction).
    branch: List[int] = []
    level = leaves[:]
    idx = this_index
    while len(level) > 1:
        if len(level) % 2 == 1:
            level.append(level[-1])
        sibling = level[idx ^ 1]
        branch.append(sibling)
        next_level = []
        for i in range(0, len(level), 2):
            combined = hash_to_raw_le(level[i]) + hash_to_raw_le(level[i + 1])
            next_level.append(hash_to_int(sha256d(combined)))
        level = next_level
        idx >>= 1
    root = level[0] if level else 0
    return root, branch, this_index


# ---------------------------------------------------------------------------
# Block header (the 6 consensus-critical fields every CBlockHeader has) and
# CAuxPow itself -- a direct, literal transcription of
# SerializeBlockHeaderWithAuxPow/CAuxPow::SERIALIZE_METHODS (src/auxpow.h).
# ---------------------------------------------------------------------------


@dataclass
class BlockHeader:
    version: int = 1
    hash_prev_block: int = 0
    hash_merkle_root: int = 0
    time: int = 0
    bits: int = 0
    nonce: int = 0

    def serialize(self) -> bytes:
        return (
            struct.pack("<i", self.version)
            + hash_to_raw_le(self.hash_prev_block)
            + hash_to_raw_le(self.hash_merkle_root)
            + struct.pack("<III", self.time, self.bits, self.nonce)
        )

    def block_hash(self) -> int:
        """The header's own PoW hash -- covers only these 6 fields,
        regardless of anything else (matches CBlockHeader::GetHash()
        exactly; a CAuxPow attached to a BitAIcoin child never changes its
        own hash for this reason -- see docs/MERGE_MINING_INTEGRATION.md
        sec.2)."""
        return hash_to_int(sha256d(self.serialize()))


@dataclass
class AuxPow:
    coinbase_tx: Transaction
    coinbase_branch: List[int]     # parent coinbase's merkle branch (this reference: always [])
    coinbase_index: int            # must be 0 (real deployments always place the coinbase at index 0)
    chain_branch: List[int]        # aux-chain merkle branch (this reference: always [] at tree_size=1)
    chain_index: int               # aux-chain merkle index (this reference: always 0 at tree_size=1)
    parent_block: BlockHeader

    def serialize(self) -> bytes:
        """Field order matches CAuxPow::SERIALIZE_METHODS exactly:
        coinbaseTx (no witness), vMerkleBranch, nIndex, vChainMerkleBranch,
        nChainIndex, parentBlock (6-field header only -- never recursively
        AuxPoW-aware, since a parent chain carrying its own AuxPoW bit is
        explicitly rejected by CAuxPow::Check() as "merge-mining of
        merge-mining")."""
        return (
            self.coinbase_tx.serialize()
            + ser_hash_vector(self.coinbase_branch)
            + struct.pack("<i", self.coinbase_index)
            + ser_hash_vector(self.chain_branch)
            + struct.pack("<i", self.chain_index)
            + self.parent_block.serialize()
        )

    def hex(self) -> str:
        return self.serialize().hex()


def solve_parent_header(header: BlockHeader, target: int, max_nonce: int = 2**32) -> BlockHeader:
    """Grinds `header.nonce` until its block_hash() satisfies `target` (the
    BitAIcoin child's own required target, uint256_from_compact of its
    "bits" -- NOT the parent header's own, unrelated `bits` field, which
    CAuxPow::Check() never even looks at). This is a plain, synchronous CPU
    loop for the synthetic reference miner only (item 4/9: a real pool does
    this externally, at real SHA256d ASIC scale, not inside the
    coordinator's own core API)."""
    h = BlockHeader(**header.__dict__)
    for nonce in range(max_nonce):
        h.nonce = nonce
        if h.block_hash() <= target:
            return h
    raise RuntimeError("exhausted nonce space without finding a solution (should never happen at regtest difficulty)")
