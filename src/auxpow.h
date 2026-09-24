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
// STATUS (see docs/AUXPOW_MILESTONE.md for the full history of how this was
// built up in separate, reviewed slices): serialization + validation core
// (CAuxPow::Check()), the header-level CheckBitAIProofOfWork() dispatcher,
// and the CheckAuxPowRules() height/policy gate are all wired into the live
// path -- CheckBlockHeader()/HasValidProofOfWork() (src/validation.cpp) call
// CheckBitAIProofOfWork() in place of the plain CheckProofOfWork() for every
// header; ContextualCheckBlockHeader() calls CheckAuxPowRules() using
// consensusParams.BitAIAuxpowActivationHeight (227808 on the real BitAIcoin
// chain). net_processing's header/block relay and on-demand disk storage
// (node/blockstorage.cpp) are likewise wired -- see the real call-site
// comments in each of those files, not just this one. `CheckProofOfWorkImpl`/
// `CheckProofOfWork` (src/pow.h) remain deliberately untouched, exactly as
// designed (see CheckBitAIProofOfWork()'s own doc comment below). GBT/mining
// RPCs (createauxblock/submitauxblock) and ASERT dispatch remain separate,
// not-yet-built later slices. Existing blocks 225430-225823 remain untouched
// by any of this -- they are all pre-activation and below AuxPoW's height
// gate regardless.

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
 * Matches this codebase's own `CTransactionRef` convention
 * (`std::shared_ptr<const CTransaction>`, primitives/transaction.h) exactly:
 * a shared, immutable-after-construction handle. `CBlockHeader::auxpow` is
 * declared using the raw type directly (not this alias) since block.h only
 * forward-declares `CAuxPow` and cannot see this alias; this is the
 * convenience name for use everywhere else (tests, and any future code that
 * builds a proof to attach to a header).
 */
using CAuxPowRef = std::shared_ptr<const CAuxPow>;

/**
 * Height-gated activation and pre/post-activation version-rule enforcement.
 *
 * SPLICED into the live path as of 2026-09-23 (docs/AUXPOW_MILESTONE.md
 * sec.5): called from `ContextualCheckBlockHeader()` (src/validation.cpp)
 * using `consensusParams.BitAIAuxpowActivationHeight` as `activationHeight`
 * (227808 for the real BitAIcoin chain; INT_MAX, i.e. permanently
 * pre-activation, for every other chain type; 1 for REGTEST, matching
 * BIP34Height's own "always active unless overridden" convention, so
 * regtest-based tests can exercise real acceptance near genesis).
 *
 * Originally built and unit-tested standalone, against explicit parameters,
 * before `CBlockHeader::auxpow` storage existed at all -- two real hazards
 * were resolved before this function could be usefully called from real
 * validation: (1) `CBlockHeader::GetHash()` had to be proven to cover only
 * the 6 base fields regardless of auxpow presence (a naive addition to
 * `SERIALIZE_METHODS` would make an AuxPoW block's own identity hash
 * circularly depend on the very proof it's used to validate), solved via the
 * separate `SerializeBlockHeaderWithAuxPow`/`GetHash()` split documented on
 * `CBlockHeader::auxpow` in primitives/block.h; (2) storing a `CAuxPow` on
 * `CBlockHeader` needed the forward-declaration/shared_ptr design also
 * documented there. Both landed in earlier, separate, dedicated passes
 * before this splice -- kept as history here since it explains why this
 * function was built and tested well before it was actually wired in, not
 * because that state still holds today.
 *
 * This function itself remains callable/testable in complete isolation
 * (see src/test/auxpow_tests.cpp's boundary tests, which call it directly
 * with literal heights rather than mining a real chain to them) -- the live
 * splice is an additional real call site, not a replacement for that.
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
 * BitAIcoin's real, frozen AuxPoW chain ID (docs/AUXPOW_MILESTONE.md sec.5/B):
 * 16969 / 0x4249, the two ASCII bytes "BI" read as a big-endian u16. Promoted
 * here from a proposal-only value to a real, named consensus constant now
 * that CheckBitAIProofOfWork() below needs to reference it directly.
 */
static constexpr int32_t BITAI_AUXPOW_CHAIN_ID = 16969;

/**
 * Local copies of src/versionbits.h's VERSIONBITS_TOP_BITS/VERSIONBITS_TOP_MASK
 * (0x20000000 / 0xE0000000), NOT #include'd from there: versionbits.h
 * includes chain.h, which now includes THIS file (for CBlockIndex::auxpow),
 * so auxpow.h including versionbits.h would be a circular include. These are
 * long-standardized, stable BIP9 protocol constants (unlikely to ever change
 * upstream), reproduced here only so this file can PROVE, at compile time,
 * that BitAIcoin's chain-ID encoding never collides with BIP9's own marker
 * bits -- see the static_assert below and the runtime defense in
 * CheckBitAIProofOfWork() (auxpow.cpp), which is the actual enforcement;
 * this header-only copy exists for the proof, not as a second source of
 * truth for the real constant (auxpow.cpp separately #includes the real
 * versionbits.h, which a .cpp file can do without a cycle).
 */
static constexpr int32_t AUXPOW_VERSIONBITS_TOP_MASK = 0xE0000000;
static constexpr int32_t AUXPOW_VERSIONBITS_TOP_BITS = 0x20000000;

/**
 * PROVEN at compile time, not assumed: BitAIcoin's real AuxPoW chain ID,
 * placed in nVersion's bits 16-31 the way MakeAuxpowVersion() does, never
 * produces the BIP9 top-bits marker (0b001 in bits 29-31) -- i.e. a
 * genuinely AuxPoW-flagged BitAIcoin header can never accidentally look
 * like a BIP9-signaling version by construction, for THIS chain ID. (The
 * runtime check in CheckBitAIProofOfWork() is still the real enforcement,
 * covering any future encoding change; this is the belt to that
 * suspenders, checked at every single compile rather than left as a
 * one-time manual calculation.)
 */
static_assert((MakeAuxpowVersion(BITAI_AUXPOW_CHAIN_ID, 0) & AUXPOW_VERSIONBITS_TOP_MASK) != AUXPOW_VERSIONBITS_TOP_BITS,
              "BITAI_AUXPOW_CHAIN_ID's encoding collides with the BIP9 versionbits top-bits marker -- "
              "an AuxPoW-flagged header with this chain ID would be indistinguishable from a BIP9-signaling "
              "version in the affected bits; choose a different chain ID or revisit the encoding before proceeding");

/**
 * The single header-level proof-of-work decision point: "is this header's
 * OWN SELECTED proof mechanism (direct SHA256d, or AuxPoW) cryptographically
 * valid?" -- deliberately NOT a statement about whether that mechanism is
 * ALLOWED at this header's height (that is CheckAuxPowRules()'s job, kept
 * separate on purpose -- see the comment there and docs/AUXPOW_MILESTONE.md
 * sec.0/4). Do not collapse the two: this function has no height parameter
 * and must not gain one.
 *
 * CHAIN-AWARE, first (2026-09-23, docs/AUXPOW_MILESTONE.md sec.6): if
 * `!params.fBitAIAuxpowEnabled`, this chain has no AuxPoW concept at all --
 * `header.nVersion`'s bit 8 retains whatever ordinary historical meaning it
 * has on that chain (nothing here), and the ONLY thing checked is the plain,
 * unmodified `CheckProofOfWork(header.GetHash(), header.nBits, params)`,
 * REGARDLESS of the header's own `IsAuxpow()`/`auxpow` state. This is the
 * real fix for a found-in-review bug: earlier, this function dispatched on
 * `header.IsAuxpow()` alone, with no chain check, which would have been
 * WRONG on MAIN/TESTNET/TESTNET4/SIGNET-style chains (a header there with
 * bit 8 set for unrelated reasons must never be treated as "carries an
 * AuxPoW proof").
 *
 * `CheckProofOfWorkImpl`/`CheckProofOfWork` (src/pow.h) remain exactly what
 * they always were -- the plain, low-level hash-vs-target primitive, with
 * NO knowledge of AuxPoW. This function is the header-aware DISPATCHER in
 * front of it, matching the architectural split real Namecoin/Dogecoin-
 * style AuxPoW implementations use (their own AuxPoW-aware check dispatches
 * between child-header PoW and parent-header PoW, while their generic
 * CheckProofOfWork stays a plain hash/target check) -- not a new pattern
 * invented for this codebase.
 *
 * Semantics, once `params.fBitAIAuxpowEnabled` is true:
 *   - DIRECT block (`header.IsAuxpow()` false): no proof required; the
 *     header's OWN hash must satisfy the header's OWN `nBits`, via the
 *     ordinary, unmodified `CheckProofOfWork`. Byte-for-byte the same
 *     decision every block before this function existed would have gotten.
 *   - AUXPOW block (`header.IsAuxpow()` true): a proof MUST be attached
 *     (`header.auxpow != nullptr`); the header's own hash is explicitly NOT
 *     required to satisfy `nBits` (only the parent block's hash is, and
 *     that check lives inside `CAuxPow::Check()`); `CAuxPow::Check()` is
 *     called with this header's own hash (the commitment the proof must
 *     prove the parent committed to) and this header's own `nBits` (the
 *     BitAIcoin-chain target the parent's hash must satisfy) --
 *     `BITAI_AUXPOW_CHAIN_ID` is passed as the required chain ID, so a
 *     proof legitimately produced for some OTHER merge-mined chain's ID is
 *     rejected here, at the cryptographic-validity layer, not deferred to
 *     policy.
 *
 * Safe to call unconditionally on every header, at every height, on any
 * chain type: on an AuxPoW-disabled chain it is unconditionally
 * behavior-identical to a plain `CheckProofOfWork(header.GetHash(),
 * header.nBits, params)` call; on an AuxPoW-enabled chain before any real
 * AuxPoW-producing code has ever run, no header can have the AUXPOW bit set
 * in practice, so it is likewise behavior-identical there too.
 */
bool CheckBitAIProofOfWork(const CBlockHeader& header, const Consensus::Params& params, BlockValidationState& state);

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
 *
 * `auxpowEnabled` is REQUIRED (no default), by explicit design
 * (docs/AUXPOW_MILESTONE.md sec.6): `IsAuxpowVersion(nVersion)` alone is
 * NEVER a safe basis for deciding whether an auxpow payload follows -- bit 8
 * has its ordinary historical meaning on any chain where AuxPoW is not a
 * defined concept (MAIN/TESTNET/TESTNET4/SIGNET-style chains), and a header
 * from one of those chains that happens to have bit 8 set for unrelated
 * reasons must serialize/deserialize with EXACTLY the historical 80-byte
 * format, never an auxpow payload. Every call site must pass this explicitly
 * from `params.fBitAIAuxpowEnabled` -- there is no safe way to infer it from
 * the header alone, which is why this isn't a defaulted parameter.
 */
template <typename Stream>
void SerializeBlockHeaderWithAuxPow(const CBlockHeader& header, Stream& s, bool auxpowEnabled)
{
    s << header.nVersion << header.hashPrevBlock << header.hashMerkleRoot
      << header.nTime << header.nBits << header.nNonce;
    if (auxpowEnabled && header.IsAuxpow()) {
        if (!header.auxpow) {
            throw std::ios_base::failure("SerializeBlockHeaderWithAuxPow: AUXPOW version bit set but no auxpow proof attached");
        }
        s << *header.auxpow;
    }
}

template <typename Stream>
void UnserializeBlockHeaderWithAuxPow(CBlockHeader& header, Stream& s, bool auxpowEnabled)
{
    s >> header.nVersion >> header.hashPrevBlock >> header.hashMerkleRoot
      >> header.nTime >> header.nBits >> header.nNonce;
    if (auxpowEnabled && header.IsAuxpow()) {
        auto proof = std::make_shared<CAuxPow>();
        s >> *proof;
        if (proof->vMerkleBranch.size() > MAX_MERKLE_BRANCH_LENGTH ||
            proof->vChainMerkleBranch.size() > MAX_MERKLE_BRANCH_LENGTH) {
            throw std::ios_base::failure("UnserializeBlockHeaderWithAuxPow: merkle branch implausibly long, rejected");
        }
        header.auxpow = std::move(proof);
    } else {
        // Either genuinely no proof attached, OR this chain has no AuxPoW
        // concept at all -- in the latter case bit 8 (if set) is left alone
        // in nVersion (its ordinary historical meaning is preserved
        // untouched) but no payload is ever read for it.
        header.auxpow.reset();
    }
}

/**
 * Full, auxpow-aware CBlock (de)serialization: header (6 fields) + optional
 * auxpow proof + the block's own transactions, in that order.
 *
 * DELIBERATELY NOT implemented by changing `CBlock::SERIALIZE_METHODS`
 * itself (in primitives/block.h): that would require block.h to see this
 * file's declarations (or vice versa), and the two files already can't
 * include each other (block.h forward-declares CAuxPow specifically to
 * avoid that cycle -- see the comment there). Exactly as with
 * SerializeBlockHeaderWithAuxPow above, this is a SEPARATE, explicit
 * function pair rather than baked into the generic type's own Serialize --
 * consistent with, not a workaround for, this whole slice's established
 * pattern. `CBlock::SERIALIZE_METHODS` itself is UNCHANGED (still
 * `AsBase<CBlockHeader>(obj), obj.vtx`, i.e. base-header-only, exactly as
 * before this slice) -- so it, and therefore every existing caller of plain
 * `<<`/`>>` on a `CBlock` (which never expected an auxpow field to exist
 * before this slice), continues to serialize non-AuxPoW blocks
 * byte-for-byte identically. Only code that explicitly calls these new
 * functions gets the auxpow-aware behavior.
 *
 * `with_witness` controls the block's own transactions' witness inclusion
 * (default true, the modern/common case) -- NOT the same thing as the
 * `TX_NO_WITNESS` used unconditionally for the auxpow proof's internal
 * coinbase above, which is a completely different, unrelated transaction
 * with its own, separate, permanent requirement (see the SERIALIZE_METHODS
 * comment on CAuxPow). Real net_processing relay needs both variants for a
 * block's own transactions (legacy no-witness relay to pre-SegWit peers vs.
 * modern with-witness relay) -- exposed as a parameter here, mirroring
 * net_processing.cpp's own real `TX_NO_WITNESS(*pblock)` /
 * `TX_WITH_WITNESS(*pblock)` call sites, rather than silently hardcoding
 * one and losing the other for AuxPoW blocks specifically.
 *
 * `auxpowEnabled` is REQUIRED, same rationale as
 * SerializeBlockHeaderWithAuxPow above -- placed before `with_witness`
 * (which keeps its default) since it is never safe to default.
 */
template <typename Stream>
void SerializeBlockWithAuxPow(const CBlock& block, Stream& s, bool auxpowEnabled, bool with_witness = true)
{
    SerializeBlockHeaderWithAuxPow(block, s, auxpowEnabled); // CBlock IS-A CBlockHeader
    if (with_witness) {
        s << TX_WITH_WITNESS(block.vtx);
    } else {
        s << TX_NO_WITNESS(block.vtx);
    }
}

template <typename Stream>
void UnserializeBlockWithAuxPow(CBlock& block, Stream& s, bool auxpowEnabled, bool with_witness = true)
{
    UnserializeBlockHeaderWithAuxPow(block, s, auxpowEnabled);
    if (with_witness) {
        s >> TX_WITH_WITNESS(block.vtx);
    } else {
        s >> TX_NO_WITNESS(block.vtx);
    }
}

/**
 * Send/receive wrapper objects carrying the block plus its explicit
 * chain-awareness/witness flags, usable directly with `<<`/`>>` and
 * `GetSerializeSize()` (via their own `Serialize`/`Unserialize` methods --
 * the same pattern `AuxPowHeadersVectorWrapper` below already uses, NOT
 * `Using<>()`: `Using<F>()` dispatches on a static TYPE, so it cannot carry
 * a runtime `auxpowEnabled` bool the way these plain wrapper objects can).
 * Replaced the earlier `AuxPowBlockWithWitness()`/`AuxPowBlockNoWitness()`
 * `Using<>()`-based convenience wrappers (2026-09-23,
 * docs/AUXPOW_MILESTONE.md sec.6): those had NO way to take a runtime
 * chain-awareness parameter at all, so every call site silently assumed
 * AuxPoW semantics applied -- correct on BitAIcoin/regtest, wrong on every
 * other chain type. Every real call site now passes
 * `params.fBitAIAuxpowEnabled` explicitly.
 */
namespace detail {
struct AuxPowBlockSerializer {
    const CBlock& block;
    bool auxpowEnabled;
    bool with_witness;
    template <typename Stream>
    void Serialize(Stream& s) const { SerializeBlockWithAuxPow(block, s, auxpowEnabled, with_witness); }
};
struct AuxPowBlockDeserializer {
    CBlock& block;
    bool auxpowEnabled;
    bool with_witness;
    template <typename Stream>
    void Unserialize(Stream& s) { UnserializeBlockWithAuxPow(block, s, auxpowEnabled, with_witness); }
};
} // namespace detail
inline detail::AuxPowBlockSerializer AuxPowBlockForSend(const CBlock& block, bool auxpowEnabled, bool with_witness = true)
{
    return detail::AuxPowBlockSerializer{block, auxpowEnabled, with_witness};
}
inline detail::AuxPowBlockDeserializer AuxPowBlockForRecv(CBlock& block, bool auxpowEnabled, bool with_witness = true)
{
    return detail::AuxPowBlockDeserializer{block, auxpowEnabled, with_witness};
}

// NOTE: the earlier `AuxPowHeaderFormatter` (for `Using<>()` on a bare
// `CBlockHeader` field, e.g. BIP152 compact blocks) was REMOVED (2026-09-23,
// docs/AUXPOW_MILESTONE.md sec.6), for the same reason: `Using<>()`'s
// static-type dispatch cannot carry a runtime chain-awareness flag, and
// `CBlockHeaderAndShortTxIDs::SERIALIZE_METHODS` (src/blockencodings.h) is a
// generic template with no way to inject one either. Compact blocks
// (BIP152) now use PLAIN, always-stock `CBlockHeader` serialization for
// their header field -- byte-for-byte identical to upstream Bitcoin Core,
// unconditionally, regardless of chain type or nVersion bit 8. AuxPoW-flagged
// blocks are deliberately never relayed via compact blocks on an
// AuxPoW-enabled chain (see the real send-site gates in net_processing.cpp);
// they fall back to full BLOCK/HEADERS relay, both of which ARE correctly
// chain-aware. This is a real, disclosed simplification (compact-block
// bandwidth savings do not apply to AuxPoW blocks specifically), not a gap:
// nothing about it can silently misinterpret bit 8 on any chain.

/**
 * HEADERS-message (de)serialization, auxpow-aware, for
 * `std::vector<CBlockHeader>` -- the real wire shape net_processing.cpp
 * uses for a HEADERS message: a compact-size count, then per header the six
 * base fields (plus an auxpow payload when its version bit is set) followed
 * by a compact-size 0 (the historical "as if each header were a CBlock with
 * zero transactions" convention -- preserved EXACTLY, not reinterpreted:
 * real Bitcoin's own headers-first design reused the block wire shape for
 * this, and this stays byte-for-byte compatible with that for any header
 * with no auxpow attached).
 *
 * Only a SEND-side wrapper is provided here (`AuxPowHeadersForAnnounce`):
 * net_processing.cpp's real receive path already does its own manual
 * per-element loop (to enforce `max_headers_result` via `Misbehaving()`
 * before ever allocating `nCount` headers' worth of memory -- a real DoS
 * defense that must not be bypassed by a generic vector-formatter that
 * would resize/loop before that check could run). The receive side is
 * fixed in place, in net_processing.cpp itself, by swapping only its
 * per-header deserialization call from the plain generic one to
 * `UnserializeBlockHeaderWithAuxPow` (passing `auxpowEnabled` explicitly at
 * that call site too) -- not by introducing a competing mechanism here that
 * would have to duplicate that size check to be safe.
 *
 * `auxpowEnabled` is REQUIRED here as well, same rationale as the functions
 * above -- a plain object (not `Using<>()`) precisely so it CAN carry this
 * runtime flag.
 */
namespace detail {
struct AuxPowHeadersVectorWrapper {
    const std::vector<CBlockHeader>& headers;
    bool auxpowEnabled;
    template <typename Stream>
    void Serialize(Stream& s) const
    {
        WriteCompactSize(s, headers.size());
        for (const auto& header : headers) {
            SerializeBlockHeaderWithAuxPow(header, s, auxpowEnabled);
            WriteCompactSize(s, 0); // historical trailing tx-count=0, preserved exactly
        }
    }
};
} // namespace detail
inline detail::AuxPowHeadersVectorWrapper AuxPowHeadersForAnnounce(const std::vector<CBlockHeader>& headers, bool auxpowEnabled)
{
    return detail::AuxPowHeadersVectorWrapper{headers, auxpowEnabled};
}

#endif // BITCOIN_AUXPOW_H
