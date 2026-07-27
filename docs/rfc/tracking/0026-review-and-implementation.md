# RFC 0026 Review And Implementation Tracker

## Discussion Record

### 2026-07-26 Design Intake

Implementation review identified missing stable graph value, codec, read-set,
independent verifier, structural transaction, SCC, and final publication
contracts. RFC 0026 entered required-owner review to close that boundary.

### Review Record

All four required owners approved proposal SHA-256
`39df5d3f11dbdcb2e95056b1cd14fd5220a19688f31a3e3180230ad465a3f84d`.
The accepted contract authorized dependency-ordered stable transaction, query,
graph, SCC, session-barrier, and verification work.

### 2026-07-27 RFC 0027 Acceptance Synchronization

All nine RFC 0027 required owners approved exact proposal SHA-256
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`.
Acceptance transaction `rfc0027-accept-20260727-e2f4ba5e` synchronizes RFC
0026 to the typed `MaterializedModuleGraphWitness`, complete-root
`MaterializeModuleGraph`, tracked active-membership expansion, final-snapshot
seal, retained capability lifetime, and module-system publication contract.

The synchronization does not change RFC 0026's `ACCEPTED` status and does not
promote pending implementation or verification work.

## Owner Review Matrix

| Owner | Required review | Status | Evidence |
|---|---|---|---|
| `rfc` | metadata, current contract, synchronization, status | Approved | RFC 0026 accepted snapshot and RFC 0027 exact-hash approval. |
| `module-system` | inputs, graph, SCC, materializer, membership, session seal | Approved | RFC 0026 accepted snapshot and RFC 0027 exact-hash approval. |
| `binder-checker` | stable graph consumers, bound-module lineage, lease handoff | Approved | RFC 0026 accepted snapshot and RFC 0027 exact-hash approval. |
| `verification` | codecs, mutations, native gates, coverage, benchmarks | Approved | RFC 0026 accepted snapshot and RFC 0027 exact-hash approval. |

## Decision Record

RFC 0026 is `ACCEPTED`.

The original accepted proposal SHA-256 is
`39df5d3f11dbdcb2e95056b1cd14fd5220a19688f31a3e3180230ad465a3f84d`.

On 2026-07-27, transaction `rfc0027-accept-20260727-e2f4ba5e`
synchronized the current RFC 0026 contract to RFC 0027 proposal SHA-256
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`.
The synchronized design is accepted, while product implementation and final
verification retain the statuses below.

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R26-01` | `rfc` | None | Complete RFC 0026 draft and RFC index row. | `python3 scripts/check-rfc.py` | Complete |
| `R26-02` | `module-system` | `R26-01` | Review stable records, transaction, query read sets, SCC algorithms, and barriers. | Recorded review candidate | Complete |
| `R26-02B` | `binder-checker` | `R26-01` | Review graph consumers, provenance, stable projection, and verification boundary. | Recorded review candidate | Complete |
| `R26-03` | `verification` | `R26-01` | Review mutation matrix, native gates, and benchmark contract. | Recorded review candidate | Complete |
| `R26-04` | `rfc` | `R26-02`; `R26-02B`; `R26-03` | Record the original decision and synchronize its accepted RFC set. | RFC and repository text gates | Complete |
| `R26-05` | `module-system` | `R26-04` | Implement stable codecs and the atomic module-structure transaction. | Focused native unit tests | Complete |
| `R26-06` | `module-system` | `R26-05` | Implement selected-source, active-module, dependency-site, request, and dependency queries. | Focused native unit tests and architecture gates | Complete |
| `R26-07` | `module-system` | `R26-06` | Implement stable `ModuleGraph`, `ModuleGraphScc`, cycle verification, and stable session barriers. | Graph, SCC, session, and architecture tests | Complete |
| `R26-10` | `rfc` | `R26-07` | Synchronize the current RFC 0026 design through RFC 0027 transaction `rfc0027-accept-20260727-e2f4ba5e`. | `python3 scripts/check-rfc.py` | Complete |
| `R26-11` | `module-system` | `R26-10` | Implement complete context authority, active memberships, typed materialized witness, final-snapshot `MaterializeModuleGraph`, session seal, and retained capability lifetime. | RFC 0027 `Q3`, `Q4`, `I1`, `I2`, `M1`, `T1`, `T2A`, `T2B`, and `T2C` evidence | Pending |
| `R26-12` | `binder-checker` | `R26-11` | Migrate bound-module graph consumers and downstream lease lineage. | RFC 0027 `M2` through `M5`, `C1`, and downstream lineage evidence | Pending |
| `R26-08` | `verification` | `R26-11`; `R26-12` | Run full sanitizer, unit, lit, architecture, coverage, format, versioning, and benchmark gates. | RFC 0027 Test Plan | Pending |
| `R26-09` | `rfc` | `R26-08` | Audit completion criteria and transition implementation status truthfully. | `python3 scripts/check-rfc.py` | Pending |

Implementation of the synchronized capability begins only after the RFC 0027
immutable implementation-series base is recorded. Completed stable graph tasks
remain complete; the typed materializer and retained publication path remain
pending.

## Acceptance Transaction Record

| Field | Value |
|---|---|
| Transaction | `rfc0027-accept-20260727-e2f4ba5e` |
| Proposal SHA-256 | `e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435` |
| RFC 0026 status before transaction | `ACCEPTED` |
| RFC 0026 status after transaction | `ACCEPTED` |
| Synchronized authority | Typed graph witness, complete-root final-snapshot materializer, active membership, session seal, and retained capability lifetime |
| Implementation authority | RFC 0027 dependency graph after the immutable implementation-series base |

## Required Review Commands

- `python3 scripts/check-rfc.py`
- `python3 scripts/check-no-internal-versioning.py --check`
- `python3 scripts/check-format.py`
- `git diff --check`
