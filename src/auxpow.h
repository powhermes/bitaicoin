// Copyright (c) 2026 The BitAIcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// AuxPoW (merged mining) header/proof serialization and validation core.
//
// Design ported from Namecoin's original SHA256d-native AuxPoW (the reference
// this milestone names as primary), cross-checked against Syscoin's more
// modern maintained fork of the same design, but re-implemented against this
// tree's own modern types (CBlockHeader's SERIALIZE_METHODS style,
// CTransactionRef, arith_uint256) rather than vendored verbatim from either,
// per docs/AUXPOW_MILESTONE.md sec.4.
//
// SCOPE OF THIS FILE, stated explicitly (see docs/AUXPOW_MILESTONE.md for the
// full status): this is the serialization + standalone validation core
// (CAuxPow::Check()), covered by real unit tests in src/test/auxpow_tests.cpp
// that construct a fake parent block end-to-end and verify both accept and
// reject paths. It is NOT YET wired into CheckProofOfWorkImpl,
// ContextualCheckBlockHeader, net_processing's header/block relay, or GBT/
// mining -- that wiring is the deliberately separate next slice, because
// splicing new block-acceptance-path serialization into the live consensus
// code without its own dedicated review pass would be exactly the kind of
// unreviewed, rushed consensus change this whole milestone process exists to
// avoid. Existing blocks 225430-225823, and all validation of blocks below
// the (still unactivated) AuxPoW height, are untouched by this file.

#ifndef BITCOIN_AUXPOW_H
#define BITCOIN_AUXPOW_H

#include <consensus/params.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <uint256.h>

#include <cstdint>
#include <vector>

class BlockValidationState;

/**
 * Height-gated version-bit convention for AuxPoW-flagged blocks, matching
 * Namecoin's original encoding (also used by Syscoin, Dogecoin, Elastos and
 * others):
 *
 *   nVersion bit 8 (0x100)      -- VERSION_AUXPOW: this header carries a
 *                                  CAuxPow structure and MUST be validated
 *                                  via merge-mining, not via its own hash.
 *   nVersion bits 16-31         -- the merge-mined chain's ID (BitAIcoin's is
 *                                  16969 / 0x4249, "BI" -- see
 *                                  docs/AUXPOW_MILESTONE.md sec.5/B). A block
 *                                  whose top 16 bits don't match BitAIcoin's
 *                                  chain ID is not a BitAIcoin AuxPoW block,
 *                                  even if the AUXPOW bit is set (this is the
 *                                  chain-ID collision defense).
 *
 * A block below the AuxPoW activation height with the AUXPOW bit set is
 * invalid outright (the bit has no meaning pre-activation and must not be
 * silently ignored). A block at/after activation height WITHOUT the AUXPOW
 * bit set is still valid -- own-chain SHA256d mining and merge-mining both
 * remain accepted after activation, exactly as in every real deployment of
 * this design; AuxPoW is an additional accepted proof format, not a
 * replacement that forbids direct mining.
 */
static constexpr int32_t VERSION_AUXPOW = (1 << 8);
static constexpr int32_t VERSION_CHAIN_ID_SHIFT = 16;

inline int32_t GetBaseVersion(int32_t nVersion)
{
    return nVersion % VERSION_AUXPOW;
}

inline int32_t GetChainId(int32_t nVersion)
{
    return nVersion >> VERSION_CHAIN_ID_SHIFT;
}

inline bool IsAuxpowVersion(int32_t nVersion)
{
    return (nVersion & VERSION_AUXPOW) != 0;
}

inline int32_t MakeAuxpowVersion(int32_t nChainId, int32_t nBaseVersion)
{
    return (nChainId << VERSION_CHAIN_ID_SHIFT) | VERSION_AUXPOW | nBaseVersion;
}

/**
 * The four-byte magic that must appear in the parent-chain coinbase
 * scriptSig, immediately followed by the 32-byte merge-mining commitment
 * (the merkle root of the chain-merkle-tree of all chains being merge-mined
 * in this coinbase), a little-endian uint32 tree size, and a little-endian
 * uint32 nonce (used only to compute each chain's expected slot -- see
 * GetExpectedIndex below). Identical to Namecoin's original constant, kept
 * unchanged since parent-chain (real Bitcoin/BitAIcoin-as-parent) miners'
 * merge-mining software already recognizes this exact byte sequence.
 */
static const unsigned char MERGE_MINING_HEADER[4] = {0xfa, 0xbe, 0x6d, 0x6d};

/**
 * Computes the deterministic slot a given chain ID must occupy in the
 * chain-merkle-tree of a given size, seeded by the coinbase's committed
 * nonce. This is the standard Namecoin index-grinding defense: without it, a
 * malicious parent-chain miner could try many nonces until one happens to
 * validate an AuxPoW for a chain ID it was never honestly targeting, or
 * could let one coinbase serve as a forged proof for a chain it doesn't
 * intend to commit to. Reproduced exactly (same LCG constants) since this is
 * a cross-chain wire-format detail, not an internal implementation choice --
 * any deviation would make BitAIcoin AuxPoW incompatible with real-world
 * merge-mining pool software that already implements this exact formula.
 */
int GetExpectedMerkleTreeIndex(uint32_t nNonce, int32_t nChainId, unsigned nTreeSize);

/**
 * Verifies that `hash`, combined with `merkleBranch` (ordered from the
 * deepest level up) and `nIndex` (bit i of nIndex selects, at level i,
 * whether `hash`'s current position is combined as the left or right child),
 * reproduces `merkleRoot`. Generic -- used both for the coinbase-tx-to-
 * parent-block-merkle-root branch and for the chain-merkle-tree branch.
 */
uint256 CheckMerkleBranch(uint256 hash, const std::vector<uint256>& merkleBranch, int nIndex);

/**
 * An AuxPoW proof: a full parent-chain block header plus its coinbase
 * transaction and the two merkle branches needed to tie that coinbase back
 * to (a) the parent block's own merkle root and (b) this chain's committed
 * slot in the coinbase's merge-mining tag.
 */
class CAuxPow
{
public:
    /** The parent-chain coinbase transaction, which must contain the merge-mining tag. */
    CTransactionRef coinbaseTx;

    /** Merkle branch from coinbaseTx up to parentBlock.hashMerkleRoot. */
    std::vector<uint256> vMerkleBranch;
    /** Index of coinbaseTx within the parent block (always 0 in every real
     *  deployment -- the coinbase is always the first transaction -- but
     *  kept explicit and checked, per the original format, rather than
     *  hardcoded, since a consensus check should not assume what it can
     *  instead verify. */
    int nIndex{0};

    /** Merkle branch from this chain's slot up to the value committed in the
     *  coinbase's merge-mining tag (the "chain merkle tree", used when one
     *  coinbase merge-mines several chains at once). */
    std::vector<uint256> vChainMerkleBranch;
    /** This chain's index within the chain merkle tree. Must equal
     *  GetExpectedMerkleTreeIndex(parentBlock's committed nonce, our chain
     *  ID, chain merkle tree size) -- see Check(). */
    int nChainIndex{0};

    /** The full parent-chain block header this proof is embedded in. */
    CBlockHeader parentBlock;

    SERIALIZE_METHODS(CAuxPow, obj)
    {
        READWRITE(obj.coinbaseTx);
        READWRITE(obj.vMerkleBranch);
        READWRITE(obj.nIndex);
        READWRITE(obj.vChainMerkleBranch);
        READWRITE(obj.nChainIndex);
        READWRITE(obj.parentBlock);
    }

    void SetNull()
    {
        coinbaseTx = nullptr;
        vMerkleBranch.clear();
        nIndex = 0;
        vChainMerkleBranch.clear();
        nChainIndex = 0;
        parentBlock.SetNull();
    }

    bool IsNull() const { return coinbaseTx == nullptr; }

    /**
     * Full validation of this proof against a claimed auxiliary-chain block
     * hash and chain ID. Checks, in order (each documented at its call site
     * in the .cpp, matching what the state rejection reason names):
     *   1. The parent block's own proof-of-work is valid against the
     *      AUXILIARY chain's nBits (nBitsAux) -- NOT the parent block's own
     *      claimed difficulty, and no other validation of the parent block's
     *      contents (its own tx validity, its own retarget correctness,
     *      etc.) is performed or required; merge mining borrows only the
     *      hash-vs-target proof-of-work property from the parent chain.
     *   2. The coinbase transaction's merkle branch correctly reproduces
     *      parentBlock.hashMerkleRoot, at the claimed nIndex.
     *   3. The coinbase's scriptSig contains the merge-mining magic tag
     *      exactly once (never zero, never more than once -- both are
     *      rejected, the latter being the historical ambiguity/forgery
     *      defense), immediately followed by a 32-byte commitment, a
     *      4-byte little-endian tree size, and a 4-byte little-endian nonce.
     *   4. The chain merkle branch, applied to hashAuxBlock at nChainIndex,
     *      reproduces the 32-byte commitment read in step 3.
     *   5. nChainIndex equals the deterministic expected slot for
     *      (committed nonce, nChainId, committed tree size) -- the
     *      index-grinding defense.
     *   6. The committed tree size is a sane, small power-of-two-bounded
     *      value (rejects a maliciously huge claimed size used to try to
     *      force excessive validation work or hash-grinding room).
     *
     * `nBitsAux` is the AUXILIARY (BitAIcoin) block's own required target --
     * i.e. what BitAIcoin's own DAA computed for the block being validated.
     * This is deliberately a separate parameter from `parentBlock.nBits`
     * (the real parent chain's own, unrelated, separately-tracked
     * difficulty): a real, caught-before-shipping bug in an earlier draft of
     * this function compared the parent hash against `parentBlock.nBits`
     * instead, which would have (a) validated proofs against the wrong
     * chain's difficulty entirely and (b) made BitAIcoin's own DAA output
     * irrelevant to AuxPoW validation -- exactly backwards from how merge
     * mining is supposed to work. Fixed before any test was written around
     * the wrong version, not discovered by a failing test.
     */
    bool Check(const uint256& hashAuxBlock, int32_t nChainId, uint32_t nBitsAux,
               const Consensus::Params& params, BlockValidationState& state) const;
};

#endif // BITCOIN_AUXPOW_H
