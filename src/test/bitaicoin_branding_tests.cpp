// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <clientversion.h>
#include <policy/feerate.h>
#include <test/util/setup_common.h>

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

// BitAIcoin RC2 branding assertions.
//
// These deterministically pin the product/network identity so that a future
// accidental revert to upstream "Bitcoin Core"/"Satoshi" strings fails the
// test suite. They intentionally avoid any filesystem paths, timestamps, or
// build-output filenames (those are checked at package/archive-inspection time,
// not here) so the tests are not brittle.
//
// These are pure string/identity checks and touch no consensus code.

BOOST_AUTO_TEST_SUITE(bitaicoin_branding_tests)

BOOST_AUTO_TEST_CASE(client_name_is_bitaicoin)
{
    // Compiled product identity (help/version/GUI surfaces derive from this).
    BOOST_CHECK_EQUAL(std::string(CLIENT_NAME), std::string("BitAIcoin"));
}

BOOST_AUTO_TEST_CASE(ua_name_is_bitaicoin)
{
    // Technical P2P client identity reported in the 'version' message.
    BOOST_CHECK_EQUAL(UA_NAME, std::string("BitAIcoin"));
}

BOOST_AUTO_TEST_CASE(p2p_subversion_is_bitaicoin)
{
    // The network-visible subversion string. With the frozen RC1 numeric
    // client version (31.1.0) this is exactly "/BitAIcoin:31.1.0/".
    const std::string subversion = FormatSubVersion(UA_NAME, CLIENT_VERSION, std::vector<std::string>());
    BOOST_CHECK(subversion.rfind("/BitAIcoin:", 0) == 0); // starts with, robust to version bumps
    BOOST_CHECK_EQUAL(subversion, std::string("/BitAIcoin:31.1.0/"));
    // And must not advertise the upstream "Satoshi" identity.
    BOOST_CHECK(subversion.find("Satoshi") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(product_name_is_not_bitcoin)
{
    // CLIENT_NAME is the product identity embedded in every tool's --version
    // line (e.g. bitcoin-wallet/-tx/-util build their version string as
    // CLIENT_NAME + " <role> utility version ..."). Guard that the product name
    // is neither the upstream "Bitcoin Core" nor any "bitcoin-" form, so no
    // user-visible version line can describe the product as bitcoin-*.
    const std::string name{CLIENT_NAME};
    BOOST_CHECK(name.find("Bitcoin Core") == std::string::npos);
    BOOST_CHECK(name.find("bitcoin-") == std::string::npos);
    BOOST_CHECK(name.find("bitcoin") == std::string::npos); // case-sensitive: excludes lowercase upstream name
}

BOOST_AUTO_TEST_CASE(currency_unit_is_baic)
{
    // Daemon/CLI/RPC display unit label. Numeric amounts and the smallest-unit
    // terminology are unchanged; only the visible ticker label is BitAIcoin's.
    BOOST_CHECK_EQUAL(CURRENCY_UNIT, std::string("BAIC"));
    BOOST_CHECK_EQUAL(CURRENCY_ATOM, std::string("sat"));
}

BOOST_AUTO_TEST_SUITE_END()
