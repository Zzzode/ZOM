# RFC 0013 Review And Implementation Tracker

This document records review and implementation evidence for RFC 0013. It does
not approve the proposal. Proposal frontmatter and the Decision Record remain
authoritative.

## Discussion Record

### 2026-07-11 RFC 0007 Integration Return

RFC 0007 review found that accepted RFC 0010 had no legal source-rejection seam
for ownership analysis and accepted RFC 0008 had no cross-module borrow-region
summary. RFC 0013 isolates those integration amendments so RFC 0007 can consume
stable contracts without mutating accepted RFCs silently.

### 2026-07-11 Exact-Hash Entry Review Return

Proposal
`1142a40a362ae2b1a1edec9739f27bf40fe5ce15a85b218029f410776c9b09a4`
and tracker
`d70c51aa6493b9dd461c01c3d258efe19482b26371c8728531381516c13d189b`
were returned by governance, semantic, and invariant review.

The returned draft silently amended accepted RFCs, did not add RFC 0013 to RFC
0007's dependency graph, collapsed multiple reference leaves into one input,
did not handle generic substitution, used a nonexistent checked-signature
revision, omitted the complete module-interface replacement codec, left
`ZOM4082` outside a legal source-failure result, and did not bind borrow
evidence into Built MIR.

The replacement draft is a hash-bound additive overlay. It adopts a
conservative direct-root relation, rejects unexpressible and unverified extern
contracts, defines the complete canonical RFC 0008 replacement, and binds verified
borrow evidence through the RFC 0010 frontend handoff.

### 2026-07-11 Root-Only Draft Review Return

Proposal
`d9b1e602e4cff8d09d90f6ca7da4b68543fdbd9eff37ee5cadb6a89650a97f51`
and tracker
`e2573ba2bea2a67512ded51ec8d38cfb24cb7df08bf82618d02ea8b09e37f10b`
received governance authorization for `DRAFT -> REVIEW`, but semantic and
invariant review returned the technical contract before transition.

The returned draft incorrectly sourced body-local closure summaries from
signature facts, stored imported revision numbers without a branded immutable
surface lookup, left `BorrowEvidenceRevision` without exact framing or an
oracle, did not commit borrow evidence into the MIR revision, and left source
failure anchor, sorting, and callable-local precedence incomplete.

The next draft rejects borrow-bearing body-local closures through the checked
body source result, embeds foreign verified surfaces in a session-owned branded
repository, defines the 173-byte evidence oracle, binds borrow evidence into the
single canonical MIR revision, and closes source-failure selection and ordering.

### 2026-07-11 Evidence-Lineage Draft Review Return

Proposal
`646bda084d037b5011212a091026bb71a831d32077e82d09c158a4438fe0c0fe`
and tracker
`0ed00c864c4ca8a450eeaaedc83ea8fa074ec4bafffc21fd4394d323aac95380`
received semantic approval. Invariant review returned transitive re-export
origin selection and source-versus-invariant phase precedence. Governance
returned the incomplete RFC 0010 MIR replacement enumeration, missing RFC
0005 tracker transaction, and stale owner/path routing.

The next draft requires byte-identical summaries across every authorized
import/re-export surface and selects a canonical proof source, validates all
identity/revision/codec/input invariants before source classification, lists
every canonical MIR ownership clause, and records implementation evidence in
RFC 0005, RFC 0008, and RFC 0010 trackers.

### 2026-07-11 Formal Owner Approval

All ten required owners approved proposal
`25b8f6fc4c127c996904edf2cdd353e1712cf3983972beda76746d83ceea3b5f`
and tracker
`81e5a32662974487898103116dc648843484282cae4ac8f5a6b12fff2e725f59`.

- `task-router` and `rfc` approved routing, dependencies, overlay governance,
  the RFC 0007 gate, and the acceptance transaction.
- `binder-checker`, `module-system`, `concurrency`, and `runtime-memory`
  approved root-shape semantics, transitive re-export proof selection,
  concurrency non-implication, and storage/extern safety.
- `error-system`, `ir-backend`, `spec-audit`, and `verification` approved the
  diagnostic paths, staged precedence, MIR revision lineage, cross-RFC
  consistency, nine byte oracles, and negative/determinism gates.

No owner recorded a blocking or non-blocking objection.

## Owner Review Checklist

| Owner | Review State | Blocking Surface |
|---|---|---|
| `task-router` | Approved | Amendment routing and RFC 0007 dependency escalation |
| `rfc` | Approved | Governance, accepted-RFC amendment, dependencies, and transition |
| `binder-checker` | Approved | Total root-shape classification, elision, signature facts, and body-local closure rejection |
| `module-system` | Approved | Borrow surface, module interface canonical, transitive re-export proof selection, and cross-module consumption |
| `error-system` | Approved | `ZOM4082-ZOM4085`, checked-body and interface source paths, invariants, and staged precedence |
| `concurrency` | Approved | Proof that task and suspension semantics are not implied |
| `ir-backend` | Approved | Ownership result seam, canonical MIR revision and oracles, Built MIR artifact legality, and successor suppression |
| `runtime-memory` | Approved | Reference/storage lifetime safety and Chapter 14 boundary |
| `spec-audit` | Approved | Type, module, memory, and compiler-contract alignment |
| `verification` | Approved | Nine codec oracles, staged negative matrices, determinism, and architecture gates |

The table summarizes the exact-hash owner approvals recorded above. Every owner
approved the same proposal and tracker snapshot; no non-blocking objection was
recorded.

## Decision Record

Decision: ACCEPTED on 2026-07-11.

Final accepted proposal SHA-256:
`e3909d5caad48a1c0255ee57d2a8fcc327e046945f20a586e0c6bf0115a237c3`.

All ten required owners approved the exact REVIEW proposal
`25b8f6fc4c127c996904edf2cdd353e1712cf3983972beda76746d83ceea3b5f`
and tracker
`81e5a32662974487898103116dc648843484282cae4ac8f5a6b12fff2e725f59`
with no objections. Governance authorized the mechanical transition from the
approval-recorded proposal
`76a5d4af43869a0f1f5e493eb87795f2ffe05a258462223920d248b3d77b888f`
and tracker
`e16f87e10be7836cf10c4ebe7896d2d99d6ee9fb2005baf5d09700194bac59a0`.

RFC 0013 was accepted by this decision and entered `IMPLEMENTING` through the
direct replacement series below on 2026-07-17. RFC 0007 remains independently
governed and requires its own accepted transition before ownership analysis may
publish production facts.

## Implementation Tracker

The direct replacement series started on 2026-07-17. It contains no parallel
interface decoder, AST ownership fallback, unbranded evidence lookup, optional
lineage, or parallel MIR revision.

| Slice | State | Required evidence |
|---|---|---|
| Borrow signature surface | Implemented | Closed direct-root summaries, source rejection, canonical codec, verified module-interface publication, and focused sanitizer tests |
| BorrowEvidence authority | Implemented | `borrow-evidence-test` passes complete local/imported reconstruction, the exact 173-byte empty oracle, independent verification, branded repository leases, and deterministic missing/additional/duplicate/order/codec/stale/swap mutations |
| Checked-module and HIR lineage | Implemented | `hir-module-test` and `compiler-session-package-test` pass exact retained checked and borrow leases, repository re-resolution, deterministic lineage dumps, and all-or-nothing session publication |
| Built MIR revision | Implemented | `built-mir-test` passes the exact non-empty `9f8de0ad...ad7985` and empty `b9a8988d...38cbc9` oracles; direct HIR lowering, the independent verifier, atomic session adoption, and all 21 IR architecture mutations pass |
| Ownership result seam | Pending | Closed source, identity, capability, and IR-invariant branches at OwnershipProofValidation with successor suppression |
| Production cutover | Pending | RFC 0007 implementation prerequisites, full sanitizer/default CTest, determinism, spec alignment, architecture, format, and diff-hygiene evidence |

The series may publish no production ownership result until the RFC 0007
proposal independently reaches `IMPLEMENTING`. RFC 0013 implementation may
establish only the accepted evidence and transport prerequisites before that
gate.
# RFC 0015 Accepted Overlay

RFC 0015 was approved at exact review SHA-256
`642836225d54f6fa28f8c27e9985972081dbd221c2e8f3e61a0aafd04fe9bb1e`.
Its accepted-file SHA-256 is
`9704d5651606e8a74034c8af4be5172b4007a6c9f0ee8ea2f5ee183223401c01`.
The overlay directly replaces the RFC 0013 marker-proof and
body-checking-input lineage contracts named by RFC 0015.

## RFC 0025 Acceptance Synchronization

### Decision Record Synchronization

On 2026-07-25, RFC 0025 received all 12 required-owner approvals at exact
proposal SHA-256
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`.
Its `R25-02` acceptance transaction changes imported borrow input to
exhaustive `VerifiedInterfaceSource`, preserves callable user borrow surfaces,
and permits a toolchain-core source only through the closed no-callable branch
with no synthetic surface. RFC 0013 remains `IMPLEMENTING`, and RFC 0007's
independent production-publication gate remains unchanged.

### Implementation And Evidence Binding

| RFC 0025 Task | RFC 0013 Evidence Responsibility |
|---|---|
| `R25-09A` | Publish the flat finalized core interface consumed by borrow reconstruction. |
| `R25-10` | Implement callable-driven evidence, no-callable core validation, and bootstrap-data exclusion. |
| `R25-09B` | Carry completed borrow and visible-interface lineage through checked-module, HIR, and MIR. |
| `R25-08T` | Prove user and core alternatives, wrong revisions, synthetic surfaces, callable injection, and exact failures. |
| `R25-14` | Register architecture gates for the closed core branch and ownership boundary. |
| `R25-15` | Supply clean-build, native-suite, ownership, architecture, and final-owner evidence. |

Only the RFC 0025 tracker may advance these implementation states. This
tracker records no new ownership-result publication evidence.
