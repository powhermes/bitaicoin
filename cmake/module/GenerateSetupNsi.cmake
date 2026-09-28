# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

function(generate_setup_nsi)
  set(abs_top_srcdir ${PROJECT_SOURCE_DIR})
  set(abs_top_builddir ${PROJECT_BINARY_DIR})
  set(CLIENT_URL ${PROJECT_HOMEPAGE_URL})
  # BitAIcoin: user-facing Windows executable/package identity (OUTPUT_NAME rename).
  # These must match the actual produced binaries ($<TARGET_FILE_NAME:...>), which
  # honor the CMake OUTPUT_NAME rebrand. CLIENT_TARNAME is also the registered URL
  # protocol scheme, so it must be the BitAIcoin payment scheme "bitaicoin".
  # test_bitcoin is an internal test binary and is NOT renamed.
  set(CLIENT_TARNAME "bitaicoin")
  set(BITCOIN_WRAPPER_NAME "bitaicoin")
  set(BITCOIN_GUI_NAME "bitaicoin-qt")
  set(BITCOIN_DAEMON_NAME "bitaicoind")
  set(BITCOIN_CLI_NAME "bitaicoin-cli")
  set(BITCOIN_TX_NAME "bitaicoin-tx")
  set(BITCOIN_WALLET_TOOL_NAME "bitaicoin-wallet")
  set(BITCOIN_TEST_NAME "test_bitcoin")
  set(EXEEXT ${CMAKE_EXECUTABLE_SUFFIX})
  configure_file(${PROJECT_SOURCE_DIR}/share/setup.nsi.in ${PROJECT_BINARY_DIR}/bitcoin-win64-setup.nsi USE_SOURCE_PERMISSIONS @ONLY)
endfunction()
