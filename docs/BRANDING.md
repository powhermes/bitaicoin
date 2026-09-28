# BitAIcoin Branding & Ecosystem Identity

This document defines the BitAIcoin identity model. It is documentation only and
has no effect on consensus, the network protocol, or monetary units.

## Identity model

- **BitAIcoin** — the network, coin, software, and **technical identity**. This is
  the name used in the client (`CLIENT_NAME = BitAIcoin`), in version and help
  output, and in the P2P subversion string `/BitAIcoin:31.1.0/`.
- **Saitoshi** — the **symbolic ecosystem identity**: a persona / cultural brand
  for BitAIcoin.

  > Saitoshi is the symbolic identity of the BitAIcoin ecosystem — a nod to
  > Satoshi, reimagined for BitAIcoin.

  Saitoshi is a cultural/branding concept only. It deliberately does **not**
  appear in technical surfaces: client/help/version strings, the P2P subversion,
  monetary-unit terminology (a *satoshi* remains a satoshi), historical
  references to Satoshi Nakamoto, or any consensus/protocol identifier.
- **Primary visual mark** — the circular orange-and-white BitAIcoin logo
  (`doc/bitaicoin_logo.png`). It is a flat two-colour design on a transparent
  background: no gradients, shadows, or backgrounds.

## Upstream attribution

BitAIcoin is a fork of Bitcoin Core. Upstream copyright and licensing notices are
preserved unchanged, and the client continues to credit *The Bitcoin Core
developers* (enforced by the `CopyrightHolders()` guard in
`src/clientversion.cpp`). Renaming for BitAIcoin identity is limited to
product-facing surfaces and never rewrites historical attribution.

## Visual assets

| Asset | Path | Purpose |
|---|---|---|
| Master logo | `doc/bitaicoin_logo.png` | canonical high-resolution mark (1254×1254 RGBA) |
| Doc mark | `doc/bitaicoin_icon.png` | same mark, used in docs |
| App icon (Qt) | `src/qt/res/icons/bitcoin.png` | GUI window/app icon (resource name kept for build stability) |
| App icon (Windows) | `src/qt/res/icons/bitcoin.ico` | Windows application icon |
| App icon (macOS) | `src/qt/res/icons/bitcoin.icns` | macOS application icon |
| Linux desktop | `share/pixmaps/bitcoin{16,32,64,128,256}.{png,xpm}`, `share/pixmaps/bitcoin.ico` | desktop/packaging icons |

Internal resource **filenames** remain `bitcoin.*` deliberately — the Qt resource
alias and build wiring reference those names, and renaming the files would add
build churn without changing what users see. The **image content** is the
BitAIcoin mark.

Assets deliberately left unchanged: functional UI glyphs in
`src/qt/res/icons/` (send/receive/etc., not brand marks); the network-variant
icons `bitcoin_signet.ico` / `bitcoin_testnet.ico` (only shown when running
`-signet` / `-testnet`, which is not BitAIcoin's operating mode — a badged
BitAIcoin variant is a future design task); and `src/qt/res/src/bitcoin.svg`
(historical vector source, not shipped in binaries).

## Deterministic derivative generation

All shipped raster derivatives are regenerated from the single master
(`doc/bitaicoin_logo.png`, SHA-256
`dff06fb8fec7175566e4096439e04e280154df43531f79d52d5a8ee087f410e6`). Recorded
checksums of the master and every derivative are in
[`branding-assets.SHA256`](./branding-assets.SHA256).

Tooling and commands (transparency preserved, `-strip` to drop timestamps,
`SOURCE_DATE_EPOCH=0`):

- Rasters (PNG/ICO/XPM) — ImageMagick 6.9.12-98 Q16:
  ```sh
  export SOURCE_DATE_EPOCH=0
  convert master.png -strip -define png:exclude-chunks=date,time -background none \
      -resize <N>x<N> PNG32:bitcoin<N>.png
  convert bitcoin16.png bitcoin32.png bitcoin48.png bitcoin64.png \
      bitcoin128.png bitcoin256.png bitcoin.ico
  convert bitcoin<N>.png -strip bitcoin<N>.xpm
  ```
- macOS `.icns` — `iconutil -c icns bitaicoin.iconset -o bitcoin.icns`, where the
  iconset members are the ImageMagick-generated PNGs.

No gradients, shadows, backgrounds, or artwork alterations are introduced; only
resizing and format conversion.
