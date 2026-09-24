// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <script/verify_flags.h>
#include <uint256.h>

#include <array>
#include <chrono>
#include <limits>
#include <map>
#include <vector>

namespace Consensus {

/**
 * A buried deployment is one where the height of the activation has been hardcoded into
 * the client implementation long after the consensus change has activated. See BIP 90.
 */
enum BuriedDeployment : int16_t {
    // buried deployments get negative values to avoid overlap with DeploymentPos
    DEPLOYMENT_HEIGHTINCB = std::numeric_limits<int16_t>::min(),
    DEPLOYMENT_CLTV,
    DEPLOYMENT_DERSIG,
    DEPLOYMENT_CSV,
    DEPLOYMENT_SEGWIT,
};
constexpr bool ValidDeployment(BuriedDeployment dep) { return dep <= DEPLOYMENT_SEGWIT; }

enum DeploymentPos : uint16_t {
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_TAPROOT, // Deployment of Schnorr/Taproot (BIPs 340-342)
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in deploymentinfo.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};
constexpr bool ValidDeployment(DeploymentPos dep) { return dep < MAX_VERSION_BITS_DEPLOYMENTS; }

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit{28};
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime{NEVER_ACTIVE};
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout{NEVER_ACTIVE};
    /** If lock in occurs, delay activation until at least this block
     *  height.  Note that activation will only occur on a retarget
     *  boundary.
     */
    int min_activation_height{0};
    /** Period of blocks to check signalling in (usually retarget period, ie params.DifficultyAdjustmentInterval()) */
    uint32_t period{2016};
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t threshold{1916};

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;

    /** Special value for nStartTime indicating that the deployment is never active.
     *  This is useful for integrating the code changes for a new feature
     *  prior to deploying it on some or all networks. */
    static constexpr int64_t NEVER_ACTIVE = -2;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /**
     * Hashes of blocks that
     * - are known to be consensus valid, and
     * - buried in the chain, and
     * - fail if the default script verify flags are applied.
     */
    std::map<uint256, script_verify_flags> script_flag_exceptions;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int CSVHeight;
    /** Block height at which Segwit (BIP141, BIP143 and BIP147) becomes active.
     * Note that segwit v0 script rules are enforced on all blocks except the
     * BIP 16 exception blocks. */
    int SegwitHeight;
    /** Don't warn about unknown BIP 9 activations below this height.
     * This prevents us from warning about the CSV and segwit activations. */
    int MinBIP9WarningHeight;
    std::array<BIP9Deployment,MAX_VERSION_BITS_DEPLOYMENTS> vDeployments;
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    /**
      * Enforce BIP94 timewarp attack mitigation. On testnet4 this also enforces
      * the block storm mitigation.
      */
    bool enforce_BIP94;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    std::chrono::seconds PowTargetSpacing() const
    {
        return std::chrono::seconds{nPowTargetSpacing};
    }
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    /** The best chain should have at least this much work */
    uint256 nMinimumChainWork;
    /** By default assume that the signatures in ancestors of this block are valid */
    uint256 defaultAssumeValid;

    /**
     * BitAIcoin fork parameters (see doc/bitaicoin/CONSENSUS.md). All default to
     * "off" (INT_MAX / zero) so every chain except BitAIcoin is provably
     * unaffected by these fields' existence.
     *
     * BitAIForkAnchorHeight/Hash: the real Bitcoin mainnet block required as
     * BitAIcoin's historical ancestor. Enforced once, in
     * ContextualCheckBlockHeader, so no alternative history can masquerade
     * as BitAIcoin's pre-fork past.
     *
     * BitAIActivationHeight: the first BitAIcoin-native block (one past the
     * anchor). Gates the one-time easy-difficulty transition (GetNextWorkRequired)
     * and the replay-protection / Taproot-spend-rejection rules (CheckInputScripts).
     *
     * BitAIActivationPowLimit: the target used for exactly the activation
     * block itself; every block after it resumes Bitcoin's ordinary 2016-block
     * retarget algorithm using `powLimit` as the floor.
     *
     * BitAIForkId: nonzero constant folded into the legacy/BIP143 sighash for
     * every input spent at height >= BitAIActivationHeight, making BitAIcoin
     * signatures unconditionally invalid under Bitcoin's sighash and vice versa.
     *
     * BitAIHistoricalPowLimit: real Bitcoin mainnet's original (strict) powLimit,
     * used ONLY inside CalculateNextWorkRequired's retarget-clamp for retargets
     * below BitAIActivationHeight. `powLimit` itself is loosened chain-wide (see
     * its assignment in kernel/chainparams.cpp) so BitAIcoin's own post-activation
     * blocks can mine easily -- but real historical retargets that were themselves
     * clamped to mainnet's tight ceiling (e.g. Bitcoin's very first retarget, real
     * height 2016, where the raw computed target exceeded mainnet's powLimit and
     * got clamped back down to it) must be recomputed against that SAME tight
     * ceiling, or the recomputed nBits will not match the real, already-recorded
     * historical value and ContextualCheckBlockHeader will reject the block as
     * "bad-diffbits" -- see docs/CONSENSUS.md. Every other chain leaves
     * BitAIActivationHeight at INT_MAX, so the height gate that selects this field
     * is never reached there and behavior is unchanged.
     */
    int BitAIForkAnchorHeight{std::numeric_limits<int>::max()};
    uint256 BitAIForkAnchorHash{};
    int BitAIActivationHeight{std::numeric_limits<int>::max()};
    uint256 BitAIActivationPowLimit{};
    uint32_t BitAIForkId{0};
    uint256 BitAIHistoricalPowLimit{};

    /**
     * fBitAIAuxpowEnabled: whether THIS CHAIN TYPE has AuxPoW semantics
     * defined AT ALL. Deliberately a separate, explicit flag from
     * BitAIAuxpowActivationHeight below -- an activation height of INT_MAX
     * means "permanently pre-activation" (nVersion bit 8 IS meaningful on
     * this chain, but AuxPoW is not yet allowed at any reachable height), NOT
     * "AuxPoW does not exist here." Real, found-in-review bug this flag
     * fixes (2026-09-23, docs/AUXPOW_MILESTONE.md sec.6): every AuxPoW-aware
     * code path (CheckBitAIProofOfWork's dispatch, the auxpow-aware
     * (de)serialization functions in auxpow.h, net_processing's relay,
     * node/blockstorage's disk I/O) inferred "this header carries a CAuxPow
     * payload" directly from nVersion bit 8 (IsAuxpowVersion()) with NO
     * chain-type check at all -- correct on the real BitAIcoin chain (and
     * REGTEST, used deliberately to test it), but WRONG on ordinary
     * MAIN/TESTNET/TESTNET4/SIGNET-style chains, where bit 8 is just an
     * ordinary version bit with its own historical meaning and must never be
     * interpreted as "an AuxPoW proof follows." Every AuxPoW-aware code path
     * must check this flag FIRST, before ever calling IsAuxpowVersion() to
     * mean anything; see the flag's own real enforcement points listed at
     * each call site.
     *
     * BitAIAuxpowActivationHeight: a SEPARATE, later milestone from
     * BitAIActivationHeight above -- not to be confused with it. That field
     * gates the one-time Bitcoin-to-BitAIcoin fork transition (225430, long
     * since passed); this one gates AuxPoW (merge-mining) support, height
     * 227808 on the real BitAIcoin chain, enforced via CheckAuxPowRules()
     * (src/auxpow.h) from ContextualCheckBlockHeader -- but ONLY when
     * fBitAIAuxpowEnabled is also true; see that function's own call site.
     * See docs/AUXPOW_MILESTONE.md sec.0 for the permanent policy this
     * height activates (both direct SHA256d mining AND AuxPoW valid forever,
     * never AuxPoW-only).
     */
    bool fBitAIAuxpowEnabled{false};
    int BitAIAuxpowActivationHeight{std::numeric_limits<int>::max()};

    /**
     * BitAIASERTActivationHeight / BitAIASERTHalfLife: the ASERT (`aserti3-2d`,
     * BCH/BCHN-shape) difficulty-adjustment activation -- a SEPARATE, later
     * milestone from BOTH BitAIActivationHeight (the original fork
     * transition) AND BitAIAuxpowActivationHeight/fBitAIAuxpowEnabled above,
     * even though both this and AuxPoW are intended to activate at the same
     * real production height (227808). Deliberately NOT inferred from
     * fBitAIAuxpowEnabled or from nVersion's AuxPoW bit: direct-mined and
     * AuxPoW-mined BitAIcoin blocks must receive EXACTLY the same required
     * nBits after activation -- the proof mechanism must never affect
     * difficulty, so this must be its own independent gate, checked in
     * GetNextWorkRequired() (src/pow.cpp) purely by height, with no
     * reference to nVersion at all.
     *
     * Defaults (INT_MAX / 0) so every other chain, and BitAIcoin below this
     * height, is provably unaffected -- ordinary legacy-DAA behavior is
     * completely unchanged there. `BitAIASERTHalfLife` is 0 (meaningless)
     * whenever `BitAIASERTActivationHeight == INT_MAX`; a real chain that
     * sets one MUST set the other, non-zero.
     *
     * ANCHOR CONVENTION (BCH ASERT, followed exactly -- see
     * docs/AUXPOW_MILESTONE.md's ASERT spec section): the anchor is the
     * block immediately BEFORE `BitAIASERTActivationHeight` (i.e. the last
     * block validated under the legacy DAA); its own nBits is the anchor
     * target; the time reference is the anchor's PARENT's timestamp, not
     * the anchor's own. Resolved deterministically FROM THE ACTIVE CHAIN,
     * BY HEIGHT (`pindexLast->GetAncestor(BitAIASERTActivationHeight - 1)`)
     * every time -- never a hardcoded hash/time, since for the real
     * production activation (227808) that block does not exist yet at the
     * time this code is written; it can only be hardcoded later, once the
     * activation chain is actually mined and frozen.
     */
    int BitAIASERTActivationHeight{std::numeric_limits<int>::max()};
    int64_t BitAIASERTHalfLife{0};

    /**
     * If true, witness commitments contain a payload equal to a Bitcoin Script solution
     * to the signet challenge. See BIP325.
     */
    bool signet_blocks{false};
    std::vector<uint8_t> signet_challenge;

    int DeploymentHeight(BuriedDeployment dep) const
    {
        switch (dep) {
        case DEPLOYMENT_HEIGHTINCB:
            return BIP34Height;
        case DEPLOYMENT_CLTV:
            return BIP65Height;
        case DEPLOYMENT_DERSIG:
            return BIP66Height;
        case DEPLOYMENT_CSV:
            return CSVHeight;
        case DEPLOYMENT_SEGWIT:
            return SegwitHeight;
        } // no default case, so the compiler can warn about missing cases
        return std::numeric_limits<int>::max();
    }
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
