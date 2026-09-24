// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/params.h>

#include <cstdint>

class CBlockHeader;
class CBlockIndex;
class uint256;
class arith_uint256;

/**
 * Convert nBits value to target.
 *
 * @param[in] nBits     compact representation of the target
 * @param[in] pow_limit PoW limit (consensus parameter)
 *
 * @return              the proof-of-work target or nullopt if the nBits value
 *                      is invalid (due to overflow or exceeding pow_limit)
 */
std::optional<arith_uint256> DeriveTarget(unsigned int nBits, uint256 pow_limit);

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/**
 * BitAIcoin ASERT (`aserti3` arithmetic, 6-hour half-life -- NOT `aserti3-2d`;
 * that name specifically denotes BCH's own 2-day/172800-second half-life,
 * which BitAIcoin does not use -- see docs/AUXPOW_MILESTONE.md sec.9 for why
 * this naming distinction matters) target computation -- a direct, checked
 * C++ port of the already-validated arithmetic core from
 * `contrib/asert_halflife_simulation.py`/`contrib/asert_reference.py`
 * `calculate_asert()`, itself referenced against BCH/BCHN's real
 * `aserti3-2d` (docs/AUXPOW_MILESTONE.md sec.8.2/sec.9 -- the arithmetic is
 * derived from/validated against BCH's implementation; the frozen
 * production half-life is BitAIcoin's own, unrelated 21600-second choice).
 * Deliberately a pure function of its arguments -- no `CBlockIndex`/`Consensus::Params`
 * lookups here, so it is directly unit-testable with synthetic/extreme
 * inputs (clamp, overflow, extreme-schedule tests) without needing a real
 * chain. `GetNextWorkRequired()` resolves the real anchor/height/time
 * values from the active chain and calls this.
 *
 * `refTarget` is the ANCHOR block's own target (its `nBits`, converted).
 * `timeDiff` is `(block whose target is being computed)'s parent's time -
 * anchor's PARENT's time` (BCH convention: the time reference is the
 * anchor's parent, not the anchor itself). `heightDiff` is `(that same
 * parent)'s height - anchor's height`; the formula's own `+1` (baked into
 * the implementation, matching real BCH source) accounts for the block
 * actually being computed being one past that parent -- so for the first
 * ASERT block (at the activation height), whose parent IS the anchor,
 * `heightDiff == 0`.
 *
 * PRECONDITIONS (asserted, not just documented -- matching the Python
 * reference's own rigor): `0 < refTarget <= powLimit`; `powLimit`'s bit
 * length `<= 239` (proven exact safety bound for the `refTarget * factor`
 * multiply -- see the .cpp for the full derivation, already established
 * against BitAIcoin's real 228-bit powLimit); `heightDiff >= 0`.
 *
 * Result is always clamped to `[1, powLimit]`.
 */
arith_uint256 ComputeASERTTarget(const arith_uint256& refTarget, int64_t targetSpacing,
                                  int64_t timeDiff, int64_t heightDiff,
                                  const arith_uint256& powLimit, int64_t halfLife);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);
bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params&);

/**
 * Return false if the proof-of-work requirement specified by new_nbits at a
 * given height is not possible, given the proof-of-work on the prior block as
 * specified by old_nbits.
 *
 * This function only checks that the new value is within a factor of 4 of the
 * old value for blocks at the difficulty adjustment interval, and otherwise
 * requires the values to be the same.
 *
 * Always returns true on networks where min difficulty blocks are allowed,
 * such as regtest/testnet.
 */
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits);

#endif // BITCOIN_POW_H
