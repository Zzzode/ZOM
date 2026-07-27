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

## Decision Record

Accepted by `task-router`, `rfc`, `module-system`, `binder-checker`,
`runtime-memory`, `error-system`, `ir-backend`, `spec-audit`, and
`verification` against exact proposal hash
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`.
The synchronized acceptance transaction is
`rfc0027-accept-20260727-e2f4ba5e`. Immutable implementation-series base
`109947943519ec2d380a3e8d71813b40bc68bde5` is recorded and source
implementation is authorized.

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
| `R27-13` | `binder-checker` | `R27-12B` | Implement stable keys, headers, facts, result algebra, and generated field inventory. | Focused Binder schema tests | Pending |
| `R27-14` | `binder-checker` | `R27-13` | Implement literal-domain codecs, bounds, exact consumption, fixed oracles, and mutations. | Codec oracle and mutation tests | Pending |
| `R27-15` | `binder-checker` | `R27-13` | Implement body disposition and independently verified staging-safe headers. | Header producer/verifier tests | Pending |
| `R27-16` | `error-system` | `R27-13` | Implement the sole RFC 0017 diagnostic wire integration, `ZOM3028`, and failed-lookup bijection. | Diagnostic coverage and focused tests | Pending |
| `R27-17` | `module-system` | `R27-12B` | Implement final seal, literal descriptors, retained dependency edges, and compile-time materializer permissions. | Query capability tests | Pending |
| `R27-17A` | `module-system` | `R27-12B` | Implement the handle-free canonical package request records, verified-request projection, exact codecs, and independent projection verifier. | Package request codec and projection tests | Pending |
| `R27-17B` | `module-system` | `R27-17`; `R27-17A` | Implement the complete context authority descriptor, three payload schemas and codecs, transaction digests, atomic transaction controls, and synchronous live verified-package-request input with independent projection before any membership or materializer consumer. | Input schema, projection, mutation, lifetime, and atomicity tests | Pending |
| `R27-18` | `module-system` with required `runtime-memory` review | `R27-17` | Replace frozen registries with the arena-owned eight-domain typed interner set. | Identity, concurrency, collision, and teardown tests | Pending |
| `R27-19` | `module-system` | `R27-15`; `R27-17B`; `R27-18` | Implement all eight complete-record active-membership projections. | Membership and readiness tests | Pending |
| `R27-20` | `binder-checker` | `R27-14`; `R27-15` | Implement `BindModuleSkeleton`, lookup projections, and independent verifier. | Skeleton and read-set tests | Pending |
| `R27-21` | `binder-checker` | `R27-20` | Implement contextual `BindOwnerBody` and independent traversal/verifier. | Body, capture, control, and source-failure tests | Pending |
| `R27-22` | `binder-checker` | `R27-21` | Implement the deterministic five-domain module allocation plan. | Overflow and reversed-demand tests | Pending |
| `R27-23` | `module-system` | `R27-17B`; `R27-19` | Implement typed graph witness and final-snapshot `MaterializeModuleGraph`. | Graph, provenance, and read-set mutations | Pending |
| `R27-24` | `binder-checker` | `R27-19`; `R27-22`; `R27-23` | Implement materialized module skeleton and owner-body capabilities. | Typed expansion and retained-child tests | Pending |
| `R27-25` | `binder-checker` | `R27-24` | Implement `VerifyBoundModule`, immutable aggregate storage, and failure projection. | Coverage, lineage, and failure tests | Pending |
| `R27-26` | `binder-checker` | `R27-25` | Migrate Checker consumers to `CheckerBoundModuleView`. | Focused Checker tests | Pending |
| `R27-26A` | `module-system` | `R27-26` | Migrate module-interface publication to the lease-owning Checker view. | Focused interface tests | Pending |
| `R27-27A` | `ir-backend` | `R27-26` | Migrate verified checked module to a retained bound-module lease. | Checked-module lineage tests | Pending |
| `R27-27B` | `ir-backend` | `R27-27A` | Migrate verified HIR to a retained bound-module lease. | HIR lineage tests | Pending |
| `R27-27C` | `ir-backend` | `R27-27B` | Migrate Built MIR to a retained bound-module lease. | MIR lineage tests | Pending |
| `R27-27D` | `runtime-memory` | `R27-27C` | Migrate the ownership overlay to a retained bound-module lease and exact destruction order. | Ownership lineage tests | Pending |
| `R27-28A` | `module-system` | `R27-17B`; `R27-23`; `R27-25` | Supply the session-owned live verified package request, execute the three predeclared input transactions, and implement the session state machine plus named snapshots without redeclaring payload schemas. | Session transaction and borrowed-input lifetime tests | Pending |
| `R27-28B` | `module-system` | `R27-26A`; `R27-27D`; `R27-28A` | Implement the dependency-first production capability root and irreversible final seal. | Session architecture and end-to-end tests | Pending |
| `R27-28C` | `module-system` | `R27-28B` | Implement surviving-lease and session teardown order. | Teardown tests | Pending |
| `R27-29` | `module-system` | `R27-28C` | Delete identity registry/freeze authority, session ledgers, and the session-owned handleful graph root. | Identity and session zero-reference gates | Pending |
| `R27-30` | `binder-checker` | `R27-28C` | Delete frozen Binder inventory, production batch binding, mirrors, detached clones, and non-owning bound-module input. | Binder and Checker zero-reference gates | Pending |
| `R27-31` | `ir-backend` | `R27-27C` | Delete Binder-to-IR failure conversion and non-owning checked/HIR/MIR bound-module references. | IR zero-reference gates | Pending |
| `R27-31A` | `runtime-memory` | `R27-27D` | Delete non-owning ownership-overlay bound-module references. | Ownership zero-reference gate | Pending |
| `R27-31B` | `binder-checker` | `R27-30` | Synchronize Binder and Checker build source lists. | Focused build | Pending |
| `R27-31C` | `module-system` | `R27-29` | Synchronize identity, query, graph, and session build source lists. | Focused build | Pending |
| `R27-31D` | `ir-backend` | `R27-31` | Synchronize compiler, checked-module, HIR, MIR, and IR build wiring. | Focused build | Pending |
| `R27-31E` | `runtime-memory` | `R27-31A` | Synchronize ownership-overlay build wiring. | Focused build | Pending |
| `R27-32` | `verification` | `R27-12B` through `R27-31E` | Implement the English-only and specification-alignment gates under their synchronized routing ownership; add the exact IR diagnostic regression and unique installed-consumer fixture; then run the complete sanitizer, unit, lit, codegen, architecture, format, versioning, frozen-base coverage, English-only, install-consumer, spec-alignment, and Release compare matrix. | RFC 0027 Test Plan | Pending |
| `R27-33A` | `spec-audit` | `R27-32` | Update current-state compiler design documentation from the landed production path and publish `docs/reports/zom-core-library-spec-alignment.md` with the report-producing core alignment command. | Spec and design audit plus fixed report publication | Pending |
| `R27-32A` | `verification` | `R27-33A` | Recompute and byte-compare the fixed report without writing it, then rerun English-only, RFC, format, versioning, and diff gates after every spec-audit edit. | Final write-free RFC 0027 Test Plan subset | Pending |
| `R27-33` | `rfc` | `R27-32A` | Audit all implementation, report, and final verification evidence, then perform the only synchronized RFC implementation-state transitions. | `python3 scripts/check-rfc.py` | Pending |

`R27-12B` is complete. Implementation is authorized in the dependency order
recorded above.
