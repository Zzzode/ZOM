# RFC 0028 Review And Implementation Tracker

## Discussion Record

### 2026-07-27 Implementation Closure Audit

RFC 0027 implementation began from immutable series base
`109947943519ec2d380a3e8d71813b40bc68bde5`. Query result types and the
canonical package-compilation request landed, but implementation mapping found
that `S1` and `Q2` still required undefined contracts for database identity,
final-seal admission, transaction failures, literal descriptors, exact
materializer permissions, membership authority equality, and module dependency
provenance.

Independent schema review also found that `ClosureEnvironmentMap` had no
production consumer, duplicated `BoundOwnerBody` closure facts, and could not
preserve the closed Binder result alternatives without adding another result
domain. RFC 0028 removes that projection and defines
`ModuleDependencyProvenanceMap` as a retained runtime capability.

No RFC 0028 source implementation is authorized while this proposal is
`DRAFT` or `REVIEW`.

### Review Candidates

The first complete review candidate,
`735df50c5f6f27d53e58a7ab934a03c301b9f68a1c1a72217a4758b5a6d91f99`,
was rejected after the affected-owner and supplemental reviews found
unavailable capability-failure types, an undefined complete-context verifier
contract, incomplete failure precedence and mutation gates, incorrect
ThreadPool spelling, production/test inventory and provenance-schema gaps,
owner-invalid implementation partitions, and non-executable final gate prose.
No approval is retained.

The second complete review candidate,
`b4b2321a5df46c8fc3d5c75d2bc232a3c596a63e02bdd5b367d3e5c9082cdd90`,
was rejected after review found a rollout-order contradiction, a missing
production registration caller for the provenance descriptor, and references
to coverage commands and an exemption schema that do not exist in the current
repository. No approval is retained.

The third complete review candidate,
`7eefdbf612dc579ef1dfcedcc599d27f3c5802644c17805dd94b06f6425f61c8`,
was rejected because its existing CMake coverage flow required only executed
lines instead of the repository's per-file 70 percent threshold or an exact
owner-reviewed FFI or unreachable-code exemption. No approval is retained.

The fourth complete review candidate,
`944b68ffc0aff5576d079a243ff092d7d19fba5ffed65551dda8e68adf230db4`,
was approved without findings by all seven affected-owner scopes and both
supplemental technical scopes. Acceptance transaction
`rfc0028-accept-20260727-944b68ff` binds that exact proposal hash to the
synchronized RFC, tracker, index, and routing tree.

### RFC 0029 Synchronized Acceptance

RFC 0029 proposal SHA-256
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`
was approved by every required owner. Transaction
`rfc0029-accept-20260727-8d393a0c` synchronizes this tracker and RFC 0028 to
unforgeable retained database identity, separated request-result alternatives,
independent identity-site provenance, stable identity admission, complete
contextual keys, the five exact Binder capability failure contracts, and the
corrected foundation-before-runtime dependency order. The original RFC 0028
acceptance remains historical evidence; the RFC 0029 synchronization is the
current implementation authority.

### RFC 0031 Synchronized Acceptance

RFC 0031 proposal SHA-256
`c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5`
and tracker SHA-256
`d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d`
were approved by every required owner. Transaction
`rfc0031-accept-20260728-c25fcb18` synchronizes the generic
descriptor-dependent capability-demand sum, the descriptor-parameterized
module-dependency-provenance result, its runtime-only payload, and staged
compile-time equality checks for both hand-written descriptor aliases.
Implementation authority remains with the pending tasks below.

### 2026-07-29 R29-14 Exact-Scope Correction

Preparation of the atomic runtime landing found four required production
callers that the accepted exact-file tables omitted. The query transaction
replacement requires `zomlang/compiler/binder/binding-input.cc` to
consume the unversioned publication result directly. Stable identity admission
requires the existing `ModuleBodySyntax` producer and independent verifier to
consume the admitted authority at their Binder-owned boundary.

The correction adds only `binding-input.cc`, `module-body-syntax.h`,
`module-body-syntax-producer.cc`, and `module-body-syntax-verifier.cc` to the
existing `R29-14` union. It does not change runtime semantics, add a
compatibility path, expand landing authority beyond those four files, or claim
implementation completion.

## Decision Record

Accepted by `task-router`, `rfc`, `module-system`, `lexer-parser`,
`binder-checker`, `spec-audit`, and `verification`, with supplemental
`runtime-memory` and `error-system` approval, against exact proposal SHA-256
`944b68ffc0aff5576d079a243ff092d7d19fba5ffed65551dda8e68adf230db4`.
The original acceptance transaction is
`rfc0028-accept-20260727-944b68ff`. The current synchronized implementation
authority is RFC 0029 proposal SHA-256
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`
under transaction `rfc0029-accept-20260727-8d393a0c`. Source implementation
is authorized only through the dependency-ordered pending tasks below.
Transaction `rfc0031-accept-20260728-c25fcb18` binds the current schema
overlay to RFC 0031 proposal SHA-256
`c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5`
and tracker SHA-256
`d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d`
without completing any implementation task.
Transaction `rfc0028-r29-14-scope-20260729-521d82c7` binds exact
four-document candidate manifest SHA-256
`521d82c731dee0a4b262e937d5578651850446eebfe7448a71a39cb63fc8e086`.
It adds only `binding-input.cc`, `module-body-syntax.h`,
`module-body-syntax-producer.cc`, and `module-body-syntax-verifier.cc` to the
existing atomic union. RFC 0029 `R29-14` remains the sole source landing
authority.

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R28-01` | `rfc` | None | Complete RFC 0028 draft, tracker, and index row. | `python3 scripts/check-rfc.py` | Complete |
| `R28-02` | `task-router` | `R28-01` | Confirm required owners, file ownership, gates, and atomic migration boundary. | Exact-hash review | Complete |
| `R28-03` | `rfc` | `R28-01` | Review process completeness, prior art, exact-hash governance, and synchronization. | Exact-hash review | Complete |
| `R28-04` | `module-system` | `R28-01` | Review identity, transaction, seal, descriptor, permission, and provenance contracts. | Exact-hash review | Complete |
| `R28-05` | `lexer-parser` | `R28-01` | Review parser capability descriptors and final parse lifetime. | Exact-hash review | Complete |
| `R28-06` | `binder-checker` | `R28-01` | Review membership authority, closure deletion, schema boundary, and consumers. | Exact-hash review | Complete |
| `R28-07` | `runtime-memory` | `R28-01` | Review token identity and lifetime, locking, admission order, leases, and teardown. | Exact-hash review | Complete |
| `R28-08` | `error-system` | `R28-01` | Review failure algebra, precedence, and atomicity. | Exact-hash review | Complete |
| `R28-09` | `spec-audit` | `R28-01` | Review synchronized current-contract claims and duplicate authority removal. | Exact-hash review | Complete |
| `R28-10` | `verification` | `R28-01` | Review tests, generator coverage, gates, determinism, and Release evidence. | Exact-hash review | Complete |
| `R28-11` | `rfc` | `R28-02`; `R28-03`; `R28-04`; `R28-05`; `R28-06`; `R28-07`; `R28-08`; `R28-09`; `R28-10` | Record exact-hash approvals and prepare the synchronized acceptance transaction. | `python3 scripts/check-rfc.py` | Complete |
| `R28-11A` | `task-router` | `R28-11` | Assign the two descriptor scripts to `verification` and synchronize routing documentation. | Routing and RFC review | Complete |
| `R28-12` | `rfc` | `R28-11`; `R28-11A` | Validate one synchronized tree, record one transaction identifier and proposal hash, and accept atomically. | `python3 scripts/check-rfc.py` | Complete |
| `R28-13A` | `module-system` with `runtime-memory` review | `R29-12A`; `R29-12B`; `R29-12AB`; `R29-12D` | Prepare the reviewed token-identity, separated request-result, and RFC 0031 generic descriptor-dependent capability-demand runtime-sum partition; do not land independently. | Type, lifetime, and failure review | Complete through RFC 0029 `R29-14` |
| `R28-13B` | `module-system` | `R28-13A` | Prepare the reviewed transaction, seal, sealed-snapshot, and admission partition; do not land independently. | Query database review | Complete through RFC 0029 `R29-14` |
| `R28-13C` | `module-system` | `R28-13B` | Prepare the descriptor schema and query build-wiring partition; do not land independently. | Query inventory review | Complete through RFC 0029 `R29-14` |
| `R28-13C1` | `verification` | `R28-13C` | Prepare the descriptor generator, architecture gate, and adversarial self-tests; do not land independently. | Gate self-tests | Complete through RFC 0029 `R29-14` |
| `R28-13D` | `module-system` | `R28-13C1` | Prepare bounded identity and driver descriptor/caller partitions; do not land independently. | Owner-focused review | Complete through RFC 0029 `R29-14` |
| `R28-13E` | `lexer-parser` | `R28-13C1` | Prepare the parse capability descriptor, failure codec, and caller partition; do not land independently. | Parser capability review | Complete through RFC 0029 `R29-14` |
| `R28-13F` | `binder-checker` with `verification` review | `R28-13C1` | Prepare the Binder transaction-consumer production and native-test cutover; do not land independently. | Binder consumer review | Complete through RFC 0029 `R29-14` |
| `R28-13G` | `verification` | `R28-13A`; `R28-13B`; `R28-13C`; `R28-13C1`; `R28-13D`; `R28-13E`; `R28-13F` | Prepare bounded native tests, generated test inventory, real-object decoder, race gate, CTest wiring, and negative compile partitions; do not land independently. | Verification review | Complete through RFC 0029 `R29-14` |
| `R28-14` | `module-system` with all partition-owner review | `R28-13G`; `R29-13B`; `R29-13C` | Complete the runtime partition join for RFC 0029 `R29-14`; this row has no independent landing authority. | Partition-join review | Complete through RFC 0029 `R29-14`; commit `d83eed927ad782963dc49a143b4dab48cb857f85` |
| `R28-16A` | `module-system` with `lexer-parser` review | RFC 0029 `R29-14` | Prepare the production provenance descriptor, provider, verifier, query schema row, owned stable-Binder row checks for both `Capability` and `FailureAlternatives`, `registerModuleGraphQueries` registration, and build wiring; do not land independently. | Production provenance and dual-alias review | Complete: commit `be51e559` landed the descriptor, provider, verifier, schema, registration, and build wiring; focused sanitizer target and descriptor gates pass on 2026-08-02. |
| `R28-16B` | `verification` with `binder-checker` review | `R28-16A` | Prepare provenance, registration, result-type, capability-alias, failure-alternative-alias, and final architecture-gate mutation tests, the updated test inventory, and test build wiring; do not land independently. | Provenance and dual-alias mutation review | Complete: commit `be51e559` landed the provenance and registration tests, test inventory, and architecture mutations; focused sanitizer tests and `check-query-descriptor-architecture.py --check --self-test` pass on 2026-08-02. |
| `R28-16` | `module-system` with all partition-owner review | `R28-16B` | Assemble and land one buildable provenance source, schema, test, and CMake transaction. | Provenance capability and mutation tests | Complete: commit `be51e559` is the buildable provenance transaction; source/prelude, ordering, witness mutation, registration, descriptor-schema, and architecture negative checks pass on 2026-08-02. |
| `R28-17` | `verification` | `R28-16` | Run focused, full, architecture, generated-inventory, determinism, and Release verification. | RFC 0028 Test Plan | Complete; focused provenance, descriptor-schema, and architecture gates passed; sanitizer CTest passed except for the system `lit` `psutil` prerequisite, whose AST and diagnostics suites passed in an isolated environment; the complete ANTLR grammar matrix passed with exit code 0 and `ALL PASSED` on 2026-08-02. |
| `R28-18` | `spec-audit` | `R28-17` | Publish only the production-backed current compiler contract. | Complete: aligned the RFC descriptor name and result type with `ModuleDependencyProvenance`; RFC structure, provenance capability, descriptor schema, architecture, generator, and adversarial architecture self-test checks pass on 2026-08-02. | Complete |
| `R28-19` | `rfc` | `R28-18` | Audit evidence and perform truthful synchronized implementation-state transitions. | RFC and evidence audit | Complete; re-audited the landed provenance transaction on 2026-08-02. The focused sanitizer test, production and test descriptor inventories, descriptor architecture check and self-test, generator check, CompilerSession architecture check, and RFC structure check pass. RFC 0028 remains IMPLEMENTING because RFC 0027 R27-21, R27-22, and R27-28A remain pending. |

The shared query-runtime partitions and their complete RFC 0029 evidence are
landed. Commit `be51e559` activated and verified the production
module-dependency-provenance row through the sole `R28-16` source transaction.
R28-19 completed the current evidence audit on 2026-08-02. RFC 0028 remains
IMPLEMENTING until the RFC 0027 materializer dependencies are complete.

### 2026-07-30 Complete-Context Final-Seal Atomicity Correction

Independent RFC 0027 source review found that a separately landed
complete-context descriptor could expose a static final verifier before the
graph, SCC, authority, readiness, and transaction-witness descriptors existed.
That intermediate contract cannot satisfy this RFC.

The synchronized correction makes RFC 0027 I1A, I2, and M1 prepare-only and
T1 their sole atomic landing authority. T1 also migrates the query inventory
to the production descriptor and deletes both test-only complete-context
shadows in the same buildable transaction. A key/value/self-witness-only
verifier is forbidden.

The user-designated independent approver accepted exact pre-evidence Git diff
SHA-256
`b25aef908d13395fce59151e6e31a9fea2f11f788fdd2806d17fa378b99d8821`.
Transaction `rfc0027-context-atomic-20260730-b25aef90` records the synchronized
correction and does not change the status of any RFC 0028 implementation task.

### 2026-07-30 Final-Seal Test Responsibility Correction

Runtime preflight proved that generic final-seal phase-order, race, and
irreversibility tests need a minimal descriptor with independently selectable
authority outcomes. Those tests use the distinct `Frozen`
`query::test::TestCompleteContextInput` at the first test-only inventory slot
with domain
`test.input.complete-context`. Both same-name production-role shadows are
deleted. `QueryCapability` final-sealed integration and driver session
final-seal tests use `CompleteCompilationContextAuthorityInput` with a valid
production root key, complete authority, installed read set, and final
witness. The test fixture enters no production schema or library and no alias,
verifier injection seam, or fallback is permitted.

The user-designated independent approver accepted exact four-document
pre-evidence Git diff SHA-256
`f6c041551684ac722a7b4e12682d963f65f01cd4557cc06ca0faaa5f07879437`.
Transaction
`rfc0027-final-seal-test-boundary-20260730-f6c04155` records the synchronized
correction without completing an RFC 0028 source task.

### 2026-07-30 Transaction Ownership Scope Correction

Final-seal implementation preflight found that RFC 0027 T1 omitted the live
owner and native test of `VerifiedCoreDistributionInputTransaction` from its
exact file list. The corrected scope adds
`core-library-query-provider.{h,cc}` and
`core-library-query-provider-test.cc`, replaces that transaction directly,
and requires the same caller-supplied previous revision, closed commit result,
canonical payload verification, and same-revision transaction witness as the
other two session transactions. No wrapper or follow-up witness revision is
permitted, and no RFC 0028 implementation task changes status.

The user-designated independent approver accepted exact four-document
pre-evidence Git diff SHA-256
`d0979738a664312a018922acc7d13fe8aa3fb5efe705c806cc3cef58a3ef7539`.
Transaction `rfc0027-transaction-ownership-20260730-d0979738` records the
synchronized correction without completing an RFC 0028 source task.

### 2026-07-30 Transaction Witness Inventory Correction

Descriptor preflight found that the final-authority verifier required three
transaction-witness inputs while the production prefix ended at ordinal 55.
The corrected contract adds three static transaction-witness input descriptors
at production ordinals 56 through 58. Each uses the complete context roots as
key and exactly one transaction's canonical payload digest as value. The
generic complete-context fixture moves to ordinal 59 and every later test
descriptor advances by three. No tagged key, aggregate witness input, runtime
name dispatch, or RFC 0028 status change is permitted. The descriptor generator
and its self-test reject a missing, duplicate, reordered, renamed, mistyped,
mutable, or test-tail witness row and any first test ordinal other than 59.

The user-designated independent approver accepted exact four-document
pre-evidence Git diff SHA-256
`ddd640c83235ff8d178b615f8a532f7179588b21d477ae58fe293f0ba5e87b60`.
Transaction `rfc0027-transaction-witness-inventory-20260730-ddd640c8`
records the synchronized correction without completing an RFC 0028 source
task.

### 2026-07-30 Transaction Caller Scope Correction

The closed transaction API cutover has three production mutation callers in
`compiler-session.cc`: core distribution, module structure, and the authority
refresh path. RFC 0027 T1 therefore includes that file and
`compiler-session-package-test.cc` for caller-supplied previous revisions,
closed result handling, descriptor registration, direct replacement of
authority refresh, and caller coverage in the same buildable landing. T2A
retains the session state machine, named snapshots, semantic resource
implementation, and final capability root. No compatibility overload, refresh
adapter, or RFC 0028 status change is permitted.

The user-designated independent approver accepted exact four-document
pre-evidence Git diff SHA-256
`490a96eba8bbb8b8b1f96008c864fd9d1eb5ef2781771385e2ce74682d57b5cf`.
Transaction `rfc0027-transaction-callers-20260730-490a96eb` records the
synchronized correction without completing an RFC 0028 source task.
