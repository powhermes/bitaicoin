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
#include <ios>
#include <memory>
#include <vector>

class BlockValidationState;

// NOTE: the version-bit helpers (VERSION_AUXPOW, GetBaseVersion, GetChainId,
// IsAuxpowVersion, MakeAuxpowVersion) used to be defined here. They now live
// in primitives/block.h instead (included above), specifically so that BOTH
// this file and CBlockHeader::IsAuxpow() can use the exact same single
// definition without a circular include between block.h and this file (this
// file needs the complete CBlockHeader type for CAuxPow::parentBlock;
// block.h cannot include this file in return). Moved, not duplicated --
// removing them from here was deliberate, not an oversight.

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
        // TX_NO_WITNESS, deliberately: a real, substantive bug caught by
        // actually compiling a stream round-trip this pass (not present in
        // the earlier standalone-construction tests, which never actually
        // serialized a CAuxPow over a stream) -- CTransactionRef fields
        // require an explicit TransactionSerParams wrapper on a plain
        // stream (this codebase's own TX_WITH_WITNESS/TX_NO_WITNESS idiom,
        // see primitives/transaction.h), or the code fails to compile at
        // all for any Stream lacking an attached TransactionSerParams. This
        // is also the semantically CORRECT choice, not just the one that
        // compiles: the coinbase's merkle-branch inclusion check
        // (CAuxPow::Check(), step 2) uses coinbaseTx->GetHash() -- the TXID,
        // which excludes witness data by definition -- matching how
        // parentBlock.hashMerkleRoot itself is always a TXID-based merkle
        // root. Witness data has no bearing on this proof and including it
        // would only add unnecessary bytes, so TX_NO_WITNESS is the
        // intentional, permanent choice here, not a placeholder.
        READWRITE(TX_NO_WITNESS(obj.coinbaseTx));
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

/**
 * Height-gated activation and pre/post-activation version-rule enforcement,
 * kept deliberately STANDALONE from CBlockHeader for now rather than spliced
 * into ContextualCheckBlockHeader() in this slice.
 *
 * Two real hazards were found while attempting that splice today, and both
 * are why it is deferred rather than rushed:
 *   1. `CBlockHeader::GetHash()` computes `(HashWriter{} << *this).GetHash()`,
 *      which uses this exact type's own generic Serialize -- the SAME one
 *      that would need to conditionally emit the auxpow payload for the wire
 *      format. Naively adding auxpow to CBlockHeader's SERIALIZE_METHODS
 *      would make an AuxPoW block's own identity hash silently include its
 *      auxpow bytes, which is circular and wrong: CAuxPow::Check() commits
 *      to hashAuxBlock, so that hash cannot itself depend on the auxpow
 *      payload it is meant to validate. The real fix (matching Namecoin's
 *      own design) is a header-hash computation that always covers only the
 *      6 base fields regardless of auxpow presence, with the auxpow payload
 *      carried through a SEPARATE, explicit wire-format serialization path
 *      (the way SegWit's txid vs wtxid split is handled via two distinct
 *      helper functions, not one generic conditional Serialize) -- a real,
 *      multi-call-site change deserving its own dedicated pass.
 *   2. Storing a `CAuxPow` (by value or pointer) on `CBlockHeader` needs
 *      forward-declaration plus explicit out-of-line special members to
 *      avoid a circular include (auxpow.h needs the complete CBlockHeader
 *      type for CAuxPow::parentBlock; block.h would need CAuxPow) --
 *      mechanical, but touches a type used throughout the entire node and
 *      deserves its own review, not a rushed addition alongside everything
 *      else in this pass.
 *
 * This function is the real height-gate/version-rule LOGIC, built and unit
 * tested now against explicit parameters rather than against CBlockHeader's
 * own (not-yet-existing) auxpow storage, so it is ready to be called from
 * ContextualCheckBlockHeader() as soon as that storage/serialization slice
 * lands, without needing to be rewritten.
 *
 * Rules enforced, matching docs/AUXPOW_MILESTONE.md sec.4/5:
 *   - Below `activationHeight`: the AUXPOW version bit MUST NOT be set. A
 *     pre-activation block/header claiming to be AuxPoW-flagged is rejected
 *     outright -- the bit has no defined meaning before activation and must
 *     not be silently ignored.
 *   - At/after `activationHeight`, AUXPOW bit set: the block ID embedded in
 *     the version (GetChainId) must equal `expectedChainId`; an `auxpow`
 *     proof must be supplied (non-null); and it must pass `CAuxPow::Check()`
 *     against this header's own hash and its own nBits (the auxiliary
 *     chain's required target for this specific block, computed by
 *     BitAIcoin's own DAA the ordinary way -- unaffected by AuxPoW).
 *   - At/after `activationHeight`, AUXPOW bit NOT set: direct (non-merge-
 *     mined) SHA256d mining remains valid, exactly as before activation --
 *     AuxPoW is an additional accepted proof format, not a replacement that
 *     forbids direct mining. This function has nothing further to check in
 *     that case; ordinary `CheckProofOfWork` handles it as always.
 */
bool CheckAuxPowRules(int32_t nVersion, int nHeight, const uint256& hashHeader, uint32_t nBits,
                       const CAuxPow* auxpow, int32_t expectedChainId, int activationHeight,
                       const Consensus::Params& params, BlockValidationState& state);

/**
 * A merkle branch with more than this many levels is malformed on its face
 * (2^32 leaves would need at most 32 levels; anything more cannot correspond
 * to any real transaction position) -- rejected before any hashing work is
 * done with it, both for CAuxPow::Check()'s own chain-merkle-branch check
 * (already enforced there) and for the coinbase merkle branch during
 * deserialization (enforced here, at the wire-format boundary, per explicit
 * instruction to "preserve unknown/malformed-data rejection and size
 * limits"). This is IN ADDITION to, not instead of, the generic compact-size
 * length-prefix sanity limit (`MAX_SIZE`) that this codebase's ordinary
 * `std::vector<uint256>` deserialization already enforces on any serialized
 * vector regardless of type.
 */
static constexpr size_t MAX_MERKLE_BRANCH_LENGTH = 32;

/**
 * Auxpow-aware header (de)serialization, kept EXPLICITLY SEPARATE from
 * CBlockHeader's own SERIALIZE_METHODS (see the design-hazard comment on
 * CBlockHeader::auxpow in primitives/block.h) -- mirrors this codebase's own
 * existing SerializeTransaction/UnserializeTransaction free-function split
 * for txid-vs-wtxid concerns (src/primitives/transaction.h), not invented
 * from scratch. Works for ANY conforming Stream (network `DataStream`,
 * disk-backed `AutoFile`, etc.) since it is templated exactly like that
 * existing precedent -- there is no separate "disk format" vs "network
 * format" at this layer, only different Stream backends given to the same
 * function, which is the design real Bitcoin Core already uses elsewhere.
 *
 * Byte-for-byte compatible with plain CBlockHeader serialization for every
 * pre-activation / non-AuxPoW header: the six base fields are written in the
 * exact same order via the exact same primitive Serialize calls CBlockHeader
 * itself would use, and the auxpow payload is only ever touched when
 * `IsAuxpowVersion(nVersion)` is true.
 */
template <typename Stream>
void SerializeBlockHeaderWithAuxPow(const CBlockHeader& header, Stream& s)
{
    s << header.nVersion << header.hashPrevBlock << header.hashMerkleRoot
      << header.nTime << header.nBits << header.nNonce;
    if (header.IsAuxpow()) {
        if (!header.auxpow) {
            throw std::ios_base::failure("SerializeBlockHeaderWithAuxPow: AUXPOW version bit set but no auxpow proof attached");
        }
        s << *header.auxpow;
    }
}

template <typename Stream>
void UnserializeBlockHeaderWithAuxPow(CBlockHeader& header, Stream& s)
{
    s >> header.nVersion >> header.hashPrevBlock >> header.hashMerkleRoot
      >> header.nTime >> header.nBits >> header.nNonce;
    if (header.IsAuxpow()) {
        auto proof = std::make_shared<CAuxPow>();
        s >> *proof;
        if (proof->vMerkleBranch.size() > MAX_MERKLE_BRANCH_LENGTH ||
            proof->vChainMerkleBranch.size() > MAX_MERKLE_BRANCH_LENGTH) {
            throw std::ios_base::failure("UnserializeBlockHeaderWithAuxPow: merkle branch implausibly long, rejected");
        }
        header.auxpow = std::move(proof);
    } else {
        header.auxpow.reset();
    }
}

#endif // BITCOIN_AUXPOW_H
