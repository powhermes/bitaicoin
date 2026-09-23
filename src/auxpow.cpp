// Copyright (c) 2026 The BitAIcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <auxpow.h>

#include <consensus/validation.h>
#include <hash.h>
#include <pow.h>
#include <script/script.h>

#include <cstring>

int GetExpectedMerkleTreeIndex(uint32_t nNonce, int32_t nChainId, unsigned nTreeSize)
{
    // Identical LCG formula to Namecoin's original merged-mining spec.
    // NOT a cryptographic function -- it is deliberately simple and
    // deterministic so every party (parent-chain miner, aux-chain node) can
    // compute the same expected slot from public data. Its security property
    // is not "hard to predict" but "hard for a miner to grind a *different*
    // slot for the same nonce/chain-id/tree-size combination," which a
    // reproducible non-cryptographic LCG already achieves for this purpose:
    // the miner does not get to choose the tree size or chain ID after
    // deciding which slot they want, since both are fixed by what's actually
    // being merge-mined.
    uint32_t rand = nNonce;
    rand = rand * 1103515245 + 12345;
    rand += static_cast<uint32_t>(nChainId);
    rand = rand * 1103515245 + 12345;
    return static_cast<int>(rand % nTreeSize);
}

uint256 CheckMerkleBranch(uint256 hash, const std::vector<uint256>& merkleBranch, int nIndex)
{
    if (nIndex == -1) return uint256();
    for (const uint256& otherside : merkleBranch) {
        // uint256::Serialize writes exactly its 32 raw bytes with no framing
        // (verified in src/uint256.h), so pushing the two halves through
        // HashWriter in the right order reproduces double-SHA256(left ||
        // right) exactly, matching every other merkle computation in this
        // codebase (see src/consensus/merkle.cpp's own use of raw
        // concatenation via SHA256D64) without needing a raw byte buffer.
        HashWriter hasher{};
        if ((nIndex & 1) != 0) {
            hasher << otherside << hash;
        } else {
            hasher << hash << otherside;
        }
        hash = hasher.GetHash();
        nIndex >>= 1;
    }
    return hash;
}

namespace {

/**
 * Searches `script` (as a raw byte string) for the merge-mining magic tag,
 * requiring EXACTLY ONE occurrence, immediately followed by a 32-byte
 * commitment hash, a 4-byte little-endian tree size, and a 4-byte
 * little-endian nonce (40 bytes total after the 4-byte tag).
 *
 * The "exactly one" requirement is the historical Namecoin
 * ambiguity/forgery defense: if the tag could legally appear more than once,
 * or could be interpreted starting at more than one byte offset, a
 * maliciously constructed coinbase could be simultaneously valid "proof" for
 * two different interpretations, one of which the honest chain-merkle
 * verification in step 4 might not actually cover -- rejecting any script
 * with more than one match closes that ambiguity outright rather than trying
 * to reason about which occurrence is "the real one."
 */
bool FindMergeMiningTag(const CScript& script, uint256& commitment, uint32_t& nTreeSize, uint32_t& nMergeMineNonce, std::string& err)
{
    const std::vector<unsigned char> bytes(script.begin(), script.end());
    const size_t tagLen = sizeof(MERGE_MINING_HEADER);
    const size_t payloadLen = 32 + 4 + 4;

    std::vector<size_t> matches;
    if (bytes.size() >= tagLen) {
        for (size_t i = 0; i + tagLen <= bytes.size(); ++i) {
            if (std::memcmp(bytes.data() + i, MERGE_MINING_HEADER, tagLen) == 0) {
                matches.push_back(i);
            }
        }
    }

    if (matches.empty()) {
        err = "merge-mining tag not found in coinbase";
        return false;
    }
    if (matches.size() > 1) {
        err = "multiple merge-mining tags found in coinbase (ambiguous, rejected)";
        return false;
    }

    const size_t pos = matches[0] + tagLen;
    if (pos + payloadLen > bytes.size()) {
        err = "merge-mining tag found but truncated payload";
        return false;
    }

    std::memcpy(commitment.begin(), bytes.data() + pos, 32);
    std::memcpy(&nTreeSize, bytes.data() + pos + 32, 4);
    std::memcpy(&nMergeMineNonce, bytes.data() + pos + 36, 4);
    // Fields are wire little-endian; on a big-endian host these would need
    // byte-swapping. Not handled here since every real deployment target for
    // this codebase is little-endian, matching the rest of Bitcoin Core's
    // own (documented, long-standing) assumption.
    return true;
}

} // namespace

bool CAuxPow::Check(const uint256& hashAuxBlock, int32_t nChainId, uint32_t nBitsAux,
                     const Consensus::Params& params, BlockValidationState& state) const
{
    if (IsNull()) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER,
                              "auxpow-null", "AuxPoW proof is empty");
    }

    // (1) Parent block's hash must satisfy the AUXILIARY chain's own current
    // target (nBitsAux, computed by BitAIcoin's own DAA for the block being
    // validated) -- deliberately NOT parentBlock.nBits, which is the real
    // parent chain's own, unrelated difficulty and is otherwise unchecked
    // here: merge mining borrows only the parent hash itself, compared
    // against the auxiliary chain's own required difficulty, not the parent
    // chain's difficulty bookkeeping.
    if (!CheckProofOfWork(parentBlock.GetHash(), nBitsAux, params)) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER,
                              "auxpow-pow-invalid",
                              "parent block hash does not satisfy the auxiliary chain's required target");
    }

    // A parent block that itself carries the AUXPOW version bit would mean
    // "merge-mining of merge-mining," which this design does not support and
    // must reject explicitly rather than silently mis-parse.
    if (IsAuxpowVersion(parentBlock.nVersion)) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER,
                              "auxpow-parent-is-auxpow",
                              "parent block itself has the AuxPoW version bit set");
    }

    if (coinbaseTx == nullptr || coinbaseTx->vin.empty()) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER,
                              "auxpow-no-coinbase", "AuxPoW coinbase transaction missing or empty");
    }

    // (2) Coinbase merkle branch must reproduce the parent block's own
    // merkle root, at the claimed index.
    const uint256 coinbaseHash = coinbaseTx->GetHash().ToUint256();
    const uint256 computedRoot = CheckMerkleBranch(coinbaseHash, vMerkleBranch, nIndex);
    if (computedRoot != parentBlock.hashMerkleRoot) {
        return state.Invalid(BlockValidationResult::BLOCK_MUTATED,
                              "auxpow-coinbase-merkle-mismatch",
                              "coinbase merkle branch does not reproduce parent block's merkle root");
    }
    // Real deployments always place the coinbase at index 0. Anything else is
    // not itself a cryptographic break (the branch check above already
    // proves inclusion at the claimed index), but it is never legitimate for
    // a real coinbase and is rejected as a sanity/anti-malleability measure,
    // matching Namecoin's own implementation.
    if (nIndex != 0) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER,
                              "auxpow-coinbase-index-nonzero",
                              "coinbase transaction must be at index 0");
    }

    // (3) Locate the merge-mining tag in the coinbase's first input's
    // scriptSig (the conventional, and only sane, place for it -- it must be
    // part of what the parent chain's own PoW commits to, which scriptSig
    // is, unlike e.g. a later output).
    uint256 commitment;
    uint32_t nTreeSize = 0;
    uint32_t nMergeMineNonce = 0;
    std::string tagErr;
    if (!FindMergeMiningTag(coinbaseTx->vin[0].scriptSig, commitment, nTreeSize, nMergeMineNonce, tagErr)) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER,
                              "auxpow-tag-invalid", tagErr);
    }

    // (6) Reject a maliciously huge claimed tree size before doing any
    // further work with it. A real merge-mining setup commits a handful of
    // chains at most; this is a generous but real ceiling, not an
    // arbitrary-precision allowance.
    static constexpr uint32_t MAX_CHAIN_MERKLE_TREE_SIZE = 1u << 20; // ~1M slots
    if (nTreeSize == 0 || nTreeSize > MAX_CHAIN_MERKLE_TREE_SIZE) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER,
                              "auxpow-tree-size-invalid",
                              "committed chain merkle tree size is zero or unreasonably large");
    }
    if (vChainMerkleBranch.size() > 32) { // 2^32 tree size would need at most 32 branch levels; anything more is malformed
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER,
                              "auxpow-chain-branch-too-long",
                              "chain merkle branch has an implausible number of levels");
    }

    // (4) The chain merkle branch, applied to our own claimed aux block
    // hash at nChainIndex, must reproduce the commitment read from the tag.
    const uint256 computedChainRoot = CheckMerkleBranch(hashAuxBlock, vChainMerkleBranch, nChainIndex);
    if (computedChainRoot != commitment) {
        return state.Invalid(BlockValidationResult::BLOCK_MUTATED,
                              "auxpow-chain-merkle-mismatch",
                              "chain merkle branch does not reproduce the coinbase's committed value");
    }

    // (5) Index-grinding defense: nChainIndex must equal the deterministic
    // expected slot, not merely be self-consistent with the branch above (a
    // self-consistent-but-wrong-slot branch is exactly what index grinding
    // would try to construct).
    const int expectedIndex = GetExpectedMerkleTreeIndex(nMergeMineNonce, nChainId, nTreeSize);
    if (nChainIndex != expectedIndex) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER,
                              "auxpow-wrong-chain-index",
                              "chain merkle index does not match the deterministic expected slot");
    }

    return true;
}

bool CheckAuxPowRules(int32_t nVersion, int nHeight, const uint256& hashHeader, uint32_t nBits,
                       const CAuxPow* auxpow, int32_t expectedChainId, int activationHeight,
                       const Consensus::Params& params, BlockValidationState& state)
{
    const bool isAuxpow = IsAuxpowVersion(nVersion);

    if (nHeight < activationHeight) {
        if (isAuxpow) {
            return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER,
                                  "auxpow-before-activation",
                                  "AuxPoW version bit set before the activation height");
        }
        // Pre-activation, non-AuxPoW: nothing further to check here; ordinary
        // CheckProofOfWork on the header's own hash applies as always.
        return true;
    }

    if (!isAuxpow) {
        // Post-activation, direct mining: still valid. AuxPoW is an
        // additional accepted proof format, not a replacement.
        return true;
    }

    const int32_t chainId = GetChainId(nVersion);
    if (chainId != expectedChainId) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER,
                              "auxpow-wrong-chain-id",
                              "AuxPoW version's chain ID does not match this chain's registered ID");
    }

    if (auxpow == nullptr) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER,
                              "auxpow-missing",
                              "AuxPoW version bit set but no AuxPoW proof was supplied");
    }

    return auxpow->Check(hashHeader, expectedChainId, nBits, params, state);
}
