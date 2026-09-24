// Copyright (c) 2015-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <auxpow.h>
#include <chain.h>
#include <chainparams.h>
#include <pow.h>
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

BOOST_AUTO_TEST_SUITE_END()
