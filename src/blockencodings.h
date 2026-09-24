// Copyright (c) 2016-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCKENCODINGS_H
#define BITCOIN_BLOCKENCODINGS_H

#include <crypto/siphash.h>
#include <primitives/block.h>

#include <functional>

class CTxMemPool;
class BlockValidationState;
namespace Consensus {
struct Params;
};

// Transaction compression schemes for compact block relay can be introduced by writing
// an actual formatter here.
using TransactionCompression = DefaultFormatter;

class DifferenceFormatter
{
    uint64_t m_shift = 0;

public:
    template<typename Stream, typename I>
    void Ser(Stream& s, I v)
    {
        if (v < m_shift || v >= std::numeric_limits<uint64_t>::max()) throw std::ios_base::failure("differential value overflow");
        WriteCompactSize(s, v - m_shift);
        m_shift = uint64_t(v) + 1;
    }
    template<typename Stream, typename I>
    void Unser(Stream& s, I& v)
    {
        uint64_t n = ReadCompactSize(s);
        m_shift += n;
        if (m_shift < n || m_shift >= std::numeric_limits<uint64_t>::max() || m_shift < std::numeric_limits<I>::min() || m_shift > std::numeric_limits<I>::max()) throw std::ios_base::failure("differential value overflow");
        v = I(m_shift++);
    }
};

class BlockTransactionsRequest {
public:
    // A BlockTransactionsRequest message
    uint256 blockhash;
    std::vector<uint16_t> indexes;

    SERIALIZE_METHODS(BlockTransactionsRequest, obj)
    {
        READWRITE(obj.blockhash, Using<VectorFormatter<DifferenceFormatter>>(obj.indexes));
    }
};

class BlockTransactions {
public:
    // A BlockTransactions message
    uint256 blockhash;
    std::vector<CTransactionRef> txn;

    BlockTransactions() = default;
    explicit BlockTransactions(const BlockTransactionsRequest& req) :
        blockhash(req.blockhash), txn(req.indexes.size()) {}

    SERIALIZE_METHODS(BlockTransactions, obj)
    {
        READWRITE(obj.blockhash, TX_WITH_WITNESS(Using<VectorFormatter<TransactionCompression>>(obj.txn)));
    }
};

// Dumb serialization/storage-helper for CBlockHeaderAndShortTxIDs and PartiallyDownloadedBlock
struct PrefilledTransaction {
    // Used as an offset since last prefilled tx in CBlockHeaderAndShortTxIDs,
    // as a proper transaction-in-block-index in PartiallyDownloadedBlock
    uint16_t index;
    CTransactionRef tx;

    SERIALIZE_METHODS(PrefilledTransaction, obj) { READWRITE(COMPACTSIZE(obj.index), TX_WITH_WITNESS(Using<TransactionCompression>(obj.tx))); }
};

typedef enum ReadStatus_t
{
    READ_STATUS_OK,
    READ_STATUS_INVALID, // Invalid object, peer is sending bogus crap
    READ_STATUS_FAILED, // Failed to process object
} ReadStatus;

class CBlockHeaderAndShortTxIDs {
    mutable std::optional<PresaltedSipHasher> m_hasher;
    uint64_t nonce;

    void FillShortTxIDSelector() const;

    friend class PartiallyDownloadedBlock;

protected:
    std::vector<uint64_t> shorttxids;
    std::vector<PrefilledTransaction> prefilledtxn;

public:
    static constexpr int SHORTTXIDS_LENGTH = 6;

    CBlockHeader header;

    /**
     * Dummy for deserialization
     */
    CBlockHeaderAndShortTxIDs() = default;

    /**
     * @param[in]  nonce  This should be randomly generated, and is used for the siphash secret key
     */
    CBlockHeaderAndShortTxIDs(const CBlock& block, uint64_t nonce);

    uint64_t GetShortID(const Wtxid& wtxid) const;

    size_t BlockTxCount() const { return shorttxids.size() + prefilledtxn.size(); }

    SERIALIZE_METHODS(CBlockHeaderAndShortTxIDs, obj)
    {
        // AuxPoW addition, REVERTED to plain (2026-09-23,
        // docs/AUXPOW_MILESTONE.md sec.6): `obj.header` is intentionally
        // serialized with the ordinary, always-stock CBlockHeader format
        // here, unconditionally, on every chain -- never an auxpow payload.
        // This type's SERIALIZE_METHODS is a generic template with no chain
        // context available to check (`Using<>()`'s static dispatch cannot
        // carry a runtime "is AuxPoW enabled here" flag either), so it
        // cannot safely decide "bit 8 means a proof follows" the way the
        // chain-aware call sites elsewhere (auxpow.h's AuxPowBlockForSend/
        // Recv, AuxPowHeadersForAnnounce) do. AuxPoW-flagged blocks are
        // therefore deliberately never relayed via compact blocks (BIP152)
        // on an AuxPoW-enabled chain -- see the real send-site gates in
        // net_processing.cpp (ProcessGetData's CMPCTBLK branch and the
        // announce-new-tip loop), which fall back to full, chain-aware
        // BLOCK/HEADERS relay instead. A real, disclosed simplification
        // (compact-block bandwidth savings do not apply to AuxPoW blocks),
        // not a gap: nothing here can misinterpret bit 8 on any chain.
        READWRITE(obj.header, obj.nonce, Using<VectorFormatter<CustomUintFormatter<SHORTTXIDS_LENGTH>>>(obj.shorttxids), obj.prefilledtxn);
        if (ser_action.ForRead()) {
            if (obj.BlockTxCount() > std::numeric_limits<uint16_t>::max()) {
                throw std::ios_base::failure("indexes overflowed 16 bits");
            }
            obj.FillShortTxIDSelector();
        }
    }
};

class PartiallyDownloadedBlock {
protected:
    std::vector<CTransactionRef> txn_available;
    size_t prefilled_count = 0, mempool_count = 0, extra_count = 0;
    const CTxMemPool* pool;
public:
    CBlockHeader header;

    // Can be overridden for testing
    using IsBlockMutatedFn = std::function<bool(const CBlock&, bool)>;
    IsBlockMutatedFn m_check_block_mutated_mock{nullptr};

    explicit PartiallyDownloadedBlock(CTxMemPool* poolIn) : pool(poolIn) {}

    // extra_txn is a list of extra transactions to look at, in <witness hash, reference> form
    ReadStatus InitData(const CBlockHeaderAndShortTxIDs& cmpctblock, const std::vector<std::pair<Wtxid, CTransactionRef>>& extra_txn);
    bool IsTxAvailable(size_t index) const;
    // segwit_active enforces witness mutation checks just before reporting a healthy status
    ReadStatus FillBlock(CBlock& block, const std::vector<CTransactionRef>& vtx_missing, bool segwit_active);
};

#endif // BITCOIN_BLOCKENCODINGS_H
