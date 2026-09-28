// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/baicbrandingtests.h>

#include <qt/bitcoinunits.h>

#include <QString>

// The wallet must display BitAIcoin/BAIC, never Bitcoin/BTC, while preserving the
// inherited smallest-unit terminology (sat). Internal enum names are unchanged.
void BAICBrandingTests::unitStrings()
{
    QCOMPARE(BitcoinUnits::longName(BitcoinUnit::BTC), QString("BAIC"));
    QCOMPARE(BitcoinUnits::longName(BitcoinUnit::mBTC), QString("mBAIC"));
    QCOMPARE(BitcoinUnits::shortName(BitcoinUnit::SAT), QString("sat"));
    // uBTC long name is "µBAIC (bits)"
    QVERIFY(BitcoinUnits::longName(BitcoinUnit::uBTC).contains("BAIC"));
    QVERIFY(BitcoinUnits::longName(BitcoinUnit::uBTC).contains("bits"));
    // No user-visible "BTC"/"Bitcoin" in the coin/unit labels.
    QVERIFY(!BitcoinUnits::longName(BitcoinUnit::BTC).contains("BTC"));
    QVERIFY(!BitcoinUnits::description(BitcoinUnit::BTC).contains("Bitcoin"));
}
