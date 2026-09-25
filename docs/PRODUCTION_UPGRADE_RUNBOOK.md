# BitAIcoin production-upgrade runbook (DESIGN ONLY — do not execute)

Target: upgrade every real/private BitAIcoin production-capable node to release candidate
`46a5c65cea` **while the real chain is still below height 227808**. This document is a procedure to
review; it is not to be run until explicitly authorized.

**Golden rule:** do not let a mixed old/new production fleet cross activation (227808). Every
production-capable node upgrades and converges to an identical tip *before* production is allowed to
advance toward 227808.

---

## Phase A — Pre-upgrade (per node)

1. **Record current chain state** (keep the output):
   - `bitaicoin-cli getblockcount`
   - `bitaicoin-cli getbestblockhash`
   - `bitaicoin-cli getblockchaininfo` → note `blocks`, `bestblockhash`, `chainwork`, `pruned`,
     `size_on_disk`, `verificationprogress`.
2. **Record the running binary identity:**
   - `bitaicoind -version` (record full version line)
   - SHA-256 of the currently-installed `bitaicoind` and `bitaicoin-cli` on disk.
3. **Confirm height is safely below activation:** current `blocks` must be `< 227808`. If any node is
   at/above 227808 already, STOP — this runbook's assumptions no longer hold; escalate.
4. **Clean shutdown:** `bitaicoin-cli stop`; wait for the process to exit and for
   `bitcoind`/`bitaicoind` to release the datadir lock. Confirm no lingering process.
5. **Backup / snapshot the datadir** (node stopped):
   - Snapshot the entire datadir (`blocks/`, `chainstate/`, `banlist`, `peers.dat`, config).
   - Store the snapshot on separate media, labeled with node id + height + hash + timestamp.
6. **Preserve the wallet separately:** copy the wallet(s) (`wallets/` dir, or legacy `wallet.dat`) to
   a distinct, access-controlled backup location — independent of the datadir snapshot.
7. **Verify the snapshot** before proceeding: confirm snapshot size/manifest matches source; ideally
   test-restore into a throwaway datadir on a spare host and confirm it opens read-only.

## Phase B — Install (per node)

8. **Install the candidate release binaries** (`bitaicoind`, `bitaicoin-cli`) built from
   `46a5c65cea` (or the official reproducibly-packaged build once produced — record which).
9. **Make no consensus/config parameter changes.** Do NOT alter activation height, chain ID, ASERT,
   AuxPoW settings, BIP34, checkpoints, `nMinimumChainWork`, ports, or network params. Leave
   `bitcoin.conf` unchanged except as strictly required for the binary path.
10. **Retain the existing datadir** (do not wipe; do not force reindex).
11. **Start the node:** launch the new binary against the existing datadir. Watch startup logs for a
    clean load (no unexpected reindex, no consensus error, no "corrupt block database" prompt).

## Phase C — Post-upgrade verification, BEFORE any mining (per node)

12. **Same tip:** `getblockcount` and `getbestblockhash` equal the pre-upgrade values (Phase A step 1).
13. **Same chainwork:** `getblockchaininfo.chainwork` unchanged.
14. **Wallet availability:** `listwallets` / `getwalletinfo` show the expected wallet(s) loaded and
    balances intact.
15. **Peers:** `getpeerinfo` shows expected peer connections re-establishing; `getnetworkinfo` sane.
16. **No unexpected reindex:** logs show normal load from existing chainstate, not a full rescan/reindex
    (a full reindex here would indicate a problem — investigate before continuing).
17. **Activation gating sanity (below 227808):** confirm `createauxblock` activation gating behaves
    correctly for the current pre-activation height (AuxPoW acceptance still gated off until 227808), and
    that ordinary direct mining/template RPC (`getblocktemplate` / `getmininginfo`) responds sanely.
    **Do not actually advance production** unless explicitly authorized.

## Phase D — Multi-node gate (fleet-wide)

18. Every production-capable node has completed Phases A–C successfully.
19. Every node reports an **identical current tip and chainwork**.
20. **Obsolete binaries removed** from all production service (no pre-activation binary can be
    accidentally restarted into the fleet).
21. Only when 18–20 hold is production allowed to continue toward 227808. Until then, hold production
    height steady.

**Do not allow a mixed old/new production fleet to cross activation.**
