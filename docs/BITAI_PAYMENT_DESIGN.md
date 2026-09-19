# BitAI Payment — Design (Not Implemented)

Status: **Design only. No production code exists yet**, per explicit instruction.
This document is the deliverable `docs/AGENT_PAYMENTS.md` called for: "a
dedicated design/plan phase... informed by whatever real usage patterns or
requirements emerge from actually running the Phase 1 Synthetic Lab network."

Grounded directly against the existing `agent-marketplace` codebase
(`~/Desktop/Projects/agent-marketplace`, reviewed file-by-file for this
design — `packages/core/src/payments/payment-adapter.ts`,
`packages/core/src/payments/adapter-conformance.ts`,
`packages/contracts/src/entities/payments.ts`,
`apps/api/src/modules/payments/base-usdc-adapter.ts`,
`packages/db/src/schema/tables.ts`, and the payments/architecture docs), not
from generic knowledge of what a payment adapter "should" look like. Every
interface reproduced below is copied from that code, not paraphrased.

## Target architecture (as specified)

```
Agent Marketplace
  -> BitaiPaymentAdapter        (implements the marketplace's PaymentAdapter, in the marketplace repo)
  -> BitAI Payment              (a new, separate service — this document's main subject)
  -> BitAIcoin Core / RPC       (this repo — narrow settlement layer, no new consensus features)
```

BitAIcoin Core's role in this design is **read/write RPC client only**:
`getnewaddress`, `listtransactions`, `gettransaction`, `listunspent`,
`sendtoaddress`/`send`, `getblockcount`, `getblockhash`. All already exist,
unmodified, as stock Bitcoin Core RPCs. Nothing in this design requires a new
opcode, a new RPC, or a new consensus rule.

---

## 1. The exact existing PaymentAdapter interface and conformance requirements

Reproduced from `packages/core/src/payments/payment-adapter.ts` (ADR-0098),
because this is the fixed boundary — nothing in this design is free to
deviate from it:

```ts
export interface PaymentAdapter {
  readonly railId: string;
  capabilities(): AdapterCapabilities;
  submitTransfer(request: TransferRequest): Promise<RailTransferStatus>;
  getTransfer(externalReference: string): Promise<RailTransferStatus>;
  reconcileByIdempotencyKey(idempotencyKey: string): Promise<RailTransferStatus | null>;
  finalityOf(status: RailTransferStatus): RailFinality;
  verifyCallback?(payload: string, signature: string): boolean;
}
```

`reconcileByIdempotencyKey` is **mandatory** — the marketplace's whole
crash-recovery story depends on every adapter answering "what does the rail
say happened to the key I generated before I called you?"

Supporting types (from `packages/contracts/src/entities/payments.ts`):

- `TransferRequest { idempotencyKey, purpose, asset, amount: string, instrument: { reference, metadata? }, memo? }`
- `PaymentAdapterError` — carries an `errorClass` of `TRANSIENT_ERROR | PERMANENT_ERROR | UNKNOWN_OUTCOME | REJECTED | UNSUPPORTED`, with `.mayResend` / `.mustReconcile` derived getters.
- `AdapterCapabilities` — a fully-declared struct (`initiatesTransfers`, `supportsDeposits`, `supportsWithdrawals`, `supportsRefunds`, `supportsMicropayments`, `supportsMemo`, `supportsNativeIdempotency`, `supportsQueryByIdempotencyKey`, `supportsFinality`, `supportsCallbacks`, `supportedAssets`, `supportedNetworks`, `handlesRealValue`). Marketplace code never branches on `railId`.
- `RailFinality = "NONE" | "PROVISIONAL" | "FINAL" | "REVERSED"`. Only `FINAL` may credit a ledger (`mayCreditLedger()`, ADR-0101).
- `RailTransferStatus { externalReference, state, finality, observedAmount, failureClass, failureReason, observedAt, detail, observation? }`.
- `RailObservation { externalReference, finality, observedAt, provider: { name, agreement }, provenance: Record<string, string|number|null> }` — durable, chain-specific evidence (ADR-0127). `provenance` is opaque to marketplace code; this is where every BitAIcoin-specific fact (txid, vout, block height, block hash) lives.
- `ExternalAsset` enum currently: `MOCK_USD | USDC | USDC_SEPOLIA | BTC | HBAR | FIAT_USD`. **`BAIC` and `BAIC_TEST` do not exist yet and must be added** (see §11).
- `SettlementCurrency` enum currently: `USD | USDC_TEST`. **`BAIC_TEST` must be added**, mirroring exactly how `USDC_TEST` is kept structurally isolated from `USD` (ADR-0113) — a testnet/lab-chain BAIC balance must never be reachable from a USD-denominated mandate, quote, or contract.

### The conformance suite (`packages/core/src/payments/adapter-conformance.ts`)

Every assertion `BitaiPaymentAdapter` must pass, verbatim from the file:

1. Declares capabilities; `supportedAssets`/`supportedNetworks` non-empty; `handlesRealValue` is a boolean.
2. `supportsQueryByIdempotencyKey` is `true` (this is asserted directly — it is not optional for any adapter).
3. Declares `initiatesTransfers` honestly; if `false`, `submitTransfer` must throw `UNSUPPORTED`. **BitaiPaymentAdapter must declare `true`** (unlike `BaseUsdcAdapter`) because it needs to originate withdrawals — see §2.
4. Submitting the same `idempotencyKey` two or three times returns the *same* `externalReference` every time.
5. `reconcileByIdempotencyKey` on a never-seen key returns `null` — not an error.
6. `submitTransfer`'s result has a `state` in the enum and a `finality` in the enum, and `observedAt` is set.
7. The reported `observedAmount.{value, asset}` matches what was actually moved.
8. Once an `externalReference` exists, `getTransfer` by that reference returns it.
9. The rail eventually reaches `FINAL` (via the harness's `settle()` hook if asynchronous), and never claims `FINAL` early.
10. A lost response is surfaced as `UNKNOWN_OUTCOME` (`mustReconcile: true`, `mayResend: false`), and `reconcileByIdempotencyKey` afterward finds what actually happened.
11. A terminal transfer stays terminal across repeated queries (same `state`, same `externalReference`).
12. Distinct keys produce distinct transfers.

This suite is framework-agnostic and reusable — `BitaiPaymentAdapter`'s own
test file runs it exactly the way `base-usdc-adapter.test.ts` does, with a
`ConformanceHarness` whose `settle()` hook mines a lab-chain block (or waits
for the configured confirmation depth) and whose `loseResponseFor()` hook
simulates a dropped HTTP response between the marketplace and BitAI Payment.

---

## 2. Proposed `BitaiPaymentAdapter` — mapping marketplace operations to BitAI Payment

`BitaiPaymentAdapter` lives in the marketplace repo
(`apps/api/src/modules/payments/bitai-payment-adapter.ts`) and is a **thin
HTTP client**, structurally the same role `BaseUsdcAdapter` plays over an
`EvmRpcClient` — except the thing it talks to is BitAI Payment's own API
(§3), not a chain RPC directly. BitAI Payment is what talks to BitAIcoin
Core.

| PaymentAdapter method | Maps to |
|---|---|
| `railId` | `"bitaicoin-lab"` while on the Phase 1 Synthetic Lab chain; `"bitaicoin"` once a production chain exists. Distinct strings on purpose, mirroring how `BASE_SEPOLIA_RAIL_ID` is distinct from a hypothetical mainnet one — a deployment must not be able to point a lab-rail-configured marketplace at a real-value rail by accident. |
| `capabilities()` | See table below. |
| `submitTransfer(request)` — `purpose: "WITHDRAWAL"` | `POST /v1/withdrawals` on BitAI Payment, passing the marketplace's `idempotencyKey` through as BitAI Payment's own idempotency key (never regenerated — this is the one identifier that must survive the whole chain). |
| `submitTransfer(request)` — `purpose: "DEPOSIT"` | **Refuses with `UNSUPPORTED`.** A deposit is not "submitted" — see the deposit-intent flow in §7. `submitTransfer` is only ever called by the marketplace for outbound value (withdrawals), matching how `TransferRequest.purpose` is used elsewhere in the marketplace. |
| `getTransfer(externalReference)` | `GET /v1/transfers/{externalReference}` |
| `reconcileByIdempotencyKey(key)` | `GET /v1/transfers/by-idempotency-key/{key}` — the mandatory recovery path |
| `finalityOf(status)` | Pure pass-through of `status.finality`. BitAI Payment normalizes finality itself (§7) before ever returning a status to the adapter — the adapter does not compute anything. |
| `verifyCallback(payload, sig)` | Verifies BitAI Payment's webhook signature (Ed25519 or HMAC — §6 reuses the same Ed25519 key/verification the marketplace already uses for agent auth, ADR-0011, rather than introducing a second crypto primitive). Per ADR-0104, a verified callback still only triggers a re-query (`reconcileByIdempotencyKey`) — it never credits directly. |

### Capabilities declaration

```ts
capabilities(): AdapterCapabilities {
  return {
    initiatesTransfers: true,       // unlike BaseUsdcAdapter: BitAI Payment holds custody and can send
    supportsDeposits: true,
    supportsWithdrawals: true,
    supportsRefunds: true,          // pre-capture refunds are pure off-chain ledger moves inside BitAI Payment
    supportsMicropayments: true,    // the whole point of the internal-transfer/instant-payment layer
    supportsMemo: true,
    supportsNativeIdempotency: false, // BitAIcoin/Bitcoin Core RPC has none (see §7); BitAI Payment supplies it
    supportsQueryByIdempotencyKey: true, // mandatory, satisfied
    supportsFinality: true,
    supportsCallbacks: true,        // BitAI Payment is a service we operate and can push webhooks from
    supportedAssets: ["BAIC_TEST"], // ["BAIC"] once a production chain exists — never both from one adapter instance
    supportedNetworks: ["bitaicoin-lab"],
    handlesRealValue: false,        // true only once BitAI Payment points at a real-value BitAIcoin chain
  };
}
```

`handlesRealValue: false` on the lab chain is load-bearing the same way
`assertSepoliaOnly()` is load-bearing for `BaseUsdcAdapter` (ADR-0117):
**a config error must not be able to make this silently become `true`.**
§9 specifies the equivalent hard guard.

### The deposit-attribution question (mirrors Base's hardest problem)

`BaseUsdcAdapter`'s biggest design cost was attributing an arriving transfer
to the right intent (`DepositExpectation`, `SENDER_PROOF` vs
`UNIQUE_ADDRESS`, ADR-0123/ADR-0130). BitAIcoin inherits this exact problem
in UTXO form, and the same resolution applies directly: **use
`UNIQUE_ADDRESS` attribution, not `SENDER_PROOF`.**

- BitAI Payment allocates one freshly-derived BitAIcoin address per deposit
  intent (`getnewaddress` or an internally-managed HD descriptor — §7).
- Nothing arrives at that address except the deposit it was created for, so
  attribution needs no sender-control proof at all — simpler than Base's
  worst case, not harder, because UTXO outputs don't share an account the
  way EVM balances do.
- Overpayment/underpayment policy mirrors ADR-0130 exactly: underpayment
  never confirms (funds sit visibly, not silently dropped); overpayment
  confirms the intent at its declared amount and the surplus is a distinct,
  separately-reconcilable UTXO at the same address (an operator-visible
  reconciliation finding, not silently swept in).

---

## 3. The BitAI Payment service boundary and API

A new, separate deployable — not part of this repo, not part of the
marketplace monorepo. Language/framework is the user's call; reusing the
marketplace's own stack (TypeScript, Fastify, PostgreSQL, Drizzle) is a
reasonable default only because it avoids introducing a second stack for
one team to operate, not because anything here requires it.

```
POST   /v1/deposit-intents              { idempotencyKey, expectedAmount, asset, ownerRef }
                                         -> { depositAddress, expiresAt, fromHeight }

GET    /v1/transfers/{externalReference}
GET    /v1/transfers/by-idempotency-key/{key}
                                         -> RailTransferStatus-shaped JSON (§1's exact shape)

POST   /v1/withdrawals                  { idempotencyKey, amount, asset, destinationAddress, memo? }
                                         -> RailTransferStatus | 202 UNKNOWN_OUTCOME-equivalent

POST   /v1/internal-transfers           { idempotencyKey, fromAccount, toAccount, amount, asset }
                                         -> instant, off-chain, no BitAIcoin transaction at all

POST   /v1/authorizations               { idempotencyKey, amount, asset, payerAccount, resourceRef }
                                         -> { authorizationId, holdExpiresAt }        [direct/non-marketplace L402 use — see §12's note]
POST   /v1/authorizations/{id}/capture  { amount? }   (defaults to full authorized amount)
POST   /v1/authorizations/{id}/void

GET    /v1/challenges/{resourceRef}     -> HTTP-402-shaped payment requirement (amount, asset, address/invoice, expiry)
                                            [BitAI Payment's own negotiation endpoint for direct L402 flows]

POST   /v1/receipts/verify              { receipt, signature } -> { valid: boolean, reason? }
                                            [convenience only — primary verification is offline, §6]

GET    /v1/accounts/{accountId}/balance -> { available, pending, currency }

GET    /v1/rail-status                  -> chain reachability, head height, confirmation depth, handlesRealValue
POST   /v1/reconcile                    -> operator-triggered reconciliation sweep (read-only findings, §7)

Webhook (BitAI Payment -> subscriber):
POST   {subscriber_webhook_url}         RailCallbackEnvelope-shaped body (railId, externalReference,
                                          claimedState, signature, emittedAt, nonce) — a hint, per ADR-0104
```

All amounts are decimal strings, matching `TransferRequest.amount` exactly —
no floats anywhere in this boundary, same reasoning as the existing contract
(a satoshi amount is an integer; a float would eventually lie about one).

---

## 4. The internal payment state machine

Three separate state machines, deliberately not fused — same reasoning as
the marketplace keeping `PaymentIntentState` separate from every internal
entity's own machine (external systems must not make an internal state
depend on their own availability).

**Deposit intent:**
```
CREATED -> AWAITING_PAYMENT -> DETECTED -> CONFIRMING -> CONFIRMED
                             \                        \-> REORGED -> CONFIRMING (re-watch) | EXPIRED
                              \-> EXPIRED (nothing arrived in time)
```
`DETECTED` = seen in a block or the mempool, 0+ confirmations, below the
configured depth. `UNDERPAID` is not a separate state — it is `CONFIRMING`
with an amount-mismatch note, per the Base precedent (§2); it can still
resolve to `CONFIRMED` if a top-up UTXO arrives.

**Withdrawal intent:**
```
CREATED -> RESERVED -> BROADCASTING -> BROADCAST -> CONFIRMING -> CONFIRMED
                                     \-> UNKNOWN (crash mid-broadcast; never resend, always reconcile by label — §7/§8)
        \-> FAILED (rejected before broadcast: bad address, insufficient funds) -> RELEASED
```
`RESERVED` mirrors the marketplace's own withdrawal reserve model
(`AGENT_AVAILABLE -> EXTERNAL_PENDING`, ADR-0106) one layer down, inside
BitAI Payment's own ledger: funds are neither spendable nor gone while the
outcome is unknown.

**Authorization hold** (direct/non-marketplace use only — §12's note):
```
CREATED -> HELD -> CAPTURED (partial or full)
                 \-> VOIDED
                 \-> EXPIRED
```

Normalization into the marketplace's own `PaymentIntentState` (`CREATED →
SUBMITTED → PENDING → CONFIRMED / FAILED / REJECTED / UNKNOWN`) happens at
the `BitaiPaymentAdapter` boundary, one direction only — BitAI Payment never
learns the marketplace's state names, matching how `BaseUsdcAdapter` returns
its own `RailTransferStatus.state` and the marketplace does the mapping.

---

## 5. The minimum ledger/data model

BitAI Payment owns its own PostgreSQL database, structurally independent of
the marketplace's. Adopting the marketplace's own proven ledger discipline
(double-entry, append-only postings, balance-sums-to-zero enforced by a
constraint, derived rather than stored balances) is **reuse of a pattern**,
not duplication of a system — this ledger tracks BAIC custodial balances,
a genuinely different thing from the marketplace's USD-denominated ledger.

```
accounts                  (id, owner_ref, currency [BAIC | BAIC_TEST], created_at)
ledger_transactions        (id, kind, idempotency_key UNIQUE, correlation_ref, created_at)
ledger_postings            (id, transaction_id, account_id, amount, created_at)  -- append-only; trigger enforces sum(amount) = 0 per transaction_id

deposit_intents            (id, idempotency_key UNIQUE, owner_ref, expected_amount, asset,
                             deposit_address UNIQUE, hd_index, from_height,
                             state, recorded_txid, recorded_vout, recorded_block_hash,
                             confirmations_last_seen, created_at, expires_at)

withdrawal_intents         (id, idempotency_key UNIQUE, owner_ref, amount, asset,
                             destination_address, state, broadcast_label,
                             broadcast_txid, recorded_block_hash, fee_paid,
                             created_at)

authorizations             (id, idempotency_key UNIQUE, payer_account_id, resource_ref,
                             amount_held, amount_captured, state, expires_at)

receipts                   (id, ledger_transaction_id, kind, payload_json, signature,
                             signing_key_id, issued_at)

wallet_addresses           (address UNIQUE, hd_index, purpose, allocated_to_deposit_intent_id, created_at)

webhook_deliveries         (id, subscriber_url, envelope_json, attempt_count, last_attempt_at, delivered_at)

reconciliation_findings    (id, code, severity, detail_json, found_at, resolved_at)  -- read-only findings, no repair function (mirrors ADR-0090)
```

Structural guarantees, mirroring the marketplace's own:

| Constraint | Prevents |
|---|---|
| `UNIQUE(idempotency_key)` per intent table | Two intents for one intention |
| `UNIQUE(deposit_address)` | Two intents sharing one destination |
| `UNIQUE(recorded_txid, recorded_vout)` | Two intents claiming one UTXO |
| Balanced-transaction trigger | A posting set that doesn't sum to zero |
| One `ledger_transaction_id` per settled intent | Two credits for one confirmation |

---

## 6. The signed receipt format and verification procedure

A receipt is a canonical, deterministically-serialized JSON object signed
with an **Ed25519** key — reusing the exact primitive the marketplace
already uses for agent identity and challenge/response auth (ADR-0011),
rather than introducing a second cryptographic stack for one feature.

```ts
interface BitaiPaymentReceipt {
  receiptId: string;
  schemaVersion: 1;
  kind: "DEPOSIT_SETTLED" | "WITHDRAWAL_SETTLED" | "INTERNAL_TRANSFER_SETTLED" | "AUTHORIZATION_CAPTURED";
  payerRef: string;
  payeeRef: string;
  amount: string;          // decimal string, never a float
  asset: "BAIC" | "BAIC_TEST";
  ledgerTransactionId: string;
  externalReference: string | null;   // "bitaicoin:<txid>:<vout>" for on-chain settlements; null for pure internal transfers
  chainAnchor: { blockHeight: number; blockHash: string } | null; // present only when externalReference is
  settledAt: string;       // ISO-8601, BitAIcoin's own clock (block time) when chainAnchor is present
  issuedAt: string;        // ISO-8601, BitAI Payment's own clock
}
```

**Signing:** canonicalize (sorted keys, no whitespace — the same discipline
`packages/contracts`'s "canonical challenge payload" already uses) then sign
the UTF-8 bytes with Ed25519. The signature and the `signingKeyId` (to
support key rotation) travel alongside the payload, never inside it.

**Verification (the primary path is offline):**
1. Recompute the canonical serialization of the payload.
2. Verify the Ed25519 signature against BitAI Payment's published public
   key for `signingKeyId`.
3. Optionally, if `chainAnchor` is present, independently confirm
   `blockHash` is what BitAIcoin itself reports at `blockHeight` (a fully
   independent party can do this against their *own* `bitaicoin-cli`, with
   zero trust in BitAI Payment at all — this is the concrete
   machine-verifiability property the user's spec asked for).

`POST /v1/receipts/verify` exists only as a convenience for a caller that
doesn't want to implement step 1-2 itself; it is not the trust boundary.

---

## 7. Deposit, withdrawal, confirmation, and reorg behavior

### Deposits

1. `POST /v1/deposit-intents` allocates a fresh address (`getnewaddress` or
   an internally-tracked HD descriptor index) and records
   `from_height = getblockcount()` **before** anything else — this is the
   same binding-boundary technique as `DepositExpectation.fromBlock`
   (ADR-0122): a transfer at or before this height cannot be what this
   intent is waiting for, closing off a replay of an old UTXO.
2. A background poller calls `listtransactions`/`listunspent` for the
   watched address and cross-checks against `gettransaction` for the
   `confirmations` field BitAIcoin's own RPC already computes.
3. **Independent block-hash tracking is still required**, not just trusting
   `confirmations`: record the block hash at first observation
   (`recorded_block_hash`), and on every later poll confirm the transaction
   is still reported inside a block with that same hash. A reorg is
   detected the moment it disagrees — exactly `evaluateFinality`'s reorg
   check (§ design note below), not a new mechanism.
4. Below the configured confirmation depth: `CONFIRMING`. At or above it:
   `CONFIRMED`, and only then does BitAI Payment credit the owner's
   off-chain ledger balance (mirrors "only `FINAL` may credit," ADR-0101,
   one layer down).
5. **Reorg:** if a previously-recorded `(txid, vout, blockhash)` triple is
   no longer found in the canonical chain, do **not** immediately fail the
   intent. Bitcoin-model reorgs commonly re-mine the same mempool
   transaction within the next block or two; transition back to
   `CONFIRMING` and keep watching. Only after a configured timeout with no
   reappearance does the intent move to `FAILED`. This mirrors
   `BaseUsdcAdapter.reconcileByIdempotencyKey`'s exact handling of "we
   recorded a transfer that vanished."

### Withdrawals

Bitcoin Core RPC (and therefore BitAIcoin's, unmodified) has **no native
request-level idempotency**: calling `sendtoaddress` twice sends twice.
This is the one place BitAI Payment must build discipline the chain does
not provide — but it is exactly the discipline the marketplace's own
`PaymentAdapter` contract already demands, so there is no impedance
mismatch, only implementation care:

1. Write the `withdrawal_intents` row (state `RESERVED`, ledger posting
   `AGENT_AVAILABLE -> EXTERNAL_PENDING`-equivalent inside BitAI Payment's
   own accounts) and **commit it** before calling BitAIcoin at all.
2. Call `sendtoaddress`/`send` with a `comment`/label parameter set to the
   withdrawal's own idempotency key. The label is the recovery anchor.
3. On success, record the returned `txid` and move to `BROADCAST`.
4. **Crash between step 2 and step 3** (the RPC call succeeded but the
   process died before persisting the result): on restart, **never
   re-call `sendtoaddress`.** Instead, call `listtransactions` (or
   `listlabels` + `listtransactions <label>`) filtering for the label and
   recover the actual `txid` that way. This is the BitAIcoin-native
   equivalent of `reconcileByIdempotencyKey`'s promise.
5. Confirmation and reorg handling for a withdrawal's own transaction
   mirror the deposit path exactly (§ above), tracked from BitAI Payment's
   side rather than watched externally.

### A note on `evaluateFinality`

`packages/core/src/payments/evm-finality.ts` is **not directly reusable** —
its `receiptStatus: 0 | 1 | null` field encodes EVM transaction revert,
which has no BitAIcoin/Bitcoin-model equivalent (a transaction inside a
valid block is unconditionally valid; there is no "mined but reverted"
state). The right move is a small, new, analogous function
(`bitaicoin-finality.ts`, `BITAICOIN_FINALITY_POLICY_VERSION =
"bitaicoin-confirmation-depth-v1"`) that reuses the exact same *logic*
(confirmations = head − height + 1; reorg = recorded block hash ≠ current
block hash; below depth = `PROVISIONAL`; at or above = `FINAL`) minus the
revert check. Same pattern, new file — not a code fork, not a reinvention.

**Confirmation depth is an open parameter, deliberately not specified
here.** It depends on the still-open production security-model decision
(`PHASE1_REPORT.md`'s Known Limitation on `PRODUCTION_DIFFICULTY_NOT_FINAL`)
— a low-difficulty lab chain needs a *deeper* confirmation requirement to
reach the same reorg-resistance a high-difficulty chain gets from a
shallow one, not a shallower one, and picking a number now would be
guessing at a decision this document explicitly defers.

---

## 8. Idempotency and crash/recovery behavior

The governing rule, restated one layer down from the marketplace's own
(ADR-0100): **uncertainty causes reconciliation, never duplication.**

| Crash point | Recovery |
|---|---|
| After the DB row commits, before BitAIcoin is called at all | Safe — nothing was sent. Retry for real. |
| After BitAIcoin accepts a `sendtoaddress`, before the txid is persisted | **Never resend.** Recover via label lookup (§7). |
| Mid-reconciliation read | Reconciliation is just asking again — idempotent by construction, safe to retry or run concurrently. |
| Mid-webhook-delivery | The webhook is a hint (ADR-0104); a lost delivery is invisible to correctness, only to latency. A retry queue (`webhook_deliveries`) exists for operator visibility, not for correctness. |
| Internal transfer, response lost | Ordinary DB-transaction idempotency: insert-or-return-existing on `(idempotency_key)`, no chain interaction, no `UNKNOWN` state possible — matches the marketplace's own idempotency-lease pattern for internal writes (ADR-0093), because this operation never leaves BitAI Payment's own database. |

`UNKNOWN` in the withdrawal state machine is **not terminal** and is never
treated as failure, for exactly the reason the marketplace's own
`PaymentIntentState.UNKNOWN` exists: returning reserved funds on an unknown
outcome risks paying twice; treating them as gone robs the owner.
Reserved is the honest position until a label lookup resolves it.

---

## 9. Custody and trust assumptions

**Version 1 is fully custodial. Stated plainly, not softened.** BitAI
Payment holds every private key involved — every deposit address and the
withdrawal wallet — via BitAIcoin Core's own wallet. This is
mechanically **the same trust model as a centralized exchange**, and the
same one the marketplace's own Turnkey integration attempt reached for and
then explicitly shelved rather than ship half-verified (ADR-0143). This
design does not claim to be safer than that; it inherits the same
honesty about it.

"Semi-custodial" is not used to describe v1, deliberately: that term
implies some real cryptographic split (a 2-of-2 or 2-of-3 multisig where
BitAI Payment alone cannot move funds), which v1 does not have. If a
multisig deposit scheme is wanted, it's a genuine, separate design
decision — BitAIcoin already supports P2WSH multisig (SegWit is active
from the fork point, Phase 1 M5), so it's technically available, but it
requires the depositor to generate and safeguard their own key and cosign
withdrawals, which is real added complexity a "minimum viable" first
version should not carry by default.

**Threat model, stated as bullets:**
- A compromised BitAI Payment signing process/key can move any
  custodial fund it controls. Mitigation is operational (hot/cold wallet
  split, withdrawal velocity limits, monitoring) — the same playbook the
  marketplace's own threat-model doc already establishes, not a new one.
- A compromised BitAI Payment database can forge internal ledger state but
  **cannot forge a receipt** without also compromising the signing key —
  the receipt's trust anchor is deliberately narrower than the database's.
- A BitAIcoin-side compromise (a consensus bug, a 51%-class attack on the
  underlying chain) is outside BitAI Payment's threat model entirely and
  is BitAIcoin Core's own concern (see the still-open security-model
  question in `PHASE1_REPORT.md`).

This should never be marketed or documented anywhere as trustless. It
is not, and pretending otherwise is exactly the kind of overclaim this
project has avoided throughout (see `PRODUCTION_DIFFICULTY_NOT_FINAL`,
`docs/HISTORICAL_LINEAGE.md`'s refusal to overclaim what was recovered).

---

## 10. How a future bilateral-channel backend replaces the ledger backend

The move that makes this possible: **BitAI Payment's own HTTP API (§3)
already only exposes settlement *outcomes*** (a deposit confirmed, a
withdrawal confirmed, a balance, a receipt) — it never exposes "there is a
custodial ledger behind this" as part of its contract. That's the same
seam-design lesson the marketplace's own `PaymentAdapter` already
teaches, applied recursively one layer down.

Concretely: introduce a `SettlementEngine` interface *inside* BitAI
Payment, and make every HTTP handler in §3 call it rather than touching
the ledger tables directly:

```ts
interface SettlementEngine {
  recordDeposit(intent: DepositIntent, observed: ObservedUtxo): Promise<SettlementResult>;
  reserveForWithdrawal(intent: WithdrawalIntent): Promise<void>;
  settleWithdrawal(intent: WithdrawalIntent, txid: string): Promise<SettlementResult>;
  transferInternal(from: string, to: string, amount: string, idempotencyKey: string): Promise<SettlementResult>;
  capture(authorizationId: string, amount: string): Promise<SettlementResult>;
}
```

- **v1**: `CustodialLedgerEngine implements SettlementEngine` — exactly
  §5's double-entry ledger plus `sendtoaddress`.
- **v2 (future, not designed here)**: `ChannelSettlementEngine implements
  SettlementEngine` — `transferInternal` routes an off-chain payment
  through the payer's bilateral HTLC channel to the payee when one is
  open, falling back to the ledger otherwise; `settleWithdrawal`
  cooperatively closes or updates a channel instead of calling
  `sendtoaddress` directly.
- **Nothing above the `SettlementEngine` line changes.** The HTTP API in
  §3 is unchanged. `BitaiPaymentAdapter` in the marketplace repo is
  unchanged — it never learns which engine is behind BitAI Payment. The
  receipt format (§6) already accommodates this: `externalReference`
  becomes a channel-update or channel-close reference instead of a raw
  `txid:vout`, and `chainAnchor` is simply `null` until a channel closes
  on-chain — both already optional/nullable fields in the schema, not a
  breaking change.

This is exactly why §6 was designed with `externalReference` as an opaque
string and `chainAnchor` as nullable from the start, rather than assuming
every settlement has a UTXO behind it.

---

## 11. Exact modules/files likely to be added or changed

**Marketplace repo (`agent-marketplace`):**

| File | Change |
|---|---|
| `packages/contracts/src/entities/payments.ts` | Add `"BAIC"`, `"BAIC_TEST"` to `ExternalAsset`; add `"BAIC_TEST"` to `SettlementCurrency` (mirrors `USDC_TEST`'s isolation, ADR-0113) |
| `apps/api/src/modules/payments/bitai-payment-client.ts` | **New.** Thin typed HTTP client for BitAI Payment's own API — plays the role `EvmRpcClient` plays for Base |
| `apps/api/src/modules/payments/bitai-payment-adapter.ts` | **New.** The `BitaiPaymentAdapter` class (§2) |
| `apps/api/test/bitai-payment-adapter.test.ts` | **New.** Runs `runAdapterConformance` against it, mirroring `base-usdc-adapter.test.ts` |
| `docs/bitai-payment-adapter.md` | **New**, in the marketplace repo. Mirrors `base-sepolia-usdc.md`'s documentation role |
| `docs/decisions/01XX-bitaicoin-utxo-first-rail.md` | **New ADR**, recording the UTXO-vs-account-model distinction (unique-address attribution, no sender-proof needed) as a first-class fact, the way ADR-0108/0122/0123/0130 did for Base |

**BitAI Payment (new, separate service — not designed to exist in either
existing repo):**

- HTTP API layer (§3)
- `SettlementEngine` interface + `CustodialLedgerEngine` (§10)
- `BitcoinCoreRpcClient` — thin wrapper over `bitaicoind`'s JSON-RPC
  (`getnewaddress`, `listtransactions`, `gettransaction`, `listunspent`,
  `sendtoaddress`/`send`, `getblockcount`, `getblockhash`)
- `bitaicoin-finality.ts` (§7's note — new, small, chain-specific)
- Database schema (§5)
- Ed25519 receipt signer/verifier (§6) — a well-known library, not new
  cryptography
- Webhook dispatcher + retry queue

**BitAIcoin Core (this repo):** no code changes. Optionally, a short
addition to `docs/AGENT_PAYMENTS.md` or a new `docs/RPC_SURFACE.md` listing
exactly which stock RPCs BitAI Payment depends on, so a future BitAIcoin
change doesn't silently break an external dependent — documentation only.

---

## 12. The smallest end-to-end acceptance test

Working through the user's specified flow against the marketplace's actual,
existing architecture surfaced one real design decision worth making
explicit rather than defaulting past it (see the note after the flow).

```
1. Buyer deposits BAIC_TEST:
   POST /v1/deposit-intents (BitAI Payment) -> deposit address
   Buyer sends BAIC on the lab chain -> address
   BitAI Payment observes confirmation (§7) -> credits buyer's BitAI Payment balance
   BitaiPaymentAdapter's DEPOSIT PaymentIntent reaches CONFIRMED
   -> marketplace's own internal ledger credits buyer AGENT_AVAILABLE in a BAIC_TEST-denominated account
      (isolated from USD exactly like USDC_TEST, ADR-0113)

2. Buyer creates a Task, accepts a Quote -> Contract           (existing marketplace flow, unmodified)
3. Mandate evaluation authorizes the spend                      (existing marketplace flow, unmodified)
4. Marketplace funds Escrow: ESCROW_FUND posting,
   buyer AGENT_AVAILABLE(BAIC_TEST) -> contract ESCROW(BAIC_TEST) (existing marketplace ledger, unmodified — no adapter call)
5. Seller completes work off-platform; buyer verifies delivery  (existing marketplace flow, unmodified)
6. Marketplace releases Escrow: ESCROW_RELEASE posting,
   contract ESCROW(BAIC_TEST) -> seller AGENT_AVAILABLE(BAIC_TEST) (existing marketplace ledger, unmodified)
7. Contract -> COMPLETED; marketplace emits CONTRACT_SETTLED     (existing marketplace flow, unmodified)

8. (Separately) Seller requests a withdrawal of their BAIC_TEST proceeds:
   BitaiPaymentAdapter.submitTransfer(purpose=WITHDRAWAL) -> POST /v1/withdrawals
   BitAI Payment reserves, calls sendtoaddress with the idempotency key as label (§7)
   Confirms on the BitAIcoin lab chain -> WithdrawalIntent CONFIRMED
   BitAI Payment issues a signed receipt (§6): kind=WITHDRAWAL_SETTLED,
     externalReference="bitaicoin:<txid>:<vout>", chainAnchor={height, hash}

9. Acceptance criterion: an independent verifier — no API call to BitAI
   Payment required — checks the receipt's Ed25519 signature against BitAI
   Payment's published key, and independently confirms chainAnchor.blockHash
   against their own bitaicoin-cli. Both pass.
```

**The design note this surfaced:** the user's own diagram phrasing —
`mandate approval → BitAI Payment authorization → ... → BitAI Payment
settles → BitAIcoin-backed settlement recorded → signed receipt created →
CONTRACT_SETTLED` — reads naturally as BitAI Payment's own
authorization/capture primitive (§3's `/v1/authorizations`) backing the
contract's escrow directly, with the receipt issued *before*
`CONTRACT_SETTLED`. That is a **different, larger architecture change**
than the one above: it would make the marketplace's `Escrow` module
rail-aware for the first time, when today "escrow funding moves value
between two internal accounts and involves no adapter at all" for *every*
existing rail, including the live Base Sepolia one. Introducing that
now would be exactly the kind of duplicated authority §duplication (below)
warns about — two systems (BitAI Payment's hold and the marketplace's
escrow) both claiming to be the reason a contract's funds are locked.

The flow above gets the same real-world outcome — a contract that could
only be funded because real BAIC moved on-chain, and a cryptographic
receipt proving a real BitAIcoin settlement occurred — **without**
touching the marketplace's Escrow module at all: the deposit is the
"authorization" (funds are real and available before the contract exists),
and escrow/release stay exactly as proven today. If per-contract holds
inside BitAI Payment itself are genuinely wanted (e.g., so a deposit isn't
required upfront), that is worth a deliberate follow-up decision, not a
default.

---

## What duplicates existing functionality (and should not be built twice)

Direct answer to the question asked, gathered in one place:

1. **Authorization/capture for marketplace contracts** — already fully
   provided by the marketplace's own Mandate → Escrow → Release/Refund
   pipeline. BitAI Payment's own `/v1/authorizations` primitive should be
   reserved for **direct, non-marketplace** L402 use (an agent paying a
   raw HTTP 402 endpoint with no Task/Quote/Contract behind it) — not
   re-invoked per marketplace contract. See §12's note.
2. **Idempotency infrastructure** — the marketplace has its own
   idempotency-lease system (ADR-0093) for its own API; BitAI Payment
   needs its own, separate one for its own API and for BitAIcoin RPC calls
   (§8). These are naturally distinct layers, not a duplicate system —
   but they should not be merged, and the marketplace's idempotency key
   for a `PaymentAdapter` call is what *becomes* BitAI Payment's own
   idempotency key, not a second, independent one generated at that
   boundary.
3. **Reconciliation** — the marketplace's reconciliation module
   (`docs/reconciliation.md`) checks its own ledger against its own
   postings; it cannot and should not be asked to also reconcile BitAI
   Payment's ledger against BitAIcoin. BitAI Payment needs its own
   reconciliation sweep (§3's `POST /v1/reconcile`, §5's
   `reconciliation_findings`) — but should **adopt the same design
   discipline** (read-only, stable finding codes, no repair function for
   authoritative money, ADR-0090) rather than inventing a different
   philosophy for the same category of problem.
4. **The double-entry ledger pattern** — BitAI Payment should use the
   same discipline (append-only postings, balance derived not stored,
   balanced-transaction constraint) the marketplace already proved, for
   its own, separately-denominated (`BAIC`/`BAIC_TEST`) ledger. Same
   pattern, different money — not the same ledger, and never
   commingled with USD accounts (mirrors ADR-0113 exactly).
5. **Confirmation-depth/reorg evaluation** — `evm-finality.ts`'s exact
   code is not reusable (its `receiptStatus` field is EVM-specific), but
   its *design* (confirmations = head − height + 1; reorg = recorded
   block hash disagreement; below-depth is `PROVISIONAL`, never
   creditable) transfers directly and should be re-implemented as a small
   new function, not reinvented differently or skipped.
6. **UTXO/key management** — BitAIcoin Core's own wallet already does
   address generation, UTXO tracking, transaction construction and
   signing correctly (verified live, repeatedly, throughout Phase 1).
   BitAI Payment must not reimplement any of this — it is an
   orchestration layer over `bitaicoind`'s existing RPC, exactly as this
   design specifies, consistent with "BitAIcoin Core remains a narrow
   settlement/monetary layer."
7. **Agent identity / signature verification** — the marketplace already
   has a working Ed25519 challenge/response system (ADR-0011) and a
   canonical-serialization discipline for signed payloads. BitAI Payment's
   receipt signing (§6) reuses the same primitive and the same
   serialization discipline rather than introducing a second one.

---

## What remains genuinely open (not decided here, on purpose)

- **Confirmation depth** — depends on the still-open BitAIcoin production
  security-model decision.
- **Facilitator/direct-L402 trust policy** — whether BitAI Payment ever
  trusts a third-party facilitator's payment claim, or only ever trusts
  its own chain observation (the marketplace's own x402-compatibility.md
  flagged this as unresolved for the analogous case, and the same
  reasoning applies here unchanged: a claim is a hint, never an
  instruction).
- **Multisig/semi-custodial upgrade path** — technically available
  (SegWit is active), not designed here, and not assumed by anything
  above — §10's `SettlementEngine` seam is what would carry it, whichever
  form it eventually takes.
- **HD wallet vs. per-address `getnewaddress` calls** — an implementation
  detail of the `CustodialLedgerEngine`, deliberately left open since it
  doesn't affect anything above the `SettlementEngine` line.
