# Testnet Runbook

Exact, reproducible steps for standing up the Phase 1 BitAIcoin Synthetic
Lab network from scratch: the historical bootstrap import, and the
three-node live network with mining and wallet transfers. This is the
literal procedure used to produce the results in `PHASE1_REPORT.md`.

All paths below are examples under `~/Downloads/bitaicoin-dev/` (kept
entirely separate from any real Bitcoin Core installation on the same
machine — see `docs/ARCHITECTURE.md`'s bootstrap section for why this
separation matters). Adjust paths for your own machine; nothing else needs
to change.

## Prerequisites

- A build of this repo's `bitaicoin-phase1` branch (`cmake -B build
  -DBUILD_GUI=OFF -DENABLE_IPC=OFF && cmake --build build -j$(nproc)`; on
  macOS with Homebrew Boost/libevent/sqlite/pkgconf installed).
- Outbound internet access, for step 1's real Bitcoin P2P sync.
- Enough disk space for a ~225,430-block prefix of real Bitcoin history
  (the linearized+remagicked bootstrap file used here was
  6,348,228,602 bytes) plus 2-3x that for working copies across nodes.

## Step 1 — Independent scratch sync of real Bitcoin history (M0)

Sync an **unmodified** `bitcoind` (stock v31.1, not this fork — any
build works) against the real Bitcoin network, in its own throwaway
datadir, stopping exactly at height 225429:

```bash
mkdir -p ~/Downloads/bitaicoin-dev/m0-scratch-btc-sync
bitcoind -datadir=~/Downloads/bitaicoin-dev/m0-scratch-btc-sync \
  -stopatheight=225429 -server=1 -rpcuser=__cookie__ \
  -daemon=0
```

Wait for it to reach height 225429 and stop on its own (this is a real
sync of the first ~225k Bitcoin blocks; expect it to take a while
depending on peer availability — add `-addnode=<known-reliable-peer>` if
initial block discovery is slow). Confirm before proceeding:

```bash
bitcoin-cli -datadir=~/Downloads/bitaicoin-dev/m0-scratch-btc-sync getblockcount   # expect 225429
bitcoin-cli -datadir=~/Downloads/bitaicoin-dev/m0-scratch-btc-sync getbestblockhash # expect 0000000000000366ce98ca28338900094e8cbf445776253181749f782546d006
```

## Step 2 — Linearize into one ordered block file

Using the stock `contrib/linearize/linearize-data.py` tool already in this
tree, with a config like:

```ini
# ~/Downloads/bitaicoin-dev/m0-linearize-work/linearize.cfg
rpcuser=__cookie__
rpcpassword=<value from m0-scratch-btc-sync/.cookie>
host=127.0.0.1
port=<rpcport used above>

max_height=225429
min_height=0

netmagic=f9beb4d9
genesis=000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f
input=~/Downloads/bitaicoin-dev/m0-scratch-btc-sync/blocks
output_file=~/Downloads/bitaicoin-dev/m0-linearize-work/bitaicoin-genesis-225429.dat
hashlist=~/Downloads/bitaicoin-dev/m0-linearize-work/hashlist.txt

out_of_order_cache_sz = 200000000
```

**`netmagic` here must be Bitcoin's real magic (`f9beb4d9`), matching what
is actually on disk in `input=`** — this script uses `netmagic` to detect
which records to read, not what to write it out as. Setting it to
BitAIcoin's magic here causes every record to fail detection and the
script to fall into a byte-by-byte rescan (this was hit during
development and looked like the script hanging for 70+ minutes before
being root-caused).

```bash
cd ~/Downloads/bitaicoin-dev/bitcoin-core-v31.1/contrib/linearize
python3 linearize-hashes.py ~/Downloads/bitaicoin-dev/m0-linearize-work/linearize.cfg \
  > ~/Downloads/bitaicoin-dev/m0-linearize-work/hashlist.txt
python3 linearize-data.py ~/Downloads/bitaicoin-dev/m0-linearize-work/linearize.cfg
```

Output: `bitaicoin-genesis-225429.dat`, still framed with Bitcoin's real
`f9beb4d9` magic, containing exactly 225,430 blocks (genesis through
225429) in height order.

## Step 3 — Re-magic to BitAIcoin's network identity

A small purpose-written script (not part of stock `contrib/linearize`)
rewrites each length-prefixed record's magic bytes, using the record
framing (`magic[4] + size[4 LE] + payload[size]`) to find exact
boundaries rather than a blind search/replace:

```bash
python3 remagic.py \
  ~/Downloads/bitaicoin-dev/m0-linearize-work/bitaicoin-genesis-225429.dat \
  ~/Downloads/bitaicoin-dev/m0-linearize-work/bitaicoin-genesis-225429.remagicked.dat
```

This swaps `f9beb4d9` → `b778d811` (BitAIcoin's `pchMessageStart`, see
`docs/CHAIN_IDENTITY.md`) at every record boundary, verifying magic bytes
match expectations at each step and raising an error rather than silently
producing a corrupt file if anything is out of alignment. It should report
`remagicked 225430 records`.

## Step 4 — Import into a BitAIcoin datadir (Node A)

```bash
mkdir -p ~/Downloads/bitaicoin-dev/bitaicoin-datadir
bitcoind -chain=bitaicoin -datadir=~/Downloads/bitaicoin-dev/bitaicoin-datadir \
  -loadblock=~/Downloads/bitaicoin-dev/m0-linearize-work/bitaicoin-genesis-225429.remagicked.dat \
  -server=1 -maxconnections=0 -daemon=0
```

Let the import run to completion (it exits/returns to prompt when done
importing, or logs completion in `debug.log`), then verify before doing
anything else:

```bash
bitcoin-cli -chain=bitaicoin -datadir=~/Downloads/bitaicoin-dev/bitaicoin-datadir getblockcount
# expect 225430
bitcoin-cli -chain=bitaicoin -datadir=~/Downloads/bitaicoin-dev/bitaicoin-datadir getblockhash 225429
# expect 0000000000000366ce98ca28338900094e8cbf445776253181749f782546d006
```

This is Node A. **Do not mine on top of it until both checks above pass.**

## Step 5 — Seed Node B and Node C from Node A (not by re-importing)

Re-running the P2P import a second and third time is expensive and, on a
memory-constrained machine, was observed to fail non-deterministically
(the OS killed the import process after apparent successful completion,
under real memory pressure from other concurrently-running processes —
see `PHASE1_REPORT.md`'s Known Limitations). The reliable approach used
here: stop Node A cleanly, then copy its already-validated data directly:

```bash
bitcoin-cli -chain=bitaicoin -datadir=~/Downloads/bitaicoin-dev/bitaicoin-datadir stop
mkdir -p ~/Downloads/bitaicoin-dev/node-b/bitaicoin ~/Downloads/bitaicoin-dev/node-c/bitaicoin
cp -R ~/Downloads/bitaicoin-dev/bitaicoin-datadir/bitaicoin/blocks     ~/Downloads/bitaicoin-dev/node-b/bitaicoin/
cp -R ~/Downloads/bitaicoin-dev/bitaicoin-datadir/bitaicoin/chainstate ~/Downloads/bitaicoin-dev/node-b/bitaicoin/
cp -R ~/Downloads/bitaicoin-dev/bitaicoin-datadir/bitaicoin/blocks     ~/Downloads/bitaicoin-dev/node-c/bitaicoin/
cp -R ~/Downloads/bitaicoin-dev/bitaicoin-datadir/bitaicoin/chainstate ~/Downloads/bitaicoin-dev/node-c/bitaicoin/
```

## Step 6 — Start all three nodes as a private mesh

Ports used in this run: Node A = 28333/28332 (P2P/RPC), Node B =
28433/28432, Node C = 28533/28532. Each node connects only to the other
two, explicitly (no DNS seeds, no public peering):

```bash
bitcoind -chain=bitaicoin -datadir=~/Downloads/bitaicoin-dev/bitaicoin-datadir \
  -daemon -server=1 -listen=1 -port=28333 -rpcport=28332 -dbcache=300 \
  -maxconnections=32 -connect=127.0.0.1:28433 -connect=127.0.0.1:28533

bitcoind -chain=bitaicoin -datadir=~/Downloads/bitaicoin-dev/node-b \
  -daemon -server=1 -listen=1 -port=28433 -rpcport=28432 -dbcache=300 \
  -maxconnections=32 -connect=127.0.0.1:28333 -connect=127.0.0.1:28533

bitcoind -chain=bitaicoin -datadir=~/Downloads/bitaicoin-dev/node-c \
  -daemon -server=1 -listen=1 -port=28533 -rpcport=28532 -dbcache=300 \
  -maxconnections=32 -connect=127.0.0.1:28333 -connect=127.0.0.1:28433
```

**Use `-maxconnections=32`, not a low value like 8.** A first attempt at
8 left every node at 0 peer connections despite raw TCP reachability,
logging `"failed to find an eviction candidate - connection dropped
(full)"` under `-debug=net` — Bitcoin Core's internal inbound/outbound
slot reservations exhausted the small pool before the two intended
`-connect=` peers could both land. 32 leaves ample headroom for a 3-node
mesh (4 peers each: 2 manual outbound + 2 inbound, confirmed via
`getpeerinfo`).

Verify convergence:

```bash
bitcoin-cli -chain=bitaicoin -datadir=~/Downloads/bitaicoin-dev/bitaicoin-datadir getpeerinfo | grep -c '"id"'
# expect 4 on each node
```

## Step 7 — Mine past activation and confirm the difficulty transition

```bash
NODE_A="bitcoin-cli -chain=bitaicoin -datadir=~/Downloads/bitaicoin-dev/bitaicoin-datadir"
ADDR=$($NODE_A -rpcwallet=<wallet> getnewaddress "" legacy)   # legacy (P2PKH) -- see docs/CONSENSUS.md open question
$NODE_A generatetoaddress 1 "$ADDR"                            # mines block 225430 (activation)
$NODE_A getblock $($NODE_A getbestblockhash) | grep '"bits"'   # confirm easy activation target
```

## Step 8 — Mature a coinbase and test cross-node transfers (M4)

```bash
$NODE_A generatetoaddress 100 "$ADDR"    # maturity
ADDR_B=$(bitcoin-cli -chain=bitaicoin -datadir=~/Downloads/bitaicoin-dev/node-b -rpcwallet=<wallet> getnewaddress "" legacy)
$NODE_A -rpcwallet=<wallet> sendtoaddress "$ADDR_B" 1.0
$NODE_A generatetoaddress 1 "$ADDR"      # confirm it

# on Node B:
bitcoin-cli -chain=bitaicoin -datadir=~/Downloads/bitaicoin-dev/node-b -rpcwallet=<wallet> getbalance

# repeat B -> C similarly, then confirm all three nodes report the same
# getblockcount / getbestblockhash / getbalance-after-transfer.
```

**Use legacy (P2PKH) addresses, not bech32.** A bech32 (P2WPKH) address
produces a witness-carrying transaction that is rejected at this chain
height (`"unexpected-witness"`), since SegWit is inherited dormant — see
`docs/CONSENSUS.md`'s open question. If a wallet ever gets an
un-minable transaction stuck in its own auto-rebroadcast (it retries on
every load, re-poisoning the mempool even after `mempool.dat` is
cleared), abandon that wallet and create a fresh one rather than trying
to evict the specific transaction.

## Cleanup

```bash
for d in ~/Downloads/bitaicoin-dev/bitaicoin-datadir ~/Downloads/bitaicoin-dev/node-b ~/Downloads/bitaicoin-dev/node-c; do
  bitcoin-cli -chain=bitaicoin -datadir="$d" stop
done
```

Confirm each process has actually exited (`lsof -i :28332,28432,28532`)
before reusing those ports — a stale process from a prior run holding an
RPC port was hit once during development and needed a manual `kill`.
