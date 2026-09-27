# Reproducible Builds — RC1 (`v31.1.0-bitaicoin.1-rc1`)

This document records the independent reproducibility evidence for the BitAIcoin
release candidate **`v31.1.0-bitaicoin.1-rc1`**. It contains only non-sensitive
release evidence (tags, commits, artifact filenames, and SHA-256 digests).

## Scope of the claim

`v31.1.0-bitaicoin.1-rc1` is **independently reproducible** for the following
targets, built through the unmodified `contrib/guix/guix-build` pipeline:

- `x86_64-linux-gnu`
- `aarch64-linux-gnu`

using the pinned Guix revision below.

This claim is **deliberately limited** to the two Linux targets and the pinned
Guix revision named here. It is **not** generalized to macOS, Windows, other
platform triples, future commits, or future toolchains.

## Identifiers

| Item | Value |
|---|---|
| RC1 tag | `v31.1.0-bitaicoin.1-rc1` |
| Commit | `44338f3014a07b0bce30cb8f0d55fb5da9029536` |
| Pinned Guix revision | `5cb84f2013c5b1e48a7d0e617032266f1e6059e2` |

## Builders

Two independent Linux builders were used. Each used a **separate `/gnu/store`**
and **independently recompiled** the BitAIcoin release targets from the frozen
source (no reuse of the other builder's final release archives).

| Builder | Machine | OS | Arch |
|---|---|---|---|
| Builder 1 | ASUS host | Ubuntu 24.04 | x86_64 |
| Builder 2 | Independent Vultr Linux host | Ubuntu 22.04 | x86_64 |

The two builders differed in physical machine, OS release, and host Guix version,
and each bootstrapped the pinned Guix revision via `guix time-machine`.

## Method

On each builder, from a clean checkout of the RC1 tag (commit verified to peel to
`44338f3014a07b0bce30cb8f0d55fb5da9029536`):

```sh
env HOSTS="x86_64-linux-gnu" ./contrib/guix/guix-build   # then aarch64-linux-gnu
```

Same-builder repeatability was first confirmed on Builder 1 (two clean builds per
target produced identical artifacts). The independent-reproducibility result below
was then established by comparing Builder 1 and Builder 2 outputs.

## Results

**Independent reproducibility was demonstrated when the corresponding artifacts
produced on the two independent builders were byte-for-byte identical.**

### x86_64-linux-gnu

| Artifact | SHA-256 |
|---|---|
| `bitcoin-31.1.0-bitaicoin.1-rc1-x86_64-linux-gnu.tar.gz` | `030f12d7c2e8a7354635a695cbf4f7e1179cd4d9a721ad06215eab9874690766` |
| `bitcoin-31.1.0-bitaicoin.1-rc1-x86_64-linux-gnu-debug.tar.gz` | `0ba4574e2a557e13cc20a2386cddf576303cea80fecb1dfdb4c03fc5d77d08f7` |
| `SHA256SUMS.part` | `d0cb02abe01f063b6993ceda5cd45874309a8c2ac822c5dd4b3a488f884be594` |
| `dist-archive/bitcoin-31.1.0-bitaicoin.1-rc1.tar.gz` | `5f0629c97c0a24d22cb37e849c9211196b4793922fa371f6a193f173faf21208` |

### aarch64-linux-gnu

| Artifact | SHA-256 |
|---|---|
| `bitcoin-31.1.0-bitaicoin.1-rc1-aarch64-linux-gnu.tar.gz` | `3861caca394881c72af4a115b4e5e0410d5863b27faf23e9668e93dc3d752435` |
| `bitcoin-31.1.0-bitaicoin.1-rc1-aarch64-linux-gnu-debug.tar.gz` | `987d902f86de0678bd673aa50f001d9f152b4b02a7bdbd4df51d0707a4076bf6` |
| `SHA256SUMS.part` | `d2d0c4c3ae46fc13bc3a817c0e73f9c0fec5e651f3df43bc00aad2d1fc3d3f28` |
| `dist-archive/bitcoin-31.1.0-bitaicoin.1-rc1.tar.gz` | `5f0629c97c0a24d22cb37e849c9211196b4793922fa371f6a193f173faf21208` |

The `dist-archive` (source tarball) is architecture-independent and is identical
across both targets and both builders.

## Attestation and signing status

- `guix-attest` was exercised using `NO_SIGN=1`, which produces a consolidated
  `noncodesigned.SHA256SUMS` **without** creating any signature.
- **No GPG signature was created for RC1.**
- **No signing claim is made** for these artifacts.
- `guix-verify` requires signed attestation files (`.asc`) to verify. Because RC1
  was attested unsigned, `guix-verify` was **not** a successful
  signature-verification step in this proof; the RC1 evidence rests on the
  byte-for-byte artifact equality recorded above, not on signatures.

Establishing a formal GPG release-signing identity and process remains a separate,
later gate.

## Naming note (informational)

For RC1, the compiled executables are named `bitaicoind` / `bitaicoin-cli`, while
the release **archive basename** and the internal top-level directory still use
the upstream `bitcoin-...` form. This is a known packaging/branding item tracked
for a later candidate and does not affect the reproducibility result.
