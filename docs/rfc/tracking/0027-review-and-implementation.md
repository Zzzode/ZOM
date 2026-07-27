# RFC 0027 Review And Implementation Tracker

## Discussion Record

### 2026-07-27 RFC 0025 Implementation Blocker

Implementation mapping found that the accepted Binder query inventory lacked a
complete stable fact model. `BindModuleSkeleton` could not derive the complete
body-owner set during authority staging, `MaterializeModuleSkeleton` omitted
implementation-site provenance, and the first production materialization
prototype used a frozen-registry lookup plus a prohibited session ledger.

RFC 0027 defines the stable Binder schema, exact descriptor read sets,
semantic-context-arena-owned append-only identity interner, capability lease
closure, and atomic production-root replacement required before core role
bootstrap can continue.

### Review Candidates

The first complete review candidate,
`d940570bc31876f1b5b0580ef936bb8b93fcfe4925c21ac3cff2c5e19f7ed44c`,
was rejected before the complete required-owner review finished. Review found
incomplete implementation scope, header inputs, identity authority, lifetime,
failure, synchronization, and verification contracts. No approval is
retained.

The second complete review candidate,
`8abe36d734937cf2b51a7f28cc2d3e03a746f0ac96d4e76eb08e96d8630b74b4`,
was rejected after all nine required-owner scopes independently reviewed the
exact unchanged hash. Findings covered contextual owner-body keys, the three
session transactions, fact and allocation completeness, diagnostic
duplication, codec and descriptor closure, graph materialization authority,
Checker and IR lease lineage, synchronization deletion scope, routing,
coverage, and the Release benchmark protocol. No approval is retained.

The third complete review candidate,
`def9c2f82597c4c70a39622a2e6182d26b1fbb70589cdc7eda00c5ccc8f38c4e`,
was rejected after all nine required-owner scopes independently reviewed the
exact unchanged hash. Findings covered projection-domain ambiguity, capability
result closure, missing Binder facts and provenance, undefined context and
transaction codecs, diagnostic emitter and payload closure, synchronization
deletions, manifest-owner conflicts, oversized IR tasks, fixed merge-base
evidence, and missing registered gates. No approval is retained.

The fourth complete review candidate,
`11bc26e10d27407a4f0987a5283953023b6f1e3449923bf1118a2ddecfbdeee6`,
was rejected after all nine required-owner scopes independently reviewed the
exact unchanged hash. Findings covered syntax-root identity, materialized
binding loss, handle-free context authority, transaction dependency order,
cross-owner synchronization, and exact verification ownership. No approval is
retained.

The fifth complete review candidate,
`e6e70449f4029345af520b67620dd3522c1290362d535e7c058bd2e5962dd925`,
was rejected after all nine required-owner scopes independently reviewed the
exact unchanged hash. Findings covered the missing live verified-package
request input to the transaction verifier and contradictory fixed-report
ownership and completion order. No approval is retained.

The sixth complete review candidate,
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`,
was approved without findings by all nine required-owner scopes. Acceptance
transaction `rfc0027-accept-20260727-e2f4ba5e` binds that exact proposal hash
to the synchronized RFC, tracker, and routing tree.

### 2026-07-27 Implementation Series Base

`products/zomlang/tests/coverage/implementation-series-base.txt` records clean
committed tree
`109947943519ec2d380a3e8d71813b40bc68bde5` as the immutable implementation
series base. The exact forty-lowercase-hex-plus-newline record names an
ancestor of the base-recording commit. `R27-12B` is complete and source
implementation is authorized in the dependency order below.

### 2026-07-27 RFC 0029 Acceptance Synchronization

Acceptance transaction `rfc0029-accept-20260727-8d393a0c` binds RFC 0029
proposal SHA-256
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`.
The synchronized design fixes the complete contextual Binder keys, adds the
identity-syntax-site inventory and stable-identity-admission capabilities,
closes the five provenance capability failure contracts, and orders the
stable Binder foundation before query-runtime work. RFC 0027 remains
`ACCEPTED`; completed tasks `R27-12B` and `R27-17A` remain complete.

### 2026-07-28 RFC 0031 Acceptance Synchronization

Acceptance transaction `rfc0031-accept-20260728-c25fcb18` binds RFC 0031
proposal SHA-256
`c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5`
and tracker SHA-256
`d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d`.
The synchronized design makes the stable-binding schema a hand-authored
canonical inventory, fixes descriptor-parameterized capability rows and their
dual owner-task alias checks, assigns complete-context artifacts to T1, and
assigns the comprehensive canonical-package schema mutation test to RFC 0030
`R30-13`. RFC 0027 remains `ACCEPTED`; completed Q3 task `R27-17A` remains
complete and its production files and existing tests are not reopened.

## Decision Record

Accepted by `task-router`, `rfc`, `module-system`, `binder-checker`,
`runtime-memory`, `error-system`, `ir-backend`, `spec-audit`, and
`verification` against exact proposal hash
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`.
The synchronized acceptance transaction is
`rfc0027-accept-20260727-e2f4ba5e`. Immutable implementation-series base
`109947943519ec2d380a3e8d71813b40bc68bde5` is recorded and source
implementation is authorized.

Transaction `rfc0030-accept-20260728-4ed0e6b8` establishes the implementation
order. `S1`, `S2`, and `S3` remain bounded review partitions and land only
through the exact atomic `R29-12AB` allowlist. `S6` then lands separately
through `R29-12D`. Query-runtime work begins at `R29-13A` only after both
transactions pass, and the corrected source transaction lands through
`R29-14`.

Transaction `rfc0031-accept-20260728-c25fcb18` establishes the accepted
metamodel consumed by RFC 0030 `R30-11`, binds both reviewed hashes, preserves
completed Q3 production authority, and places the additional package-schema
mutation test in `R30-13`.

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R27-01` | `rfc` | None | Complete RFC 0027 draft, tracker, and index row. | `python3 scripts/check-rfc.py` | Complete |
| `R27-02` | `task-router` | `R27-01` | Review path ownership, mandatory gates, escalation, and the ownership-analysis assignment. | Exact-hash review | Complete |
| `R27-03` | `rfc` | `R27-01` | Review completeness, prior art, exact-hash governance, synchronization, and implementation authorization. | Exact-hash review | Complete |
| `R27-04` | `module-system` | `R27-01` | Review query keys, exact read sets, session barriers, graph replacement, and production-root cutover. | Exact-hash review | Complete |
| `R27-05` | `binder-checker` | `R27-01` | Review stable Binder facts, body-owner closure, producers, verifiers, materializers, and Checker handoff. | Exact-hash review | Complete |
| `R27-06` | `runtime-memory` | `R27-01` | Review interner ownership, concurrency, handles, arenas, leases, teardown, ownership-overlay lineage, and ownership-analysis routing. | Exact-hash review | Complete |
| `R27-07` | `error-system` | `R27-01` | Review stable failure algebra, diagnostic facts, provenance, and unified materialization handoff. | Exact-hash review | Complete |
| `R27-08` | `ir-backend` | `R27-01` | Review downstream lease ownership through checked module, HIR, Built MIR, IR failure deletion, and build wiring. | Exact-hash review | Complete |
| `R27-09` | `spec-audit` | `R27-01` | Review current-state design claims, synchronized RFC overlays, and removal of obsolete architecture prose. | Exact-hash review | Complete |
| `R27-10` | `verification` | `R27-01` | Review mutation matrix, architecture gates, native tests, coverage, and benchmark protocol. | Exact-hash review | Complete |
| `R27-11` | `rfc` | `R27-02`; `R27-03`; `R27-04`; `R27-05`; `R27-06`; `R27-07`; `R27-08`; `R27-09`; `R27-10` | Record nine exact-hash approvals and prepare synchronized overlays for RFCs 0018, 0017, 0020, 0019, 0026, 0010, and 0025 plus all seven trackers while RFC 0027 remains REVIEW. | `python3 scripts/check-rfc.py` | Complete |
| `R27-12` | `task-router` | `R27-11` | Synchronize manifest and routing documentation, assign ownership analysis to `runtime-memory`, and assign `scripts/check-english-only.py` plus `scripts/check-spec-alignment.py` to `verification`. | RFC, English-only, and routing review | Complete |
| `R27-12A` | `rfc` | `R27-11`; `R27-12` | Read the completed routing tree without editing it, validate one tree state, record one transaction identifier and proposal hash in RFC metadata, then transition acceptance metadata atomically. | `python3 scripts/check-rfc.py` | Complete |
| `R27-12B` | `verification` | `R27-12A` | Record the clean committed accepted synchronization SHA as the immutable implementation-series base. | Frozen-base ancestry check | Complete; base `109947943519ec2d380a3e8d71813b40bc68bde5` is an ancestor of the recording commit |
| `R27-12C` | `rfc` | `R27-12B`; RFC 0029 `R29-11` | Synchronize complete contextual keys, identity-site provenance, stable-identity admission, exact typed provenance failures, and the corrected dependency order through transaction `rfc0029-accept-20260727-8d393a0c`. | `python3 scripts/check-rfc.py` | Complete |
| `R27-12D` | `rfc` | `R27-12C`; RFC 0031 `R31-09` | Synchronize the accepted hand-authored schema metamodel, direct visibility result, descriptor capability and failure aliases, Q3/`R30-13` test split, and exact T1 artifacts through transaction `rfc0031-accept-20260728-c25fcb18`. | `python3 scripts/check-rfc.py` | Complete |
| `R27-15` | `binder-checker` | RFC 0029 `R29-12AB` | Implement body disposition and independently verified staging-safe headers. | Header producer/verifier tests | Pending |
| `R27-17A` | `module-system` | `R27-12B` | Implement the handle-free canonical package request records, verified-request projection, exact codecs, and independent projection verifier. | Package request codec and projection tests | Complete; implementation `3039da5259be25b139954834f900a21b7b891fcf`, sanitizer unit tests 135/135, focused package request test 1/1, independent integration review approved |
| `R27-18` | `module-system` with required `runtime-memory` review | RFC 0029 `R29-14` | Replace frozen registries with the arena-owned eight-domain typed interner set. | Identity, concurrency, collision, and teardown tests | Pending |
| `R27-19` | `module-system` | `R27-15`; RFC 0029 `R29-14`; `R27-18` | Implement all eight exact complete-record membership projections after sealed admission and before any interner lookup. | Membership and readiness tests | Pending |
| `R27-20` | `binder-checker` | RFC 0029 `R29-12AB`; RFC 0029 `R29-12D`; `R27-15` | Implement `BindModuleSkeleton`, lookup projections, and independent verifier. | Skeleton and read-set tests | Pending |
| `R27-21` | `binder-checker` | `R27-20` | Implement contextual `BindOwnerBody` and independent traversal/verifier. | Body, capture, control, and source-failure tests | Pending |
| `R27-22` | `binder-checker` | `R27-21` | Implement the deterministic five-domain module allocation plan. | Overflow and reversed-demand tests | Pending |
| `R27-23` | `module-system` | RFC 0029 `R29-14`; RFC 0028 `R28-16`; `R27-19` | Implement the typed graph witness and final-sealed-snapshot `MaterializeModuleGraph` consumer over runtime-only module-dependency provenance. | Graph, provenance, and read-set mutations | Pending |
| `R27-24` | `binder-checker` | `R27-19`; `R27-22`; `R27-23` | Implement materialized module skeleton and owner-body capabilities. | Typed expansion and retained-child tests | Pending |
| `R27-25` | `binder-checker` | `R27-24` | Implement `VerifyBoundModule`, immutable aggregate storage, and failure projection. | Coverage, lineage, and failure tests | Pending |
| `R27-26` | `binder-checker` | `R27-25` | Migrate Checker consumers to `CheckerBoundModuleView`. | Focused Checker tests | Pending |
| `R27-26A` | `module-system` | `R27-26` | Migrate module-interface publication to the lease-owning Checker view. | Focused interface tests | Pending |
| `R27-27A` | `ir-backend` | `R27-26` | Migrate verified checked module to a retained bound-module lease. | Checked-module lineage tests | Pending |
| `R27-27B` | `ir-backend` | `R27-27A` | Migrate verified HIR to a retained bound-module lease. | HIR lineage tests | Pending |
| `R27-27C` | `ir-backend` | `R27-27B` | Migrate Built MIR to a retained bound-module lease. | MIR lineage tests | Pending |
| `R27-27D` | `runtime-memory` | `R27-27C` | Migrate the ownership overlay to a retained bound-module lease and exact destruction order. | Ownership lineage tests | Pending |
| `R27-28A` | `module-system` | RFC 0029 `R29-14`; RFC 0028 `R28-16`; `R27-23`; `R27-25` | In `module-graph-query-input.{h,cc}`, implement `CompleteCompilationContextAuthority`, its canonical codec, `CompleteCompilationContextAuthorityInput`, and its independent verifier; then consume the session-owned live verified package request, execute the input transactions, and implement the session state machine plus sealed snapshots. | Complete-context verifier, session transaction, and borrowed-input lifetime tests | Pending |
| `R27-28B` | `module-system` | `R27-26A`; `R27-27D`; `R27-28A` | Implement the dependency-first production capability root over the RFC 0028 sealed snapshot and inherited admission contract. | Session architecture and end-to-end tests | Pending |
| `R27-28C` | `module-system` | `R27-28B` | Implement surviving-lease and session teardown order. | Teardown tests | Pending |
| `R27-29` | `module-system` | `R27-28C` | Delete identity registry/freeze authority, session ledgers, and the session-owned handleful graph root. | Identity and session zero-reference gates | Pending |
| `R27-30` | `binder-checker` | `R27-28C` | Delete frozen Binder inventory, production batch binding, mirrors, detached clones, and non-owning bound-module input. | Binder and Checker zero-reference gates | Pending |
| `R27-31` | `ir-backend` | `R27-27C` | Delete Binder-to-IR failure conversion and non-owning checked/HIR/MIR bound-module references. | IR zero-reference gates | Pending |
| `R27-31A` | `runtime-memory` | `R27-27D` | Delete non-owning ownership-overlay bound-module references. | Ownership zero-reference gate | Pending |
| `R27-31B` | `binder-checker` | `R27-30` | Synchronize Binder and Checker build source lists. | Focused build | Pending |
| `R27-31C` | `module-system` | `R27-29` | Synchronize identity, query, graph, and session build source lists. | Focused build | Pending |
| `R27-31D` | `ir-backend` | `R27-31` | Synchronize compiler, checked-module, HIR, MIR, and IR build wiring. | Focused build | Pending |
| `R27-31E` | `runtime-memory` | `R27-31A` | Synchronize ownership-overlay build wiring. | Focused build | Pending |
| `R27-32` | `verification` | RFC 0029 `R29-12AB`; RFC 0029 `R29-12D`; `R27-15` through `R27-31E`; RFC 0029 `R29-15` | Implement the English-only and specification-alignment gates under their synchronized routing ownership; add the exact IR diagnostic regression and unique installed-consumer fixture; then run the complete sanitizer, unit, lit, codegen, architecture, format, versioning, frozen-base coverage, English-only, install-consumer, spec-alignment, and Release compare matrix. | RFC 0027, RFC 0028, and RFC 0029 Test Plans | Pending |
| `R27-33A` | `spec-audit` | `R27-32` | Update current-state compiler design documentation from the landed production path and publish `docs/reports/zom-core-library-spec-alignment.md` with the report-producing core alignment command. | Spec and design audit plus fixed report publication | Pending |
| `R27-32A` | `verification` | `R27-33A` | Recompute and byte-compare the fixed report without writing it, then rerun English-only, RFC, format, versioning, and diff gates after every spec-audit edit. | Final write-free RFC 0027 Test Plan subset | Pending |
| `R27-33` | `rfc` | `R27-32A` | Audit all implementation, report, and final verification evidence, then perform the only synchronized RFC implementation-state transitions. | `python3 scripts/check-rfc.py` | Pending |

`R27-12B`, `R27-12C`, `R27-12D`, and `R27-17A` are complete. Remaining
implementation is authorized only in the dependency order recorded above.
`R27-17A` remains the completed Q3 production task; RFC 0030 `R30-13` owns
only the additional comprehensive schema mutation test and exact landing
allowlist entry.

## RFC 0029 Acceptance Transaction Record

| Field | Value |
|---|---|
| Transaction | `rfc0029-accept-20260727-8d393a0c` |
| Proposal SHA-256 | `8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7` |
| RFC 0027 status before transaction | `ACCEPTED` |
| RFC 0027 status after transaction | `ACCEPTED` |
| Synchronized authority | Complete contextual keys, identity-site provenance, stable-identity admission, exact typed provenance failures, and schema-before-runtime ordering |
| Implementation authority | exact RFC 0030 `R29-12AB`, then separate `R29-12D`, then runtime work beginning at `R29-13A` and landing through `R29-14` |

## RFC 0030 Acceptance Transaction Record

| Field | Value |
|---|---|
| Transaction | `rfc0030-accept-20260728-4ed0e6b8` |
| Proposal SHA-256 | `4ed0e6b885abc87a1c4251855780cf115a85b3623b1d46f774a4b664110f7b6b` |
| Tracker SHA-256 | `31eb9abae5aa70465a8408e05130263a75cea4ca91c0ada8ac673d238c2664f9` |
| RFC 0027 status before transaction | `ACCEPTED` |
| RFC 0027 status after transaction | `ACCEPTED` |
| Synchronized authority | Exact build-visible S1-plus-S2-plus-S3 landing set, driver-owned contextual-key cutover, native and mutation gates, landing-scope proof, and exact S6 diagnostic transaction |
| Implementation authority | RFC 0030 `R30-11` through `R30-16`, then runtime work beginning at RFC 0029 `R29-13A` |

## RFC 0031 Acceptance Transaction Record

| Field | Value |
|---|---|
| Transaction | `rfc0031-accept-20260728-c25fcb18` |
| Proposal SHA-256 | `c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5` |
| Tracker SHA-256 | `d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d` |
| RFC 0027 status before transaction | `ACCEPTED` |
| RFC 0027 status after transaction | `ACCEPTED` |
| Synchronized authority | Hand-authored RFC 0031 schema metamodel, direct visibility result, descriptor result/payload/failure closure, exact Q3 versus `R30-13` test ownership, and T1 complete-context artifacts |
| Implementation authority | RFC 0030 `R30-11` through `R30-16` under the accepted RFC 0031 metamodel, then runtime work beginning at RFC 0029 `R29-13A`; completed Q3 remains closed |
