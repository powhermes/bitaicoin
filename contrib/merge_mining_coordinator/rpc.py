#!/usr/bin/env python3
# Copyright (c) 2026 The BitAIcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""A minimal, dependency-free JSON-RPC client for talking to bitaicoind.

Deliberately does not import python-bitcoinrpc or anything from BitAIcoin
Core's own test framework -- an external pool integrator reading this
coordinator as a reference should see exactly how little is actually needed
to call createauxblock/submitauxblock: a plain HTTP POST with HTTP basic
auth and a JSON body, nothing more.
"""

from __future__ import annotations

import base64
import http.client
import json
import itertools
from typing import Any, List, Optional


class RPCError(Exception):
    def __init__(self, code: int, message: str):
        super().__init__(f"RPC error {code}: {message}")
        self.code = code
        self.message = message


class BitAIcoinRPC:
    """One connection to one bitaicoind RPC endpoint. Not thread-safe by
    itself -- callers that need concurrent requests should construct one
    instance per thread (this mirrors a real limitation of a single HTTP
    keep-alive connection, not anything specific to BitAIcoin)."""

    def __init__(self, host: str, port: int, user: str, password: str, timeout: float = 30.0):
        self._host = host
        self._port = port
        self._auth = base64.b64encode(f"{user}:{password}".encode()).decode()
        self._timeout = timeout
        self._id_counter = itertools.count(1)

    def call(self, method: str, params: Optional[List[Any]] = None) -> Any:
        body = json.dumps({
            "jsonrpc": "1.0",
            "id": next(self._id_counter),
            "method": method,
            "params": params or [],
        })
        conn = http.client.HTTPConnection(self._host, self._port, timeout=self._timeout)
        try:
            conn.request(
                "POST", "/", body,
                {"Authorization": f"Basic {self._auth}", "Content-Type": "text/plain"},
            )
            resp = conn.getresponse()
            data = json.loads(resp.read())
        finally:
            conn.close()
        if data.get("error"):
            raise RPCError(data["error"]["code"], data["error"]["message"])
        return data["result"]

    # Thin, explicit wrappers for the two frozen RPCs this coordinator is
    # scoped to. Deliberately not a generic __getattr__ proxy -- this file
    # is meant to be read, not just used, and the frozen contract is
    # exactly these two calls (docs/AUXPOW_MILESTONE.md sec.10-12).

    def createauxblock(self, payout_address: str) -> dict:
        return self.call("createauxblock", [payout_address])

    def submitauxblock(self, child_hash_hex: str, auxpow_hex: str) -> bool:
        return self.call("submitauxblock", [child_hash_hex, auxpow_hex])

    def getbestblockhash(self) -> str:
        return self.call("getbestblockhash")

    def getblockchaininfo(self) -> dict:
        return self.call("getblockchaininfo")

    def getnewaddress(self) -> str:
        return self.call("getnewaddress")

    def createwallet(self, name: str) -> dict:
        return self.call("createwallet", [name])

    def generatetoaddress(self, n: int, address: str) -> List[str]:
        return self.call("generatetoaddress", [n, address])
