// Copyright (c) 2026 The BitAIcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// createauxblock / submitauxblock: the conventional Namecoin/Dogecoin-style
// two-call AuxPoW mining interface (docs/AUXPOW_MILESTONE.md sec.10).
//
// SCOPE, stated explicitly: this exposes a clean, pool-facing RPC API for
// producing and submitting merge-mined BitAIcoin child candidates. It is
// deliberately NOT a mining pool -- no Stratum, no share accounting, no
// payouts, no pool-node orchestration. See docs/AUXPOW_MILESTONE.md sec.10
// for the full boundary.
//
// DESIGN, in one place for reference (implemented in auxpow.cpp):
//   - createauxblock builds a complete, real BitAIcoin block via the same
//     interfaces::Mining path generatetoaddress/generateblock already use
//     (real tip, real mempool, real fees/subsidy, supplied payout script,
//     real witness commitment, real GetNextWorkRequired()/ASERT result),
//     then marks it AuxPoW-flagged via the already-frozen
//     MakeAuxpowVersion(BITAI_AUXPOW_CHAIN_ID, baseVersion) -- never a
//     manual OR of chain-ID/versionbits fields. The block is cached,
//     complete and immutable, keyed by its own GetHash() (which never
//     depends on any attached auxpow -- see primitives/block.h).
//   - submitauxblock looks up the cached candidate, copies it (never
//     mutates the cached entry), attaches the submitted CAuxPow to the
//     copy, and hands the copy to the REAL validation pipeline
//     (ChainstateManager::ProcessNewBlock, the same one submitblock uses)
//     -- exactly the way submitblock captures its own real result, via a
//     CValidationInterface state-catcher. No consensus rule is
//     re-implemented here; this file only does cheap structural/parameter
//     checks (chain-awareness, activation height, hash/hex well-formedness,
//     known-candidate lookup) before deferring entirely to that pipeline.
//   - The candidate cache (AuxBlockCandidateCache) is RPC infrastructure,
//     not consensus, but is designed to be safe under concurrent pool
//     submissions: thread-safe, bounded, and every stored entry is a
//     complete, immutable snapshot -- a submission takes a COPY under lock,
//     then releases the lock before running BlockAssembler-adjacent work or
//     ProcessNewBlock, so the cache mutex is never held across either.

#ifndef BITCOIN_RPC_AUXPOW_H
#define BITCOIN_RPC_AUXPOW_H

#include <primitives/block.h>
#include <script/script.h>
#include <sync.h>
#include <uint256.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>

class CRPCTable;

void RegisterAuxPowRPCCommands(CRPCTable& t);

/**
 * A single cached child-block candidate: a complete, otherwise-normal
 * BitAIcoin block (real tip, real mempool, real fees/subsidy, real payout
 * script, real witness commitment, AuxPoW-flagged nVersion) with no auxpow
 * attached yet. Immutable once constructed -- every field is set once, at
 * construction, and never modified afterward. A submission that wants to
 * attach a proof must copy `*block` into a NEW CBlock and attach the proof
 * to that copy; it must never reach into a cached entry and mutate it in
 * place (see AuxBlockCandidateCache::Get()'s own doc comment).
 */
struct AuxBlockCandidate {
    std::shared_ptr<const CBlock> block; // complete child template, no auxpow attached
    uint256 hash;                        // block->GetHash(), cached for convenience (never depends on auxpow)
    uint256 prev_hash;                   // block->hashPrevBlock -- the tip this candidate builds on
    int height;                          // the height this block would have if accepted
    int64_t create_time;                 // GetTime() when this candidate was created
    CScript payout_script;               // metadata only -- not consulted by validation
};

/**
 * Thread-safe cache of outstanding AuxBlockCandidate entries, keyed by their
 * own child-block hash.
 *
 * Eviction policy (documented explicitly, per docs/AUXPOW_MILESTONE.md
 * sec.10 -- deliberately simple, not a redesign of classic pool-server
 * caching conventions, just made explicitly bounded/thread-safe/immutable
 * where the classic Namecoin/Dogecoin implementations were more ad hoc):
 *   - Bounded by count: at most MAX_CANDIDATES entries. Inserting past the
 *     cap evicts the OLDEST entry by insertion order (FIFO) -- matching the
 *     real, longstanding Namecoin/Dogecoin convention of a small, capped
 *     "recent candidates" list, made explicit and enforced here rather than
 *     left as an implementation detail.
 *   - Opportunistic tip-based pruning: every Insert() also drops any
 *     existing entry whose prev_hash no longer matches the block's own
 *     prev_hash's sibling-at-the-new-tip relationship is NOT checked here
 *     (this cache has no chain-state access of its own, by design -- see
 *     below); actual tip-staleness rejection happens at submission time in
 *     rpc/auxpow.cpp, which DOES have access to the active chain and
 *     compares candidate->prev_hash against the real current tip directly.
 *     This keeps the cache class itself simple, chain-state-free, and
 *     trivially unit-testable without needing a real ChainstateManager.
 *   - A candidate is NEVER invalidated merely because the mempool changed
 *     or a NEWER candidate was created for the SAME tip: multiple
 *     candidates for the same tip may coexist, and each remains submittable
 *     independently until the tip itself moves (checked at submission
 *     time) or it is evicted by the FIFO cap.
 *   - "Old work never silently maps to a different candidate": keys are
 *     real block hashes, which are cryptographically unique per distinct
 *     block content; an evicted key is simply gone (Get() returns nullptr,
 *     surfaced by the RPC as "unknown or stale candidate"), never reused
 *     for different content.
 */
class AuxBlockCandidateCache
{
public:
    static constexpr size_t MAX_CANDIDATES = 30;

    /** Inserts a new, already-fully-constructed candidate. Evicts the
     * oldest entry first if this would exceed MAX_CANDIDATES. */
    void Insert(std::shared_ptr<const AuxBlockCandidate> candidate) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Returns the cached candidate for `hash`, or nullptr if unknown/evicted.
     *
     * Returns a `shared_ptr<const AuxBlockCandidate>` deliberately: the
     * caller gets its own reference-counted handle to an object neither it
     * nor this cache will ever mutate, and can safely use it (including
     * reading `*candidate->block`) after this call returns, even if the
     * cache concurrently evicts its own copy of the entry. Callers must
     * still COPY `*candidate->block` into a new CBlock before attaching a
     * submitted auxpow proof -- never const_cast/mutate through this
     * pointer.
     */
    std::shared_ptr<const AuxBlockCandidate> Get(const uint256& hash) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    size_t Size() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

private:
    mutable Mutex m_mutex;
    std::map<uint256, std::shared_ptr<const AuxBlockCandidate>> m_candidates GUARDED_BY(m_mutex);
    std::deque<uint256> m_insertion_order GUARDED_BY(m_mutex);
};

/** The process-global candidate cache. Exposed here (not file-local to
 * auxpow.cpp) so tests can exercise it directly, including under real
 * concurrent access. */
AuxBlockCandidateCache& GetAuxBlockCandidateCache();

#endif // BITCOIN_RPC_AUXPOW_H
