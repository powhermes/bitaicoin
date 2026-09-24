# BitAIcoin merge-mining integration guide

This document explains, from an external SHA256d pool operator's
perspective, how to merge-mine BitAIcoin using only the frozen public RPC
contract (`createauxblock` / `submitauxblock`) -- the same contract
documented in `docs/AUXPOW_MILESTONE.md` sec.10-13, and exercised end to end
by the reference implementation in `contrib/merge_mining_coordinator/`.

**Scope**: this is an integration guide and a reference implementation, not
a mining pool. It answers exactly one question: *can an external SHA256d
pool obtain BAIC work, embed it correctly, and return a valid proof through
the frozen public RPC API?* It does not cover Stratum, share accounting,
payouts, worker authentication, or running a real Bitcoin Core node as your
parent chain (see sec.8, "future work").

## 1. The two-call contract

```
createauxblock <payout_address>          -- get a BitAIcoin work package
    (build your own parent-chain merged-mining commitment)
    (do your real SHA256d work on your own parent chain)
submitauxblock <child_hash> <auxpow_hex> -- submit your solved proof
```

`createauxblock` returns:

| field | type | meaning |
|---|---|---|
| `hash` | hex, 64 chars | the BAIC child block's hash -- the value your parent coinbase must commit to |
| `chainid` | number | always `16969` |
| `previousblockhash` | hex, 64 chars | the BAIC tip this candidate builds on |
| `coinbasevalue` | number | BAIC subsidy + fees, in satoshis |
| `bits` | hex, 8 chars | BAIC's compact required target |
| `height` | number | the height this candidate would have if accepted |
| `target` | hex, 64 chars | the full 256-bit required target, in the **conventional Namecoin/Dogecoin AuxPoW RPC byte order** -- see sec.2, do not skip this |

`submitauxblock` returns `true` only on real acceptance (including a clean
idempotent resubmission of an already-accepted child -- sec.5), `false`
for a well-formed proof that real BitAIcoin consensus validation rejected,
or a distinct `JSONRPCError` for malformed input, an unknown/evicted
candidate, or a chain that isn't ready (sec.6, sec.7).

## 2. Byte order -- read this before writing any code

Every hash-like value in this system has one of **three different, and
mutually incompatible, byte-order conventions**. Getting this wrong is the
single most common way to build an integration that fails silently or
rejects everything.

1. **Displayed hash convention** (`createauxblock`'s own `hash` and
   `previousblockhash` fields, and everything `bitaicoin-cli`/block
   explorers show you): raw internal bytes, **reversed**, then hex-encoded.
   This is `uint256::GetHex()`'s own convention.
2. **`createauxblock`'s own `target` field**: the conventional
   Namecoin/Dogecoin AuxPoW RPC byte order -- raw internal bytes,
   hex-encoded with **no reversal** (`HexStr(BEGIN(target), END(target))`
   in those codebases). This is the exact byte-reversal of convention 3
   below, and is deliberately **not** the same as convention 1.
3. **`getblocktemplate`'s own `target` field**: big-endian/"natural"
   display convention (`arith_uint256::GetHex()`) -- the same shape as
   convention 1, but describing a *target*, not a *hash*.
4. **Raw wire bytes inside the merge-mining tag and the AuxPoW proof
   itself** (the aux-chain merkle root, merkle branch entries, a block
   header's `hashPrevBlock`/`hashMerkleRoot`): raw internal bytes, **no
   reversal** -- the same convention as #2, since this is the actual
   on-the-wire representation everything else is derived from.

Worked, verified example (captured from a real regtest run --
`contrib/merge_mining_coordinator/demo.py`):

```
createauxblock "target" : 0000000000000000000000000000000000000000000000000000000000ffff7f
reverse those bytes     : 7fffff0000000000000000000000000000000000000000000000000000000000
getblocktemplate "target": 7fffff0000000000000000000000000000000000000000000000000000000000   <- matches
```

```python
# contrib/merge_mining_coordinator/wire.py's own helpers do this for you:
numeric_target = target_from_createauxblock_hex(createauxblock_result["target"])  # int, ready to compare a parent hash against
gbt_style_hex   = reverse_hex_bytes(createauxblock_result["target"])              # matches getblocktemplate's own convention
```

**Never guess a reversal. Never assume two hex strings that "look like a
hash" share a byte order just because they're both 64 hex characters.**

## 3. Step by step (a pool operator's perspective)

**A. Call `createauxblock <your_BAIC_payout_address>`.** No wallet is
required or used by this RPC -- any valid address for the target network
works. Store the entire response; you'll need `hash`, `previousblockhash`,
`bits`, and `target` again later.

**B. Decode the target correctly.** Use convention #2 above
(`target_from_createauxblock_hex`), not a naive `int(target_hex, 16)` (which
silently gives you the wrong number, byte-reversed). Independently
cross-check against `bits` (`compact_to_target(int(bits, 16))`) -- they
must describe the identical number; if they ever don't, that is a real bug
in the frozen RPC contract, not your integration (see
`docs/AUXPOW_MILESTONE.md` sec.11.1 for the exact regression that once
caught this class of bug).

**C. Commit the BAIC child hash into your own parent coinbase's merged-mining
tree.** For BitAIcoin's current, real, single-aux-chain mode (tree_size=1 --
sec.6), the "tree" is trivial: the aux merkle root **is** the child hash
directly, the branch is empty, and the index is 0. Build the tag:

```
fabe6d6d || aux_merkle_root (32 bytes, raw LE) || tree_size (4 bytes, LE) || nonce (4 bytes, LE)
```

using convention #4's byte order for `aux_merkle_root` (i.e. the SAME raw
bytes `hash_from_hex(createauxblock_hash)` produces once you have the
integer -- do not hex-reverse it again). Insert these exact bytes anywhere
in your own parent coinbase transaction's first input's scriptSig. `nonce`
is your own choice (any uint32); it only affects which chain-merkle-tree
slot a future multi-chain setup would assign you (sec.6) -- for tree_size=1
it has no effect on validation at all.

**D. Mine normal SHA256d parent work**, exactly as you would for your own
chain, with your own real parent-chain difficulty. The parent header's own
claimed `bits`/difficulty is **never checked** by BitAIcoin -- only the
parent header's actual hash matters, and only against BitAIcoin's own
target from step B.

**E. When a parent header's hash satisfies the BitAIcoin target (not your
parent chain's own target -- these are two independent, unrelated
thresholds), construct the `CAuxPow` proof**:
- the solved parent block header (all 6 fields, real proof-of-work nonce)
- your parent coinbase transaction (no witness data)
- the coinbase's merkle branch within your parent block, and its index
  (0, if your parent coinbase is transaction index 0, which it always is)
- the aux-chain merkle branch (`[]` for tree_size=1) and index (`0` for
  tree_size=1)

Serialize these in this exact order (`coinbaseTx`, `vMerkleBranch`,
`nIndex`, `vChainMerkleBranch`, `nChainIndex`, `parentBlock`) -- see
`contrib/merge_mining_coordinator/wire.py`'s `AuxPow.serialize()` for a
complete, literal reference implementation.

**F. Call `submitauxblock <hash> <auxpow_hex>`.** `true` means accepted.
`false` means a real, well-formed-but-invalid submission -- BitAIcoin's own
consensus rules are the sole authority on why (this document and the
coordinator never re-implement or second-guess that decision). A thrown
RPC error means something more fundamental: malformed input, an
unknown/evicted candidate, or the chain isn't ready yet (sec.7).

**G. Refresh your work when the BitAIcoin tip changes.** A `createauxblock`
candidate is bound to the tip it was built against
(`previousblockhash`); once BitAIcoin's real tip moves past that value
(from anyone's submission, not just yours), your outstanding work is stale
-- request a fresh candidate rather than continuing to mine the old one
(sec.5).

## 4. The external-pool handoff, explicitly

**What you insert into your own parent coinbase** (this is the entire
integration surface on the "commit" side):
```
fabe6d6d || aux_merkle_root || tree_size || nonce
```
using the exact existing BitAIcoin/Namecoin-compatible byte ordering above
-- nothing else, no additional framing.

**What you must produce after finding a solution** (this is the entire
integration surface on the "prove" side) -- there is exactly one proof
format; nothing here is invented for this reference:
- the solved parent block header
- your parent coinbase transaction
- the coinbase-to-parent-merkle-root branch, and the coinbase's index
- the aux-chain merkle branch, and the aux-chain index

For BitAIcoin's current, real, single-aux-chain (tree_size=1) reference
path specifically: **aux-chain branch = `[]`, aux-chain index = `0`,
always.**

## 5. Stale-work and idempotent-retry semantics

A job is bound to the exact `(child_hash, previousblockhash, height,
target)` tuple `createauxblock` returned. Before submitting, re-check the
current BitAIcoin tip:
- if the tip still equals your job's `previousblockhash` -> submit normally.
- if the tip has moved to something else -> your job is **stale**. Discard
  it (never mutate or recycle it into a "new" job) and call
  `createauxblock` again.
- if the tip has moved to *exactly your own job's `child_hash`* -> your
  work was already accepted (perhaps your own earlier submission
  succeeded but the response was lost to a timeout, or a racing duplicate
  submission won first) -- resubmitting is safe and returns `true`
  (`docs/AUXPOW_MILESTONE.md` sec.11.2/12.1: this idempotent-retry behavior
  is real, frozen `submitauxblock` semantics, not something this
  coordinator invents). This is narrowed to only apply when that hash is
  genuinely an already-accepted **AuxPoW** block (sec.12.1) -- it can never
  be tricked by submitting an arbitrary already-valid direct-mined block's
  hash.

`contrib/merge_mining_coordinator/coordinator.py`'s
`MergeMiningCoordinator.submit_parent_solution()` implements exactly this
policy as a thin RPC-layer courtesy check -- it never decides validity
itself; bitaicoind's own response is always authoritative.

## 6. Multi-aux-chain future (tree_size > 1) -- not exercised today

BitAIcoin's current, real integration is single-aux-chain: `tree_size=1`,
`chain_index=0`, `chain_branch=[]`, always. The chain-merkle-tree machinery
in `wire.py` (`expected_merkle_tree_index()`, `build_multi_chain_commitment()`)
is written generally enough that a future pool merge-mining BitAIcoin
alongside other aux chains (Dogecoin, Namecoin, etc., sharing one parent)
could use `tree_size > 1` without rewriting the coordinator -- but this is
explicitly **not** built out, tested, or claimed working today. One
consequence worth knowing if this is ever extended: `CAuxPow` carries no
explicit chain-ID field of its own (the real chain ID is supplied
server-side, from the child block's own already-fixed `nVersion`, never
from the submitted proof) -- so a "wrong chain ID" submission is only
observable once `tree_size > 1` actually assigns different chains to
different slots (`docs/AUXPOW_MILESTONE.md` sec.10.8 has the full
explanation).

## 7. A frozen-interface gotcha worth knowing: IBD readiness

`createauxblock` refuses to produce work while BitAIcoin considers itself
still in initial sync ("in IBD") -- except on `REGTEST`, which is exempt by
design (a pool-facing RPC must not require a warm-up block before
producing the very first candidate). On a real (non-regtest) node, expect:
```
error code: -10
error message: "... is in initial sync and waiting for blocks..."
```
promptly (never a hang) if your node hasn't caught up yet. This was a real
bug, found and fixed via this very integration effort -- see
`docs/AUXPOW_MILESTONE.md` sec.13 for the full story, including why it
took building a real integration client to surface it (every internal test
before this one always mined a warm-up block first, which masked it
completely).

## 8. Architecture reference (this repository's own coordinator)

- `wire.py` -- dependency-free wire-format primitives (hashing, merkle
  branches, the merge-mining tag, `CAuxPow` (de)serialization). Read this
  file first; it is written to be understood without any BitAIcoin Core
  C++ knowledge.
- `rpc.py` -- a minimal JSON-RPC client (no external dependencies).
- `provider.py` -- the `ParentWorkProvider` interface (three operations:
  get a parent candidate, insert the commitment, receive solved work) and
  `SyntheticParentWorkProvider`, the only implementation built so far. A
  real `BitcoinCoreGBTParentWorkProvider` (talking to a real Bitcoin Core
  node via `getblocktemplate`/`submitblock`, handling longpoll,
  `coinbaseaux`, witness commitments) is **documented future work, not
  implemented** -- building it would drag this reference into real Bitcoin
  GBT semantics, explicitly out of scope for this milestone.
- `coordinator.py` -- `MergeMiningCoordinator`, with exactly two public
  operations: `create_job()` and `submit_parent_solution()`. The real
  SHA256d mining loop is deliberately NOT part of this class -- see
  `wire.solve_parent_header()`, used only by the demo/test clients below.
- `demo.py` -- single-command, real, end-to-end integration proof:
  `python3 contrib/merge_mining_coordinator/demo.py --regtest --rpcport=<port> --rpcuser=<u> --rpcpassword=<p>`.
- `test_negative_matrix.py` -- the adversarial regression matrix (16 real
  checks against a live node): valid proof accepted; idempotent
  resubmission accepted; stale candidate cleanly rejected; and 11 distinct
  malformed/invalid-proof constructions each rejected the correct way,
  entirely via bitaicoind's own real validation (never re-implemented
  here).

## 9. Complete real regtest example

Captured from an actual run (`demo.py`, unmodified, against a fresh
regtest node -- no secrets, no production data):

```json
{
  "child_hash": "80857237853d247013bf39b12b5c49e3988e1d2b102591f1e30794215ffbf59c",
  "chain_id": 16969,
  "previousblockhash": "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206",
  "coinbasevalue": 5000000000,
  "child_bits": "207fffff",
  "child_height": 1,
  "child_target": "0000000000000000000000000000000000000000000000000000000000ffff7f",
  "child_target_numeric_hex": "7fffff0000000000000000000000000000000000000000000000000000000000",
  "commitment": "fabe6d6d9cf5fb5f219407e3f19125102b1d8e98e3495c2bb139bf1370243d8537728580010000009301eec9",
  "tree_size": 1,
  "merkle_nonce": 3387818387,
  "chain_index": 0,
  "chain_branch": []
}
```

Commitment tag decomposed (44 raw bytes total):
```
fabe6d6d                                                            magic
9cf5fb5f219407e3f19125102b1d8e98e3495c2bb139bf1370243d85377285 80   aux_merkle_root (== child_hash, raw LE bytes)
01000000                                                            tree_size = 1 (LE)
9301eec9                                                            merkle_nonce = 3387818387 (LE)
```

Cross-checks performed on this exact run, all confirmed:
- `target_from_createauxblock_hex(child_target) == compact_to_target(int(child_bits, 16))` (two independent
  derivations of the same numeric target agree).
- `reverse_hex_bytes(child_target) == getblocktemplate(...)["target"]` exactly
  (`7fffff0000000000000000000000000000000000000000000000000000000000`), and `child_bits == getblocktemplate(...)["bits"]`.
- A synthetic parent header was mined (nonce found: `0` at this trivial regtest target) whose hash
  satisfied `child_target_numeric`.
- `submitauxblock(child_hash, auxpow_hex)` returned `true`.
- `getbestblockhash()` afterward returned exactly `80857237853d247013bf39b12b5c49e3988e1d2b102591f1e30794215ffbf59c`
  -- the identical `child_hash` value from the original `createauxblock` response.

## 10. Explicitly out of scope

Stratum server or client, pool accounts or a pool database, PPS/PPLNS,
share difficulty or vardiff, payouts, worker authentication, real Bitcoin
block submission, real Bitcoin Core node orchestration, a web dashboard,
ZMQ, a production daemon, or a Docker deployment stack. This milestone
answers only whether an external SHA256d pool *can* obtain BAIC work,
embed it correctly, and return a valid proof through the frozen public RPC
API -- not how to run a pool.
