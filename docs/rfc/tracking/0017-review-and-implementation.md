# RFC 0017 Review And Implementation Tracker

This document records discussion, owner review, decisions, and implementation
evidence for RFC 0017. It does not approve the proposal. The RFC frontmatter
remains authoritative for status and approvers.

## Discussion Record

### 2026-07-18 Design Intake

The live Binder audit established that the current system is a deterministic
whole-module batch pipeline rather than an incremental query system.
`CompilerSession::bindSources()` performs sequential readiness selection,
repeated module-edge scans, linear dependency-output searches, and complete
module `runBinding()` calls. It has no query keys, actual-read dependency log,
cross-revision memo validation, red-green change pruning, or item-level demand
boundary.

The audit also found that the current stable metadata cannot be reused as the
query identity and equality contract. Named definition and implementation keys
contain source positions and traversal ordinals; module and module-resolution
keys retain source-position and whole-environment inputs; and export and module
interface revisions include a global semantic-context fingerprint that covers
all source snapshots. These designs are deterministic lineage proofs but would
amplify invalidation across unrelated edits.

### 2026-07-18 Prior-Art Review

The design review compared rustc queries, Salsa, Swift RequestEvaluator,
Skyframe, Buck2 DICE, Clang dependency discovery and caching, Build Systems a la
Carte, and self-adjusting computation. The common production contract is a
typed immutable key-to-value graph, actual-read dependency capture, semantic
equality with early cutoff, narrow projections, explicit inputs, single-flight
execution, and from-scratch consistency testing.

The selected design combines rustc's cross-revision red-green and projection
model, Salsa's typed tracked reads, equality backdating and durability, Swift's
named-item granularity, and DICE's single-flight concurrent execution. It uses
an in-tree C++23/zc implementation rather than Salsa through Rust FFI. Local
disk persistence is deferred until the in-memory engine passes clean-build
equivalence and hot-edit shielding gates.

### 2026-07-18 Initial Review Entry

RFC 0017 entered `REVIEW` with a complete provider, key, identity, revision,
memo, red-green, concurrency, Binder decomposition, projection, independent
verification, diagnostics, persistence, rollout, and test contract. Entry to
review is not acceptance. Every required owner must review one exact proposal
snapshot before the RFC may move to `ACCEPTED`.

The proposal snapshot submitted for owner review has SHA-256
`7a8fab33c147d6c5c6a8e3bd433c59df0f0f9c0008b6ae7f6035cf57e2ac3c91`.

### 2026-07-18 First Formal Review Return

Every required owner returned the initial snapshot and denied the mechanical
`REVIEW` to `ACCEPTED` transition. The blocking findings were:

- canonical definition, implementation, scope, import, closure, signature, and
  named-item syntax identities were not complete enough to implement without
  inventing correctness-critical preimages;
- `RevisionLocal` backdating, durability direction and downgrade, parallel
  dependency groups, verifier reads, deterministic-result dependencies, value
  eviction, and revision-bound flights left false-green paths;
- parse and diagnostic providers lacked query-local speculative collection and
  the closed source, package, build-script, module, and compilation roots;
- persisted cache admission did not demand current dependencies and its
  filesystem, partial-I/O, concurrency, and bounded-decoder adversaries were
  incomplete;
- producer/verifier independence, routing ownership, CMake direction, and
  performance budgets were principles rather than executable gates; and
- implementation ordering incorrectly placed acceptance after production work.

The initial review results were `Returned` for `task-router`, `rfc`,
`binder-checker`, `module-system`, `error-system`, `ir-backend`, `spec-audit`,
and `verification`. The RFC status history records `REVIEW` to `RETURNED` to
`DRAFT`; no production implementation was authorized from the returned
snapshot.

### 2026-07-18 Review Repair

The repaired RFC specifies the exact canonical identity codecs, physical
database ownership, semantic and revision-local inventory split, detached
named-item syntax, stable projections and rehydration, complete deterministic
result read tracking, reuse-class red-green branches, durability downgrade,
parallel validation, atomic verifier dependencies, revision-bound flights,
diagnostic occurrence and provenance contracts, current-dependency cache
admission, cache adversaries, producer/verifier build isolation, routing and
CMake direction, and reproducible performance protocol.

The repaired proposal resubmitted to every owner has SHA-256
`8a9df40851ea67b3ffd404ed15e8db2390f94ba3e53796c24467e8da2dddae7b`.

### 2026-07-18 Second Formal Review Return

The second exact-snapshot review approved `task-router`, `rfc`, `ir-backend`,
and `verification` and returned `module-system`, `binder-checker`,
`error-system`, and `spec-audit`. It found four remaining blockers:

- build-script output content still entered `CrateKey` indirectly through RFC
  0011 and RFC 0012, amplifying one generated-file edit into every downstream
  entity key;
- lazy named-definition discovery required identity admission through immutable
  snapshot membership while preserving provider purity;
- Binder skeleton and body queries had no mandatory reuse class or explicit
  semantic fact versus revision-local materialization split; and
- the diagnostic contract required source provenance even for legitimate
  invocation, build-script, resource, materialization, and bootstrap failures.

No second-snapshot approval was carried across the normative repair; every
owner must review the next exact hash.

### 2026-07-18 Second Review Repair

The RFC now replaces output-derived crate and generated-source identity with an
exact stable `BuildScriptProducerKey` and makes current output records,
generated digests, and semantic environment explicit inputs and projections.
It defines immutable active-membership queries and database-only lazy handle
materialization, mandatory Semantic Binder skeleton and body values with
RevisionLocal materializers, and a source-or-locationless diagnostic location
sum with deterministic rendering and ordering.

The second repaired proposal submitted to every owner has SHA-256
`74cebc91b0f758d77d84939a264e0e14b8df0e72d831a9e53eb8f83b36d19980`.

### 2026-07-18 Third Formal Review Return

The third exact-snapshot review retained approval for routing, RFC structure,
CMake direction, Binder semantic reuse, and the prior red-green verification
contract, but returned the snapshot for these remaining contradictions:

- one diagnostic query still used the removed output-derived build-script key;
- the repository-impact inventory omitted six RFCs named by the documentation
  plan;
- persisted decode attempted handle materialization before verification despite
  materialization being a `RevisionLocal`-only capability;
- the generated-source identity tests did not mutate every producer-plan and
  current-output field independently; and
- diagnostic collection, current provenance resolution, and rendering were
  combined without a reuse-class boundary or a canonical provenance-key sum.

### 2026-07-18 Third Review Repair

The closed diagnostic inventory now uses `BuildScriptProducerKey`, repository
impact names every affected RFC, persisted decode and verification remain
handle-free, the producer/current-output/path mutation matrix is exhaustive,
and diagnostics are split into Semantic unresolved collection, RevisionLocal
provenance materialization, and non-query driver rendering. The RFC defines the
complete source, package, build-script, and module provenance-key variants.

The third repaired proposal submitted to every owner has SHA-256
`4b134fcb4e61313690b95541beffdfec496a375478afa2fd9a62cbab3bc891e1`.

### Review Queue

| Owner | State | Required review evidence |
|---|---|---|
| `task-router` | Approved `4b134fcb...891e1` | Query subsystem, architecture gate, and implementation routing are complete |
| `rfc` | Approved `4b134fcb...891e1` | Structure, prior art, acceptance gates, tracker, and status discipline are complete |
| `binder-checker` | Approved `4b134fcb...891e1` | Semantic skeleton/body facts, materializers, projections, equality, and verifier boundaries are complete |
| `module-system` | Approved `4b134fcb...891e1` | Database ownership, identity admission, module graph, and cache capability boundaries are complete |
| `error-system` | Approved `4b134fcb...891e1` | Diagnostic facts, canonical provenance, materialization, ordering, and rendering boundaries are complete |
| `ir-backend` | Approved `4b134fcb...891e1` | Compiler target and CMake direction are complete without backend expansion |
| `spec-audit` | Approved `4b134fcb...891e1` | Cross-RFC 0004/0005/0008/0011/0012/0015 replacement contracts are complete |
| `verification` | Approved `4b134fcb...891e1` | Red-green, mutation, cache, concurrency, performance, and adversarial gates are complete |

## Decision Record

RFC 0017 is accepted. All required owners approved exact proposal snapshot
`4b134fcb4e61313690b95541beffdfec496a375478afa2fd9a62cbab3bc891e1` after
three returned review cycles and complete blocker repair. The acceptance
authorizes implementation only within the stable identity, typed query,
semantic/materialization, verifier, diagnostic, cache, and rollout boundaries
of that snapshot.

### 2026-07-25 RFC 0025 Acceptance Synchronization

The RFC 0025 `R25-02` acceptance transaction is authorized by all twelve
required-owner approvals on exact proposal SHA-256
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`.
It synchronizes RFC 0017's current normative contract without changing this
RFC's `IMPLEMENTING` status.

| Binding | RFC 0025 Task Authority |
|---|---|
| Acceptance-time RFC synchronization | `R25-02` |
| Query database, capability leases, contextual keys, and core provider/verifier graph | `R25-07` |
| Crate-keyed parse options | `R25-07P` |
| Query, invalidation, mutation, and benchmark evidence | `R25-07T` |
| Contextual core diagnostic publication | `R25-09E` |
| Configured consumer prelude publication | `R25-11` |
| Final integrated evidence | `R25-15` |

The acceptance evidence is the exact 12/12 RFC 0025 approval set. Existing
RFC 0017 implementation evidence remains accurate for the earlier slices it
records, but it does not complete any replacement above. The RFC 0025 task
states and native gates are the sole completion authority for those rows.

### 2026-07-26 RFC 0026 Acceptance Synchronization

All four RFC 0026 required owners approved proposal SHA-256
`39df5d3f11dbdcb2e95056b1cd14fd5220a19688f31a3e3180230ad465a3f84d`.
RFC 0017 now delegates the structural-input transaction, derived
selected-source and topology queries, stable graph and SCC records, failure
mapping, session barriers, and final Binder bridge to RFC 0026.
Implementation evidence is recorded only by RFC 0026 tasks `R26-05` through
`R26-09` and the dependent RFC 0025 rows.

### 2026-07-27 RFC 0027 Acceptance Synchronization

Acceptance transaction `rfc0027-accept-20260727-e2f4ba5e` synchronizes RFC
0017 to exact RFC 0027 proposal SHA-256
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`.
The current contract assigns the eight typed identity interners exclusively to
`SemanticContextCapabilityArena`, gates handle admission through typed tracked
membership after the final seal, and retains the arena and snapshot through
every capability memo and surviving lease. RFC 0017 remains `IMPLEMENTING`;
completion evidence belongs to the RFC 0027 implementation tracker.

### 2026-07-27 RFC 0028 Acceptance Synchronization

Acceptance transaction `rfc0028-accept-20260727-944b68ff` synchronizes RFC
0017 to exact RFC 0028 proposal SHA-256
`944b68ffc0aff5576d079a243ff092d7d19fba5ffed65551dda8e68adf230db4`.
The current contract defines query-database identity, kind-specific literal
descriptor metadata, explicit inventory ordinals, one typed registration path,
closed transaction and runtime failures, canonical capability rejection
envelopes, inherited sealed admission, typed capability contexts, and direct
`BoundOwnerBody` closure facts.

| Binding | RFC 0028 Task Authority |
|---|---|
| Routing and acceptance synchronization | `R28-11A`; `R28-12` |
| Query types, transactions, seal, descriptors, callers, and tests | `R28-13A` through `R28-14` |
| Stable schema and direct owner-body closure inventory | RFC 0029 `R29-12AB` |
| Provenance capability and failure bridge | `R28-16A`; `R28-16B`; `R28-16` |
| Integrated evidence and truthful status transition | `R28-17` through `R28-19` |

RFC 0017 remains `IMPLEMENTING`. Existing implementation evidence does not
complete these replacements; completion authority belongs to the listed RFC
0028 tasks and their dependency edges.

### 2026-07-27 RFC 0029 Acceptance Synchronization

Acceptance transaction `rfc0029-accept-20260727-8d393a0c` synchronizes RFC
0017 to exact RFC 0029 proposal SHA-256
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`.
The current contract uses one opaque retained token per database, a closed
move-only request result with a dedicated `CapabilityPublished` branch,
descriptor-ordinal/database/revision decoder checks, independently published
identity-site provenance, stable-identity admission, and descriptor-specific
failure contracts for the five Binder provenance capabilities.

| Binding | RFC 0029 Task Authority |
|---|---|
| Stable Binder schema and facts | `R29-12A`; `R29-12B`; atomic `R29-12AB` |
| Bounded codecs and diagnostic payload | `R29-12C`; `R29-12D` |
| Token identity, result algebra, decoder, and query-type partition | `R29-13A` |
| Identity-site provenance, stable admission, and exact descriptor failures | `R29-13B` |
| Native, mutation, race, and negative-compile gates | `R29-13C` |
| Atomic runtime source transaction | `R29-14` |
| Integrated evidence, current design, and truthful status | `R29-15` through `R29-17` |

`S1` and `S2` remain separately reviewed and land only through `R29-12AB`.
`S3` and `S6` land after that transaction. Runtime work resumes only after
both focused gate sets pass. RFC 0017 remains `IMPLEMENTING`; no RFC 0029
implementation task is complete through this synchronization.

## Implementation Tracker

| Slice | State | Required evidence |
|---|---|---|
| RFC 0029 query identity and failure closure | Pending | Completion authority is RFC 0029 `R29-12A` through `R29-17`; exact token, request-result, provenance, descriptor-failure, atomic-runtime, and final-gate evidence is not yet recorded |
| Routing and ownership | Complete | Query subsystem and architecture-gate ownership is explicit and validated |
| Stable identity replacement | In progress | RFC 0018 named-definition and implementation record replacement is complete; subordinate-parameter, semantic-import, stable-body-owner, and owner-local domains have strict canonical decoders. RFC 0019 registers detached module-body, named-item, owner-inventory, owner-syntax, and owner-provenance queries, while persistent query inventory ownership, owner binding, and remaining materialization migration stay open |
| Revision-domain separation | In progress | Database revisions, durability, reuse classes, semantic query values, stable semantic-import keys, revision-local materialization values, detached module-body syntax, module-body provenance, semantic owner-body syntax, and revision-local owner-body provenance are distinct; owner binding, materialization, identity-store ownership, and call-site migration remain open |
| In-memory query runtime | Complete | Typed registration, atomic set/erase transactions, snapshots, required reads, input-only optional probes, `Present` and `Absent` dependency observations, canonical parallel dependency groups, red-green validation, equality backdating, durability, cycles, cancellation, single-flight, eviction metadata retention, and observability are covered by native tests |
| Session and module migration | In progress | `CompilerSession` owns the sole frontend scheduler and query database, atomically stages compilation, package graph, topology, source, exact module-resolution inputs, and the readiness-gated complete active-definition authority map. It demands module order, semantic module candidates, module-body values, and every active named-item syntax and provenance value from a ready snapshot. Owner projections are registered and tested but are not yet production session roots; tracked request derivation, owner-body binding and materialization, and a reusable edit lifecycle remain open |
| Binder aggregate query | Blocked by prior slices | Existing complete Binder behind one verified query with clean-build differential evidence |
| Projection firewalls | Blocked by Binder aggregate query | Export, name, header, signature, visibility, import, closure, and diagnostic projections with mutation tests |
| Item-level Binder split | Blocked by projections | Immutable skeleton and named-item body providers plus independent domain verification |
| Production query publication | Blocked by item-level split | One verified query publication path, stable keys, and projection-only downstream reads |
| Local persistence | Blocked by every in-memory gate | Canonical envelope, bounded decoder, atomic writes, allowlist, corruption-as-miss, pruning, and verifier tests |
| Landing | Blocked by all prior slices | Sanitizer, full tests, deterministic differential corpus, architecture gates, format, and performance budgets |

### 2026-07-18 Routing And Architecture Gate Evidence

The first implementation slice assigned query runtime and incremental identity
work to `module-system`, assigned the gate, adversaries, corpus, runner, and
baseline to `verification`, synchronized the manifest, routing matrix, owner
specifications, and repository summary, and added
`check-incremental-query-architecture.py` with live and adversarial modes.

Evidence:

- sanitizer configure and the 469-action sanitizer build completed;
- CTest `incremental-query-architecture` passed;
- CTest `incremental-query-architecture-negative` passed;
- the direct architecture check and self-test passed;
- the RFC gate and format gate passed; and
- scoped `git diff --check` passed.

### 2026-07-18 Stable Identity Replacement Progress

The first stable-identity implementation series separated build producer
identity from current build output, removed generated content and source
provenance from entity keys, and made ordinary crate identity available before
build-script execution. `BuildScriptProducerKey` now derives only from the
verified producer plan, while `ArtifactFingerprint` detects changes to the
current output record. Generated `SourceFileKey` identity contains only the
producer key and logical path. `ModuleKey` contains only its crate and canonical
module path; revision-local module-to-source provenance is published by the
verified module graph and independently checked against the structural module
catalog.

The final target crate graph now consumes `VerifiedBuildScriptPlan` rather than
completed build results. An end-to-end mutation test executes equal plans with
different generated bytes and proves that artifact fingerprints differ while
crate keys remain equal before and after execution. Explicit source provenance
was carried into Binder and Checker verification inputs instead of being
recovered from `ModuleKey`.

The identity architecture gate now runs under the project-scope configured
Python interpreter and rejects output-derived build identities, mutated domain tags,
generated-source content digests, source- or position-bearing module keys,
missing module-source projections, and output-dependent crate finalization.
Every new rule has an adversarial self-test.

Evidence:

- the affected identity, Binder, Checker, driver, and test targets build with
  the sanitizer preset;
- focused build-script, source, definition, package-request, and compiler-session
  tests pass;
- the output-independence compiler-session mutation test passes;
- identity and incremental-query architecture checks pass; and
- identity and incremental-query architecture adversarial self-tests pass.

This slice is not complete. Semantic module-resolution policy and request keys,
requester ancestry, and catalog-path-bucket values have fixed vectors and
architecture mutations, and all module-dependency callers use the single
identity-layer dependency-kind enum. The resolver adapter retains the admitted
narrow values and resolves through indexed exact path-bucket reads. Production
source snapshots, compilation options, package and crate graphs, and module
topology now enter one atomic query input transaction and record actual parallel
dependency reads. Manifest, build-script, module-search, prelude, and
module-resolution input families are not yet query-backed. The
canonical header schema, name records, complete sixteen-variant type codec, and
nine-field overload record are implemented and covered by fixed vectors and
mutation gates. Explicit and omitted Unit results share one stable encoding.
The overload digest authority, canonical implementation header, and independent
type, callable, and implementation AST producers are also implemented.
`DefinitionKey` and `ImplKey` now derive from complete position-independent
canonical records, subordinate parameter registries are separate from the
definition registry, and import/re-export bindings use
`SemanticImportBindingKey` instead of alias `DefId` values. RFC 0019 now supplies
the closed owner model for module-owned statements and anonymous-callable
subordinate entities. Its Phase 2 module-body syntax and provenance queries are
registered and demanded in production. Owner-body materialization, complete
query ownership beyond Phase 2, and identity-store migration are still required
before revision-domain separation can complete.

### 2026-07-19 Narrow Module Input Evidence

The accepted requester-ancestry and module-catalog-bucket query records are now
implemented as independently admitted identity-layer values. The current
resolver adapter constructs them during environment freeze, indexes exact
module and bucket keys, and no longer scans the complete catalog while
resolving one request. Architecture mutations prove that the exact input fields,
admission rules, retained values, indexed read, and no-scan boundary are
enforced. This advances the stable-identity prerequisite without changing the
tracker state: explicit query input transactions, `changedAt` shielding, and
dynamic dependency capture remain blocked behind the remaining RFC 0018
definition and implementation key replacement.

### 2026-07-19 Canonical Header Producer Evidence

RFC 0018 canonical syntax production is now executable rather than represented
only by identity-layer value objects. The Binder maps every canonical type
variant, all callable header forms, and both implementation source forms into
the exact admitted values. Generic names are alpha-normalized by binder depth
and ordinal, empty current binders reserve depth, callable receivers and
results normalize before digest authority construction, implementation
obligations canonicalize across inline and where forms, and positive safe
marker implementations remain admitted for later signature classification.

Focused sanitizer tests and the identity architecture positive and adversarial
gates pass. The verifier is mechanically forbidden from reusing these producer
classes. Complete definition and implementation records, owner validation,
site and duplicate-bound provenance, digest admission, registry replacement,
and the compiler-wide named-identity consumer cut are implemented. The slice
remains in progress because owner-body binding, materialization, complete
production query ownership, and remaining consumer migration are not yet
complete.

### 2026-07-19 In-Memory Runtime And Production Topology Evidence

The query runtime now implements the accepted in-memory execution contract.
Typed input and derived kinds use bounded canonical codecs. Complete input
transactions support set and erase without tombstones, advance one database
revision atomically, reject frozen transactions, and preserve deterministic
ordering. Snapshots record actual reads, validate dependencies red-green,
backdate equal recomputations, track minimum durability, reject cycles, support
cancellation, provide one single-flight computation per key and snapshot, and
retain dependency metadata after value eviction. Observability distinguishes
execution, green reuse, equal recomputation, changed recomputation, and
eviction. Parallel demand returns values in caller-key order while recording
one canonically ordered dependency group.

The query database no longer creates a fixed private worker pool. Its
constructor requires a borrowed `basic::ThreadPool` whose lifetime encloses the
database. `CompilerSession` constructs and owns that scheduler before the query
database, so query dependency groups share one frontend concurrency budget and
database destruction cannot outlive the worker authority. Both the
CompilerSession and incremental-query architecture gates reject a private query
scheduler or missing constructor injection, including adversarial mutations.

The production module-graph family stages `SelectedModuleCatalogInput` and
`ModuleDependencySiteInput` with the complete resolver authority in one
verified transaction. It derives selected sources, active modules, dependency
sites, requests, resolved dependencies, the stable `ModuleGraph`, and the
independently reconstructed `ModuleGraphScc`. `CompilerSession` owns and
registers the database, commits the complete structural authority atomically,
and demands the verified graph and SCC before Binder execution. Stable
module-key indexes join parsed inputs, dependencies, and exact active identity
membership without caller-assembled topology capabilities.

The same complete input root includes low-durability `SourceSnapshot` values
keyed by stable source identity. Each value contains bounded source bytes and a
SHA-256 digest that is recomputed during decode. Staging rebuilds the exact
registry snapshot set, erases stale source keys, and independently checks key,
digest, content, and cardinality before opening the transaction. Every selected
module source must reference exactly one staged source snapshot; missing,
replaced, or duplicate cross-root edges are rejected before commit.

The input root also includes a medium-durability `CompilationOptions` value.
It records the exact verified host and target registry selections, including
panic strategy, together with the semantic language options. Its independent
decoder admits the complete canonical target-selection records, requires one
nonzero registry revision shared by host and target, closes the profile,
target, panic, and boolean domains, and rejects truncation, trailing bytes, and
oversized selections. Equal recommits are backdated while a semantic option
change advances `changedAt`. The session independently compares the staged
projection with the verified package request before the atomic transaction.

Stable module and source query keys no longer admit arbitrary bounded byte
strings. They compose the identity-layer `ModuleKey` and `SourceFileKey`
decoders, require exact envelope consumption, and require byte-for-byte
canonical re-encoding. Architecture mutations and native regressions reject
malformed nonempty keys and trailing data at every module-key, source-key, and
selected-source boundary.

The package-root input boundary now uses a non-empty canonical
`PackageRootSetQueryKey` composed from strict `PackageKey` decoders. Its
medium-durability `ActiveCrates` value is a bounded sorted unique set composed
from strict `CrateKey` decoders. Session staging independently reconstructs the
exact verified root package set and final crate-graph membership, removes a
stale prior root key, and publishes the replacement in the same transaction as
the compilation, source, and module roots. Equal crate sets are backdated;
membership replacement advances `changedAt`.

Each active crate now owns separate low-durability `ActiveSources` and
`ActiveModules` inputs. Both are strict bounded canonical sets keyed by the
complete `CrateKey`; session staging reconstructs them independently from the
frozen source registry and verified module graph, rejects an empty module
membership, removes every stale per-crate root, and commits the complete
replacement atomically. The module-order query no longer reads a
compilation-wide active-module authority. It reads `ActiveCrates`, demands the
per-crate active-module sets as one parallel group, verifies every module's
crate membership, then demands outgoing dependencies as a second parallel
group. Its independent verifier repeats those tracked reads and reconstructs
the complete unique module set before checking the retained order or failure.

The same package-root-set key now owns a medium-durability `PackageGraphInput`.
Its value retains the complete resolved package-key and dependency-edge sets,
the final target-selected package-edge projection, and the complete selected
crate-key and crate-edge graph. All package, crate, and edge records use strict
compositional identity decoders with exact canonical re-encoding. Admission
rejects duplicate or dangling records, selected edges outside the resolution,
crate endpoints outside the active graph, package/crate endpoint disagreement,
selected edges without a crate-edge projection, self-edges, cycles, truncation,
trailing data, and resource-limit violations. `CompilerSession` independently
compares every projected set against `ResolutionOutput` and
`VerifiedCrateGraph`, proves every package root occurs exactly once, removes a
stale graph root, and stages the value in the same transaction as all other
compiler inputs.

The import/re-export identity cut is complete across Binder, Checker, and
driver consumers. A selected namespace slot carries one
`SemanticImportBindingKey`; local exports retain their canonical binding target;
signature selection and root authorization accept only definition or semantic
import targets. The definition inventory, frozen inventory, codecs, validators,
and interface projectors contain no alias-definition identity path.

Evidence:

- query database, concurrency, red-green, eviction, and observability CTests
  pass under the sanitizer preset;
- module topology chain, diamond, malformed input, semantic failure, equal
  recommit, and red-green tests pass;
- source snapshot fixed-vector, corruption, truncation, trailing-data,
  oversize, equal-recommit, changed-content, stale-erasure, and selected-source
  closure tests pass;
- compilation-option closed-codec, invalid-panic, invalid-boolean, truncation,
  trailing-data, oversize, equal-recommit, and changed-option tests pass;
- active-crate package-root, strict package and crate codec, duplicate-set,
  trailing-data, equal-recommit, and membership-replacement tests pass;
- per-crate active-source and active-module closed codecs, crate-key admission,
  duplicate rejection, equal-recommit, replacement, parallel-read, and
  module-to-crate membership tests pass;
- package-graph package/crate/edge codecs, dangling and outside-resolution
  rejection, exact selected-edge projection, cycle rejection, equal-recommit,
  replacement, CompilerSession staging, and stale-root architecture mutations
  pass;
- compositional package, crate, source, module, body-owner, owner-local, and
  anonymous-owner-local decoder tests pass under the sanitizer preset;
- CompilerSession dependency-first and complete package tests pass;
- semantic import identity, Binder import/re-export, cross-module facts, module
  interface, and definition-key tests pass;
- identity, Checker, Binder-fact-schema, and incremental-query architecture
  checks and adversarial self-tests pass; and
- format and diff checks pass for the completed slices.

This evidence completes only the in-memory runtime row and advances session and
stable-identity rows. It does not implement the Binder aggregate query,
projection firewalls, item-level providers, diagnostic query roots, persistent
cache, or repeated-edit production lifecycle.

# RFC 0018 Stable Identity And Resolution Overlay

RFC 0018 was accepted after all nine owners approved exact REVIEW SHA-256
`bdcbee8761d5476822cbe5bb2548332ad36e4d5f507c38e74d06751c6f444379`.
It supplies the mandatory digest-based definition and implementation records,
one-authority occurrence bridge, semantic module-resolution key, and narrow
requester-ancestry and catalog-path input queries. The implementation has
completed the named-item and owner-local record portion of stable identity and
the in-memory query runtime. Session migration now owns the production
compilation-option, package-graph, active-crate, source-snapshot, and
module-topology roots.
RFC 0019 Phase 2 is complete: `ParseSource`, stable named inventories,
module-body syntax, and revision-local module-body provenance are registered and
demanded through the production session. RFC 0020 adds input-only tracked
absence, the readiness-gated complete active-definition authority map, and
independently verified named-item syntax and provenance. RFC 0019 Phase 3 now
adds canonical owner inventories, semantic owner-body syntax, and
revision-local owner-body provenance. Owner-body binding and materialization
remain open.
Manifest inputs, the complete revision-domain migration, Binder queries, and
diagnostic migration also remain open.
