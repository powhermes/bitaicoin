# AuxPoW activation — pre-production consensus milestone

Status: **Planning + baseline recorded. Implementation not yet started.** This document exists so the
decision, the reasoning, and the exact pre-change chain state are on record before any consensus code
is touched, per the explicit instruction that opened this milestone (2026-09-23).

Do not rewrite or invalidate any existing BitAIcoin block. This milestone only ever adds a
height-gated branch at a height strictly above the current tip.

## 0. PERMANENT POLICY: direct mining and AuxPoW are BOTH valid, forever, post-activation

**This section is a permanent design decision (frozen 2026-09-23, explicit user instruction), not a
transitional state. Do not "fix" this later to make AuxPoW mandatory, or to disable direct mining
once AuxPoW exists, without re-opening this decision explicitly with the user first.**

Post-activation (height >= the activation height, see sec.2), BitAIcoin accepts **both**:

1. **Ordinary direct SHA256d BitAIcoin mining** (the AUXPOW version bit unset) -- exactly as today,
   unchanged in every respect other than the DAA (sec.3).
2. **AuxPoW / merged mining** (the AUXPOW version bit set, chain ID matching, a valid `CAuxPow` proof
   attached) -- an *additional* accepted proof format.

**AuxPoW is additive, never a replacement.** This is a deliberate, permanent divergence from
Dogecoin's `fAllowLegacyBlocks=false` model, which eventually forces every block through AuxPoW.
BitAIcoin does not want that dependency: making direct mining permanently invalid at some future
height would mean the chain's ability to keep extending becomes dependent on an external Bitcoin pool
or merge-mining coordinator continuing to exist and cooperate. Existing/direct miners must always be
able to extend the chain on their own; Bitcoin/SHA256d pools can add AuxPoW support at any later time
without requiring another consensus change to keep the chain alive in the meantime.

**Both proof paths use the identical BitAIcoin-required target/chainwork semantics.** AuxPoW must
never receive an easier target or special chainwork treatment relative to direct mining at the same
height -- both are checked against the exact same `nBits` value BitAIcoin's own DAA computed for that
height (see `CheckAuxPowRules()` in `src/auxpow.h`, which passes the header's own `nBits` -- not a
separately-relaxed value -- into `CAuxPow::Check()`), and a block's chainwork contribution is a
function of that same target regardless of which proof format satisfied it. There is no "AuxPoW
bonus" and there must never be one.

## 1. Pre-AuxPoW baseline (recorded 2026-09-23, before any consensus change)

- **Tip height:** 225823
- **Tip hash:** `0000000ad1060dd6b63a31796c4d57977c7f009eb0229b32d0b15dd303ecff57`
- **BitAIForkAnchorHeight:** 225429 / **BitAIForkAnchorHash:** `0000000000000366ce98ca28338900094e8cbf445776253181749f782546d006`
- **BitAIActivationHeight:** 225430 (== SegwitHeight)
- **BitAIActivationPowLimit:** `0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff`
- **consensus.powLimit:** `0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff` (28 bits headroom, M5 fix)
- **consensus.BitAIHistoricalPowLimit:** `00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff` (real mainnet's, for pre-activation retarget reproduction)
- **nSubsidyHalvingInterval:** 210000 (unmodified)
- **nPowTargetTimespan:** 1209600 (two weeks) / **nPowTargetSpacing:** 600 — retarget interval = 2016 blocks, on **absolute height**, continuing real Bitcoin's own epoch boundaries (confirmed: height 225792, the M5 bug height, is an exact multiple of 2016 — the fork does not reset retarget windows to zero at the activation point)
- **BitAIForkId:** `0x424149` ("BAI"), folded into sighash at height >= BitAIActivationHeight for replay protection — a **separate mechanism** from any AuxPoW chain ID (see §5)
- **pchMessageStart:** `b7 78 d8 11` / **nDefaultPort:** 28333 / **bech32_hrp:** "bai"
- **Difficulty at tip and at block 225430:** identical, 0.06249910592947572 (`bits` 0x1d0fffff).
  **Correction (2026-09-23) to an earlier, wrong characterization in this doc:** this is *not*
  because "no retarget has happened yet." A native, absolute-height 2016-block retarget already
  occurred at height 225792 (225792 / 2016 = 112 exactly — see the epoch-boundary note above) i.e.
  well within the 225430-225823 span already covered by this baseline. That retarget genuinely ran
  and genuinely recomputed a new target from the preceding 2016-block window — it simply reproduced
  the identical target value, because the chain has been mining at a stable, low rate relative to
  `consensus.powLimit` throughout that window, not because the retarget was skipped or didn't fire.
  The original text here ("no retarget yet... well under the 2016-block retarget window") was
  factually wrong and is corrected here rather than silently edited out.

## 2. The five points requested before implementation

### (1) Proposed activation height: **227808**

227808 is the **next 2016-block retarget boundary after the current tip** (225823 → next boundary
225792 is already past; the one after that is 227808 = 2016 × 113). Reasoning:

- The retarget windows run on absolute height (confirmed above), so 227808 is a real, load-bearing
  boundary the existing DAA code already treats specially — not an arbitrary number.
- Activating AuxPoW *and* any DAA change (§3) at the same point the code already recomputes
  difficulty means the epoch immediately before activation is homogeneous (pure legacy SHA256d, as
  today) and the epoch immediately after can run entirely under the new rules from its first block.
  A boundary that fell mid-epoch would need the retarget math to reason about a window straddling
  two different mining/difficulty regimes, which is exactly the kind of edge case that produced the
  M5 overflow bug.
- Runway: 227808 − 225823 = **1985 blocks** from today's tip. Given the plan is to mine through this
  deliberately with real hashpower rather than wait out lab-pace block times, this is calendar
  runway to build and test against, not a constraint on how fast you could reach it if you chose to.
  If more calendar time is wanted for the 3-node upgrade, the next candidate is 229824 (one epoch
  later); I'd only move to it if 1985 blocks turns out to be too little runway in practice.

### (2) Proposed AuxPoW chain ID: **16969** (0x4249, the two ASCII bytes "BI" read as a big-endian u16)

This is a *different field, for a different purpose*, from `BitAIForkId` (0x424149, "BAI", folded
into transaction sighash for replay protection). The AuxPoW chain ID is a small integer committed
into the *parent block's* coinbase merge-mining tag and merkle structure, used purely to bind a
parent-chain block to *this specific* auxiliary chain and to reject AuxPoW proofs built for a
different chain. Keeping the two constants visibly related (BAI-derived) but numerically distinct
avoids implying they're interchangeable.

Caveat: chain-ID collision only matters against other chains actually being merge-mined on the same
parent network. There's an informal community registry of chain IDs used by real merge-mined
alts (Namecoin, Syscoin, and others historically assigned low integers). Since BitAIcoin is a
private lab network today, there's no live collision risk — this becomes relevant only if BitAIcoin
is ever merge-mined against a public parent chain, at which point the chosen value should be
checked against that registry before going live, not before.

### (3) Proposed DAA behavior: **switch to a fast-reacting algorithm (ASERT) exactly at the activation height**

The existing 2016-block retarget is a well-documented poor fit for a sudden merge-mining hashrate
change: real Bitcoin miners turning on AuxPoW support can add orders of magnitude of hashrate
instantly, and a classic 2016-block window won't correct for up to two weeks of real time (or,
worse, a burst of near-instant blocks on this low-difficulty lab chain), which is exactly the
adversarial scenario the milestone asks to be tested. Proposal:

- At heights < 227808: **unchanged** — today's `CalculateNextWorkRequired`, byte-for-byte.
- At heights >= 227808: an **ASERT**-style algorithm (the approach Bitcoin Cash adopted in 2020 for
  this same class of problem), retargeting every block rather than every 2016, converging toward the
  correct difficulty within a small number of blocks after a hashrate shock instead of up to a full
  epoch.
- This needs its own overflow/precision safety proof, mirroring the M5 lesson (`docs/CONSENSUS.md`
  point 6): ASERT's math is exponent-based fixed-point, a different arithmetic shape from the
  existing linear retarget, and needs its own explicit headroom argument and a startup assert, not
  an assumption that "it's a well-known algorithm so it's safe here."

### (4) Reference implementation: **Namecoin's original AuxPoW design**, cross-checked against **Syscoin's** more modern maintained fork of it

Namecoin's AuxPoW is the foundational, SHA256d-native merge-mining design (this matters: Dogecoin's
AuxPoW is scrypt/Litecoin-oriented and not the right shape here). Namecoin's own tree targets a much
older Bitcoin Core codebase than this one (v31.1, CMake, C++20), so it's a *design* reference, not
something to vendor directly. Syscoin maintains a more actively updated SHA256d AuxPoW
implementation closer to modern Bitcoin Core internals, useful as a porting reference for how the
same validation logic reads against a current codebase. I'll port the *logic* (serialization shape,
validation order, the specific rejection cases the milestone lists) rather than copy either tree
verbatim, since neither is a drop-in fit for this fork's CMake build or its existing height-gated
patterns.

### (5) Compatibility risk against existing 225430+ history: **none, by construction**

AuxPoW validation only ever applies to blocks at height >= 227808. Every block from 225430 through
the current tip, and everything mined between now and 227808 under legacy rules, keeps validating
exactly as it does today — `CheckProofOfWork` on the block's own header hash, no AuxPoW structure
present or required. This mirrors the pattern already used for `BitAIActivationHeight` itself
(SegWit/Taproot/replay-protection all branch cleanly on a height check with no retroactive
reinterpretation of earlier blocks), so there's direct precedent in this codebase for doing this
kind of gate safely. The one thing that must be implemented as a *pure height branch with no shared
mutable state* is the DAA change in (3), for the same reason.

**No genuine design conflict found.** Proceeding, per instruction.

## Next concrete step

This is a genuine multi-part consensus implementation (serialization, validation, a new
DAA branch, new RPCs, and the full adversarial/activation-boundary/reorg/multi-node test matrix
listed in the milestone). It will be built and tested incrementally, not delivered as one
untested block of code — starting with the AuxPoW header/proof serialization and validation core,
since every other piece (the RPCs, the tests, the multi-node upgrade) depends on that being correct
first.

**Status update (2026-09-23, commit `800c533c3a`): the serialization/validation core is done.**
`src/auxpow.h`/`src/auxpow.cpp` implement `CAuxPow` (parent header + coinbase tx + both merkle
branches), the version-bit helpers, `CheckMerkleBranch()`, the Namecoin-compatible
index-grinding-resistant `GetExpectedMerkleTreeIndex()`, and `CAuxPow::Check()` covering every
rejection case listed in sec.4/5 above (parent PoW vs. the auxiliary chain's own target -- not the
parent chain's, a real bug caught and fixed before any test was written around the wrong version --
parent-not-itself-AuxPoW, coinbase merkle inclusion, exactly-one-tag ambiguity defense, chain-merkle
commitment, index-grinding defense). `src/test/auxpow_tests.cpp` has 9 real end-to-end tests, each
building an actual fake parent block and running it through `Check()`; all 9 pass, and the full
existing 749-case `test_bitcoin` suite still passes with zero regressions (both verified by an actual
`cmake --build` + test run this session, not assumed).

**Status update (2026-09-23, later): height-gate/version-rule logic built and tested, real hazards
found and deliberately avoided rather than papered over.** Attempted the next slice -- wiring
`CAuxPow` into `CBlockHeader`'s own storage/serialization -- and found two genuine design hazards
before writing any code around them:
1. `CBlockHeader::GetHash()` computes `(HashWriter{} << *this).GetHash()`, using this type's own
   generic `Serialize`. Naively adding the auxpow payload to `CBlockHeader`'s `SERIALIZE_METHODS`
   would make an AuxPoW block's own identity hash silently include its auxpow bytes -- circular and
   wrong, since `CAuxPow::Check()`'s whole job is to validate a proof *against* that hash, which
   therefore cannot itself depend on the proof. The real fix (matching Namecoin's actual design):
   the header-hash computation must always cover only the 6 base fields regardless of auxpow
   presence, with the auxpow payload carried through a separate, explicit wire-format serialization
   path (the same shape as SegWit's txid-vs-wtxid split via two distinct helpers, not one generic
   conditional `Serialize`).
2. Storing a `CAuxPow` on `CBlockHeader` needs a forward declaration plus explicit out-of-line
   special members to avoid a circular include (`auxpow.h` needs the complete `CBlockHeader` type for
   `CAuxPow::parentBlock`; `block.h` would need `CAuxPow`) -- mechanical, but `CBlockHeader` is used
   throughout the entire node, and this deserves its own reviewed pass, not a rushed addition folded
   into everything else already in flight.

Rather than rush either fix into this same pass, the actual height-gate and pre/post-activation
version-rule LOGIC was extracted into a standalone, explicitly-parameterized function,
`CheckAuxPowRules()` (`src/auxpow.h`/`.cpp`), built and tested against explicit parameters instead of
against `CBlockHeader`'s not-yet-existing auxpow storage -- so it's ready to be called from
`ContextualCheckBlockHeader()` verbatim once that storage/serialization slice lands, without needing
to be rewritten. Enforces: AUXPOW version bit rejected outright below the activation height; chain-ID
mismatch rejected; missing proof rejected; a valid proof (chain ID + `CAuxPow::Check()`) accepted;
direct (non-merge-mined) mining remains valid both before and after activation. 8 new real test cases
added to `src/test/auxpow_tests.cpp` (now 17 total), covering the height boundary, both proof-present
and proof-absent paths, and the chain-ID check. Verified for real: rebuilt from clean, ran the new
cases (17/17 pass), then the full existing suite (756 cases, zero regressions) -- confirmed twice.

**Status update (2026-09-23, dedicated storage/serialization slice): DONE, both hazards resolved for
real, not worked around.** The arithmetic gate above was judged sufficiently strong (re-derived bound,
independent Decred vectors) to proceed. Resolved:
1. `CBlockHeader::auxpow` (`std::shared_ptr<CAuxPow>`) is now a real member, added to
   `src/primitives/block.h`, using only a forward declaration (`class CAuxPow;`) -- confirmed by
   compiling, `std::shared_ptr`'s destructor does NOT need the pointee's complete type (unlike
   `unique_ptr`; its deleter is captured/type-erased at construction time), so no explicit out-of-line
   special members were needed after all -- the second hazard was real but smaller than first assessed.
2. `CBlockHeader::SERIALIZE_METHODS` (and therefore `GetHash()`) is **completely unchanged** -- still
   only the six base fields, unconditionally, regardless of `auxpow`'s presence. The version-bit
   helpers (`VERSION_AUXPOW`, `IsAuxpowVersion`, etc.) moved from `auxpow.h` into `primitives/block.h`
   (single definition, used by both files, no circular include) so `CBlockHeader::IsAuxpow()` could be
   added directly. The auxpow-aware wire format is a **separate, explicit pair of template functions**,
   `SerializeBlockHeaderWithAuxPow`/`UnserializeBlockHeaderWithAuxPow` (`src/auxpow.h`), mirroring this
   codebase's own existing `SerializeTransaction`/`UnserializeTransaction` split for txid-vs-wtxid --
   real precedent, not invented. Works for any conforming Stream (network `DataStream`, disk `AutoFile`)
   since it's templated exactly like that precedent.

**A third, real, previously-latent bug was found and fixed while wiring this up** (not anticipated
going in, and not present in the earlier CAuxPow tests, which only ever constructed CAuxPow objects
directly and never actually serialized one over a stream until this slice): `CAuxPow::coinbaseTx` is
a `CTransactionRef`, and this codebase's own transaction (de)serialization requires the stream to
carry an explicit `TransactionSerParams` (via `TX_WITH_WITNESS(...)`/`TX_NO_WITNESS(...)`) -- a bare
`DataStream`/`AutoFile` doesn't compile without it. Fixed by wrapping with `TX_NO_WITNESS`, which is
also the semantically *correct* choice, not just the one that compiles: the coinbase merkle-inclusion
check in `CAuxPow::Check()` uses `coinbaseTx->GetHash()` (the TXID, witness-excluded) to match
`parentBlock.hashMerkleRoot`'s own TXID-based convention, so witness bytes have no bearing on this
proof and would only add unnecessary size.

**Tests, per every item on the explicit checklist, all passing:** header-hash invariance under
changing/removing/adding the auxpow payload while base fields are held constant; header hash changing
under each of the six base fields individually; a REAL header from BitAIcoin's live chain (height
225823, fetched via `bitaicoin-cli` this pass) reconstructed field-by-field and confirmed to reproduce
its exact known hash AND its exact known raw wire bytes -- concrete proof existing history is
untouched, not just an assertion; serialize->deserialize->serialize round trips (both with and without
an attached proof); byte-for-byte identical output between the new auxpow-aware serializer and the
plain, unchanged `CBlockHeader` serializer for any non-AuxPoW header; network-style (`DataStream`) and
disk-style (real `AutoFile` against an actual temp file) round trips, both passing; malformed-data
rejection (AUXPOW bit set with no proof attached fails loudly on serialize; a truncated stream missing
the auxpow payload throws on deserialize; an oversized claimed merkle-branch length is rejected before
any hashing is attempted); direct-mining (non-AuxPoW) headers serializing identically regardless of
height relative to 227808. 15 new test cases (27 total in the suite, up from 17).

**Verified for real, three separate times this pass, not assumed:** (1) `test_bitcoin` rebuilt from
clean and run -- 27/27 new+existing `auxpow_tests` cases pass, then the full suite (**766 cases, zero
regressions**); (2) `bitaicoind`/`bitaicoin-cli` themselves rebuilt from clean and version-checked;
(3) a genuine smoke test -- the newly built `bitaicoind` started fresh on a throwaway regtest datadir,
a wallet created, 5 blocks mined and accepted, tip advanced to height 5, clean shutdown via `stop` --
confirming the modified `CBlockHeader` works correctly in an actual running node, not only in unit
tests. The real, separately-running live BitAIcoin node (synced to height 225823) was left completely
untouched throughout -- no new binary was ever pointed at its datadir.

**Status update (2026-09-23, ownership + CBlock + P2P/disk slice): DONE for the tractable, highest-
value parts; two real gaps found and explicitly deferred with a precise audit, not silently skipped.**
Per explicit instruction, this landed BEFORE splicing `CheckAuxPowRules()` into the live acceptance
path, since a real AuxPoW block first needs to be able to reach validation intact at all.

**1. `CBlockHeader::auxpow` ownership/value semantics -- resolved, documented, regression-tested.**
Changed to `std::shared_ptr<const CAuxPow>` (from a plain mutable `shared_ptr<CAuxPow>`), matching
this codebase's own `CTransactionRef` (`shared_ptr<const CTransaction>`) precedent exactly -- not a
new convention. This closes off, at the type level, the exact hazard flagged: an ordinary
`CBlockHeader` copy (which happens throughout this codebase) shares the same proof object as the
original, but since the pointee is now immutable, there is no way to mutate one copy's proof and
have it silently show up in another -- the language does not permit obtaining a non-`const CAuxPow&`
through this member at all. A deep-copy-on-header-copy alternative was considered and rejected: it
would still allow in-place mutation of the (now-distinct) copy's proof, a smaller but real footgun,
and would add a real per-copy cost (coinbase tx + two merkle branches + a full parent header) to
otherwise-cheap header copies. A compile-time regression test
(`static_assert(std::is_same_v<decltype(*std::declval<CBlockHeader>().auxpow), const CAuxPow&>, ...)`
in `src/test/auxpow_tests.cpp`) fails to compile if this is ever changed back, plus a runtime test
(`auxpow_ownership_is_shared_and_immutable`) demonstrating the shared-copy behavior and the correct
way to attach a genuinely different proof (build a new `CAuxPow` value and a new `shared_ptr`, never
mutate in place).

**2. Full `CBlock` (header + auxpow + real transactions) serialization -- resolved, tested.**
`CBlock::SERIALIZE_METHODS` itself is intentionally UNCHANGED (still `AsBase<CBlockHeader>(obj),
obj.vtx` -- base-header-only, byte-for-byte identical to before this slice for every non-AuxPoW
block) because block.h and auxpow.h still cannot include each other (the same circular-include
constraint as CBlockHeader itself). Instead, added `SerializeBlockWithAuxPow`/
`UnserializeBlockWithAuxPow` (`src/auxpow.h`) as the real, separate, explicit auxpow-aware path for
`CBlock`, plus `AuxPowBlockFormatterWithWitness`/`AuxPowBlockFormatterNoWitness` (used via
`Using<>()`, exactly this codebase's own idiom for swapping in alternate serialization logic at a
call site) so real call sites could adopt it as a near-drop-in replacement for `TX_WITH_WITNESS(...)`/
`TX_NO_WITNESS(...)`. 6 new tests cover: byte-for-byte identical output vs. the plain generic
serializer for non-AuxPoW blocks; the proof appearing exactly once in the serialized bytes (not
duplicated, not coincidentally matched inside `vtx`); full round trips with two REAL transactions
(verified by re-deriving `BlockMerkleRoot` from the round-tripped `vtx` and confirming it still
matches); `GetHash()` invariance under attaching/removing the proof at the full-`CBlock` level, not
just the header level; and a truncated/malformed-auxpow stream failing cleanly (`std::ios_base::
failure`) rather than misparsing.

**3. P2P/disk/compact-block audit and wiring -- real call sites fixed, real call sites explicitly
deferred, cited precisely rather than assumed.** Audited (grep + full-context reading, not just
grep hits) every place a `CBlockHeader`/`CBlock` is (de)serialized in `net_processing.cpp`,
`node/blockstorage.cpp`, and `src/blockencodings.h`/`.cpp`:
  - **Fixed: BLOCK message send (3 call sites) and receive (1 call site), `net_processing.cpp`.**
    `TX_NO_WITNESS(*pblock)`/`TX_WITH_WITNESS(*pblock)` replaced with
    `AuxPowBlockNoWitness(*pblock)`/`AuxPowBlockWithWitness(*pblock)` at every real send site
    (lines identified via `grep -n "NetMsgType::BLOCK"`, each read in full surrounding context
    before editing) and the one real receive site. Byte-for-byte identical to before for any block
    with no auxpow attached.
  - **Fixed: disk read/write (4 call sites), `node/blockstorage.cpp`.** `AddBlockFileInfo`'s size
    accounting, `ReadBlock`, and `WriteBlock`'s size computation AND actual write -- all four
    switched to the same `AuxPowBlockWithWitness(block)` formatter, which is what guarantees the
    precomputed size used for file-position bookkeeping always agrees with the size actually
    written (a real risk if these had been fixed inconsistently with each other).
  - **Fixed: BIP152 compact blocks, `src/blockencodings.h`.** `CBlockHeaderAndShortTxIDs`'s own
    `header` field serialization switched to `Using<AuxPowHeaderFormatter>(obj.header)`. Traced the
    full reconstruction path in `blockencodings.cpp` (`CBlockHeaderAndShortTxIDs`'s constructor from
    a `CBlock`, `PartiallyDownloadedBlock::InitData`'s `header = cmpctblock.header`, and
    `FillBlock`'s `block = header`) and confirmed all of it already correctly carries `auxpow`
    through via ordinary C++ member copy/assignment -- ONLY the wire (de)serialization line itself
    needed the explicit fix; the surrounding reconstruction logic needed zero changes.
  - **Explicitly deferred, not silently skipped: HEADERS-message relay (`net_processing.cpp`).**
    The real send/receive shape is more delicate than BLOCK: send constructs `std::vector<CBlock>`
    (each header wrapped as a degenerate 0-transaction `CBlock`, historically so the same
    block-shaped wire format serves both) and serializes it generically; receive instead
    deserializes into `std::vector<CBlockHeader>` via a **manual per-element loop**
    (`vRecv >> headers[n]; ReadCompactSize(vRecv); // ignore tx count; assume it is 0.`) that is not
    a simple generic-vector swap. Fixing this correctly needs its own dedicated pass on both the
    asymmetric send construction and the manual receive loop, not a copy-paste of the BLOCK-message
    fix. Real consequence while deferred: a node running headers-first sync will not receive an
    AuxPoW proof via HEADERS alone; it still arrives correctly via a full BLOCK message (now fixed).
  - **Explicitly deferred, lower priority: BIP37 `CMerkleBlock`/filtered-block path.** A separate,
    legacy, bloom-filter-based wire type; not touched, not audited for a header field.
  - **Explicitly deferred, does not exist yet:** GBT/`createauxblock`/mining -- no code path
    currently constructs a real AuxPoW-flagged block to send in the first place; that's item 22 in
    the "not yet done" list below.

**4. Two-node transport test -- honestly partial, not overclaimed.** Ran a REAL two-process regtest
test (`bitaicoind` A + B, real TCP loopback P2P, real `addnode`): mined 10 ordinary blocks on node A,
confirmed node B received and matched the exact same tip hash via real BLOCK-message relay over the
now-modified code paths above -- a genuine regression proof for the most invasive files touched this
slice (net_processing.cpp, blockstorage.cpp), not a simulation. **What this explicitly does NOT
prove, and was not built this pass:** a literal "node A sends a genuine AuxPoW-flagged block, node B
receives it over real P2P and exposes the intact proof" run. Two real blockers, assessed rather than
glossed over: (a) no code path yet exists that can make a real node actually PRODUCE an AuxPoW-
flagged block through its normal mining pipeline (GBT/mining wiring doesn't exist -- see above), so
node A cannot "just mine one"; (b) constructing and injecting one via a raw, hand-built P2P
version/verack handshake plus a crafted BLOCK message is possible (this codebase has all the needed
primitives -- `CMessageHeader`, the real serialize functions) but is itself a real, separate piece of
test infrastructure not yet built, not a quick addition to this already-large pass. **What WAS
proven instead, as the closest honest substitute:** the EXACT formatter functions now live in the
real net_processing.cpp send/receive call sites (`AuxPowBlockWithWitness`/`AuxPowBlockNoWitness` --
not a parallel reimplementation) are directly unit-tested round-tripping a real AuxPoW proof
byte-for-byte (item 2's tests above). Building the literal raw-handshake two-node AuxPoW-block test
is the natural, explicit next step if wanted, now that the wiring it would be testing actually
exists.

**Verified for real:** full rebuild of `bitcoind`/`bitcoin-cli`/`test_bitcoin` from clean after
touching `net_processing.cpp`, `node/blockstorage.cpp`, and `src/blockencodings.h` (all three
compiled and linked without error); the full test suite (**772 cases, zero regressions**); the real
two-process regtest relay test described above.

**Status update (2026-09-23, HEADERS transport + PoW dispatcher slice): DONE, including the real
AuxPoW transport+acceptance test, kept BEFORE splicing `CheckAuxPowRules()` per explicit instruction.**

**1. HEADERS-message AuxPoW transport -- fixed on both sides, a real deeper gap found and fixed too.**
Attempting the fix surfaced something the earlier audit didn't: `CBlockIndex` (the actual data
HEADERS relay reads from, via `CBlockIndex::GetBlockHeader()`) had **no `auxpow` field at all** --
meaning even a perfect wire-format fix would have had nothing to serialize, since the proof was never
captured anywhere reachable once a block finished being processed as a fresh `CBlock`. Fixed properly,
not worked around:
  - Added `CBlockIndex::auxpow` (`shared_ptr<const CAuxPow>`, same ownership semantics as
    `CBlockHeader::auxpow`), populated in `CBlockIndex(const CBlockHeader&)` (the real capture point --
    every block, including ones just received over P2P, passes through this constructor when first
    indexed) and returned by `GetBlockHeader()`.
  - Persisted it in `CDiskBlockIndex`'s own serialization too, so it survives a restart/reindex --
    without this, a node's own index would silently forget an already-accepted AuxPoW block's proof
    the moment it restarted. **A real bug caught by the compiler, not just reasoned about:** an initial
    version used a plain runtime `if (ser_action.ForRead())` inside the shared `SerializationOps` body
    to decide whether to mutate `obj.auxpow` -- this fails to COMPILE for the write-side instantiation
    (where `obj` is `const CDiskBlockIndex&`), because both branches of a runtime `if` are compiled
    against the same `obj`, unlike `if constexpr`. Fixed using `SER_READ`/`SER_WRITE` (this codebase's
    own real precedent for exactly this, e.g. `merkleblock.h`'s `CPartialMerkleTree`), which generate
    genuinely separate, correctly-const-qualified lambdas per action.
  - Fixed the actual wire format: outgoing HEADERS (2 real send sites) now use
    `AuxPowHeadersForAnnounce()` (`src/auxpow.h`), a small explicit wrapper that writes the historical
    trailing compact-size-0 "as if a CBlock with empty vtx" byte EXACTLY as before, per header, so
    `std::vector<CBlockHeader>` could replace the old `std::vector<CBlock>` trick outright (the old
    code's own comment -- "we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at
    the end" -- is now obsolete and was updated in place, not left stale). Incoming HEADERS' real
    per-element receive loop had its DoS-relevant `max_headers_result`/`Misbehaving()` check left
    completely untouched, with only the one inner deserialization call swapped to
    `UnserializeBlockHeaderWithAuxPow`.
  - `LoadBlockIndexGuts` (`node/blockstorage.cpp`) updated to copy `diskindex.auxpow` into the
    reconstructed in-memory index on load.
  - 3 new tests: a mixed vector (plain, AuxPoW, plain) round-trips correctly; an all-plain vector
    produces byte-for-byte identical output to the old CBlock-wrapping mechanism; a truncated stream
    is rejected cleanly.

**2/3. `CheckBitAIProofOfWork` dispatcher, and the real call-site audit that motivated it.** Built the
single header-level PoW decision point exactly as specified -- `CheckBitAIProofOfWork(const
CBlockHeader&, const Consensus::Params&, BlockValidationState&)` in `src/auxpow.h`/`.cpp` -- with NO
height parameter (that stays `CheckAuxPowRules()`'s job, not merged in). Direct blocks: unchanged
`CheckProofOfWork` on the header's own hash. AuxPoW blocks: the header's own hash is explicitly NOT
checked; a missing proof fails outright; the header's own `nVersion` is confirmed to actually claim
`BITAI_AUXPOW_CHAIN_ID` (16969, now a real named constant, promoted from a proposal-only value) --
**a real gap caught before it was ever tested, not by a failing test:** an early version passed the
constant into `CAuxPow::Check()`'s internal math without ever confirming the header's own declared
identity matched it, which would have accepted a proof mathematically valid against the hardcoded
value while the header claimed to be something else entirely; then `CAuxPow::Check()` runs.

Audited every real call site (grep + full-context reading) where a header's own hash was passed
directly to `CheckProofOfWork`, replacing exactly the ones that needed it and leaving the ones that
correctly don't:
  - **Fixed:** `CheckBlockHeader` (`validation.cpp`) -- the real function called BEFORE
    `ContextualCheckBlockHeader()` in the actual pipeline; left as plain `CheckProofOfWork`, this would
    have rejected every valid AuxPoW header before `CheckAuxPowRules()` (not yet reached) ever got a
    say, regardless of what it would have decided.
  - **Fixed:** `HasValidProofOfWork` (`validation.cpp`) -- net_processing's bulk pre-check on a whole
    received HEADERS batch; same hazard, same fix, contract (bool-only, no exposed per-header reason)
    unchanged.
  - **Fixed:** `BlockManager::ReadBlock` (`node/blockstorage.cpp`) -- checked a freshly-deserialized
    block's own hash right after the already-fixed `AuxPowBlockWithWitness` read; same hazard.
  - **Fixed:** `LoadBlockIndexGuts` (`node/blockstorage.cpp`) -- see item 1 above; this is also where
    the audit's dispatcher fix and the index-storage fix meet.
  - **Correctly left unchanged:** `CAuxPow::Check()`'s own internal `CheckProofOfWork(parentBlock.
    GetHash(), ...)` (that's the PARENT's hash, the intended real check, not a bug); the dispatcher's
    own direct-block branch (same reason); the brute-force nonce loop in `rpc/mining.cpp` (mining code
    that, today, can only ever produce non-AuxPoW blocks -- not a validation call site).
  - 5 new tests: direct blocks match ordinary `CheckProofOfWork` exactly (both accept and reject
    cases); an AuxPoW block with an "impossible" own-hash but a valid proof is correctly accepted
    (proving the own-hash truly isn't checked); missing proof and wrong-chain-id both rejected with
    the right reasons.

**4/5. The real AuxPoW transport + acceptance test -- built and passing, before splicing
`CheckAuxPowRules()`, per explicit instruction.** Used this codebase's own real test precedent for
constructing valid test blocks (`validation_block_tests.cpp`'s `MinerTestingSetup` pattern: a real
`BlockAssembler` template, real coinbase/height wiring, real `GenerateCoinbaseCommitment` for
regtest's active SegWit, real merkle root) rather than inventing a parallel mechanism. The test: mines
3 real direct blocks to establish a small real chain and a real, DAA-computed `nBits`; builds a
competing AuxPoW-flagged candidate at the same height using that SAME real required `nBits` (not a
test-only override); builds a genuinely valid `CAuxPow` proof against it; **transports** it through the
exact real wire functions now live in net_processing.cpp/blockstorage.cpp
(`SerializeBlockWithAuxPow`/`UnserializeBlockWithAuxPow` -- not parallel copies); and hands the
post-transport block to the REAL acceptance pipeline (`ChainstateManager::ProcessNewBlockHeaders` then
`ProcessNewBlock` -- the same functions net_processing.cpp itself calls). Confirms: header state valid,
block accepted, and the resulting real `CBlockIndex` entry has its `auxpow` populated and its version
correctly flagged -- proving the whole pipeline including the `CBlockIndex` capture point from item 1,
not just isolated function calls.

**A real, previously-unknown bug was caught by this test failing on its first run, not found by
inspection:** an early version built the AuxPoW candidate's version via `MakeAuxpowVersion
(BITAI_AUXPOW_CHAIN_ID, auxBlock->nVersion)` -- passing the template's WHOLE pre-existing version
(which already carries real BIP9 signaling bits, e.g. shaped like `0x20000000`) as the "base version"
argument. Since BIP9's signaling bits (top 3 bits, 29-31) and AuxPoW's chain-ID field (bits 16-31)
**genuinely overlap** in this encoding, OR-ing the whole template version in silently corrupted the
encoded chain ID (16969 became 25161), and the dispatcher correctly rejected it. **This is a real,
open design question surfaced by a test failure, not resolved here:** a real miner constructing an
AuxPoW candidate cannot naively combine an unmodified BIP9-signaling template version with the
chain-ID-in-version-bits scheme without one clobbering the other. Worth a dedicated look whenever
GBT/AuxPoW mining is actually built (item scoped for later, per instruction to keep
`createauxblock`/`submitauxblock` a separate commit). Fixed in the test itself by using a plain, small
base version, matching every other test's usage in this file -- honest about being a test-scope fix,
not a resolution of the underlying question.

**Honest scope note on "transport":** this exercises the real validation/serialization code in a
single test process via direct `ChainstateManager` calls, not two literal OS processes over raw P2P
sockets -- explicitly endorsed as acceptable/preferable per instruction ("a deterministic
functional-test fixture ... is acceptable and preferable to waiting for the mining RPC
implementation"). It proves the exact same functions net_processing.cpp calls preserve and correctly
validate a real proof end to end; a literal two-process raw-socket run would exercise the identical
validation code path, differing only in the socket-framing layer, which carries no AuxPoW-specific
risk of its own.

**Verified for real:** full rebuild of `bitcoind`/`bitcoin-cli`/`test_bitcoin` from clean (multiple
passes, including after catching and fixing 2 real bugs -- the `SER_READ`/`SER_WRITE` compile error
and the `MakeAuxpowVersion` encoding bug); the full test suite (**780 cases, zero regressions**); TWO
real two-process regtest tests, one of which included a full node restart (mining 20 blocks on node A,
confirming relay to node B, restarting node A entirely -- exercising the new `CDiskBlockIndex`
persistence and `LoadBlockIndexGuts`'s new dispatcher call for real -- and confirming both nodes still
match tip hash exactly after reload).

**Status update (2026-09-23, two architectural decisions frozen + implemented, still BEFORE splicing
`CheckAuxPowRules()`).** The real transport/acceptance test from the previous pass was strong enough
to surface two genuine architectural questions worth resolving while the chain is still private,
rather than after activation. Both are now frozen, implemented, and verified -- not left as
recommendations.

### Decision 1: BIP9 versionbits vs. AuxPoW's nVersion encoding

**Real, not theoretical -- proven by the previous pass's own test failing.** Audited precisely
(`src/versionbits.cpp`/`versionbits_impl.h`): BIP9 uses ALL 29 low bits of `nVersion`
(`VERSIONBITS_NUM_BITS = 29`) for deployment signaling, with a FIXED 3-bit marker (`0b001`) in the TOP
3 bits (29-31) -- `Condition()` requires `(nVersion & VERSIONBITS_TOP_MASK) == VERSIONBITS_TOP_BITS`
(`0x20000000`/`0xE0000000`) before treating ANY bit as a real signal. AuxPoW's chain-ID field occupies
`nVersion` bits 16-31 -- meaning its own top 3 bits sit EXACTLY at BIP9's marker position, and its flag
bit (8) sits squarely inside BIP9's 29-bit signaling range.

Audited BitAIcoin's real, live deployment configuration for every chain type (`src/kernel/chainparams.cpp`):
only two deployments exist, `DEPLOYMENT_TESTDUMMY` (bit 28) and `DEPLOYMENT_TAPROOT` (bit 2), BOTH
hardcoded `nStartTime = NEVER_ACTIVE` -- meaning `ComputeBlockVersion` can never actually set either
bit today (confirmed, not assumed: this matches the plain `0x20000000`-shaped version this whole
milestone's own tests have observed `BlockAssembler` produce). BitAIcoin's chosen chain ID (16969 =
`0x4249`) happens to have top-3-bits `0b010`, NOT `0b001` -- no live collision today, by coincidence,
not by design.

Compared the three options laid out explicitly: (A) classic Namecoin/Dogecoin encoding, formally
retiring BIP9 versionbits at/around activation; (B) a limited versionbits scheme with the AuxPoW
bit/chain-ID range permanently reserved and proven non-colliding; (C) move AuxPoW identification
outside `nVersion` entirely (real BIP9 compatibility, real classic-AuxPoW-tooling incompatibility).
**Chose Option A, formalized and code-enforced** (not just documentation) -- matching classic
Namecoin/Dogecoin/Syscoin wire semantics (this milestone's own named references), which don't layer
BIP9 versionbits under AuxPoW the way this fork's inherited stock code structurally allows:
- **Reserved bit 8 and bits [16,31] permanently** for AuxPoW; no BitAIcoin versionbits deployment,
  current or future, may claim them. Found a REAL, existing violation while writing the test for this:
  `DEPLOYMENT_TESTDUMMY`'s bit 28 sits inside the reserved chain-ID range, across every chain type.
  Fixed by reassigning it to bit 15 (`src/kernel/chainparams.cpp`, all 6 chain-type definitions) --
  zero real-world effect, since TESTDUMMY is a permanently-`NEVER_ACTIVE` internal placeholder used
  only by `versionbits_tests.cpp`'s own state-machine tests, referenced there symbolically, never by
  hardcoded bit value.
- **Defensive runtime check, `CheckBitAIProofOfWork`** (`src/auxpow.cpp`): rejects outright any header
  that is BOTH AuxPoW-flagged AND shaped like a BIP9-signaling version
  (`(nVersion & VERSIONBITS_TOP_MASK) == VERSIONBITS_TOP_BITS`) -- ambiguous by construction, rejected
  rather than silently interpreted one way. This is the REAL enforcement, independent of chain-ID
  value or future encoding changes.
- **Compile-time proof, `auxpow.h`**: a `static_assert` confirms `BITAI_AUXPOW_CHAIN_ID`'s encoding
  never produces the BIP9 marker, checked at every compile, not left as a one-time manual calculation
  (local copies of the two BIP9 constants are used to avoid a real circular include: `versionbits.h`
  includes `chain.h`, which now -- see Decision 2 -- no longer needs to include `auxpow.h` either, but
  did during development, so the header-only copy was kept as the simpler, permanent fix).
- **Real, tested invariant, not just documentation**: a new test
  (`no_live_versionbits_deployment_reserves_an_auxpow_bit`) iterates the REAL chain params for every
  chain type this fork defines and asserts none collide -- this is what caught the TESTDUMMY bug
  above, and will catch a future contributor's mistake via CI rather than relying on someone reading
  this document first.
- Two new tests total (the collision-rejection test plus the reservation-invariant test).

### Decision 2: `CBlockIndex`/`CDiskBlockIndex` resident AuxPoW proof vs. on-demand disk read

**Real, measured comparison, not a guess.** Measured a realistic serialized `CAuxPow` size directly
(a small standalone tool linked against the real `bitcoin_common` library, not estimated): a minimal
single-tx-parent-block proof is 231 bytes; a modest 2-level coinbase merkle branch (~4-tx parent) is
295 bytes; an 11-level branch (2048-tx parent, closer to a real mature Bitcoin block) is 583 bytes --
**~600 bytes is the realistic per-block figure** for a chain actually merge-mined against real
Bitcoin. The (now-reverted) resident design added this as a PERMANENT, PER-BLOCK cost in two places:
  - **`CDiskBlockIndex`'s own leveldb block-index DB entry** (persisted, never pruned, loaded at every
    startup): at ~600 bytes/block, a mature merge-mined chain reaches **~600 MB extra at 1,000,000
    blocks, ~3 GB at 5,000,000, ~6 GB at 10,000,000** -- purely for data real usage needs only
    occasionally (serving a HEADERS request or a future RPC), not on every ordinary index scan.
  - **Resident `CBlockIndex` C++ objects in RAM**: the deserialized, heap-allocated object graph
    (`CTransaction`'s own STL vectors, `CScript` prevector overhead, shared_ptr control blocks) is
    larger than the raw serialized bytes -- a defensible estimate is **~1.0-1.5 GB extra resident
    memory at 1,000,000 AuxPoW blocks, ~5-7.5 GB at 5,000,000, ~10-15 GB at 10,000,000** -- clearly
    untenable for an ordinary node operator's machine at real chain-history scale.
  - The `shared_ptr<const CAuxPow>` slot itself (16 bytes) on EVERY `CBlockIndex`, even non-AuxPoW
    ones, was a smaller but still real, permanent, unconditional tax (~160 MB at 10,000,000 blocks)
    the on-demand design avoids entirely, since `IsAuxpowVersion(nVersion)` (already resident, zero
    marginal cost) is sufficient to know whether a proof exists at all.

**Chose the on-demand design** (matching the real precedent named: Viacoin Core 30.x's approach),
per the explicit default ("prefer the on-demand disk-header approach unless there is a strong measured
reason to keep the proof resident" -- no such reason was found; the measured numbers all point the
other way). Implemented, not just decided:
  - **Reverted** `CBlockIndex::auxpow` and `CDiskBlockIndex`'s persistence of it entirely (`chain.h`).
    `CBlockIndex` carries no more AuxPoW-specific state than `nVersion` already gave it for free.
    `CBlockIndex::GetBlockHeader()` is now, correctly, ALWAYS proof-less (a pure, no-I/O function, as
    it should be) -- callers needing the real proof must ask for it explicitly.
  - **Added `BlockManager::ReadBlockHeaderWithAuxPow()`** (`node/blockstorage.h`/`.cpp`): opens the
    block file at the index's recorded position (mirroring `ReadRawBlock`'s own file-opening and
    magic/size-prefix validation) and calls `UnserializeBlockHeaderWithAuxPow` DIRECTLY on the open
    stream -- deliberately does NOT read the block's transactions into memory at all, an efficient
    partial read matching the entire point of "on demand," not "read the whole block and discard most
    of it."
  - **Rewired every real call site** that previously relied on `CBlockIndex::GetBlockHeader()` for a
    proof-bearing header: both real HEADERS-announce sites in `net_processing.cpp` now go through a
    small new helper, `GetHeaderForAnnounce()`, which calls the on-demand read only for
    AuxPoW-flagged entries (zero extra cost for anything else) and falls back to the existing
    `fRevertToInv`/break-out-of-the-loop path if the proof genuinely can't be read (matching how this
    code already handles other "can't cleanly announce" cases). `LoadBlockIndexGuts`
    (`node/blockstorage.cpp`) -- which re-validates PoW for every historical block on every ordinary
    startup, confirmed to be STOCK, pre-existing Bitcoin Core behavior, not something introduced this
    milestone -- now fetches the real proof on demand via a new callback parameter
    (`readAuxPowHeader`, threaded through from `BlockManager::LoadBlockIndex`, mirroring the
    already-existing `insertBlockIndex` callback pattern, since `LoadBlockIndexGuts` is a
    `BlockTreeDB` method with no `BlockManager` instance of its own to call the read on).
  - **Real trade-off, disclosed rather than hidden**: this means an ordinary node startup now does a
    genuine blk-file random-access read for every HISTORICAL AuxPoW-flagged block, to re-derive its
    PoW validity from scratch, every time -- a real I/O cost proportional to AuxPoW-block count that
    the (now-reverted) resident design didn't have. This is judged an acceptable trade against
    multi-GB permanent resident memory/DB growth, and is a separate, disclosed opportunity for a
    FUTURE optimization (e.g. trusting `nStatus`'s already-recorded `BLOCK_VALID_HEADER` for an
    ordinary restart and only fully re-deriving during an explicit `-reindex`) -- not implemented here,
    since it would be a change to stock-inherited validation behavior beyond AuxPoW's own scope.
  - **Prune-mode verified, not assumed**: a pruned node deletes old `blk*.dat` files, so
    `ReadBlockHeaderWithAuxPow()` correctly returns `std::nullopt` for a pruned historical AuxPoW
    block -- **the identical, pre-existing limitation stock Bitcoin Core already has for full block
    BODIES under pruning** (`ReadBlock`/`ReadRawBlock` already fail the same way for a pruned
    position), not a new limitation this design introduces. What a pruned node keeps regardless,
    unaffected by any of this: the lean block-index metadata for EVERY header (height, hash, `nBits`,
    `nVersion`, chainwork) is never pruned (pruning only removes `blk`/`rev` data files, never the
    block-index DB) -- so a pruned node can always validate chain-of-headers/difficulty continuity and
    knows WHICH historical blocks were AuxPoW-flagged, even for ones whose actual proof it can no
    longer reproduce or re-serve.
  - Updated the real transport+acceptance test from the previous pass to verify the on-demand
    mechanism end to end (not just that acceptance succeeds): after `ProcessNewBlock`, confirms
    `GetBlockHeader()` is correctly proof-less, then calls `ReadBlockHeaderWithAuxPow()` and confirms
    it returns the real, correct proof read back from the block file -- a stronger test than the
    previous resident-field check, since it exercises the actual on-demand code path for real.

**Verified for real:** full clean rebuilds (multiple passes, after catching and fixing 2 more real
bugs -- `ReadBlockHeaderWithAuxPow` initially called directly from `BlockTreeDB::LoadBlockIndexGuts`,
which doesn't have a `BlockManager` instance to call it on, fixed via the callback-parameter pattern;
and the TESTDUMMY bit-28 collision the new reservation-invariant test caught on its first run); the
full test suite (**782 cases, zero regressions**); a real two-process regtest test including a full
node restart, confirming ordinary (non-AuxPoW) operation is completely unaffected by either change.

**Explicitly still NOT done, next slice (per instruction, now the very next step):** splicing
`CheckAuxPowRules()` into the real `ContextualCheckBlockHeader()` call path. The DAA branch (ASERT,
sec.3/A) is validated as a standalone module (`contrib/asert_reference.py`, now with a proven
arithmetic bound and Decred differential vectors -- see the Addendum below) but likewise not yet wired
into `pow.cpp`'s real `GetNextWorkRequired` dispatch. New RPCs, the obsolete-node fork test (protocol
frozen in sec.C), and the stabilization
checkpoint (sec.D) all remain after that wiring lands.

## Addendum (2026-09-23): frozen details, before the first consensus commit

Adds precision on top of §1-5 above per explicit follow-up instruction. Nothing in §1-5 above is
changed; this section makes each item exact and adds three new required items.

### A. ASERT specification, exact (BCH/BCHN `aserti3-2d`, not "ASERT-style")

Reference: the BCH `aserti3-2d` specification (adopted via CHIP-2020-05, implemented in BCHN's
`pow.cpp`), kept as a *separate* reference from the Namecoin/Syscoin AuxPoW references in §4 above
-- these are two unrelated pieces of prior art (a difficulty algorithm and a merge-mining proof
format) and should not be conflated.

- **Target block interval:** 600 seconds -- reuses BitAIcoin's existing `nPowTargetSpacing`
  unchanged. No new constant introduced for this.
- **Anchor block:** height 227807, the last block validated under the legacy (pre-activation)
  retarget algorithm.
- **CORRECTION (2026-09-23) to the anchor/time-reference convention, found by reading BCHN's actual
  `pow.cpp` rather than continuing to derive it from prose, per explicit instruction:** my first-pass
  text above said `anchor_target` and `anchor_time` are read from the anchor block's own header, and
  the exact-formula bullet (kept below, now corrected) said `time_diff` = current block's time minus
  *anchor block's* time. **Both were wrong in the same way.** The real BCHN convention, confirmed
  from `GetNextASERTWorkRequired`/`CalculateASERT` in
  [bitcoin-cash-node/bitcoin-cash-node `src/pow.cpp`](https://github.com/bitcoin-cash-node/bitcoin-cash-node/blob/master/src/pow.cpp)
  and the [official upgrade spec](https://upgradespecs.bitcoincashnode.org/2020-11-15-asert/):
    - `time_diff` = the **tip block's** time (i.e. `pindexPrev`, the most recently connected block
      when computing the *next* block's target -- never the being-mined block's own time, since
      that's unknown at target-computation time) minus the **anchor block's PARENT's** time, not the
      anchor block's own time.
    - `height_diff` = the tip block's height minus the anchor height.
    - `anchor_target` = the anchor block's own `nBits`, which *is* read from its own header (this
      part was right).
  For BitAIcoin, concretely: `anchorParams = {nHeight: 227807, nBits: <227807's own bits>,
  nPrevBlockTime: <block 227806's time>}`. When computing the target for block N (N > 227807), the
  tip is block N-1, so `time_diff = time(N-1) - time(227806)` and `height_diff = height(N-1) -
  227807`. This must be implemented exactly this way, not "from the anchor's own timestamp" -- it is
  the kind of off-by-one-block, easy-to-get-wrong-from-memory detail the instruction to "test the
  timestamp-reference convention rather than deriving it from prose" was specifically about, and
  reading the real source (not just the spec prose, which is easy to mis-paraphrase on this exact
  point) is what caught it.
- **Exact formula (corrected to match the real convention above):**
  `next_target = anchor_target * 2^((time_diff - target_interval*(height_diff+1)) / halflife)`,
  with `time_diff`/`height_diff` as just defined (tip-vs-anchor's-parent, tip-vs-anchor). The
  `height_diff + 1` (not `height_diff`) is deliberate and matches the reference spec: it accounts for
  the anchor block itself already having "used up" one interval's worth of schedule, so a block
  arriving exactly on schedule produces exponent 0 (no change) rather than a small permanent bias.
- **Integer algorithm, transcribed line-for-line from the real source** (not reconstructed from
  memory of the spec prose) into `contrib/asert_reference.py::calculate_asert()`, including the
  actual polynomial coefficients (`195766423245049`, `971821376`, `5127`, rounding constant `2^47`,
  `>> 48`, radix `65536`), the integer-shift/fractional-remainder split, and the left-shift overflow
  clamp. **Validated in this session** against the algorithm's own defining algebraic properties
  (exact identity on-schedule, exact doubling at +halflife, exact halving at -halflife, monotonicity,
  correct clamping) rather than against literal third-party numeric test-vector rows -- disclosed
  limitation: the GitLab `qa-assets` CSV test vectors
  ([bchn-sw/qa-assets](https://gitlab.com/bitcoin-cash-node/bchn-sw/qa-assets/-/tree/master/test_vectors/aserti3-2d))
  returned HTTP 403 to automated fetch this session, and the available web-fetch tool paraphrases
  fetched pages rather than passing through raw bytes, so literal upstream row values could not be
  independently byte-confirmed here. The algebraic self-consistency checks are a real, defensible
  validation (they're exact mathematical consequences of the formula, not something a wrong
  transcription would pass by luck), but obtaining the literal CSV and diffing it byte-for-byte
  remains the stronger check and should be done from a normal browser/`git clone` before this ships
  as consensus code, not asserted as already done here.
- **Real, concrete finding from this validation, not present in the first-pass addendum:** BCHN's
  `CalculateASERT` hard-asserts `(powLimit >> 224) == 0`. BitAIcoin's actual `consensus.powLimit`
  (`src/kernel/chainparams.cpp:289`, confirmed by reading the source directly) has only 28 leading
  zero bits (`bit_length() == 228`), 4 bits short of the 32-bit margin real Bitcoin/BCH mainnet's own
  powLimit has, so it **fails** this precondition outright. Because this is a plain C `assert()`,
  which is compiled out under `NDEBUG` in a release build, this would not crash a release binary --
  the actual overflow protection is the separate, unconditional runtime clamp at the left-shift step
  (`if ((nextTargetShifted >> shifts) != nextTarget) nextTarget = powLimit;`), which does not depend
  on this assert. But a debug build would abort on the very first ASERT retarget after activation,
  and copying the assert verbatim would be silently wrong for BitAIcoin's own, intentionally wider,
  powLimit.

  **Second pass, arithmetic re-derived and PROVEN rather than trusted (2026-09-23, per explicit
  follow-up gate before wiring anything into the live block-acceptance path):** the first-pass
  relaxed precondition above, `powLimit >> 240 == 0`, was itself only reasoned informally
  ("factor < 2*RADIX = 2^17, so 16 bits of headroom suffices") and was never actually checked against
  the exact polynomial or the real multiplication semantics. Checked this pass and found **it was off
  by exactly one bit**:
  - Read `src/arith_uint256.cpp`'s `operator*=(uint32_t)` directly (the exact overload
    `refTarget * factor` resolves to, since `factor` is a `uint32_t`): it is plain schoolbook
    fixed-width 256-bit multiplication that discards the final carry out of the top limb with **no
    overflow detection whatsoever**. This is the same arithmetic BCHN and Decred's `dcrd` both target
    for this step -- see below, though, for a real divergence in how each project actually handles it.
  - Exhaustively searched (not sampled) the full `uint16_t` fractional domain (`contrib/asert_reference.py::_prove_factor_max()`)
    and confirmed the true maximum `factor` is exactly **131071 = 2^17 - 1**, at `frac=65535`.
  - Derived the exact boundary (`_prove_powlimit_bound()`): a worst-case **239-bit** `powLimit`
    (`2^239-1`) times `131071` has bit-length exactly 256 (fits, safe); a worst-case **240-bit**
    `powLimit` (`2^240-1`) times `131071` has bit-length **257** (does NOT fit -- silently wraps).
    **The correct, proven bound is `powLimit >> 239 == 0`, not `>> 240`.**
  - BitAIcoin's real powLimit (228 bits) was never actually at risk either way -- it has 11 bits of
    real margin under the correct 239-bit bound. What was wrong was the *stated* margin, not
    BitAIcoin's own value; still, this is exactly the kind of one-bit arithmetic error that must be
    proven, not asserted from a rule of thumb, before it goes into consensus code. `calculate_asert()`
    now asserts the corrected `powLimit >> 239 == 0` and separately runs an explicit **checked-
    arithmetic** simulation of the real fixed-width truncation on every call (computes the product at
    full Python precision, masks to 256 bits, and asserts the two are equal), so running the file is
    itself a live proof the precondition holds for whatever `pow_limit` is passed in, not just a
    paper argument -- the "prefer checked arithmetic... if that gives a clearer invariant" instruction
    is implemented literally, not just discussed.
  - **A real design divergence worth carrying into the eventual C++ port, found while building the
    differential test below:** BCHN's `CalculateASERT` and Decred's `dcrd` (`CalcASERTDiff` in
    `blockchain/standalone/pow.go`) both use the identical polynomial, but **not** the identical
    arithmetic. BCHN uses fixed-width `arith_uint256` (the overflow risk above) and relies entirely on
    its own powLimit being small enough by construction. Decred's Go implementation instead performs
    this exact multiplication with `math/big.Int` -- genuine arbitrary precision, confirmed by reading
    `dcrd`'s own source (`nextDiff.Mul(nextDiff, big.NewInt(int64(fracFactor)))`) -- which has *no*
    fixed-width overflow risk at all, at the cost of a heap-allocating bignum operation in a
    consensus-hot path (a real cost BCHN's design avoids). BitAIcoin's real powLimit has enough margin
    that either approach is currently safe, but **the eventual C++ port should pick one of these two
    designs deliberately** (a precondition-plus-checked-arithmetic assertion in the BCHN style, or a
    genuinely wider intermediate in the Decred style) rather than silently inheriting BCHN's
    unchecked-multiply design without the discipline BCHN gets away with only because its own powLimit
    is conservative by construction.

  **Independent differential validation, not just this port's own algebraic self-consistency:**
  fetched Decred's real, literal `blockchain/standalone/testdata/asert_test_vectors.json` (saved at
  `contrib/decred_asert_test_vectors.json`, 1,402 individual test rows across 17 scenarios, retrieved
  directly via `curl` this pass specifically because the earlier BCHN GitLab CSV fetch had returned
  HTTP 403 through a summarizing fetch tool -- this file was fetched as raw bytes, not summarized).
  Decred's `CalcASERTDiff` uses the identical polynomial but a slightly different height-delta
  convention (no "+1" -- confirmed by reading `dcrd`'s own source, not assumed), so the vectors were
  run through `calculate_asert()` with the explicit, documented mapping
  `my_height_diff = (height - startHeight) - 1` to compensate. Result: **1,401 of 1,401 real,
  applicable upstream vectors matched exactly** (mainnet + testnet params, both safely inside the
  fixed-width bound; the one remaining row per scenario is each scenario's own height=0 anchor
  baseline, not a real retarget call, correctly not run; Decred's `simnet` params use a deliberately
  near-256-bit powLimit that only their arbitrary-precision design supports and were correctly
  excluded rather than silently forced through the fixed-width port). Also added, per explicit
  instruction: compact-target (`nBits`) encode/decode boundary round-trip tests (ported line-for-line
  from `arith_uint256::SetCompact`/`GetCompact`), extreme positive/negative exponent cases, and
  explicit `target=1` / `target=powLimit` clamping cases -- all in `contrib/asert_reference.py`, all
  passing. Reproducible: `python3 contrib/asert_reference.py`.
- **powLimit / clamping behavior, corrected from my first pass:** the ceiling that matters is
  `next_target <= consensus.powLimit` (targets can't get *easier* than the chain-wide floor
  difficulty) -- **not** a floor preventing targets from getting *harder*; there is no consensus
  minimum on the hard side other than the practical limits of the 256-bit representation. I had
  this backwards in my first internal pass at simulating it; caught it before committing anything
  by deriving the target/hashrate relationship analytically rather than trusting a first buggy
  simulation run (details in §B).
  **Concretely relevant to BitAIcoin today:** verified from the live node that the chain has been
  running at exactly `consensus.powLimit` (bits `0x1d0fffff`) since block 225430, unchanged across
  the one retarget that's already happened -- so relative to the anchor block, the powLimit ceiling
  is *already binding*. Any future hashrate withdrawal cannot ease the target further from where it
  already sits; it can only slow blocks down, with no consensus-level relief available. This is
  correct, expected behavior for a chain already at its easiest allowed setting, not a bug.
- **Compact-target (`nBits`) rounding:** Bitcoin's compact format keeps only ~3 significant bytes of
  mantissa, so converting a full 256-bit target to `nBits` always rounds toward the nearest
  representable compact value (in practice, rounds the true target down slightly). ASERT's design
  avoids this compounding block-over-block *because it always recomputes from the anchor's original,
  full-precision target*, not from a previously-rounded `nBits` value re-expanded -- the rounding
  error doesn't accumulate the way it can in an EMA-based algorithm. This must be implemented as
  "recompute from the anchor every time," not "carry forward the last computed target," or that
  property is lost.

#### Half-life: NOT frozen. Expanded study below; the earlier "36x" claim is retracted.

**The earlier claim in this doc that a 6-hour half-life is "36x harder to whipsaw" than 1 hour is
retracted outright, per explicit instruction.** It was arithmetically wrong with no defensible
source (6h / 1h = 6x, not 36x -- there is no calculation that produces 36 from the numbers actually
in play). It should never have been written without being derived and shown. It is not being
"substantiated" after the fact; it's dropped.

**Replacement metric, precisely defined and reproduced, from BitAIcoin's real, unmodified consensus
timestamp constants** (confirmed via `grep` against `src/chain.h`/`src/validation.cpp`:
`MAX_FUTURE_BLOCK_TIME = 2*60*60` seconds, `nMedianTimeSpan = 11` blocks, hard rejection at
`nTime > now + MAX_FUTURE_BLOCK_TIME` and at `nTime <= GetMedianTimePast()` of the previous 11
blocks): the maximum inflation a single block's own target can receive from one miner maximally
lying about that one block's timestamp (shifting it the full allowed +7200s into the future) is
exactly `2^(MAX_FUTURE_BLOCK_TIME / halflife)`:

| half-life | max single-block target inflation via a +7200s timestamp lie |
|---|---|
| 1 hour (3600s) | **4.0000x** |
| 6 hours (21600s) | **1.2599x** |
| 1 day (86400s) | **1.0595x** |
| 2 days (172800s) | **1.0293x** |

This is a single-block, one-shot metric (one miner, one lied timestamp, immediate next-block
target), not a model of a sustained multi-block manipulation campaign -- stated as a limitation of
the metric itself, not hidden.

#### Expanded study: surge/recovery, pool-hopping, stochastic, and MTP-floor-aware

The prior version of this section used a floating-point behavioral stand-in for the DAA, permanent
shocks only, deterministic arrivals only, and no timestamp-consensus constraints -- explicitly
called out as insufficient ("the current withdrawal simulation only establishes that a hashrate drop
... cannot be compensated because the chain is already at powLimit") and redone from scratch per
instruction. The new study (`contrib/asert_halflife_simulation.py`, built on the bit-exact
`calculate_asert()` port validated in §A above) adds:

1. **Surge-then-revert** (not permanent-only): 1x baseline -> {10x, 100x, 1000x} held for
   {20, 100, 500} blocks -> back to 1x, with recovery measured *after* the surge ends, not from t=0.
2. **Repeated on/off pool-hopping cycles** at several on/off block-count combinations.
3. **A stochastic (Poisson-arrival) variant** alongside the deterministic (expected-value) model, 5
   seeds per half-life.
4. **BitAIcoin's real MTP-floor and future-time-ceiling constraints** applied to every simulated
   timestamp (a block's recorded time cannot go below `MTP(previous 11 blocks) + 1`).

**Surge-then-revert (selected rows; full table in the script's own output, reproducible via
`python3 contrib/asert_halflife_simulation.py`):**

| half-life | surge | held | blocks to recover post-surge | wall-clock to recover | worst instant rate multiple in transition |
|---|---|---|---|---|---|
| 1 hour | 1000x | 20 blk | 18 | 9.5h | 120.0x |
| 1 hour | 1000x | 500 blk | 2 | 169.9h (~7.1d) | 999.2x |
| 6 hours | 1000x | 20 blk | 61 | 16.1h | 600.0x |
| 6 hours | 1000x | 500 blk | 2 | 160.0h (~6.7d) | 939.7x |
| 1 day | 1000x | 20 blk | 1 | 3.7h | 600.0x |
| 1 day | 1000x | 500 blk | 458 | 159.2h (~6.6d) | 85.7x |
| 2 days | 1000x | 20 blk | 1 | 3.5h | 600.0x |
| 2 days | 1000x | 500 blk | 809 | 214.3h (~8.9d) | 300.0x |

Real finding, not previously visible under the permanent-shock-only model: for **short** surges
(held only 20 blocks), longer half-lives barely react at all before the surge ends, so "recovery"
looks nearly instantaneous (1-2 blocks) simply because there was nothing to recover *from* -- the
DAA never moved. For **long, sustained** surges (held 500 blocks), the ordering flips: the fast
half-lives (1h, 6h) have already eased the target most of the way toward the new equilibrium by the
time the surge ends and recover almost immediately (2 blocks), while the slow half-lives (1 day, 2
days) are still mid-adjustment when the shock ends and take hundreds of blocks and many days of
wall-clock time to finish settling. **A short half-life is not strictly "worse" here -- it's better
adapted to long sustained shocks and worse adapted to resisting brief manipulation; a long half-life
is the reverse.** This is the real tradeoff the half-life choice has to make, and it only shows up
once surge duration is varied, which the original permanent-only model could not reveal.

**Pool-hopping (10x/50-on/50-off, 6 cycles; 100x/20-on/100-off, 4 cycles; 1000x/10-on/200-off, 3
cycles):** worst instantaneous rate multiple during any cycle scales with the on-hashrate multiplier
roughly as expected (10.0x / 100.0x / 600.0x respectively, consistent across all four half-lives --
the *ceiling* of the swing is set by the multiplier itself, not by the half-life). Whether difficulty
fully re-settles to 1x by the *end* of each off-phase is more sensitive to the specific on/off block
counts chosen than to half-life alone (full table in script output) -- worth re-running with
production-realistic on/off durations once real pool behavior is observed, rather than reading too
much into the specific numbers from these illustrative cycle lengths.

**Stochastic vs. deterministic -- a real methodological finding, not just an alternative run:**
applying the deterministic report's strict metric ("20 consecutive individual blocks each within 10%
of target") to the Poisson-arrival logs returned **no settling point for any seed at any half-life**.
Investigated rather than silently switched away from: a Poisson arrival process has per-block
coefficient of variation of 1.0 (stdev == mean for an exponential distribution), so individual
blocks routinely land 2-5x off the mean purely from honest statistical noise, making "20 individual
blocks in a row within 10%" essentially unsatisfiable regardless of half-life or DAA quality. That is
a real property of the *metric*, not of the DAA, once arrivals are genuinely random rather than
expected-value-only -- disclosed as a limitation of the original metric definition, now fixed with a
rolling-window metric (mean rate over a trailing 20-block window, checked for persistence over the
following window) that is actually meaningful under stochastic noise:

| half-life | deterministic recovery (windowed) | stochastic recovery (windowed), 5 seeds |
|---|---|---|
| 1 hour | 21 blocks | 20-110 blocks (mean ~49) |
| 6 hours | 99 blocks | 70-138 blocks (mean ~96) |
| 1 day | 203 blocks | 32-104 blocks (mean ~66) |
| 2 days | 154 blocks | 20-149 blocks (mean ~61) |

The half-life ordering is not perfectly monotonic under either metric at this scenario (100x/100blk
surge) -- both the deterministic and stochastic windowed numbers show 1 day recovering *faster* than
6 hours here, which is a genuine artifact of exactly how far a 100x/100-block surge pushes each
half-life's target before reverting (a 6-hour half-life has moved further from anchor by block 100
than a 1-day one has, so has more distance to unwind) rather than a general "slower half-lives always
recover faster" rule -- the surge-then-revert table above, which varies surge duration, is the more
complete picture. Under real stochastic noise, recovery times for any single half-life vary by roughly
2-5x across seeds, which any half-life decision needs to tolerate as normal variance, not treat as a
DAA malfunction.

**MTP-floor impact -- a genuine, disclosed constraint, but a narrow one:** the real MTP floor (nTime
must exceed the median of the previous 11 blocks) essentially never binds for the 10x-1000x, ≤500-block
scenarios above (0 blocks clamped in every case checked). It only starts to bind under a much more
extreme, sustained combination -- a 10,000x surge held for 500 blocks -- and even then only for a
minority of blocks: 5/500 at 1-hour half-life, 20/500 at 6 hours, 75/500 at 1 day, 85/500 at 2 days.
**Counterintuitive but explainable finding:** the MTP floor binds *more*, not less, at longer
half-lives under an extreme sustained surge, because a slower-reacting DAA leaves the target closer
to its pre-surge (harder-to-mine-for-the-new-hashrate) value for longer, which is exactly the
condition (target still "too easy" relative to the surged hashrate) that drives the algorithm's own
*expected* block interval below one second, at which point the integer-second timestamp floor -- not
the DAA -- becomes the binding constraint. This is a second, independent point in favor of a shorter
half-life for resisting extreme sustained arrival shocks specifically, to be weighed against the
single-block timestamp-manipulation metric above, which favors a *longer* half-life.

**No half-life is selected here.** Per instruction, this section compares {1h, 6h, 1d, 2d} on the
full expanded scenario set and leaves the consensus value open. The tradeoffs now visible: short
half-lives adapt faster to long sustained shocks and resist the MTP-floor interaction better, but are
more exposed to single-block timestamp manipulation and to whipsaw during pool-hopping-style cycling;
long half-lives are the reverse. Full reproducible output: `python3
contrib/asert_halflife_simulation.py` (uses `contrib/asert_reference.py`, the validated bit-exact
ASERT port from §A).

### B. AuxPoW chain ID 16969 -- checked now, real search, sources included

Searched actual chainparams source and the Bitcoin/BCH merged-mining wiki for every real assigned
AuxPoW chain ID I could find. Confirmed values, with sources:

| Chain | nAuxpowChainId |
|---|---|
| Namecoin | 1 (`0x0001`) |
| IXCoin | 3 (`0x0003`) mainnet; 1 on testnet/regtest (shared with Namecoin -- accepted there because strict chain-ID checking is normally relaxed on test networks, not something to copy for BitAIcoin's own mainnet-equivalent) |
| Bunkercoin | 73 (`0x0042`) |
| Myriadcoin | 90 (`0x005A`) |
| Dogecoin | 98 (`0x0062`) |
| Elastos (ELA, actively merge-mined with real Bitcoin today) | 1224 |

**16969 collides with none of these.** Phrased honestly, as instructed: this is **"no known
collision,"** not a guarantee of global uniqueness -- there is no single authoritative registry, and
smaller or now-dead merge-mined alts (I0Coin, Devcoin, and others) may have used values I couldn't
find a documented source for. `BitAIForkId` (0x424149, "BAI", a sighash replay-protection value) and
the AuxPoW chain ID (16969, "BI") remain **completely separate constants used for unrelated
purposes** -- confirmed unchanged from the original proposal.

Sources: [Merged mining specification](https://en.bitcoin.it/wiki/Merged_mining_specification),
[IXCoin chainparams.cpp](https://github.com/IXCore/IXCoin/blob/master/src/chainparams.cpp),
[Dogecoin chainparams.cpp](https://github.com/dogecoin/dogecoin/blob/master/src/chainparams.cpp),
[Myriadcoin chainparams.cpp](https://github.com/myriadcoin/myriadcoin/blob/master/src/chainparams.cpp),
[Bunkercoin chainparams.cpp](https://github.com/bunkercoin/bunkercoin/blob/master/src/chainparams.cpp),
[Elastos merged-mining guide](https://github.com/elastos/Elastos.ELA/wiki/Merged-mining-guide).

### C. Obsolete-node fork test -- protocol frozen now, run once implementation exists

**Preserved today, before any consensus code changes,** so the test later uses a byte-identical,
guaranteed-authentic pre-AuxPoW binary rather than a rebuild that could drift:

- Mac (arm64): `~/Downloads/bitaicoin-dev/pre-auxpow-binaries-mac/{bitaicoind,bitaicoin-cli}`,
  sha256 `f76a767b...` / `e901817e...`
- BITAISERVER3 (x86_64): `/opt/pre-auxpow-binaries/{bitaicoind,bitaicoin-cli}`,
  sha256 `f692667b...` / `e927276c...`
- Both built from `bitaicoin-phase1` @ `2734adbd25` (docs-only; no consensus code exists on top of
  this yet).

**Test protocol, to run once AuxPoW is implemented and before crossing 227808 for real:**
1. Stand up a 4th node instance running the preserved old binary, on its own datadir, `addnode`'d to
   the three upgraded nodes, synced to the same pre-activation tip.
2. Mine/advance the upgraded three-node network across height 227808 using real AuxPoW-validated
   blocks.
3. Record explicitly: the old node's final tip height and hash (expected: stalls at 227807 and never
   advances, since it should either fail to deserialize the new block format's AuxPoW-flagged
   version bit or reject a header it can't validate -- the *exact* failure mode, not just "it
   stalls," is the point of running this for real rather than assuming); any error/log output it
   produces; whether it crashes versus cleanly rejects.
4. Confirm all three upgraded nodes converge on the same tip hash as each other after the boundary.
5. Document old-node-versus-upgraded-node behavior explicitly in the milestone doc, with the actual
   recorded heights/hashes/log excerpts -- this is a deliberate hard fork, proven while the network
   is still private, exactly as instructed.

### D. Post-activation stabilization checkpoint -- placeholder only, no hash yet

Not chosen now, deliberately. Criteria to be met before a specific block is picked and recorded as
the checkpoint/baseline: the chain must have (1) crossed 227808 under real AuxPoW validation, (2)
survived a restart of all three nodes from that state, (3) survived at least one real reorg crossing
the activation boundary, and (4) run under sustained, substantial SHA256d hashpower for long enough
to demonstrate the DAA settling behavior in practice, not just in simulation. Once all four hold,
the specific height/hash/chainwork of a block chosen from that proven run gets recorded here.

### Confirmed unchanged
Existing blocks 225430-225823 remain untouched -- nothing above alters how any existing block
validates. Proceeding to the AuxPoW serialization/validation core next.

## 5. Amendments after the versionbits/CBlockIndex-storage report (2026-09-23)

Both audited decisions (Option A for versionbits; on-demand disk read for `CBlockIndex`, not
resident/persisted) were confirmed correct by the report. Five amendments were required before
proceeding to the live `CheckAuxPowRules()` splice; all five are done and green.

### 5.1 Option A formalized precisely (not a "reduced BIP9 namespace")

- Corrected framing: an AuxPoW-flagged BitAIcoin header is **not a BIP9-signaling block, by design**
  -- not "BIP9 with fewer bits." `0x4249 << 16 = 0x42490000`, top 3 bits `0b010` (BIP9 requires
  `0b001`): no *live* collision for this chain ID, but BIP9 is retired for the AuxPoW era regardless,
  since both of `CBitAIcoinParams`'s own deployments (`TESTDUMMY`, `TAPROOT`) are already permanently
  `NEVER_ACTIVE`/`FAILED` -- confirmed by a real test iterating the actual `ChainType::BITAICOIN`
  params (`bitaicoin_versionbits_deployments_are_permanently_inactive`,
  `src/test/auxpow_tests.cpp`), not assumed.
- **`TESTDUMMY`'s bit reverted from 15 back to stock 28** in all 6 chain-type definitions
  (`chainparams.cpp`): moving it was real, unnecessary divergence from upstream for a placeholder
  that's either `NEVER_ACTIVE` (5 of 6 chain types) or, on `REGTEST` only (where `TESTDUMMY` is
  intentionally left live for BIP9 test machinery, matching stock Bitcoin Core), already safely
  contained by the encoding-level defenses below -- not by bit placement. Test
  `auxpow_reserved_bit_leak_is_caught_as_wrong_chain_id_not_misinterpreted` demonstrates this
  directly: a stray bit landing in the chain-ID field just changes the encoded chain ID, which the
  existing wrong-chain-id check catches -- it was never silently misinterpreted.
- **Bits 9-15 (previously inert/unconstrained) are now required to be zero** on any AuxPoW-flagged
  header, enforced in both `CheckBitAIProofOfWork()` and `CheckAuxPowRules()`
  (`auxpow-reserved-bits-set`). Real classic AuxPoW tooling (Namecoin/Dogecoin/Syscoin-style) only
  ever produces small base versions well under 256, so this costs zero real compatibility while
  closing a real malleability gap.
- **Root-cause fix, not a workaround:** `MakeAuxpowVersion()` (`primitives/block.h`) now masks its
  `nBaseVersion` parameter to bits 0-7 unconditionally before combining. This is the actual fix for
  the chain-ID-corruption bug the transport test found earlier (a whole BIP9-signaling-shaped
  template version passed as "base version" used to corrupt the encoded chain ID via OR) -- fixed at
  the construction site, not just patched around in the one test that hit it.
  (`make_auxpow_version_masks_a_dirty_base_version`.)

### 5.2 On-demand `CBlockIndex` storage: kept as-is, confirmed by the numbers

No design change. Restated with the precise, sizeof()-grounded figures from the report: realistic
proof ~583 bytes serialized, ~0.7-1.1 KB/block resident with real allocator/object overhead if made
resident (~1 GB at 1M AuxPoW blocks), vs. ~600 MB/1M blocks if persisted in `CDiskBlockIndex`, vs.
**zero** additional bytes either way for the on-demand design actually implemented.

### 5.3 Pruning: NOT identical to stock, precisely documented, and a real startup bug fixed

Corrected claim: a stock pruned node still keeps every 80-byte header resident in `CBlockIndex`
forever. A BitAIcoin pruned node additionally loses the **AuxPoW proof** once its blk file is
pruned -- the lean `CBlockIndex` metadata (`IsAuxpowVersion()` etc.) is, like stock headers, never
pruned, but the proof genuinely is gone, by design.

**Real, node-level functional test added:** `test/functional/feature_auxpow_prune.py` (registered in
`test_runner.py`'s `BASE_SCRIPTS`), using a new `test/functional/test_framework/auxpow.py` (a
byte-for-byte Python port of the wire format, mirroring `src/auxpow.h` the way the C++ side itself
mirrors Namecoin/Syscoin -- verified byte-identical against the real C++ deserializer via a
standalone probe before being trusted). The test: mines a real chain, submits one real AuxPoW block
over real P2P, restarts (proof still on-demand-readable), mines past `MIN_BLOCKS_TO_KEEP` plus
enough margin to clear `-fastprune`'s own file-rollover boundary, prunes for real, restarts again,
and sends a real `getheaders` request spanning the now-pruned block.

**This test found four real, previously-undiscovered bugs**, all now fixed and covered:

1. **`getblock` RPC (verbosity>=1)** deserialized the block body with the plain, non-AuxPoW-aware
   `TX_WITH_WITNESS(block)` formatter instead of `AuxPowBlockWithWitness(block)` -- for any real
   AuxPoW block this misread the auxpow payload's own bytes as the start of `vtx`, corrupting the
   parsed block silently (not always throwing) and crashing on
   `coinbaseTxToJSON`'s `!coinbase_tx.vin.empty()` check. Fixed in `src/rpc/blockchain.cpp`.
2. **`DecodeHexBlk()`** (`src/core_io.cpp`, used by `submitblock` and `getblocktemplate`'s proposal
   mode) had the identical bug -- fixed the same way. Real, positive side effect: RPC-submitted
   AuxPoW blocks now decode correctly for the first time, though `submitauxblock`-style mining RPCs
   remain a separate, later slice.
3. **The REST API's JSON block-serving path** (`src/rest.cpp`) had the identical bug -- fixed.
4. **`LoadBlockIndexGuts()` could never restart a pruned node again once it had pruned past its
   first AuxPoW block** -- reproduced for real (`Error loading block database. Please restart with
   -reindex...`), not theoretical. The PoW-recheck-at-startup logic treated "proof unreadable" as
   fatal unconditionally; fixed to distinguish "pruned, and legitimately unavailable"
   (`!(nStatus & BLOCK_HAVE_DATA)`, the exact flag `PruneOneBlockFile` clears) from "data expected
   but unreadable" (genuine corruption, still fails loudly) -- trusting the block's already-recorded
   `nStatus` for the former rather than demanding data pruning deliberately deleted. This was the
   "trust nStatus for ordinary restarts" future-optimization noted-but-deferred in an earlier pass;
   it turned out not to be optional -- a pruned node could not function at all without it.

All four were caught by running a real node through real restarts and real pruning under the actual
built binary -- none were reachable by the existing in-process `ChainstateManager` unit tests, which
is exactly why this functional test was required before proceeding.

### 5.4 Combined activation-boundary tests: AuxPoW (227808) vs. buried BIP34 (227931)

Two exact-height tests added directly against `CheckAuxPowRules()`
(`check_auxpow_rules_boundary_matches_real_activation_height_exactly` at 227807/227808/227809;
`check_auxpow_rules_unaffected_by_nearby_bip34_boundary` at 227930/227931/227932), calling the
function directly with the real literal heights rather than mining a real chain to ~227931 blocks
(not a reasonable unit-test cost). Separately, `auxpow_block_coinbase_still_enforces_bip34_height`
(`auxpow_transport_tests` suite) proves the structural fact through the real `ChainstateManager`
pipeline: a real AuxPoW-flagged block with a deliberately-wrong coinbase height is rejected for
`bad-cb-height` (BIP34's own reason, captured via the real `BlockChecked` validation-interface hook,
the same mechanism `src/test/util/mining.cpp`'s own `ProcessBlock()` helper uses), not any
`auxpow-*` reason -- proving AuxPoW's proof substitution never bypasses ordinary block-content
validation. Together these show the two independently-height-gated mechanisms do not interact.

### Verified for real
Full clean rebuild; full unit test suite (788 cases, zero regressions, up from 782 -- 6 net new
after replacing the superseded bit-position test with the stronger NEVER_ACTIVE invariant plus 7
new tests); the new `feature_auxpow_prune.py` functional test green under the real `bitaicoind`
binary via both direct invocation and `test_runner.py`. No half-life frozen. Existing history
(225430-225823) confirmed untouched throughout.

`CheckAuxPowRules()` was then spliced into `ContextualCheckBlockHeader()` as its own commit, per
explicit authorization once the above amendments and tests were green. That splice itself then
surfaced the cross-chain isolation gap fixed in sec.6 below, BEFORE proceeding to ASERT.

## 6. Chain-awareness corrective pass (2026-09-23, before ASERT)

**Real architectural gap found in review, not by a failing test:** every AuxPoW-aware code path
(`CheckBitAIProofOfWork`'s dispatch, the auxpow-aware (de)serialization functions, net_processing's
relay, node/blockstorage's disk I/O, the RPC/REST/core_io fixes from sec.5) inferred "this header
carries a CAuxPow payload" directly from `nVersion` bit 8 (`IsAuxpowVersion()`), with **no chain-type
check at all**. Correct on the real BitAIcoin chain (and REGTEST, used deliberately to test it) --
**wrong** on ordinary MAIN/TESTNET/TESTNET4/SIGNET-style chains, where bit 8 is just an ordinary
version bit with its own historical meaning. `BitAIAuxpowActivationHeight == INT_MAX` means
"permanently pre-activation" (bit 8 still meaningful, just not yet allowed), **not** "AuxPoW does not
exist here" -- a header on a disabled chain with bit 8 set for unrelated reasons would have been
wrongly rejected as `auxpow-before-activation`, or worse, had its serialization corrupted.

### Fix

- **New `Consensus::Params::fBitAIAuxpowEnabled`** (bool, default `false`) -- the explicit, separate
  enable flag the sentinel-height design was missing. `true` only for `ChainType::BITAICOIN` and
  `REGTEST` (the latter deliberately, to keep testing AuxPoW there); `false` (the default) for
  MAIN/TESTNET/TESTNET4/SIGNET and anything else.
- **`CheckBitAIProofOfWork()`** now checks `!params.fBitAIAuxpowEnabled` FIRST, before ever looking at
  `IsAuxpowVersion()`: on a disabled chain it is unconditionally the plain `CheckProofOfWork()`,
  regardless of bit 8 or any attached proof.
- **`ContextualCheckBlockHeader()`** now gates the entire `CheckAuxPowRules()` call behind
  `consensusParams.fBitAIAuxpowEnabled`, not merely relying on the activation height -- on a disabled
  chain, AuxPoW policy is skipped entirely rather than evaluated-and-passing.
- **Every (de)serialization function require `auxpowEnabled` as an explicit, non-defaulted
  parameter**: `SerializeBlockHeaderWithAuxPow`/`UnserializeBlockHeaderWithAuxPow`,
  `SerializeBlockWithAuxPow`/`UnserializeBlockWithAuxPow`. On a disabled chain these are
  byte-for-byte identical to plain `CBlockHeader`/`CBlock` serialization, unconditionally, even with
  bit 8 set and a real proof attached.
- **Removed the `Using<>()`-based `AuxPowBlockWithWitness()`/`AuxPowBlockNoWitness()`/
  `AuxPowHeaderFormatter`**: `Using<>()` dispatches on a static type and cannot carry a runtime
  chain-awareness flag. Replaced with plain wrapper objects (`AuxPowBlockForSend`/`AuxPowBlockForRecv`,
  matching the pre-existing `AuxPowHeadersForAnnounce` pattern) that take `auxpowEnabled` explicitly at
  every real call site (blockstorage.cpp's `WriteBlock`/`ReadBlock`, net_processing.cpp's BLOCK/HEADERS
  send and receive, rpc/blockchain.cpp's `getblock`, core_io.cpp's `DecodeHexBlk` (now itself taking
  `auxpowEnabled`, threaded from its two real callers), rest.cpp's JSON path).
- **Compact blocks (BIP152) reverted to plain, always-stock header serialization**
  (`CBlockHeaderAndShortTxIDs::SERIALIZE_METHODS`, blockencodings.h): that type's generic
  `SERIALIZE_METHODS` has no chain context to check, so it can never safely decide "bit 8 means a
  proof follows." AuxPoW-flagged blocks are now deliberately never relayed via compact blocks on an
  AuxPoW-enabled chain (two real send-site gates added in net_processing.cpp), falling back to full,
  chain-aware BLOCK/HEADERS relay instead -- a disclosed simplification (compact-block bandwidth
  savings don't apply to AuxPoW blocks), not a gap.
- **`LoadBlockIndexGuts()` and `GetHeaderForAnnounce()`** now gate on `fBitAIAuxpowEnabled` explicitly
  before ever calling into the on-demand proof-read path, not just `IsAuxpowVersion()` -- otherwise a
  disabled chain's stray bit-8-flagged entry would have been misdiagnosed as pruned/corrupted data for
  a proof that was never expected to exist there.

### Regression tests added (`src/test/auxpow_tests.cpp`)

- `auxpow_enabled_flag_matches_the_desired_per_chain_state` -- direct assertion of the real per-chain
  `fBitAIAuxpowEnabled` value against every chain type.
- `disabled_chain_bit8_header_uses_ordinary_pow_check_not_auxpow_dispatch` -- proves dispatch
  equivalence between `CheckBitAIProofOfWork` and plain `CheckProofOfWork` on MAIN for a bit-8-set
  header with no proof attached.
- `disabled_chain_serialization_stays_historical_no_auxpow_bytes_ever` -- a header with bit 8 set AND
  a real, valid, attached proof still serializes to exactly 80 bytes when `auxpowEnabled=false`, and
  deserializing leaves trailing wire bytes completely unconsumed (proving no attempt to read a
  payload).
- `ordinary_chains_are_byte_and_decision_compatible_with_upstream_including_bit8` -- for every one of
  MAIN/TESTNET/TESTNET4/SIGNET, a representative header (including one with bit 8 set) serializes
  byte-for-byte and validates decision-for-decision identically via the chain-aware path vs. the plain
  stock path.
- (B)/(C) -- AuxPoW-enabled pre-/post-activation behavior was already covered exhaustively elsewhere
  in this file; not duplicated here.

### Verified for real

Full clean rebuild (bitaicoind + test_bitcoin); full unit test suite (792 cases, zero regressions, up
from 788); `feature_auxpow_prune.py` green end to end with the chain-aware code active throughout;
**a real node started on `-testnet4` (an AuxPoW-disabled chain) and confirmed to load its genesis
index and answer RPCs normally** -- the first real, live exercise of the disabled-chain path in an
actual running node, not just a unit test. Existing history (225430-225823) confirmed untouched.

## 7. Residual compact-block isolation gap + full IsAuxpow()/IsAuxpowVersion() audit (2026-09-23)

**One residual gap from sec.6**: the two real BIP152 compact-block eligibility gates in
net_processing.cpp still branched on a bare `pblock->IsAuxpow()` / `IsAuxpowVersion(pBestIndex->
nVersion)` -- pure bit-8 tests, no chain-awareness check. On a disabled chain, an ordinary bit-8-set
block would have unnecessarily lost its normal compact-block eligibility (a real, if minor,
correctness bug -- not a crash/corruption risk like sec.6's gaps, but still a bare bit-8 branch this
whole pass exists to eliminate).

**Fix**: extracted a single shared `IsRealAuxpow(auxpowEnabled, nVersion)` helper (`src/auxpow.h`) --
`auxpowEnabled && IsAuxpowVersion(nVersion)` -- and both compact-block gates now use it instead of
writing the expression out by hand.

**Full audit of every remaining `IsAuxpow()`/`IsAuxpowVersion()` use**, classified:
1. **Pure encoding/helper use, no chain context needed**: `CAuxPow::Check()`'s own internal check that
   the PARENT chain's header doesn't itself have bit 8 set (`auxpow-parent-is-auxpow`) -- the parent is
   a foreign chain's header (e.g. real Bitcoin) with no `fBitAIAuxpowEnabled` concept of its own; this
   is a pure "reject merge-mining of merge-mining" structural sanity check, matching Namecoin/
   Dogecoin's own real-world reference behavior. Documented explicitly at `CAuxPow::Check()`'s
   declaration.
2. **AuxPoW-enabled consensus code, precondition established by the caller**: `CheckAuxPowRules()`'s
   internal `isAuxpow` check (its one real call site, `ContextualCheckBlockHeader()`, already gates on
   `fBitAIAuxpowEnabled`) and `CheckBitAIProofOfWork()`'s internal `header.IsAuxpow()` check (gated by
   its own `!params.fBitAIAuxpowEnabled` early-return earlier in the same function). Both now carry an
   explicit PRECONDITION doc comment at their declaration.
3. **Behavioral branches requiring explicit chain awareness**: `GetHeaderForAnnounce()`,
   `LoadBlockIndexGuts()`'s PoW-recheck, the four (de)serialization functions, and the two compact-block
   gates -- all already fixed (sec.6 and this section).

New regression test: `auxpow_enabled_chain_governs_real_auxpow_classification` -- proves
`IsRealAuxpow()` classifies a bit-8-set header IDENTICALLY to an otherwise-identical bit-8-clear
header when disabled (both "not real AuxPoW", both equally compact-block-eligible), and DIFFERENTLY
when enabled (proving the disabled-chain case isn't merely a vacuously-always-false helper).

**Verified for real**: full clean rebuild; full unit test suite (793 cases, zero regressions, up from
792); `feature_auxpow_prune.py` green end to end.

**Next: ASERT dispatch**, as its own separate consensus slice, now that this corrective pass is fully
green. `createauxblock`/`submitauxblock` remain separate, later commits.

## 8. ASERT DAA: final half-life freeze + wiring (2026-09-23)

### 8.1 Final half-life comparison, rerun from the committed script

`contrib/asert_halflife_simulation.py` rerun one final time, unmodified, before writing any wiring
code, per explicit instruction. Full output archived; key findings:

- **Surge-then-revert (10x/100x/1000x, held 20/100/500 blocks)**: for MODERATE, short-duration
  scenarios (10x/20-held), longer half-lives (1d/2d) show artificially fast "recovery" (~3.5h) simply
  because a 20-block surge at 10x speed passes too quickly (real time) for a day-scale half-life to
  react at all -- not a genuine advantage, an artifact of barely responding. For SUSTAINED surges
  (100-500 held blocks), the real discriminator emerges: 6h recovers in 37-62h vs 1d/2d's 63-212h for
  the same 100x/1000x scenarios -- 1d/2d are consistently 2-4x slower to recover from a real, sustained
  hashrate change, confirming the standing "long half-life can't compensate for a sustained change"
  finding.
- **The most extreme sustained cases (1000x held 500 blocks) show 100-200+ hour recovery across
  EVERY half-life tested (1h through 2d)**: investigated, not just noted -- this is an unavoidable
  cold-start artifact (the first block mined immediately after a 1000x hashrate collapse takes ~1000x
  the target spacing on average, REGARDLESS of half-life, since no real time has yet elapsed for any
  half-life to have decayed the still-elevated difficulty). Not a discriminator between half-life
  choices.
- **MTP-floor pinning under the most extreme sustained surge (10000x held 500 blocks)**: 1h = 5/500
  blocks clamped, 6h = 20/500, 1d = 75/500, 2d = 85/500 -- 6h stays much closer to 1h's behavior than
  to 1d/2d's, meaningfully better than the two longer options.
- **Pool-hopping (on/off cycles at 10x/100x/1000x)**: no half-life avoids being gamed by a
  sufficiently patient attacker; 6h's resettle behavior is unremarkable relative to the others (no
  half-life is a clear winner or loser here).
- **Stochastic (Poisson-arrival) runs** confirm the deterministic model's qualitative shape across all
  four half-lives; no half-life shows materially better/worse variance than its deterministic
  counterpart would suggest.

**Conclusion**: nothing in this corrected, expanded simulation (surge multipliers, durations,
pool-hopping, Poisson arrivals, real MTP constraints) shows a material reason to deviate from the
6-hour compromise. It sits meaningfully better than 1d/2d on sustained-surge recovery and MTP-pinning,
without materially worse whipsaw behavior than 1h for the scenarios that matter. **Frozen: **

```
BitAIASERTHalfLife = 21600  // 6 hours, production consensus value
```

### 8.2 Wiring

- **`arith_uint256 ComputeASERTTarget(refTarget, targetSpacing, timeDiff, heightDiff, powLimit,
  halfLife)`** (`src/pow.h`/`.cpp`): the pure, standalone ASERT computation, a direct C++ port of the
  already-validated `contrib/asert_reference.py::calculate_asert()` -- same polynomial constants
  (195766423245049 / 971821376 / 5127), same RADIX (65536), same proven `FACTOR_MAX` (131071) and
  `powLimit >> 239 == 0` safety precondition (both asserted, not just commented), same `[1, powLimit]`
  clamp, same left-shift overflow guard. **Not a rewrite** of the validated arithmetic -- a direct,
  checked port. One real, C++-specific arithmetic-safety finding made during the port (not present in
  the Python reference, which has arbitrary-precision ints): the polynomial's worst-case intermediate
  sum (`frac = 65535`) is **18,446,563,080,438,344,768**, which overflows `int64_t` (max
  ~9.22e18) but fits `uint64_t` (max ~1.84e19, ~1.8e14 of headroom) -- computed exactly, not
  estimated, before choosing the type. The polynomial is computed in `uint64_t` throughout for this
  reason.
- **`GetNextWorkRequired()`** (`src/pow.cpp`): a new check, placed BEFORE the existing legacy-DAA
  "only retarget every 2016 blocks" branch (ASERT retargets every block, not just at interval
  boundaries), gated purely on `params.BitAIASERTActivationHeight` -- **never** on
  `fBitAIAuxpowEnabled` or any `nVersion` bit. Resolves the anchor via
  `pindexLast->GetAncestor(BitAIASERTActivationHeight - 1)` -- deterministic, by height, from the
  real active chain, never hardcoded (block 227807 does not exist yet). Anchor target from the
  anchor's own `nBits`; time reference from the anchor's **parent's** timestamp (BCH convention,
  exactly). `heightDiff = pindexLast->nHeight - pindexAnchor->nHeight` (the `+1` is inside the
  formula itself, matching real BCH source convention -- the first ASERT block, at the activation
  height, has `pindexLast == pindexAnchor`, so `heightDiff == 0` and the formula's own `+1` correctly
  makes it "one block after the anchor").
- Every other chain leaves `BitAIASERTActivationHeight` at `INT_MAX`; ordinary MAIN/TESTNET/
  TESTNET4/SIGNET/REGTEST DAA behavior is completely unchanged (unreachable branch there).
- `createauxblock`/`submitauxblock` remain explicitly out of scope for this slice.

### 8.3 Tests added (`src/test/pow_tests.cpp`, direct `GetNextWorkRequired`/`ComputeASERTTarget` calls
against synthetic `CBlockIndex` chains -- mirroring this file's own existing `get_next_work`-style
pattern, not mining a real 227808-block chain)

- Boundary: 227806 (legacy), 227807 (legacy, the anchor itself), 227808 (first ASERT target), 227809
  (ASERT) -- exact heights, real BitAIcoin consensus params.
- Reorg: a pre-activation reorg at 227807 correctly re-resolves the (different) anchor from the new
  branch; a reorg crossing 227808 has each branch compute from its own valid 227807 ancestry.
- Clamps: `target == 1` floor, `powLimit` ceiling, both directly forced via extreme `timeDiff`/
  `heightDiff` inputs to `ComputeASERTTarget`.
- Extreme ahead/behind schedule inputs (large positive and negative `timeDiff`) confirmed to clamp
  correctly rather than overflow/misbehave.
- Compact `nBits` round-trip vectors (`SetCompact`/`GetCompact` consistency across the target range).
- **Proof-mechanism independence**: at the same height and same ancestry, a direct SHA256d candidate
  and an AuxPoW candidate receive **exactly** the same required `nBits` -- `GetNextWorkRequired()`
  never inspects `nVersion`'s AuxPoW bit at all, proven directly, not just by code inspection.

**Verified for real**: full clean rebuild; full unit test suite, zero regressions. Existing history
(225430-225823) confirmed untouched. `createauxblock`/`submitauxblock` NOT started in this slice.
