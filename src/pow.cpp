// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/check.h>

#include <cstdlib>

// BitAIcoin ASERT: fixed-point radix, matching the validated
// contrib/asert_reference.py exactly (RADIX = 2**16). File-scope, used only
// by ComputeASERTTarget() below.
static constexpr int64_t RADIX = 65536;

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // BitAIcoin one-time activation difficulty transition: the first
    // BitAIcoin-native block gets a fresh, easy, lab-appropriate target,
    // independent of whatever difficulty real Bitcoin history had reached
    // at the fork anchor. Every block after it falls through to the
    // ordinary logic below unmodified -- it simply inherits this block's
    // nBits (like any other non-retarget-boundary height) until the next
    // natural 2016-block boundary, at which point normal retargeting
    // resumes using consensus.powLimit as the floor. Every other chain
    // leaves BitAIActivationHeight at INT_MAX, so this is unreachable there.
    if (pindexLast->nHeight + 1 == params.BitAIActivationHeight) {
        return UintToArith256(params.BitAIActivationPowLimit).GetCompact();
    }

    // BitAIcoin ASERT DAA activation -- a SEPARATE, later milestone from
    // BitAIActivationHeight above and from AuxPoW's own activation, even
    // though both this and AuxPoW are intended for the same real
    // production height (227808). Deliberately gated ONLY by height --
    // never by fBitAIAuxpowEnabled or any nVersion bit
    // (docs/AUXPOW_MILESTONE.md sec.8.2): direct-mined and AuxPoW-mined
    // blocks must receive EXACTLY the same required nBits, so the proof
    // mechanism must never be consulted here. Every other chain leaves
    // BitAIASERTActivationHeight at INT_MAX, so this is unreachable there --
    // ordinary DAA behavior is completely unchanged. Placed BEFORE the
    // legacy "only retarget every 2016 blocks" branch below: ASERT
    // retargets every single block once active, not just at interval
    // boundaries.
    if (params.BitAIASERTActivationHeight != std::numeric_limits<int>::max() &&
        pindexLast->nHeight + 1 >= params.BitAIASERTActivationHeight) {
        // Anchor = the last legacy-DAA block, i.e. the block immediately
        // before activation. Resolved DETERMINISTICALLY BY HEIGHT from the
        // real active chain -- never hardcoded (for the real 227808
        // production activation, that block does not exist yet at the time
        // this code is written; it can only be hardcoded later, once the
        // activation chain is actually mined and frozen).
        const CBlockIndex* pindexAnchor = pindexLast->GetAncestor(params.BitAIASERTActivationHeight - 1);
        assert(pindexAnchor); // guaranteed by the height check above
        assert(pindexAnchor->pprev); // activation height is always > 0 in every real chain
        arith_uint256 refTarget;
        refTarget.SetCompact(pindexAnchor->nBits);
        // BCH ASERT convention, exactly: the time reference is the anchor's
        // PARENT's timestamp, not the anchor's own.
        const int64_t nTimeReference = pindexAnchor->pprev->GetBlockTime();
        const int64_t timeDiff = pindexLast->GetBlockTime() - nTimeReference;
        const int64_t heightDiff = pindexLast->nHeight - pindexAnchor->nHeight;
        const arith_uint256 nextTarget = ComputeASERTTarget(refTarget, params.nPowTargetSpacing,
                                                             timeDiff, heightDiff,
                                                             UintToArith256(params.powLimit),
                                                             params.BitAIASERTHalfLife);
        return nextTarget.GetCompact();
    }

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then it MUST be a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    // BitAIcoin: retargets for real historical blocks below the activation height
    // must clamp against real Bitcoin mainnet's original (strict) powLimit, not
    // BitAIcoin's own loosened chain-wide ceiling, to reproduce the real,
    // already-recorded historical nBits values wherever the real retarget was
    // itself clamped to mainnet's ceiling -- see BitAIHistoricalPowLimit's comment
    // in consensus/params.h and docs/CONSENSUS.md. Every other chain leaves
    // BitAIActivationHeight at INT_MAX, so this is unreachable/unaffected there.
    const bool use_historical_pow_limit = params.BitAIActivationHeight != std::numeric_limits<int>::max()
        && pindexLast->nHeight + 1 < params.BitAIActivationHeight;
    const arith_uint256 bnPowLimit = UintToArith256(use_historical_pow_limit ? params.BitAIHistoricalPowLimit : params.powLimit);
    arith_uint256 bnNew;

    // Special difficulty rule for Testnet4
    if (params.enforce_BIP94) {
        // Here we use the first block of the difficulty period. This way
        // the real difficulty is always preserved in the first block as
        // it is not allowed to use the min-difficulty exception.
        int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
        const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
        bnNew.SetCompact(pindexFirst->nBits);
    } else {
        bnNew.SetCompact(pindexLast->nBits);
    }

    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    if (height % params.DifficultyAdjustmentInterval() == 0) {
        int64_t smallest_timespan = params.nPowTargetTimespan/4;
        int64_t largest_timespan = params.nPowTargetTimespan*4;

        // BitAIcoin: same historical/loosened split as CalculateNextWorkRequired's
        // retarget clamp (see BitAIHistoricalPowLimit's comment in consensus/params.h)
        // -- this function is also called from headerssync.cpp's presync/redownload
        // anti-DoS difficulty-transition check, and using the loosened chain-wide
        // powLimit there for a real historical (pre-fork) height widens its tolerance
        // window past what real Bitcoin's own historical security margin intended.
        // Every other chain leaves BitAIActivationHeight at INT_MAX, so this is
        // unreachable/unaffected there.
        const bool use_historical_pow_limit = params.BitAIActivationHeight != std::numeric_limits<int>::max()
            && height < params.BitAIActivationHeight;
        const arith_uint256 pow_limit = UintToArith256(use_historical_pow_limit ? params.BitAIHistoricalPowLimit : params.powLimit);
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // Calculate the largest difficulty value possible:
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= largest_timespan;
        largest_difficulty_target /= params.nPowTargetTimespan;

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        // Calculate the smallest difficulty value possible:
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= smallest_timespan;
        smallest_difficulty_target /= params.nPowTargetTimespan;

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
}

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    if (EnableFuzzDeterminism()) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit))
        return {};

    return bnTarget;
}

bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    auto bnTarget{DeriveTarget(nBits, params.powLimit)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

arith_uint256 ComputeASERTTarget(const arith_uint256& refTarget, int64_t targetSpacing,
                                  int64_t timeDiff, int64_t heightDiff,
                                  const arith_uint256& powLimit, int64_t halfLife)
{
    assert(refTarget > 0 && refTarget <= powLimit);
    assert(heightDiff >= 0);
    assert(halfLife > 0);
    // Proven exact safety bound (contrib/asert_reference.py's
    // _prove_powlimit_bound(), re-derived, not re-trusted, this pass):
    // refTarget*factor (factor <= FACTOR_MAX = 131071 = 2^17-1) fits in 256
    // bits with no silent wraparound for every refTarget up to powLimit iff
    // powLimit's bit length is <= 239. BitAIcoin's real powLimit is 228
    // bits (11 bits of real margin). This assert is the live, permanent
    // enforcement of that precondition -- not just a comment -- checked on
    // every real call, not only in the reference script.
    assert((powLimit >> 239) == 0);
    // Matches the reference's own explicit overflow-magnitude bound so the
    // `* RADIX` multiply below can't silently wrap a real int64_t either.
    assert(std::abs(timeDiff - targetSpacing * heightDiff) < (int64_t{1} << (63 - 16)));

    // C++'s integer division already truncates toward zero (since C++11),
    // which is exactly the semantics the Python reference had to manually
    // reconstruct from Python's floor-based divmod (see its own comment) --
    // no correction step needed here.
    const int64_t numer = (timeDiff - targetSpacing * (heightDiff + 1)) * RADIX;
    const int64_t exponent = numer / halfLife;

    int64_t shifts = exponent >> 16; // arithmetic (sign-extending) shift, universal on real platforms (C++20 well-defined)
    // Low 16 bits of exponent's two's-complement representation, matching
    // Python's `exponent & 0xFFFF` (which produces the same bit pattern for
    // a negative operand under Python's own two's-complement-like `&`).
    const uint16_t frac = static_cast<uint16_t>(static_cast<uint64_t>(exponent) & 0xFFFF);

    // Real, C++-specific arithmetic-safety finding made during this port
    // (docs/AUXPOW_MILESTONE.md sec.8.2): the Python reference's ints are
    // arbitrary-precision, so this polynomial never needed a fixed width
    // there. Computed exactly for the worst case (frac=65535): the raw sum
    // is 18,446,563,080,438,344,768 -- this OVERFLOWS int64_t (max
    // ~9.22e18) but fits uint64_t (max ~1.84e19, ~1.8e14 of real headroom).
    // Every intermediate term below is therefore computed in uint64_t, not
    // int64_t -- a silent int64_t overflow here would be undefined behavior
    // and/or a wrong, wrapped `factor`, not a merely-imprecise one.
    const uint64_t frac64 = frac;
    const uint64_t poly = 195766423245049ULL * frac64
                         + 971821376ULL * frac64 * frac64
                         + 5127ULL * frac64 * frac64 * frac64
                         + (uint64_t{1} << 47);
    const uint32_t factor = 65536u + static_cast<uint32_t>(poly >> 48);
    assert(factor <= 131071u); // FACTOR_MAX, proven exact maximum (contrib/asert_reference.py)

    // Safe by the powLimit>>239==0 precondition asserted above: refTarget <=
    // powLimit (<=239 bits) times factor (<=131071, 17 bits) fits in <=256
    // bits with no wraparound -- arith_uint256::operator*=(uint32_t) is
    // exactly the fixed-width multiply BCHN/Decred's own C++ performs here.
    arith_uint256 nextTarget = refTarget;
    nextTarget *= factor;

    shifts -= 16;
    if (shifts <= 0) {
        nextTarget >>= static_cast<unsigned int>(-shifts);
    } else {
        const arith_uint256 shifted = nextTarget << static_cast<unsigned int>(shifts);
        // Overflow check: does shifting back down recover the pre-shift
        // value? If not, the left shift silently dropped high bits -- clamp
        // to powLimit rather than use the corrupted result (matching the
        // reference's own explicit check, not a new behavior).
        if ((shifted >> static_cast<unsigned int>(shifts)) != nextTarget) {
            nextTarget = powLimit;
        } else {
            nextTarget = shifted;
        }
    }

    if (nextTarget == 0) {
        nextTarget = 1;
    } else if (nextTarget > powLimit) {
        nextTarget = powLimit;
    }
    return nextTarget;
}
