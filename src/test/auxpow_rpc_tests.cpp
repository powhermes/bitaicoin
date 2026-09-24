// Copyright (c) 2026 The BitAIcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Unit tests for AuxBlockCandidateCache (src/rpc/auxpow.h), the RPC-layer
// candidate cache behind createauxblock/submitauxblock
// (docs/AUXPOW_MILESTONE.md sec.10 item 5). This file deliberately does NOT
// re-test AuxPoW consensus rules (see src/test/auxpow_tests.cpp for that) --
// it only exercises the cache's own explicit contract: bounded, FIFO
// eviction, immutable entries, and safe under real concurrent access. The
// cache has no chain-state dependency of its own, so BasicTestingSetup is
// sufficient.

#include <primitives/block.h>
#include <rpc/auxpow.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(auxpow_rpc_tests, BasicTestingSetup)

namespace {

std::shared_ptr<const AuxBlockCandidate> MakeCandidate(const uint256& hash, const uint256& prev_hash, int height)
{
    return std::make_shared<const AuxBlockCandidate>(AuxBlockCandidate{
        .block = std::make_shared<const CBlock>(),
        .hash = hash,
        .prev_hash = prev_hash,
        .height = height,
        .create_time = 0,
        .payout_script = CScript(),
    });
}

} // namespace

BOOST_AUTO_TEST_CASE(insert_and_get_roundtrip)
{
    AuxBlockCandidateCache cache;
    const uint256 hash = m_rng.rand256();
    const uint256 prev = m_rng.rand256();
    cache.Insert(MakeCandidate(hash, prev, 7));

    auto got = cache.Get(hash);
    BOOST_REQUIRE(got != nullptr);
    BOOST_CHECK_EQUAL(got->hash.GetHex(), hash.GetHex());
    BOOST_CHECK_EQUAL(got->prev_hash.GetHex(), prev.GetHex());
    BOOST_CHECK_EQUAL(got->height, 7);
    BOOST_CHECK_EQUAL(cache.Size(), 1U);
}

BOOST_AUTO_TEST_CASE(unknown_hash_returns_nullptr)
{
    AuxBlockCandidateCache cache;
    cache.Insert(MakeCandidate(m_rng.rand256(), m_rng.rand256(), 1));
    BOOST_CHECK(cache.Get(m_rng.rand256()) == nullptr);
}

BOOST_AUTO_TEST_CASE(bounded_fifo_eviction)
{
    // "Old work never silently maps to a different candidate" (sec.10
    // item 5): insert well past MAX_CANDIDATES and confirm the cache never
    // grows past its cap, the OLDEST entries are the ones gone, and the
    // newest MAX_CANDIDATES entries are all still exactly retrievable.
    AuxBlockCandidateCache cache;
    const size_t total = AuxBlockCandidateCache::MAX_CANDIDATES + 10;
    std::vector<uint256> hashes;
    hashes.reserve(total);
    for (size_t i = 0; i < total; ++i) {
        uint256 h = m_rng.rand256();
        hashes.push_back(h);
        cache.Insert(MakeCandidate(h, m_rng.rand256(), static_cast<int>(i)));
    }

    BOOST_CHECK_EQUAL(cache.Size(), AuxBlockCandidateCache::MAX_CANDIDATES);

    // The oldest 10 must be gone.
    for (size_t i = 0; i < 10; ++i) {
        BOOST_CHECK(cache.Get(hashes[i]) == nullptr);
    }
    // The newest MAX_CANDIDATES must all still be present, and each must map
    // back to exactly its own hash/height -- never a different candidate's
    // content.
    for (size_t i = 10; i < total; ++i) {
        auto got = cache.Get(hashes[i]);
        BOOST_REQUIRE(got != nullptr);
        BOOST_CHECK_EQUAL(got->hash.GetHex(), hashes[i].GetHex());
        BOOST_CHECK_EQUAL(got->height, static_cast<int>(i));
    }
}

BOOST_AUTO_TEST_CASE(entries_are_immutable_shared_snapshots)
{
    // Get() returns shared_ptr<const AuxBlockCandidate> -- enforced at
    // compile time (the type itself has no non-const accessor), so this
    // test instead proves the *sharing* half of the contract: two Get()
    // calls for the same still-cached hash return the SAME underlying
    // object (not independent copies), so a caller's use of the data after
    // Get() returns is guaranteed to see one consistent snapshot even if
    // the cache is concurrently mutated by other callers.
    AuxBlockCandidateCache cache;
    const uint256 hash = m_rng.rand256();
    cache.Insert(MakeCandidate(hash, m_rng.rand256(), 3));

    auto first = cache.Get(hash);
    auto second = cache.Get(hash);
    BOOST_REQUIRE(first != nullptr);
    BOOST_REQUIRE(second != nullptr);
    BOOST_CHECK(first.get() == second.get());
}

BOOST_AUTO_TEST_CASE(concurrent_insert_and_get_is_safe_and_never_cross_contaminates)
{
    // A real concurrent-access stress test (docs/AUXPOW_MILESTONE.md sec.10
    // item 9: "simultaneous submissions against one cached candidate cannot
    // corrupt cache state"). This cannot deterministically prove the
    // absence of a data race the way ThreadSanitizer would, but it does
    // prove the cache's actual, checkable invariant under real concurrent
    // load: the cache never exceeds its cap, and -- critically -- every
    // successful Get() during and after the race returns a candidate whose
    // own `hash` field equals the key it was looked up under. A key/value
    // mixup (the concrete way an unsynchronized map could misbehave here)
    // would be caught by that check on any run in which it occurred.
    AuxBlockCandidateCache cache;
    static constexpr int kThreads = 8;
    static constexpr int kPerThread = 250;

    // All randomness is generated up front, single-threaded: FastRandomContext
    // (m_rng, from BasicTestingSetup) has no internal synchronization of its
    // own, so calling it concurrently from multiple threads is a genuine,
    // unsynchronized data race on its own internal ChaCha20 state -- caught
    // for real here as an intermittent segfault (memory access violation)
    // under this test before this fix, NOT a bug in AuxBlockCandidateCache
    // itself (which locks its own m_mutex correctly on every access). Every
    // thread below touches only pre-generated, per-thread data plus the
    // cache's own locked methods -- no shared mutable state outside the
    // cache is ever touched concurrently.
    std::vector<std::vector<uint256>> per_thread_hashes(kThreads);
    std::vector<std::vector<uint256>> per_thread_prev_hashes(kThreads);
    for (auto& v : per_thread_hashes) {
        v.reserve(kPerThread);
        for (int i = 0; i < kPerThread; ++i) v.push_back(m_rng.rand256());
    }
    for (auto& v : per_thread_prev_hashes) {
        v.reserve(kPerThread);
        for (int i = 0; i < kPerThread; ++i) v.push_back(m_rng.rand256());
    }

    std::atomic<bool> mismatch{false};
    std::vector<std::thread> threads;

    // Writer threads: each inserts its own unique hashes.
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kPerThread; ++i) {
                cache.Insert(MakeCandidate(per_thread_hashes[t][i], per_thread_prev_hashes[t][i], i));
            }
        });
    }
    // Reader threads: concurrently poll for entries other threads are
    // inserting (and possibly evicting), checking the one invariant that
    // must never be violated regardless of interleaving.
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kPerThread; ++i) {
                auto got = cache.Get(per_thread_hashes[t][i]);
                if (got && got->hash != per_thread_hashes[t][i]) {
                    mismatch = true;
                }
                // Also exercise Size() concurrently; it must never observe
                // more than the documented cap.
                if (cache.Size() > AuxBlockCandidateCache::MAX_CANDIDATES) {
                    mismatch = true;
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    BOOST_CHECK(!mismatch);
    BOOST_CHECK(cache.Size() <= AuxBlockCandidateCache::MAX_CANDIDATES);
}

BOOST_AUTO_TEST_SUITE_END()
