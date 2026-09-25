# BitAIcoin activation-day + BIP34 observation checklist (DESIGN ONLY)

Observation and recording procedure for the REAL production chain. Nothing here mines or advances the
chain; it defines exactly what to record when the real chain reaches the milestones.

---

## 1. Pre-activation snapshot — real height 227807

Record (from a fully-upgraded node):
- block hash at 227807
- `nBits`
- `nTime`
- chainwork
- active peers (`getpeerinfo` count + summary)
- upgraded binary version + SHA-256 on **every** production node (confirm all upgraded)

## 2. Activation block — real height 227808

Record:
- actual block hash at 227808
- proof mechanism: **direct** or **AuxPoW**
- actual `nBits`
- **expected ASERT `nBits`** computed from the real production ancestry (compare to actual)
- chainwork
- convergence: all upgraded nodes report the identical 227808 tip

Notes:
- **If the first activation block is direct SHA256d, that is valid.** Do not delay activation waiting
  for an AuxPoW block.
- AuxPoW can be observed later, when merge mining is actually introduced (which requires the GBT parent
  adapter — not part of this release).

## 3. BIP34 production observation — real height 227931

When the REAL production chain eventually reaches 227931:
- record the **real** 227931 block hash and chainwork (do **not** substitute the rehearsal's 227931
  hash — rehearsal data is disposable-lab data only).
- verify the real child coinbase satisfies the BIP34 height-in-coinbase rule.

## 4. Stabilization — criteria-based, separate later review (do NOT do now)

Do **not** create a checkpoint or set `nMinimumChainWork` now. Only after sufficient REAL
post-activation / post-BIP34 history exists, gather in a separate review:
- real production height
- real block hash
- real chainwork
- peer-convergence evidence
- restart/reindex evidence on real data
- actual production AuxPoW evidence, if/when merge mining is operating

Only then consider a real checkpoint / minimum-chainwork candidate — in its own review, not here.
