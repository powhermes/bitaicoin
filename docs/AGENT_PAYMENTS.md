# Agent Payments (Future Architecture — Not Implemented)

**Nothing in this document is implemented in Phase 1.** BitAIcoin Phase 1
delivers a working, tested, replay-protected private chain and nothing
more. This document exists solely to record the long-term motivation for
the project (a Bitcoin-derived settlement rail suited to autonomous
AI-agent commerce) so that later phases inherit a considered design intent
rather than reverse-engineering one from scratch. Everything here is a
proposal to validate and refine in a later phase, not a spec to build
against blindly.

## Motivation

L402 (formerly LSAT) is an existing pattern — built on HTTP 402 Payment
Required, a Lightning invoice, and a bearer macaroon — for machine-to-
machine API monetization: a client (often an autonomous agent, not a
human) requests a resource, receives a 402 with a payment request, pays,
and retries the request with a proof-of-payment token that authorizes
further access. The appeal for an AI-agent context is that the entire
negotiate-pay-retry cycle can happen without a human in the loop, with a
receipt that's independently verifiable rather than trust-based.

BitAIcoin's long-term goal is to provide a base settlement layer suited to
this pattern for agent-to-agent and agent-to-service commerce: real
consensus-level finality and scarcity (inherited from Bitcoin's proven
model) with a chain identity, tooling, and roadmap free to evolve
specifically toward this use case, unconstrained by needing to stay
compatible with Bitcoin mainnet itself.

## Why nothing is implemented yet

L402-style payments in their common form assume a **fast, cheap settlement
or channel layer** (Lightning, in the original design) sitting on top of
on-chain BAIC, not on-chain transactions per API call — on-chain
settlement per request would be both slow (BitAIcoin currently inherits
Bitcoin's ~10-minute target block spacing) and needlessly heavyweight for
machine-scale request volume. Building that layer correctly — whether an
actual payment-channel network, a custodial/semi-custodial settlement
service, or something else — is substantial, security-critical work that
deserves its own dedicated phase, its own threat model, and its own
explicit user sign-off, not an afterthought bolted onto a Phase 1 chain
fork.

Additionally, per `docs/CONSENSUS.md`'s open question, BitAIcoin's native
SegWit/Taproot activation timing is currently unresolved — most realistic
payment-channel designs (Lightning-style HTLCs, PTLCs) depend on features
gated behind those same soft forks. That question needs an explicit answer
before channel-layer design can start in earnest.

## Candidate directions for a future phase (unevaluated, listed for context only)

- **Payment channels**: a BitAIcoin-native adaptation of BOLT-style
  payment channels, once SegWit/Taproot activation timing is decided.
- **Macaroon-based bearer tokens**: reusing the L402 macaroon pattern
  as-is (it's payment-rail-agnostic), scoped to a specific BAIC invoice
  or channel payment rather than a Lightning one.
- **Agent-specific wallet/RPC ergonomics**: a minimal RPC or SDK surface
  designed for autonomous callers (deterministic fee estimation, invoice
  request/response schemas, machine-readable error semantics) rather than
  adapting Bitcoin Core's human-oriented wallet RPCs as-is.
- **Settlement batching**: since agent-to-agent request volume is likely
  to be high-frequency/low-value, some form of batched or netted
  settlement (channel-based or otherwise) is likely necessary regardless
  of which channel design is chosen.

None of these have been designed, prototyped, or committed to. They are
recorded here only so that a future phase starts from "here is what we
were thinking and why" instead of nothing.

## What would need to happen before any of this is built

1. Resolve the open SegWit/Taproot activation-timing question in
   `docs/CONSENSUS.md`.
2. Explicit scoping and sign-off from the user on which settlement
   mechanism to pursue, since this is security-critical, user-facing
   financial infrastructure — not a decision to make unilaterally.
3. A dedicated design/plan phase (Goal/Findings/Plan, same as Phase 1),
   informed by whatever real usage patterns or requirements emerge from
   actually running the Phase 1 Synthetic Lab network.
