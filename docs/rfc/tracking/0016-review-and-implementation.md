# RFC 0016 Review And Implementation Tracker

This document records review and implementation evidence for RFC 0016. It does
not approve the proposal. RFC frontmatter remains authoritative for status and
approvers.

## Current Snapshot

| Field | Value |
|---|---|
| Status | `REVIEW` |
| Proposal SHA-256 | `efe800c6c2aaeda60d6beedde45adaa459536002bf3ccf5704135411e02d808c` |
| Review manager | `rfc` |
| Decision | `TBD` |
| Implementation | Blocked until `ACCEPTED` |

The previous draft baseline was
`a9d1995e60ac443e303ed01ac3f4bbe76a1e954feee1a9b0c192ba0fe1db314a`.
It did not define the final code-generation authority required by the
downstream LIR design, and its tracker recorded a stale current hash. No
approval from that snapshot carries forward.

## Discussion Record

### 2026-07-23 Final Code-Generation Authority Repair

Three independent pre-edit reviews found the same blocking contract:

- a public LIR/backend consumer could accept
  `VerifiedFinalTargetSelection`, while the downstream LIR design required a
  wrapper-owned operation;
- importing the downstream `LirAlgebraRegistry` and translator contract into
  RFC 0016 would create a circular proposal dependency;
- a runtime ABI manifest containing a semantic-context brand and physical
  function ABI keys could not exist before the final context and LIR ABI store;
  and
- a caller-provided monomorphization plan could not already bind target and
  runtime facts selected only inside the later target-dependent operation.

The repaired draft ends RFC 0016 at an acyclic authority boundary:

1. One process-root transaction publishes a complete
   `VerifiedTargetAuthorityBundle`.
2. The bundle contains the target registry, runtime capability snapshot,
   code-generation capability registry, and target-independent runtime ABI
   contract registry.
3. The exact bundle moves through the RFC 0012 preparation and final wrappers.
4. Final target verification is private.
5. The first target operation consumes the complete final wrapper and obtains
   one private operation state containing RFC 0012 cleanup ownership plus a
   move-only `VerifiedFinalCodegenAuthority`.
6. The downstream LIR RFC owns `lowerToLir`, the LIR algebra, LLVM translator
   contract, ABI-classifier registry, physical runtime ABI manifest, and
   monomorphization sequencing.
7. Later backend operations consume authority-carrying verified typestates;
   they do not return to the destroyed wrapper or reissue authority.

The draft also removes the controlled Linux/KVM coverage execution model. That
model was unrelated to target authority, occupied most of the proposal, and
was the source of repeated review returns. RFC 0016 now uses the repository's
native sanitizer, unit, integration, CTest, architecture, format, and CI gates.

### Review Transition Evidence

Three independent post-edit audits approved exact DRAFT SHA-256
`0f2cedcad9b06b6190657339d11eb2db61e453e2bf20d0252a3a36fd84166062`
without editing the proposal:

- backend contract: approved capability algebra, issuance order, ABI
  classifier ownership, and authority lineage;
- runtime authority: approved canonical capability predicates, logical runtime
  ABI lowering, callback ABI, bundle ownership, and physical-manifest handoff;
  and
- session governance: approved whole-wrapper consumption, one-shot issuance,
  RFC 0012 cleanup precedence, and closed bundle construction.

`python3 scripts/check-rfc.py`, `python3 scripts/check-format.py`, and
`git diff --check` passed before the status-only transition. The transition
changed only frontmatter, status history, index, and tracker state. The current
`REVIEW` snapshot then required every owner below to approve exact SHA-256
`fe1f2937b9426c0b0fe4729af50dc39930355d7fe7836de8b43c7501a3f4f59c`
before the RFC could move to `ACCEPTED`.

### 2026-08-11 Unversioned Contract Snapshot Rebinding

The repository-wide internal-contract replacement changed normative target,
runtime, and codec names after the prior REVIEW snapshot was recorded. No
required owner had approved that former snapshot. The current REVIEW snapshot
is SHA-256 `efe800c6c2aaeda60d6beedde45adaa459536002bf3ccf5704135411e02d808c`.
Every required owner must review this exact snapshot; no earlier readiness or
review evidence carries forward.

## Required-Owner Review

| Owner | State | Required focus |
|---|---|---|
| `task-router` | Pending exact REVIEW snapshot | Single-owner path census and routing consistency |
| `rfc` | Pending exact REVIEW snapshot | RFC structure, dependency direction, status governance |
| `module-system` | Pending exact REVIEW snapshot | Package wrapper ownership, phase authority, cleanup |
| `error-system` | Pending exact REVIEW snapshot | Closed failure mapping and diagnostic ownership |
| `ir-backend` | Pending exact REVIEW snapshot | LLVM admission, code-generation capability registry, downstream handoff |
| `runtime-memory` | Pending exact REVIEW snapshot | Runtime capability and ABI contract registries |
| `spec-audit` | Pending exact REVIEW snapshot | Architecture documentation and cross-RFC consistency |
| `verification` | Pending exact REVIEW snapshot | Native tests, architecture gates, LLVM CI matrix |

Readiness-audit approvals are not required-owner approvals.

## Decision Record

`TBD`

## Implementation Tracker

| Slice | State | Required evidence |
|---|---|---|
| LLVM build and CI contract | Blocked by acceptance | Exact LLVM 22.1.8 provenance, components, X86/AArch64 inventory, positive and negative configure tests |
| Runtime capability and ABI contract registries | Blocked by acceptance | Generated closed records, deterministic revisions, independent encoders, no ambient query |
| Target and code-generation capability registries | Blocked by prior slice | LLVM-backed admission, exact target associations, bundle publication |
| Package-session authority bundle | Blocked by prior slice | Exact RFC 0012 handoffs, move-only ownership, cleanup precedence |
| Final code-generation issuance | Blocked by prior slice | Private target selection, one-shot state, cross-wrapper/reuse rejection |
| Consumer cutover | Blocked by prior slice | Consuming first wrapper operation, authority-carrying later typestates, deleted public final-target consumer path |
| Repository verification | Pending | Sanitizer build, full CTest, architecture, RFC, format, determinism, and diff gates |

Implementation states may change only after the RFC decision permits work and
the named evidence is attached.

## Technical Closure Audit (2026-07-24)

Independent repository inspection confirmed the proposal's live dependencies:

- `CanonicalQueryKey` and the RFC 0017 snapshot/cancellation/provenance
  foundations exist in `products/zomlang/compiler/query/query-database.cc`
  and `query-types.h`.
- The `VerifiedTargetAuthorityBundle`, `VerifiedFinalCodegenAuthority`,
  code-generation capability registry, and target-independent runtime ABI
  contract registry are new types defined by this RFC; they do not require
  pre-existing production code.
- No diagnostic code conflicts are introduced; RFC 0016 does not register
  new `ZOMxxxx` codes.
- The dependency direction is acyclic: RFC 0016 imports only RFC 0012
  preparation/final wrappers and RFC 0016's own target-authority bundle;
  the downstream LIR design (RFC 0021) consumes RFC 0016's output, not
  the reverse.
- `python3 scripts/check-rfc.py` passed for all 23 proposal RFCs.

No blocking technical gaps found. Remaining work is required-owner approval
of the former REVIEW SHA-256
`fe1f2937b9426c0b0fe4729af50dc39930355d7fe7836de8b43c7501a3f4f59c`.
The current REVIEW snapshot requires a fresh audit and owner approval.

### 2026-08-12 Current Snapshot Readiness Audit

The current proposal bytes reproduce SHA-256
`efe800c6c2aaeda60d6beedde45adaa459536002bf3ccf5704135411e02d808c`.
Every required-owner row remains pending for that exact snapshot, so this audit
does not record an approval, decision, status transition, or implementation
authorization.

The current target-registry unit suite passes its canonical target, Mach-O,
revision-bound selection, unavailable capability, foreign revision, and
projection-mismatch cases under the sanitizer build. It validates the existing
RFC 0010 registry surface only; it does not provide the context-bound bundle or
final code-generation authority defined by this RFC.

The repository currently has no LLVM package discovery or component-linking
contract. The local development environment resolves `llvm-config` and `clang`
at version `19.1.5`, while this RFC requires the exact `22.1.8` package,
explicit CMake provenance, and the `X86` and `AArch64` component inventory.
That mismatch is expected before implementation and must be corrected through
the accepted cross-platform CMake and CI transaction, not by admitting an
ambient local LLVM installation.
