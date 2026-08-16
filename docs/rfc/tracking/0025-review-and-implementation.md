# RFC 0025 Review And Implementation Tracker

## Discussion Record

### 2026-07-25 Core Library Architecture Audit

The live audit found a source-backed seed under `products/zomcore`, containing
ordinary ZOM declarations for `Copy` and `Linear`. The seed is packaged as a
normal release with `version = "0.0.0"`. Production `CompilerSession`
materializes its query-backed core authority and projects the verified marker
policy into Checker, including the configured prelude and the canonical
`Copy` and `Linear` definitions.

The architecture review separates four authorities:

- language primitives owned by the language specification and compiler;
- user-visible core APIs owned by ZOM source;
- closed intrinsic lowering owned by verified compiler IR; and
- platform services owned by a narrow verified runtime ABI.

Rust, Swift, Zig, and Go primary sources were reviewed. The candidate adopts an
allocation-free core boundary, a target-language standard library with a
separate runtime, a toolchain-supplied source module, and a small language
universe. It rejects package-version selection for the mandatory toolchain core
and rejects name-based intrinsic discovery.

The first review candidate,
`09599f7da6b47cdfbc19b98f97fdf32627d105501c787523cbd9887dc37286bd`,
was rejected. Review found that the content digest was embedded in stable unit
identity while the RFC also promised narrow incremental projection reuse, the
proposed `CrateKey` omitted the complete RFC 0011 compilation configuration,
runtime scope was too narrow, role locators were outside authenticated
lineage, the failure algebra was open, and the RFC 0024 replacement transaction
was not mechanical. No approval is retained.

The revised draft separates stable toolchain identity from verified
distribution content, preserves `CompilationConfigKey`, authenticates role
locators, closes failures and diagnostics, fixes prelude cardinality, removes
the unused intrinsic source token, and expands the atomic accepted-RFC
transaction.

The second review candidate,
`1385d71c6095897ac79067f39e16e8b428b67830855e83a5f01c948deb1c94a6`,
was rejected. Review found that its exact root source used multiple module
declarations forbidden by the current grammar, and one context-mismatch case
had incompatible pre-publication and post-publication diagnostic precedence.
The revised draft uses the valid single-declaration root, regenerates every
affected hash and golden vector, and splits input-context from verified-state
failure. No approval from the second candidate is retained.

The third review candidate,
`86492ce69bd60f68e0a697f706f63808fb43911c9b0fd1f97dc945204dbee176`,
was rejected. Governance and specification audits found no high-severity
issue, but production-path review found that the proposed lowering union did
not follow the existing move-only checked-module ownership path, preparatory
host-library dependencies were omitted from the core-edge matrix, the closed
module-search-root union had no toolchain-core alternative, and the role
publication schema named an undefined authority instead of retaining RFC
0024's verified authority. The revised draft uses the live checked-module,
HIR, MIR, ownership-overlay, module-interface-lineage, stable-body-owner, and
standard-marker-authority contracts; it covers the complete RFC 0008 host
closure and defines verified inventory-backed core module discovery. No
approval from the third candidate is retained.

The fourth review candidate,
`0b3212cffda8f8876d4605c1022988a05e4acd43e83a2d2a2fe54f81f0f02d93`,
was rejected. It closed the third candidate's production ownership, host
closure, search-root representation, and authority-type findings, but its
module catalog required frozen source and module identities before parsing and
declaration validation. The revised draft separates a pre-parse structural
`AdmittedCoreSourceCatalog` from the post-validation, post-freeze
`VerifiedCoreModuleCatalog`, so rejected source never requires identity
registry rollback. No approval from the fourth candidate is retained.

The fifth review candidate,
`317a555494ceba8944a8d72330b6aa63020fc5ebba69aa4b2ad840e1bd35dcb7`,
was rejected. Product-path and governance review cleared the two-stage catalog
and prior findings, while specification review found an incomplete
user-package dependency-edge codec, unmapped catalog and lowering failures, an
insufficiently mechanical RFC 0008 replacement transaction, and a duplicate
Mermaid node identifier. The revised draft closes both dependency-origin
encodings, maps every new producer condition into the typed failure algebra,
enumerates the exact RFC 0008 surfaces replaced at acceptance, and assigns
unique bootstrap node identifiers. No approval from the fifth candidate is
retained.

The sixth review candidate,
`c6ef28b5e313be36ba81f5027ec08ed0db345bdf045efce514225774ab45e237`,
was rejected. It closed dependency-edge encoding, RFC 0008 synchronization,
catalog/lowering producer enumeration, and the bootstrap graph, but conflated
source-semantic body rejection with the invariant-only RFC 0010 checked-module,
HIR, MIR, and RFC 0007 ownership result branches. The revised draft gives
those exact identity/IR invariant results their own issue and cause domains,
maps stable owners deterministically, preserves their native fatal
diagnostics, and reserves core-specific verifier disagreement for failures
outside an existing typed result algebra. No approval from the sixth candidate
is retained.

The seventh review candidate,
`835342ed429b93420cfb2444f2c881e9bc09f9680d3f4fbf9553bfbb20b3846b`,
was rejected. It separated source rejection from pipeline invariants and added
the checked-module boundary, but did not partition heterogeneous invariant
owners, conflicted with RFC 0010 invalid-descriptor normalization, and relied
on an unspecified whole-sequence encoding. The revised draft groups IR facts
by expanded stable coordinate, frames normative expanded sort-key bytes,
defines a complete local identity-invariant record, preserves one-step RFC
0010 normalization, and retains RFC 0011 plus codec-first RFC 0010 diagnostic
precedence. No approval from the seventh candidate is retained.

The eighth review candidate,
`edcadc6f3395b71eea077cc490f5e4d8c077b62f79147bd91f6afd7a745d8225`,
was rejected. Product and governance review cleared its grouped pipeline
invariant framing, while specification review found that the live expanded
identity-invariant tag domains were not mechanically synchronized with RFC
0011. The revised draft declares every phase, kind, and API-site enumerator
with its exact tag and adds the RFC 0011 replacement table, including
`DigestCollision` sharing `ZOM9921` with `NonCanonicalEncoding`. No approval
from the eighth candidate is retained.

The ninth technical candidate,
`f81c115f06bdc6f40575e18e828be0c3f30b56370fc6e8d9b00d6d2bb4ef841c`,
received no `BLOCKER` or `MAJOR` from the production-path,
specification-alignment, or governance/unversioned adversarial reviews.
Those scoped clearances authorize promotion to `REVIEW`; they are not the
required-owner approvals for `ACCEPTED`. The status-only promotion creates the
exact review hash
`a9d15b034cf646bc46f2bfed01c4ec1d133afc5fb3edc7c405bb93422457a35c`
that every required owner must cite. No implementation is authorized by this
tracker.

### 2026-07-25 Prior RFC Owner Exact-Hash Approval

The `rfc` owner independently reviewed
`a9d15b034cf646bc46f2bfed01c4ec1d133afc5fb3edc7c405bb93422457a35c`
and returned `APPROVE` with no `BLOCKER`, `MAJOR`, or `MINOR` findings.
The review covered RFC governance, scope, prior art, status discipline,
acceptance boundaries, and the atomic replacement contract. The native RFC and
internal-versioning checks passed, the final hash matched, and the worktree
remained clean.

This approval covers only the `rfc` owner. It does not approve any other
required owner, move the proposal to `ACCEPTED`, or authorize implementation.
The normative revision recorded below invalidates this approval.

### 2026-07-25 Core Owner Review And Bootstrap Revision

The first required-owner review of
`a9d15b034cf646bc46f2bfed01c4ec1d133afc5fb3edc7c405bb93422457a35c`
returned:

- `binder-checker`: `REJECT` with one `BLOCKER`. Signature checking required
  final standard-marker authority before the core signatures needed to build
  that authority existed.
- `module-system`: `REJECT` with two `MAJOR` findings. Multi-context core
  publication had no exact session lifecycle, and the named core query
  projections lacked complete RFC 0017 descriptors.
- `runtime-memory`: `APPROVE` with no finding for that exact hash.

The revised candidate closes the bootstrap dependency with an independently
verified role seed, a separate closed core-signature input, a golden seed
revision, and an authority build that does not depend on ordinary consumer
graphs. It makes one `CompilerSession` own one semantic context and query
database, publishes an exact projected-core library set, and defines one
explicit distribution input plus three derived semantic queries with complete
readiness and invalidation contracts.

The revised exact review hash is
`d23974d943b68a2505812cf31b398ceefffb33161917c27a354c52a824d620cb`.
Every approval for the prior hash, including the `rfc` and `runtime-memory`
approvals, is invalid. All required owners must review the revised hash.

### 2026-07-25 Core Owner Rejection And Query-Graph Revision

The second core-owner review of
`d23974d943b68a2505812cf31b398ceefffb33161917c27a354c52a824d620cb`
returned:

- `binder-checker`: `REJECT` with one `BLOCKER` and two `MAJOR` findings. The
  RFC reused RFC 0015's whole-session marker inventories before ordinary
  modules existed, could not encode role-less seed failures, and did not give
  the seed verifier the marker module's verified binding/export surface.
- `module-system`: `REJECT` with two `MAJOR` findings. Per-role invalidation
  contradicted aggregate authority revisions, and providers read an untracked
  policy table, final authority, interface publication, and core graph.
- `runtime-memory`: `REJECT` with two `MAJOR` findings. Each projected library
  attempted to own the same move-only distribution and the role-seed coordinate
  algebra could not encode its context-wide failures.

The next candidate gives the orchestrator sole ownership of the move-only
distribution and stores only its digest in library publications. It splits
context-wide and role-specific seed coordinates, makes the marker binding
surface explicit, introduces core-scoped marker shape and policy capabilities,
and delays RFC 0015 whole-session inventories until ordinary binding. It uses
`CoreSemanticContextFingerprint` for stable core equality and closes the
tracked dependency graph with one distribution input, stable core graph and
role-seed projections, one revision-local core-signature query, stable export
and prelude projections, and one aggregate role-authority projection. No
provider reads a frozen side table or final authority.

The revised exact review hash is
`d60238011a0235243b60380ed925ca78a0276927bb8881032d014b92cbde27f0`.
Every review result for `d23974d943b68a2505812cf31b398ceefffb33161917c27a354c52a824d620cb`
is historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Core Owner Rejection And Bootstrap-Materialization Revision

The third core-owner review of
`d60238011a0235243b60380ed925ca78a0276927bb8881032d014b92cbde27f0`
returned:

- `binder-checker`: `REJECT` with one `BLOCKER` and one `MAJOR` finding. The
  bootstrap signature record could not reconstruct the complete imported
  signature view without cycling through the ordinary final interface, and
  free-standing materializers retained untracked registry, bound-module, and
  semantic-store dependencies.
- `module-system`: `REJECT` with three `MAJOR` findings. The stable core
  fingerprint still included broad distribution and policy inputs, the
  package-only root-set key could not represent a singleton toolchain core,
  and the materialization steps were not revision-local query descriptors with
  exact tracked read sets.
- `runtime-memory`: `REJECT` with two `MAJOR` findings. The admitted catalog
  duplicated ownership of the move-only source-root capability, and the source
  snapshot text contradicted the existing byte-owning `SourceSnapshot` input.

The next candidate makes the catalog logical and leaves sole source-root and
original-snapshot ownership with the orchestrator while each session owns its
exact tracked source-input bytes. It adds a complete handle-free bootstrap
interface record, a core-only imported-signature view, a distinct finalized
core interface, and an exhaustive ordinary-or-core interface source. It
narrows stable core fingerprints to the projected core key, introduces
`CompilationRootSetQueryKey`, and defines six semantic projections plus four
ephemeral revision-local materialization queries with exact provider and
verifier reads. The initial final publication is declaration-only and carries
no fabricated checked-body, HIR, MIR, ownership, or backend capability.

The first pre-review hash was
`c1501a5ced9bd53dcdd09fee037eca73b63601ff040a35295fda5523f5eea648`.
Before any owner result was retained, the native gate pass found that the RFC
misclassified RFC 0024's not-yet-implemented ownership architecture script as
an existing gate. The wording now distinguishes the existing identity,
incremental-query, and IR gates from the ownership gate that must exist before
`LANDED`. This normative correction invalidated the pre-review hash.

The revised exact review hash is
`1e38c2b75b65f71db4910d35d114d414c83a76635bbfda194bc8bb8ce34d476f`.
Every review result for `d60238011a0235243b60380ed925ca78a0276927bb8881032d014b92cbde27f0`
is historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Core Owner Rejection And Capability-Memo Revision

The fourth core-owner review of
`1e38c2b75b65f71db4910d35d114d414c83a76635bbfda194bc8bb8ce34d476f`
returned:

- `binder-checker`: `REJECT` with two `BLOCKER` findings. The current RFC 0017
  canonical-byte query runtime could not own or transfer the proposed
  move-only materialization values, and the declared read sets could not
  construct inputs that required an untracked module catalog, registry family,
  and semantic type store.
- `module-system`: `REJECT` with one `MAJOR` finding. The revision-local graph
  still lacked typed dependencies for the module catalog and distribution
  needed by its declared builder inputs.
- `runtime-memory`: `REJECT` with one `MAJOR` finding. Query memos and final
  library publication both claimed ownership of the same move-only authority
  and final-interface capabilities.

The next candidate replaces RFC 0017's canonical-byte-only completion path
with a closed revision-local capability-memo alternative. The query memo is the
sole owner, repeated demands and final publication receive snapshot-bound
`QueryCapabilityLease` values, and capability cloning, equality, backdating,
and persistence are prohibited. The initial role-seed and signature inputs no
longer consume a whole module catalog, registry family, or semantic type store;
they use tracked stable distribution and graph records, bound and skeleton
capabilities, active identity materialization, and the closed type-free initial
signature algebra. Exact distribution, active-membership, definition, syntax,
and lease reads are enumerated.

The revised exact review hash is
`208d91db8f94a8e6ac7427eb7f1db4d98b975a2c16007983e14b745692e7d689`.
Every review result for `1e38c2b75b65f71db4910d35d114d414c83a76635bbfda194bc8bb8ce34d476f`
is historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Core Owner Rejection And Lease-Lifetime Revision

The fifth core-owner review of
`208d91db8f94a8e6ac7427eb7f1db4d98b975a2c16007983e14b745692e7d689`
returned:

- `binder-checker`: `REJECT` with one `BLOCKER` and one `MAJOR` finding.
  Eviction could create a second capability generation for the same snapshot,
  and one role-seed paragraph still required an undeclared catalog check.
- `module-system`: `REJECT` with one `MAJOR` and one `MINOR` finding. A lease
  pinned only its direct memo rather than the revision-local dependency memos
  behind retained borrows, and the same stale catalog wording remained.
- `runtime-memory`: `REJECT` with one `MAJOR` finding. The lease schema did not
  structurally retain the snapshot and semantic-context owners required by
  borrowed bound-module and repository state.

The next candidate makes every capability memo `zc::AtomicRefcounted`, retained
and unique for its complete snapshot. It adds acyclic semantic-context and
snapshot capability arenas, retains every revision-local capability dependency
memo in canonical key order, forbids unanchored borrows, and defines reverse
dependency teardown with leases that may safely outlive query-database or
session lookup ownership. All existing revision-local queries that return
handles, views, references, AST objects, or other process-local capabilities
switch to the same memo-and-lease alternative; `VerifyBoundModule` and
`MaterializeModuleSkeleton` are explicit capability dependencies. The stale
catalog check is replaced by graph and active-membership verification.

The revised exact review hash is
`8e0c8e4e3f1bd9d2461b555a16b75899694c02ee618eb4567147e9f93593da6b`.
Every review result for `208d91db8f94a8e6ac7427eb7f1db4d98b975a2c16007983e14b745692e7d689`
is historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Core Owner Exact-Hash Approval

The `binder-checker`, `module-system`, and `runtime-memory` owners independently
reviewed
`8e0c8e4e3f1bd9d2461b555a16b75899694c02ee618eb4567147e9f93593da6b`
and each returned `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR` findings.

The approvals cover the acyclic role-seed and signature bootstrap, complete
bootstrap interface and imported-signature projection, narrow stable query
values, exhaustive compilation roots, exact provider and verifier read sets,
memo-owned move-only capability publication, transitive dependency and arena
lifetime retention, distribution and source ownership, and declaration-only
final core publication.

These approvals cover only the three named owners for this exact RFC hash.
Every remaining owner is still pending. No implementation or `ACCEPTED` status
is authorized yet.

### 2026-07-25 Peripheral Owner Rejection And Declaration-Only Revision

The first `lexer-parser`, `error-system`, and `ir-backend` review of
`8e0c8e4e3f1bd9d2461b555a16b75899694c02ee618eb4567147e9f93593da6b`
returned:

- `lexer-parser`: `REJECT` with one `BLOCKER`. Removing the `INTRINSIC` lexer
  token did not include the canonical `ZomParser.g4` `pathSegment` reference in
  the atomic migration.
- `error-system`: `REJECT` with one `MAJOR` finding. The declaration-only core
  retained unreachable body, checked-module, HIR, MIR, and ownership failure
  alternatives and test requirements.
- `ir-backend`: `REJECT` with one `MAJOR` finding for the same unreachable
  pipeline contract. The initial core has no legal producer for those
  alternatives and must not fabricate one.

The next candidate adds `ZomParser.g4` and the exact `pathSegment` alternative
deletion to the intrinsic-token transaction. It removes the unreachable body
and pipeline issue tags, coordinates, cause domains, framing, diagnostics,
accepted-RFC synchronization, rollout steps, repository impact, and core test
requirements. RFC 0011 identity invariant results remain in their native typed
algebra. The initial core ends at declaration-only final-interface and library
publication; its architecture gate rejects any core body-checker, checked
module, HIR/MIR, ownership-overlay, or backend producer.

The revised exact review hash is
`85755770706fea5549d931ae80d30643c5c6109035d5e32d4c004a4b9e18f504`.
The normative edit invalidates the three core-owner approvals for
`8e0c8e4e3f1bd9d2461b555a16b75899694c02ee618eb4567147e9f93593da6b`.
All required owners must review the revised hash.

### 2026-07-25 Core Owner Rejection And Stable-Identity Revision

The core-owner re-review of
`85755770706fea5549d931ae80d30643c5c6109035d5e32d4c004a4b9e18f504`
returned:

- `binder-checker`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `runtime-memory`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `module-system`: `REJECT` with two `MAJOR` findings. The proposed
  `VerifiedCoreModuleCatalog` was an unowned handleful session side state
  outside the query graph, and the RFC 0011/RFC 0018 replacement transaction
  did not mechanically enumerate the handle hierarchy, user-package encoding,
  dependency origin, transitive identity keys, allocation phases, dumps,
  fixed vectors, or downstream query-key cascade.

The next candidate deletes the post-freeze handleful catalog and publishes
only the stable RFC 0017 active-source, active-module, path-bucket, ancestry,
root-set, and module-graph inputs through one independently verified atomic
transaction. Revision-local handles can be created only by the four tracked
materialization queries. The RFC 0011 and RFC 0018 synchronization tables now
define the exhaustive compilation-unit hierarchy, both union branches,
dependency origins, transitive key replacement, fingerprint input,
allocation phase, deterministic dump, fixed-vector, root-set, query-key,
trace, mutation, and one-step no-compatibility cutover.

The revised exact review hash is
`91e587026cdf827767f0cb0a3b9e236ed10d02b56bcf8682bb6d3bc426172806`.
The normative edit invalidates every approval for
`85755770706fea5549d931ae80d30643c5c6109035d5e32d4c004a4b9e18f504`.
All required owners remain pending for the revised hash.

### 2026-07-25 Core Owner Rejection And Graph-Staging Revision

The core-owner review of
`91e587026cdf827767f0cb0a3b9e236ed10d02b56bcf8682bb6d3bc426172806`
returned:

- `binder-checker`: `REJECT` with one `BLOCKER` and two `MAJOR` findings.
  Active membership and root-set publication order contradicted readiness,
  the four-core-materializer exclusivity contradicted existing binder
  capability queries, and module-input transaction verifier disagreement had
  no failure mapping.
- `module-system`: `REJECT` with two `MAJOR` findings. The RFC incorrectly
  treated derived `ModuleGraph` as an atomically committed input and repeated
  the overbroad materializer exclusivity rule.
- `runtime-memory`: `REJECT` with one `MAJOR` finding for the same
  materializer-access contradiction.

The next candidate defines one order: commit pre-parse distribution, snapshot,
option, and search-root inputs; derive active crates and sources; parse and
select structural modules; atomically commit only explicit module-graph
prerequisites; derive and independently verify active modules, dependencies,
graph, and SCC; then open binder and materialization readiness. A failed
snapshot may contain committed stable inputs but creates no identity handle or
verified core artifact and is discarded without rollback.

The revision also distinguishes direct `materializeActive` authority, tracked
lease access, and forbidden handle access. Existing registered RFC 0017
`MaterializeModuleSkeleton` and `VerifyBoundModule` queries remain permitted
alongside the four new core-specific materializers, while Semantic and
Persisted providers, side tables, and free-standing helpers cannot materialize
handles. Module-input transaction and module-graph verifier disagreement maps
exactly to `VerifierDisagreement` at the `Context` coordinate.

The revised exact review hash is
`9a82efd599407658f5ee270941a5a5cf2cd2aeed0b56da649c0dbd07ec7cbded`.
All review results for
`91e587026cdf827767f0cb0a3b9e236ed10d02b56bcf8682bb6d3bc426172806`
are historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Core Owner Rejection And Global-Snapshot Revision

The core-owner review of
`9a82efd599407658f5ee270941a5a5cf2cd2aeed0b56da649c0dbd07ec7cbded`
returned:

- `binder-checker`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `runtime-memory`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `module-system`: `REJECT` with two `MAJOR` findings. Unequal projected core
  crates could be staged into different database revisions and then assembled
  into one library set, and `VerifiedCoreLibrary` directly owned a handleful
  `VerifiedModuleGraph` without a query memo or lease owner.

The next candidate derives the complete projected-core set before query demand,
commits every projection's pre-parse inputs in one transaction, commits every
projection's graph prerequisites in one second transaction, and uses the
resulting `finalCoreSnapshot` for all graph, binder, semantic, and
materialization queries. One global barrier opens only after every projected
graph verifies, and every library-set lease must carry the same final database
revision.

The fixed singleton `CompilationUnitQueryKey` is deleted and
`CompilationOptions` is keyed by complete `CrateKey`, allowing unequal
projections to carry distinct exact options without ambient session state.
`VerifiedCoreLibrary.graph` now stores the handle-free
`CoreModuleGraphRecord`; no fifth graph materializer or session-side handle
owner is introduced. Native tests require at least two unequal projections,
two session-wide input transactions, one final snapshot, same-revision leases,
and rejection of mixed-snapshot assembly.

The revised exact review hash is
`f9db0fd5445d8800a5cefc25953586445d2d2f1e64ce7f6ec0d7c238d0135d12`.
The two approvals and one rejection for
`9a82efd599407658f5ee270941a5a5cf2cd2aeed0b56da649c0dbd07ec7cbded`
are historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Core Owner Rejection And Final-Snapshot Input Revision

The core-owner review of
`f9db0fd5445d8800a5cefc25953586445d2d2f1e64ce7f6ec0d7c238d0135d12`
returned:

- `binder-checker`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `runtime-memory`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `module-system`: `REJECT` with two `MAJOR` findings. Committing
  `ConfiguredPreludeInput` after library-set construction advanced the database
  beyond `finalCoreSnapshot`, and deleting `CompilationUnitQueryKey` left RFC
  0020 authority readiness and RFC 0017 compilation diagnostics without a
  replacement key.

The next candidate commits every non-core consumer's configured prelude in the
second session-wide graph-prerequisite transaction before acquiring
`finalCoreSnapshot`. No later input commit is permitted until core publication
and all non-core consumer queries for the session finish. Failed core
publication prevents every consumer from reading the already committed prelude
inputs and discards the failed session.

The key migration now separates crate-local and whole-context authority.
`CompilationOptions` remains keyed by complete `CrateKey`. RFC 0020
`ActiveDefinitionAuthorityReadyInput` and RFC 0017 compilation diagnostic
aggregation use the session's complete `CompilationRootSetQueryKey`.
`ActiveDefinitionAuthorityRecord` carries that root-set key so named-item
providers select readiness through a tracked definition-keyed input instead of
ambient state. Every production and test use of `CompilationUnitQueryKey`
migrates to one of those two typed keys, and the old type and codec are deleted
without compatibility.

The revised exact review hash is
`97dab6ec09c678f66eea7c3d2663222bd469cfb5654431535c7e972a1181dc39`.
The two approvals and one rejection for
`f9db0fd5445d8800a5cefc25953586445d2d2f1e64ce7f6ec0d7c238d0135d12`
are historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Core Owner Rejection And Contextual-Authority Revision

The core-owner review of
`97dab6ec09c678f66eea7c3d2663222bd469cfb5654431535c7e972a1181dc39`
returned:

- `binder-checker`: `REJECT` with two `BLOCKER` findings.
- `module-system`: `REJECT` with two `MAJOR` findings.
- `runtime-memory`: `REJECT` with one `BLOCKER` finding.

All three owners identified the same authority-installation ordering conflict:
RFC 0020 must derive complete named-definition inventories from the graph/input
snapshot, install the complete authority map and readiness in a later atomic
transaction, and only then permit named-item queries. The candidate instead
called the graph/input snapshot final and prohibited that required transaction.
The binder and module owners additionally found that storing `contextRoots`
only in a possibly absent authority value made the negative authority path
unable to select readiness and distinguish not-ready from inactive.

The next candidate uses three session-wide transactions. The second graph and
configured-prelude transaction creates `authorityStagingSnapshot`; only
handle-free graph, semantic skeleton, and complete named-definition inventory
queries may run there. The third RFC 0020 transaction atomically installs the
complete contextual authority map and readiness. Its resulting snapshot alone
is `finalCoreSnapshot`, and every later named-item, owner-body, core-bootstrap,
materialization, library, and consumer query uses it.

The authority, named-item, RFC 0019 module-owner and owner-body, and core query
keys now carry the complete context root set. A present authority probe does
not read readiness; absence uses the key's context roots to distinguish
`ProviderRejected` from `InactiveOwner`. Values retain narrow stable semantic
identities, and no ambient context, tombstone, old key, alias, or compatibility
decoder remains.

The revised exact review hash is
`3e3ad119bec867b98538e35d9d156678c6683560a4a731b94d94e875c7fe6b1b`.
All review results for
`97dab6ec09c678f66eea7c3d2663222bd469cfb5654431535c7e972a1181dc39`
are historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Core Owner Rejection And Contextual-Parent Revision

The core-owner review of
`3e3ad119bec867b98538e35d9d156678c6683560a4a731b94d94e875c7fe6b1b`
returned:

- `binder-checker`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `runtime-memory`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `module-system`: `REJECT` with one `MAJOR` finding.

The module owner confirmed the authority absence path, conditional readiness,
three-transaction staging, RFC 0019/0020 contextual child keys, and all earlier
closures. The remaining finding was that `VerifyBoundModule(ModuleKey)` still
had to select contextual `ModuleBodyOwners` and `BindOwnerBody` dependencies
without context roots.

The next candidate replaces `VerifyBoundModule` and
`ModuleDiagnosticFacts` with `ContextualModuleKey`, and replaces
`ResolveDiagnosticProvenance` with
`ContextualDiagnosticProvenanceKey`. Core provider read sets pass their exact
context roots to every bound-module query. The accepted staging-only
`NamedDefinitionInventory`, `BindModuleSkeleton`, `ModuleBodySyntax`,
`ModuleBodyProvenance`, and `MaterializeModuleSkeleton` queries retain plain
module keys because their closed read sets never select contextual children;
architecture mutations enforce that boundary. Every provider, verifier,
caller, dependency record, codec, dump, trace, vector, diagnostic aggregator,
test, and old overload participates in the one-step replacement.

The revised exact review hash is
`147fb2e26478219cdcae0dc714be4626d0f822327668af2a28b1648c3fc2700c`.
The two approvals and one rejection for
`3e3ad119bec867b98538e35d9d156678c6683560a4a731b94d94e875c7fe6b1b`
are historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Core Owner Rejection And Staging-Eligibility Revision

The core-owner review of
`147fb2e26478219cdcae0dc714be4626d0f822327668af2a28b1648c3fc2700c`
returned:

- `binder-checker`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `runtime-memory`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `module-system`: `REJECT` with zero `BLOCKER`, zero `MAJOR`, and one
  `MINOR` finding.

The module owner confirmed the contextual parent-key cascade and every earlier
closure. The remaining finding was that the RFC incorrectly described
`ModuleBodyProvenance` as a handle-free authority-staging producer even though
RFC 0019 classifies it as `RevisionLocal` and its result carries AST node and
source-range handles.

The next candidate separates plain-key eligibility from staging eligibility.
`NamedDefinitionInventory`, `BindModuleSkeleton`, `ModuleBodySyntax`,
`ModuleBodyProvenance`, and `MaterializeModuleSkeleton` retain plain
`ModuleKey` inputs only because their closed read sets do not select contextual
children. Only handle-free `Semantic` members required by authority
installation may execute against `authorityStagingSnapshot`.
`ModuleBodyProvenance` and `MaterializeModuleSkeleton` remain
`RevisionLocal` and cannot execute before the final snapshot barrier.
Acceptance criteria, RFC 0019 synchronization, negative architecture gates,
and the native test plan enforce both parts of that distinction.

The revised exact review hash is
`add71a9575ac4c0f6594150d5764bdbb1cc529c6b68ebf6dd32c8eb24a1857c5`.
The two approvals and one rejection for
`147fb2e26478219cdcae0dc714be4626d0f822327668af2a28b1648c3fc2700c`
are historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Pre-Review Acceptance-Set Correction

The root self-audit invalidated
`add71a9575ac4c0f6594150d5764bdbb1cc529c6b68ebf6dd32c8eb24a1857c5`
before any owner returned a verdict. The accepted-RFC replacement table already
required atomic RFC 0019 and RFC 0020 synchronization, but the rollout step's
explicit acceptance-change-set list omitted those two RFCs. The rollout list
now names both RFCs and their trackers, matching the normative replacement
transaction.

The revised exact review hash is
`9acf3bf6a7fdc2a46444f85782396578cb4139b772f4fed490a099a2e22aaa5d`.
No approval exists for the invalidated hash. All required owners remain
pending for the revised hash.

### 2026-07-25 Core Owner Rejection And Terminology Revision

The core-owner review of
`9acf3bf6a7fdc2a46444f85782396578cb4139b772f4fed490a099a2e22aaa5d`
returned:

- `binder-checker`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `runtime-memory`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `module-system`: `REJECT` with zero `BLOCKER`, zero `MAJOR`, and one
  `MINOR` finding.

The module owner confirmed every substantive module-system contract. The
remaining finding was one RFC 0017 synchronization row that still called all
plain-module-key queries "staging queries", contradicting the normative
separation between key contextuality and execution-phase eligibility.

The next candidate calls those queries "non-contextual queries" and states in
the same row that only handle-free `Semantic` authority prerequisites may
execute during staging. The `RevisionLocal` provenance and materialization
queries remain behind the final snapshot barrier.

The revised exact review hash is
`a246968166f90358f049733fdb6955c0cdd04faddb963011f53649c8c3903e88`.
The two approvals and one rejection for
`9acf3bf6a7fdc2a46444f85782396578cb4139b772f4fed490a099a2e22aaa5d`
are historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Core Owner Exact-Hash Approval

The core-owner review of
`a246968166f90358f049733fdb6955c0cdd04faddb963011f53649c8c3903e88`
returned:

- `binder-checker`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `module-system`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `runtime-memory`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.

All three owners confirmed the exact RFC hash before and after review and made
no workspace edits. Their review covered the complete bootstrap, authority,
identity, source-admission, three-transaction publication, contextual-query,
snapshot, lifetime, allocation-free, and runtime-boundary contracts. Native
RFC, format, internal-versioning, identity, incremental-query,
CompilerSession, sanitizer-build, and focused CTest evidence passed.

These approvals cover only the three named owners for this exact RFC hash.
Every remaining required owner remains pending.

### 2026-07-25 Peripheral Owner Rejection And Consumer-Handoff Revision

The lexer, error, and IR owner review of
`a246968166f90358f049733fdb6955c0cdd04faddb963011f53649c8c3903e88`
returned:

- `lexer-parser`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.
- `error-system`: `REJECT` with zero `BLOCKER`, three `MAJOR`, and zero
  `MINOR` findings.
- `ir-backend`: `REJECT` with one `BLOCKER` and zero `MAJOR` or `MINOR`
  findings.

The IR blocker was that `VerifiedInterfaceSource` ended at signature
projection: accepted RFC 0010 and RFC 0013 plus the live checked-module and
borrow-evidence builders still required imported `VerifiedModuleInterface`
values and their borrow surfaces. A non-core module importing
`core::prelude` therefore could not enter checked-module assembly, HIR, or MIR.

The next candidate makes `VerifiedInterfaceSource` the sole imported-interface
input for ordinary checked-module and borrow-evidence construction. The
toolchain-core branch independently proves that every lookup and support
definition is non-callable under the closed initial algebra and contributes no
`ImportedBorrowSurface`; it rejects a synthetic empty surface, user wrapper,
wrong sum alternative, or injected callable. The ordinary checked-module
retains the exact core interface revision. RFC 0010 and RFC 0013 plus their
trackers enter the atomic acceptance transaction.

The error findings were:

1. a distribution coordinate did not select one canonical digest;
2. core failures did not project complete RFC 0017 diagnostic root, phase,
   emitter, location, owner, or occurrence fields; and
3. the CLI named two unsupported failure-status classes without exact values.

The next candidate stores only the compiler-embedded expected distribution
digest, adds exact core diagnostic-root tag `0x05`, producer and emitter tags,
canonical first-category occurrence indices, locationless invocation or
compiler-invariant origins, no semantic owner or secondary records, and
complete diagnostic wire tests. `zomc` now returns `0` on success and exactly
`1` for every failure, matching the existing `zc::MainBuilder::Validity`
channel. Cause-only parser, module, signature, and role failures retain their
native RFC 0017 facts and do not acquire a duplicate core wrapper fact.

The revised exact review hash is
`d22e8a5bde42be583664f5e7c22d19863d836dedd697cb568c9d3c37663b9296`.
Every approval and rejection for
`a246968166f90358f049733fdb6955c0cdd04faddb963011f53649c8c3903e88`
is historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Consumer Failure And Revision-Lineage Correction

Focused re-review of
`d22e8a5bde42be583664f5e7c22d19863d836dedd697cb568c9d3c37663b9296`
was stopped after:

- `ir-backend` returned `REJECT` with zero `BLOCKER`, one `MAJOR`, and zero
  `MINOR` findings;
- `binder-checker` confirmed the same failure-algebra defect and identified one
  additional imported-revision lineage gap before returning a verdict; and
- `error-system` confirmed that its three preceding findings were closed but
  correctly returned no approval after the hash was invalidated.

The rejected consumer branch used RFC 0013 module-interface publication names
`InputMismatch` and `InvalidProjection` even though verified borrow evidence
uses the RFC 0010 `IrFailureKind` algebra. The replacement now maps context,
module, or revision disagreement to `InputRevisionMismatch`, a missing core
interface to `MissingRequiredFact`, a duplicate source or synthetic surface to
`AdditionalFact`, a callable core definition or wrong interface alternative to
`InvalidFact`, and malformed signature bytes to
`CanonicalCodecMismatch`.

The additional lineage gap was that
`ImportedSignatureModule.interfaceRevision` still accepted only
`ModuleInterfaceRevision`. The replacement introduces the exhaustive tagged
`ImportedInterfaceRevision = User(ModuleInterfaceRevision) |
ToolchainCore(CoreModuleInterfaceRevision)`. Every projector, signature/body
checker, coherence input, borrow-evidence builder, checked-module verifier,
diagnostic record, codec, dump, trace, vector, and invalidation edge switches
on the exact source/revision pair. The old untagged field and every adapter or
wrapper are deleted in the acceptance transaction.

The revised exact review hash is
`6f4ff3a94312934f5244fde676abd1916cd931f9239791d4f4a3ea68dd1c1a36`.
No approval exists for the invalidated hash. All required owners remain
pending for the revised hash.

### 2026-07-25 Bootstrap And Final-Revision Separation

Focused review of
`6f4ff3a94312934f5244fde676abd1916cd931f9239791d4f4a3ea68dd1c1a36`
returned:

- `error-system`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings;
- `ir-backend`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings; and
- `binder-checker`: `REJECT` with one `BLOCKER`, one `MAJOR`, and zero
  `MINOR` findings.

The blocker was that the bootstrap imported view still reused
`ImportedSignatureModule`, whose toolchain-core revision alternative now
required a final `CoreModuleInterfaceRevision`. Final authority depends on
bootstrap signatures, so that field created a bootstrap/final cycle.

The next candidate gives bootstrap its own
`CoreBootstrapSignatureRootAuthorization` and
`CoreBootstrapImportedSignatureModule` records. They carry only
`CoreBindingSurfaceRevision` and
`CoreBootstrapModuleInterfaceRevision`, are built in core-graph topological
order, and are forbidden from every ordinary checker and coherence input. The
final projector replaces the bootstrap origin with the direct finalized core
source's exact final revision.

The major finding was that the RFC 0005 migration named only
`ImportedSignatureModule.interfaceRevision` while
`SignatureAuthorizationOrigin::Imported` and
`ModuleInterfaceRevisionEntry` also retained the old revision type. The
replacement now moves all three fields to `ImportedInterfaceRevision` and
moves `SignatureRootAuthorization`, imported module binding surfaces, and
module-target surfaces to a parallel tagged
`ImportedBindingSurfaceRevision`. Signature, imported-view, coherence, body,
borrow, checked-module, diagnostic, codec, vector, and invalidation contracts
all switch exhaustively in one change.

The revised exact review hash is
`11b04fb4d4ca6b95bf5c00b1d82b24bf058cbb2561dace176ee54d5704fe912d`.
The two approvals and one rejection for
`6f4ff3a94312934f5244fde676abd1916cd931f9239791d4f4a3ea68dd1c1a36`
are historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Revision-Mismatch Outcome Closure

Focused review of
`11b04fb4d4ca6b95bf5c00b1d82b24bf058cbb2561dace176ee54d5704fe912d`
returned:

- `binder-checker`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings;
- `ir-backend`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings; and
- `error-system`: `REJECT` with zero `BLOCKER`, one `MAJOR`, and zero
  `MINOR` findings.

The error owner confirmed the bootstrap/final separation and all earlier
diagnostic closures. The remaining finding was that new revision-tag,
source-alternative, and bootstrap-leakage rejections did not select unique
existing failure and diagnostic outcomes.

The next candidate maps an expected RFC 0005 alternative with different bytes
to `StaleRevision`/`ZOM9930`, a valid but source-incompatible alternative to
`ViewMismatch`/`ZOM9931`, and an illegal or non-canonical tag, payload, or
bootstrap schema to `CanonicalCodecMismatch`/`ZOM9935`. At RFC 0010
checked-module and borrow-evidence boundaries, the corresponding exact outcomes
are `InputRevisionMismatch`, `InvalidFact`, `CanonicalCodecMismatch`,
`MissingRequiredFact`, and `AdditionalFact`. Native mutation tests assert every
mapping.

The revised exact review hash is
`b47941dca65c876aab8f82dfc2aab0501ab93dac0f45ad636778a96c89e68b1f`.
The two approvals and one rejection for
`11b04fb4d4ca6b95bf5c00b1d82b24bf058cbb2561dace176ee54d5704fe912d`
are historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Consumer And Error Exact-Hash Approval

Focused review of
`b47941dca65c876aab8f82dfc2aab0501ab93dac0f45ad636778a96c89e68b1f`
returned:

- `binder-checker`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings;
- `error-system`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings; and
- `ir-backend`: `APPROVE` with zero `BLOCKER`, `MAJOR`, or `MINOR`
  findings.

All three owners confirmed the exact RFC hash before and after review and made
no workspace edits. They approved the bootstrap/final separation, complete
RFC 0005 authorization, interface, binding-surface, and coherence lineage,
callable-driven RFC 0013 borrow evidence, ordinary RFC 0010 checked-module and
HIR/MIR handoff, and exact checker, IR, diagnostic, and CLI failure mappings.
Sanitizer builds, focused native CTest, RFC, diagnostic, checker, identity,
query, IR, CompilerSession, internal-versioning, format, and diff gates passed.

These approvals cover only the three named owners for this exact RFC hash.
Every remaining required owner remains pending.

### 2026-07-25 Flat Final-Interface Revision

Required-owner review of
`b47941dca65c876aab8f82dfc2aab0501ab93dac0f45ad636778a96c89e68b1f`
returned zero-finding approvals from `binder-checker`, `error-system`,
`ir-backend`, `lexer-parser`, and `runtime-memory`.
`module-system` returned `REJECT` with zero `BLOCKER`, one `MAJOR`, and zero
`MINOR` findings.

The module finding was that `CoreModuleInterfaceRecord` still nested the
complete `CoreBootstrapModuleInterfaceRecord`, including its bootstrap
interface revision. That made bootstrap-only schema reachable through the
ordinary `VerifiedInterfaceSource` boundary despite the explicit prohibition.

The next candidate replaces that wrapper with a flat final record containing
only canonical final bindings, roots without bootstrap authorization origins,
definitions, module targets, roles, the core binding-surface revision, final
authority revision, and its derived final interface revision. The finalizer
privately consumes and verifies the bootstrap memo, then discards every
bootstrap record, imported-view revision, and bootstrap-interface revision.
The final stable witness, codec, projector, and ordinary consumer cannot
observe them. Revision preimages, provider/verifier read sets, native vectors,
mutation assertions, and architecture gates enforce the flat boundary.

The revised exact review hash is
`7a5482bfc1abc50a62fccf03ba53901b016af517743ed657c2ee4376974df581`.
Every approval and rejection for
`b47941dca65c876aab8f82dfc2aab0501ab93dac0f45ad636778a96c89e68b1f`
is historical only. All required owners remain pending for the revised hash.

### 2026-07-25 Exact-Hash Review And ZOM3027 Rejection

Required-owner review of
`7a5482bfc1abc50a62fccf03ba53901b016af517743ed657c2ee4376974df581`
returned zero-finding approvals from `binder-checker`, `module-system`,
`ir-backend`, and `lexer-parser`.
`error-system` returned `REJECT` with zero `BLOCKER`, one `MAJOR`, and zero
`MINOR` findings.

The remaining major finding was that `ZOM3027 ToolchainModuleRootReserved`
did not have a complete ordinary-module diagnostic contract. The missing
closure covered registry and severity, the exact producer and source anchor,
precedence and suppression, RFC 0004 and RFC 0017 synchronization,
specification synchronization, and project-native assertions.

The normative repair invalidates every approval and rejection for
`7a5482bfc1abc50a62fccf03ba53901b016af517743ed657c2ee4376974df581`.
No approval is retained for the next review hash. That hash must be computed
only after the repair is complete.

### 2026-07-25 Owner-DAG And Diagnostic-Wire Revision

Finding-oriented preflight of
`0f0fc48aecf1120d7c81767a68f60b59128ac9e4a44a9994e70e77db2960e593`
identified no retained approval and required another normative repair.

The repair keeps package reservation occurrences and `PackageSite` records on
the pre-resolution RFC 0017 `PackageRootSetKey`, supplies complete compilation
context only through `ContextualDiagnosticProvenanceKey`, and assigns the
diagnostic fact codec, binding-input failure rail, independent verifier,
contextual query projection, core diagnostic projection, and complete mutation
oracles to exact implementation rows. It also defines the deterministic core
inventory generator and generated artifact, registers a fixed installed
consumer CTest contract, splits build and documentation files by their primary
owners, orders checked-module integration after borrow evidence, and gives the
core-library, specification-alignment, and RFC 0007 gates executable owner
tasks.

The revised exact review hash is
`31221bf7cf8e97ba43804bb061f818c6a2f6c342383d42ff35b252e8b3baeb3c`.
Every approval, rejection, and preflight verdict for an earlier hash is
historical only. All required owners remain pending for this candidate.

### 2026-07-25 Unique-Ownership And Ordered-Cutover Revision

Exact-hash review of
`31221bf7cf8e97ba43804bb061f818c6a2f6c342383d42ff35b252e8b3baeb3c`
returned no approval. The three reviewing owners requested unique primary
ownership for `binding-input.{h,cc}` and `docs/design/tooling/**`, explicit
dependency edges for every repeated implementation path, and the complete
diagnostic registry and build-registration file set.

The repair gives the binding-input failure algebra and verifier to
`binder-checker`, keeps its module adapter and publication under
`module-system`, excludes tooling design from `spec-audit` primary ownership,
orders every repeated diagnostic, module-discovery, query, session, and test
path, and adds `diagnostic-ids.h`, `diagnostic-info.h`, and the diagnostics
CMake target to the core diagnostic task.

The revised exact review hash is
`4d544bd64cba2ca55ac352a7fe87e4927f5cc270e3f5c2092a20ab7b97a83e54`.
Every verdict for an earlier hash is historical only. All required owners
remain pending for this candidate.

### 2026-07-25 Atomic Syntax And Query-Cutover Revision

Review of
`4d544bd64cba2ca55ac352a7fe87e4927f5cc270e3f5c2092a20ab7b97a83e54`
returned approvals from `rfc`, `task-router`, `error-system`, and
`binder-checker`, followed by changes requested from `lexer-parser` and
`module-system`. The normative repair invalidates all of those verdicts.

The repair makes the `intrinsic` token, AST, grammar, normative chapters,
lexer tests, corpus, and FileCheck expectations one indivisible cutover. It
also expands the compilation-unit replacement through the registry hierarchy,
fingerprint, dump, invariant, source query inputs, every contextual query
caller, parser provider, capability lease, core provider and verifier, build
registration, and native identity/query/read-set evidence. The tracker records
one coordinated no-compatibility transaction and names the first complete
sanitizer build and full native verification points.

The revised exact review hash is
`9a72770704eba12a73d638231e482ad8f5433497ffd72a0f7b55ca3be3ce9418`.
All required owners remain pending for this candidate.

### 2026-07-25 Executable Validation DAG Revision

Pre-review of
`9a72770704eba12a73d638231e482ad8f5433497ffd72a0f7b55ca3be3ce9418`
returned no approval. The reviewers found that native tests authored after a
configure-time glob could execute stale binaries, the first benchmark lacked
owned core scenarios, and remaining `CrateKey::package()` production and test
callers were not named in the atomic identity cutover.

The tracker-only repair adds exact production and native-test consumer rows,
makes the first complete build depend on both, assigns the core benchmark
corpus and baseline to the first benchmark task, defers pre-build syntax test
execution, and requires reconfiguration and rebuilding after every later
native-test or test-CMake edit. It also places the signature, role-seed,
bootstrap, final-interface, borrow, checked-module, HIR, and MIR native tests
before the first complete build. The normative RFC is unchanged. All required
owners remain pending for this candidate.

### 2026-07-25 Silent Check And Runtime FFI Closure Revision

Required-owner review of
`9a72770704eba12a73d638231e482ad8f5433497ffd72a0f7b55ca3be3ce9418`
found that the installed consumer had no backend-free full-frontend success
action and that `docs/design/runtime-ffi-examples.md` retained unsupported
concurrency and versioned ABI contracts outside the synchronization matrix.
No approval for that candidate remains valid.

The normative repair defines one closed compilation action model and a silent
`--check` action through production checked-module, HIR, Built MIR, and
ownership-overlay verification without LIR or backend emission. It replaces
the positional-source fixture with an exact package manifest, target, argv,
exit, output, and artifact contract. It also adds the runtime FFI document to
the deletion-only synchronization matrix and exact `spec-audit` task.

The revised exact review hash is
`2c08d5ace009b0d4817eadedd144c2a74524303296c90a30ea4465182b399568`.
All required owners remain pending for this candidate.

### 2026-07-25 Compile Entry And CLI Oracle Revision

Review of
`2c08d5ace009b0d4817eadedd144c2a74524303296c90a30ea4465182b399568`
found that the exact installed argv omitted the existing `compile` subcommand
and that option-state unit tests could not prove real CLI conflict handling.
No approval for that candidate remains valid.

The normative repair binds `Check` exclusively to the existing `compile`
subcommand, corrects the installed argv, assigns the registered
`package-invocation-cli` test, and requires real-process coverage for repeated
and mixed action selectors, prohibited output requests, exact parser errors,
nonzero status, and absence of output artifacts.

The revised exact review hash is
`2b86bc9202b292715ea81fbdd970339c131dce15f756551f23499a6a43fb7a44`.
All required owners remain pending for this candidate.

### 2026-07-25 Tooling Dependency And Verification Closure Revision

Review of
`2b86bc9202b292715ea81fbdd970339c131dce15f756551f23499a6a43fb7a44`
found that the tooling slice could create the IDE and LSP products before RFC
0023 was accepted, the incremental benchmark command incorrectly used a
sanitizer build rejected by the production runner, the final sanitizer build
could reuse stale objects, and the accepted RFC 0007 ownership architecture
and coverage gates were absent. The tooling and verification owners rejected
that candidate. No approval for it remains valid.

The normative repair adds an explicit RFC 0023 foundation gate before any
tooling implementation, uses a freshly configured and clean-built Release
tree plus an explicit baseline record, review, and commit sequence for
benchmarks, requires a clean final sanitizer build, and implements the accepted
RFC 0007 ownership architecture and fail-hard coverage infrastructure. The
coverage census includes every non-test compiler `.cc` file added or modified
by the RFC 0025 implementation series.

The revised exact review hash is
`907c11f18fd0969045d9b39f1afd36d2b733b897825721488a33adcb5bc9fb25`.
All required owners remain pending for this candidate.

### 2026-07-25 English-Only Completion-Gate Revision

Verification review of
`907c11f18fd0969045d9b39f1afd36d2b733b897825721488a33adcb5bc9fb25`
found that the final evidence omitted RFC 0007's required changed-file CJK
scan. The RFC, format, and internal-versioning checks do not perform that scan.
No approval for that candidate remains valid.

The normative repair registers a reusable project-native English-only checker,
mutation self-test, implementation-series changed-file census, final evidence,
and CI gate. It rejects Han, Hiragana, Katakana, and Hangul characters in every
added, copied, modified, or renamed text artifact.

The revised exact review hash is
`276480f774ebc8641208e01519f50cb7fdefc60520a689f8e120865619fa9079`.
All required owners remain pending for this candidate.

### 2026-07-25 Specification-Index Concurrency Revision

Spec-audit review of
`276480f774ebc8641208e01519f50cb7fdefc60520a689f8e120865619fa9079`
found that `docs/spec/specification.md` still presented an M:N work-stealing
scheduler, task combinators, a supervisor tree, atomics, bounded backpressure
channels, a priority-inheritance mutex, scope-local storage, and timer-wheel
internals as current language capabilities. The fixed drift matrix and task DAG
did not own or repair that entry point. No approval for that candidate remains
valid.

The normative repair assigns the specification index to the concurrency
deletion slice, adds it to the fixed alignment inventory, and requires
independent mutation rejection for every unsupported capability claim.

The revised exact review hash is
`e4535170dd0467bf55fddb1572d79abdd6575cdaf5402e8126a73cf494c93f52`.
All required owners remain pending for this candidate.

### 2026-07-25 Speculative-Concurrency Document Deletion Revision

Concurrency review of
`e4535170dd0467bf55fddb1572d79abdd6575cdaf5402e8126a73cf494c93f52`
found that enumerated cleanup could still preserve independent unimplemented
checker, diagnostic, task, scheduler, cancellation, observability, stack,
unwind, runtime, FFI, memory-model, and ABI claims in the governed concurrency
and runtime FFI design documents. No approval for that candidate remains valid.

The normative repair deletes
`docs/concurrency/zom-async-canonical-design.md` and
`docs/design/runtime-ffi-examples.md` in full. The specification index and
Chapter 15 retain only implemented parser and frontend facts. The fixed
alignment gate rejects restoration of either file and independently mutates
every unsupported current-capability class.

The revised exact review hash is
`a82a5d737a1dc5db8014006924102b2e2ec3ad09887c96d308a5f26743c6ef6c`.
All required owners remain pending for this candidate.

### 2026-07-25 Deleted-Document Reference Closure Revision

RFC review of
`a82a5d737a1dc5db8014006924102b2e2ec3ad09887c96d308a5f26743c6ef6c`
found that `docs/overview.md` still linked the runtime FFI document selected
for deletion. No task owned the inbound reference, so the proposed transaction
would leave a repository dead link. No approval for that candidate remains
valid.

The normative repair adds the documentation index to the spec-audit
transaction and makes the fixed alignment gate reject any retained
documentation reference to either deleted speculative design.

The revised exact review hash is
`dac6ced976054a6325b46a2c1c340f2f218acb1e63a30dd337b72d66200f6ae4`.
All required owners remain pending for this candidate.

### 2026-07-25 Current-State Reference-Census Revision

Review of
`dac6ced976054a6325b46a2c1c340f2f218acb1e63a30dd337b72d66200f6ae4`
found that the inbound-reference gate also scanned RFC and tracker decision
history, which must retain non-link text naming the deleted paths. The gate
would therefore reject its own governing records. No approval for that
candidate remains valid.

The normative repair rejects dead Markdown links across every retained
documentation artifact, limits plain-text path rejection to current-state
overview, specification, design, and concurrency documentation, permits
non-link RFC and report decision history, and requires the self-test to operate
on an isolated temporary documentation tree.

The revised exact review hash is
`7af8851ef57b0e8022a44029a8f864ad278e1d07f00e893df0cb36c998856c90`.
All required owners remain pending for this candidate.

### 2026-07-25 Exhaustive Documentation-Census Revision

Verification review of
`7af8851ef57b0e8022a44029a8f864ad278e1d07f00e893df0cb36c998856c90`
found that the current-state plain-text census omitted retained top-level,
development, and plan documentation, while the self-test did not prove that
RFC and report Markdown dead links fail even though their non-link decision
text is allowed. No approval for that candidate remains valid.

The normative repair scans every retained documentation artifact for resolved
dead Markdown links, scans every retained documentation artifact outside the
explicit RFC/report history allowlist for plain-text remnants, and adds
inline-link, reference-link, allowed RFC/report history, top-level current-
state, and nested current-state fixtures to the isolated self-test.

The revised exact review hash is
`905930ee95f3e26ee4d5fc78cb54057f2f3eb204f01297f952be9424371012cf`.
All required owners remain pending for this candidate.

### 2026-07-25 Dead-Link Mutation-Matrix Revision

Spec-audit review of
`905930ee95f3e26ee4d5fc78cb54057f2f3eb204f01297f952be9424371012cf`
found that the self-test exercised relative Markdown targets but did not prove
the required absolute repository target branch. A checker that ignored
absolute links could pass. No approval for that candidate remains valid.

The normative repair requires the isolated self-test to cover the complete
inline/reference-style by absolute-repository/normalized-relative four-link
matrix in both RFC and report fixtures.

The revised exact review hash is
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`.
All required owners remain pending for this candidate.

## Owner Review Matrix

| Owner | State | Review Surface |
|---|---|---|
| `rfc` | Approved | Governance, scope, prior art, status, and exact-hash review |
| `task-router` | Approved | Core path ownership and mandatory gate routing |
| `lexer-parser` | Approved | Removal of the unused intrinsic token and grammar-facing inventory alignment |
| `binder-checker` | Approved | Bootstrap signatures, role authority, declaration-only final interfaces, and language/core boundary |
| `module-system` | Approved | Unversioned identity, source admission, module graph, queries, and session publication |
| `error-system` | Approved | Closed failures, diagnostics, anchors, ordering, suppression, and CLI behavior |
| `concurrency` | Approved | Removal of unsupported marker and standard-library claims from concurrency design |
| `ir-backend` | Approved | Intrinsic boundary, HIR/MIR consumption, build/install layout, and target capabilities |
| `runtime-memory` | Approved | ZOM source ownership, allocation-free boundary, marker roles, and runtime ABI |
| `tooling-lsp` | Approved | Core definition navigation and semantic source locations |
| `spec-audit` | Approved | RFC synchronization and normative/specification drift |
| `verification` | Approved | Native tests, mutation oracles, installation evidence, architecture gate, and full validation |

Every approval must identify the exact RFC SHA-256. Normative edits invalidate
earlier approvals.

All twelve required owners approved proposal SHA-256
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`
with zero critical, major, or minor findings. The reviewed tracker SHA-256 was
`1746140fc18d7fef0551360d5d95c2d2dd9ee4d28826e7c00738219b259261eb`.

## Decision Record

Decision: Accepted.

RFC 0025 is `IMPLEMENTING`. All required owners approved the exact proposal
hash, and `R25-02` applied the accepted-RFC replacement transaction. Product
implementation proceeds only in the dependency order recorded below.
The tooling slice remains externally blocked by `R25-12G` and RFC 0023.

### 2026-07-25 Implementation Sequencing Correction

Live implementation inspection after acceptance found that the existing
`DiagnosticFact` and `zom.source-diagnostic-facts` wire are parser-only, while
package-resolution diagnostics require the lower-layer `PackageRootSetKey`.
The unified fact-wire task owns every source producer and materializer together
with the complete diagnostic sum. The core diagnostic root depends on the
identity, query, core signature, and failure contracts.

The execution plan therefore keeps `R25-02A` as the tracked `ZOM3027`
registration, establishes the shared typed argument in `R25-02AS` before
package or Binder integration, keeps `R25-02B` limited to the typed package and
module failure rails, and moves the only fact-wire replacement into
`R25-09C`. That atomic cutover owns the lower-layer package-root key, all source
fact producers and consumers, the complete Source, Package, BuildScript,
Module, and CoreLibrary schema, and both reservation and core diagnostic
projections. `R25-02P` immediately authors the producer, verifier, adapter,
rendering, suppression, and CLI assertions required when `R25-02B` creates the
production emitters. It removes the temporary diagnostic reservation only
after static coverage recognizes the production emitters and native
assertions. `R25-02C` waits for `R25-09C` and owns only the later fact-wire and
contextual-query mutation proof. This correction changes implementation order
and file ownership only; it does not change the accepted normative diagnostic
contract.

### 2026-07-28 RFC 0042 Diagnostic Cutover Correction

The six-path RFC 0030 diagnostic preflight could not replace the live
source-relative fact, because its producers, parser query values, materializer,
Binder result codec, and native callers were outside the transaction. RFC 0042
therefore moves the complete current source-only fact cutover into
`R29-12D`. Its exact landing set replaces the earlier six-path set.

`R25-09C` no longer owns `diagnostic-fact.*`, the source draft collector,
source caller migration, or the initial sole-wire replacement. It retains the
later atomic expansion from the Source-and-Module contract published by RFC
0029 `R29-14` to executable `Package`, `BuildScript`, and `CoreLibrary`
origins, `ZOM3027` and core projections, contextual diagnostic queries, and
their producer/verifier/materializer coverage. `R25-02C` continues to wait for
that later expansion.

### 2026-07-25 Package Reservation Preflight Correction

Live-path preflight after `R25-02BA` found that the accepted package priority
could not construct its required `PackageKey`: the selected package key depends
on successful feature expansion, while an unknown root feature is itself
`TargetSelectionInvalid`. Constructing a provisional key from requested or
empty features would publish false package identity. The normative contract now
keeps every `TargetSelectionInvalid` result ahead of reservation, constructs the
exact selected `PackageKey` from expanded features, then orders
`UserTargetRoot` before `DependencyAlias` and all downstream package work.

The preflight also found two omitted production paths.
`dependency-manifest.h` and `dependency-manifest.cc` own the retained alias-key
provenance required for independent reconstruction. `zomc.cc` is the sole
production consumer of `PackageCompilationVerificationResult` and must
exhaustively diagnose the new failure before resolver, lock, materialization,
or build-script work. `R25-02B` establishes the failure in that live closed
package-compilation result; it does not claim that the still-unimplemented
whole RFC 0012 `PackagePipelineFailure` union has landed.

Approval agent review of RFC SHA-256
`d01d0c481ff6dbe8f2f8014030f242ef13d9ad0e24bb51680cdc14eb0b73ffeb`
and tracker SHA-256
`d207ab6b25fa8a697e4b186fe367a820cc2654fd9b466a22132ccb924bf0576a`
returned `APPROVED` with zero critical, major, or minor findings. The superseded
candidate `f0673b014576d7ce678b74a8a9fd930414ea4ba5dcc52e72b5f174778690b724`
was rejected because one RFC 0012 synchronization row retained the old
priority; no approval from that candidate is retained.

### 2026-07-26 RFC 0026 Acceptance Synchronization

All four RFC 0026 required owners approved proposal SHA-256
`39df5d3f11dbdcb2e95056b1cd14fd5220a19688f31a3e3180230ad465a3f84d`.
RFC 0025 now uses RFC 0026 as the normative module-topology transaction,
derived-query, stable graph/SCC, failure, barrier, and final Binder-bridge
contract. The synchronized implementation remains incomplete and proceeds
through RFC 0026 tasks `R26-05` through `R26-09` before the dependent RFC 0025
bootstrap, interface, prelude, diagnostic, and final verification rows may
claim completion.

### 2026-07-27 RFC 0027 Acceptance Synchronization

RFC 0027 was accepted on exact proposal SHA-256
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`
through transaction `rfc0027-accept-20260727-e2f4ba5e`.

The synchronized contract uses contextual Binder query keys, an arena-owned
canonical interner set and semantic type store, typed graph and Binder
capabilities, the three exact input transactions followed by one irreversible
final seal, Binder-owned diagnostic facts, and exact retained
`VerifiedBoundModuleLease` ownership through checked module, HIR, Built MIR,
and the ownership-event overlay. Product implementation remains incomplete.
RFC 0027 tasks `Q1-Q4`, `I1-I2`, `B1-B4`, `M1-M5`, `C1-C2`, `L1-L4`,
`T1-T2C`, `D1-D5`, `W1-W4`, `E1-E8`, and `A1-A3` govern completion and
evidence.

### 2026-07-27 RFC 0029 Acceptance Synchronization

RFC 0029 was accepted on exact proposal SHA-256
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`
through transaction `rfc0029-accept-20260727-8d393a0c`.

The synchronized contract requires complete module-qualified Binder keys,
revision-local identity-site provenance, stable-identity admission before
semantic inventories, and the exact typed failure alternatives and read order
for all five Binder provenance capabilities. Product implementation remains
incomplete.

The dependency overlay is:

| Local work | Current dependency |
|---|---|
| Stable Binder schema, facts, codecs, tests, and gates | exact RFC 0030 `R29-12AB` |
| Current source-only canonical diagnostic cutover | RFC 0029 `R29-12D` through RFC 0042 |
| Live Binder and identity diagnostic facts | RFC 0029 `R29-13B`, published through `R29-14` |
| `R25-07` graph, session, and materializer completion | RFC 0029 `R29-14`; RFC 0028 `R28-16` |
| `R25-07` owner-body closure completion | RFC 0029 `R29-12AB`; `R29-14` |
| `R25-07T` native and architecture evidence | RFC 0029 `R29-15` |

This documentation synchronization changes neither RFC 0025's `ACCEPTED`
status nor any implementation row status.

### 2026-08-09 Implementation Start Reconciliation

The reserved-root diagnostic rail, exhaustive compilation-unit identity,
source-distribution admission, intrinsic lexical cutover, stable graph
foundation, final materialization, and retained consumer migration have
production evidence. RFC 0025 therefore moves to `IMPLEMENTING`. The complete
source-backed core bootstrap, six semantic core projections, four
revision-local core materializers, final interface, prelude, diagnostic, build,
and verification cutover remain pending in their recorded dependency order.

The module-graph staging boundary now demands `CoreModuleGraphQuery` for every
projected core crate before it can seal the final snapshot. The demand rejects
runtime failure, non-value results, a mismatched crate, and an empty module
set. Sanitizer build, `compiler-session-test`,
`compiler-session-package-test`, `core-library-query-provider-test`, and the
CompilerSession, incremental-query, and identity architecture checks passed.
This is production use of the independently verified core graph, not completion
of the remaining core query graph or bootstrap pipeline.

## Implementation Tracker

`R25-03` extends its exact path set with
`products/zomlang/compiler/identity/CMakeLists.txt`,
`products/zomlang/compiler/identity/key/compilation-unit-key.h`, and
`products/zomlang/compiler/identity/key/compilation-unit-key.cc`. The dedicated
leaf keeps RFC 0012 package identity independent from semantic
compilation-unit identity; it does not add another contract or compatibility
surface.

`R25-03CT` extends its exact path set with
`scripts/codegen/gen_package_oracles.py` and
`products/zomlang/tests/fixtures/package-oracles/package-generated-oracles.json`.
The package generated-oracle gate authenticates the runtime
`BuildScriptExecutionKey` vector whose semantic-context fingerprint is replaced
by the compilation-unit cutover. The generator and its catalog therefore move
atomically with that native caller and retain no prior digest.

| Task ID | Owner | Exact paths | Depends on | Outcome | Native evidence | State |
|---|---|---|---|---|---|---|
| `R25-00` | `task-router` | `AGENTS.md`<br>`.agents/subagents/README.md`<br>`.agents/subagents/manifest.yaml`<br>`.agents/subagents/task-router.md`<br>`.agents/subagents/rfc.md`<br>`.agents/subagents/lexer-parser.md`<br>`.agents/subagents/binder-checker.md`<br>`.agents/subagents/module-system.md`<br>`.agents/subagents/error-system.md`<br>`.agents/subagents/concurrency.md`<br>`.agents/subagents/ir-backend.md`<br>`.agents/subagents/runtime-memory.md`<br>`.agents/subagents/tooling-lsp.md`<br>`.agents/subagents/spec-audit.md`<br>`.agents/subagents/verification.md` | None | Freeze one primary owner and every mandatory review gate for each RFC 0025 path without granting product implementation authority. | `python3 scripts/check-rfc.py`<br>`python3 scripts/check-no-internal-versioning.py --check`<br>`git diff --check` | Complete |
| `R25-01` | `rfc` | `docs/rfc/0025-source-backed-core-library-architecture.md`<br>`docs/rfc/tracking/0025-review-and-implementation.md` | `R25-00` | Close the RFC-only diagnostic, synchronization, silent-check, installation, tooling-dependency, benchmark, clean-build, coverage, English-only, speculative-document deletion, exhaustive documentation-census, dead-link mutation-matrix, and native-oracle contracts, with every required owner reviewing the exact candidate, then compute and freeze the exact review hash. | `python3 scripts/check-rfc.py`<br>`python3 scripts/check-no-internal-versioning.py --check`<br>`python3 scripts/check-format.py`<br>`git diff --check` | Complete; all twelve required owners approved proposal `4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0` |
| `R25-02` | `rfc` | `docs/rfc/README.md`<br>`docs/rfc/0004-binder-architecture.md`<br>`docs/rfc/tracking/0004-review-and-implementation.md`<br>`docs/rfc/0005-type-system-architecture.md`<br>`docs/rfc/tracking/0005-review-and-implementation.md`<br>`docs/rfc/0007-borrow-lifetime-ownership-checker.md`<br>`docs/rfc/tracking/0007-review-and-implementation.md`<br>`docs/rfc/0008-compiler-session-cross-module.md`<br>`docs/rfc/tracking/0008-review-and-implementation.md`<br>`docs/rfc/0010-intermediate-representation-pipeline.md`<br>`docs/rfc/tracking/0010-review-and-implementation.md`<br>`docs/rfc/0011-semantic-identity-foundation.md`<br>`docs/rfc/tracking/0011-review-and-implementation.md`<br>`docs/rfc/0012-package-manifest-and-resolver.md`<br>`docs/rfc/tracking/0012-review-and-implementation.md`<br>`docs/rfc/0013-ownership-analysis-integration-boundary.md`<br>`docs/rfc/tracking/0013-review-and-implementation.md`<br>`docs/rfc/0015-canonical-checker-codec-closure.md`<br>`docs/rfc/tracking/0015-review-and-implementation.md`<br>`docs/rfc/0017-incremental-compiler-query-architecture.md`<br>`docs/rfc/tracking/0017-review-and-implementation.md`<br>`docs/rfc/0018-stable-query-identity-wire-closure.md`<br>`docs/rfc/tracking/0018-review-and-implementation.md`<br>`docs/rfc/0019-stable-body-owner-and-query-closure.md`<br>`docs/rfc/tracking/0019-review-and-implementation.md`<br>`docs/rfc/0020-active-definition-authority-projection.md`<br>`docs/rfc/tracking/0020-review-and-implementation.md`<br>`docs/rfc/0024-standard-marker-authority.md`<br>`docs/rfc/tracking/0024-review-and-implementation.md`<br>`docs/rfc/0025-source-backed-core-library-architecture.md`<br>`docs/rfc/tracking/0025-review-and-implementation.md`<br>`docs/rfc/0006-error-lowering-runtime-abi.md` (read-only verification)<br>`docs/rfc/tracking/0006-review-and-implementation.md` (read-only verification) | `R25-00`; `R25-01`; exact-hash approval from every required owner | Atomically synchronize RFCs 0004, 0005, 0007, 0008, 0010, 0011, 0012, 0013, 0015, 0017, 0018, 0019, 0020, and 0024 plus their trackers, retain RFC 0006 unchanged, update the index and RFC 0025 decision, and transition RFC 0025 to `ACCEPTED`. | `python3 scripts/check-rfc.py`<br>`python3 scripts/check-no-internal-versioning.py --check`<br>`python3 scripts/check-format.py`<br>`git diff --check` | Complete; accepted-RFC replacement transaction synchronized all fourteen RFCs and trackers, retained RFC 0006 unchanged, and passed every R25-02 native gate |
| `R25-02A` | `error-system` | `products/zomlang/compiler/diagnostics/defs/diagnostics-module.def`<br>`products/zomlang/tests/coverage/diagnostic-reservations.json` | `R25-02` | Register exactly `ZOM3027 ToolchainModuleRootReserved` and reserve it against this tracker until all three production emitters and their native assertions land in `R25-02B` and `R25-02P`; retain no untracked diagnostic identifier. | `cmake --preset sanitizer`<br>`cmake --build --preset sanitizer`<br>`python3 scripts/check-diagnostic-coverage.py --check`<br>`python3 scripts/check-diagnostic-coverage.py --self-test` | Complete; registry added, the temporary tracked reservation covered the pre-emitter interval and was removed by `R25-02P`, diagnostic coverage check and self-test passed, and sanitizer configure and build passed |
| `R25-02AS` | `error-system` | `products/zomlang/compiler/diagnostics/toolchain/module-root-argument.h`<br>`products/zomlang/compiler/diagnostics/toolchain/module-root-argument.cc`<br>`products/zomlang/compiler/diagnostics/CMakeLists.txt`<br>`products/zomlang/tests/unittests/compiler/diagnostics/toolchain-module-root-argument-test.cc` | `R25-02A` | Establish the single shared `ModuleRootArgument` schema before package or Binder integration. Admit exactly one canonical `core` module-path segment from a typed canonical path, expose no raw-string constructor, and provide one strict canonical codec used later by both package and module rails and the unified fact wire. | `cmake --preset sanitizer`<br>`cmake --build --preset sanitizer`<br>`ctest --preset default -R '^toolchain-module-root-argument-test$' --output-on-failure`<br>`python3 scripts/check-diagnostic-coverage.py --check` | Complete; shared typed argument, strict codec, focused native test, sanitizer build, and static gates passed |
| `R25-02BA` | `binder-checker` | `products/zomlang/compiler/binder/binding-input.h`<br>`products/zomlang/compiler/binder/binding-input.cc` | `R25-02AS` | Extend `ModuleGraphSourceFailure` with the accepted source-root reservation alternative and make the builder and independent `VerifiedModuleGraphVerifier` reconstruct and compare its complete module, source, local path, schema ordinal, and typed argument. | `cmake --preset sanitizer`<br>`cmake --build --preset sanitizer`<br>`python3 scripts/check-binder-architecture.py --check`<br>`python3 scripts/check-binder-architecture.py --self-test` | Complete; closed source-root failure algebra, typed builder, independent verifier reconstruction, sanitizer build, Binder architecture checks, format check, and diff check passed |
| `R25-02B` | `module-system` | `products/zomlang/compiler/driver/package/dependency-manifest.h`<br>`products/zomlang/compiler/driver/package/dependency-manifest.cc`<br>`products/zomlang/compiler/driver/package/manifest-model.h`<br>`products/zomlang/compiler/driver/package/manifest-model.cc`<br>`products/zomlang/compiler/driver/package/manifest-parser.h`<br>`products/zomlang/compiler/driver/package/manifest-parser.cc`<br>`products/zomlang/compiler/driver/package/package-compilation-request.h`<br>`products/zomlang/compiler/driver/package/package-compilation-request.cc`<br>`products/zomlang/compiler/driver/package/package-diagnostic.h`<br>`products/zomlang/compiler/driver/package/package-diagnostic.cc`<br>`products/zomlang/compiler/driver/graph/crate-graph.h`<br>`products/zomlang/compiler/driver/graph/crate-graph.cc`<br>`products/zomlang/compiler/driver/graph/module-discovery.h`<br>`products/zomlang/compiler/driver/graph/module-discovery.cc`<br>`products/zomlang/compiler/driver/session/compiler-session.h`<br>`products/zomlang/compiler/driver/session/compiler-session.cc`<br>`products/zomlang/compiler/driver/query/binding/named-identity-inventory-query.cc`<br>`products/zomlang/compiler/binder/diagnostics/module-graph-diagnostic-adapter.h`<br>`products/zomlang/compiler/binder/diagnostics/module-graph-diagnostic-adapter.cc`<br>`products/zomlang/utils/zomc/zomc.cc` | `R25-02AS`; `R25-02BA`; exact-hash approval of the package reservation preflight correction | Extend the live closed package-compilation result and RFC 0004 module failure rail; implement the target, alias, and source-root `ZOM3027` producers and typed adapters with exact anchors, precedence, suppression, and no module-interface adapter. All existing package, feature, and requested-target selection failures precede reservation so that the failure carries the exact expanded-feature `PackageKey`; reservation precedes registry graph resolution, lock, materialization, and build-script work. Permit only the typed reserved `core` declaration to pass the named-identity source-selection name check so that the independent module-graph verifier owns its rejection; retain ordinary source/module mismatch rejection. The whole RFC 0012 `PackagePipelineFailure` remains accepted but unimplemented. The single canonical diagnostic-fact publication path lands later in the atomic `R25-09C` wire cutover; this row creates no local fact record, codec, or emission fallback. | `python3 scripts/check-compiler-session-architecture.py --check`<br>`python3 scripts/check-package-architecture.py --check`<br>`python3 scripts/check-binder-architecture.py --check` | Complete; all three production emitters, typed adapters, exact anchors, precedence, no-publication behavior, requester and SCC-derived suppression, CLI early stop, full sanitizer build, architecture gates, format, and diff checks passed; adversarial re-review approved with zero findings |
| `R25-02BC` | `module-system` | `products/zomlang/compiler/driver/CMakeLists.txt` (read-only verification) | `R25-02B` | Verify that every modified `ZOM3027` driver or module adapter source is already registered in the production driver target. Registration of the unified incremental diagnostic query waits for `R25-05M` after the atomic `R25-09C` wire cutover. | `cmake --preset sanitizer`<br>`cmake --build --preset sanitizer` | Complete; all modified sources were already registered and the full sanitizer build linked the production compiler without a CMake change |
| `R25-02P` | `verification` | `products/zomlang/tests/unittests/compiler/binder/binding-input-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/active-definition-authority-session-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/compiler-session-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/manifest-parser-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/package-compilation-request-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/package-diagnostic-test.cc`<br>`products/zomlang/tests/conformance/corpus/13-modules/toolchain_core_root_reserved_reject_neg.zom`<br>`products/zomlang/tests/conformance/expectations/ast/13-modules/toolchain_core_root_reserved_reject_neg.check`<br>`products/zomlang/tests/conformance/expectations/diagnostics/13-modules/toolchain_core_root_reserved_reject_neg.check`<br>`products/zomlang/tests/conformance/expectations/grammar/13-modules/toolchain_core_root_reserved_reject_neg.yml`<br>`products/zomlang/tests/coverage/diagnostic-reservations.json` | `R25-02B`; `R25-02BC` | Immediately prove all three `ZOM3027` producers, independent reconstruction, exact target, alias-key, and declared-name anchors, typed reserved-root passage through named-identity source selection, ordinary mismatch rejection, expanded-feature package identity, target-before-alias and selection-before-reservation precedence, cycle and other derived module-diagnostic suppression, typed adapter rendering, CLI failure, grammar acceptance followed by semantic rejection, AST coverage, and negative graph publication. Remove the temporary diagnostic reservation only after static diagnostic coverage recognizes the production emitters and assertions. | `cmake --preset sanitizer`<br>`cmake --build --preset sanitizer`<br>`ctest --preset default -R '^active-definition-authority-session-test$' --output-on-failure`<br>`ctest --preset default -R '^binding-input-test$' --output-on-failure`<br>`ctest --preset default -R '^compiler-session-test$' --output-on-failure`<br>`ctest --preset default -R '^manifest-parser-test$' --output-on-failure`<br>`ctest --preset default -R '^package-compilation-request-test$' --output-on-failure`<br>`ctest --preset default -R '^package-diagnostic-test$' --output-on-failure`<br>`ctest --preset default -L lit --output-on-failure`<br>`ctest --preset default -R '^conformance-ast-coverage$' --output-on-failure`<br>`ctest --preset default -R '^conformance-grammar$' --output-on-failure`<br>`python3 scripts/check-diagnostic-coverage.py --check`<br>`python3 scripts/check-diagnostic-coverage.py --self-test`<br>`python3 scripts/check-binder-architecture.py --check`<br>`python3 scripts/check-compiler-session-architecture.py --check` | Complete; all package and module producers, independent reconstruction, typed identity-query exception, exact anchors, precedence, SCC-derived suppression, retained independent failure, CLI rejection, grammar acceptance followed by semantic rejection, AST coverage, no graph publication, and reservation removal passed sanitizer build, focused native tests, lit, diagnostic coverage, architecture, format, and diff gates |
| `R25-02C` | `verification` | `products/zomlang/tests/unittests/compiler/diagnostics/diagnostic-fact-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/compiler-session-diagnostic-query-test.cc` | `R25-02P`; `R25-09C` | Prove every `ZOM3027` fact-wire field, occurrence, provenance, contextual query projection, rendering handoff, and outer and inner mutation after the unified diagnostic wire cutover. | `python3 scripts/check-diagnostic-coverage.py --check`<br>`python3 scripts/check-diagnostic-coverage.py --self-test`<br>`python3 scripts/check-incremental-query-architecture.py --check`<br>`python3 scripts/check-compiler-session-architecture.py --check` | Blocked by the unified diagnostic wire cutover `R25-09C`; native execution waits for `R25-07T` |
| `R25-03` | `module-system` | `products/zomlang/compiler/identity/key/package-key.h`<br>`products/zomlang/compiler/identity/key/package-key.cc`<br>`products/zomlang/compiler/identity/key/crate-key.h`<br>`products/zomlang/compiler/identity/key/crate-key.cc`<br>`products/zomlang/compiler/identity/key/module-resolution-key.h`<br>`products/zomlang/compiler/identity/key/module-resolution-key.cc`<br>`products/zomlang/compiler/identity/key/source-key.h`<br>`products/zomlang/compiler/identity/key/source-key.cc`<br>`products/zomlang/compiler/identity/frozen-registry.h`<br>`products/zomlang/compiler/identity/semantic-identity-registry-set.h`<br>`products/zomlang/compiler/identity/semantic-identity-registry-set.cc`<br>`products/zomlang/compiler/identity/semantic/context-fingerprint.h`<br>`products/zomlang/compiler/identity/semantic/context-fingerprint.cc`<br>`products/zomlang/compiler/identity/identity-dump.h`<br>`products/zomlang/compiler/identity/identity-dump.cc`<br>`products/zomlang/compiler/identity/identity-invariant.h`<br>`products/zomlang/compiler/identity/identity-invariant.cc`<br>`products/zomlang/compiler/identity/canonical/canonical-encoder.h`<br>`products/zomlang/compiler/identity/canonical/canonical-encoder.cc`<br>`products/zomlang/compiler/identity/canonical/canonical-decoder.h`<br>`products/zomlang/compiler/identity/canonical/canonical-decoder.cc` | `R25-02` | Replace package-shaped core identity with one exhaustive unversioned compilation-unit identity; replace the hierarchy registry, frozen handles, context fingerprint, invariant, dump, codec, and every identity-internal caller in one cutover with no package accessor or decoder fallback. | `python3 scripts/check-identity-architecture.py --check`<br>`python3 scripts/check-no-internal-versioning.py --check` | Complete; exhaustive compilation-unit identity, hierarchy, strict codec, frozen handles, context fingerprint, invariant, dump, and production callers passed sanitizer build and all identity gates |
| `R25-03T` | `verification` | `products/zomlang/tests/unittests/compiler/identity/package-key-test.cc`<br>`products/zomlang/tests/unittests/compiler/identity/crate-key-test.cc`<br>`products/zomlang/tests/unittests/compiler/identity/source-key-test.cc`<br>`products/zomlang/tests/unittests/compiler/identity/frozen-registry-test.cc`<br>`products/zomlang/tests/unittests/compiler/identity/semantic-identity-registry-set-test.cc`<br>`products/zomlang/tests/unittests/compiler/identity/semantic-context-fingerprint-test.cc`<br>`products/zomlang/tests/unittests/compiler/identity/identity-dump-test.cc`<br>`products/zomlang/tests/unittests/compiler/identity/identity-invariant-test.cc` | `R25-03` | Add native proofs for both compilation-unit alternatives, complete transitive key bytes, registry hierarchy, frozen-handle ancestry, context fingerprint, dump, invariant, wrong-branch rejection, and the absence of package-only core accessors and decode fallbacks. | `python3 scripts/check-identity-architecture.py --check`<br>`python3 scripts/check-identity-architecture.py --self-test` | Complete; all eight focused identity tests, the complete unittest label, fixed vectors, strict negative cases, and architecture mutation fixtures passed |
| `R25-03C` | `module-system` | `products/zomlang/compiler/driver/package/package-compilation-request.cc`<br>`products/zomlang/compiler/driver/graph/crate-graph.cc` | `R25-02B`; `R25-03` | Replace every remaining production `CrateKey::package()` call in package request and crate graph construction with an exhaustive `CompilationUnitIdentity` branch, preserving package behavior and introducing the fixed core branch with no accessor fallback. | `python3 scripts/check-package-architecture.py --check`<br>`python3 scripts/check-identity-architecture.py --check` | Complete; package request and crate graph use exhaustive compilation-unit branches, retain no package-only accessor, and passed package, identity, build, and CTest gates |
| `R25-03CT` | `verification` | `products/zomlang/tests/unittests/compiler/binder/binding-input-test.cc`<br>`products/zomlang/tests/unittests/compiler/binder/frozen-definition-inventory-test.cc`<br>`products/zomlang/tests/unittests/compiler/binder/identity-pre-admission-test.cc`<br>`products/zomlang/tests/unittests/compiler/binder/import-binding-test.cc`<br>`products/zomlang/tests/unittests/compiler/binder/local-identity-test.cc`<br>`products/zomlang/tests/unittests/compiler/binder/module-body-syntax-test.cc`<br>`products/zomlang/tests/unittests/compiler/binder/module-dependency-requests-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/active-definition-authority-query-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/active-definition-authority-session-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/build-script-execution-key-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/compiler-session-package-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/incremental-binding-query-adapter-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/package-compilation-request-test.cc`<br>`products/zomlang/tests/unittests/compiler/identity/definition-key-test.cc`<br>`products/zomlang/tests/unittests/compiler/identity/module-resolution-key-test.cc`<br>`products/zomlang/tests/unittests/compiler/identity/semantic-import-binding-key-test.cc`<br>`products/zomlang/tests/unittests/compiler/hir/hir-module-test.cc`<br>`products/zomlang/tests/unittests/compiler/test-semantic-identities.h`<br>`products/zomlang/tests/unittests/compiler/type/semantic-type-key-test.cc` | `R25-02P`; `R25-03C`; `R25-03T` | Before the first complete build, migrate every remaining native caller of `CrateKey::from(PackageKey, ...)`, every direct `PackageId` construction, every `module.package()` comparison, every crate graph package read, and the shared semantic-identity helper to exhaustive compilation-unit assertions, including fixed-core wrong-branch rejection and no package-accessor fallback. | `python3 scripts/check-identity-architecture.py --check`<br>`python3 scripts/check-ir-architecture.py --check` | Complete; all Binder, Checker, Driver, identity, HIR, MIR, IR, and type callers migrated, package oracles regenerated, and the complete 210-test sanitizer CTest passed |
| `R25-04` | `module-system` | `products/zomcore/Zom.toml`<br>`products/zomlang/compiler/source/**`<br>`products/zomlang/compiler/driver/package/source-admission-limits.h`<br>`products/zomlang/compiler/driver/package/source-inventory.h`<br>`products/zomlang/compiler/driver/package/source-inventory.cc`<br>`products/zomlang/compiler/driver/package/source-record.h`<br>`products/zomlang/compiler/driver/package/source-record.cc`<br>`products/zomlang/compiler/driver/package/source-snapshot.h`<br>`products/zomlang/compiler/driver/package/source-snapshot.cc`<br>`products/zomlang/compiler/driver/package/source-tree.h`<br>`products/zomlang/compiler/driver/package/source-tree.cc`<br>`products/zomlang/compiler/driver/graph/module-discovery.h`<br>`products/zomlang/compiler/driver/graph/module-discovery.cc` | `R25-02B`; `R25-03`; `R25-03T` | Delete the package manifest and admit one fixed source layout through a canonical catalog and independently verified inventory with normalized paths, bounded inputs, and build/install parity. | `python3 scripts/check-impl-source-architecture.py --check`<br>`python3 scripts/check-package-architecture.py --check` | Complete; the package manifest and package-shaped prelude were deleted, the exact three-source tree now materializes and installs directly, distribution and catalog admission are independently verified, source drift fails closed, the focused native tests pass, and both architecture gates pass |
| `R25-04A` | `runtime-memory` | `products/zomcore/src/**`<br>`products/zomcore/README.md` | `R25-04`; `R25-08` | Implement the accepted core declarations in ZOM source and document their ownership, allocation, intrinsic, and contributor boundaries without adding a C++ runtime API duplicate. | `python3 scripts/check-no-internal-versioning.py --check`<br>`python3 scripts/check-impl-source-architecture.py --check` | In progress; the exact root, marker, and prelude sources and unversioned contributor boundary are present and pass source and versioning gates. Bootstrap semantic validation and the complete ownership and allocation documentation wait for `R25-08` |
| `R25-05G` | `verification` | `scripts/codegen/gen_core_library_inventory.py`<br>`products/zomlang/tests/unittests/compiler/driver/core-library-inventory-test.cc`<br>`products/zomlang/tests/unittests/compiler/basic/compiler-opts-test.cc`<br>`products/zomlang/tests/tools/check-package-invocation.py`<br>`products/zomlang/tests/integration/core-library/installed-consumer/Zom.toml`<br>`products/zomlang/tests/integration/core-library/installed-consumer/src/main.zom`<br>`products/zomlang/tests/cmake/verify-core-library-install-consumer.cmake`<br>`products/zomlang/tests/CMakeLists.txt` | `R25-04A` | Implement the single deterministic inventory generator, independent inventory oracle, closed compilation-action tests, real `zomc compile` conflict and no-artifact CLI cases, fixed installed-consumer package and target, and registered `core-library-install-consumer` CTest contract; the generator output is exactly `${PROJECT_BINARY_DIR}/generated/zom/core/core-library-inventory.inc`, and the installed test invokes only the accepted silent `compile --check` action. | `python3 scripts/codegen/gen_core_library_inventory.py --self-test` | Blocked by `R25-04A`; compiled CLI, unit, and installation evidence wait for `R25-05M`, `R25-05`, and `R25-07T` |
| `R25-05M` | `module-system` | `products/zomlang/compiler/driver/CMakeLists.txt` | `R25-02BC`; `R25-03C`; `R25-03CT`; `R25-04A`; `R25-08T`; `R25-09B`; `R25-09E`; `R25-11` | Extend the earlier diagnostic-query registration with every source-admission, core-distribution, capability-query, core provider, final-interface, borrow, prelude, and independent-verifier source after all production and native-test callers are migrated. | `python3 scripts/check-compiler-session-architecture.py --check`<br>`python3 scripts/check-incremental-query-architecture.py --check` | Blocked by every production and native-test consumer row; full build waits for `R25-05` |
| `R25-05` | `ir-backend` | `CMakeLists.txt`<br>`products/zomcore/CMakeLists.txt`<br>`products/zomlang/compiler/CMakeLists.txt`<br>`products/zomlang/compiler/basic/compiler-opts.h`<br>`products/zomlang/utils/CMakeLists.txt`<br>`products/zomlang/utils/zomc/CMakeLists.txt`<br>`products/zomlang/utils/zomc/zomc.cc` | `R25-04`; `R25-04A`; `R25-05G`; `R25-05M` | Register `generate-core-library-inventory`, make `zomc` depend on and embed its exact build-tree artifact, install the exact source-backed core tree, and resolve it only from the executable-relative toolchain layout. Replace output-type plus `syntaxOnly` state with one closed `CompilationAction` private to the existing `compile` subcommand; add the canonical silent `compile --check` action through production parse, bind, check, final interface, checked-module, HIR, Built MIR, and ownership-overlay verification; reject conflicting action/output selectors with the accepted exact messages; and request no LIR, backend, or artifact. This is the first complete sanitizer build after the atomic production cutover. | `cmake --preset sanitizer`<br>`cmake --build --preset sanitizer`<br>`cmake --build --preset sanitizer --target generate-core-library-inventory` | Blocked by `R25-04`, `R25-04A`, `R25-05G`, and the completed production caller and target-registration cutover in `R25-05M` |
| `R25-06` | `lexer-parser` | `products/zomlang/compiler/lexer/**`<br>`products/zomlang/compiler/parser/**` (excluding `products/zomlang/compiler/parser/parse-source-query*`)<br>`products/zomlang/compiler/ast/**`<br>`docs/spec/ZomLexer.g4`<br>`docs/spec/ZomParser.g4`<br>`docs/spec/chapters/02-lexical-structure.md`<br>`docs/spec/chapters/04-expressions.md`<br>`docs/spec/chapters/17-grammar-reference.md` | `R25-02` | Atomically remove the unused intrinsic token and AST kind, make `intrinsic` an ordinary identifier, synchronize both grammars and every owning normative chapter, and parse core roots, markers, and prelude declarations through the ordinary immutable AST. | `python3 scripts/check-lexer-architecture.py`<br>`python3 scripts/check-parser-coverage.py`<br>`python3 scripts/codegen/gen_ast.py --check`<br>`python3 scripts/check-no-internal-versioning.py --check` | Complete; the token, AST kind, lexer mapping, token spelling, and both grammar references were removed, the normative keyword inventory was synchronized, and every static gate passed |
| `R25-06T` | `verification` | `products/zomlang/tests/unittests/compiler/lexer/lexer-inventory-test.cc`<br>`products/zomlang/tests/unittests/compiler/lexer/lexer-identifier-test.cc`<br>`products/zomlang/tests/conformance/corpus/02-lexical/identifiers/intrinsic-as-identifier.zom`<br>`products/zomlang/tests/conformance/expectations/ast/02-lexical/identifiers/intrinsic-as-identifier.check` | `R25-06` | Author exact lexer unit and FileCheck coverage proving `intrinsic` is absent from the token inventory and is accepted as an ordinary identifier in every legal declaration, type, value, member, module, import-alias, pattern, and label position; execute that coverage only at `R25-07T` after the first complete build. | `python3 scripts/check-lexer-architecture.py`<br>`python3 scripts/check-parser-coverage.py` | Complete; focused lexer tests, the complete four-test lit label, the 49-case lexical grammar suite, AST/FileCheck coverage, lexer architecture, and parser coverage all passed |
| `R25-07` | `module-system` | `products/zomlang/compiler/query/query-database.h`<br>`products/zomlang/compiler/query/query-database.cc`<br>`products/zomlang/compiler/query/query-types.h`<br>`products/zomlang/compiler/query/query-types.cc`<br>`products/zomlang/compiler/identity/source-query-input.h`<br>`products/zomlang/compiler/identity/source-query-input.cc`<br>`products/zomlang/compiler/driver/session/compiler-session.h`<br>`products/zomlang/compiler/driver/session/compiler-session.cc`<br>`products/zomlang/compiler/driver/query/binding/incremental-binding-query-adapter.h`<br>`products/zomlang/compiler/driver/query/binding/incremental-binding-query-adapter.cc`<br>`products/zomlang/compiler/driver/query/module-graph/incremental-module-resolution-query.h`<br>`products/zomlang/compiler/driver/query/module-graph/incremental-module-resolution-query.cc`<br>`products/zomlang/compiler/driver/query/binding/incremental-package-graph-query-input.h`<br>`products/zomlang/compiler/driver/query/binding/incremental-package-graph-query-input.cc`<br>`products/zomlang/compiler/driver/query/binding/active-definition-authority-query.h`<br>`products/zomlang/compiler/driver/query/binding/active-definition-authority-query.cc`<br>`products/zomlang/compiler/driver/query/binding/active-definition-authority-session.h`<br>`products/zomlang/compiler/driver/query/binding/active-definition-authority-session.cc`<br>`products/zomlang/compiler/driver/query/binding/named-identity-inventory-query.h`<br>`products/zomlang/compiler/driver/query/binding/named-identity-inventory-query.cc`<br>`products/zomlang/compiler/driver/query/binding/named-item-query.h`<br>`products/zomlang/compiler/driver/query/binding/named-item-query.cc`<br>`products/zomlang/compiler/driver/query/binding/owner-body-query.h`<br>`products/zomlang/compiler/driver/query/binding/owner-body-query.cc`<br>`products/zomlang/compiler/driver/core-library-query-provider.h`<br>`products/zomlang/compiler/driver/core-library-query-provider.cc`<br>`products/zomlang/compiler/driver/core-library-query-verifier.h`<br>`products/zomlang/compiler/driver/core-library-query-verifier.cc` | `R25-02B`; `R25-03T`; `R25-04`; `R25-06T` | Delete the fixed compilation-unit key and package-only semantic root key; replace clone-based revision-local values with memo-owned snapshot-bound capability leases; contextualize authority, named-item, owner-body, bound-module, and diagnostic queries; and publish the complete core provider/verifier graph with exact read sets, negative caching, invalidation, and no package-resolver dependency. | `python3 scripts/check-compiler-session-architecture.py --check`<br>`python3 scripts/check-incremental-query-architecture.py --check`<br>`python3 scripts/check-identity-architecture.py --check` | In progress; the fixed key and old package-root semantic key are deleted, contextual authority, named, owner, core-crate, and core-module keys compile, crate-keyed parser options and the toolchain-core module-search-root branch have native coverage, `CoreDistributionInput` is registered, and retained module materialization provides independently reconstructed stable witnesses, transitive dependency retention, non-eviction, new-revision generations, and old-lease survival. One session resource arena owns their context. `ActiveCrates` plus `ActiveSources` derive exact user-package or toolchain-core membership from tracked inputs. `CoreModuleGraphQuery` and `CoreRoleSeedQuery` are independently verified; the module-graph staging boundary demands the graph and the checking boundary demands `MaterializeCoreRoleSeed`. `MaterializeCoreBootstrapModuleInterface` independently verifies and retains each initial core module's bound-module and role-seed leases, graph and export revisions, source-backed signatures, imported signature views, and the root, marker, and prelude surfaces. `FinalizeCoreModuleInterfaceQuery` independently reconstructs the handle-free final interface record for each of those modules; marker roles retain the exact `Copy` and `Linear` authority, and Prelude retains imported signature roots from the marker interface. `CompilerSession::materializeCoreLibrary` demands that final query for every core graph module and publishes only final interface leases. The role seed projects exact `Copy` and `Linear` keys, validates closed marker syntax and visibility, and maps missing or duplicate roles to the closed semantic failure algebra. General signature publication beyond the authorized declaration-only surface, diagnostic provenance, the remaining contextual consumer migration, and complete verification have not yet landed. |
| `R25-07P` | `lexer-parser` | `products/zomlang/compiler/parser/query/parse-source-query.h`<br>`products/zomlang/compiler/parser/query/parse-source-query.cc`<br>`products/zomlang/compiler/parser/query/parse-source-query-verifier.cc` | `R25-07` | Derive crate-keyed compilation options exclusively from `SourceFileKey.crate`, remove every fixed compilation-unit lookup from parser providers and verifiers, and preserve exact snapshot and read-set behavior. | `python3 scripts/check-parser-coverage.py`<br>`python3 scripts/check-incremental-query-architecture.py --check` | Complete in the coordinated cutover; provider and verifier independently decode the complete source key, derive its crate, read only that crate's options, and retain no fixed-key lookup or decoder |
| `R25-08` | `binder-checker` | `products/zomlang/compiler/checker/**` (excluding `products/zomlang/compiler/checker/checker-source-diagnostics.def`)<br>`products/zomlang/compiler/type/**`<br>`products/zomlang/compiler/binder/**` (excluding `products/zomlang/compiler/binder/module-*`) | `R25-06T`; `R25-07P`; RFC 0027 `R27-26` | Bind and bootstrap-check only the RFC-authorized declaration surface, publish signatures and semantic roles, migrate every binder/checker caller to contextual query keys and capability leases, and reject any unauthorized body or duplicate intrinsic behavior. | `python3 scripts/check-binder-architecture.py --check`<br>`python3 scripts/check-checker-architecture.py --check`<br>`python3 scripts/check-incremental-query-architecture.py --check` | In progress; the session materializes the verified source-backed core authority and projects its `Copy` and `Linear` marker policy into the Checker before body checking. The projection retains the authoritative declaration owners and supports unconditional references plus both raw-pointer mutabilities. The declaration-only root, marker, and prelude modules now publish source-backed final interface leases; the marker interface carries the canonical type-free `Copy` and `Linear` signatures, while the prelude independently retains their imported signature roots. Body checking, HIR construction and independent verification, and Built MIR construction and independent verification now retain same-module direct calls with an identifier callee, no type arguments, one checker-verified scalar literal argument for each parameter with an exact semantic type match, no raises clause, and no ABI override. They also retain one bounded mutable receiver form: a dot-member call on a mutable owner-local identifier. Built MIR emits its receiver borrow temporary and records the call as an explicit terminator with one source slot for each argument, a result destination, a normal continuation edge, and no unwind edge. The verified ownership-event overlay publishes argument source slots, the destination write, and the receiver's `BorrowActivation` only on that normal edge; the independent loan verifier reconstructs the temporary-to-activation relation, and both paths remain fail-closed for unwind. Checked-module assembly independently builds, verifies, adopts, and retains borrow evidence whose local inventory covers every non-enclosed callable signature; direct-call fixtures prove both zero-region summaries survive through the HIR-held lease. Generic function signatures retain their generic parameter inventory through HIR, Built MIR, and ownership-event verification. Generic direct-borrow signatures preserve shared and mutable parameter modes through body checking, HIR, Built MIR, and ownership-event verification. Type-bearing generic signatures reject a returned generic value without an explicit borrow contract. Production ownership proof publication and the remaining contextual consumer migration have not yet landed. |
| `R25-07T` | `verification` | `products/zomlang/tests/unittests/compiler/query/query-database-test.cc`<br>`products/zomlang/tests/unittests/compiler/query/query-red-green-test.cc`<br>`products/zomlang/tests/unittests/compiler/query/query-eviction-test.cc`<br>`products/zomlang/tests/unittests/compiler/query/query-concurrency-test.cc`<br>`products/zomlang/tests/unittests/compiler/query/query-observability-test.cc`<br>`products/zomlang/tests/unittests/compiler/query/query-capability-lease-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/incremental-binding-query-adapter-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/active-definition-authority-query-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/active-definition-authority-session-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/core-library-query-provider-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/contextual-query-key-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/query-read-set-test.cc`<br>`products/zomlang/tests/unittests/compiler/parser/parse-source-query-test.cc`<br>`products/zomlang/tests/performance/incremental-query-corpus.json`<br>`products/zomlang/tests/performance/incremental-query-baseline.json` | `R25-02C`; `R25-05`; `R25-07P`; `R25-08`; `R25-08T` | Prove capability ownership and leases, all contextual keys, exact provider/verifier read sets, fixed-key deletion, negative caching, invalidation, recomputed-equal shielding, single flight, cancellation, cross-snapshot rejection, mutation inventories, core-source conformance, signature and role authority, final-interface and borrow lineage, checked-module, HIR, MIR, and the full atomic identity/query/consumer cutover through native unit, lit, architecture, and benchmark gates. The benchmark corpus and baseline must contain explicit clean-core, unchanged-core, core-export, marker-policy, and semantic-role scenarios before comparison. If either input changes, record a baseline from the clean repository, inspect its deterministic diff, record the written cause and `verification` approval, commit the baseline, then reconfigure and clean-build that committed Release revision before comparison. | `cmake --preset sanitizer`<br>`cmake --build --preset sanitizer`<br>`ctest --preset default -L unittest --output-on-failure`<br>`ctest --preset default -L lit --output-on-failure`<br>`python3 scripts/check-identity-architecture.py --check`<br>`python3 scripts/check-identity-architecture.py --self-test`<br>`python3 scripts/check-incremental-query-architecture.py --check`<br>`python3 scripts/check-incremental-query-architecture.py --self-test`<br>`python3 scripts/check-compiler-session-architecture.py --check`<br>`python3 scripts/check-compiler-session-architecture.py --self-test`<br>`python3 scripts/check-binder-architecture.py --check`<br>`python3 scripts/check-binder-architecture.py --self-test`<br>`python3 scripts/check-checker-architecture.py --check`<br>`python3 scripts/check-checker-architecture.py --self-test`<br>`python3 scripts/check-ir-architecture.py --check`<br>`python3 scripts/check-ir-architecture.py --self-test`<br>`cmake --preset release`<br>`cmake --build --preset release --clean-first`<br>`python3 scripts/run-incremental-query-benchmarks.py --repository . --build-dir build-release --corpus products/zomlang/tests/performance/incremental-query-corpus.json --baseline products/zomlang/tests/performance/incremental-query-baseline.json --worker-count <machine-matched-count> --record-baseline`<br>`cmake --preset release`<br>`cmake --build --preset release --clean-first`<br>`python3 scripts/run-incremental-query-benchmarks.py --repository . --build-dir build-release --corpus products/zomlang/tests/performance/incremental-query-corpus.json --baseline products/zomlang/tests/performance/incremental-query-baseline.json --worker-count <machine-matched-count> --compare` | Blocked by `R25-02C`, the first complete build in `R25-05`, parser callers, binder/checker callers, and the complete pre-build semantic-test authoring row; baseline recording requires a clean repository and comparison requires the reviewed baseline to be committed |
| `R25-05I` | `verification` | `products/zomlang/tests/integration/core-library/installed-consumer/Zom.toml`<br>`products/zomlang/tests/integration/core-library/installed-consumer/src/main.zom`<br>`products/zomlang/tests/cmake/verify-core-library-install-consumer.cmake`<br>`products/zomlang/tests/CMakeLists.txt` | `R25-05`; `R25-07T` | Run exactly `<prefix>/bin/zomc compile --check --manifest-path <repository>/products/zomlang/tests/integration/core-library/installed-consumer/Zom.toml --bin installed-consumer` only after the complete production chain and native identity/query/core conformance gates pass. Verify installed core files and bytes, executable and fixture provenance, ordinary consumer checked-module/HIR/Built-MIR/ownership lineage, exit status `0`, empty output, and absence of any LIR, backend, or output artifact. | `ctest --preset default -R '^core-library-install-consumer$' --output-on-failure` | Blocked by `R25-05` and `R25-07T`. The registered test already executes direct, `PATH`, and symlinked installed `compile --check` invocations and passed on 2026-08-14, but it does not establish the complete prerequisite matrix or consumer lineage required to complete this task. |
| `R25-09A` | `module-system` | `products/zomlang/compiler/driver/interface/module-interface.h`<br>`products/zomlang/compiler/driver/interface/module-interface.cc`<br>`products/zomlang/compiler/driver/interface/imported-signature-view-projector.h`<br>`products/zomlang/compiler/driver/interface/imported-signature-view-projector.cc`<br>`products/zomlang/compiler/driver/session/compiler-session.h`<br>`products/zomlang/compiler/driver/session/compiler-session.cc` | `R25-07P`; `R25-08` | Finalize and publish one flat core module interface, privately consume then discard bootstrap-only records, and make those records unreachable from ordinary session consumers. | `python3 scripts/check-compiler-session-architecture.py --check`<br>`python3 scripts/check-incremental-query-architecture.py --check` | Blocked by `R25-07P` and `R25-08`; native build waits for `R25-05` |
| `R25-09C` | `error-system` with `module-system` and `verification` review | `products/zomlang/compiler/diagnostics/diagnostics-core.def`<br>`products/zomlang/compiler/diagnostics/core/diagnostic-ids.h`<br>`products/zomlang/compiler/diagnostics/core/diagnostic-info.h`<br>`products/zomlang/compiler/diagnostics/fact/diagnostic-fact.h`<br>`products/zomlang/compiler/diagnostics/fact/diagnostic-fact.cc`<br>`products/zomlang/compiler/diagnostics/fact/diagnostic-materializer.h`<br>`products/zomlang/compiler/diagnostics/fact/diagnostic-materializer.cc`<br>`products/zomlang/compiler/diagnostics/core-library-diagnostic-adapter.h`<br>`products/zomlang/compiler/diagnostics/core-library-diagnostic-adapter.cc`<br>`products/zomlang/compiler/diagnostics/CMakeLists.txt`<br>`products/zomlang/compiler/query/query-types.h`<br>`products/zomlang/compiler/query/query-types.cc`<br>`products/zomlang/compiler/driver/query/binding/incremental-binding-query-adapter.h`<br>`products/zomlang/compiler/driver/query/binding/incremental-binding-query-adapter.cc`<br>`products/zomlang/compiler/driver/query/binding/active-definition-authority-session.h`<br>`products/zomlang/compiler/driver/query/binding/active-definition-authority-session.cc`<br>`products/zomlang/compiler/driver/query/binding/incremental-package-graph-query-input.h`<br>`products/zomlang/compiler/driver/query/binding/incremental-package-graph-query-input.cc`<br>`products/zomlang/compiler/driver/incremental-diagnostic-query.h`<br>`products/zomlang/compiler/driver/incremental-diagnostic-query.cc`<br>`products/zomlang/compiler/driver/session/compiler-session.cc` | RFC 0029 `R29-14`; RFC 0042 `R42-16`; `R25-02A`; `R25-02B`; `R25-03`; `R25-07`; `R25-08`; `R25-09A` | Directly replace the closed Source-and-Module contract with the executable Source, Package, BuildScript, Module, and CoreLibrary occurrence, provenance, root, phase, emitter, typed-argument, source-or-locationless location, secondary, and fix-it schema; use lower-layer `PackageRootSetKey` for package resolution; register `ZOM7101`, `ZOM7102`, and `ZOM9907`; project `ZOM3027` and the closed core failures through the sole fact rail; publish contextual diagnostic queries; and retain no earlier-domain decoder, parallel codec, adapter, alias, or fallback. | `python3 scripts/check-diagnostic-coverage.py --check`<br>`python3 scripts/check-diagnostic-coverage.py --self-test`<br>`python3 scripts/check-incremental-query-architecture.py --check`<br>`python3 scripts/check-incremental-query-architecture.py --self-test` | RFC 0042 `R42-16` is satisfied by `58897c116cafe3463ec6a46ac3bbdd530ef991a5`; blocked by RFC 0029 `R29-14`, `R25-02A`, `R25-02B`, `R25-03`, `R25-07`, `R25-08`, and `R25-09A`; native build waits for `R25-05` |
| `R25-09E` | `module-system` | `products/zomlang/compiler/driver/session/compiler-session.h`<br>`products/zomlang/compiler/driver/session/compiler-session.cc`<br>`products/zomlang/compiler/driver/incremental-diagnostic-query.h`<br>`products/zomlang/compiler/driver/incremental-diagnostic-query.cc`<br>`products/zomlang/compiler/query/query-types.h`<br>`products/zomlang/compiler/query/query-types.cc` | `R25-09C` | Publish core failure facts through the same contextual diagnostic query, preserve exact read sets and outer context keys, and route no core failure around the accepted fact and provenance codecs. | `python3 scripts/check-incremental-query-architecture.py --check`<br>`python3 scripts/check-compiler-session-architecture.py --check` | Blocked by `R25-09C`; native build waits for `R25-05` |
| `R25-09D` | `verification` | `products/zomlang/tests/unittests/compiler/diagnostics/core-library-diagnostic-adapter-test.cc`<br>`products/zomlang/tests/unittests/compiler/diagnostics/diagnostic-fact-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/compiler-session-diagnostic-query-test.cc`<br>`products/zomlang/tests/conformance/corpus/13-modules/core_library_diagnostics.zom`<br>`products/zomlang/tests/conformance/expectations/diagnostics/13-modules/core_library_diagnostics.check` | `R25-02C`; `R25-07T`; `R25-09E` | Prove every core failure category, code, coordinate, cause, root, phase, emitter, occurrence index, locationless origin, render result, failure status, suppression edge, builder/verifier disagreement, outer and inner tag, field, contextual query projection, and sequence-framing mutation through native unit and FileCheck tests. | `cmake --preset sanitizer`<br>`cmake --build --preset sanitizer`<br>`ctest --preset default -L unittest --output-on-failure`<br>`ctest --preset default -L lit --output-on-failure`<br>`python3 scripts/check-diagnostic-coverage.py --check`<br>`python3 scripts/check-diagnostic-coverage.py --self-test` | Blocked by `R25-02C`, the complete native cutover gate `R25-07T`, and `R25-09E`; must reconfigure and rebuild after authoring the diagnostic tests |
| `R25-09B` | `ir-backend` | `products/zomlang/compiler/hir/checked-module.h`<br>`products/zomlang/compiler/hir/checked-module.cc`<br>`products/zomlang/compiler/hir/hir-module.h`<br>`products/zomlang/compiler/hir/hir-module.cc`<br>`products/zomlang/compiler/mir/built-mir.h`<br>`products/zomlang/compiler/mir/built-mir.cc` | `R25-09A`; `R25-10` | Replace every package-shaped identity consumer, consume the flat imported interface and completed borrow-evidence cutover in ordinary checked-module, HIR, and MIR lineage, and prove that declaration-only core modules never enter those stages. | `python3 scripts/check-ir-architecture.py --check`<br>`python3 scripts/check-ir-architecture.py --self-test` | Blocked by `R25-09A` and `R25-10`; native build waits for `R25-05` |
| `R25-10` | `module-system` | `products/zomlang/compiler/driver/interface/borrow-evidence.h`<br>`products/zomlang/compiler/driver/interface/borrow-evidence.cc` | `R25-09A` | Derive callable-driven borrow evidence from the flat final interface, with `runtime-memory` required review, and prevent bootstrap authorization data from reaching ordinary consumers. | `python3 scripts/check-compiler-session-architecture.py --check`<br>`python3 scripts/check-ir-architecture.py --check` | Blocked by `R25-09A`; `runtime-memory` review required; native build waits for `R25-05` |
| `R25-08T` | `verification` | `products/zomlang/tests/unittests/compiler/checker/signature-facts-test.cc`<br>`products/zomlang/tests/unittests/compiler/checker/borrow-interface-test.cc`<br>`products/zomlang/tests/unittests/compiler/checker/checked-facts-test.cc`<br>`products/zomlang/tests/unittests/compiler/checker/cross-module-facts-test.cc`<br>`products/zomlang/tests/unittests/compiler/checker/dispatch-facts-test.cc`<br>`products/zomlang/tests/unittests/compiler/checker/marker-proof-test.cc`<br>`products/zomlang/tests/unittests/compiler/checker/core-signature-bootstrap-test.cc`<br>`products/zomlang/tests/unittests/compiler/checker/core-role-seed-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/module-interface-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/borrow-evidence-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/core-final-interface-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/compiler-session-package-test.cc`<br>`products/zomlang/tests/unittests/compiler/driver/compiler-session-test.cc`<br>`products/zomlang/tests/unittests/compiler/hir/hir-module-test.cc`<br>`products/zomlang/tests/unittests/compiler/mir/built-mir-test.cc`<br>`products/zomlang/tests/unittests/compiler/ownership/ownership-event-overlay-test.cc` | `R25-03CT`; `R25-08`; `R25-09A`; `R25-09B`; `R25-10` | Before the first complete build, migrate every live native caller of the replaced signature, body, coherence, marker-proof, tagged imported-interface, final-interface, borrow-evidence, checked-module, HIR, and MIR contracts and add dedicated role-seed, signature-bootstrap, and flat-final-interface proofs. Install a real verified core distribution in every binder, checker, package, incremental-session, HIR, and MIR fixture. Cover the complete initial signature algebra, forbidden declaration forms, role seed vector and mutations, tagged revision codecs and golden vectors, authority/policy/proof lineage, bootstrap-record exclusion, exhaustive interface-source and revision alternatives, callable-driven borrow evidence, declaration-only core rejection from checked-module/HIR/MIR, exact RFC 0005 diagnostics, and exact RFC 0010 failure mappings. | `python3 scripts/check-binder-architecture.py --check`<br>`python3 scripts/check-checker-architecture.py --check`<br>`python3 scripts/check-compiler-session-architecture.py --check`<br>`python3 scripts/check-ir-architecture.py --check` | Blocked by the complete production semantic and IR cutover; the live old-contract and session-fixture inventory must be empty before `R25-05`, with native execution in `R25-07T` after the first complete build |
| `R25-11` | `module-system` | `products/zomlang/compiler/driver/session/compiler-session.h`<br>`products/zomlang/compiler/driver/session/compiler-session.cc`<br>`products/zomlang/compiler/driver/graph/module-discovery.h`<br>`products/zomlang/compiler/driver/graph/module-discovery.cc`<br>`products/zomlang/compiler/driver/query/module-graph/incremental-module-resolution-query.h`<br>`products/zomlang/compiler/driver/query/module-graph/incremental-module-resolution-query.cc`<br>`products/zomlang/compiler/binder/graph/module-dependency-requests.h`<br>`products/zomlang/compiler/binder/graph/module-dependency-requests.cc`<br>`products/zomlang/compiler/binder/graph/module-resolution.h`<br>`products/zomlang/compiler/binder/graph/module-resolution.cc` | `R25-04A`; `R25-08`; `R25-09A`; `R25-09E`; `R25-10` | Replace every package-shaped module-resolution caller, inject the exact target-and-host prelude projection into each non-core module once after diagnostic query publication, preserve explicit imports, and prove that core modules have no prelude self-edge. | `python3 scripts/check-compiler-session-architecture.py --check`<br>`python3 scripts/check-incremental-query-architecture.py --check` | Blocked by `R25-04A`, `R25-08`, `R25-09A`, `R25-09E`, and `R25-10`; native build waits for `R25-05` |
| `R25-12G` | `rfc` | `docs/rfc/0023-ide-semantic-snapshots-and-language-server-architecture.md` (read-only verification)<br>`docs/rfc/tracking/0023-review-and-implementation.md` (read-only verification)<br>`docs/rfc/tracking/0025-review-and-implementation.md` | `R25-02`; RFC 0023 `ACCEPTED`; RFC 0023 named production foundation complete | Record RFC 0023's exact accepted hash and verify that its tracker marks `Recoverable parser and CST`, `Verified AST bridge`, `Workspace, editor inputs, and leases`, `IDE query family`, `Partial semantics`, `RFC 0022 integration`, `IDE facade`, `LSP adapter`, and `Initial language features` complete before RFC 0025 creates or changes an IDE or LSP product. | `python3 scripts/check-rfc.py`<br>`git diff --check` | Externally blocked while RFC 0023 remains `REVIEW` and its production foundation is incomplete |
| `R25-12` | `tooling-lsp` | `products/zomlang/tools/ide/**`<br>`products/zomlang/tools/lsp/**`<br>`editors/**` | `R25-05I`; `R25-09A`; `R25-11`; `R25-12G` | Resolve hover, navigation, completion, and diagnostics for core definitions to canonical installed ZOM source locations without exposing bootstrap-only records. The slice extends only the accepted, production RFC 0023 products and cannot create an alternate IDE or LSP architecture. | `cmake --preset sanitizer`<br>`cmake --build --preset sanitizer` | Blocked by the installed-consumer gate `R25-05I`, `R25-09A`, `R25-11`, and the external RFC 0023 foundation gate `R25-12G` |
| `R25-12A` | `verification` | `products/zomlang/tests/unittests/tools/ide/core-library-source-navigation-test.cc`<br>`products/zomlang/tests/unittests/tools/lsp/core-library-source-navigation-test.cc` | `R25-12` | Prove exact installed-source navigation, hover/completion locations, immutable snapshots, stale suppression, cancellation, missing-source failure, bootstrap exclusion, and compiler/tooling differential equality through the accepted RFC 0023 product surfaces. | `cmake --preset sanitizer`<br>`cmake --build --preset sanitizer`<br>`ctest --preset default -L unittest --output-on-failure`<br>`ctest --preset default -L lit --output-on-failure` | Blocked by `R25-12`; must reconfigure and rebuild after authoring the tooling tests |
| `R25-13B` | `binder-checker` | `docs/spec/chapters/03-types.md`<br>`docs/spec/chapters/06-declarations.md`<br>`docs/spec/chapters/09-interfaces.md`<br>`docs/spec/chapters/12-generics.md` | `R25-08`; `R25-09A`; `R25-10` | Align type, declaration, interface, and generic contracts with the accepted source-backed signatures, role authority, final interface, and borrow boundary. | `ctest --preset default -L lit --output-on-failure`<br>`python3 scripts/check-binder-architecture.py --check`<br>`python3 scripts/check-checker-architecture.py --check` | Blocked by `R25-08`, `R25-09A`, and `R25-10` |
| `R25-13M` | `module-system` | `docs/spec/chapters/13-modules-and-imports.md` | `R25-02P`; `R25-03`; `R25-04`; `R25-07`; `R25-09A`; `R25-11` | Align module identity, reserved core root, source admission, import, prelude, and publication contracts with the accepted implementation. | `ctest --preset default -L lit --output-on-failure`<br>`python3 scripts/check-package-architecture.py --check`<br>`python3 scripts/check-compiler-session-architecture.py --check` | Blocked by the module implementation rows |
| `R25-13R` | `runtime-memory` | `docs/spec/chapters/14-memory-management.md` | `R25-04A`; `R25-08`; `R25-10` | Align ownership, allocation, intrinsic, and borrow-surface claims with the accepted ZOM core declarations and fail-closed implementation. | `ctest --preset default -L lit --output-on-failure`<br>`python3 scripts/check-checker-architecture.py --check` | Blocked by `R25-04A`, `R25-08`, and `R25-10` |
| `R25-13C` | `concurrency` | `docs/spec/specification.md`<br>`docs/spec/chapters/15-concurrency.md`<br>`docs/concurrency/zom-async-canonical-design.md` (delete) | `R25-04A`; `R25-08` | Delete the speculative concurrency design in full; make the specification index and Chapter 15 describe only live parser and frontend facts; retain no unsupported semantic, checker, diagnostic, task, scheduler, cancellation, observability, stack, unwind, runtime, FFI, memory-model, ABI, standard-library, or marker claim; add no concurrency behavior; and record the RFC's deletion-only `ultracode-audit` waiver. | `ctest --preset default -L lit --output-on-failure`<br>`python3 scripts/check-no-internal-versioning.py --check` | Blocked by `R25-04A` and `R25-08` |
| `R25-13T` | `tooling-lsp` | `docs/design/tooling/**` | `R25-12`; `R25-12A` | Align tooling architecture with canonical installed-source locations, immutable snapshots, stale suppression, and bootstrap-record exclusion. | `ctest --preset default -L unittest --output-on-failure` | Blocked by `R25-12` and `R25-12A` |
| `R25-13V` | `verification` | `scripts/check-core-library-spec-alignment.py` | `R25-06T`; `R25-13B`; `R25-13M`; `R25-13R`; `R25-13C`; `R25-13T` | Implement the registered project-native five-way core-library alignment gate with a fixed inventory that includes `docs/spec/specification.md` and requires both deleted paths to be absent. Across every retained `docs/**` artifact, reject inline or reference-style Markdown links whose absolute repository or normalized relative destination resolves to either deleted path. Reject plain-text occurrences in every retained `docs/**` artifact except non-link decision history under `docs/rfc/**` and `docs/reports/**`. The isolated temporary-tree self-test restores each deleted file; makes the complete inline/reference-style by absolute-repository/normalized-relative four-link matrix fail under both RFC and report fixtures; makes non-link path text under both fixtures pass; makes plain-text path references in top-level and nested current-state fixtures fail; then independently restores every prohibited semantic, checker, diagnostic, task, scheduler, cancellation, observability, stack, unwind, runtime, FFI, memory-model, ABI, standard-library, marker, and compatibility claim. Every prohibited mutation must fail and every allowed-history fixture must pass. | `python3 scripts/check-core-library-spec-alignment.py --self-test` | Blocked by the atomic lexical/spec cutover and every remaining specialized documentation row |
| `R25-13` | `spec-audit` | `docs/overview.md`<br>`docs/spec/chapters/16-attributes-and-annotations.md`<br>`docs/spec/chapters/18-ffi-and-interop.md`<br>`docs/design/algebraic-data-types.md`<br>`docs/design/compiler-contracts.md`<br>`docs/design/runtime-ffi-examples.md` (delete)<br>`docs/reports/zom-core-library-spec-alignment.md` | `R25-09B`; `R25-09C`; `R25-09D`; `R25-13V` | Align the remaining cross-cutting specification and design surfaces, audit every specialized-owner result, delete the speculative runtime FFI design in full, remove every Markdown link resolving to either deleted path from retained documentation and every plain-text deleted-path reference outside RFC/report decision history, preserve only non-link RFC/report history, retain no unsupported semantic, checker, diagnostic, task, scheduler, cancellation, observability, stack, unwind, runtime, FFI, memory-model, ABI, future-contract, versioning, or compatibility prose, and publish the fixed zero-drift report without taking primary ownership of another subsystem's files. | `python3 scripts/check-core-library-spec-alignment.py --check --report docs/reports/zom-core-library-spec-alignment.md`<br>`python3 scripts/check-rfc.py`<br>`python3 scripts/check-no-internal-versioning.py --check`<br>`python3 scripts/check-format.py` | Blocked by the completed implementation, diagnostic, and specialized documentation rows; `concurrency` must review both file deletions |
| `R25-14` | `verification` | `scripts/check-core-library-architecture.py`<br>`scripts/check-ownership-architecture.py`<br>`scripts/check-english-only.py`<br>`products/zomlang/tests/CMakeLists.txt`<br>`.github/workflows/**` | `R25-02BA`; `R25-02BC`; `R25-02C`; `R25-03`; `R25-03C`; `R25-03CT`; `R25-03T`; `R25-04`; `R25-04A`; `R25-05`; `R25-05G`; `R25-05I`; `R25-05M`; `R25-06`; `R25-06T`; `R25-07`; `R25-07P`; `R25-07T`; `R25-08`; `R25-08T`; `R25-09A`; `R25-09B`; `R25-09C`; `R25-09D`; `R25-09E`; `R25-10`; `R25-11`; `R25-12`; `R25-13` | Register the core-library architecture gate, accepted RFC 0007 ownership architecture gate, and reusable implementation-series English-only gate in the test CMake graph and CI; reject prohibited core identity, lookup, bootstrap leakage, duplicate API, missing installed source, missing ownership-evidence patterns, and CJK characters in changed text artifacts. | `python3 scripts/check-core-library-architecture.py --check`<br>`python3 scripts/check-core-library-architecture.py --self-test`<br>`python3 scripts/check-ownership-architecture.py --check`<br>`python3 scripts/check-ownership-architecture.py --self-test`<br>`python3 scripts/check-english-only.py --self-test`<br>`python3 scripts/check-english-only.py --check --base <implementation-series-merge-base>`<br>`cmake --preset sanitizer`<br>`cmake --build --preset sanitizer`<br>`ctest --preset default --output-on-failure` | Blocked by all implementation and documentation rows; must reconfigure and rebuild after changing the test CMake graph |
| `R25-14C` | `verification` | `scripts/run-ownership-coverage.py`<br>`scripts/check-ownership-coverage.py`<br>`products/zomlang/tests/coverage/ownership-exemptions.json`<br>`.github/workflows/**` | `R25-14` | Implement and register RFC 0007's accepted fail-hard coverage runner, checker, deterministic evidence, mutation self-test, and retained CI artifacts. The census is the union of RFC 0007's ownership census and every non-test compiler `.cc` file added or modified between the RFC 0025 implementation-series merge base and head. Require at least 70 percent line coverage per file, no aggregate baseline regression, and only exact approved, unexpired exemptions with path, uncovered ranges, technical reason, `verification` approval, and expiry commit. | `python3 scripts/check-ownership-coverage.py --self-test`<br>`python3 scripts/run-ownership-coverage.py`<br>`python3 scripts/check-ownership-coverage.py` | Blocked by `R25-14`; missing profiles, empty or incomplete census, malformed LLVM output, below-threshold files, baseline regression, and invalid exemptions must fail |
| `R25-15` | `verification` | `products/zomlang/tests/**`<br>`scripts/codegen/gen_core_library_inventory.py`<br>`scripts/check-core-library-architecture.py`<br>`scripts/check-core-library-spec-alignment.py`<br>`scripts/check-english-only.py`<br>`scripts/check-ownership-architecture.py`<br>`scripts/run-ownership-coverage.py`<br>`scripts/check-ownership-coverage.py`<br>`products/zomlang/tests/coverage/ownership-exemptions.json`<br>`.github/workflows/**`<br>`README.md` | `R25-00`; `R25-01`; `R25-02`; `R25-02A`; `R25-02AS`; `R25-02BA`; `R25-02B`; `R25-02BC`; `R25-02C`; `R25-03`; `R25-03C`; `R25-03CT`; `R25-03T`; `R25-04`; `R25-04A`; `R25-05G`; `R25-05I`; `R25-05M`; `R25-05`; `R25-06`; `R25-06T`; `R25-07`; `R25-07P`; `R25-07T`; `R25-08`; `R25-08T`; `R25-09A`; `R25-09B`; `R25-09C`; `R25-09D`; `R25-09E`; `R25-10`; `R25-11`; `R25-12G`; `R25-12`; `R25-12A`; `R25-13B`; `R25-13M`; `R25-13R`; `R25-13C`; `R25-13T`; `R25-13V`; `R25-13`; `R25-14`; `R25-14C` | Prove the accepted design from a clean sanitizer build through unit, lit, fixed installation, deterministic inventory generation, every owner architecture gate, fail-hard per-file coverage, changed-file English-only enforcement, the clean Release benchmark protocol, RFC/versioning/format checks, and final required-owner evidence review. | `cmake --preset sanitizer`<br>`cmake --build --preset sanitizer --clean-first`<br>`cmake --build --preset sanitizer --target generate-core-library-inventory`<br>`python3 scripts/codegen/gen_core_library_inventory.py --self-test`<br>`ctest --preset default --output-on-failure`<br>`ctest --preset default -R '^core-library-install-consumer$' --output-on-failure`<br>`python3 scripts/check-lexer-architecture.py`<br>`python3 scripts/check-parser-coverage.py`<br>`python3 scripts/codegen/gen_ast.py --check`<br>`python3 scripts/check-impl-source-architecture.py --check`<br>`python3 scripts/check-impl-source-architecture.py --self-test`<br>`python3 scripts/check-package-architecture.py --check`<br>`python3 scripts/check-package-architecture.py --self-test`<br>`python3 scripts/codegen/gen_package_oracles.py --check`<br>`python3 scripts/codegen/gen_package_oracles.py --self-test`<br>`python3 scripts/generate-canonical-header-syntax-schema.py --check`<br>`python3 scripts/generate-canonical-header-syntax-schema.py --self-test`<br>`python3 scripts/check-diagnostic-coverage.py --check`<br>`python3 scripts/check-diagnostic-coverage.py --self-test`<br>`python3 scripts/check-lit-exec-root.py --check`<br>`python3 scripts/check-lit-exec-root.py --self-test`<br>`python3 scripts/check-identity-architecture.py --check`<br>`python3 scripts/check-identity-architecture.py --self-test`<br>`python3 scripts/check-incremental-query-architecture.py --check`<br>`python3 scripts/check-incremental-query-architecture.py --self-test`<br>`python3 scripts/check-compiler-session-architecture.py --check`<br>`python3 scripts/check-compiler-session-architecture.py --self-test`<br>`python3 scripts/check-binder-architecture.py --check`<br>`python3 scripts/check-binder-architecture.py --self-test`<br>`python3 scripts/check-checker-architecture.py --check`<br>`python3 scripts/check-checker-architecture.py --self-test`<br>`python3 scripts/check-ir-architecture.py --check`<br>`python3 scripts/check-ir-architecture.py --self-test`<br>`python3 scripts/check-core-library-architecture.py --check`<br>`python3 scripts/check-core-library-architecture.py --self-test`<br>`python3 scripts/check-core-library-spec-alignment.py --check --report docs/reports/zom-core-library-spec-alignment.md`<br>`python3 scripts/check-core-library-spec-alignment.py --self-test`<br>`python3 scripts/check-ownership-architecture.py --check`<br>`python3 scripts/check-ownership-architecture.py --self-test`<br>`python3 scripts/check-ownership-coverage.py --self-test`<br>`python3 scripts/run-ownership-coverage.py`<br>`python3 scripts/check-ownership-coverage.py`<br>`cmake --preset release`<br>`cmake --build --preset release --clean-first`<br>`python3 scripts/run-incremental-query-benchmarks.py --repository . --build-dir build-release --corpus products/zomlang/tests/performance/incremental-query-corpus.json --baseline products/zomlang/tests/performance/incremental-query-baseline.json --worker-count <machine-matched-count> --compare`<br>`python3 scripts/check-english-only.py --self-test`<br>`python3 scripts/check-english-only.py --check --base <implementation-series-merge-base>`<br>`python3 scripts/check-rfc.py`<br>`python3 scripts/check-no-internal-versioning.py --check`<br>`python3 scripts/check-format.py`<br>`git diff --check` | Blocked by every remaining prior task |

### 2026-07-27 RFC 0027 Current Implementation Binding

The RFC 0027 transaction
`rfc0027-accept-20260727-e2f4ba5e` establishes the current implementation
authority for the Binder, identity, graph, session, core-bootstrap, diagnostic,
and downstream-lineage portions of this tracker. Completed rows below remain
historical implementation evidence only where their product surfaces satisfy
the synchronized contract. Current completion requires RFC 0027 tasks
`Q1-Q4`, `I1-I2`, `B1-B4`, `M1-M5`, `C1-C2`, `L1-L4`, `T1-T2C`, `D1-D5`,
`W1-W4`, `E1-E8`, and `A1-A3`; none is marked complete by this documentation
transaction.

The `R25-08` bootstrap and Checker cutover begins only after RFC 0027
`R27-26`. That dependency transitively requires typed named inventories,
independently verified definition and implementation headers, the semantic
Binder query chain, `VerifyBoundModule`, and `CheckerBoundModuleView`. This
tracker does not treat a public type, draft producer, or focused test as a
substitute for that production capability chain.

### 2026-07-27 RFC 0029 Current Implementation Binding

Transaction `rfc0030-accept-20260728-4ed0e6b8` establishes the
stable-foundation and dependency authority for this tracker. The Binder
schema, facts, and codecs land
atomically through the exact RFC 0030 `R29-12AB` transaction; `R29-12D` then
lands as the separate diagnostic transaction. `R25-07` and `R25-07T` may
claim completion only after the applicable `R29-14`, RFC 0028 `R28-16`, and
`R29-15` dependencies above have completed.

### 2026-07-26 Core Pre-Parse Transaction And Module-Graph Progress

The coordinated `R25-07` and `R25-11` cutover now admits the verified
distribution through one atomic pre-parse query-input transaction. It derives
the exact projected `Toolchain(Core)` crate set, writes contextual compilation
options, module-search roots, and source snapshots without partial mutation,
registers the three admitted sources in `CompilerSession`, and parses them
through the ordinary parser.

`VerifiedModuleGraphVerifier` now independently recomputes the complete semantic
fingerprint from the tracked toolchain distribution input, package edges, and
the exact implicit consumer-to-core crate edges. Toolchain-owned `core` is
excluded from the user-package reserved-root diagnostic, while a user package
claiming that root remains rejected. CLI AST emission projects only
user-package syntax by typed compilation-unit identity and does not expose
toolchain modules.

Focused sanitizer tests for the atomic transaction, strict identity projection,
complete session graph, and module graph passed. The AST coverage gate and full
AST conformance suite passed. Package parsing now fails closed when the core
distribution is absent. `R25-07` remains in progress until all stable and
capability queries, session barriers, and independent verification are
complete; `R25-11` remains blocked on the bootstrap, final-interface,
diagnostic, borrow, and prelude rows.

The `R25-07` implementation review then found that RFCs 0017 and 0025 name
`ModuleGraph` and `ModuleGraphScc` without defining their complete value
records, canonical domains, failure codecs, tracked read sets, or independent
verifier algorithms. Product implementation stopped before inventing those
contracts. RFC 0026, `Module Graph Query Closure`, is now `ACCEPTED` on exact
proposal SHA-256
`39df5d3f11dbdcb2e95056b1cd14fd5220a19688f31a3e3180230ad465a3f84d`
after approval by `rfc`, `module-system`, `binder-checker`, and `verification`.
Its acceptance transaction synchronized RFCs 0017, 0018, 0019, 0020, and 0025
plus their trackers. The graph-query, authority-staging, and Binder-bootstrap
portion of `R25-07` now proceeds only through RFC 0026 tasks `R26-05` through
`R26-09`; no product implementation predating that contract counts as
completion evidence.

`R25-03`, `R25-03C`, `R25-03CT`, `R25-03T`, `R25-06`, `R25-06T`, `R25-07`, `R25-07P`,
`R25-08`, `R25-08T`, `R25-04A`, `R25-09A`, `R25-09C`, `R25-09E`, `R25-10`,
`R25-09B`, `R25-11`, `R25-05M`, and `R25-05` form one coordinated,
uncommitted identity, syntax, query, final-interface, diagnostic, borrow,
prelude, IR-consumer, and build cutover. No row in that set may land
independently. Static owner gates run while the replacement is in progress;
`R25-05` supplies the first complete sanitizer build only after every
production caller and target registration is present. `R25-07T` then supplies
the complete unit, lit, lease, read-set, invalidation, and benchmark evidence;
`R25-09D` supplies the exact core-diagnostic unit and FileCheck evidence; and
`R25-05I` supplies the installed-consumer evidence. The cutover contains no old
decoder, fixed-key lookup, clone fallback, adapter, shim, or dual path at any
intermediate or final boundary.

`R25-02` is the sole transition into product implementation. Its completion
authorizes rows `R25-02A` through `R25-15` only in the dependency order shown
above. No row may skip a dependency, and the tooling slice remains externally
blocked by `R25-12G`.

## Verification Evidence

- Live inspection confirmed the package-shaped core seed and its two ZOM marker
  declarations.
- Live inspection confirmed that production `CompilerSession` materializes the
  final verified core authority and final module interfaces, authorizes the
  configured prelude and its defining modules, and projects the source-backed
  marker policy into Checker. Bootstrap role-seed, bootstrap-interface, and
  export-surface queries remain inside the core provider and verifier boundary.
- RFC 0024 was reviewed for package-release bootstrap, configured-prelude,
  semantic-role authority, policy, proof, and fail-closed requirements.
- Rust, Swift, Zig, and Go primary documentation and source repositories were
  reviewed for the core/source/runtime/compiler boundary.
- All twelve required owners independently approved proposal SHA-256
  `4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`
  with zero critical, major, or minor findings. The reviewed tracker SHA-256
  was
  `1746140fc18d7fef0551360d5d95c2d2dd9ee4d28826e7c00738219b259261eb`.
- RFC 0027 acceptance synchronization records proposal SHA-256
  `e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`
  and transaction `rfc0027-accept-20260727-e2f4ba5e`. It changes design and
  tracker authority only; the RFC 0027 implementation and evidence DAG remains
  pending.
- RFC 0028 acceptance synchronization records proposal SHA-256
  `944b68ffc0aff5576d079a243ff092d7d19fba5ffed65551dda8e68adf230db4`
  and transaction `rfc0028-accept-20260727-944b68ff`. `R28-13A` through
  `R28-14` are complete through RFC 0029; `R28-16A` through `R28-19` remain
  pending.
- RFC 0029 acceptance synchronization records proposal SHA-256
  `8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`
  and transaction `rfc0029-accept-20260727-8d393a0c`. `R29-12A` through
  `R29-17` are complete; RFC 0029 is `LANDED`.

## Blocking Dependencies

- RFC 0029 completed the runtime prerequisites through `R29-17`.
- RFC 0028 `R28-16` and the RFC 0029 owner-body prerequisites are satisfied.
  `R25-07` and `R25-07T` still require their remaining local implementation
  and verification evidence.
- `R25-12G` remains externally blocked by RFC 0023 acceptance and its named
  production foundation.
