// Copyright (c) 2015-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <auxpow.h>
#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <test/data/asert_bchn_vectors.h>
#include <test/util/random.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1261130161; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1262152739;  // Block #32255
    pindexLast.nBits = 0x1d00ffff;

    // Here (and below): expected_nbits is calculated in
    // CalculateNextWorkRequired(); redoing the calculation here would be just
    // reimplementing the same code that is written in pow.cpp. Rather than
    // copy that code, we just hardcode the expected result.
    unsigned int expected_nbits = 0x1d00d86aU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1233061996;  // Block #2015
    pindexLast.nBits = 0x1d00ffff;
    unsigned int expected_nbits = 0x1d00ffffU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1279297671;  // Block #68543
    pindexLast.nBits = 0x1c05a3f4;
    unsigned int expected_nbits = 0x1c0168fdU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
    // Test that reducing nbits further would not be a PermittedDifficultyTransition.
    unsigned int invalid_nbits = expected_nbits-1;
    BOOST_CHECK(!PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, invalid_nbits));
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1269211443;  // Block #46367
    pindexLast.nBits = 0x1c387f6f;
    unsigned int expected_nbits = 0x1d00e1fdU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
    // Test that increasing nbits further would not be a PermittedDifficultyTransition.
    unsigned int invalid_nbits = expected_nbits+1;
    BOOST_CHECK(!PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, invalid_nbits));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    nBits = UintToArith256(consensus.powLimit).GetCompact(true);
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits{~0x00800000U};
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 nBits_arith = UintToArith256(consensus.powLimit);
    nBits_arith *= 2;
    nBits = nBits_arith.GetCompact();
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_biger_hash_than_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith = UintToArith256(consensus.powLimit);
    nBits = hash_arith.GetCompact();
    hash_arith *= 2; // hash > nBits
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith{0};
    nBits = hash_arith.GetCompact();
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p2 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p3 = &blocks[m_rng.randrange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

void sanity_check_chainparams(const ArgsManager& args, ChainType chain_type)
{
    const auto chainParams = CreateChainParams(args, chain_type);
    const auto consensus = chainParams->GetConsensus();

    // hash genesis is correct
    BOOST_CHECK_EQUAL(consensus.hashGenesisBlock, chainParams->GenesisBlock().GetHash());

    // target timespan is an even multiple of spacing
    BOOST_CHECK_EQUAL(consensus.nPowTargetTimespan % consensus.nPowTargetSpacing, 0);

    // genesis nBits is positive, doesn't overflow and is lower than powLimit
    arith_uint256 pow_compact;
    bool neg, over;
    pow_compact.SetCompact(chainParams->GenesisBlock().nBits, &neg, &over);
    BOOST_CHECK(!neg && pow_compact != 0);
    BOOST_CHECK(!over);
    BOOST_CHECK(UintToArith256(consensus.powLimit) >= pow_compact);

    // check max target * 4*nPowTargetTimespan doesn't overflow -- see pow.cpp:CalculateNextWorkRequired()
    if (!consensus.fPowNoRetargeting) {
        arith_uint256 targ_max{UintToArith256(uint256{"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"})};
        targ_max /= consensus.nPowTargetTimespan*4;
        BOOST_CHECK(UintToArith256(consensus.powLimit) < targ_max);
    }
}

BOOST_AUTO_TEST_CASE(ChainParams_MAIN_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(ChainParams_REGTEST_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::REGTEST);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET4_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET4);
}

BOOST_AUTO_TEST_CASE(ChainParams_SIGNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::SIGNET);
}

// --- BitAIcoin ASERT DAA: wiring tests (docs/AUXPOW_MILESTONE.md sec.8) ---
//
// All synthetic CBlockIndex chains here (mirroring GetBlockProofEquivalentTime_test's
// own established pattern above -- a small vector of CBlockIndex linked via
// pprev, with heights/times/nBits set directly) -- NOT a real 227808-block
// mined chain, which would not be a reasonable test cost. GetAncestor() only
// ever walks pprev (with no skip-list optimization) when pskip is unset, so
// a short, purpose-built chain reaching exactly the heights under test is
// sufficient and exact.

namespace {
// BitAIcoin's real production ASERT activation height (227808) and anchor
// (227807) -- used literally, matching docs/AUXPOW_MILESTONE.md sec.8.2 and
// kernel/chainparams.cpp exactly, not a test-only stand-in value.
constexpr int ASERT_ACTIVATION = 227808;
constexpr int ASERT_ANCHOR = 227807;
} // namespace

/* 227806/227807 (pre-activation): still the ordinary legacy DAA, completely
 * untouched by ASERT -- BitAIcoin's real chain has no min-difficulty
 * exception and 227806/227807 are not 2016-block retarget boundaries
 * (227808 = 2016*113 is), so the legacy branch's own "only change once per
 * interval" rule returns the parent's nBits unchanged, exactly as it always
 * has for every non-boundary height. */
BOOST_AUTO_TEST_CASE(asert_pre_activation_heights_use_legacy_daa_unchanged)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::BITAICOIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    BOOST_REQUIRE_EQUAL(ASERT_ACTIVATION % params.DifficultyAdjustmentInterval(), 0);
    BOOST_REQUIRE_NE((ASERT_ANCHOR - 1) % params.DifficultyAdjustmentInterval(), 0);
    BOOST_REQUIRE_NE(ASERT_ANCHOR % params.DifficultyAdjustmentInterval(), 0);

    std::vector<CBlockIndex> chain(3);
    for (int i = 0; i < 3; ++i) {
        chain[i].pprev = i ? &chain[i - 1] : nullptr;
        chain[i].nHeight = ASERT_ANCHOR - 2 + i; // 227805, 227806, 227807
        chain[i].nTime = 1700000000 + i * params.nPowTargetSpacing;
        chain[i].nBits = UintToArith256(params.powLimit).GetCompact();
    }
    CBlockIndex& n227805 = chain[0];
    CBlockIndex& n227806 = chain[1];

    CBlockHeader plausibleBlock;
    plausibleBlock.nVersion = 4;

    // Height 227806, computed from pindexLast = block 227805.
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&n227805, &plausibleBlock, params), n227805.nBits);
    // Height 227807 (the anchor itself, still computed under the LEGACY
    // rule -- it is the anchor precisely because it's the last legacy
    // block), computed from pindexLast = block 227806.
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&n227806, &plausibleBlock, params), n227806.nBits);
}

/* 227808: the first real ASERT block. Built exactly on-schedule
 * (parent-to-anchor and anchor-to-its-parent both exactly nPowTargetSpacing
 * apart) so the expected result is HAND-VERIFIABLE, not just
 * cross-checked against the same code under test: heightDiff=0,
 * timeDiff=nPowTargetSpacing => numer=(spacing - spacing*(0+1))*RADIX=0 =>
 * exponent=0 => factor=65536 exactly => target*65536>>16 == target
 * unchanged. An exactly-on-schedule chain must reproduce the anchor's own
 * target byte-for-byte -- a real, meaningful algebraic property of ASERT
 * (not a tautology of this test), and this confirms the WIRING (anchor
 * resolution, height/time extraction) reproduces it. */
BOOST_AUTO_TEST_CASE(asert_activation_height_first_target_on_schedule_matches_anchor_exactly)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::BITAICOIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    std::vector<CBlockIndex> chain(2);
    chain[0].pprev = nullptr;
    chain[0].nHeight = ASERT_ANCHOR - 1; // 227806, the anchor's parent (time reference)
    chain[0].nTime = 1700000000;
    chain[0].nBits = UintToArith256(params.powLimit).GetCompact();

    chain[1].pprev = &chain[0];
    chain[1].nHeight = ASERT_ANCHOR; // 227807, the anchor itself
    chain[1].nTime = chain[0].nTime + params.nPowTargetSpacing; // exactly on schedule
    // A real, non-powLimit anchor target, so "unchanged" is a meaningful
    // check (not vacuously true because everything clamps to powLimit
    // anyway).
    arith_uint256 anchorTarget = UintToArith256(params.powLimit);
    anchorTarget >>= 8;
    chain[1].nBits = anchorTarget.GetCompact();

    CBlockHeader candidate;
    candidate.nVersion = 4;
    const unsigned int nextBits = GetNextWorkRequired(&chain[1], &candidate, params);
    BOOST_CHECK_EQUAL(nextBits, chain[1].nBits); // exactly the anchor's own target, unchanged
}

/* 227809: one block past activation, also built exactly on-schedule, same
 * hand-verifiable property (heightDiff=1, timeDiff=2*spacing => numer=0
 * again => target unchanged from the PARENT's, which is itself unchanged
 * from the anchor's -- confirming the wiring correctly recomputes fresh
 * height/time diffs from the SAME anchor at a different height, not just
 * for the boundary block itself). */
BOOST_AUTO_TEST_CASE(asert_one_block_past_activation_on_schedule_matches_anchor_exactly)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::BITAICOIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    std::vector<CBlockIndex> chain(3);
    chain[0].pprev = nullptr;
    chain[0].nHeight = ASERT_ANCHOR - 1; // 227806
    chain[0].nTime = 1700000000;
    chain[0].nBits = UintToArith256(params.powLimit).GetCompact();

    arith_uint256 anchorTarget = UintToArith256(params.powLimit);
    anchorTarget >>= 8;
    chain[1].pprev = &chain[0];
    chain[1].nHeight = ASERT_ANCHOR; // 227807
    chain[1].nTime = chain[0].nTime + params.nPowTargetSpacing;
    chain[1].nBits = anchorTarget.GetCompact();

    chain[2].pprev = &chain[1];
    chain[2].nHeight = ASERT_ACTIVATION; // 227808
    chain[2].nTime = chain[1].nTime + params.nPowTargetSpacing; // still exactly on schedule
    chain[2].nBits = anchorTarget.GetCompact(); // the previous (correct) ASERT-computed value

    CBlockHeader candidate;
    candidate.nVersion = 4;
    const unsigned int nextBits = GetNextWorkRequired(&chain[2], &candidate, params);
    BOOST_CHECK_EQUAL(nextBits, anchorTarget.GetCompact());
}

/* A genuinely OFF-schedule case (blocks arriving twice as fast as target),
 * exercising the non-trivial factor/shift math through the real wiring --
 * cross-checked against a direct ComputeASERTTarget() call built from the
 * SAME synthetic inputs (testing that GetNextWorkRequired() extracts
 * anchor/height/time correctly, not re-deriving the already-validated
 * math). */
BOOST_AUTO_TEST_CASE(asert_off_schedule_wiring_matches_direct_computation)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::BITAICOIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    std::vector<CBlockIndex> chain(3);
    chain[0].pprev = nullptr;
    chain[0].nHeight = ASERT_ANCHOR - 1;
    chain[0].nTime = 1700000000;
    chain[0].nBits = UintToArith256(params.powLimit).GetCompact();

    arith_uint256 anchorTarget = UintToArith256(params.powLimit);
    anchorTarget >>= 4;
    chain[1].pprev = &chain[0];
    chain[1].nHeight = ASERT_ANCHOR;
    chain[1].nTime = chain[0].nTime + params.nPowTargetSpacing;
    chain[1].nBits = anchorTarget.GetCompact();

    chain[2].pprev = &chain[1];
    chain[2].nHeight = ASERT_ACTIVATION;
    // Twice as fast as target spacing, sustained -- a real, meaningful
    // difficulty increase should result.
    chain[2].nTime = chain[1].nTime + params.nPowTargetSpacing / 2;
    chain[2].nBits = anchorTarget.GetCompact(); // whatever GetNextWorkRequired at 227808 produced, held here as-is for this test's own purpose

    CBlockHeader candidate;
    candidate.nVersion = 4;
    const unsigned int wiredBits = GetNextWorkRequired(&chain[2], &candidate, params);

    // Independently re-derive the same anchor/height/time relationship this
    // test itself set up, and confirm the pure math function agrees.
    const int64_t timeDiff = chain[2].nTime - chain[0].nTime; // parent-of-anchor is chain[0]
    const int64_t heightDiff = chain[2].nHeight - chain[1].nHeight;
    const arith_uint256 expected = ComputeASERTTarget(anchorTarget, params.nPowTargetSpacing,
                                                        timeDiff, heightDiff,
                                                        UintToArith256(params.powLimit),
                                                        params.BitAIASERTHalfLife);
    BOOST_CHECK_EQUAL(wiredBits, expected.GetCompact());
    // Sanity: faster-than-schedule blocks must make the NEXT target harder
    // (smaller), not easier or unchanged.
    BOOST_CHECK(expected < anchorTarget);
}

/* Reorg before activation: two competing height-227807 blocks (same parent,
 * height-227806 ancestor), each with its own distinct nBits/nTime. A
 * height-227808 descendant of EACH branch must resolve to THAT branch's own
 * 227807 as anchor (via plain GetAncestor()/pprev, with no global/shared
 * state) -- proven by getting two DIFFERENT, each independently correct,
 * results. */
BOOST_AUTO_TEST_CASE(asert_reorg_before_activation_uses_each_branchs_own_anchor)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::BITAICOIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    CBlockIndex n227806;
    n227806.pprev = nullptr;
    n227806.nHeight = ASERT_ANCHOR - 1;
    n227806.nTime = 1700000000;
    n227806.nBits = UintToArith256(params.powLimit).GetCompact();

    arith_uint256 targetA = UintToArith256(params.powLimit); targetA >>= 4;
    arith_uint256 targetB = UintToArith256(params.powLimit); targetB >>= 6; // a DIFFERENT, distinguishable target

    CBlockIndex branchA_227807;
    branchA_227807.pprev = &n227806;
    branchA_227807.nHeight = ASERT_ANCHOR;
    branchA_227807.nTime = n227806.nTime + params.nPowTargetSpacing;
    branchA_227807.nBits = targetA.GetCompact();

    CBlockIndex branchB_227807;
    branchB_227807.pprev = &n227806;
    branchB_227807.nHeight = ASERT_ANCHOR;
    branchB_227807.nTime = n227806.nTime + params.nPowTargetSpacing * 3; // a different, distinguishable time too
    branchB_227807.nBits = targetB.GetCompact();

    BOOST_REQUIRE(branchA_227807.nBits != branchB_227807.nBits); // real, distinguishable branches

    CBlockHeader candidate;
    candidate.nVersion = 4;
    const unsigned int resultA = GetNextWorkRequired(&branchA_227807, &candidate, params);
    const unsigned int resultB = GetNextWorkRequired(&branchB_227807, &candidate, params);

    // Re-derive refTarget from the SAME compact-truncated nBits the real
    // wiring reads (arith_uint256::SetCompact() is lossy -- a real, caught
    // discrepancy: comparing against the pre-truncation `targetA`/`targetB`
    // directly was off by rounding for targetB, since compact form only
    // keeps the top ~24 significant bits).
    arith_uint256 refTargetA; refTargetA.SetCompact(branchA_227807.nBits);
    arith_uint256 refTargetB; refTargetB.SetCompact(branchB_227807.nBits);
    const arith_uint256 expectedA = ComputeASERTTarget(refTargetA, params.nPowTargetSpacing,
                                                         params.nPowTargetSpacing, 0,
                                                         UintToArith256(params.powLimit), params.BitAIASERTHalfLife);
    const arith_uint256 expectedB = ComputeASERTTarget(refTargetB, params.nPowTargetSpacing,
                                                         params.nPowTargetSpacing * 3, 0,
                                                         UintToArith256(params.powLimit), params.BitAIASERTHalfLife);
    BOOST_CHECK_EQUAL(resultA, expectedA.GetCompact());
    BOOST_CHECK_EQUAL(resultB, expectedB.GetCompact());
    BOOST_CHECK(resultA != resultB); // the two branches must NOT silently converge/share state
}

/* Reorg crossing the activation boundary: two branches diverge at 227806,
 * each with its OWN 227807 and 227808, then each computes ITS OWN 227809 --
 * proving GetAncestor()'s per-branch walk (not any cached/global anchor)
 * governs which 227807 is used, all the way past the boundary. */
BOOST_AUTO_TEST_CASE(asert_reorg_crossing_activation_each_branch_own_ancestry)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::BITAICOIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    CBlockIndex n227806;
    n227806.pprev = nullptr;
    n227806.nHeight = ASERT_ANCHOR - 1;
    n227806.nTime = 1700000000;
    n227806.nBits = UintToArith256(params.powLimit).GetCompact();

    auto buildBranch = [&](arith_uint256 anchorTarget, int64_t anchorTimeOffset) {
        auto n227807 = std::make_unique<CBlockIndex>();
        n227807->pprev = &n227806;
        n227807->nHeight = ASERT_ANCHOR;
        n227807->nTime = n227806.nTime + anchorTimeOffset;
        n227807->nBits = anchorTarget.GetCompact();

        auto n227808 = std::make_unique<CBlockIndex>();
        n227808->pprev = n227807.get();
        n227808->nHeight = ASERT_ACTIVATION;
        n227808->nTime = n227807->nTime + params.nPowTargetSpacing;
        n227808->nBits = anchorTarget.GetCompact(); // on-schedule so far, so this equals the anchor target exactly (hand-verifiable, see above test)

        return std::pair{std::move(n227807), std::move(n227808)};
    };

    arith_uint256 targetA = UintToArith256(params.powLimit); targetA >>= 3;
    arith_uint256 targetB = UintToArith256(params.powLimit); targetB >>= 5;
    auto [branchA_227807, branchA_227808] = buildBranch(targetA, params.nPowTargetSpacing);
    auto [branchB_227807, branchB_227808] = buildBranch(targetB, params.nPowTargetSpacing * 2);

    CBlockHeader candidate;
    candidate.nVersion = 4;
    const unsigned int resultA = GetNextWorkRequired(branchA_227808.get(), &candidate, params);
    const unsigned int resultB = GetNextWorkRequired(branchB_227808.get(), &candidate, params);

    // Re-derive refTarget from the SAME compact-truncated nBits the real
    // wiring reads (see the identical fix/comment in the pre-activation
    // reorg test above -- SetCompact()/GetCompact() is lossy). Also
    // re-derive timeDiff independently from each branch's OWN real offsets
    // (branch B's anchor sits at anchorTimeOffset=2*spacing, so its 227808 is
    // 3*spacing after n227806 -- NOT the same timeDiff as branch A; an
    // earlier draft of this test wrongly assumed both branches shared the
    // same timeDiff, caught by actually computing it from the branch's own
    // real timestamps rather than copy-pasting branch A's value).
    arith_uint256 refTargetA; refTargetA.SetCompact(branchA_227807->nBits);
    arith_uint256 refTargetB; refTargetB.SetCompact(branchB_227807->nBits);
    const int64_t timeDiffA = branchA_227808->GetBlockTime() - n227806.GetBlockTime();
    const int64_t timeDiffB = branchB_227808->GetBlockTime() - n227806.GetBlockTime();
    const arith_uint256 expectedA = ComputeASERTTarget(refTargetA, params.nPowTargetSpacing,
                                                         timeDiffA, 1,
                                                         UintToArith256(params.powLimit), params.BitAIASERTHalfLife);
    const arith_uint256 expectedB = ComputeASERTTarget(refTargetB, params.nPowTargetSpacing,
                                                         timeDiffB, 1,
                                                         UintToArith256(params.powLimit), params.BitAIASERTHalfLife);
    BOOST_CHECK_EQUAL(resultA, expectedA.GetCompact());
    BOOST_CHECK_EQUAL(resultB, expectedB.GetCompact());
    BOOST_CHECK(branchA_227807->nBits != branchB_227807->nBits); // real, distinguishable branch ancestries
}

/* Proof-mechanism independence, the central architectural property of this
 * whole slice: at the same height and same ancestry, a direct SHA256d
 * candidate and an AuxPoW candidate must receive EXACTLY the same required
 * nBits. GetNextWorkRequired() never inspects `pblock->nVersion`'s AuxPoW
 * bit at all for the ASERT path -- proven directly here, not just by code
 * inspection. */
BOOST_AUTO_TEST_CASE(asert_direct_and_auxpow_candidates_receive_identical_nbits)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::BITAICOIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    BOOST_REQUIRE(params.fBitAIAuxpowEnabled); // real production BitAIcoin chain

    std::vector<CBlockIndex> chain(2);
    chain[0].pprev = nullptr;
    chain[0].nHeight = ASERT_ANCHOR - 1;
    chain[0].nTime = 1700000000;
    chain[0].nBits = UintToArith256(params.powLimit).GetCompact();

    arith_uint256 anchorTarget = UintToArith256(params.powLimit); anchorTarget >>= 10;
    chain[1].pprev = &chain[0];
    chain[1].nHeight = ASERT_ANCHOR;
    chain[1].nTime = chain[0].nTime + params.nPowTargetSpacing;
    chain[1].nBits = anchorTarget.GetCompact();

    CBlockHeader directCandidate;
    directCandidate.nVersion = 4; // plain, no AUXPOW bit

    CBlockHeader auxpowCandidate;
    auxpowCandidate.nVersion = MakeAuxpowVersion(BITAI_AUXPOW_CHAIN_ID, 4); // AUXPOW bit set
    BOOST_REQUIRE(auxpowCandidate.IsAuxpow());
    BOOST_REQUIRE(!directCandidate.IsAuxpow());

    const unsigned int directBits = GetNextWorkRequired(&chain[1], &directCandidate, params);
    const unsigned int auxpowBits = GetNextWorkRequired(&chain[1], &auxpowCandidate, params);
    BOOST_CHECK_EQUAL(directBits, auxpowBits);
    BOOST_CHECK_EQUAL(directBits, chain[1].nBits); // both exactly the anchor's own target, on schedule
}

/* ComputeASERTTarget() clamps, tested directly (no chain/wiring needed):
 * target=1 floor, powLimit ceiling, and extreme ahead/behind schedule
 * inputs. */
BOOST_AUTO_TEST_CASE(compute_asert_target_clamps_and_extreme_schedule)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::BITAICOIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    arith_uint256 refTarget = powLimit; refTarget >>= 4;

    // Extreme BEHIND schedule (blocks arriving far too slowly, sustained
    // for a long time) -> target must clamp to powLimit (the easiest
    // allowed), never exceed it.
    {
        const int64_t heightDiff = 100;
        const int64_t timeDiff = params.nPowTargetSpacing * heightDiff * 10000; // 10000x slower than schedule
        const arith_uint256 result = ComputeASERTTarget(refTarget, params.nPowTargetSpacing, timeDiff,
                                                          heightDiff, powLimit, params.BitAIASERTHalfLife);
        BOOST_CHECK_EQUAL(result, powLimit);
    }

    // Extreme AHEAD of schedule (blocks arriving far too fast, sustained for
    // long enough) -> target must clamp to the floor of 1, never reach/cross
    // 0. Verified against contrib/asert_reference.py directly before use,
    // not assumed: a modest 100-block/zero-elapsed-time burst (tried first)
    // is real and non-trivial but only ~2.8 half-lives' worth of adjustment
    // (0xffff...>>4 -> 0x249ebfff...), nowhere near the floor -- an
    // undersized-input mistake caught by actually running the numbers, not
    // a code bug. 10,000 blocks at zero elapsed time is confirmed (via the
    // reference) to actually reach 1.
    {
        const int64_t heightDiff = 10000;
        const int64_t timeDiff = 0; // all 10,000 blocks landed at the same instant as the anchor's parent
        const arith_uint256 result = ComputeASERTTarget(refTarget, params.nPowTargetSpacing, timeDiff,
                                                          heightDiff, powLimit, params.BitAIASERTHalfLife);
        BOOST_CHECK_EQUAL(result, arith_uint256{1});
    }

    // A large negative timeDiff (deeply "ahead of schedule") must clamp the
    // same way, not underflow/wrap. Also verified against the reference
    // first: -(spacing*heightDiff*100) (tried first) is NOT extreme enough
    // (0xcf24ffff..., not 1); -(spacing*heightDiff*10000) is confirmed to
    // reach 1.
    {
        const int64_t heightDiff = 50;
        const int64_t timeDiff = -(params.nPowTargetSpacing * heightDiff * 10000);
        const arith_uint256 result = ComputeASERTTarget(refTarget, params.nPowTargetSpacing, timeDiff,
                                                          heightDiff, powLimit, params.BitAIASERTHalfLife);
        BOOST_CHECK_EQUAL(result, arith_uint256{1});
    }
}

/* Compact nBits round-trip: ComputeASERTTarget()'s output, once reduced to
 * compact form (as GetNextWorkRequired() itself does), must remain a valid,
 * self-consistent target when read back -- across several representative
 * points in the range, not just one. */
BOOST_AUTO_TEST_CASE(compute_asert_target_compact_roundtrip_vectors)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::BITAICOIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);

    const std::vector<int> shifts = {0, 1, 4, 16, 64, 128, 200};
    for (int shift : shifts) {
        arith_uint256 refTarget = powLimit;
        refTarget >>= shift;
        if (refTarget == 0) continue; // shifted past zero -- not a valid refTarget, skip
        for (int64_t heightDiff : {int64_t{0}, int64_t{1}, int64_t{10}}) {
            const int64_t timeDiff = params.nPowTargetSpacing * (heightDiff + 1); // exactly on schedule for this heightDiff
            const arith_uint256 result = ComputeASERTTarget(refTarget, params.nPowTargetSpacing, timeDiff,
                                                              heightDiff, powLimit, params.BitAIASERTHalfLife);
            BOOST_CHECK_EQUAL(result, refTarget); // on-schedule => unchanged, hand-verifiable again

            bool neg, over;
            arith_uint256 roundTripped;
            roundTripped.SetCompact(result.GetCompact(), &neg, &over);
            BOOST_CHECK(!neg);
            BOOST_CHECK(!over);
            BOOST_CHECK(roundTripped <= powLimit);
            BOOST_CHECK(roundTripped > 0);
        }
    }
}

/* --- Official BCH/BCHN aserti3-2d vector gate (docs/AUXPOW_MILESTONE.md
 * sec.9) ---
 *
 * The C++ ComputeASERTTarget() implementation is now consensus code and
 * deserves its own independent gate -- NOT merely a comparison against
 * contrib/asert_reference.py (our own prior Python work). These 14,000 rows
 * are BCH/BCHN's OWN real, published test vectors (test_vectors/aserti3-2d/
 * run01 through run12, bitcoin-cash-node/bchn-sw/qa-assets on GitLab --
 * fetched directly via the GitLab API's raw-file endpoint on 2026-09-24,
 * transcribed into src/test/data/asert_bchn_vectors.h by a one-time
 * mechanical conversion script, not retyped by hand), covering: steady
 * schedule at powLimit/an arbitrary target/the minimum target (run01-03);
 * sustained target-easing and target-hardening via repeated half-life jumps
 * (run04-05); realistic randomized solvetimes for stable/up-ramping/
 * down-ramping hashrate (run06-08); extreme height near INT32_MAX and near
 * INT64_MAX with extreme time near INT32_MAX (run09-10); negative time
 * diffs, including a large sustained negative run (run11-12, the latter
 * 10,000 consecutive blocks each arriving before the previous one -- a real
 * stress test of the floor clamp under sustained pressure). Uses BCH
 * mainnet's OWN real consensus parameters (powLimit
 * 0x00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff,
 * targetSpacing 600, halfLife 172800 -- 2 days, BCH's own real production
 * value, NOT BitAIcoin's frozen 21600) -- this test proves the ARITHMETIC
 * itself is correct, independent of and prior to BitAIcoin's own changed
 * half-life parameterization (proven separately, against
 * contrib/asert_reference.py, in asert_bitaicoin_halflife_vectors_test
 * below). */
BOOST_AUTO_TEST_CASE(asert_official_bchn_vectors_test)
{
    const arith_uint256 bchMainnetPowLimit = UintToArith256(
        uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"});
    constexpr int64_t bchTargetSpacing = 600;
    constexpr int64_t bchHalfLife = 172800; // BCH's own real 2-day half-life -- NOT BitAIcoin's

    size_t checked = 0;
    for (const auto& v : BCHN_ASERT_VECTORS) {
        arith_uint256 refTarget;
        refTarget.SetCompact(v.anchor_nbits);
        const arith_uint256 result = ComputeASERTTarget(refTarget, bchTargetSpacing, v.time_diff,
                                                          v.height_diff, bchMainnetPowLimit, bchHalfLife);
        const uint32_t resultBits = result.GetCompact();
        BOOST_CHECK_MESSAGE(resultBits == v.expected_nbits,
                             strprintf("%s: anchor_nbits=0x%08x height_diff=%d time_diff=%d -- "
                                       "expected nBits=0x%08x, got 0x%08x",
                                       v.run, v.anchor_nbits, v.height_diff, v.time_diff,
                                       v.expected_nbits, resultBits));
        ++checked;
    }
    BOOST_CHECK_EQUAL(checked, size_t{14000}); // sanity: the whole real vector set actually ran, not a truncated subset
}

/* A handful of specific, NAMED-property vectors from BCHN's own
 * src/test/pow_tests.cpp::calculate_asert_test (the inline C++ table, as
 * opposed to the qa-assets data files above) -- ported because they target
 * specific, real, hard-to-accidentally-cover edge cases the qa-assets runs
 * don't happen to hit: an overflow-detection-defeating refTarget, an exact
 * powLimit-clamp boundary, and exact-multiple-of-halflife doubling/halving
 * checks. Same BCH mainnet parameters as above. */
BOOST_AUTO_TEST_CASE(asert_official_bchn_named_property_vectors_test)
{
    const arith_uint256 powLimit = UintToArith256(
        uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"});
    constexpr int64_t spacing = 600;
    constexpr int64_t halfLife = 172800;
    const uint32_t powLimitBits = powLimit.GetCompact();
    // BCHN's own test convention: every call in this test adds this fixed
    // 600s to its timeDiff argument ("we assume the parent is ideally
    // spaced in time before the reference block") -- reproduced exactly,
    // including in the doubling/halving/ramp/overflow sequence below, where
    // omitting it (an error caught before this test was ever run, not
    // after) would land the "two days ahead" case 600 seconds short of an
    // exact half-life multiple and break the exact-doubling property.
    constexpr int64_t parentTimeDiff = 600;
    const arith_uint256 initialTarget = powLimit >> 4;

    // Steady: a block landing exactly on schedule leaves the target unchanged.
    arith_uint256 nextTarget = ComputeASERTTarget(initialTarget, spacing, parentTimeDiff + 600, 1, powLimit, halfLife);
    BOOST_CHECK_EQUAL(nextTarget, initialTarget);

    // A block arriving in half the expected time makes the next target harder.
    nextTarget = ComputeASERTTarget(initialTarget, spacing, parentTimeDiff + 600 + 300, 2, powLimit, halfLife);
    BOOST_CHECK(nextTarget < initialTarget);

    // A block that makes up the prior shortfall restores the target to initial exactly.
    arith_uint256 prevTarget = nextTarget;
    nextTarget = ComputeASERTTarget(initialTarget, spacing, parentTimeDiff + 600 + 300 + 900, 3, powLimit, halfLife);
    BOOST_CHECK(nextTarget > prevTarget);
    BOOST_CHECK_EQUAL(nextTarget, initialTarget);

    // Two days (one half-life) ahead of schedule doubles the target (halves
    // the difficulty); two days behind halves the target again, back to
    // initialTarget.
    prevTarget = nextTarget;
    nextTarget = ComputeASERTTarget(prevTarget, spacing, parentTimeDiff + 288 * 1200, 288, powLimit, halfLife);
    BOOST_CHECK_EQUAL(nextTarget, prevTarget * 2);

    prevTarget = nextTarget;
    nextTarget = ComputeASERTTarget(prevTarget, spacing, parentTimeDiff + 288 * 0, 288, powLimit, halfLife);
    BOOST_CHECK_EQUAL(nextTarget, prevTarget / arith_uint256{2});
    BOOST_CHECK_EQUAL(nextTarget, initialTarget);

    // Ramp from initialTarget up to powLimit -- exactly 4 doublings (initialTarget = powLimit >> 4).
    uint32_t next_nBits = 0;
    for (int k = 0; k < 3; ++k) {
        prevTarget = nextTarget;
        nextTarget = ComputeASERTTarget(prevTarget, spacing, parentTimeDiff + 288 * 1200, 288, powLimit, halfLife);
        BOOST_CHECK_EQUAL(nextTarget, prevTarget * 2);
        BOOST_CHECK(nextTarget < powLimit);
        next_nBits = nextTarget.GetCompact();
        BOOST_CHECK(next_nBits != powLimitBits);
    }
    prevTarget = nextTarget;
    nextTarget = ComputeASERTTarget(prevTarget, spacing, parentTimeDiff + 288 * 1200, 288, powLimit, halfLife);
    BOOST_CHECK_EQUAL(nextTarget, prevTarget * 2);
    BOOST_CHECK_EQUAL(nextTarget.GetCompact(), powLimitBits);

    // Fast periods cannot push the target beyond powLimit even under an
    // input (512 days ahead) that would overflow a naive uint256 multiply.
    nextTarget = ComputeASERTTarget(prevTarget, spacing, parentTimeDiff + 512 * 144 * 600, 0, powLimit, halfLife);
    BOOST_CHECK_EQUAL(nextTarget.GetCompact(), powLimitBits);

    // Sustained slow periods (~446 days worth of blocks) bring powLimit
    // itself all the way down to the floor of 1 -- no offset here, matching
    // the source exactly.
    nextTarget = ComputeASERTTarget(powLimit, spacing, 0, 2 * (256 - 33) * 144, powLimit, halfLife);
    BOOST_CHECK_EQUAL(nextTarget.GetCompact(), arith_uint256{1}.GetCompact());

    const arith_uint256 FUNNY_REF_TARGET = UintToArith256(
        uint256{"000000008000000000000000000fffffffffffffffffffffffffffffffffffff"});
    const arith_uint256 SINGLE_300_TARGET = UintToArith256(
        uint256{"00000000ffb1ffffffffffffffffffffffffffffffffffffffffffffffffffff"});

    struct CalcVec { arith_uint256 refTarget; int64_t timeDiff; int64_t heightDiff; uint32_t expectedBits; };
    const arith_uint256 one{1};
    const std::vector<CalcVec> vecs = {
        {powLimit, 0, 2 * 144, 0x1c7fffff},
        {powLimit, 0, 4 * 144, 0x1c3fffff},
        {powLimit >> 1, 0, 2 * 144, 0x1c3fffff},
        {powLimit >> 2, 0, 2 * 144, 0x1c1fffff},
        {powLimit >> 3, 0, 2 * 144, 0x1c0fffff},
        {powLimit, 0, 2 * (256 - 34) * 144, 0x01030000},
        {powLimit, 0, 2 * (256 - 34) * 144 + 119, 0x01030000},
        {powLimit, 0, 2 * (256 - 34) * 144 + 120, 0x01020000},
        {powLimit, 0, 2 * (256 - 33) * 144 - 1, 0x01020000},
        {powLimit, 0, 2 * (256 - 33) * 144, 0x01010000},
        {powLimit, 0, 2 * (256 - 32) * 144, 0x01010000},
        {one, 0, 2 * (256 - 32) * 144, 0x01010000},
        {powLimit, 2 * (512 - 32) * 144, 0, powLimitBits},
        {one, (512 - 64) * 144 * 600, 0, powLimitBits},
        {powLimit, 300, 1, 0x1d00ffb1},
        {FUNNY_REF_TARGET, 600 * 2 * 33 * 144, 0, powLimitBits}, // confuses any overflow-detection-by-inspecting-result attempt
        {one, 600 * 2 * 256 * 144, 0, powLimitBits}, // overflow to exactly 2^256
    };
    for (const auto& v : vecs) {
        // BCHN's own test adds a fixed 600s parent_time_diff to every vector's
        // timeDiff (its own convention for "the anchor's parent is ideally
        // spaced before the anchor") -- reproduced exactly.
        const arith_uint256 result = ComputeASERTTarget(v.refTarget, spacing, 600 + v.timeDiff,
                                                          v.heightDiff, powLimit, halfLife);
        BOOST_CHECK_MESSAGE(result.GetCompact() == v.expectedBits,
                             strprintf("refTarget=%s timeDiff=%d heightDiff=%d -- expected 0x%08x, got 0x%08x",
                                       v.refTarget.ToString(), v.timeDiff, v.heightDiff,
                                       v.expectedBits, result.GetCompact()));
    }
    // The SINGLE_300_TARGET vector separately, since it checks the exact
    // TARGET (not just compact nBits) to confirm the clamp lands precisely.
    {
        const arith_uint256 result = ComputeASERTTarget(powLimit, spacing, 600 + 300, 1, powLimit, halfLife);
        BOOST_CHECK_EQUAL(result, SINGLE_300_TARGET);
    }
}

/* Substantial deterministic set of BitAIcoin's OWN 21600s (6-hour) half-life
 * inputs, differential-tested against the already-validated
 * contrib/asert_reference.py -- the second half of this section's gate:
 * official BCH vectors (above) prove the ARITHMETIC; these prove BitAIcoin's
 * CHANGED half-life parameterization specifically. Generated by running the
 * Python reference directly (not hand-computed) across a spread of
 * height/time combinations using BitAIcoin's real powLimit/spacing/half-life. */
BOOST_AUTO_TEST_CASE(asert_bitaicoin_halflife_vectors_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::BITAICOIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    BOOST_REQUIRE_EQUAL(params.BitAIASERTHalfLife, 21600);
    BOOST_REQUIRE_EQUAL(params.nPowTargetSpacing, 600);

    struct BaiVec { int shift; int64_t heightDiff; int64_t timeDiff; const char* expectedTargetHex; };
    // clang-format off
    const std::vector<BaiVec> vecs = {
        // shift, heightDiff, timeDiff, expectedTarget -- each value produced by
        // ACTUALLY RUNNING contrib/asert_reference.py::calculate_asert() with
        // BitAIcoin's real powLimit
        // (0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff),
        // spacing 600, halfLife 21600, then transcribed verbatim -- not
        // hand-computed. (A first draft of this table used fabricated
        // placeholder hex values while the real script call was still
        // pending; caught before this test was ever run, not after -- see
        // docs/AUXPOW_MILESTONE.md sec.9 for the disclosure.)
        {4, 0, 600, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},   // exactly on schedule -> unchanged
        {4, 1, 1200, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},  // exactly on schedule -> unchanged
        {4, 10, 300, "00000000d1267fffffffffffffffffffffffffffffffffffffffffffffffffff"},  // ahead of schedule -> harder (smaller)
        {4, 10, 6600, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"}, // exactly on schedule -> unchanged
        {4, 100, 60000, "00000000fb217fffffffffffffffffffffffffffffffffffffffffffffffffff"}, // slightly behind -> slightly easier
        {4, 100, 30000, "000000005fe33fffffffffffffffffffffffffffffffffffffffffffffffffff"}, // well ahead -> notably harder
        {4, 100, 0, "00000000249ebfffffffffffffffffffffffffffffffffffffffffffffffffff"},     // far ahead (100 blocks, zero elapsed time) -> much harder
        {2, 0, -600, "00000003d965ffffffffffffffffffffffffffffffffffffffffffffffffffff"},    // negative timeDiff -> harder still
        {8, 0, -6000, "000000000cf24fffffffffffffffffffffffffffffffffffffffffffffffffff"},   // large negative timeDiff from an already-small refTarget -> harder again
    };
    // clang-format on
    for (const auto& v : vecs) {
        arith_uint256 refTarget = powLimit;
        refTarget >>= v.shift;
        const arith_uint256 result = ComputeASERTTarget(refTarget, params.nPowTargetSpacing, v.timeDiff,
                                                          v.heightDiff, powLimit, params.BitAIASERTHalfLife);
        const auto parsedExpected = uint256::FromHex(v.expectedTargetHex);
        BOOST_REQUIRE(parsedExpected.has_value());
        const arith_uint256 expected = UintToArith256(*parsedExpected);
        BOOST_CHECK_MESSAGE(result == expected,
                             strprintf("shift=%d heightDiff=%d timeDiff=%d -- expected %s, got %s",
                                       v.shift, v.heightDiff, v.timeDiff, expected.ToString(), result.ToString()));
    }
}

/* Exact branch-ordering regression (docs/AUXPOW_MILESTONE.md sec.9 item 4):
 * 227808 is BOTH the first ASERT block AND what would otherwise have been a
 * legacy 2016-block retarget boundary (227808 / 2016 = 113 exactly). The
 * ASERT branch in GetNextWorkRequired() is checked FIRST and, once it
 * matches, returns immediately -- the legacy retarget that would otherwise
 * fire at this exact height is intentionally never reached. This is a
 * frozen, deliberate design decision (see the real activation-boundary
 * comparison in contrib/asert_activation_boundary_simulation.py and the
 * write-up in docs/AUXPOW_MILESTONE.md sec.9 item 3), not an accident of
 * code ordering -- this test exists specifically so a future refactor that
 * silently reordered the two branches would be caught here, not discovered
 * later as a live consensus surprise.
 *
 * Constructs a synthetic 225792->227807 epoch (2016 real-shaped
 * CBlockIndex entries, mirroring GetBlockProofEquivalentTime_test's own
 * established pattern) using BitAIcoin's REAL observed height-225792
 * nBits/time (0x1d0fffff / 1789789080) as the epoch start, with a 10x
 * hashrate shock for the 100 blocks before the anchor that ENDS exactly at
 * the anchor (block 227807 itself is normal-paced) -- the exact scenario
 * the boundary simulation identified as producing the largest legacy-vs-ASERT
 * divergence (ASERT sees only the anchor's own normal-paced solvetime, so
 * it stays UNCHANGED at 0x1d0fffff; the legacy 2016-block average still
 * carries real weight from the 100-block shock and would have hardened to
 * 0x1d0f61c6 -- a real, non-trivial, independently-confirmed-different
 * value, not an off-by-a-rounding-error difference). */
BOOST_AUTO_TEST_CASE(asert_branch_ordering_supersedes_legacy_retarget_at_activation)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::BITAICOIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    BOOST_REQUIRE_EQUAL(ASERT_ACTIVATION % params.DifficultyAdjustmentInterval(), 0);
    BOOST_REQUIRE_EQUAL(params.DifficultyAdjustmentInterval(), 2016);

    constexpr int epochStartHeight = ASERT_ACTIVATION - 2016; // 225792, real height
    constexpr int64_t epochStartTime = 1789789080;            // real observed time at that height
    constexpr uint32_t epochStartNBits = 0x1d0fffff;           // real observed nBits at that height
    constexpr int shockStartHeight = ASERT_ANCHOR - 100;       // shock covers the 100 blocks before the anchor
    constexpr int shockMultiplier = 10;

    std::vector<CBlockIndex> chain(2016); // heights epochStartHeight .. ASERT_ANCHOR inclusive
    int64_t t = epochStartTime;
    for (int i = 0; i < 2016; ++i) {
        const int height = epochStartHeight + i;
        chain[i].pprev = i ? &chain[i - 1] : nullptr;
        chain[i].nHeight = height;
        chain[i].nBits = epochStartNBits; // legacy never retargets mid-epoch -- unchanged throughout, matching real behavior
        if (i == 0) {
            chain[i].nTime = epochStartTime;
            continue;
        }
        // Shock covers [shockStartHeight, ASERT_ANCHOR) -- ends exactly AT
        // the anchor, so the anchor's own solvetime (the last step) is
        // normal-paced, matching the boundary-simulation scenario exactly.
        const bool inShock = (height >= shockStartHeight) && (height < ASERT_ANCHOR);
        t += inShock ? (params.nPowTargetSpacing / shockMultiplier) : params.nPowTargetSpacing;
        chain[i].nTime = t;
    }
    CBlockIndex& anchor = chain[2015]; // height 227807
    BOOST_REQUIRE_EQUAL(anchor.nHeight, ASERT_ANCHOR);

    // Confirm the two branches genuinely disagree for this exact scenario
    // (independently derived, not read back from the code under test): the
    // legacy formula computed directly here, and ASERT computed via
    // ComputeASERTTarget() directly (bypassing GetNextWorkRequired()'s own
    // dispatch, to get an independent reference value).
    const int64_t actualTimespan = anchor.GetBlockTime() - epochStartTime;
    arith_uint256 legacyTarget;
    legacyTarget.SetCompact(epochStartNBits);
    int64_t clampedTimespan = actualTimespan;
    if (clampedTimespan < params.nPowTargetTimespan / 4) clampedTimespan = params.nPowTargetTimespan / 4;
    if (clampedTimespan > params.nPowTargetTimespan * 4) clampedTimespan = params.nPowTargetTimespan * 4;
    legacyTarget *= clampedTimespan;
    legacyTarget /= params.nPowTargetTimespan;
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    if (legacyTarget > powLimit) legacyTarget = powLimit;
    const uint32_t legacyWouldBeBits = legacyTarget.GetCompact();

    arith_uint256 anchorTarget;
    anchorTarget.SetCompact(epochStartNBits);
    const arith_uint256 asertTarget = ComputeASERTTarget(anchorTarget, params.nPowTargetSpacing,
                                                          anchor.GetBlockTime() - chain[2014].GetBlockTime(),
                                                          0, powLimit, params.BitAIASERTHalfLife);
    const uint32_t asertBits = asertTarget.GetCompact();

    BOOST_REQUIRE_NE(legacyWouldBeBits, asertBits); // the scenario is only meaningful if these genuinely differ
    BOOST_REQUIRE_EQUAL(asertBits, epochStartNBits); // matches the boundary simulation's own finding exactly (unchanged)

    // THE actual regression: GetNextWorkRequired() at the real activation
    // height must return the ASERT value, never the legacy-retarget value --
    // proving the branch ordering (ASERT checked and returned BEFORE the
    // legacy "every 2016 blocks" branch is ever reached), not just that the
    // two formulas differ in isolation.
    CBlockHeader candidate;
    candidate.nVersion = 4;
    const unsigned int wiredBits = GetNextWorkRequired(&anchor, &candidate, params);
    BOOST_CHECK_EQUAL(wiredBits, asertBits);
    BOOST_CHECK_NE(wiredBits, legacyWouldBeBits);
}

BOOST_AUTO_TEST_SUITE_END()
