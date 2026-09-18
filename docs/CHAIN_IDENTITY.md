# Chain Identity

BitAIcoin (ticker: **BAIC**) is registered as its own `ChainType` inside
Bitcoin Core, not a repurposed signet/regtest/mainnet configuration. This
document is the single source of truth for every network-identity constant.
All values below are collision-checked against Bitcoin mainnet, testnet3,
testnet4, signet, and regtest as they ship in v31.1.

## Selecting the chain

```
bitcoind -chain=bitaicoin ...
bitcoin-cli -chain=bitaicoin ...
```

Per-chain datadir subdirectory: `bitaicoin/` (e.g. `<datadir>/bitaicoin/`),
following the same convention as `testnet3/`, `signet/`, `regtest/`.

## Network

| Constant | Value | Notes |
|---|---|---|
| P2P magic (`pchMessageStart`) | `0xb7 0x78 0xd8 0x11` | Frames every P2P message and every raw block-file record. Distinct from mainnet's `f9beb4d9`, testnet3's `0b110907`, testnet4's `1c163f28`, signet's (message-defined, no fixed 4 bytes), regtest's `fabfb5da`. |
| Default P2P port | `28333` | Distinct from mainnet 8333, testnet3 18333, testnet4 48333, signet 38333, regtest 18444. |
| Default RPC port | `28332` | Distinct from mainnet 8332, testnet3 18332, testnet4 48332, signet 38332, regtest 18443. |
| DNS seeds | none | `vSeeds` and `vFixedSeeds` are both empty. BitAIcoin is a private lab network for Phase 1; nodes only find each other via explicit `-connect=`/`-addnode=`. |
| Base58 human-readable chain name | `bitaicoin` | Used by `-chain=`, RPC `getblockchaininfo().chain`, log lines, etc. |

## Addresses

| Constant | Value | Prefix character(s) |
|---|---|---|
| `base58Prefixes[PUBKEY_ADDRESS]` | `25` | Base58Check addresses start with `B` |
| `base58Prefixes[SCRIPT_ADDRESS]` | `40` | P2SH addresses start with `b` |
| `base58Prefixes[SECRET_KEY]` (WIF) | `153` | WIF private keys start with `9`/`c`-ish range (base58 alphabet dependent) |
| `base58Prefixes[EXT_PUBLIC_KEY]` | `0x04 0x33 0x1a 0x38` | xpub-equivalent |
| `base58Prefixes[EXT_SECRET_KEY]` | `0x04 0x33 0x15 0x94` | xprv-equivalent |
| `bech32_hrp` | `bai` | Native SegWit/Taproot addresses would read `bai1...` if/when those are activated (see `docs/CONSENSUS.md`) |

None of these values collide with mainnet (`1`/`3`/`bc1`), testnet
(`m`/`n`/`2`/`tb1`), or regtest (`bcrt1`) prefixes. Address encode/decode
required zero code changes — `key_io.cpp` is fully chainparams-driven.

## Consensus-identity fields

These live on `Consensus::Params` and are unique to BitAIcoin (see
`docs/CONSENSUS.md` for full detail on how each is used):

| Field | Value | Meaning |
|---|---|---|
| `BitAIForkAnchorHeight` | `225429` | Last block height shared with real Bitcoin history. |
| `BitAIForkAnchorHash` | `0000000000000366ce98ca28338900094e8cbf445776253181749f782546d006`\* | Enforced exact-match at that height; anchors BitAIcoin's ledger to real Bitcoin's, non-negotiably. |
| `BitAIActivationHeight` | `225430` | First BitAIcoin-native block. |
| `BitAIActivationPowLimit` | `7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff`\* | One-time easy retarget at the activation boundary (see `docs/CONSENSUS.md`). |
| `BitAIForkId` | `0x424149` (ASCII `"BAI"`) | Folded into sighash for replay protection (see `docs/REPLAY_PROTECTION.md`). |

\* *Recorded here exactly as configured in `kernel/chainparams.cpp`; see that
file for the authoritative byte values, since hash/target strings are easy
to mistranscribe by hand.*

## Genesis block

Identical to real Bitcoin's genesis block — same timestamp (`1231006505`),
nonce (`2083236893`), bits (`0x1d00ffff`), coinbase, and therefore the same
genesis hash
(`000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f`).
`CreateGenesisBlock(...)`'s output is asserted against this real hash at
startup, exactly as `CMainParams` does. This is deliberate: BitAIcoin's
identity as "the same ledger as Bitcoin, up to a documented point" starts
at block 0, not block 225430.

## No premine

No coins are created outside the normal block-subsidy schedule inherited
from Bitcoin (50 BTC-equivalent halving every 210,000 blocks, starting from
the same genesis). The first BitAIcoin-native coinbase (height 225430) pays
out under the same schedule as if it were still real Bitcoin at that
height — there is no special "founder" transaction or extra output.
