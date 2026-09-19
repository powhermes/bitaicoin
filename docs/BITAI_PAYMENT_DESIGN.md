# BitAI Payment — Design (Not Implemented)

Status: **Design only. No production code exists yet**, per explicit
instruction. This document is the deliverable `docs/AGENT_PAYMENTS.md`
called for: "a dedicated design/plan phase... informed by whatever real
usage patterns or requirements emerge from actually running the Phase 1
Synthetic Lab network."

**Revision note (this version):** amended after review to fix four issues
before implementation begins — a double-ledger ownership bug, a missing
authentication/authorization model, an unverified claim about BitAIcoin
RPC idempotency (corrected against the actual v31.1 fork), and a
conflation of two genuinely different attestations. Each is called out
where it changes prior content, rather than silently rewritten, so the
reasoning stays auditable. Nothing below assumes consensus changes;
`BitAIcoin Core` remains a narrow settlement/monetary layer throughout.

Grounded directly against the existing `agent-marketplace` codebase
(`~/Desktop/Projects/agent-marketplace`, reviewed file-by-file —
`packages/core/src/payments/payment-adapter.ts`,
`packages/core/src/payments/adapter-conformance.ts`,
`packages/contracts/src/entities/payments.ts`,
`apps/api/src/modules/payments/base-usdc-adapter.ts`,
`packages/db/src/schema/tables.ts`), and against this repo's actual RPC
surface (verified live against a running `bitaicoind -chain=regtest` for
this revision — see §8's withdrawal redesign).

## Target architecture (as specified)

```
Agent Marketplace
  -> BitaiPaymentAdapter        (implements the marketplace's PaymentAdapter, in the marketplace repo)
  -> BitAI Payment              (a new, separate service — this document's main subject)
  -> BitAIcoin Core / RPC       (this repo — narrow settlement layer, no new consensus features)
```

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
- `ExternalAsset` enum currently: `MOCK_USD | USDC | USDC_SEPOLIA | BTC | HBAR | FIAT_USD`. **`BAIC` and `BAIC_TEST` do not exist yet and must be added** (see §12).
- `SettlementCurrency` enum currently: `USD | USDC_TEST`. **`BAIC_TEST` must be added**, mirroring exactly how `USDC_TEST` is kept structurally isolated from `USD` (ADR-0113) — a testnet/lab-chain BAIC balance must never be reachable from a USD-denominated mandate, quote, or contract.

### The conformance suite (`packages/core/src/payments/adapter-conformance.ts`)

Every assertion `BitaiPaymentAdapter` must pass, verbatim from the file:

1. Declares capabilities; `supportedAssets`/`supportedNetworks` non-empty; `handlesRealValue` is a boolean.
2. `supportsQueryByIdempotencyKey` is `true` (asserted directly — not optional for any adapter).
3. Declares `initiatesTransfers` honestly; if `false`, `submitTransfer` must throw `UNSUPPORTED`. **BitaiPaymentAdapter declares `true`** (unlike `BaseUsdcAdapter`) because it originates withdrawals.
4. Submitting the same `idempotencyKey` two or three times returns the *same* `externalReference` every time.
5. `reconcileByIdempotencyKey` on a never-seen key returns `null` — not an error.
6. `submitTransfer`'s result has a `state` in the enum and a `finality` in the enum, and `observedAt` is set.
7. The reported `observedAmount.{value, asset}` matches what was actually moved.
8. Once an `externalReference` exists, `getTransfer` by that reference returns it.
9. The rail eventually reaches `FINAL` (via the harness's `settle()` hook if asynchronous), and never claims `FINAL` early.
10. A lost response is surfaced as `UNKNOWN_OUTCOME` (`mustReconcile: true`, `mayResend: false`), and `reconcileByIdempotencyKey` afterward finds what actually happened.
11. A terminal transfer stays terminal across repeated queries (same `state`, same `externalReference`).
12. Distinct keys produce distinct transfers.

`BitaiPaymentAdapter`'s own test file runs this exact suite, with a
`ConformanceHarness` whose `settle()` hook mines a lab-chain block (or waits
for the configured confirmation depth) and whose `loseResponseFor()` hook
simulates a dropped HTTP response between the marketplace and BitAI Payment.

---

## 2. Proposed `BitaiPaymentAdapter` — mapping marketplace operations to BitAI Payment

`BitaiPaymentAdapter` lives in the marketplace repo
(`apps/api/src/modules/payments/bitai-payment-adapter.ts`) and is a **thin,
authenticated HTTP client** — structurally the same role `BaseUsdcAdapter`
plays over an `EvmRpcClient`, except the thing it talks to is BitAI
Payment's own API (§3), not a chain RPC directly.

**Account model (fixes the double-ledger issue — see the design note
below the table).** Every call `BitaiPaymentAdapter` makes is authenticated
as a single **tenant principal** representing the whole marketplace
deployment (§4). It never passes, and BitAI Payment never accepts, an
individual agent identifier as the thing that determines fund ownership.
All marketplace-originated value lives in **one omnibus account per
currency**, owned by the marketplace's tenant principal.

| PaymentAdapter method | Maps to |
|---|---|
| `railId` | `"bitaicoin-lab"` while on the Phase 1 Synthetic Lab chain; `"bitaicoin"` once a production chain exists. Distinct strings on purpose — a deployment must not be able to point a lab-rail-configured marketplace at a real-value rail by accident. |
| `capabilities()` | See table below. |
| `submitTransfer(request)` — `purpose: "WITHDRAWAL"` | `POST /v1/withdrawals`, authenticated as the marketplace tenant, debiting the tenant's own omnibus account, paying out to `request.instrument.reference` (an external address the marketplace itself supplies — e.g. the withdrawing agent's own wallet). BitAI Payment never learns or needs to know which agent this is for. |
| `submitTransfer(request)` — `purpose: "DEPOSIT"` | **Refuses with `UNSUPPORTED`.** A deposit is not "submitted" — see §8. |
| `getTransfer(externalReference)` | `GET /v1/transfers/{externalReference}`, authenticated, scoped to the tenant's own transfers. |
| `reconcileByIdempotencyKey(key)` | `GET /v1/transfers/by-idempotency-key/{key}` — the mandatory recovery path. |
| `finalityOf(status)` | Pure pass-through of `status.finality`. |
| `verifyCallback(payload, sig)` | Verifies BitAI Payment's webhook signature (§4). Per ADR-0104, a verified callback still only triggers a re-query — it never credits directly. |

### Capabilities declaration

```ts
capabilities(): AdapterCapabilities {
  return {
    initiatesTransfers: true,
    supportsDeposits: true,
    supportsWithdrawals: true,
    supportsRefunds: true,
    supportsMicropayments: true,
    supportsMemo: true,
    supportsNativeIdempotency: false, // BitAIcoin/Bitcoin Core RPC has none (see §8); BitAI Payment supplies it
    supportsQueryByIdempotencyKey: true,
    supportsFinality: true,
    supportsCallbacks: true,
    supportedAssets: ["BAIC_TEST"], // ["BAIC"] once a production chain exists — never both from one adapter instance
    supportedNetworks: ["bitaicoin-lab"],
    handlesRealValue: false,        // true only once BitAI Payment points at a real-value BitAIcoin chain
  };
}
```

### Design note: why an omnibus account, not per-agent BitAI Payment accounts

The original version of this design had BitAI Payment attribute each
deposit to the depositing agent's *own* BitAI Payment account, while the
marketplace *separately* tracked that same agent's balance in its own
`BAIC_TEST` ledger. That is two systems of record for one ownership fact,
and they diverge the moment escrow moves ownership: the marketplace's own
ledger reassigns buyer → seller entirely internally (correctly — "escrow
funding moves value between two internal accounts and involves no adapter
at all" is already how every existing rail works), but BitAI Payment would
never be told, and would still believe the *buyer* owns the funds when the
*seller* later tries to withdraw them.

The fix is the standard pattern for exactly this shape of problem (it's
how a real custodian handles an institutional client, not a novel
invention): **BitAI Payment is authoritative for aggregate custody
belonging to the marketplace; the marketplace remains authoritative for
who, inside that aggregate, owns what.** BitAI Payment tracks one number
(the marketplace's total BAIC_TEST at BitAI Payment); the marketplace's
own ledger — already proven, already audited, already the place Mandates
and Escrow live — tracks the rest. No second ownership ledger exists to
disagree with the first.

Direct, non-marketplace BitAI Payment users (§13's note on the standalone
L402 use case) still get individual accounts, because there is no
marketplace ledger standing behind them to be the second source of truth.

---

## 3. The BitAI Payment service boundary and API

A new, separate deployable — not part of this repo, not part of the
marketplace monorepo. Every endpoint below requires the authentication
described in §4; none of them accept a caller-supplied account ID that
isn't first checked against the authenticated principal's own ownership.

```
POST   /v1/deposit-intents              { idempotencyKey, expectedAmount, asset, memo? }
                                         -> { depositAddress, expiresAt, fromHeight }
                                         Credits the AUTHENTICATED PRINCIPAL's own account
                                         (its omnibus account, if a TENANT). No ownerRef parameter exists.

GET    /v1/transfers/{externalReference}
GET    /v1/transfers/by-idempotency-key/{key}
                                         -> RailTransferStatus-shaped JSON (§1's exact shape)
                                         Scoped to transfers the caller's own account was party to.

POST   /v1/withdrawals                  { idempotencyKey, amount, asset, destinationAddress, memo? }
                                         -> RailTransferStatus | 202 UNKNOWN_OUTCOME-equivalent
                                         Debits the AUTHENTICATED PRINCIPAL's own account.

POST   /v1/internal-transfers           { idempotencyKey, toAccountId, amount, asset }
                                         -> instant, off-chain, no BitAIcoin transaction
                                         `toAccountId` is a destination the caller names; the SOURCE is
                                         always the caller's own account, never a parameter.

POST   /v1/authorizations               { idempotencyKey, amount, asset, resourceRef }
                                         -> { authorizationId, holdExpiresAt }
                                         Direct/non-marketplace L402 use only — see §13's note. Holds
                                         against the AUTHENTICATED PRINCIPAL's own account.
POST   /v1/authorizations/{id}/capture  { amount? }
POST   /v1/authorizations/{id}/void

GET    /v1/challenges/{resourceRef}     -> HTTP-402-shaped payment requirement (amount, asset, address/invoice, expiry)

POST   /v1/receipts/verify              { receipt, signature } -> { valid: boolean, reason? }
                                         [convenience only — primary verification is offline, §7]

GET    /v1/accounts/me/balance          -> { available, pending, currency }
                                         Deliberately "me", not "/accounts/{id}" — see §4: there is no
                                         endpoint that takes an arbitrary account id as a path parameter
                                         for anything but a reference already scoped to the caller.

GET    /v1/rail-status                  -> chain reachability, head height, confirmation depth, handlesRealValue
POST   /v1/reconcile                    -> operator-only, separately authenticated (§4), read-only findings

Webhook (BitAI Payment -> subscriber):
POST   {webhook URL configured at onboarding — never a runtime parameter, §4}
                                         RailCallbackEnvelope-shaped body — a hint, per ADR-0104
```

---

## 4. Authentication, authorization, anti-replay, and webhook destination policy

Not present in the prior version of this design. Added because "no caller
should be able to withdraw, transfer from, query, or authorize against an
arbitrary account merely by supplying its ID" is a hard requirement, and
the prior draft implicitly allowed exactly that by accepting account/owner
identifiers as untrusted request parameters.

### Principals

Two kinds, and every account has exactly one owner:

| Principal kind | Example | Owns |
|---|---|---|
| `TENANT` | the agent marketplace deployment | exactly its own omnibus account(s), one per currency |
| `INDIVIDUAL` | a direct, non-marketplace L402 payer | exactly its own individual account |

A principal is provisioned **out-of-band** (an operator action — for the
single-tenant Phase 1 case this can literally be a config entry; it does
not need a self-service signup flow to be correct). Provisioning records:
`principalId`, `kind`, its Ed25519 **public** key, and (for a `TENANT`) its
webhook URL. BitAI Payment never possesses or transmits a principal's
private key.

### Request authentication

Every API call (not just webhooks) carries a signed envelope, deliberately
reusing the same shape and the same primitive already established for
receipts (§7) and already used by the marketplace itself for agent
authentication (ADR-0011) — one cryptographic scheme for the whole system,
not a second one invented for this boundary:

```
Headers:
  X-Bitai-Principal:  <principalId>
  X-Bitai-Timestamp:  <unix ms>
  X-Bitai-Nonce:      <opaque, unique per principal, e.g. a UUID>
  X-Bitai-Signature:  <Ed25519 signature, base64>

Signed material (canonicalized, same discipline as receipt signing, §7):
  method + "\n" + path + "\n" + sha256(body) + "\n" + timestamp + "\n" + nonce
```

Verification, in order, any failure is a `401`:

1. The `principalId` is known and its public key resolves.
2. `timestamp` is within a freshness window (e.g. ±60s) of BitAI Payment's
   own clock.
3. `(principalId, nonce)` has not been seen before within the freshness
   window — a small, short-TTL store (the window is bounded, so this never
   grows unbounded) rejects a replayed request even if the signature is
   valid. This is the exact `emittedAt` + `nonce` anti-replay shape
   `RailCallbackEnvelope` already uses for webhooks, applied here to
   *inbound* requests as well.
4. The Ed25519 signature verifies against the canonicalized material.

### Authorization

**The account is derived from the authenticated principal, not accepted as
a caller-supplied parameter, wherever the operation is inherently
"my account."** `GET /v1/accounts/me/balance`, `POST
/v1/deposit-intents`, and `POST /v1/withdrawals`'s debited side all work
this way — there is no `accountId` field for the caller to substitute
someone else's account into. Where an operation *does* need to name a
second account (`POST /v1/internal-transfers`'s `toAccountId`), that
field only ever names a **destination**, never a source, and the
destination is validated to exist and to accept the asset — it is never
used to authorize taking money *from* it.

A `TENANT` principal can never resolve to, query, or act on any
`INDIVIDUAL` account, or another tenant's omnibus account, under any
parameter combination — there is no code path that looks up an account
by anything other than `(authenticated principal, currency)`.

### Webhook destination policy

A principal's webhook URL is part of its out-of-band provisioning record
(above), never a value accepted from a routine, per-request API call.
This closes two real risks with one rule: a compromised or careless
caller cannot redirect another principal's notifications, and no runtime
endpoint exists that could be used as an SSRF vector by supplying an
internal or link-local URL. At provisioning time, the URL is validated to
be `https://` and to resolve to a public, non-link-local, non-loopback
address. Every webhook payload is itself signed (§7's Ed25519 discipline,
same key infrastructure) — even a misdirected or intercepted webhook
grants no capability on its own, consistent with ADR-0104's "a callback
is a hint, never an instruction," which is unaffected by any of this.

### What this does not cover (left open, §"genuinely open")

Key rotation, individual-principal self-registration flow, and rate
limiting per principal are real operational needs but are policy details
that don't change the shape of anything above — deliberately deferred
rather than guessed at here.

---

## 5. The internal payment state machine

Three separate state machines, deliberately not fused — same reasoning as
the marketplace keeping `PaymentIntentState` separate from every internal
entity's own machine.

**Deposit intent:**
```
CREATED -> AWAITING_PAYMENT -> DETECTED -> CONFIRMING -> CONFIRMED
                             \                        \-> REORGED -> CONFIRMING (re-watch) | EXPIRED
                              \-> EXPIRED (nothing arrived in time)
```

**Withdrawal intent** (revised — see §8 for why):
```
CREATED -> RESERVED -> BUILDING -> SIGNED -> BROADCASTING -> BROADCAST -> CONFIRMING -> CONFIRMED
                     \                     \-> UNKNOWN (bitcoind RPC call itself failed ambiguously;
                      \-> FAILED             reconcile via persisted txid, never resend blindly — §8/§9)
                       \-> RELEASED
```
`SIGNED` is a new, explicit state: the point at which the fully-signed raw
transaction and its deterministic `txid` are **durably persisted, before
broadcast**. This is the anchor the whole recovery story hangs from — see
§8.

**Authorization hold** (direct/non-marketplace use only — §13's note):
```
CREATED -> HELD -> CAPTURED (partial or full)
                 \-> VOIDED
                 \-> EXPIRED
```

Normalization into the marketplace's own `PaymentIntentState` happens at
the `BitaiPaymentAdapter` boundary, one direction only.

---

## 6. The minimum ledger/data model

BitAI Payment owns its own PostgreSQL database, structurally independent
of the marketplace's. Revised for the omnibus account model (§2):

```
accounts                  (id, owner_kind [TENANT_OMNIBUS | INDIVIDUAL], owner_principal_id UNIQUE per currency,
                            currency [BAIC | BAIC_TEST], created_at)
                           -- exactly one row per (owner_principal_id, currency); a TENANT's
                           -- omnibus account is this table's only representation of "the marketplace's money" —
                           -- there is no per-agent row here for marketplace-originated funds.

ledger_transactions        (id, kind, idempotency_key UNIQUE, correlation_ref, created_at)
ledger_postings            (id, transaction_id, account_id, amount, created_at)  -- append-only; trigger enforces sum(amount) = 0 per transaction_id

deposit_intents            (id, idempotency_key UNIQUE, account_id, expected_amount, asset,
                             deposit_address UNIQUE, hd_index, from_height,
                             state, recorded_txid, recorded_vout, recorded_block_hash,
                             confirmations_last_seen, created_at, expires_at)

withdrawal_intents         (id, idempotency_key UNIQUE, account_id, amount, asset,
                             destination_address, state,
                             built_psbt, signed_raw_hex, signed_txid,   -- persisted BEFORE broadcast (§8)
                             locked_utxos_json,                        -- for lockunspent recovery (§8)
                             recorded_block_hash, fee_paid, created_at)

authorizations             (id, idempotency_key UNIQUE, account_id, resource_ref,
                             amount_held, amount_captured, state, expires_at)

receipts                   (id, ledger_transaction_id, kind, payload_json, signature,
                             signing_key_id, issued_at)   -- Custody Settlement Receipts only, §7

principals                 (id, kind [TENANT | INDIVIDUAL], public_key, webhook_url, created_at)  -- §4
request_nonces             (principal_id, nonce, seen_at)  -- short-TTL, bounded by the freshness window (§4)

wallet_addresses           (address UNIQUE, hd_index, purpose, allocated_to_deposit_intent_id, created_at)
webhook_deliveries         (id, principal_id, envelope_json, attempt_count, last_attempt_at, delivered_at)
reconciliation_findings    (id, code, severity, detail_json, found_at, resolved_at)  -- read-only, no repair function (mirrors ADR-0090)
```

Structural guarantees:

| Constraint | Prevents |
|---|---|
| `UNIQUE(account_id, currency)` on `accounts` where `owner_kind = TENANT_OMNIBUS` | A second, competing omnibus account for the same tenant |
| `UNIQUE(idempotency_key)` per intent table | Two intents for one intention |
| `UNIQUE(deposit_address)` | Two intents sharing one destination |
| `UNIQUE(recorded_txid, recorded_vout)` | Two intents claiming one UTXO |
| `UNIQUE(signed_txid)` on `withdrawal_intents` where not null | Two intents believing they own one broadcast transaction |
| Balanced-transaction trigger | A posting set that doesn't sum to zero |
| Every write to `accounts`/`withdrawal_intents`/`deposit_intents` requires `account.owner_principal_id = authenticated principal` | The exact "arbitrary account by ID" risk §4 exists to close |

---

## 7. Two signed attestations, kept honestly separate

The prior version of this design had one receipt type and let the
acceptance flow imply it proved a marketplace *contract* settled
on-chain. That conflates two genuinely different facts, and the omnibus
model (§2) makes the conflation impossible to sustain even if it were
desired: **BitAI Payment has no visibility into individual marketplace
contracts at all.** It sees one tenant's aggregate balance move. What
"contract 12345 settled" means is a fact the marketplace's own ledger
holds, not one BitAI Payment could truthfully attest to even if asked.

### Custody Settlement Receipt (BitAI Payment issues this)

Proves: **"BitAI Payment moved this much BAIC on or off BitAIcoin, into
or out of this account's aggregate custody, at this txid, confirmed at
this block."** Nothing more.

```ts
interface CustodySettlementReceipt {
  receiptId: string;
  schemaVersion: 1;
  kind: "DEPOSIT_SETTLED" | "WITHDRAWAL_SETTLED" | "INTERNAL_TRANSFER_SETTLED" | "AUTHORIZATION_CAPTURED";
  accountRef: string;       // the omnibus or individual account at BitAI Payment — never a marketplace agent id
  amount: string;
  asset: "BAIC" | "BAIC_TEST";
  ledgerTransactionId: string;
  externalReference: string | null;   // "bitaicoin:<txid>:<vout>" for on-chain settlements; null for internal transfers
  chainAnchor: { blockHeight: number; blockHash: string } | null;
  settledAt: string;
  issuedAt: string;
  scopeNote: "This receipt attests to aggregate custody movement only. It does not identify, and is not evidence of, any individual marketplace contract, task, or agent-to-agent transaction that the underlying balance may be associated with.";
}
```

`scopeNote` is a literal field, not just documentation — a receipt that
carries its own honest limitation is harder to accidentally
over-interpret downstream than one that relies on a reader having read
this document.

**Signing:** canonicalize (sorted keys, no whitespace) then sign with
Ed25519 — the same key infrastructure §4 uses for request authentication.
**Verification (primary path is offline):** recompute the canonical
serialization, verify the signature against BitAI Payment's published
public key for `signingKeyId`, and independently confirm `chainAnchor`
against an independent `bitaicoin-cli`, with zero trust in BitAI Payment
required for that last step.

### Contract-Settlement Record (the marketplace issues this — BitAI Payment does not)

This is not a new artifact. It **is** the marketplace's own existing
internal ledger transaction plus its `CONTRACT_SETTLED` event — the same
thing that already exists today for the mock rail and for Base Sepolia
USDC. Proves: *"per the marketplace's own authoritative ledger, this
contract's escrow released from buyer to seller."* It says nothing about
whether, or when, any of that value has ever crossed a blockchain — under
the omnibus model, escrow release is a pure internal bookkeeping event
that happens with zero BitAI Payment involvement, exactly as it already
does for every other rail.

**A Custody Settlement Receipt is never, by itself, sufficient evidence
that any particular marketplace contract settled — only the
marketplace's own ledger record is.** The two attestations answer
different questions and neither substitutes for the other; §14's
acceptance test checks them separately for exactly this reason.

---

## 8. Deposit, withdrawal, confirmation, and reorg behavior

### Deposits

Unchanged from the prior version's design, now explicitly crediting the
omnibus/individual account per §2 rather than a per-agent one:

1. `POST /v1/deposit-intents` allocates a fresh address and records
   `from_height = getblockcount()` **before** anything else — the same
   binding-boundary technique as `BaseUsdcAdapter`'s `fromBlock`
   (ADR-0122).
2. A background poller watches the address via `listunspent`/
   `gettransaction`, independently tracking the block hash at first
   observation (`recorded_block_hash`) rather than trusting
   `confirmations` alone, so a reorg is detected the moment the reported
   block hash disagrees with what was recorded — the same logical check
   `evaluateFinality` makes for EVM (§ note below), reimplemented for
   BitAIcoin's transaction model.
3. Below the configured confirmation depth: `CONFIRMING`. At or above:
   `CONFIRMED`, and only then is the account credited.
4. **Reorg:** a previously-recorded `(txid, vout, blockhash)` that
   disappears does not immediately fail the intent — it returns to
   `CONFIRMING` and keeps watching, since Bitcoin-model reorgs commonly
   re-mine the same mempool transaction within the next block or two.
   Only a configured timeout with no reappearance moves it to `FAILED`.
5. Attribution is `UNIQUE_ADDRESS` (one address per intent, mirroring
   ADR-0130) — simpler than Base's account-model case, since UTXO outputs
   don't share an address the way EVM balances share an account.
   Underpayment never confirms; overpayment confirms at the declared
   amount, leaving the surplus as a separately-reconcilable UTXO.

### Withdrawals — corrected against the actual fork's RPC behavior

**The prior version of this design was wrong, and the correction below
is verified live against this repo's own build (`bitaicoind
-chain=regtest`), not assumed.**

`sendtoaddress`'s `comment`/`comment_to` parameters are, per the RPC's
own help text, **"not part of the transaction, just kept in your
wallet"** — pure local metadata. Worse, `listtransactions "label"`
filters by a **label assigned to an address the wallet owns**, and only
for the `"receive"` category — it cannot recover an outgoing send to an
external destination at all, by comment or otherwise. There is no
"search by comment" recovery path in Bitcoin Core's RPC, full stop, and
the prior draft's claim that one existed was incorrect.

**The actual, verified-available primitive is transaction determinism
itself.** A fully-signed Bitcoin-model transaction's `txid` is fixed the
moment it is finalized — Bitcoin Core signs deterministically (RFC 6979),
so the same inputs signed with the same keys always produce the same
signature and therefore the same `txid`, and nothing about broadcasting
changes it. So: **build, sign, and compute the final txid entirely
before ever calling `sendrawtransaction`, persist both durably, and
recovery becomes "is this exact, already-known txid confirmed yet?" —
never a search.**

Verified-present RPCs this relies on (checked against this repo's own
`bitaicoin-cli help <rpc>` output): `walletcreatefundedpsbt`,
`walletprocesspsbt`, `finalizepsbt`, `decoderawtransaction`,
`lockunspent` (with `persistent=true`, survives a restart),
`sendrawtransaction`, `gettransaction`, `getrawtransaction`. All stock,
unmodified Bitcoin Core RPCs — no new RPC and no consensus change.

**The withdrawal flow:**

1. Authenticate and authorize the request (§4); verify the account has
   sufficient available balance; write a `withdrawal_intents` row
   (`CREATED`), reserve the funds with a ledger posting to a pending
   sub-balance, and **commit** — before BitAIcoin is touched at all.
2. `walletcreatefundedpsbt` — let the wallet select inputs automatically.
3. `lockunspent(unlock=false, <the selected inputs>, persistent=true)`
   immediately, so a **concurrent** withdrawal request cannot select the
   same UTXOs, and the lock survives a crash (persisted to the wallet
   database) rather than evaporating on restart.
4. `walletprocesspsbt(sign=true)` then `finalizepsbt(extract=true)` to
   get the fully-signed `hex`. If `complete` is ever `false` for a
   single-signer withdrawal wallet, unlock the inputs and retry from
   step 2 — nothing has been broadcast, so this is free to redo.
5. `decoderawtransaction(hex)` to read the deterministic `txid`.
6. **Persist `signed_raw_hex` and `signed_txid` on the withdrawal_intents
   row, move state to `SIGNED`, and commit — before step 7.** This is
   the load-bearing durability point: the exact bytes that will be
   broadcast exist in BitAI Payment's own database before they exist
   anywhere else.
7. `sendrawtransaction(hex)`:
   - Accepted, or rejected as "already in the mempool" → `BROADCAST`.
     Either answer means the same transaction exists on the network;
     which of possibly several calls actually put it there is
     irrelevant, because they are byte-identical.
   - Rejected as "missing inputs" (our own inputs are already spent) →
     query `getrawtransaction`/`gettransaction` on the persisted
     `signed_txid`: if it is already confirmed, this is a **success
     signal** from an earlier, crashed attempt whose response was lost —
     move to `CONFIRMING`/`CONFIRMED` as appropriate, not to `FAILED`.
   - Any other failure calling BitAIcoin itself (RPC unreachable, etc.)
     → state stays `SIGNED` (the marketplace-facing equivalent of
     `UNKNOWN_OUTCOME`). **Never resend a fresh `walletcreatefundedpsbt`
     here** — the already-signed bytes are what must eventually be
     broadcast, or nothing, never a second, differently-selected
     transaction.
8. Confirmation and reorg handling mirror the deposit path (§ above),
   tracked from BitAI Payment's own side.

### A note on `evaluateFinality`

`packages/core/src/payments/evm-finality.ts` is **not directly
reusable** — its `receiptStatus: 0 | 1 | null` field encodes EVM
transaction revert, which has no BitAIcoin/Bitcoin-model equivalent (a
transaction inside a valid block is unconditionally valid). The right
move is a small, new, analogous function
(`bitaicoin-finality.ts`, `BITAICOIN_FINALITY_POLICY_VERSION =
"bitaicoin-confirmation-depth-v1"`) reusing the same *logic*
(confirmations = head − height + 1; reorg = recorded block hash ≠
current; below depth = `PROVISIONAL`; at or above = `FINAL`) minus the
revert check. Same pattern, new file.

**Confirmation depth remains an open parameter**, deliberately not
specified here — it depends on the still-open production security-model
decision (`PHASE1_REPORT.md`'s `PRODUCTION_DIFFICULTY_NOT_FINAL`).

---

## 9. Idempotency and crash/recovery behavior

The governing rule, restated one layer down from the marketplace's own
(ADR-0100): **uncertainty causes reconciliation, never duplication.**

| Crash point | Recovery |
|---|---|
| After the reservation commits, before `walletcreatefundedpsbt` | Safe — nothing built yet. Retry from scratch. |
| After PSBT built/signed, before persisting `signed_raw_hex`/`signed_txid` (state still pre-`SIGNED`) | Safe to discard and rebuild — nothing was ever broadcast, and the previously-selected (but now possibly-unlocked) UTXOs simply get reselected. |
| After `signed_raw_hex`/`signed_txid` persisted (state `SIGNED`), before/during `sendrawtransaction` | **Never rebuild. Never resign.** Check `getrawtransaction(signed_txid)`; if absent, safely re-broadcast the identical persisted `hex` — idempotent by construction because it is byte-for-byte the same transaction. |
| Mid-reconciliation read | Idempotent by construction — it is only asking again. |
| Mid-webhook-delivery | The webhook is a hint (ADR-0104); a lost delivery affects latency, not correctness. `webhook_deliveries` exists for operator visibility and retry, not for correctness. |
| Internal transfer, response lost | Ordinary DB-transaction idempotency: insert-or-return-existing on `idempotency_key`, no chain interaction, no `UNKNOWN` state is even reachable. |
| Request-signature replay (§4) | Rejected by the nonce store regardless of process crashes on either side — the nonce table is durable and keyed by `(principal, nonce)`, not by in-flight request state. |

`UNKNOWN`/`SIGNED`-stuck states are never treated as failure, for the
same reason the marketplace's own `PaymentIntentState.UNKNOWN` isn't:
returning reserved funds on an unresolved outcome risks paying twice;
treating them as gone robs the owner. Reserved is the honest position
until a `getrawtransaction` lookup resolves it.

---

## 10. Custody and trust assumptions

**Version 1 is fully custodial. Stated plainly, not softened.** BitAI
Payment holds every private key involved — every deposit address and the
withdrawal wallet — via BitAIcoin Core's own wallet. This is
mechanically **the same trust model as a centralized exchange**, and the
same one the marketplace's own Turnkey integration attempt reached for
and then explicitly shelved rather than ship half-verified (ADR-0143).

The omnibus account model (§2) does not weaken this further, but it does
concentrate it: a single BitAI Payment compromise now puts the *entire*
marketplace tenant's aggregate balance at risk in one place, rather than
many small per-agent balances. This is an explicit, named tradeoff for
this design, not an accident — the alternative (per-agent BitAI Payment
accounts) is exactly the design that produced the double-ledger bug §2
fixes. Mitigation is the same operational playbook already named
(hot/cold split, withdrawal velocity limits, monitoring), applied with
proportionally more care given the concentration.

"Semi-custodial" is not used to describe v1: that term implies a real
cryptographic split (a 2-of-2 or 2-of-3 multisig BitAI Payment alone
cannot satisfy), which v1 does not have. BitAIcoin already supports
P2WSH multisig (SegWit active from the fork point, Phase 1 M5) if that is
wanted later — a genuine, separate design decision, not assumed here.

**Threat model, stated as bullets:**
- A compromised BitAI Payment signing process/key can move the entire
  custodied balance it controls (concentrated by the omnibus model —
  see above). Mitigation is operational, not cryptographic, in v1.
- A compromised BitAI Payment database can forge internal ledger state
  but **cannot forge a receipt** without also compromising the Ed25519
  signing key — the receipt's trust anchor is narrower than the
  database's.
- A compromised or malicious *marketplace* tenant credential (§4) can
  withdraw the marketplace's entire omnibus balance to any address it
  names — this is inherent to the tenant owning that account, not a
  bug, and is exactly why request authentication (§4) and the
  marketplace's own operational security matter as much as BitAI
  Payment's.
- A BitAIcoin-side compromise (a consensus bug, a 51%-class attack) is
  outside BitAI Payment's threat model and is BitAIcoin Core's own
  concern (the still-open security-model question in
  `PHASE1_REPORT.md`).

This should never be marketed or documented anywhere as trustless. It is
not.

---

## 11. How a future bilateral-channel backend replaces the ledger backend

Unchanged in substance from the prior version — the omnibus account
model (§2) and the auth model (§4) both sit *above* this seam and are
unaffected by what's behind it.

```ts
interface SettlementEngine {
  recordDeposit(intent: DepositIntent, observed: ObservedUtxo): Promise<SettlementResult>;
  reserveForWithdrawal(intent: WithdrawalIntent): Promise<void>;
  settleWithdrawal(intent: WithdrawalIntent, txid: string): Promise<SettlementResult>;
  transferInternal(fromAccountId: string, toAccountId: string, amount: string, idempotencyKey: string): Promise<SettlementResult>;
  capture(authorizationId: string, amount: string): Promise<SettlementResult>;
}
```

- **v1**: `CustodialLedgerEngine implements SettlementEngine` — §6's
  ledger plus §8's PSBT-based withdrawal flow.
- **v2 (future, not designed here)**: `ChannelSettlementEngine` — routes
  `transferInternal` through a bilateral HTLC channel when one is open;
  `settleWithdrawal` cooperatively closes or updates a channel instead of
  building a raw transaction.
- Nothing above this line changes: not the HTTP API (§3), not
  `BitaiPaymentAdapter`, not the account-ownership model, not the
  authentication scheme. The receipt format (§7) already accommodates
  this — `externalReference` becomes a channel reference and
  `chainAnchor` is `null` until a channel closes on-chain, both already
  nullable/optional fields.

---

## 12. Exact modules/files likely to be added or changed

**Marketplace repo (`agent-marketplace`):**

| File | Change |
|---|---|
| `packages/contracts/src/entities/payments.ts` | Add `"BAIC"`, `"BAIC_TEST"` to `ExternalAsset`; add `"BAIC_TEST"` to `SettlementCurrency` |
| `apps/api/src/modules/payments/bitai-payment-client.ts` | **New.** Typed HTTP client, including request signing per §4 |
| `apps/api/src/modules/payments/bitai-payment-adapter.ts` | **New.** The `BitaiPaymentAdapter` class (§2) |
| `apps/api/test/bitai-payment-adapter.test.ts` | **New.** Runs `runAdapterConformance` |
| `docs/bitai-payment-adapter.md` | **New**, marketplace repo |
| `docs/decisions/01XX-bitaicoin-utxo-first-rail.md` | **New ADR** — the UTXO/unique-address distinction |

**BitAI Payment (new, separate service):**

- HTTP API layer (§3)
- `AuthenticationMiddleware` — request-signature verification, nonce-replay defense (§4)
- `SettlementEngine` interface + `CustodialLedgerEngine` (§11)
- `BitcoinCoreRpcClient` — wrapper over `bitaicoind`'s JSON-RPC, exposing
  specifically: `getnewaddress`, `listunspent`, `gettransaction`,
  `getrawtransaction`, `walletcreatefundedpsbt`, `walletprocesspsbt`,
  `finalizepsbt`, `decoderawtransaction`, `lockunspent`,
  `sendrawtransaction`, `getblockcount`, `getblockhash`
- `PsbtWithdrawalBuilder` — encapsulates §8's build→lock→sign→finalize→
  persist→broadcast sequence as one auditable unit
- `bitaicoin-finality.ts` (§8's note)
- Database schema (§6), including `principals` and `request_nonces` (§4)
- Ed25519 signer/verifier, shared by request auth (§4) and receipts (§7)
- Webhook dispatcher + retry queue, reading destinations only from the
  `principals` table (§4) — never from a request body

**BitAIcoin Core (this repo):** no code changes. Optionally, a short
addition to `docs/AGENT_PAYMENTS.md` or a new `docs/RPC_SURFACE.md`
listing exactly which stock RPCs BitAI Payment depends on (the list
above), so a future BitAIcoin change doesn't silently break an external
dependent.

---

## 13. Note on direct/non-marketplace use

Several pieces of this design (`/v1/authorizations`, `/v1/challenges`,
`INDIVIDUAL` principals with their own accounts) exist for an agent
paying a raw HTTP-402 endpoint with no marketplace Task/Quote/Contract
behind it at all. They are part of the design because the user's original
spec asked for them and because BitAI Payment is meant to be a genuinely
reusable payment layer, not something married to one marketplace. They
are **not exercised by §14's acceptance test**, which is entirely the
marketplace-mediated path.

---

## 14. The smallest end-to-end acceptance test

Revised for the omnibus account model (§2) and the two-attestation
distinction (§7). The design tension the prior version glossed over —
whether "BitAI Payment authorization" in the user's original flow
diagram means BitAI Payment holds funds per-contract — is resolved the
same way as before and restated precisely here: it does not. The
existing marketplace Escrow, unmodified, is what holds funds per
contract; BitAI Payment's role is limited to the aggregate deposit that
preceded it and the aggregate withdrawal that may follow it, at an
unrelated time.

```
1. The marketplace (TENANT principal) creates a deposit-intent for ITS OWN
   omnibus account:
     POST /v1/deposit-intents  (authenticated as the marketplace tenant, §4)
     -> deposit address
   A buyer (an identity BitAI Payment never learns) sends BAIC_TEST to it.
   BitAI Payment observes confirmation (§8) -> credits the MARKETPLACE'S
   OMNIBUS account (not an individual one) -> issues a Custody Settlement
   Receipt (kind=DEPOSIT_SETTLED, accountRef=the omnibus account) to the
   marketplace tenant.

2. BitaiPaymentAdapter's DEPOSIT PaymentIntent reaches CONFIRMED.
   -> HERE, and only here, does "the buyer" as an identity enter the
      picture: the marketplace's own ledger credits that specific buyer's
      AGENT_AVAILABLE(BAIC_TEST). BitAI Payment never sees this step.

3. Buyer creates a Task, accepts a Quote -> Contract      (existing marketplace flow, unmodified)
4. Mandate evaluation authorizes the spend                 (existing marketplace flow, unmodified)
5. Marketplace funds Escrow: ESCROW_FUND posting,
   buyer AGENT_AVAILABLE(BAIC_TEST) -> contract ESCROW(BAIC_TEST)
   (existing marketplace ledger, unmodified — no BitAI Payment call; the
    funds are already inside the marketplace's aggregate custody, sitting
    in the same omnibus account, the entire time)
6. Seller completes work off-platform; buyer verifies delivery
   (existing marketplace flow, unmodified)
7. Marketplace releases Escrow: ESCROW_RELEASE posting,
   contract ESCROW(BAIC_TEST) -> seller AGENT_AVAILABLE(BAIC_TEST)
   (existing marketplace ledger, unmodified)
8. Contract -> COMPLETED; marketplace emits CONTRACT_SETTLED.
   THIS IS THE CONTRACT-SETTLEMENT RECORD (§7) IN FULL. It requires
   nothing further from BitAI Payment, and nothing from BitAI Payment
   gates it — that coupling in the prior version of this design is
   removed as incorrect.

9. (Separately, later, decoupled in time and amount — the seller may have
   accumulated proceeds from several contracts) the seller requests a
   withdrawal. The marketplace, still the TENANT principal, calls:
     BitaiPaymentAdapter.submitTransfer(purpose=WITHDRAWAL)
       -> POST /v1/withdrawals, debiting the SAME omnibus account,
          paying out to the seller's own external address (an address
          the marketplace itself supplies and manages — outside this
          design's scope, matching how withdrawals already work for the
          existing Base Sepolia rail, Phase 18/ADR-0144).
   BitAI Payment executes §8's PSBT-based withdrawal flow, confirms
   on-chain, and issues a Custody Settlement Receipt
   (kind=WITHDRAWAL_SETTLED, accountRef=the omnibus account,
   externalReference="bitaicoin:<txid>:<vout>", chainAnchor={height, hash}).

10. Acceptance criteria — checked separately, on purpose:
    a. CONTRACT_SETTLED fired correctly, sourced entirely from the
       marketplace's own ledger (step 8) — an ordinary marketplace test,
       requiring no BitAI Payment interaction to verify.
    b. Independently, an offline verifier — no API call to BitAI Payment —
       checks the withdrawal's Custody Settlement Receipt: Ed25519
       signature valid, and chainAnchor.blockHash independently confirmed
       against the verifier's own bitaicoin-cli.
    c. The receipt's scopeNote (§7) is present and the test asserts the
       receipt is NOT treated anywhere in the test as proof that step 6-8
       occurred — only as proof that step 9's on-chain movement occurred.
```

---

## 15. BitAI Payment Direct HTTP 402 Protocol v1

Implements §13's "direct/non-marketplace use" note and gives §3's
sketched `/v1/authorizations`/`/v1/challenges` endpoints their first real
shape. The marketplace-mediated path (§14) is completely untouched by
this section — no marketplace code, type, or dependency is used or
introduced here, and none of §14's acceptance test changes. This proves
BitAI Payment is a genuinely reusable payment layer, not something
married to one marketplace, exactly as §13 already claimed on paper.

**Naming, deliberately not "L402":** this is **BitAI Pay-402**
(`protocol: "bitai-pay-402/1"` in every wire artifact below), a custodial
HTTP-402 payment protocol backed by BitAI Payment's ledger. It borrows
the general HTTP-402-plus-retry shape popularized by Lightning's L402,
but nothing else — no macaroons, no Lightning invoices, no channels in
v1. Calling it "L402" anywhere in code or docs would misrepresent both
the credential format (a signed JSON receipt, not a macaroon) and the
settlement mechanism (an instant custodial ledger transfer, not an HTLC).

### Roles

- **Payer** — an `INDIVIDUAL` principal (§4) with its own BitAI Payment
  account and its own funds (typically arrived via §8's ordinary deposit
  flow).
- **Payee** — a second, independent `INDIVIDUAL` principal, operating an
  arbitrary HTTP resource server that has nothing to do with the agent
  marketplace. Both are provisioned out-of-band exactly like every other
  principal (§4) — no new provisioning mechanism.
- **BitAI Payment** — moves funds between the two accounts and signs the
  proof. It never sees the resource itself, never brokers the HTTP
  conversation between payer and payee, and is not on the critical path
  for the payee deciding whether to serve the resource — that decision is
  the payee's alone, made from an offline signature check (see below).

### Flow

```
1. Payer -> Service:      GET /some/protected/resource            (no payment yet)
2. Service -> Payer:      402 Payment Required
                           { bitaiPay402Challenge: <Challenge, see below> }
3. Payer -> BitAI Payment: POST /v1/authorizations   (signed request, §4, as the payer)
                           -> { authorizationId, state: "CAPTURED", ledgerTransactionId,
                                proof: <SignedProof, see below> }
4. Payer -> Service:      GET /some/protected/resource
                           X-Bitai-Pay402-Proof: <base64(SignedProof JSON)>
5. Service:                verify the proof OFFLINE (no call back to BitAI Payment
                            required on this hot path — same philosophy as §7's
                            receipt verification), then serve the resource.
```

Step 3 is a single call: v1 auto-captures the full amount synchronously
(see "Why auto-capture" below), matching this feature's actual
requirement ("instant... the existing custodial ledger can make this
instant") rather than the two-step hold/capture originally sketched in
§3/§5 for a hypothetical metered-billing use case that isn't needed yet.

### Challenge (minted by the payee — not a BitAI Payment API call)

A Challenge is not a BitAI-Payment-persisted entity. The payee mints it
unilaterally, the moment it decides to price an unpaid request — no
round trip to BitAI Payment is needed to hand a price to a payer, and
this keeps BitAI Payment ignorant of the payee's resource catalog
entirely (consistent with §7's "BitAI Payment has no visibility into
individual [...] transactions" principle, extended here to direct use).

```ts
interface BitaiPay402Challenge {
  protocol: "bitai-pay-402/1";
  schemaVersion: 1;
  paymentReference: string;   // opaque, unguessable, minted fresh per challenge by the PAYEE
  resourceRef: string;        // the payee's own canonical id for the exact resource/request being priced
  payeePrincipalId: string;
  amountSatoshis: string;     // decimal string, integer satoshis — never a float (§6's discipline)
  asset: "BAIC_TEST" | "BAIC";
  expiresAt: string;          // ISO 8601 — after this, BitAI Payment refuses to authorize against it
  bitaiPaymentUrl: string;    // which BitAI Payment deployment governs this challenge
}
```

Returned as the JSON body of the `402` response
(`{ "bitaiPay402Challenge": { ... } }`). `paymentReference` must be
unique per payee for the life of that payee's principal — a UUID is
sufficient and is what the reference implementation uses.

### Authorize/capture request (payer -> BitAI Payment)

`POST /v1/authorizations`, signed as the payer (§4):

```ts
interface CreateAuthorizationRequest {
  idempotencyKey: string;      // ordinary retry-safety key (§9) — NOT the single-use guarantee, see below
  paymentReference: string;    // copied verbatim from the Challenge
  payeePrincipalId: string;    // copied verbatim from the Challenge
  resourceRef: string;         // copied verbatim from the Challenge — bound into the signed proof
  amountSatoshis: string;      // copied verbatim from the Challenge
  asset: "BAIC_TEST" | "BAIC";
  expiresAt: string;           // copied verbatim from the Challenge
}
```

### Verification and state-transition rules (enforced by BitAI Payment, in order)

1. `expiresAt` must be in the future at the time of the call, else `410`.
2. `payeePrincipalId !== payerPrincipalId` (the authenticated caller),
   else `422` — a principal cannot pay itself.
3. **Single-use is enforced by a real uniqueness constraint, not
   application logic alone**: `UNIQUE (payee_principal_id,
   payment_reference)` on the new `authorizations` table. This is the
   actual "challenge is single-use" guarantee — `idempotencyKey` above
   only protects a single caller's own retries (§9's ordinary
   insert-or-return-existing discipline), exactly as it does for
   deposits/withdrawals. The two keys serve different purposes and both
   exist for exactly that reason.
4. Resolution once a `(payee, paymentReference)` row already exists:
   - same `payerPrincipalId` as the stored row → return the existing,
     already-captured result again (idempotent — covers a lost response
     or a retried call with a different `idempotencyKey`).
   - different `payerPrincipalId` → `409 ALREADY_PAID_BY_OTHER`, no funds
     move. (Nothing stops a second principal from *attempting* to pay a
     reference it observed — the reference is not a secret — but only the
     first successful capture ever moves money, by construction of the
     unique constraint plus the transactional insert below.)
5. Payer's available balance is checked against `amountSatoshis`
   (mirroring `WithdrawalService.create`'s exact check) — insufficient
   balance is `422`, funds never move.
6. On success: one balanced ledger transaction posts `payer -amount` /
   `payee +amount` via `SettlementEngine.transferInternal` (see below),
   using a *deterministic* idempotency key derived from `(payee,
   paymentReference)` — not the caller's own `idempotencyKey` — so the
   underlying ledger post is naturally deduplicated even if two HTTP
   calls for the same reference race with different client-chosen keys.
   This is the same "derive a deterministic sub-key" discipline
   `WithdrawalService` already uses for its reservation id.
7. The `authorizations` row is inserted with `state = 'CAPTURED'` in the
   same step, `ON CONFLICT DO NOTHING` against either unique constraint —
   whichever caller's insert loses a race reads back the row that won
   and applies rule 4 above.

### Duplicate-payment and expiry behavior, stated precisely

| Situation | Result |
|---|---|
| Same payer retries (same or different `idempotencyKey`) after success | Same captured result returned again, no new ledger transaction |
| A different principal also tries to pay an already-captured reference | `409 ALREADY_PAID_BY_OTHER`, no funds move |
| `expiresAt` has passed and it was never captured | `410`, no funds move (checked live at call time — there is no separate background sweep in v1, matching deposit-intents' own lazy-expiry style in `pollOnce`) |
| Insufficient payer balance | `422`, no funds move, no row is created |
| Payee tries to pay its own challenge | `422`, no funds move |

There is deliberately **no proof-expiry window separate from the
challenge's `expiresAt`**: once captured, the transfer is final (custodial
ledger, no chargebacks) and the resulting proof remains presentable
indefinitely for its one intended redemption. This is a real scope
boundary, not an oversight — see "Who enforces single delivery" below.

### Signed proof (BitAI Payment -> payer, forwarded by payer -> payee)

```ts
interface BitaiPay402Proof {
  proofId: string;
  schemaVersion: 1;
  protocol: "bitai-pay-402/1";
  paymentReference: string;
  resourceRef: string;
  payerPrincipalId: string;
  payeePrincipalId: string;
  amountSatoshis: string;
  asset: "BAIC_TEST" | "BAIC";
  ledgerTransactionId: string;
  authorizationId: string;
  capturedAt: string;
  issuedAt: string;
  scopeNote: string; // literal field, same discipline as §7's CustodySettlementReceipt.scopeNote:
                      // "This proof attests that payment was captured from payerPrincipalId to
                      //  payeePrincipalId, bound to this exact paymentReference and resourceRef.
                      //  It authorizes exactly one resource grant for that reference; the payee
                      //  is solely responsible for rejecting any repeated presentation of it."
}
interface SignedBitaiPay402Proof {
  proof: BitaiPay402Proof;
  signature: string;      // Ed25519 hex over canonicalize(proof) — §4/§7's one shared primitive
  signingKeyId: string;   // the SAME key/id as Custody Settlement Receipts; not a second keypair
}
```

Deliberately **not** a `CustodySettlementReceipt` (§7) reusing that type:
the two attestations have opposite scoping goals on purpose.
`CustodySettlementReceipt.scopeNote` exists specifically to *deny*
correlation to any individual transaction. `BitaiPay402Proof` exists
specifically to *assert* correlation to one exact resource/reference —
that is its entire job. Conflating them would silently weaken one or the
other's honesty; keeping them as two distinct signed artifact types,
sharing only the Ed25519 key/signing infrastructure, is the correct
generalization of §7's "kept honestly separate" principle to a second
use case, not an exception to it.

### Payee verification rules (performed entirely by the payee, offline)

The payee never needs to call BitAI Payment on the hot path. In order:

1. Decode `X-Bitai-Pay402-Proof` and check `protocol === "bitai-pay-402/1"`, `schemaVersion === 1`.
2. Verify the Ed25519 signature over `canonicalize(proof)` against BitAI
   Payment's published public key for `signingKeyId` — the exact same
   offline check §7 already establishes for receipts, reused unchanged.
3. `proof.payeePrincipalId === (the payee's own principal id)`.
4. `proof.resourceRef === (the resourceRef the payee itself minted for
   this exact request)` — byte-for-byte. This is what makes "a receipt
   for one API call cannot authorize another": the binding is checked by
   the payee, using a value only the payee ever chose, never by BitAI
   Payment, which has no opinion on what a resourceRef means.
5. `proof.amountSatoshis` / `proof.asset` meet the payee's own price for
   `resourceRef`.
6. `proof.paymentReference` has not already been redeemed, per the
   **payee's own** local record.
7. Only if 1–6 all pass: record `paymentReference` as redeemed, then
   serve the resource.

An optional, non-hot-path secondary check — `GET
/v1/authorizations/:id` (authenticated as the payee) — exists for
reconciliation/audit, confirming BitAI Payment's own record agrees; nothing
above depends on it.

### Who enforces single delivery of the resource — a deliberate split

BitAI Payment's guarantee (enforced by the unique constraint in rule 3
above) is narrower than "the resource was only ever delivered once": it
is **"the funds for this reference were only ever captured once."** Rule
6 above — never re-serving a resource for an already-redeemed reference —
is the *payee's* responsibility, using its own local state, exactly the
same division of labor as any other payment system: a merchant, not the
payment processor, is responsible for not shipping the same paid order
twice. Centralizing rule 6 inside BitAI Payment (a `redeem` endpoint,
say) was considered and rejected for v1: it would make BitAI Payment
responsible for a fact about the payee's own application (has this
resource been delivered yet?) that it has no way to verify independently
and no legitimate need to know, worsening exactly the
correlation/scope-creep problem §7 was written to avoid.

### `SettlementEngine`, now minimally real

§11 sketched a `SettlementEngine` interface as a design seam but no
implementation of it exists anywhere in this codebase before this
change — `DepositService`/`WithdrawalService` both call `LedgerService`
directly. This feature introduces the first real, deliberately
minimally-scoped implementation:

```ts
interface SettlementEngine {
  transferInternal(request: InternalTransferRequest): Promise<SettlementResult>;
}
```

`CustodialLedgerEngine implements SettlementEngine` wraps
`LedgerService.post()` directly — v1's "instant" behavior needs no
reservation step at the ledger layer, unlike a withdrawal, because there
is no external (on-chain) uncertainty to reserve against; both legs of
the transfer are already-custodied balances moving atomically in one
database transaction. `AuthorizationService` depends on
`SettlementEngine`, never on `LedgerService` directly, so a future
`ChannelSettlementEngine implements SettlementEngine` — routing
`transferInternal` through a bilateral channel when one is open — can be
swapped in via dependency injection alone, with **zero changes** to
`AuthorizationService`, the HTTP routes, or the Challenge/Proof wire
formats. `DepositService`/`WithdrawalService` are deliberately **not**
retrofitted onto this interface in this change — doing so is unrequested,
broader-than-asked scope, and is left as clearly-identified future work,
not silently done or silently skipped.

### New data (migration `0006_authorizations.sql`)

```sql
CREATE TYPE authorization_state AS ENUM ('CAPTURED', 'VOIDED', 'EXPIRED');
-- No HELD state is persisted in v1 (see "auto-capture" above) — the
-- enum leaves room for a future version that needs a real hold window
-- (e.g. metered billing, capturing less than the held amount) to add one
-- without a breaking migration.

CREATE TABLE authorizations (
  id text PRIMARY KEY,
  idempotency_key text NOT NULL UNIQUE,
  payer_principal_id text NOT NULL REFERENCES principals(id),
  payee_principal_id text NOT NULL REFERENCES principals(id),
  payer_account_id text NOT NULL REFERENCES accounts(id),
  payee_account_id text NOT NULL REFERENCES accounts(id),
  payment_reference text NOT NULL,
  resource_ref text NOT NULL,
  amount_satoshis bigint NOT NULL CHECK (amount_satoshis > 0),
  currency settlement_currency NOT NULL,
  state authorization_state NOT NULL DEFAULT 'CAPTURED',
  capture_ledger_transaction_id text NOT NULL REFERENCES ledger_transactions(id),
  expires_at timestamptz NOT NULL,
  captured_at timestamptz NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE (payee_principal_id, payment_reference)
);
```

### What this does not do (explicitly out of scope for v1, matching the request)

- No bilateral channels, no HTLCs — `CustodialLedgerEngine` only.
- No BitAIcoin consensus changes, no wallet behavior changes — this
  entire feature lives inside BitAI Payment's own database and HTTP
  surface.
- No marketplace dependency anywhere in this section's code path.
- No self-service principal registration — the demo's two principals are
  provisioned via the existing operator CLI
  (`pnpm principals:register`), exactly like every other principal.
- No partial capture, no merchant-triggered capture-after-delivery — both
  remain possible future extensions of the same `authorizations` table
  and `SettlementEngine` seam, not designed further here.

### The acceptance test

Two independent `INDIVIDUAL` principals (a payer agent and a
service/payee), each with a real funded `BAIC_TEST` account (funded
through §8's ordinary deposit-intent flow against a fake chain — no live
BitAIcoin node is needed for this feature, since v1 never touches
BitAIcoin at all). Three real, independently-listening HTTP servers
(BitAI Payment itself, a demo protected-resource service, and a payer
client issuing real `fetch()` calls) proving the full flow above end to
end: unpaid request → real `402` → real signed authorize/capture call →
real Ed25519-verified proof → resource served on retry → replayed proof
on a second retry is rejected by the payee without needing to ask BitAI
Payment again → a second payer attempting to pay the same, already-
captured `paymentReference` is refused by BitAI Payment with no funds
moving.

---

## 16. BitAI Pay-402 Developer Preview

Goal: make §15's already-working protocol adoptable by a developer who
never saw this repo's internals — a hard gate distinct from "does the
protocol work," which §15 already answered. **BitAI Pay-402 v1's
architecture is treated as proven and frozen by this point**: this
section is SDK/ergonomics work on top of it, not a redesign, per
explicit instruction. Nothing here touches BitAIcoin, and no bilateral
channel work starts here.

**Compatibility baseline, preserved first.** Before any SDK code was
written, the v1 state was tagged `pay402-v1-baseline` in the
`bitai-payment` repo, and `test/pay402-compatibility-baseline.test.ts`
was added to pin the exact wire shapes (protocol/schemaVersion
constants, the proof header name, the literal `scopeNote` text, the
Challenge and Proof field sets, and — added in this same pass, see
below — the exact HTTP status per authorization error code). That test
is now part of `pnpm verify` and fails loudly if the wire protocol ever
drifts by accident.

### The two additive changes this work required

Both are backward-compatible extensions, not edits to the v1 baseline;
neither bumps `protocol` (that string versions only the Challenge/Proof
shapes, which are unchanged):

1. **`POST /v1/authorizations` failure responses gained a machine-
   readable `code` field** (`src/pay402/authorization-service.ts`'s new
   `AuthorizationError`/`AuthorizationErrorCode`, replacing the route's
   original message-string-matching). The v1 baseline's `ALREADY_PAID_BY_OTHER`
   case already put a code-like value in the `error` field
   inconsistently with every other case (which used a human sentence);
   this is now uniform. This was a genuine external-adoption problem —
   asking an SDK to regex-match English error text is not a contract
   anyone should build on — and the smallest fix available.
2. **A new, unauthenticated `GET /v1/signing-key` endpoint** was added
   (`src/http/app.ts`), returning `{signingKeyId, publicKeyHex}` for the
   one Ed25519 key that signs both Custody Settlement Receipts (§7) and
   BitAI Pay-402 proofs (§15). Before this, an external integrator had
   no way to obtain that key except being told it out-of-band by a
   BitAI Payment operator — a real blocker for "usable without
   understanding BitAI Payment internals." Purely additive: no existing
   route, field, or status code changed.

No other protocol change was made. The Challenge and Proof schemas,
every endpoint's success shape, the single-use `(payee,
paymentReference)` constraint, and the auto-capture behavior are all
exactly as §15 specified.

### What was built

- **`sdk/client`** — `Pay402Client`: recognizes a 402, validates the
  Challenge client-side before spending anything
  (`validateChallenge.ts`), authorizes/pays through BitAI Payment,
  verifies the returned proof's signature *and* that its fields actually
  match what was paid for, retries the original request, and throws a
  closed `Pay402ClientError` taxonomy
  (`INVALID_CHALLENGE`/`CHALLENGE_EXPIRED`/`SELF_PAYMENT_NOT_ALLOWED`/
  `PRINCIPAL_NOT_INDIVIDUAL`/`INSUFFICIENT_BALANCE`/
  `ALREADY_PAID_BY_OTHER`/`PROOF_VERIFICATION_FAILED`/
  `RESOURCE_ALREADY_REDEEMED`/`UNEXPECTED_RESPONSE`) for every failure
  mode the task specified, rather than an untyped error.
- **`sdk/server`** — `Pay402Guard`, framework-agnostic: mints a scoped
  Challenge, verifies a presented proof against BitAI Payment's
  published key entirely offline, and atomically redeems it via a
  pluggable `RedeemedReferenceStore` (`tryRedeem()` is one atomic
  check-and-set, not a racy has()/add() pair; ships an in-memory default,
  documented as needing Redis/a DB row for a real multi-instance
  deployment). `sdk/server/fastify-pay402.ts` is the one framework-
  specific file — a ~20-line `preHandler` adapter, since this repo
  already runs on Fastify; the guard itself has no framework dependency,
  documented explicitly so an Express/Koa integrator knows exactly what
  to port.
- **`sdk/reference-app`** — one protected endpoint (`service.ts`, built
  only on `sdk/server`), one payer script (`payer-agent.ts`, built only
  on `sdk/client`), and a standalone runnable demo (`run.ts`,
  `pnpm exec tsx sdk/reference-app/run.ts`) with no other running
  service required. Deliberately separate from `examples/pay402-demo`
  (§15's own reference, which talks to BitAI Payment's internal services
  directly and remains the frozen compatibility baseline's fixture,
  untouched by this work) — the two prove different things: that one
  proves the protocol is correct; this one proves the SDK is a faithful,
  ergonomic implementation of it.
- **`sdk/test/pay402-sdk.test.ts`** — the Developer Preview's own hard
  acceptance test, exercising every required failure mode (insufficient
  balance, expired challenge, malformed challenge, a second principal
  racing to pay an already-captured reference, a replayed proof against
  the payee, a proof that fails signature verification) against real,
  independently-listening HTTP servers, entirely through `sdk/client`
  and `sdk/reference-app` — no internal-service imports.
- **`sdk/README.md` + `sdk/docs/PROTOCOL.md`** — developer-facing
  documentation living *with the SDK*, not only in this design doc: the
  10-minute integration example, exact Challenge/Proof schemas, the
  trust and security/replay model restated for an outside reader, and an
  explicit, tabular comparison against Lightning L402 and `x402`
  (HTTP-USDC) so nobody conflates BitAI Pay-402's custodial,
  ledger-settled model with either.

### A documented, deliberate scope note on where docs live

This design doc (`BITAI_PAYMENT_DESIGN.md`) lives in the `bitaicoin-dev`
repo — a reasonable home for the internal architecture record, but not
one an external Pay-402 adopter has any reason to ever clone. That's
fine for *this* document's audience (a BitAI Payment maintainer) but
would be a real adoption failure for developer-facing docs, so those
were written fresh, self-contained, inside `bitai-payment/sdk/` instead
of by reference to this file. This doc and `sdk/docs/PROTOCOL.md`
deliberately overlap on the wire-format facts (Challenge/Proof shape,
error codes) — that overlap is intentional, not a maintenance debt to
resolve, since the two serve different, non-overlapping readers.

---

## What duplicates existing functionality (and should not be built twice)

1. **Authorization/capture for marketplace contracts** — fully provided
   by the marketplace's own Mandate → Escrow → Release/Refund pipeline.
   BitAI Payment's `/v1/authorizations` (implemented, §15, as "BitAI
   Pay-402" — deliberately not called "L402", see §15) is for direct,
   non-marketplace use only (§13) — never re-invoked per marketplace
   contract, and structurally cannot be under the omnibus model, since
   BitAI Payment has no notion of "a contract" to hold funds against.
2. **Idempotency infrastructure** — the marketplace's own idempotency-
   lease system (ADR-0093) is separate from BitAI Payment's; the
   marketplace's `PaymentAdapter` idempotency key *becomes* BitAI
   Payment's own idempotency key at the boundary, never a second,
   independently-generated one.
3. **Reconciliation** — BitAI Payment needs its own sweep (§3, §6's
   `reconciliation_findings`) reconciling its own ledger against
   BitAIcoin — but adopts the marketplace's proven discipline (read-only,
   stable codes, no repair function for authoritative money, ADR-0090)
   rather than inventing a different one.
4. **The double-entry ledger pattern** — reused as a pattern, for a
   separately-denominated (`BAIC`/`BAIC_TEST`) ledger, never commingled
   with USD accounts (ADR-0113's isolation, applied identically).
5. **Confirmation-depth/reorg evaluation** — `evm-finality.ts`'s code
   isn't reusable (EVM-specific revert field); its design is, in a new,
   small, BitAIcoin-specific function (§8).
6. **UTXO/key management** — BitAIcoin Core's own wallet already does
   this correctly. BitAI Payment orchestrates it via RPC (§8, verified
   against the actual fork) rather than reimplementing any of it.
7. **Agent identity / signature verification** — BitAI Payment's request
   authentication (§4) and receipt signing (§7) reuse one Ed25519
   primitive, the same one the marketplace already uses for agent auth
   (ADR-0011) — one crypto scheme for the whole system, including this
   new boundary, not a second one.
8. **Per-agent balance tracking for marketplace-originated funds** — this
   is now explicitly the marketplace's job alone (§2). BitAI Payment
   tracking it too was the double-ledger bug this revision fixes, not a
   feature to preserve in any form.

---

## What remains genuinely open (not decided here, on purpose)

- **Confirmation depth** — depends on the still-open BitAIcoin production
  security-model decision.
- **Facilitator/direct-L402 trust policy** — whether BitAI Payment ever
  trusts a third-party facilitator's payment claim (same open question
  the marketplace's own `x402-compatibility.md` recorded for the
  analogous case; same answer applies unchanged: a claim is a hint,
  never an instruction).
- **Multisig/semi-custodial upgrade path** — technically available
  (SegWit is active), not designed here, and not assumed by anything
  above.
- **Individual-principal (`INDIVIDUAL`) self-registration flow** — how a
  direct, non-marketplace user first obtains a provisioned principal and
  submits their public key. Deliberately out of scope for the
  marketplace-mediated acceptance test (§14), which only needs one
  `TENANT` principal.
- **Key rotation and per-principal rate limiting** — real operational
  needs, don't change any interface shape above, not designed here.
- **HD wallet vs. per-address `getnewaddress` calls** — an
  implementation detail of `CustodialLedgerEngine`, left open since it
  doesn't affect anything above the `SettlementEngine` line.
- **Migrating `DepositService`/`WithdrawalService` onto the
  `SettlementEngine` interface** — §15 introduces the first real
  implementation of that interface, scoped to `transferInternal` only;
  deposits and withdrawals still call `LedgerService` directly. Whether
  they should move onto the same seam is a real future question, not
  decided or assumed here.
- **Individual-principal self-service registration for direct HTTP-402
  use** — §15's demo provisions its two principals via the existing
  operator CLI, same as above; a real facilitator/marketplace-style
  onboarding flow for arbitrary third-party payers and payees is not
  designed here.
