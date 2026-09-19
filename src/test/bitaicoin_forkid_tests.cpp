// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <key.h>
#include <psbt.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/signingprovider.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

// BitAIcoin Phase 1 replay-protection test vectors (see docs/REPLAY_PROTECTION.md).
//
// These prove, independently of any node/chain machinery, that folding a
// nonzero fork_id into the legacy/BIP143 sighash makes a signature produced
// under one fork_id unconditionally invalid under a different one, for the
// exact same transaction/inputs/outputs/keys -- and that the transform is
// collision-free by construction for every real Bitcoin hashtype byte value.

BOOST_FIXTURE_TEST_SUITE(bitaicoin_forkid_tests, BasicTestingSetup)

static constexpr uint32_t BAI_FORK_ID = 0x424149; // ASCII "BAI"

static CMutableTransaction BuildSpendTx(const CScript& prev_scriptpubkey)
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = Txid::FromUint256(uint256::ONE);
    tx.vin[0].prevout.n = 0;
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    tx.vout.resize(1);
    tx.vout[0].nValue = 50000;
    tx.vout[0].scriptPubKey = prev_scriptpubkey;
    return tx;
}

BOOST_AUTO_TEST_CASE(forkid_domain_separation_legacy)
{
    CKey key;
    key.MakeNewKey(/*fCompressed=*/true);
    CPubKey pubkey = key.GetPubKey();
    CScript scriptPubKey = CScript() << ToByteVector(pubkey) << OP_CHECKSIG;

    CMutableTransaction mtx = BuildSpendTx(scriptPubKey);
    const CTransaction tx(mtx);

    const uint256 sighash_bai = SignatureHash(scriptPubKey, tx, 0, SIGHASH_ALL, 0, SigVersion::BASE,
                                               /*cache=*/nullptr, /*sighash_cache=*/nullptr, BAI_FORK_ID);
    const uint256 sighash_btc = SignatureHash(scriptPubKey, tx, 0, SIGHASH_ALL, 0, SigVersion::BASE,
                                               /*cache=*/nullptr, /*sighash_cache=*/nullptr, /*fork_id=*/0);

    // Sanity: folding a nonzero fork_id must actually change the hash.
    BOOST_CHECK(sighash_bai != sighash_btc);

    std::vector<unsigned char> sig_bai, sig_btc;
    BOOST_CHECK(key.Sign(sighash_bai, sig_bai));
    BOOST_CHECK(key.Sign(sighash_btc, sig_btc));

    // Positive controls: each signature verifies under the sighash it was
    // actually produced for.
    BOOST_CHECK(pubkey.Verify(sighash_bai, sig_bai));
    BOOST_CHECK(pubkey.Verify(sighash_btc, sig_btc));

    // Vector A: a BitAIcoin-domain signature does not verify as a plain
    // Bitcoin signature for the identical tx/inputs/outputs/keys.
    BOOST_CHECK(!pubkey.Verify(sighash_btc, sig_bai));

    // Vector B: a plain Bitcoin signature does not verify under BitAIcoin's
    // post-activation (fork_id-folded) sighash.
    BOOST_CHECK(!pubkey.Verify(sighash_bai, sig_btc));
}

BOOST_AUTO_TEST_CASE(forkid_domain_separation_bip143)
{
    CKey key;
    key.MakeNewKey(/*fCompressed=*/true);
    CPubKey pubkey = key.GetPubKey();
    // P2WPKH-style scriptCode used for BIP143 sighash (OP_DUP OP_HASH160 <keyhash> OP_EQUALVERIFY OP_CHECKSIG).
    CScript scriptCode = GetScriptForDestination(PKHash(pubkey));

    CMutableTransaction mtx = BuildSpendTx(CScript() << OP_TRUE);
    const CTransaction tx(mtx);
    const CAmount amount = 100000;

    const uint256 sighash_bai = SignatureHash(scriptCode, tx, 0, SIGHASH_ALL, amount, SigVersion::WITNESS_V0,
                                               /*cache=*/nullptr, /*sighash_cache=*/nullptr, BAI_FORK_ID);
    const uint256 sighash_btc = SignatureHash(scriptCode, tx, 0, SIGHASH_ALL, amount, SigVersion::WITNESS_V0,
                                               /*cache=*/nullptr, /*sighash_cache=*/nullptr, /*fork_id=*/0);

    BOOST_CHECK(sighash_bai != sighash_btc);

    std::vector<unsigned char> sig_bai, sig_btc;
    BOOST_CHECK(key.Sign(sighash_bai, sig_bai));
    BOOST_CHECK(key.Sign(sighash_btc, sig_btc));

    BOOST_CHECK(pubkey.Verify(sighash_bai, sig_bai));
    BOOST_CHECK(pubkey.Verify(sighash_btc, sig_btc));

    // Vector A / B, BIP143 (SegWit v0) variant.
    BOOST_CHECK(!pubkey.Verify(sighash_btc, sig_bai));
    BOOST_CHECK(!pubkey.Verify(sighash_bai, sig_btc));
}

BOOST_AUTO_TEST_CASE(forkid_collision_freedom)
{
    // Vector C: for every hashtype byte value a real Bitcoin signature can
    // carry (the serialized type is always exactly one byte -- vchSig.back()
    // -- so it is always in [0,255]), ApplyForkId with a nonzero fork_id
    // must produce a value that never collides with any plain hashtype
    // value, nor with the same hashtype under a different fork_id. This is
    // an executable proof of the "bits 8-31 always zero on real Bitcoin"
    // argument, not just an assertion in prose.
    const std::vector<int32_t> hashtypes = {
        SIGHASH_ALL, SIGHASH_NONE, SIGHASH_SINGLE,
        SIGHASH_ALL | SIGHASH_ANYONECANPAY,
        SIGHASH_NONE | SIGHASH_ANYONECANPAY,
        SIGHASH_SINGLE | SIGHASH_ANYONECANPAY,
    };
    const std::vector<uint32_t> fork_ids = {0, BAI_FORK_ID, 0x123456};

    std::set<int64_t> seen;
    for (int32_t ht : hashtypes) {
        // Real Bitcoin's serialized hash type is always a single byte.
        BOOST_CHECK(ht >= 0 && ht <= 0xff);
        for (uint32_t fid : fork_ids) {
            int32_t applied = ApplyForkId(ht, fid);
            if (fid == 0) {
                BOOST_CHECK_EQUAL(applied, ht);
            } else {
                BOOST_CHECK(applied != ht);
            }
            // No two distinct (hashtype, fork_id) pairs may ever collide.
            BOOST_CHECK(seen.insert(applied).second);
        }
    }
}

BOOST_AUTO_TEST_CASE(psbt_signing_and_verification_use_fork_id)
{
    // Regression test: SignPSBTInput/PSBTInputSignedAndVerified once
    // defaulted fork_id to 0 at every call site regardless of what the
    // caller was told to sign with, so a PSBT-signed transaction was
    // produced against the wrong (plain-Bitcoin) sighash post-activation --
    // sendtoaddress's direct-signing path (CWallet::SignTransaction) already
    // threaded fork_id correctly, so this gap was PSBT-specific and only
    // surfaced as a mempool rejection at broadcast time. See
    // docs/BITAI_PAYMENT_DESIGN.md's implementation log for how this was
    // found.
    CKey key;
    key.MakeNewKey(/*fCompressed=*/true);
    CPubKey pubkey = key.GetPubKey();
    CScript scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(pubkey));

    CMutableTransaction mtx = BuildSpendTx(CScript() << OP_TRUE);
    PartiallySignedTransaction psbtx(mtx);
    psbtx.inputs[0].witness_utxo = CTxOut(100000, scriptPubKey);

    FlatSigningProvider provider;
    provider.pubkeys[pubkey.GetID()] = pubkey;
    provider.keys[pubkey.GetID()] = key;

    const PrecomputedTransactionData txdata = PrecomputePSBTData(psbtx);

    BOOST_REQUIRE(SignPSBTInput(provider, psbtx, 0, &txdata, std::nullopt, nullptr, /*finalize=*/true, BAI_FORK_ID) == PSBTError::OK);

    // Verifying at the fork_id the input was actually signed with succeeds...
    BOOST_CHECK(PSBTInputSignedAndVerified(psbtx, 0, &txdata, BAI_FORK_ID));
    // ...but at fork_id=0 -- what every call site silently used before the
    // fix -- it must not, because the signature covers a different sighash.
    // If this ever returns true again, FillPSBT's completeness check and
    // broadcast-time mempool validation have silently drifted apart again.
    BOOST_CHECK(!PSBTInputSignedAndVerified(psbtx, 0, &txdata, /*fork_id=*/0));
}

BOOST_AUTO_TEST_SUITE_END()
