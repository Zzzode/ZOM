# RFC 0019 Review And Implementation Tracker

This document records discussion, exact-snapshot owner review, decisions, and
implementation evidence for RFC 0019. The RFC frontmatter remains
authoritative for status and approvers. This tracker does not approve the
proposal or authorize implementation.

## Discussion Record

### 2026-07-19 Implementation Discovery

The RFC 0017 and RFC 0018 implementation cutover exposed two instances of one
missing semantic boundary:

- `docs/spec/ZomParser.g4` admits `statement` as a `moduleItem`, while
  `OwnerLocalBindingKey`, `AnonymousOwnerLocalKey`, and
  `BindDefinitionBody` require a named `DefinitionKey` owner. Module-owned
  loop patterns, match patterns, nested block locals, labels, and anonymous
  callables therefore have no stable body owner.
- `CallableParameterIdentityRecord` requires an owning `DefinitionKey`, and
  stable generic parameter identity requires a definition or implementation
  owner. Parameters of a closure or function expression are inventoried as
  subordinate parameters but have no admissible stable owner. Skipping them
  loses binding, activation, capture, and materialization facts; assigning
  them a distant named owner violates immediate callable ownership.

The implementation stopped rather than adding a producer-only module special
case or synthetic definition. RFC 0019 proposes one closed body owner and
keeps anonymous subordinate parameters in the same owner-local body domain.

### 2026-07-19 Prior-Art And Contract Review

The proposal was checked against rustc HIR owner-local ids, owner-scoped HIR
lowering, THIR body owners, rust-analyzer item/body/source-map boundaries, and
Swift top-level code contexts. The resulting contract uses:

- `StableBodyOwnerKey = ModuleOwner(ModuleKey) |
  DefinitionOwner(DefinitionKey)`;
- module-rooted or named-item-rooted `LocalSyntaxPath` selected by that owner;
- owner-local callable and generic parameter kinds for anonymous callables;
- one owner-body query, projection, materialization, closure, scope, and
  diagnostic family; and
- independent owner/path/activation/capture reconstruction in the verifier.

The contract is a direct replacement. It contains no definition-only decoder,
dual query registration, synthetic initializer definition, or global
subordinate parameter identity for an anonymous callable.

### 2026-07-19 Formal Review Entry

RFC 0019 entered `REVIEW` at exact proposal SHA-256
`3966d129deb310acf103440581fdd13bb24b1f28dc3d1135832de0ae770321f3`.
Any normative proposal edit changes this hash and voids every review result.
All required owners must approve one exact snapshot before the proposal
may move to `ACCEPTED`.

### 2026-07-19 Exact-Snapshot Return

The `3966d129deb310acf103440581fdd13bb24b1f28dc3d1135832de0ae770321f3`
snapshot was returned to `DRAFT`. No approval from that snapshot carries
forward. The blocking findings were:

- the proposal introduced multi-source module aggregation even though RFC 0008
  and the production discovery contract admit one selected source per active
  module, without governing cross-source discovery, conflicts, provenance, or
  module-initializer ordering;
- anonymous callable defaults were described as resolving before every
  ordinary parameter activates, contradicting the Binder's source-ordered
  default visibility in which preceding parameters are active;
- the definition-owner tree did not stop at strict descendant stable
  definitions or implementations, permitting nested bodies to be traversed by
  both an outer owner and their own stable owner; and
- diagnostic occurrence and provenance records were replaced without adding
  `products/zomlang/compiler/diagnostics/**` to repository impact or requiring
  `error-system` owner review.

The repaired draft preserves RFC 0008 source cardinality, specifies the closed
definition-body roots and stable item boundary census, defines sequential
anonymous-parameter default activation, and adds `error-system` ownership. A
new exact proposal hash and complete new review are required before acceptance.

### 2026-07-19 Repaired-Draft Pre-Review Return

The repaired draft snapshot
`528628297fab262a908457128914243df7bdb128f26063d10705c54f4d38b819`
was reviewed before re-entering `REVIEW`. `binder-checker`, `spec-audit`, and
`error-system` found their prior blockers closed. `module-system`, `rfc`, and
`verification` returned the draft because the owner-body catalog did not name
an explicit tracked `ModuleKey -> SourceFileKey` authority, confused reuse
class with durability, omitted complete query descriptor fields and
dependency-leak mutations, and referred to a clean-versus-incremental corpus
without defining its native path or exact assertion protocol.

The next repair adds `SelectedModuleSource`, the complete descriptor and
failure inventory for every owner-body query, dynamic source-edge replacement
rules, exact dependency mutations, and the native ztest differential corpus
contract. This pre-review did not change RFC status and records no approval
that carries to a later exact snapshot.

### 2026-07-19 Repaired Formal Review Entry

All six required owners found no blocking issue in DRAFT pre-review. RFC 0019
therefore re-entered `REVIEW` at exact proposal SHA-256
`5c206bb56782bc4d12e9be493324c1d994350e5e058101982166274cacbb2531`.
The prior DRAFT conclusions establish review readiness only. Formal approval
requires every owner below to verify this exact REVIEW snapshot; any normative
edit changes the hash and voids the complete approval set.

The first repaired REVIEW snapshot was returned because its Open Questions
text still described the proposal as `DRAFT`. No owner approval from that
snapshot carries forward. The state wording was corrected without changing
the technical contract. Formal review restarted at exact proposal SHA-256
`ba4d5fdf7e5a68c8895628299292e67d31df5b59398387bbe3be20a7c8e899b0`.

## Owner Review Matrix

| Owner | Required review | Status | Evidence |
|---|---|---|---|
| `rfc` | metadata, structure, status, prior art, replacement boundary | Approved | Exact REVIEW snapshot `ba4d5fdf...c8e899b0`. |
| `binder-checker` | body owner selection, local and anonymous facts, parameter activation and capture | Approved | Exact REVIEW snapshot `ba4d5fdf...c8e899b0`. |
| `module-system` | canonical owner wire, query catalog, provenance, aggregate demand | Approved | Exact REVIEW snapshot `ba4d5fdf...c8e899b0`. |
| `error-system` | diagnostic occurrence ownership, provenance, materialization, failure isolation | Approved | Exact REVIEW snapshot `ba4d5fdf...c8e899b0`. |
| `spec-audit` | grammar and current architecture consistency | Approved | Exact REVIEW snapshot `ba4d5fdf...c8e899b0`. |
| `verification` | independent oracle, mutation inventory, red-green and selected-source gates | Approved | Exact REVIEW snapshot `ba4d5fdf...c8e899b0`. |

No owner approval is recorded by this tracker. A return from any required
owner returns the proposal to `DRAFT`; repaired text requires a new exact
snapshot and a complete new review.

## Decision Record

Accepted on 2026-07-19. All six required owners approved proposal snapshot
`ba4d5fdf7e5a68c8895628299292e67d31df5b59398387bbe3be20a7c8e899b0`
without a blocking objection. The accepted design is the direct replacement
defined by RFC 0019: one `StableBodyOwnerKey`, explicit selected-source query
authority, stable item boundaries, owner-local anonymous parameters, and one
closed owner-body query family. Implementation may begin; no approval covers a
dual definition-only path or a change to the accepted normative contract.

## Implementation Tracker

Implementation started after the accepted RFC moved through `ACCEPTED` to
`IMPLEMENTING`. A phase is complete only after its native tests and adversarial
gate evidence pass; the accepted review snapshot does not count as
implementation evidence.

| Phase | Scope | Status | Evidence |
|---|---|---|---|
| 1 | Stable body owner and owner-local codec replacement | Complete | `StableBodyOwnerKey`, `OwnerLocalBindingKey`, and `AnonymousOwnerLocalKey` are the direct call-site authorities; strict bounded standalone decoders, fixed vectors for both owner alternatives and both owner-local domains, and adversarial codec tests pass. |
| 2 | Module body syntax and provenance | Complete | Query-safe `ParseSource`, stable named definition and implementation inventories, revision-local current-site projections, `ModuleBodySyntax`, and `ModuleBodyProvenance` are registered with exact dependencies and independent verifiers. `CompilerSession` stages the selected source authority, demands every value, and retains the verified module-body pair for owner-body queries. Native codec, dependency, range-only backdating, production-session, architecture, and adversarial gate tests pass. |
| 3 | Owner-body query catalog and aggregate verification | In progress | RFC 0020 supplies the tracked complete-record authority projection. `ModuleBodyOwners`, `OwnerBodySyntax`, and `OwnerBodyProvenance` now have strict bounded codecs, registered descriptors, exact alternative dependencies, independent executable-root and provenance reconstruction, canonical parallel owner census, and worker-count regressions. `BindModuleSkeleton`, `BindOwnerBody`, `MaterializeOwnerBody`, aggregate verification, production demand, and removal of the batch production path remain open. |
| 4 | Anonymous subordinate parameter, scope, closure, and diagnostic migration | In progress | Owner-local anonymous callable parameters and module-owned local and pattern captures are implemented. Production validators independently reconstruct explicit and inferred capture domains, module and callable boundaries, nested propagation, own-parameter exclusion, contextual `Self` owners, and receiver reachability across function and closure boundaries. Stable body scopes, diagnostic provenance, and query materialization remain open. |
| 5 | Independent schema mutations and native regressions | In progress | Identity and incremental-query gates reject malformed owner records, noncanonical module and source keys, removed selected-source and snapshot roots, source-closure violations, sequential replacement of the canonical named-item group, verifier reuse of producer root or provenance algorithms, wrong retention, missing registration, forbidden parser reads, reduced worker matrices, and missing owner codec adversaries. Native tests cover owner census, exact alternative dependencies, bodyless definitions, worker counts 1, 2, and 8, strict value codecs, duplicate, missing, and foreign module owners, stable-boundary pruning, and range shielding. Full owner-body binding and materialization mutations remain open. |
| 6 | Full sanitizer, differential, architecture, format, and benchmark gates | Pending | Requires completed implementation slices. |
| 7 | Current architecture documentation and landing evidence | Pending | Requires completed implementation and verification. |

### 2026-07-19 Stable Owner Codec And Capture Verification Evidence

The stable owner record layer is now closed. `StableBodyOwnerKey` admits exactly
module and definition alternatives. `OwnerLocalBindingKey` and
`AnonymousOwnerLocalKey` use that owner directly and have strict bounded
standalone decoders with exact domain, version, tag, enum, namespace-kind-role,
NFC, truncation, trailing-data, and resource-limit rejection. Their identity
dependencies now expose compositional canonical decoders through package,
crate, all source-origin alternatives, source file, and module. Fixed vectors
cover both stable owner alternatives and both owner-local version-one domains.

Module-owned closure capture is also a production-supported semantic path.
Binder capture authorization reaches the declaring module scope before the
module boundary rejects traversal, and free-variable construction publishes a
module capture boundary. A separate production capture validator consumes only
the verified AST, candidate scope graph, resolutions, and facts. It reconstructs
closure ownership, explicit versus inferred domains, exact explicit members,
module and callable boundaries, nested inferred propagation, reference sites,
and anonymous own-parameter exclusion. It runs after structural validation and
before source rejection and publication, and the architecture gate forbids any
producer include or symbol reuse.

A separate production context validator independently reconstructs contextual
`Self` owners, bare-receiver exclusions, receiver selection, named-function
isolation, inferred and explicit closure reachability, exact source spans, and
closed failure facts. The production control validator independently
reconstructs module, function, and closure label owners, owner-local label
indices, canonical label and control order, active-label shadowing, block and
loop targets, implicit and explicit `break` and `continue`, exact failure
diagnostics, and duplicate-label notes. Neither validator includes or calls the
corresponding producer algorithms.

Evidence:

- sanitizer identity and local-identity codec tests pass for scalar, package,
  crate, source, module, stable owner, owner-local binding, and anonymous owner;
- sanitizer `binding-input-test` passes all 190 native cases, including
  module-owned local and pattern capture plus production capture, context,
  label, and control mutations;
- Binder architecture, Binder fact-schema, identity architecture, and
  incremental-query architecture checks and adversarial self-tests pass;
- the capture validator is linked into the production Binder target and its
  stage is mutation-guarded before publication; and
- format and diff checks pass for the implemented slice.

This evidence completes Phase 1 and the implemented capture, context, and
control portions of Phase 4. The Phase 2 value foundation is recorded below;
query registration, stable body-scope and diagnostic materialization, complete
query mutations, differential editing, benchmarks, and landing evidence remain
open.

### 2026-07-20 Owner-Body Inverse-Key Blocker

Phase 3 stopped before named-item or owner-body query implementation. The
accepted key `NamedItemSyntax(DefinitionKey)` does not carry the module needed
to demand `NamedDefinitionInventory(ModuleKey)`, and `DefinitionKey` is a
one-way SHA-256 digest. `QueryContext` has no tracked inverse authority and may
not read `CompilerSession` or semantic identity registries.

RFC 0020 governs the required
`ActiveDefinitionAuthorityInput(DefinitionKey) -> DefinitionIdentityRecord`
projection, complete-map replacement protocol, collision checks, and exact
owning-inventory membership dependency. Phase 3 was blocked until that RFC was
accepted and its authority staging foundation was implemented. No module
scan, registry fallback, query-key replacement, or dual Binder path is
authorized.

### 2026-07-19 Module Body Value And Projection Evidence

The module-body representation now separates reusable semantic syntax from
revision-local source identity. `ModuleBodySyntax` owns a flat canonical
preorder forest with explicit definition and implementation boundary leaves.
It stores neither AST node ids nor source ranges. `ModuleBodyProvenance` owns
the selected source and the exact `LocalSyntaxPath` to AST node and byte-range
mapping for every admitted non-boundary syntax node. Both values have bounded,
schema-fingerprinted standalone codecs that reject invalid enum values,
noncanonical identifiers, malformed preorder forests, invalid paths,
truncation, trailing bytes, and foreign implementation occurrence records.

The production projector accepts implicit, root-declared, and inline-root
module forms, prunes every stable definition or implementation before
descending into its body, and rejects incomplete or mismatched boundary
inventories. The production verifier does not call the projector or
`DefinitionInventory`; it independently selects module items, classifies and
censuses stable boundaries, encodes AST fields, traverses child order, and
reconstructs both complete values before comparison.

Evidence:

- sanitizer `module-body-syntax-test` passes the native source-form,
  stable-boundary, range-shielding, codec, and incomplete-inventory cases;
- the Binder target links the value, projector, and independent verifier;
- Binder architecture checking and its adversarial self-test reject producer
  reuse, `DefinitionInventory` reuse, independent-census removal, CMake source
  omission, and loss of the range-shielding regression; and
- the existing `SelectedModuleSourceInput` and `SourceSnapshotInput` roots
  remain the single staged source authority.

This closes the value, codec, projection, and independent-verification portion
of Phase 2. It does not complete Phase 2 because the current parsed AST is a
borrowed batch value. A query-safe `ParseSource` result and stable named
definition and implementation inventory queries must replace that authority
before `ModuleBodySyntax` or `ModuleBodyProvenance` can be registered and
demanded without creating a second Binder path.

### 2026-07-20 Module Body Query Closure Evidence

Phase 2 now has one production query path. `ParseSource(SourceFileKey)` owns a
query-safe canonical parsed value and a closed parse-rejection value. The
session materializes `VerifiedParsedModule` exclusively from that query result.
Stable identity reconstruction feeds
semantic `NamedDefinitionInventory(ModuleKey)` and
`NamedImplementationInventory(ModuleKey)` values, while exact current AST
nodes and ranges live only in revision-local definition and implementation site
projections.

`ModuleBodySyntax(ModuleKey)` depends on the selected source, exact parse
result, both stable named inventories, and both current-site projections.
`ModuleBodyProvenance(ModuleKey)` depends on the selected source, exact parse
result, semantic module-body syntax, and both current-site projections. Their
query verifiers independently reconstruct the expected values without calling
the production projector. `CompilerSession` demands all six identity and body
values before stable identity publication and retains the verified syntax and
provenance pair as the sole Phase 3 owner-body input.

Evidence:

- sanitizer `incremental-binding-query-adapter-test` covers strict codecs,
  exact direct dependency fingerprints, stable inventory publication, and
  semantic-versus-revision-local range-only edits;
- sanitizer `module-discovery-test` exercises the production session path;
- incremental-query architecture checks require ParseSource, stable inventory,
  site, module-body registration, independent reconstruction, and production
  demand, with adversarial self-tests for their removal;
- Binder architecture checks enforce independent module-body reconstruction;
  and
- CompilerSession architecture checks require query result verification and
  retained module-body input ownership while forbidding direct parser
  authority.

This evidence completes Phase 2. Phase 3 remains open and begins with the
owner-body query catalog and its stable item syntax inputs.

### 2026-07-20 Active Definition Authority And Named-Item Evidence

RFC 0020 resolves the inverse-key blocker without a module scan, registry
fallback, query-key change, or second named-item path. The session-maintained
`ActiveDefinitionAuthorityInput(DefinitionKey)` stores the complete active
identity record and is protected by an atomically replaced readiness
fingerprint. Equal records preserve input `changedAt`; removed, renamed, and
moved definitions erase their prior keys before a ready snapshot is exposed.

Semantic `NamedItemSyntax` and revision-local `NamedItemProvenance` recover the
module only from that tracked record, prove exact membership in
`NamedDefinitionInventory(ModuleKey)`, select the canonical authority
occurrence, and reconstruct detached syntax and source provenance through
independent Binder implementations. `CompilerSession` demands both values for
every active definition after authority refresh and before binding.

Sanitizer native tests cover strict codecs, failed replacement, stale-ledger
retry, old snapshots, conditional readiness, range-only and body-only edits,
add, delete, rename, cross-module edit, active-set shrink, and definition move.
The focused differential sequence runs with worker counts 1, 2, and 8 and
compares reused and clean canonical values and dependency groups. Incremental
query, Binder, and CompilerSession architecture checks and adversarial
self-tests pass.

Phase 3 is therefore unblocked and in progress.

### 2026-07-20 Owner Projection Catalog Evidence

The first Phase 3 catalog slice is implemented without adding a second
semantic Binder publication path:

- `ModuleBodyOwners(ModuleKey)` reads one named-definition inventory and one
  canonical parallel group containing every `NamedItemSyntax` key, then
  publishes exactly one module owner plus the closed executable definition
  subset in complete canonical byte order;
- `OwnerBodySyntax(ModuleOwner)` reads only `ModuleBodySyntax`, while
  `OwnerBodySyntax(DefinitionOwner)` reads only `NamedItemSyntax` and rejects
  definitions outside the closed executable-root set;
- `OwnerBodyProvenance` reads `OwnerBodySyntax` and exactly the matching module
  or named-item provenance alternative, then independently proves total legal
  path coverage without entering a stable-item boundary; and
- semantic values retain complete owner and owning-module identity, while
  current source, node, range, and path mappings remain revision-local.

Provider and verifier code use separate executable-root field decoding and
separate provenance traversals. Native tests prove strict codec round trips,
trailing-data rejection, duplicate, missing, and foreign module-owner
rejection, exact dependency groups, bodyless-definition exclusion, and byte
determinism with worker counts 1, 2, and 8. The incremental-query architecture
gate has adversarial mutations for each dependency, verifier-separation,
retention, registration, parser-purity, worker-matrix, and codec-test marker.

The next implementation slice is `BindModuleSkeleton`, followed by
`BindOwnerBody`, revision-local materialization, aggregate verification,
production session demand, and direct removal of the batch production path.

## Required Review Commands

```bash
shasum -a 256 docs/rfc/0019-stable-body-owner-and-query-closure.md
python3 scripts/check-rfc.py
python3 scripts/check-identity-architecture.py --check
python3 scripts/check-incremental-query-architecture.py --check
git diff --check -- docs/rfc/0019-stable-body-owner-and-query-closure.md \
  docs/rfc/tracking/0019-review-and-implementation.md docs/rfc/README.md
```

The architecture gates inspect the current repository and may report
implementation work that remains intentionally blocked by this REVIEW RFC.
Such findings are implementation evidence, not approval to bypass governance.
