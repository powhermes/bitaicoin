#!/usr/bin/env python3
# Copyright (c) 2026 The BitAIcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Single-command integration demonstration for the BitAIcoin merge-mining
coordinator reference implementation.

    python3 contrib/merge_mining_coordinator/demo.py --regtest \\
        --rpcuser=x --rpcpassword=y --rpcport=18898

This is an INTEGRATION VALIDATION tool, not a production miner: it proves,
end to end, against a real running bitaicoind, that the frozen
createauxblock -> (build commitment) -> (mine parent) -> (build CAuxPow) ->
submitauxblock contract works exactly as documented, using nothing but this
coordinator's own public API (MergeMiningCoordinator.create_job() /
.submit_parent_solution()) and a synthetic (non-Bitcoin) parent chain -- see
docs/MERGE_MINING_INTEGRATION.md.

It does the SHA256d parent-mining loop itself (wire.solve_parent_header),
exactly as any other synthetic test client would -- that loop is
deliberately NOT part of MergeMiningCoordinator's own API (item 4: a real
pool does this work externally, on real hardware).
"""

from __future__ import annotations

import argparse
import json
import sys

from coordinator import MergeMiningCoordinator, SubmitStatus
from provider import ParentSolution, SyntheticParentWorkProvider
from rpc import BitAIcoinRPC, RPCError
from wire import solve_parent_header


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--regtest", action="store_true", help="informational only -- this tool always talks "
                                                             "plainly to whatever RPC endpoint it's given; "
                                                             "regtest is simply the intended target chain "
                                                             "for this demonstration")
    ap.add_argument("--rpchost", default="127.0.0.1")
    ap.add_argument("--rpcport", type=int, required=True)
    ap.add_argument("--rpcuser", required=True)
    ap.add_argument("--rpcpassword", required=True)
    ap.add_argument("--payout-address", default=None,
                     help="a valid BitAIcoin address for this network; if omitted, this demo will "
                          "create a throwaway wallet and address for convenience (regtest/test use only)")
    args = ap.parse_args()

    rpc = BitAIcoinRPC(args.rpchost, args.rpcport, args.rpcuser, args.rpcpassword)

    payout_address = args.payout_address
    if payout_address is None:
        try:
            rpc.createwallet("merge_mining_coordinator_demo")
        except RPCError as e:
            if "already exists" not in str(e) and "Database already exists" not in str(e):
                raise
        payout_address = rpc.getnewaddress()
        print(f"[demo] no --payout-address given; using throwaway address {payout_address}")

    provider = SyntheticParentWorkProvider()
    coordinator = MergeMiningCoordinator(rpc, provider, payout_address)

    print("[demo] requesting BitAIcoin aux work via createauxblock ...")
    job = coordinator.create_job()
    print("[demo] job (machine-readable work package):")
    print(json.dumps(job.to_dict(), indent=2))

    print(f"[demo] mining synthetic parent header to satisfy child target "
          f"(bits={job.child_bits}) -- this does NOT need real Bitcoin difficulty ...")
    solved_header = solve_parent_header(job.parent_candidate.header, job.child_target_numeric)
    print(f"[demo] parent header solved: nonce={solved_header.nonce}")

    solution = ParentSolution(
        header=solved_header,
        coinbase=job.parent_candidate.coinbase,
        coinbase_branch=job.parent_candidate.coinbase_branch,
        coinbase_index=job.parent_candidate.coinbase_index,
    )

    print("[demo] submitting solved AuxPoW via submitauxblock ...")
    result = coordinator.submit_parent_solution(job, solution)
    print(f"[demo] submit result: status={result.status.value} detail={result.detail!r}")

    if result.status != SubmitStatus.ACCEPTED:
        print("[demo] FAILED -- expected ACCEPTED", file=sys.stderr)
        return 1

    tip = rpc.getbestblockhash()
    if tip != job.child_hash:
        print(f"[demo] FAILED -- submitauxblock returned true but tip is {tip}, expected {job.child_hash}",
              file=sys.stderr)
        return 1

    print(f"BAIC AuxPoW accepted: {job.child_hash}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
