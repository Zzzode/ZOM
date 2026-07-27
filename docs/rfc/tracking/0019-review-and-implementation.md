# RFC 0019 Review And Implementation Tracker

## Discussion Record

### 2026-07-19 Design Intake

The Binder requires a stable owner for executable module items and definition
bodies. The accepted design introduced `StableBodyOwnerKey`, owner-local
identity, deterministic syntax paths, stable item boundaries, and one body
query family.

### 2026-07-27 RFC 0027 Acceptance Synchronization

RFC 0027 was approved by all nine required owners on exact proposal SHA-256
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`.
Acceptance transaction `rfc0027-accept-20260727-e2f4ba5e` synchronizes RFC
0019's current owner, scope, fact, header, result, allocation, complete-read,
diagnostic, materialized-provenance, and Checker contracts. No earlier
approval is treated as implementation evidence for this synchronized work.

The transaction preserves RFC 0019's `IMPLEMENTING` status. Completed phases
below remain complete only for their recorded native evidence. Remaining work
is governed by the RFC 0027 implementation graph.

### 2026-07-27 RFC 0028 Acceptance Synchronization

Acceptance transaction `rfc0028-accept-20260727-944b68ff` binds RFC 0019 to
RFC 0028 proposal SHA-256
`944b68ffc0aff5576d079a243ff092d7d19fba5ffed65551dda8e68adf230db4`.
The synchronized contract removes the duplicate closure projection, makes
`BoundOwnerBody` the sole stable closure-fact authority, and routes final
Binder capabilities through sealed admission, descriptor-specific failure
alternatives, exact active membership, and membership-before-interner
ordering. RFC 0019 remains `IMPLEMENTING`.

### 2026-07-27 RFC 0029 Acceptance Synchronization

Acceptance transaction `rfc0029-accept-20260727-8d393a0c` binds RFC 0019 to
exact RFC 0029 proposal SHA-256
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`.
The synchronized authority fixes the request-result publication seam,
stable-identity admission prerequisite, exact read order and legal failure
subset of the five Binder provenance capabilities, semantic syntax failure
mapping, and direct typed-child owner-body reconstruction.

RFC 0027 `S1`, `S2`, and `S3` land atomically through the exact RFC 0030
`R29-12AB` transaction; `S6` follows as the separate `R29-12D` diagnostic
commit. RFC 0029 `R29-13A` through `R29-13C` prepare the runtime, provenance,
descriptor, and verification partitions, and `R29-14` is their sole runtime
landing transaction. RFC 0019 remains `IMPLEMENTING`.

## Owner Review Matrix

| Owner | Required review | Status | Evidence |
|---|---|---|---|
| `rfc` | metadata, current contract, status, synchronization | Approved | RFC 0019 accepted snapshot and RFC 0027 exact-hash approval. |
| `binder-checker` | stable facts, headers, allocation, materializers, Checker handoff | Approved | RFC 0019 accepted snapshot and RFC 0027 exact-hash approval. |
| `module-system` | contextual keys, read sets, membership, graph lineage | Approved | RFC 0019 accepted snapshot and RFC 0027 exact-hash approval. |
| `error-system` | result algebra, diagnostics, provenance, bijection | Approved | RFC 0019 accepted snapshot and RFC 0027 exact-hash approval. |
| `spec-audit` | current architecture and synchronized RFC consistency | Approved | RFC 0019 accepted snapshot and RFC 0027 exact-hash approval. |
| `verification` | native gates, mutations, coverage, benchmarks | Approved | RFC 0019 accepted snapshot and RFC 0027 exact-hash approval. |

## Decision Record

RFC 0019 was accepted on 2026-07-19 after all six required owners approved
proposal snapshot
`ba4d5fdf7e5a68c8895628299292e67d31df5b59398387bbe3be20a7c8e899b0`.

On 2026-07-27, acceptance transaction
`rfc0027-accept-20260727-e2f4ba5e` replaced the current RFC 0019 contract with
the synchronized RFC 0027 design bound to proposal SHA-256
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`.
The transaction is a design synchronization. RFC 0019 remains
`IMPLEMENTING`, and implementation status changes only after the RFC 0027
native evidence and final audit complete.

RFC 0028 acceptance transaction `rfc0028-accept-20260727-944b68ff` does not
change RFC 0019's status. Query runtime, descriptor, transaction,
sealed-snapshot, failure-bridge, and provenance work follows the dependency
authority recorded below.

Transaction `rfc0030-accept-20260728-4ed0e6b8` is the dependency authority for
that source work. Schema, facts, and codecs complete through the exact
`R29-12AB` transaction and diagnostics through `R29-12D`, the reviewed source
partitions through `R29-13A` to `R29-13C`, and the atomic runtime source
transaction through `R29-14`. Completion and documentation authority remains
`R29-15` through `R29-17`.

## Implementation Tracker

| Phase | Scope | Status | Evidence And RFC 0027 Authority |
|---|---|---|---|
| 0 | RFC 0029 synchronized prerequisites and runtime replacement | Pending | RFC 0029 `R29-12A` through `R29-17`; no synchronized implementation evidence is recorded |
| 1 | Stable body owner and owner-local codec | Complete | Native codec vectors, bounds, owner alternatives, and owner-local identity tests passed. Synchronized schema follow-up is owned by RFC 0027 `S1` through `S3`. |
| 2 | Module body syntax and provenance | Complete | Selected-source, detached syntax, provenance, dependency, source-switch, session, and architecture tests passed. Synchronized header work remains under RFC 0027 `S4` through `S5`; the current query-runtime and transaction follow-up is RFC 0029 `R29-12A` through `R29-17`. |
| 3 | Owner-body query catalog and aggregate verification | In progress | Syntax, provenance, and owner census are implemented. Stable Binder queries, allocation, materializers, aggregate verification, and production demand complete through RFC 0027 `B1` through `B4` and `M2` through `M5`, after RFC 0029 `R29-14` and `R29-15` provide the atomic query-runtime, provenance, and verification prerequisites. |
| 4 | Scope, closure, control, diagnostic, and Checker migration | In progress | Owner-local capture and control validation are implemented. Stable scope publication, RFC 0017 diagnostic integration, contextual materialization, and Checker handoff complete through RFC 0027 `S6`, `C1`, and `C1A`. |
| 5 | Independent schema mutations and native regressions | In progress | Existing owner, provenance, source, registration, verifier-separation, worker-order, and range-shielding adversaries pass. Complete synchronized mutations are owned by RFC 0027 `E1` through `E7`. |
| 6 | Full sanitizer, differential, architecture, coverage, format, and benchmark gates | Pending | RFC 0027 `E7`, `E8`, and final write-free verification. |
| 7 | Current architecture documentation and landing evidence | Pending | RFC 0027 `A2`, `A3`, and `A1`. |

Implementation is authorized by the accepted RFCs. Completion claims require
the dependency-ordered RFC 0027 tasks and project-native evidence.

## Acceptance Transaction Record

| Field | Value |
|---|---|
| Transaction | `rfc0027-accept-20260727-e2f4ba5e` |
| Proposal SHA-256 | `e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435` |
| RFC 0019 status before transaction | `IMPLEMENTING` |
| RFC 0019 status after transaction | `IMPLEMENTING` |
| Synchronized authority | RFC 0027 current owner, scope, fact, header, result, allocation, read-set, diagnostic, materialization, and Checker contract |
| Implementation authority | RFC 0027 tracker after the immutable implementation-series base is recorded |

### RFC 0028 Synchronization Record

| Field | Value |
|---|---|
| Transaction | `rfc0028-accept-20260727-944b68ff` |
| Proposal SHA-256 | `944b68ffc0aff5576d079a243ff092d7d19fba5ffed65551dda8e68adf230db4` |
| RFC 0019 status before transaction | `IMPLEMENTING` |
| RFC 0019 status after transaction | `IMPLEMENTING` |
| Removed authority | duplicate closure projection catalog, read-set, schema, and plan surface |
| Implementation authority | RFC 0029 synchronization record below |

### RFC 0029 Synchronization Record

| Field | Value |
|---|---|
| Transaction | `rfc0029-accept-20260727-8d393a0c` |
| Proposal SHA-256 | `8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7` |
| RFC 0019 status before transaction | `IMPLEMENTING` |
| RFC 0019 status after transaction | `IMPLEMENTING` |
| Synchronized authority | Closed capability publication, stable-admission prerequisite, exact five-descriptor reads and failures, semantic syntax invariant mapping, and direct owner-body reconstruction |
| Current implementation authority | RFC 0029 `R29-12A` through `R29-17` plus the dependent RFC 0027 Binder and Checker tasks |

## Required Review Commands

- `python3 scripts/check-rfc.py`
- `python3 scripts/check-no-internal-versioning.py --check`
- `python3 scripts/check-format.py`
- `git diff --check`
