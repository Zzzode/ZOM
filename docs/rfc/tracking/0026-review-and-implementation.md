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

### 2026-07-27 RFC 0028 Acceptance Synchronization

RFC 0028 was accepted on exact proposal SHA-256
`944b68ffc0aff5576d079a243ff092d7d19fba5ffed65551dda8e68adf230db4`
through transaction `rfc0028-accept-20260727-944b68ff`.

The synchronized graph contract uses explicit transaction and final-seal
results, one `SealedQuerySnapshot` root with inherited immutable admission,
descriptor-dependent capability failures, exact materializer permissions, and
complete membership authority before interning.
`ModuleDependencyProvenanceMap` is a retained final-sealed runtime capability
whose memo owns the exact final parse lineage. No ambient session, current
parse, or database seal flag supplies provenance or admission authority.

The synchronization leaves RFC 0026 `ACCEPTED` and leaves all pending product
implementation and verification work pending.

## Owner Review Matrix

| Owner | Required review | Status | Evidence |
|---|---|---|---|
| `rfc` | metadata, current contract, synchronization, status | Approved | RFC 0026 accepted snapshot plus RFC 0027 and RFC 0028 exact-hash approvals. |
| `module-system` | inputs, graph, SCC, materializer, membership, session seal | Approved | RFC 0026 accepted snapshot plus RFC 0027 and RFC 0028 exact-hash approvals. |
| `binder-checker` | stable graph consumers, bound-module lineage, lease handoff | Approved | RFC 0026 accepted snapshot plus RFC 0027 and RFC 0028 exact-hash approvals. |
| `verification` | codecs, mutations, native gates, coverage, benchmarks | Approved | RFC 0026 accepted snapshot plus RFC 0027 and RFC 0028 exact-hash approvals. |

## Decision Record

RFC 0026 is `ACCEPTED`.

The original accepted proposal SHA-256 is
`39df5d3f11dbdcb2e95056b1cd14fd5220a19688f31a3e3180230ad465a3f84d`.

On 2026-07-27, transaction `rfc0027-accept-20260727-e2f4ba5e`
synchronized the current RFC 0026 contract to RFC 0027 proposal SHA-256
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`.
The synchronized design is accepted, while product implementation and final
verification retain the statuses below.

On 2026-07-27, transaction `rfc0029-accept-20260727-8d393a0c`
synchronized the current RFC 0026 contract to RFC 0029 proposal SHA-256
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`.
The accepted order lands the stable schema and facts atomically, then lands
codecs and diagnostic facts separately, and only then begins the corrected
runtime source transaction. No implementation status changes.

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
| `R26-13` | `rfc` | `R26-10`; RFC 0028 `R28-12` | Synchronize explicit transaction and seal results, sealed-root admission, typed capability permissions and failures, retained final-parse provenance lineage, and the replacement dependency boundary through transaction `rfc0028-accept-20260727-944b68ff`. | `python3 scripts/check-rfc.py` | Complete |
| `R26-14` | `rfc` | `R26-13`; RFC 0029 `R29-11` | Synchronize complete Binder contextual keys, identity-site provenance, stable-identity admission, exact typed provenance failures, and the corrected dependency order through transaction `rfc0029-accept-20260727-8d393a0c`. | `python3 scripts/check-rfc.py` | Complete |
| `R26-11` | `module-system` | `R26-14`; RFC 0029 `R29-14`; RFC 0028 `R28-16` | Implement complete context authority, active memberships, typed materialized witness, sealed-root `MaterializeModuleGraph`, retained provenance, and capability lifetime. | RFC 0029 `R29-14`, RFC 0028 `R28-16`, and applicable RFC 0027 evidence | Pending |
| `R26-12` | `binder-checker` | `R26-11`; RFC 0029 `R29-14`; RFC 0028 `R28-16` | Migrate bound-module graph consumers and downstream lease lineage. | RFC 0027 `M2` through `M5`, `C1`, and synchronized provenance lineage evidence | Pending |
| `R26-08` | `verification` | `R26-11`; `R26-12`; RFC 0029 `R29-15` | Run full sanitizer, unit, lit, architecture, coverage, format, versioning, and benchmark gates. | RFC 0027, RFC 0028, and RFC 0029 Test Plans | Pending |
| `R26-09` | `rfc` | `R26-08` | Audit completion criteria and transition implementation status truthfully. | `python3 scripts/check-rfc.py` | Pending |

Completed stable graph tasks remain complete. The typed materializer, retained
provenance, and publication path resume through RFC 0029 `R29-14`, RFC 0028
`R28-16`, and RFC 0029 `R29-15` before later RFC 0027 dependency edges; they
remain pending.

## Acceptance Transaction Record

| Field | Value |
|---|---|
| Transaction | `rfc0027-accept-20260727-e2f4ba5e` |
| Proposal SHA-256 | `e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435` |
| RFC 0026 status before transaction | `ACCEPTED` |
| RFC 0026 status after transaction | `ACCEPTED` |
| Synchronized authority | Typed graph witness, complete-root final-snapshot materializer, active membership, session seal, and retained capability lifetime |
| Implementation authority | RFC 0027 dependency graph after the immutable implementation-series base |

## RFC 0028 Acceptance Transaction Record

| Field | Value |
|---|---|
| Transaction | `rfc0028-accept-20260727-944b68ff` |
| Proposal SHA-256 | `944b68ffc0aff5576d079a243ff092d7d19fba5ffed65551dda8e68adf230db4` |
| RFC 0026 status before transaction | `ACCEPTED` |
| RFC 0026 status after transaction | `ACCEPTED` |
| Synchronized authority | Explicit transaction and seal results, sealed-root admission propagation, typed capability failures and permissions, and retained final-parse provenance lineage |
| Runtime dependencies | RFC 0029 `R29-12AB`, `R29-12C`, and `R29-12D` precede runtime; corrected runtime lands through `R29-14`, provenance continues through RFC 0028 `R28-16`, and verification completes through `R29-15` |

## RFC 0029 Acceptance Transaction Record

| Field | Value |
|---|---|
| Transaction | `rfc0029-accept-20260727-8d393a0c` |
| Proposal SHA-256 | `8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7` |
| RFC 0026 status before transaction | `ACCEPTED` |
| RFC 0026 status after transaction | `ACCEPTED` |
| Synchronized authority | Complete Binder contextual keys, identity-site provenance, stable-identity admission, exact typed provenance failures, and schema-before-runtime ordering |
| Implementation dependencies | RFC 0029 `R29-12AB`, `R29-12C`, `R29-12D`, `R29-14`, RFC 0028 `R28-16`, and RFC 0029 `R29-15` |

## Required Review Commands

- `python3 scripts/check-rfc.py`
- `python3 scripts/check-no-internal-versioning.py --check`
- `python3 scripts/check-format.py`
- `git diff --check`
