// Copyright (c) 2026 The BitAIcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Real, end-to-end tests of the AuxPoW validation core (src/auxpow.h/.cpp):
// each test constructs an actual fake parent block (header + coinbase tx
// with a real merge-mining tag embedded in its scriptSig) and runs it
// through CAuxPow::Check(), rather than testing internal helpers in
// isolation -- the goal is to exercise the exact accept/reject decision a
// real node would make, matching the rigor requested for this milestone.

#include <auxpow.h>

#include <chainparams.h>
#include <consensus/validation.h>
#include <crypto/common.h>
#include <hash.h>
#include <primitives/transaction.h>
#include <pow.h>
#include <script/script.h>
#include <serialize.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(auxpow_tests, BasicTestingSetup)

namespace {

std::vector<unsigned char> BuildTagPayload(const uint256& commitment, uint32_t nTreeSize, uint32_t nNonce)
{
    std::vector<unsigned char> data(MERGE_MINING_HEADER, MERGE_MINING_HEADER + sizeof(MERGE_MINING_HEADER));
    data.insert(data.end(), commitment.begin(), commitment.end());
    unsigned char sizeBuf[4];
    WriteLE32(sizeBuf, nTreeSize);
    data.insert(data.end(), sizeBuf, sizeBuf + 4);
    unsigned char nonceBuf[4];
    WriteLE32(nonceBuf, nNonce);
    data.insert(data.end(), nonceBuf, nonceBuf + 4);
    return data;
}

CTransactionRef MakeCoinbase(const std::vector<unsigned char>& scriptSigBytes)
{
    CMutableTransaction tx;
    tx.version = 1;
    CTxIn in;
    in.prevout.SetNull();
    in.scriptSig = CScript(scriptSigBytes.begin(), scriptSigBytes.end());
    tx.vin.push_back(in);
    CTxOut out;
    out.nValue = 0;
    out.scriptPubKey = CScript();
    tx.vout.push_back(out);
    return MakeTransactionRef(std::move(tx));
}

/**
 * Builds a minimal, valid, single-transaction (coinbase-only) AuxPoW proof
 * for a single merge-mined chain (chain merkle tree size 1, so the
 * commitment must equal hashAuxBlock directly and any chain index passes the
 * expected-slot check trivially), mined against an easy target so the
 * parent's own hash-vs-target check passes within a handful of nonces.
 */
CAuxPow BuildValidAuxPow(const uint256& hashAuxBlock, uint32_t nBitsAux, const Consensus::Params& params)
{
    const uint32_t nTreeSize = 1;
    const uint32_t nNonce = 0xDEADBEEF;
    auto scriptBytes = BuildTagPayload(hashAuxBlock, nTreeSize, nNonce);
    CTransactionRef coinbaseTx = MakeCoinbase(scriptBytes);

    CBlockHeader parent;
    parent.nVersion = 1; // real parent chain's own header, no AUXPOW bit
    parent.hashPrevBlock.SetNull();
    parent.hashMerkleRoot = coinbaseTx->GetHash().ToUint256(); // single-tx block
    parent.nTime = 1700000000;
    parent.nBits = 0x1d0fffff; // parent chain's own claimed difficulty -- irrelevant to Check(), just well-formed
    parent.nNonce = 0;

    bool found = false;
    for (uint32_t n = 0; n < 1000000; ++n) {
        parent.nNonce = n;
        if (CheckProofOfWork(parent.GetHash(), nBitsAux, params)) {
            found = true;
            break;
        }
    }
    BOOST_REQUIRE_MESSAGE(found, "failed to find a parent nonce satisfying the easy test target");

    CAuxPow auxpow;
    auxpow.coinbaseTx = coinbaseTx;
    auxpow.vMerkleBranch.clear();
    auxpow.nIndex = 0;
    auxpow.vChainMerkleBranch.clear();
    auxpow.nChainIndex = 0;
    auxpow.parentBlock = parent;
    return auxpow;
}

const int32_t TEST_CHAIN_ID = 16969; // BitAIcoin's proposed AuxPoW chain ID
const uint32_t EASY_BITS = 0x207fffff; // trivially-satisfied target for test speed
const uint32_t IMPOSSIBLE_BITS = 0x03000001; // target == 1: no real hash satisfies this

uint256 TestAuxBlockHash()
{
    return uint256{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
}

} // namespace

BOOST_AUTO_TEST_CASE(auxpow_accepts_valid_proof)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    const uint256 hashAuxBlock = TestAuxBlockHash();

    CAuxPow auxpow = BuildValidAuxPow(hashAuxBlock, EASY_BITS, params);
    BlockValidationState state;
    BOOST_CHECK(auxpow.Check(hashAuxBlock, TEST_CHAIN_ID, EASY_BITS, params, state));
    BOOST_CHECK(state.IsValid());
}

BOOST_AUTO_TEST_CASE(auxpow_rejects_insufficient_parent_pow)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    const uint256 hashAuxBlock = TestAuxBlockHash();

    // Build a valid proof against the EASY target, then re-check it against
    // an effectively-impossible target -- the parent hash that satisfied
    // EASY_BITS will not satisfy IMPOSSIBLE_BITS.
    CAuxPow auxpow = BuildValidAuxPow(hashAuxBlock, EASY_BITS, params);
    BlockValidationState state;
    BOOST_CHECK(!auxpow.Check(hashAuxBlock, TEST_CHAIN_ID, IMPOSSIBLE_BITS, params, state));
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "auxpow-pow-invalid");
}

BOOST_AUTO_TEST_CASE(auxpow_rejects_missing_tag)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    const uint256 hashAuxBlock = TestAuxBlockHash();

    CAuxPow auxpow = BuildValidAuxPow(hashAuxBlock, EASY_BITS, params);
    // Replace the coinbase with one whose scriptSig has no merge-mining tag,
    // then fix up the parent's merkle root to still match (so this test
    // isolates the tag-search failure, not an incidental merkle mismatch).
    CTransactionRef plainCoinbase = MakeCoinbase(std::vector<unsigned char>{0x51, 0x52, 0x53});
    auxpow.coinbaseTx = plainCoinbase;
    auxpow.parentBlock.hashMerkleRoot = plainCoinbase->GetHash().ToUint256();
    // Re-mine the parent so its hash still satisfies EASY_BITS with the new merkle root.
    bool found = false;
    for (uint32_t n = 0; n < 1000000; ++n) {
        auxpow.parentBlock.nNonce = n;
        if (CheckProofOfWork(auxpow.parentBlock.GetHash(), EASY_BITS, params)) { found = true; break; }
    }
    BOOST_REQUIRE(found);

    BlockValidationState state;
    BOOST_CHECK(!auxpow.Check(hashAuxBlock, TEST_CHAIN_ID, EASY_BITS, params, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "auxpow-tag-invalid");
}

BOOST_AUTO_TEST_CASE(auxpow_rejects_multiple_tags_ambiguous)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    const uint256 hashAuxBlock = TestAuxBlockHash();

    auto payload = BuildTagPayload(hashAuxBlock, 1, 0xDEADBEEF);
    std::vector<unsigned char> doubled = payload;
    doubled.insert(doubled.end(), payload.begin(), payload.end()); // tag appears twice
    CTransactionRef coinbaseTx = MakeCoinbase(doubled);

    CBlockHeader parent;
    parent.nVersion = 1;
    parent.hashPrevBlock.SetNull();
    parent.hashMerkleRoot = coinbaseTx->GetHash().ToUint256();
    parent.nTime = 1700000000;
    parent.nBits = 0x1d0fffff;
    bool found = false;
    for (uint32_t n = 0; n < 1000000; ++n) {
        parent.nNonce = n;
        if (CheckProofOfWork(parent.GetHash(), EASY_BITS, params)) { found = true; break; }
    }
    BOOST_REQUIRE(found);

    CAuxPow auxpow;
    auxpow.coinbaseTx = coinbaseTx;
    auxpow.parentBlock = parent;

    BlockValidationState state;
    BOOST_CHECK(!auxpow.Check(hashAuxBlock, TEST_CHAIN_ID, EASY_BITS, params, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "auxpow-tag-invalid");
}

BOOST_AUTO_TEST_CASE(auxpow_rejects_wrong_aux_hash_chain_merkle_mismatch)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    const uint256 hashAuxBlock = TestAuxBlockHash();
    const uint256 wrongAuxBlock = uint256{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};

    // Proof commits to hashAuxBlock, but we ask Check() to validate it for a
    // DIFFERENT aux block hash -- must fail at the chain-merkle-branch step.
    CAuxPow auxpow = BuildValidAuxPow(hashAuxBlock, EASY_BITS, params);
    BlockValidationState state;
    BOOST_CHECK(!auxpow.Check(wrongAuxBlock, TEST_CHAIN_ID, EASY_BITS, params, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "auxpow-chain-merkle-mismatch");
}

BOOST_AUTO_TEST_CASE(auxpow_rejects_coinbase_merkle_mismatch)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    const uint256 hashAuxBlock = TestAuxBlockHash();

    CAuxPow auxpow = BuildValidAuxPow(hashAuxBlock, EASY_BITS, params);
    // Tamper with the parent's committed merkle root after the coinbase was
    // fixed -- this must be caught even though the parent's own PoW (which
    // covers the whole header including hashMerkleRoot) would also normally
    // have already broken; to isolate this specific check, re-mine a fresh
    // valid PoW on top of the tampered root so ONLY the merkle check can fail.
    auxpow.parentBlock.hashMerkleRoot = uint256{"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"};
    bool found = false;
    for (uint32_t n = 0; n < 1000000; ++n) {
        auxpow.parentBlock.nNonce = n;
        if (CheckProofOfWork(auxpow.parentBlock.GetHash(), EASY_BITS, params)) { found = true; break; }
    }
    BOOST_REQUIRE(found);

    BlockValidationState state;
    BOOST_CHECK(!auxpow.Check(hashAuxBlock, TEST_CHAIN_ID, EASY_BITS, params, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "auxpow-coinbase-merkle-mismatch");
}

BOOST_AUTO_TEST_CASE(auxpow_rejects_parent_with_auxpow_bit_set)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    const uint256 hashAuxBlock = TestAuxBlockHash();

    CAuxPow auxpow = BuildValidAuxPow(hashAuxBlock, EASY_BITS, params);
    auxpow.parentBlock.nVersion = MakeAuxpowVersion(1, 1); // pretend parent is itself merge-mined
    // Re-mine since changing nVersion changes the header hash.
    bool found = false;
    for (uint32_t n = 0; n < 1000000; ++n) {
        auxpow.parentBlock.nNonce = n;
        if (CheckProofOfWork(auxpow.parentBlock.GetHash(), EASY_BITS, params)) { found = true; break; }
    }
    BOOST_REQUIRE(found);

    BlockValidationState state;
    BOOST_CHECK(!auxpow.Check(hashAuxBlock, TEST_CHAIN_ID, EASY_BITS, params, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "auxpow-parent-is-auxpow");
}

BOOST_AUTO_TEST_CASE(auxpow_rejects_wrong_chain_index_grinding_attempt)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    const uint256 hashAuxBlock = TestAuxBlockHash();

    // Tree size 4: exactly one slot (0..3) is the deterministically-expected
    // one for a given (nonce, chainId). Build the chain merkle tree with
    // hashAuxBlock at the WRONG slot relative to what it's claimed to be at,
    // simulating an attempt to use a proof that's internally self-consistent
    // (the branch really does reproduce the commitment at nChainIndex) but
    // does not sit at the index the deterministic formula requires.
    const uint32_t nTreeSize = 4;
    const uint32_t nNonce = 12345;
    const int expected = GetExpectedMerkleTreeIndex(nNonce, TEST_CHAIN_ID, nTreeSize);
    const int wrongIndex = (expected + 1) % nTreeSize;

    // Build a 4-leaf tree where leaf[wrongIndex] = hashAuxBlock and the other
    // three leaves are arbitrary filler, then compute its real root and a
    // real branch for position wrongIndex -- so the branch legitimately
    // reproduces the root (this is not a malformed-branch test; it's an
    // out-of-position-per-the-formula test).
    std::vector<uint256> leaves = {
        uint256{"0101010101010101010101010101010101010101010101010101010101010101"},
        uint256{"0202020202020202020202020202020202020202020202020202020202020202"},
        uint256{"0303030303030303030303030303030303030303030303030303030303030303"},
        uint256{"0404040404040404040404040404040404040404040404040404040404040404"},
    };
    leaves[wrongIndex] = hashAuxBlock;
    // level 0 -> level 1 (2 nodes)
    HashWriter h01{};
    h01 << leaves[0] << leaves[1];
    uint256 n01 = h01.GetHash();
    HashWriter h23{};
    h23 << leaves[2] << leaves[3];
    uint256 n23 = h23.GetHash();
    HashWriter hRoot{};
    hRoot << n01 << n23;
    uint256 root = hRoot.GetHash();

    std::vector<uint256> branch;
    if (wrongIndex == 0) branch = {leaves[1], n23};
    else if (wrongIndex == 1) branch = {leaves[0], n23};
    else if (wrongIndex == 2) branch = {leaves[3], n01};
    else branch = {leaves[2], n01};

    // Sanity: our own hand-built branch really does reproduce `root` (this
    // is testing our test fixture's own correctness, not auxpow.cpp).
    // uint256 has no operator<< for BOOST_CHECK_EQUAL's failure-message
    // printer, so compare via GetHex() strings instead.
    BOOST_REQUIRE_EQUAL(CheckMerkleBranch(hashAuxBlock, branch, wrongIndex).GetHex(), root.GetHex());

    auto scriptBytes = BuildTagPayload(root, nTreeSize, nNonce);
    CTransactionRef coinbaseTx = MakeCoinbase(scriptBytes);

    CBlockHeader parent;
    parent.nVersion = 1;
    parent.hashPrevBlock.SetNull();
    parent.hashMerkleRoot = coinbaseTx->GetHash().ToUint256();
    parent.nTime = 1700000000;
    parent.nBits = 0x1d0fffff;
    bool found = false;
    for (uint32_t n = 0; n < 1000000; ++n) {
        parent.nNonce = n;
        if (CheckProofOfWork(parent.GetHash(), EASY_BITS, params)) { found = true; break; }
    }
    BOOST_REQUIRE(found);

    CAuxPow auxpow;
    auxpow.coinbaseTx = coinbaseTx;
    auxpow.nIndex = 0;
    auxpow.vChainMerkleBranch = branch;
    auxpow.nChainIndex = wrongIndex; // internally consistent with `branch`, but not the expected slot
    auxpow.parentBlock = parent;

    BlockValidationState state;
    BOOST_CHECK(!auxpow.Check(hashAuxBlock, TEST_CHAIN_ID, EASY_BITS, params, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "auxpow-wrong-chain-index");
}

BOOST_AUTO_TEST_CASE(get_expected_merkle_tree_index_is_deterministic_and_in_range)
{
    for (uint32_t nonce : {0u, 1u, 12345u, 0xFFFFFFFFu}) {
        for (int32_t chainId : {1, 98, 16969}) {
            for (unsigned treeSize : {1u, 2u, 4u, 1000u}) {
                int idx = GetExpectedMerkleTreeIndex(nonce, chainId, treeSize);
                BOOST_CHECK(idx >= 0);
                BOOST_CHECK(static_cast<unsigned>(idx) < treeSize);
                // determinism: same inputs, same output
                BOOST_CHECK_EQUAL(idx, GetExpectedMerkleTreeIndex(nonce, chainId, treeSize));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(version_bit_helpers_roundtrip)
{
    BOOST_CHECK(!IsAuxpowVersion(1));
    BOOST_CHECK(IsAuxpowVersion(VERSION_AUXPOW | 1));
    int32_t v = MakeAuxpowVersion(16969, 4);
    BOOST_CHECK(IsAuxpowVersion(v));
    BOOST_CHECK_EQUAL(GetChainId(v), 16969);
    BOOST_CHECK_EQUAL(GetBaseVersion(v), 4);
}

BOOST_AUTO_TEST_SUITE_END()
