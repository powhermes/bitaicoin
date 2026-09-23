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
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <pow.h>
#include <script/script.h>
#include <serialize.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <util/strencodings.h>

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

// --- CheckAuxPowRules: the standalone height-gate/version-rule tests ---
// (auxpow itself not yet wired into CBlockHeader/ContextualCheckBlockHeader --
// see the design-hazard comment on CheckAuxPowRules's declaration in
// auxpow.h -- these test the extracted logic directly against explicit
// parameters, exactly as it will be called once that wiring lands.)

namespace {
const int32_t TEST_ACTIVATION_HEIGHT = 227808;
} // namespace

BOOST_AUTO_TEST_CASE(auxpow_rules_reject_auxpow_bit_before_activation)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    BlockValidationState state;
    int32_t v = MakeAuxpowVersion(TEST_CHAIN_ID, 1);
    bool ok = CheckAuxPowRules(v, TEST_ACTIVATION_HEIGHT - 1, TestAuxBlockHash(), EASY_BITS,
                               nullptr, TEST_CHAIN_ID, TEST_ACTIVATION_HEIGHT, params, state);
    BOOST_CHECK(!ok);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "auxpow-before-activation");
}

BOOST_AUTO_TEST_CASE(auxpow_rules_allow_direct_mining_before_activation)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    BlockValidationState state;
    bool ok = CheckAuxPowRules(1 /* plain version, no AUXPOW bit */, TEST_ACTIVATION_HEIGHT - 1,
                               TestAuxBlockHash(), EASY_BITS, nullptr, TEST_CHAIN_ID,
                               TEST_ACTIVATION_HEIGHT, params, state);
    BOOST_CHECK(ok);
    BOOST_CHECK(state.IsValid());
}

BOOST_AUTO_TEST_CASE(auxpow_rules_allow_direct_mining_after_activation)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    BlockValidationState state;
    bool ok = CheckAuxPowRules(1 /* plain version, no AUXPOW bit */, TEST_ACTIVATION_HEIGHT,
                               TestAuxBlockHash(), EASY_BITS, nullptr, TEST_CHAIN_ID,
                               TEST_ACTIVATION_HEIGHT, params, state);
    BOOST_CHECK(ok);
    BOOST_CHECK(state.IsValid());
}

BOOST_AUTO_TEST_CASE(auxpow_rules_reject_missing_proof_after_activation)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    BlockValidationState state;
    int32_t v = MakeAuxpowVersion(TEST_CHAIN_ID, 1);
    bool ok = CheckAuxPowRules(v, TEST_ACTIVATION_HEIGHT, TestAuxBlockHash(), EASY_BITS,
                               nullptr /* no proof supplied */, TEST_CHAIN_ID,
                               TEST_ACTIVATION_HEIGHT, params, state);
    BOOST_CHECK(!ok);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "auxpow-missing");
}

BOOST_AUTO_TEST_CASE(auxpow_rules_reject_wrong_chain_id_in_version)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    BlockValidationState state;
    int32_t v = MakeAuxpowVersion(98 /* Dogecoin's real chain ID, not ours */, 1);
    CAuxPow auxpow = BuildValidAuxPow(TestAuxBlockHash(), EASY_BITS, params);
    bool ok = CheckAuxPowRules(v, TEST_ACTIVATION_HEIGHT, TestAuxBlockHash(), EASY_BITS,
                               &auxpow, TEST_CHAIN_ID, TEST_ACTIVATION_HEIGHT, params, state);
    BOOST_CHECK(!ok);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "auxpow-wrong-chain-id");
}

BOOST_AUTO_TEST_CASE(auxpow_rules_accept_valid_proof_after_activation)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    const uint256 hashAuxBlock = TestAuxBlockHash();
    BlockValidationState state;
    int32_t v = MakeAuxpowVersion(TEST_CHAIN_ID, 1);
    CAuxPow auxpow = BuildValidAuxPow(hashAuxBlock, EASY_BITS, params);
    bool ok = CheckAuxPowRules(v, TEST_ACTIVATION_HEIGHT, hashAuxBlock, EASY_BITS,
                               &auxpow, TEST_CHAIN_ID, TEST_ACTIVATION_HEIGHT, params, state);
    BOOST_CHECK(ok);
    BOOST_CHECK(state.IsValid());
}

BOOST_AUTO_TEST_CASE(auxpow_rules_at_exact_activation_boundary)
{
    // Off-by-one sanity: the block AT activationHeight is post-activation
    // (>=), the block immediately before is pre-activation.
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    const uint256 hashAuxBlock = TestAuxBlockHash();
    int32_t v = MakeAuxpowVersion(TEST_CHAIN_ID, 1);
    CAuxPow auxpow = BuildValidAuxPow(hashAuxBlock, EASY_BITS, params);

    BlockValidationState stateAt;
    BOOST_CHECK(CheckAuxPowRules(v, TEST_ACTIVATION_HEIGHT, hashAuxBlock, EASY_BITS, &auxpow,
                                 TEST_CHAIN_ID, TEST_ACTIVATION_HEIGHT, params, stateAt));

    BlockValidationState stateBefore;
    BOOST_CHECK(!CheckAuxPowRules(v, TEST_ACTIVATION_HEIGHT - 1, hashAuxBlock, EASY_BITS, &auxpow,
                                  TEST_CHAIN_ID, TEST_ACTIVATION_HEIGHT, params, stateBefore));
    BOOST_CHECK_EQUAL(stateBefore.GetRejectReason(), "auxpow-before-activation");
}

// --- CBlockHeader/AuxPoW storage and serialization slice ---
// (SerializeBlockHeaderWithAuxPow / UnserializeBlockHeaderWithAuxPow,
// CBlockHeader::auxpow / IsAuxpow() -- see design-hazard comments in
// primitives/block.h and auxpow.h for why these are a separate path from
// CBlockHeader's own generic Serialize/GetHash().)

namespace {

// Builds a syntactically well-formed header with a REAL, Check()-valid
// attached AuxPoW proof (reusing BuildValidAuxPow from earlier in this
// file), for serialization round-trip and hash-invariance testing.
CBlockHeader MakeHeaderWithAuxpow(int32_t chainId, const Consensus::Params& params)
{
    CBlockHeader h;
    h.nVersion = MakeAuxpowVersion(chainId, 1);
    h.hashPrevBlock = uint256{"1111111111111111111111111111111111111111111111111111111111111111"};
    h.hashMerkleRoot = uint256{"2222222222222222222222222222222222222222222222222222222222222222"};
    h.nTime = 1700000000;
    h.nBits = EASY_BITS;
    h.nNonce = 42;
    h.auxpow = std::make_shared<CAuxPow>(BuildValidAuxPow(h.GetHash(), EASY_BITS, params));
    return h;
}

} // namespace

BOOST_AUTO_TEST_CASE(header_hash_covers_only_base_fields_not_auxpow)
{
    // Two headers, identical base fields, DIFFERENT auxpow payloads (one has
    // none at all) -- must hash identically. This is the exact property the
    // design-hazard comments in primitives/block.h and auxpow.h exist to
    // protect; test it directly rather than just asserting it in a comment.
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();

    CBlockHeader plain;
    plain.nVersion = MakeAuxpowVersion(TEST_CHAIN_ID, 1);
    plain.hashPrevBlock = uint256{"1111111111111111111111111111111111111111111111111111111111111111"};
    plain.hashMerkleRoot = uint256{"2222222222222222222222222222222222222222222222222222222222222222"};
    plain.nTime = 1700000000;
    plain.nBits = EASY_BITS;
    plain.nNonce = 42;
    BOOST_CHECK(plain.auxpow == nullptr);
    const uint256 hashNoProof = plain.GetHash();

    CBlockHeader withProof = plain;
    withProof.auxpow = std::make_shared<CAuxPow>(BuildValidAuxPow(hashNoProof, EASY_BITS, params));
    BOOST_CHECK_EQUAL(withProof.GetHash().GetHex(), hashNoProof.GetHex());

    // A second, DIFFERENT valid proof (different coinbase nonce -> different
    // coinbase tx -> different merkle root inside the proof, but the proof
    // is a completely separate object from the header) must produce the
    // exact same header hash too -- the hash cannot depend on which proof,
    // or whether any proof, is attached.
    CAuxPow otherProof = BuildValidAuxPow(hashNoProof, EASY_BITS, params);
    // Force it to be a genuinely different serialized object (different
    // coinbase) while still committing to the same hashNoProof, by using a
    // different chain ID in the same BuildValidAuxPow call is not possible
    // (BuildValidAuxPow always uses TEST_CHAIN_ID internally for the trivial
    // tree-size-1 case, which doesn't affect coinbase content) -- instead
    // directly perturb the AuxPoW's own irrelevant-to-header-hash internals:
    otherProof.parentBlock.nTime += 1; // changes the proof's own internals only
    CBlockHeader withOtherProof = plain;
    withOtherProof.auxpow = std::make_shared<CAuxPow>(otherProof);
    BOOST_CHECK_EQUAL(withOtherProof.GetHash().GetHex(), hashNoProof.GetHex());
}

BOOST_AUTO_TEST_CASE(header_hash_changes_with_any_base_field)
{
    CBlockHeader base;
    base.nVersion = 1;
    base.hashPrevBlock = uint256{"1111111111111111111111111111111111111111111111111111111111111111"};
    base.hashMerkleRoot = uint256{"2222222222222222222222222222222222222222222222222222222222222222"};
    base.nTime = 1700000000;
    base.nBits = 0x1d0fffff;
    base.nNonce = 42;
    const uint256 baseHash = base.GetHash();

    auto mutated = [&](auto mutator) {
        CBlockHeader h = base;
        mutator(h);
        return h.GetHash();
    };
    BOOST_CHECK(mutated([](CBlockHeader& h) { h.nVersion += 1; }) != baseHash);
    BOOST_CHECK(mutated([](CBlockHeader& h) { h.hashPrevBlock = uint256{"3333333333333333333333333333333333333333333333333333333333333333"}; }) != baseHash);
    BOOST_CHECK(mutated([](CBlockHeader& h) { h.hashMerkleRoot = uint256{"4444444444444444444444444444444444444444444444444444444444444444"}; }) != baseHash);
    BOOST_CHECK(mutated([](CBlockHeader& h) { h.nTime += 1; }) != baseHash);
    BOOST_CHECK(mutated([](CBlockHeader& h) { h.nBits += 1; }) != baseHash);
    BOOST_CHECK(mutated([](CBlockHeader& h) { h.nNonce += 1; }) != baseHash);
}

BOOST_AUTO_TEST_CASE(real_existing_block_hash_unaffected_by_auxpow_addition)
{
    // A REAL header from BitAIcoin's actual live chain (tip at the time of
    // this slice, fetched via `bitaicoin-cli getblockheader ... false` for
    // the raw hex, and the verbose form for the known hash), height 225823 --
    // well within the untouched 225430-225823 range this milestone commits
    // to never altering. Reconstructs the header from its exact raw field
    // values and confirms GetHash() still reproduces the exact known hash
    // after this slice's changes to CBlockHeader -- a real, concrete proof
    // that existing history's hashes are unaffected, not just an assertion.
    CBlockHeader h;
    h.nVersion = 536870912;
    h.hashPrevBlock = uint256{"0000000365e377e69ba48e4d404467bf717eb92e5ee00abdadc09a072a817774"};
    h.hashMerkleRoot = uint256{"ff7709be324cf2d64beae5eff8c667afa820c879d800a1e97a61e9a5ce2608e1"};
    h.nTime = 1789815555;
    h.nBits = 0x1d0fffff;
    h.nNonce = 118543244;
    BOOST_CHECK(h.auxpow == nullptr);
    BOOST_CHECK(!h.IsAuxpow());

    const uint256 expectedHash{"0000000ad1060dd6b63a31796c4d57977c7f009eb0229b32d0b15dd303ecff57"};
    BOOST_CHECK_EQUAL(h.GetHash().GetHex(), expectedHash.GetHex());

    // Also confirm the plain (unchanged) CBlockHeader serializer reproduces
    // the real raw wire bytes exactly, byte for byte.
    DataStream ss;
    ss << h;
    const std::string expectedHex =
        "000000207477812a079ac0adbd0ae05e2eb97e71bf6744404d8ea49be677e36503000000"
        "e10826cea5e9617ae9a100d879c820a8af67c6f8efe5ea4bd6f24c32be0977ff036bae6a"
        "ffff0f1d8cd31007";
    BOOST_CHECK_EQUAL(HexStr(ss), expectedHex);
}

BOOST_AUTO_TEST_CASE(serialize_roundtrip_no_auxpow_matches_plain_header_serialize)
{
    // A header WITHOUT the AUXPOW bit set, run through the NEW auxpow-aware
    // serialize/deserialize functions, must produce byte-for-byte identical
    // output to the plain (unchanged) CBlockHeader serializer -- this is the
    // "old/pre-activation blocks remain byte-for-byte compatible" property,
    // tested directly rather than just reasoned about.
    CBlockHeader h;
    h.nVersion = 536870912; // real, plain, non-AuxPoW version from the live chain
    h.hashPrevBlock = uint256{"0000000365e377e69ba48e4d404467bf717eb92e5ee00abdadc09a072a817774"};
    h.hashMerkleRoot = uint256{"ff7709be324cf2d64beae5eff8c667afa820c879d800a1e97a61e9a5ce2608e1"};
    h.nTime = 1789815555;
    h.nBits = 0x1d0fffff;
    h.nNonce = 118543244;
    BOOST_CHECK(!h.IsAuxpow());

    DataStream plainStream;
    plainStream << h; // plain CBlockHeader::SERIALIZE_METHODS, unchanged

    DataStream auxpowAwareStream;
    SerializeBlockHeaderWithAuxPow(h, auxpowAwareStream);

    BOOST_CHECK_EQUAL(HexStr(plainStream), HexStr(auxpowAwareStream));

    // Round trip through the new deserializer and back.
    CBlockHeader h2;
    UnserializeBlockHeaderWithAuxPow(h2, auxpowAwareStream);
    BOOST_CHECK(h2.auxpow == nullptr);
    BOOST_CHECK_EQUAL(h2.GetHash().GetHex(), h.GetHash().GetHex());

    DataStream reserialized;
    SerializeBlockHeaderWithAuxPow(h2, reserialized);
    BOOST_CHECK_EQUAL(HexStr(reserialized), HexStr(plainStream));
}

BOOST_AUTO_TEST_CASE(serialize_roundtrip_with_auxpow_network_style)
{
    // "Network/P2P-style" round trip: DataStream is the same generic
    // in-memory buffer type this codebase's own P2P message (de)serialization
    // is built on.
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    CBlockHeader h = MakeHeaderWithAuxpow(TEST_CHAIN_ID, params);
    BOOST_CHECK(h.IsAuxpow());
    BOOST_REQUIRE(h.auxpow != nullptr);

    DataStream ss;
    SerializeBlockHeaderWithAuxPow(h, ss);
    const std::string originalHex = HexStr(ss); // captured before Unserialize consumes the read position

    CBlockHeader h2;
    UnserializeBlockHeaderWithAuxPow(h2, ss);
    BOOST_CHECK(h2.IsAuxpow());
    BOOST_REQUIRE(h2.auxpow != nullptr);
    BOOST_CHECK_EQUAL(h2.GetHash().GetHex(), h.GetHash().GetHex());
    BOOST_CHECK_EQUAL(h2.nVersion, h.nVersion);
    BOOST_CHECK_EQUAL(h2.auxpow->nIndex, h.auxpow->nIndex);
    BOOST_CHECK_EQUAL(h2.auxpow->nChainIndex, h.auxpow->nChainIndex);
    BOOST_CHECK(h2.auxpow->parentBlock.GetHash() == h.auxpow->parentBlock.GetHash());

    // Serialize->deserialize->serialize: byte-identical the second time.
    DataStream ss2;
    SerializeBlockHeaderWithAuxPow(h2, ss2);
    BOOST_CHECK_EQUAL(HexStr(ss2), originalHex);
}

BOOST_AUTO_TEST_CASE(serialize_roundtrip_with_auxpow_disk_style)
{
    // "Disk-style" round trip: a REAL file on disk via AutoFile, not just an
    // in-memory buffer -- exercises genuinely different I/O plumbing than
    // the network-style test above, per explicit instruction to test both.
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    CBlockHeader h = MakeHeaderWithAuxpow(TEST_CHAIN_ID, params);

    const fs::path path{m_args.GetDataDirBase() / "test_auxpow_header.bin"};
    {
        AutoFile fileOut{fsbridge::fopen(path, "wb")};
        SerializeBlockHeaderWithAuxPow(h, fileOut);
        BOOST_CHECK_EQUAL(fileOut.fclose(), 0);
    }

    CBlockHeader h2;
    {
        AutoFile fileIn{fsbridge::fopen(path, "rb")};
        UnserializeBlockHeaderWithAuxPow(h2, fileIn);
        BOOST_CHECK_EQUAL(fileIn.fclose(), 0);
    }

    BOOST_CHECK(h2.IsAuxpow());
    BOOST_REQUIRE(h2.auxpow != nullptr);
    BOOST_CHECK_EQUAL(h2.GetHash().GetHex(), h.GetHash().GetHex());
    BOOST_CHECK(h2.auxpow->parentBlock.GetHash() == h.auxpow->parentBlock.GetHash());
    fs::remove(path);
}

BOOST_AUTO_TEST_CASE(auxpow_bit_set_but_proof_missing_fails_to_serialize)
{
    // A header claiming AuxPoW (bit set) but with a null auxpow pointer must
    // fail loudly on serialize, not silently write a truncated/malformed
    // stream that a peer would then have to reject after the fact.
    CBlockHeader h;
    h.nVersion = MakeAuxpowVersion(TEST_CHAIN_ID, 1);
    h.hashPrevBlock.SetNull();
    h.hashMerkleRoot.SetNull();
    h.nTime = 1;
    h.nBits = EASY_BITS;
    h.nNonce = 0;
    BOOST_CHECK(h.auxpow == nullptr);

    DataStream ss;
    BOOST_CHECK_EXCEPTION(SerializeBlockHeaderWithAuxPow(h, ss), std::ios_base::failure,
                           [](const std::ios_base::failure& e) { return std::string(e.what()).find("no auxpow proof attached") != std::string::npos; });
}

BOOST_AUTO_TEST_CASE(deserialize_rejects_truncated_auxpow_stream)
{
    // AUXPOW bit set in the version, but the stream ends right after the six
    // base fields with no auxpow payload at all -- must fail cleanly (throw),
    // not read out-of-bounds or default-construct a bogus "valid" proof.
    DataStream ss;
    int32_t version = MakeAuxpowVersion(TEST_CHAIN_ID, 1);
    uint256 prev; prev.SetNull();
    uint256 merkle; merkle.SetNull();
    uint32_t time = 1, bits = EASY_BITS, nonce = 0;
    ss << version << prev << merkle << time << bits << nonce; // no auxpow payload follows

    CBlockHeader h2;
    BOOST_CHECK_THROW(UnserializeBlockHeaderWithAuxPow(h2, ss), std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(deserialize_rejects_oversized_merkle_branch_claim)
{
    // Malformed-data rejection: a coinbase transaction whose merkle branch
    // vector claims an implausible length (well past MAX_MERKLE_BRANCH_LENGTH)
    // must be rejected at deserialization, before any hashing is attempted.
    // Hand-builds the stream rather than going through CAuxPow's normal
    // serializer, since a normal serializer would never produce this.
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    CBlockHeader h = MakeHeaderWithAuxpow(TEST_CHAIN_ID, params);
    BOOST_REQUIRE(h.auxpow != nullptr);
    h.auxpow->vChainMerkleBranch.assign(MAX_MERKLE_BRANCH_LENGTH + 1, uint256{});

    DataStream ss;
    // Can't use SerializeBlockHeaderWithAuxPow directly since CAuxPow's own
    // SERIALIZE_METHODS has no length cap of its own (the cap is enforced by
    // the caller, UnserializeBlockHeaderWithAuxPow) -- serialize the base
    // fields plus the (oversized) auxpow manually to construct the exact
    // malformed-on-the-wire scenario being tested.
    ss << h.nVersion << h.hashPrevBlock << h.hashMerkleRoot << h.nTime << h.nBits << h.nNonce;
    ss << *h.auxpow;

    CBlockHeader h2;
    BOOST_CHECK_EXCEPTION(UnserializeBlockHeaderWithAuxPow(h2, ss), std::ios_base::failure,
                           [](const std::ios_base::failure& e) { return std::string(e.what()).find("implausibly long") != std::string::npos; });
}

BOOST_AUTO_TEST_CASE(direct_mining_header_serializes_without_auxpow_both_sides_of_activation)
{
    // A direct-mined (non-AuxPoW) header serializes/deserializes identically
    // via the new auxpow-aware functions regardless of height relative to
    // 227808 -- the serialization layer itself is height-agnostic (height-
    // gating is CheckAuxPowRules()'s job, tested separately above); this
    // confirms the wire format doesn't accidentally require or assume a
    // height parameter it was never given.
    CBlockHeader h;
    h.nVersion = 536870912;
    h.hashPrevBlock.SetNull();
    h.hashMerkleRoot.SetNull();
    h.nTime = 1;
    h.nBits = 0x1d0fffff;
    h.nNonce = 0;
    BOOST_CHECK(!h.IsAuxpow());

    DataStream ss;
    SerializeBlockHeaderWithAuxPow(h, ss);
    CBlockHeader h2;
    UnserializeBlockHeaderWithAuxPow(h2, ss);
    BOOST_CHECK(h2.auxpow == nullptr);
    BOOST_CHECK_EQUAL(h2.GetHash().GetHex(), h.GetHash().GetHex());
}

BOOST_AUTO_TEST_SUITE_END()
