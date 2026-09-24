# Copyright (c) 2026 The BitAIcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""BitAIcoin AuxPoW (merged mining) test support.

Mirrors, byte-for-byte, the real wire format implemented in src/auxpow.h/.cpp
and the version-bit helpers in src/primitives/block.h -- this is a SEPARATE,
explicit Python port for functional-test use, not a generic/guessed encoding.
Kept in its own module (not added to messages.py's CBlockHeader/CBlock
directly) for the same reason the C++ side keeps SerializeBlockHeaderWithAuxPow
etc. separate from CBlockHeader::SERIALIZE_METHODS: so every OTHER functional
test's plain CBlock/CBlockHeader usage is completely unaffected by this file
existing.
"""

import struct

from .messages import (
    CBlock,
    CBlockHeader,
    CTransaction,
    deser_uint256_vector,
    hash256,
    ser_uint256_vector,
    uint256_from_str,
)

# --- version-bit layout, matching primitives/block.h exactly ---
VERSION_AUXPOW = 1 << 8
VERSION_CHAIN_ID_SHIFT = 16
VERSION_RESERVED_MASK = 0x0000FE00
BITAI_AUXPOW_CHAIN_ID = 16969  # 0x4249, "BI"


def reverse_hex_bytes(hex_str):
    """Reverses the BYTE order of a hex string (not the string's characters)
    -- e.g. 'aabbcc' -> 'ccbbaa'. Converts between the conventional
    Namecoin/Dogecoin createauxblock "target" byte order (raw internal
    bytes, hex-encoded with no reversal) and the big-endian/"natural"
    convention getblocktemplate's own "target" field uses (fixed
    2026-09-24, docs/AUXPOW_MILESTONE.md sec.10.1/10.8 -- the two are exact
    byte-reversals of each other, never directly equal)."""
    return bytes.fromhex(hex_str)[::-1].hex()


def target_from_auxpow_rpc_hex(hex_str):
    """Parses createauxblock's own "target" field -- conventional
    Namecoin/Dogecoin AuxPoW RPC byte order, i.e. HexStr(BEGIN(target),
    END(target)) in those codebases -- into the actual numeric target
    value. Equivalent to, and independently cross-checked against,
    uint256_from_compact() applied to the same candidate's "bits" field."""
    return int.from_bytes(bytes.fromhex(hex_str), "little")


def get_base_version(nVersion):
    return nVersion % VERSION_AUXPOW


def get_chain_id(nVersion):
    # nVersion is a signed int32 in the real header; Python ints are
    # arbitrary precision, so shift the raw (possibly-negative-as-int32,
    # but always non-negative in practice for these tests) value directly --
    # every test in this module only ever constructs small, positive chain
    # IDs, so no sign-extension subtlety applies here.
    return nVersion >> VERSION_CHAIN_ID_SHIFT


def is_auxpow_version(nVersion):
    return (nVersion & VERSION_AUXPOW) != 0


def make_auxpow_version(chain_id, base_version):
    # Matches the real, fixed MakeAuxpowVersion(): base_version masked to its
    # low 8 bits before combining (see the 2026-09-23 fix in
    # primitives/block.h -- this Python port reproduces the FIXED behavior).
    return (chain_id << VERSION_CHAIN_ID_SHIFT) | VERSION_AUXPOW | (base_version & (VERSION_AUXPOW - 1))


# --- merge-mining tag, matching src/auxpow.h's MERGE_MINING_HEADER exactly ---
MERGE_MINING_HEADER = bytes([0xfa, 0xbe, 0x6d, 0x6d])


def build_merge_mining_tag(commitment_int, tree_size, nonce):
    """commitment_int is the 256-bit commitment as a Python int (same
    convention as ser_uint256); tree_size/nonce are 4-byte LE per the real
    wire format."""
    from .messages import ser_uint256
    return (MERGE_MINING_HEADER + ser_uint256(commitment_int) +
            struct.pack("<I", tree_size) + struct.pack("<I", nonce))


def get_expected_merkle_tree_index(nonce, chain_id, tree_size):
    """Bit-exact port of GetExpectedMerkleTreeIndex (src/auxpow.cpp) --
    same LCG constants, same 32-bit wraparound (Python ints need an explicit
    mask; C++'s uint32_t wraps implicitly)."""
    MASK32 = 0xFFFFFFFF
    rand = nonce & MASK32
    rand = (rand * 1103515245 + 12345) & MASK32
    rand = (rand + (chain_id & MASK32)) & MASK32
    rand = (rand * 1103515245 + 12345) & MASK32
    return rand % tree_size


class CAuxPow:
    """Matches src/auxpow.h's CAuxPow::SERIALIZE_METHODS field order exactly:
    coinbaseTx (TX_NO_WITNESS), vMerkleBranch, nIndex, vChainMerkleBranch,
    nChainIndex, parentBlock."""
    __slots__ = ("coinbaseTx", "vMerkleBranch", "nIndex",
                 "vChainMerkleBranch", "nChainIndex", "parentBlock")

    def __init__(self):
        self.coinbaseTx = CTransaction()
        self.vMerkleBranch = []
        self.nIndex = 0
        self.vChainMerkleBranch = []
        self.nChainIndex = 0
        self.parentBlock = CBlockHeader()

    def serialize(self):
        r = b""
        r += self.coinbaseTx.serialize_without_witness()  # TX_NO_WITNESS
        r += ser_uint256_vector(self.vMerkleBranch)
        r += struct.pack("<i", self.nIndex)
        r += ser_uint256_vector(self.vChainMerkleBranch)
        r += struct.pack("<i", self.nChainIndex)
        r += self.parentBlock._serialize_header()
        return r

    def deserialize(self, f):
        self.coinbaseTx = CTransaction()
        self.coinbaseTx.deserialize(f)
        self.vMerkleBranch = deser_uint256_vector(f)
        self.nIndex = struct.unpack("<i", f.read(4))[0]
        self.vChainMerkleBranch = deser_uint256_vector(f)
        self.nChainIndex = struct.unpack("<i", f.read(4))[0]
        self.parentBlock = CBlockHeader()
        self.parentBlock.deserialize(f)
        return self


def _solve_header_for_target(header, target):
    """Grinds header.nNonce (a bare CBlockHeader, e.g. an AuxPoW proof's
    parent) until its hash satisfies `target` -- the auxiliary chain's
    required target (nBitsAux), NOT header.nBits itself (which is
    deliberately irrelevant to AuxPoW validation; see CAuxPow::Check()'s own
    doc comment in src/auxpow.h)."""
    while header.hash_int > target:
        header.nNonce += 1
        assert header.nNonce < 2**32


def build_valid_auxpow(aux_block_hash_int, target_aux, chain_id=BITAI_AUXPOW_CHAIN_ID):
    """Builds a minimal, single-transaction-parent, single-chain (tree size
    1) AuxPoW proof committing to `aux_block_hash_int`, whose parent header
    hash satisfies `target_aux` (the auxiliary/BitAIcoin chain's own real
    required target -- uint256_from_compact(auxBlock.nBits), computed by the
    caller). Mirrors src/test/auxpow_tests.cpp's BuildValidAuxPow() helper
    exactly (same tree size 1 / nIndex 0 / nChainIndex 0 simplification)."""
    tree_size = 1
    nonce = 0xDEADBEEF
    tag = build_merge_mining_tag(aux_block_hash_int, tree_size, nonce)

    coinbase = CTransaction()
    coinbase.vin = [_coinbase_txin(tag)]
    coinbase.vout = [_null_txout()]

    parent = CBlockHeader()
    parent.nVersion = 1  # parent chain's own header, no AUXPOW bit
    parent.hashPrevBlock = 0
    parent.hashMerkleRoot = coinbase.txid_int  # single-tx block: merkle root == coinbase txid
    parent.nTime = 1700000000
    parent.nBits = 0x1d0fffff  # parent's own claimed difficulty -- irrelevant to Check(), just well-formed
    parent.nNonce = 0
    _solve_header_for_target(parent, target_aux)

    proof = CAuxPow()
    proof.coinbaseTx = coinbase
    proof.vMerkleBranch = []
    proof.nIndex = 0
    proof.vChainMerkleBranch = []
    proof.nChainIndex = get_expected_merkle_tree_index(nonce, chain_id, tree_size)
    proof.parentBlock = parent
    assert proof.nChainIndex == 0  # true for any nonce/chain_id when tree_size == 1
    return proof


def _coinbase_txin(script_sig_bytes):
    from .messages import COutPoint, CTxIn
    return CTxIn(COutPoint(0, 0xffffffff), script_sig_bytes, 0xffffffff)


def _null_txout():
    from .messages import CTxOut
    return CTxOut(0, b"")


class CBlockHeaderWithAuxPow(CBlockHeader):
    """Auxpow-aware header (de)serialization, mirroring
    SerializeBlockHeaderWithAuxPow/UnserializeBlockHeaderWithAuxPow
    (src/auxpow.h) exactly: the 6 base fields, then -- only when
    is_auxpow_version(nVersion) -- the CAuxPow payload. Byte-for-byte
    identical to a plain CBlockHeader for any non-AuxPoW-flagged header."""
    __slots__ = ("auxpow",)

    def __init__(self, header=None):
        super().__init__(header)
        self.auxpow = None

    def serialize(self):
        r = self._serialize_header()
        if is_auxpow_version(self.nVersion):
            assert self.auxpow is not None
            r += self.auxpow.serialize()
        return r

    def deserialize(self, f):
        super().deserialize(f)  # the 6 base fields
        if is_auxpow_version(self.nVersion):
            self.auxpow = CAuxPow()
            self.auxpow.deserialize(f)
        else:
            self.auxpow = None
        return self

    @property
    def hash_int(self):
        # GetHash() covers ONLY the 6 base fields, regardless of auxpow --
        # matches CBlockHeader::GetHash() (primitives/block.h) exactly; this
        # override exists only so callers get the right value automatically
        # even though this subclass's serialize() includes more bytes.
        return uint256_from_str(hash256(self._serialize_header()))


class CBlockWithAuxPow(CBlock):
    """Full auxpow-aware CBlock: header (6 fields) + optional CAuxPow +
    transactions, matching SerializeBlockWithAuxPow/UnserializeBlockWithAuxPow
    (src/auxpow.h) exactly, including the with_witness toggle."""
    __slots__ = ("auxpow",)

    def __init__(self, header=None):
        super().__init__(header)
        self.auxpow = None

    def serialize(self, with_witness=True):
        r = self._serialize_header()
        if is_auxpow_version(self.nVersion):
            assert self.auxpow is not None
            r += self.auxpow.serialize()
        from .messages import ser_vector
        if with_witness:
            r += ser_vector(self.vtx, "serialize_with_witness")
        else:
            r += ser_vector(self.vtx, "serialize_without_witness")
        return r

    def deserialize(self, f):
        CBlockHeader.deserialize(self, f)
        if is_auxpow_version(self.nVersion):
            self.auxpow = CAuxPow()
            self.auxpow.deserialize(f)
        else:
            self.auxpow = None
        from .messages import deser_vector
        self.vtx = deser_vector(f, CTransaction)
        return self

    @property
    def hash_int(self):
        return uint256_from_str(hash256(self._serialize_header()))
