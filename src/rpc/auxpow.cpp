// Copyright (c) 2026 The BitAIcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/auxpow.h>

#include <arith_uint256.h>
#include <auxpow.h>
#include <bitcoin-build-config.h> // IWYU pragma: keep -- for CLIENT_NAME
#include <chain.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <interfaces/mining.h>
#include <key_io.h>
#include <node/context.h>
#include <node/miner.h>
#include <node/types.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <validation.h>
#include <validationinterface.h>

#include <memory>
#include <vector>

using interfaces::BlockTemplate;
using interfaces::Mining;
using node::NodeContext;

// --- AuxBlockCandidateCache ---

void AuxBlockCandidateCache::Insert(std::shared_ptr<const AuxBlockCandidate> candidate)
{
    LOCK(m_mutex);
    m_candidates[candidate->hash] = candidate;
    m_insertion_order.push_back(candidate->hash);
    while (m_insertion_order.size() > MAX_CANDIDATES) {
        const uint256 oldest = m_insertion_order.front();
        m_insertion_order.pop_front();
        // The same hash could in principle appear twice in the insertion
        // order (a candidate re-created with identical content -- possible
        // only if tip/mempool/payout are all byte-identical, which Insert()
        // does not special-case); only erase from the map if this was
        // genuinely the newest reference to that key, i.e. it isn't still
        // present later in the queue.
        if (std::find(m_insertion_order.begin(), m_insertion_order.end(), oldest) == m_insertion_order.end()) {
            m_candidates.erase(oldest);
        }
    }
}

std::shared_ptr<const AuxBlockCandidate> AuxBlockCandidateCache::Get(const uint256& hash) const
{
    LOCK(m_mutex);
    auto it = m_candidates.find(hash);
    if (it == m_candidates.end()) return nullptr;
    return it->second;
}

size_t AuxBlockCandidateCache::Size() const
{
    LOCK(m_mutex);
    return m_candidates.size();
}

AuxBlockCandidateCache& GetAuxBlockCandidateCache()
{
    static AuxBlockCandidateCache cache;
    return cache;
}

namespace {

/** Local state-catcher, mirroring rpc/mining.cpp's own submitblock_StateCatcher
 * exactly (that class is file-local there, not exported -- this is a
 * deliberate, small duplication of a ~10-line pattern, not a second
 * definition of any consensus rule). Captures the REAL validation result
 * ProcessNewBlock produces, so this file never has to guess at or
 * re-implement why a block was accepted or rejected. */
class AuxSubmitStateCatcher final : public CValidationInterface
{
public:
    uint256 hash;
    bool found{false};
    BlockValidationState state;

    explicit AuxSubmitStateCatcher(const uint256& hash_in) : hash(hash_in) {}

protected:
    void BlockChecked(const std::shared_ptr<const CBlock>& block, const BlockValidationState& state_in) override
    {
        if (block->GetHash() != hash) return;
        found = true;
        state = state_in;
    }
};

/** Chain-awareness/activation check shared by both RPCs
 * (docs/AUXPOW_MILESTONE.md sec.6/10): NEVER inferred from anything other
 * than the real, explicit consensus flags -- this is the one place both
 * createauxblock and submitauxblock enforce it, so they can't drift apart. */
void EnsureAuxPowActiveOrThrow(const Consensus::Params& params, int next_height)
{
    if (!params.fBitAIAuxpowEnabled) {
        throw JSONRPCError(RPC_MISC_ERROR,
            "AuxPoW is not enabled on this chain. This RPC only produces functioning "
            "merge-mining work on chains with AuxPoW consensus rules defined "
            "(the real BitAIcoin chain, or regtest for testing).");
    }
    if (next_height < params.BitAIAuxpowActivationHeight) {
        throw JSONRPCError(RPC_MISC_ERROR,
            strprintf("AuxPoW is not active yet on this chain (activates at height %d; the next "
                      "block would be height %d).",
                      params.BitAIAuxpowActivationHeight, next_height));
    }
}

RPCHelpMan createauxblock()
{
    return RPCHelpMan{
        "createauxblock",
        "Creates a new BitAIcoin block candidate for merge-mining (AuxPoW) and returns the data "
        "needed to embed a merge-mining commitment for it in a parent-chain coinbase.\n"
        "The candidate is cached; call submitauxblock with a solved AuxPoW proof to submit it.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
             "The BitAIcoin address that will receive the block subsidy and fees."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "hash", "hash of the created child block -- the parent-chain "
                 "coinbase's merge-mining commitment must commit to this value"},
                {RPCResult::Type::NUM, "chainid", "BitAIcoin's registered AuxPoW chain ID"},
                {RPCResult::Type::STR_HEX, "previousblockhash", "the active tip this candidate builds on"},
                {RPCResult::Type::NUM, "coinbasevalue", "subsidy plus fees, in satoshis"},
                {RPCResult::Type::STR_HEX, "bits", "compact representation of this candidate's required target"},
                {RPCResult::Type::NUM, "height", "the height this candidate would have if accepted"},
                {RPCResult::Type::STR_HEX, "target", "the full 256-bit required target, in the conventional "
                 "Namecoin/Dogecoin AuxPoW RPC byte order (raw internal bytes, hex-encoded with no "
                 "reversal -- the reverse of getblocktemplate's own big-endian \"target\" convention; "
                 "reverse this field's bytes to obtain that representation)"},
            }},
        RPCExamples{
            HelpExampleCli("createauxblock", "\"myaddress\"") +
            HelpExampleRpc("createauxblock", "\"myaddress\"")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            NodeContext& node = EnsureAnyNodeContext(request.context);
            ChainstateManager& chainman = EnsureChainman(node);
            const Consensus::Params& params = chainman.GetConsensus();

            CTxDestination destination = DecodeDestination(request.params[0].get_str());
            if (!IsValidDestination(destination)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                    "Error: Invalid address. createauxblock does not require or use a node "
                    "wallet -- pass any valid address for this network directly.");
            }
            const CScript payout_script = GetScriptForDestination(destination);

            Mining& miner = EnsureMining(node);

            std::shared_ptr<CBlock> pblock;
            uint256 prev_hash;
            int height;
            {
                LOCK(chainman.GetMutex());
                const CBlockIndex* tip = chainman.ActiveChain().Tip();
                CHECK_NONFATAL(tip);
                height = tip->nHeight + 1;
                // Chain-awareness/activation FIRST, before the readiness
                // check below -- matching the established, deliberate
                // ordering principle from submitauxblock's own equivalent
                // fix (docs/AUXPOW_MILESTONE.md sec.10.6: "the one place
                // both createauxblock and submitauxblock enforce it").
                // "This chain doesn't support AuxPoW at all" must always be
                // reported before "this chain isn't synced yet" -- a
                // disabled-chain probe (e.g. testnet4 in the functional test
                // suite, which also happens to be in IBD) must still see
                // the specific "AuxPoW is not enabled" message, not a
                // readiness error. Confirmed necessary by a real regression
                // (feature_auxpow_rpc_disabled_chains.py) that failed when
                // this ordering was tried the other way around first.
                EnsureAuxPowActiveOrThrow(params, height);

                // RPC readiness policy, NOT a cooldown/lock workaround (real
                // bug found via an actual regtest run, not by inspection --
                // see docs/AUXPOW_MILESTONE.md merge-mining milestone report
                // for the full writeup): every other real createNewBlock()
                // call site (generateblock, getblocktemplate,
                // generatetoaddress's underlying path -- rpc/mining.cpp)
                // explicitly passes /*cooldown=*/false; createauxblock
                // previously omitted it, silently inheriting cooldown=true's
                // default IBD-wait loop (node/interfaces.cpp:
                // `while (IsInitialBlockDownload()) { ... wait forever
                // ... }`, its own comment: "on regtest ... this would wait
                // forever if no block was mined in the past day"). On a
                // virgin regtest chain (height 0, ancient genesis timestamp)
                // that condition is permanently true, so the RPC worker
                // thread blocked forever and made the whole node
                // unresponsive to every other RPC too.
                //
                // The fix is deliberately NOT just adding cooldown=false
                // blindly (that alone would silently hand out mining work
                // from an actually-unsynced real production node).
                // getblocktemplate's own established pattern (rpc/mining.cpp)
                // exempts `miner.isTestChain()` chains from the IBD gate --
                // but that generic helper means "chain type != MAIN"
                // (kernel/chainparams.h), which is true for EVERY chain type
                // this fork adds, INCLUDING ChainType::BITAICOIN itself
                // (BitAIcoin's own real production chain is deliberately not
                // "MAIN" -- see chainparams.cpp). Reusing `isTestChain()`
                // here verbatim would therefore silently exempt real
                // BitAIcoin production nodes from this gate too, defeating
                // the whole point -- caught before shipping, not by
                // inspection alone. The only chain type where "virgin
                // chain, no warm-up block, must work immediately" is
                // actually the intended behavior is REGTEST specifically,
                // so that is checked explicitly by name instead. (By this
                // point, MAIN/TESTNET/SIGNET/TESTNET4 have already been
                // rejected above via fBitAIAuxpowEnabled -- this check only
                // ever matters for REGTEST vs. real BITAICOIN.) On the real
                // BitAIcoin chain, IBD is checked explicitly HERE, as
                // RPC-layer policy, and rejected fast and clearly -- never
                // silently handing out work from an incompletely-synced
                // tip, and never relying on createNewBlock's own internal
                // cooldown loop to make that decision.
                if (chainman.GetParams().GetChainType() != ChainType::REGTEST && miner.isInitialBlockDownload()) {
                    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                        CLIENT_NAME " is in initial sync and waiting for blocks...");
                }

                // include_dummy_extranonce=true: a real bug caught via an
                // actual regtest run, not by inspection -- without it, a
                // low-height coinbase's BIP34 height push alone can be a
                // single byte (e.g. OP_6 for height 6), one byte short of
                // the real consensus minimum coinbase scriptSig length (2
                // bytes), and the template's own internal self-check
                // rejects it as "bad-cb-length" before this RPC ever gets a
                // chance to return anything. Matches generateblock's/
                // generatetoaddress's own real, already-working use of this
                // same option for the identical reason.
                // cooldown=false: the RPC-readiness/IBD policy decision was
                // already made explicitly above, before any lock was taken
                // or any work begun -- createNewBlock's own internal
                // cooldown loop must never be the thing deciding whether
                // this RPC is allowed to produce work (see the comment
                // above `miner.isTestChain()`).
                std::unique_ptr<BlockTemplate> block_template{
                    miner.createNewBlock({.coinbase_output_script = payout_script, .include_dummy_extranonce = true},
                                          /*cooldown=*/false)};
                CHECK_NONFATAL(block_template);
                pblock = std::make_shared<CBlock>(block_template->getBlock());
                // Real bug found via an actual regtest run, not by
                // inspection: BlockTemplate::getBlock() does not itself
                // populate a ready-to-submit hashMerkleRoot (its own doc
                // comment's "dummy coinbase" warning extends to this too) --
                // every other real call site that uses getBlock() directly
                // (generateblock, after appending its own extra
                // transactions) explicitly calls RegenerateCommitments()
                // for exactly this reason; matched here even though this
                // RPC never modifies vtx itself, since getBlock() alone was
                // observed (via a real submitauxblock rejection,
                // "bad-txnmrklroot") to not have already done it.
                node::RegenerateCommitments(*pblock, chainman);
                prev_hash = pblock->hashPrevBlock;
            }
            CHECK_NONFATAL(!pblock->vtx.empty());

            // Mark this candidate AuxPoW-flagged using the already-frozen
            // helper -- never a manual OR of chain-ID/versionbits fields.
            // MakeAuxpowVersion() masks its base-version input to bits 0-7
            // unconditionally (primitives/block.h, fixed
            // docs/AUXPOW_MILESTONE.md sec.6), so passing the template's own
            // nVersion here is safe by construction even though BitAIcoin's
            // own versionbits deployments are permanently inactive (sec.5)
            // and this value is therefore always a small, fixed constant in
            // practice.
            pblock->nVersion = MakeAuxpowVersion(BITAI_AUXPOW_CHAIN_ID, pblock->nVersion);

            const uint256 hash = pblock->GetHash();

            auto candidate = std::make_shared<AuxBlockCandidate>(AuxBlockCandidate{
                .block = pblock,
                .hash = hash,
                .prev_hash = prev_hash,
                .height = height,
                .create_time = GetTime(),
                .payout_script = payout_script,
            });
            GetAuxBlockCandidateCache().Insert(candidate);

            const arith_uint256 target = arith_uint256().SetCompact(pblock->nBits);
            const uint256 target_u256 = ArithToUint256(target);
            // Conventional Namecoin/Dogecoin AuxPoW RPC byte order (fixed
            // 2026-09-24, docs/AUXPOW_MILESTONE.md sec.10.1/10.8): those
            // implementations return HexStr(BEGIN(target), END(target)) --
            // the RAW internal byte representation, hex-encoded with NO
            // reversal. That is the little-endian-relative-to-the-number
            // convention, the exact REVERSE of uint256/arith_uint256's own
            // GetHex() ("natural"/big-endian display convention, which is
            // what getblocktemplate's own "target" field uses). Confirmed
            // via a real regression: reversing this field's bytes reproduces
            // getblocktemplate's "target" exactly at the same tip
            // (feature_auxpow_rpc.py). A pool built against the
            // Namecoin/Dogecoin convention must see this field mean what it
            // already means there -- this is not a place to invent a
            // BitAIcoin-specific representation.
            const std::vector<unsigned char> target_bytes(target_u256.begin(), target_u256.end());

            UniValue result(UniValue::VOBJ);
            result.pushKV("hash", hash.GetHex());
            result.pushKV("chainid", BITAI_AUXPOW_CHAIN_ID);
            result.pushKV("previousblockhash", prev_hash.GetHex());
            result.pushKV("coinbasevalue", pblock->vtx[0]->vout[0].nValue);
            result.pushKV("bits", strprintf("%08x", pblock->nBits));
            result.pushKV("height", height);
            result.pushKV("target", HexStr(target_bytes));
            return result;
        },
    };
}

RPCHelpMan submitauxblock()
{
    return RPCHelpMan{
        "submitauxblock",
        "Submits a solved AuxPoW proof for a child block candidate previously returned by "
        "createauxblock.\n",
        {
            {"hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "hash of the candidate, as returned by createauxblock"},
            {"auxpow", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "hex-encoded serialized AuxPoW proof"},
        },
        RPCResult{RPCResult::Type::BOOL, "", "whether the candidate was accepted"},
        RPCExamples{
            HelpExampleCli("submitauxblock", "\"hash\" \"auxpowhex\"") +
            HelpExampleRpc("submitauxblock", "\"hash\", \"auxpowhex\"")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            NodeContext& node = EnsureAnyNodeContext(request.context);
            ChainstateManager& chainman = EnsureChainman(node);
            const Consensus::Params& params = chainman.GetConsensus();

            const uint256 hash{ParseHashV(request.params[0], "hash")};

            // Chain-awareness/activation FIRST, before any decode work --
            // matching EnsureAuxPowActiveOrThrow's own doc comment ("the one
            // place both createauxblock and submitauxblock enforce it, so
            // they can't drift apart") and createauxblock's own real
            // ordering. Originally this check ran after the auxpow-hex
            // decode and candidate-cache lookup, which meant a probe against
            // a disabled/ordinary chain surfaced a generic decode or
            // unknown-candidate error instead of the specific, actionable
            // "AuxPoW is not enabled" message -- caught via a real
            // functional-test run against testnet4
            // (feature_auxpow_rpc_disabled_chains.py), not by inspection.
            //
            // Idempotent-retry check, same lock scope (fixed 2026-09-24,
            // docs/AUXPOW_MILESTONE.md sec.11.2/11.7): if `hash` is ALREADY a
            // fully-validated, ACTUAL AuxPoW block on the current active
            // chain -- from an earlier call to this exact submitauxblock
            // whose real acceptance happened but whose RPC response was lost
            // to a client-side timeout/retry, or a second racing submission
            // for a still-current candidate -- report success immediately.
            // This runs deliberately before the auxpow-hex decode and the
            // candidate-cache lookup below, so a retry succeeds even if the
            // resubmitted hex is imperfect or the original cache entry has
            // since been evicted: once a hash is truly accepted on the
            // active chain, nothing about how it was originally submitted
            // matters anymore. A pool must be able to safely retry
            // submitauxblock without tracking whether an earlier attempt's
            // response actually arrived -- this is the ONE case where this
            // RPC returns `true` without re-running full validation on the
            // just-submitted proof, and it does so only by trusting the
            // node's own already-recorded validation result for this exact
            // hash, never the newly-submitted bytes.
            //
            // Narrowed 2026-09-24 (a real gap found before it shipped, not
            // by inspection alone -- the original version accepted ANY
            // already-valid active-chain block, including a perfectly
            // ordinary DIRECT-mined post-activation block, which would let
            // `submitauxblock <direct-block-hash> <anything>` incorrectly
            // return true even though that block was never an AuxPoW
            // submission at all): the existing block must ALSO genuinely be
            // an AuxPoW block on THIS chain -- `IsRealAuxpow()` (the same
            // shared helper the compact-block gates use, sec.7) checked
            // against its own real nVersion, defensively re-confirmed by
            // height (a pre-activation block can never legitimately carry
            // the AuxPoW bit at all, but this makes the height requirement
            // explicit rather than relying solely on IsRealAuxpow's own
            // internal consistency).
            {
                LOCK(chainman.GetMutex());
                const CBlockIndex* tip = chainman.ActiveChain().Tip();
                CHECK_NONFATAL(tip);
                EnsureAuxPowActiveOrThrow(params, tip->nHeight + 1);

                if (const CBlockIndex* existing = chainman.m_blockman.LookupBlockIndex(hash);
                    existing && chainman.ActiveChain().Contains(existing) &&
                    existing->IsValid(BLOCK_VALID_SCRIPTS) &&
                    existing->nHeight >= params.BitAIAuxpowActivationHeight &&
                    IsRealAuxpow(params.fBitAIAuxpowEnabled, existing->nVersion)) {
                    return true;
                }
            }

            // Strict structural decode ONLY (docs/AUXPOW_MILESTONE.md
            // sec.10 item 6) -- uses CAuxPow's own existing production
            // SERIALIZE_METHODS directly (src/auxpow.h). No second AuxPoW
            // parser. Truncated encodings, oversized branch vectors, and
            // malformed indices all throw std::ios_base::failure from
            // within that real, already-tested deserializer; trailing
            // garbage after an otherwise-valid proof is checked explicitly
            // here, since the generic (de)serializer has no reason to check
            // for bytes after the type it was asked to read.
            if (!IsHex(request.params[1].get_str())) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "auxpow must be a hex string");
            }
            const std::vector<unsigned char> auxpow_data{ParseHex(request.params[1].get_str())};
            auto proof = std::make_shared<CAuxPow>();
            try {
                SpanReader reader{auxpow_data};
                reader >> *proof;
                if (proof->vMerkleBranch.size() > MAX_MERKLE_BRANCH_LENGTH ||
                    proof->vChainMerkleBranch.size() > MAX_MERKLE_BRANCH_LENGTH) {
                    throw std::ios_base::failure("merkle branch implausibly long");
                }
                if (reader.size() != 0) {
                    throw std::ios_base::failure("trailing garbage after auxpow");
                }
            } catch (const std::exception& e) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("AuxPoW decode failed: %s", e.what()));
            }

            auto candidate = GetAuxBlockCandidateCache().Get(hash);
            if (!candidate) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown or evicted candidate hash");
            }

            std::shared_ptr<CBlock> block_copy;
            {
                LOCK(chainman.GetMutex());
                const CBlockIndex* tip = chainman.ActiveChain().Tip();
                CHECK_NONFATAL(tip);
                EnsureAuxPowActiveOrThrow(params, tip->nHeight + 1);
                if (tip->GetBlockHash() != candidate->prev_hash) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER,
                        "Candidate is stale: the active tip has advanced since this candidate "
                        "was created. Call createauxblock again for a fresh candidate.");
                }
                // Copy the immutable cached candidate -- never mutate the
                // cached entry itself (docs/AUXPOW_MILESTONE.md sec.10
                // item 5): a bad proof, or a second concurrent submission
                // for the same hash, must never be able to corrupt what
                // other callers (or a later retry) see for this candidate.
                block_copy = std::make_shared<CBlock>(*candidate->block);
            }
            block_copy->auxpow = std::move(proof);
            // The child hash must never change once a proof is attached --
            // GetHash() covers only the 6 base header fields, never auxpow
            // (primitives/block.h) -- checked here as a live invariant, not
            // just asserted in a comment.
            CHECK_NONFATAL(block_copy->GetHash() == hash);

            bool new_block{false};
            auto catcher = std::make_shared<AuxSubmitStateCatcher>(hash);
            CHECK_NONFATAL(chainman.m_options.signals)->RegisterSharedValidationInterface(catcher);
            // min_pow_checked=true, matching submitblock's own real
            // convention exactly (docs/AUXPOW_MILESTONE.md sec.10 item 7):
            // this RPC performs no redundant PoW/consensus pre-check of its
            // own. The REAL pipeline -- CheckBitAIProofOfWork(),
            // CheckAuxPowRules(), CAuxPow::Check(), BIP34, transaction/
            // script/UTXO validation, witness commitments, block weight --
            // remains the sole, authoritative source of validity.
            const bool accepted = chainman.ProcessNewBlock(block_copy, /*force_processing=*/true,
                                                            /*min_pow_checked=*/true, &new_block);
            CHECK_NONFATAL(chainman.m_options.signals)->UnregisterSharedValidationInterface(catcher);

            if (!new_block && accepted) {
                // A narrower duplicate case than the early idempotent-retry
                // check above: two submissions racing so closely that BOTH
                // passed that check before either's ProcessNewBlock call
                // completed. ProcessNewBlock itself recognizes the block is
                // already known/valid; the child chain now has this block
                // either way, so treat as success -- matching the
                // conventional simple boolean interface's own
                // "duplicate/replayed accepted block handled cleanly"
                // requirement.
                return true;
            }
            if (!catcher->found) {
                // Genuinely inconclusive (matches submitblock's own real
                // "inconclusive" case) -- the conventional two-call
                // interface has no third state, so this is reported as
                // failure rather than a silently-optimistic success.
                return false;
            }
            return catcher->state.IsValid();
        },
    };
}

} // namespace

void RegisterAuxPowRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"mining", &createauxblock},
        {"mining", &submitauxblock},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
