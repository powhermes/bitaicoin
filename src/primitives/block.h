// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include <primitives/transaction.h>
#include <serialize.h>
#include <uint256.h>
#include <util/time.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// --- BitAIcoin AuxPoW addition (not stock Bitcoin Core) ---
// Forward-declared, not #included: CAuxPow's own definition (src/auxpow.h)
// needs the COMPLETE CBlockHeader type (CAuxPow::parentBlock is a
// CBlockHeader held by value), so this header cannot include auxpow.h
// without a circular include. A std::shared_ptr<CAuxPow> member does not
// need the pointee's complete type at CBlockHeader's own definition point --
// unlike std::unique_ptr, shared_ptr's deleter is captured (type-erased) at
// construction time, so its destructor works fine with an incomplete type as
// long as construction (e.g. std::make_shared<CAuxPow>()) only ever happens
// in a translation unit that has included auxpow.h (verified by compiling
// this pass, not just reasoned about -- see src/test/auxpow_tests.cpp).
class CAuxPow;

/**
 * Height-gated version-bit convention for AuxPoW-flagged blocks (BitAIcoin
 * addition; see docs/AUXPOW_MILESTONE.md). Kept here, not in auxpow.h, so
 * that BOTH this file (CBlockHeader::IsAuxpow()) and auxpow.h can use the
 * same single definition without a circular include between them.
 *
 *   nVersion bit 8 (0x100)      -- VERSION_AUXPOW: this header carries a
 *                                  CAuxPow structure and MUST be validated
 *                                  via merge-mining, not via its own hash.
 *   nVersion bits 16-31         -- the merge-mined chain's ID (BitAIcoin's is
 *                                  16969 / 0x4249, "BI").
 *
 * PERMANENT DESIGN DECISION (2026-09-23, explicit, not to be "corrected"
 * later): post-activation, BOTH direct SHA256d mining (this bit unset) AND
 * AuxPoW (this bit set) remain valid forever. AuxPoW is an ADDITIONAL
 * accepted proof format, never a mandatory replacement -- see the permanent
 * policy section at the top of docs/AUXPOW_MILESTONE.md for the full
 * rationale. Do not gate direct-mining acceptance on this bit's absence in
 * any future change without re-reading that section first.
 */
static constexpr int32_t VERSION_AUXPOW = (1 << 8);
static constexpr int32_t VERSION_CHAIN_ID_SHIFT = 16;

inline int32_t GetBaseVersion(int32_t nVersion) { return nVersion % VERSION_AUXPOW; }
inline int32_t GetChainId(int32_t nVersion) { return nVersion >> VERSION_CHAIN_ID_SHIFT; }
inline bool IsAuxpowVersion(int32_t nVersion) { return (nVersion & VERSION_AUXPOW) != 0; }
inline int32_t MakeAuxpowVersion(int32_t nChainId, int32_t nBaseVersion)
{
    return (nChainId << VERSION_CHAIN_ID_SHIFT) | VERSION_AUXPOW | nBaseVersion;
}
// --- end BitAIcoin AuxPoW addition ---

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    // --- BitAIcoin AuxPoW addition (not stock Bitcoin Core) ---
    // Deliberately NOT part of SERIALIZE_METHODS below, and therefore NOT
    // part of what GetHash() hashes: GetHash() must cover only the six base
    // fields above, regardless of whether an AuxPoW proof is attached,
    // because CAuxPow::Check() validates a proof AGAINST this hash -- the
    // hash cannot depend on the very proof it's used to validate (a real
    // hazard found and deliberately designed around this pass; see
    // docs/AUXPOW_MILESTONE.md). The auxpow-aware wire/disk format is a
    // SEPARATE, explicit serialization path: SerializeBlockHeaderWithAuxPow /
    // UnserializeBlockHeaderWithAuxPow in src/auxpow.h, mirroring how this
    // codebase already splits CTransaction's txid-vs-wtxid concerns into
    // SerializeTransaction/UnserializeTransaction free functions rather than
    // one generic conditional Serialize.
    //
    // OWNERSHIP/VALUE SEMANTICS, decided explicitly (2026-09-23), not left
    // implicit: this is `shared_ptr<const CAuxPow>` -- immutable once
    // attached -- not `shared_ptr<CAuxPow>` and not a deep-copied value
    // member. A plain mutable `shared_ptr<CAuxPow>` would mean an ordinary
    // CBlockHeader copy (which happens all over this codebase, e.g.
    // `CBlock(const CBlockHeader&)`'s `*this = header` a few lines below)
    // ALIASES the same proof object as the original; mutating the proof
    // through one copy would then silently change what the other copy
    // reports too, which is exactly the kind of surprising aliasing a
    // consensus primitive that otherwise has plain value semantics must not
    // have. `const`-qualifying the pointee closes this off at the type
    // level -- there is no way to obtain a non-const `CAuxPow&` through this
    // member at all, so "mutate one copy's proof" is not merely undone by
    // convention, it does not compile. This exactly mirrors
    // `CTransactionRef` (`std::shared_ptr<const CTransaction>`,
    // primitives/transaction.h), this codebase's own established pattern
    // for shared, immutable-after-construction consensus payloads -- not a
    // new convention invented for this member. Deep-copy-on-header-copy was
    // considered and rejected: it would still allow in-place mutation of
    // the (now-distinct) copy's proof, a smaller but still real footgun,
    // and would add a real per-copy cost (coinbase tx + two merkle
    // branches + a full parent header) that cheap CBlockHeader copies
    // should not silently acquire. A future proof is attached by
    // constructing a brand-new `CAuxPow` value and a brand-new shared_ptr
    // (see `src/test/auxpow_tests.cpp`'s
    // `auxpow_ownership_is_shared_and_immutable` test), never by mutating
    // an already-attached one in place -- verified there is no call site
    // anywhere in this codebase (as of this pass) that needs to do the
    // latter (mining/GBT support, the one plausible future need, does not
    // exist yet).
    std::shared_ptr<const CAuxPow> auxpow;
    // --- end BitAIcoin AuxPoW addition ---

    CBlockHeader()
    {
        SetNull();
    }

    SERIALIZE_METHODS(CBlockHeader, obj) { READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot, obj.nTime, obj.nBits, obj.nNonce); }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
        auxpow.reset(); // BitAIcoin AuxPoW addition
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    // BitAIcoin AuxPoW addition.
    bool IsAuxpow() const { return IsAuxpowVersion(nVersion); }

    uint256 GetHash() const;

    NodeSeconds Time() const
    {
        return NodeSeconds{std::chrono::seconds{nTime}};
    }

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }
};


class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransactionRef> vtx;

    // Memory-only flags for caching expensive checks
    mutable bool fChecked;                            // CheckBlock()
    mutable bool m_checked_witness_commitment{false}; // CheckWitnessCommitment()
    mutable bool m_checked_merkle_root{false};        // CheckMerkleRoot()

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *(static_cast<CBlockHeader*>(this)) = header;
    }

    SERIALIZE_METHODS(CBlock, obj)
    {
        READWRITE(AsBase<CBlockHeader>(obj), obj.vtx);
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        fChecked = false;
        m_checked_witness_commitment = false;
        m_checked_merkle_root = false;
    }

    std::string ToString() const;
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    /** Historically CBlockLocator's version field has been written to network
     * streams as the negotiated protocol version and to disk streams as the
     * client version, but the value has never been used.
     *
     * Hard-code to the highest protocol version ever written to a network stream.
     * SerParams can be used if the field requires any meaning in the future,
     **/
    static constexpr int DUMMY_VERSION = 70016;

    std::vector<uint256> vHave;

    CBlockLocator() = default;

    explicit CBlockLocator(std::vector<uint256>&& have) : vHave(std::move(have)) {}

    SERIALIZE_METHODS(CBlockLocator, obj)
    {
        int nVersion = DUMMY_VERSION;
        READWRITE(nVersion);
        READWRITE(obj.vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

#endif // BITCOIN_PRIMITIVES_BLOCK_H
