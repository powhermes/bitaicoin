// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TEST_BAICBRANDINGTESTS_H
#define BITCOIN_QT_TEST_BAICBRANDINGTESTS_H

#include <QObject>
#include <QTest>

/** BitAIcoin GUI branding assertions (currency unit display strings). */
class BAICBrandingTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void unitStrings();
};

#endif // BITCOIN_QT_TEST_BAICBRANDINGTESTS_H
