---
rfc: 27
title: Binder Query And Identity Materialization Closure
type: compiler
status: IMPLEMENTING
author: ZOM Compiler Team
review-manager: rfc
required-owners: [task-router, rfc, module-system, binder-checker, runtime-memory, error-system, ir-backend, spec-audit, verification]
approvers: [task-router, rfc, module-system, binder-checker, runtime-memory, error-system, ir-backend, spec-audit, verification]
created: 2026-07-27
updated: 2026-08-07
area: compiler
requires: [4, 8, 10, 11, 17, 18, 19, 20, 25, 26]
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0027-review-and-implementation.md#discussion-record
decision: docs/rfc/tracking/0027-review-and-implementation.md#decision-record
implementation: docs/rfc/tracking/0027-review-and-implementation.md#implementation-tracker
tracking-issue: docs/rfc/tracking/0027-review-and-implementation.md#implementation-tracker
---

# RFC 0027: Binder Query And Identity Materialization Closure

## Summary

This RFC closes the stable-header, Binder-query, identity-admission, module
allocation, diagnostic, and capability-lifetime contracts required by RFC
0025.

The proposed design has four layers:

1. staging-safe header queries publish complete handle-free declaration and
   implementation-occurrence headers;
2. semantic Binder queries publish canonical module and owner-body facts;
3. the semantic-context capability arena owns the only append-only typed
   identity interner set; and
4. final-snapshot capability queries materialize the module graph, Binder
   skeletons, owner bodies, and verified bound modules under retained leases.

The implementation is one unversioned internal replacement. It removes
one-time physical registry freeze, session-owned Binder mirrors, non-owning
bound-module publication, and production batch binding. It introduces no
adapter, compatibility decoder, fallback authority, or dual production root.

## Motivation

RFCs 0017, 0019, and 0025 name `BindModuleSkeleton`,
`MaterializeModuleSkeleton`, `BindOwnerBody`, `MaterializeOwnerBody`, and
`VerifyBoundModule`, but do not provide enough information to implement them
without inventing contracts:

- `ModuleBodySyntax` intentionally prunes named definitions and implementation
  occurrences to boundaries, so it cannot reconstruct their header scopes or
  parameter declarations;
- implementation-owned generics and implementation-occurrence scopes require
  different stable owners;
- digest keys cannot prove their complete retained identity records or owning
  modules;
- body-local materializers require one deterministic module-wide handle plan;
- stable Binder failures and source diagnostics require different closed
  result alternatives; and
- a capability lease may outlive `CompilerSession` and `QueryDatabase`, so its
  handle reverse-lookup authority cannot be owned by either object.

The production graph bridge also requires correction. Authority staging needs
only stable graph and inventory values. Handleful graph materialization belongs
after the final input seal, where the same active-membership and lease rules as
all other capabilities apply.

## Goals

- Define complete staging-safe definition and implementation header queries.
- Define stable module-skeleton, owner-body, routing, failure, and diagnostic
  schemas with exact codecs.
- Preserve RFC 0018 definition-or-implementation generic ownership.
- Preserve implementation occurrence identity separately from shared
  implementation authority.
- Define one deterministic module allocation plan for every module-local
  handle.
- Define typed authority records and tracked active membership for all eight
  global handle domains.
- Keep the interner set alive through the semantic-context capability arena.
- Define exact capability fields, verifier algorithms, and downstream lease
  ownership.
- Move handleful module-graph publication behind the final sealed-snapshot
  barrier.
- Delete production batch binding and every session mirror in one root cutover.

## Non-Goals

- This RFC does not change ZOM syntax or user-visible name-resolution rules.
- This RFC does not define core roles, core signatures, marker policy, final
  core interfaces, or prelude contents.
- This RFC does not add query persistence.
- This RFC does not permit cyclic source-module binding.
- This RFC does not require parallel capability demand.
- This RFC does not assign semantic identity to owner-local or anonymous
  entities.

## Prior Art

Rust compiler queries separate stable query values from session-local interned
identities and retain tracked dependency edges. ZOM adopts that stable-value
boundary from the
[rustc query system](https://rustc-dev-guide.rust-lang.org/query.html).

Salsa places interned data in database-associated durable storage and
coalesces equal concurrent requests. ZOM adopts typed append-only interning
from [Salsa interned structs](https://salsa-rs.github.io/salsa/plumbing/interned.html)
while using a refcounted semantic-context arena so surviving capability leases
retain their handle authority after database teardown.

Swift request evaluation separates declaration work from body work and avoids
embedding request-evaluator state in reusable values. ZOM adopts that staged
decomposition from
[Swift request evaluation](https://github.com/swiftlang/swift/blob/main/docs/RequestEvaluator.md).

LLVM uses owning arenas and explicit lifetime anchors for immutable compiler
objects. ZOM applies that ownership rule to capability memos, typed interners,
scope storage, and downstream leases.

## Guide-Level Explanation

```mermaid
flowchart TD
    A["Stable graph and identity inventories"] --> H["Definition and implementation header queries"]
    H --> S["BindModuleSkeleton"]
    S --> B["BindOwnerBody for every canonical owner"]
    S --> P["ModuleBindingAllocationPlan"]
    B --> P
    F["Final sealed snapshot"] --> G["MaterializeModuleGraph"]
    F --> M["MaterializeModuleSkeleton"]
    F --> O["MaterializeOwnerBody"]
    P --> M
    P --> O
    G --> V["VerifyBoundModule"]
    M --> V
    O --> V
    V --> D["Lease-owning downstream consumers"]
```

Stable queries contain canonical keys and records only. A stable query may
read a revision-local syntax capability and still publish a backdatable
semantic value when its complete output bytes are equal.

Materialization starts only in the final current sealed snapshot. Every global
handle request first proves active membership through a tracked query read.
The semantic-context arena then returns the existing handle for the exact
key-and-record pair or appends one immutable entry. A previously interned
handle never makes an inactive key active.

## Reference-Level Design

### Stable Query Routing Keys

The following closed records make every owning module explicit:

```text
StableDefinitionQueryKey {
  module: ModuleKey,
  definition: DefinitionKey,
}

StableImplementationQueryKey {
  module: ModuleKey,
  implementation: ImplKey,
}

StableImplementationOccurrenceQueryKey {
  module: ModuleKey,
  occurrence: ImplSourceOccurrenceKey,
}

StableGenericParameterQueryKey {
  module: ModuleKey,
  parameter: GenericParameterKey,
}

StableCallableParameterQueryKey {
  module: ModuleKey,
  parameter: CallableParameterKey,
}

StableSemanticImportQueryKey {
  requester: ModuleKey,
  binding: ImportBindingKey,
}

StableOwnerBodyQueryKey {
  module: ModuleKey,
  owner: StableBodyOwnerKey,
}

ContextualBodyOwnerKey {
  contextRoots: CompilationRootSetQueryKey,
  body: StableOwnerBodyQueryKey,
}

ContextualCompilationUnitKey {
  contextRoots: CompilationRootSetQueryKey,
  unit: CompilationUnitIdentity,
}

ContextualCrateKey {
  contextRoots: CompilationRootSetQueryKey,
  crate: CrateKey,
}

ContextualSourceKey {
  contextRoots: CompilationRootSetQueryKey,
  source: SourceFileKey,
}

ContextualModuleKey {
  contextRoots: CompilationRootSetQueryKey,
  module: ModuleKey,
}

ContextualDefinitionKey {
  contextRoots: CompilationRootSetQueryKey,
  definition: StableDefinitionQueryKey,
}

ContextualImplementationKey {
  contextRoots: CompilationRootSetQueryKey,
  implementation: StableImplementationQueryKey,
}

ContextualGenericParameterKey {
  contextRoots: CompilationRootSetQueryKey,
  parameter: StableGenericParameterQueryKey,
}

ContextualCallableParameterKey {
  contextRoots: CompilationRootSetQueryKey,
  parameter: StableCallableParameterQueryKey,
}
```

Each record uses its field order above, complete nested canonical encodings,
and exact consumption. `ContextualBodyOwnerKey.body.module` must be active
under the exact `contextRoots`, and its `body.owner` must select that module. A decoder
recomputes every digest key from its retained authority record before
publication. A referenced definition, implementation, parameter, occurrence,
import, or body owner must belong to the routed or owner-derived module. No
provider searches all active modules to recover ownership.

### Canonical Wire And Schema Inventory

Every top-level key, value, fact, witness, and result record starts with its
ASCII domain below, one `0x00` byte, and fields in declaration order. A nested
record is framed as `uint64be(byteCount) || completeRecordBytes`. RFC 0011
encoding applies to scalars: fixed-width unsigned big-endian integers,
one-byte booleans and closed tags, 32 raw digest bytes, `uint64be` byte-string
and sequence counts, NFC UTF-8 text, and one-byte optional presence. Maps and
sets are sequences sorted by complete encoded key or element bytes. Duplicate,
unsorted, non-NFC, unknown-tag, malformed-optional, truncated, or trailing
input is rejected.

Every schema sequence count and text byte count must be at most `UINT32_MAX`,
must fit the remaining input before allocation, and must fit `size_t`.
Allocation-plan counts and prefix sums additionally fit the destination
`uint32` handle domain. A decoder checks bounds before allocation and requires
exact consumption. Encoders never write a process handle, slot, pointer,
source span, `NodeId`, brand, revision, arena identity, or worker order into a
stable record.

| Bound | Exact limit |
|---|---:|
| identifier or display-argument bytes | 4 KiB |
| local syntax path components | 4,096 |
| non-contextual routed key bytes | 64 KiB |
| complete context-root bytes | 64 MiB |
| contextual key bytes | 128 MiB |
| records in one Binder semantic sequence | 1,048,576 |
| ambiguity candidates | 1,048,576 |
| complete Binder semantic value bytes | 128 MiB |
| diagnostic facts in one result | 1,048,576 |
| complete diagnostic payload bytes | 64 MiB |
| active modules per graph | 4,096 |
| dependency sites or graph edges | 1,048,576 |
| dense local allocation count | `UINT32_MAX`, with checked prefix sums |

| Record | ASCII domain |
|---|---|
| `StableDefinitionQueryKey` | `zom.binder.definition-query-key` |
| `StableImplementationQueryKey` | `zom.binder.implementation-query-key` |
| `StableImplementationOccurrenceQueryKey` | `zom.binder.implementation-occurrence-query-key` |
| `StableGenericParameterQueryKey` | `zom.binder.generic-parameter-query-key` |
| `StableCallableParameterQueryKey` | `zom.binder.callable-parameter-query-key` |
| `StableSemanticImportQueryKey` | `zom.binder.semantic-import-query-key` |
| `StableOwnerBodyQueryKey` | `zom.binder.owner-body-query-key` |
| `ContextualBodyOwnerKey` | `zom.binder.contextual-body-owner-key` |
| `ContextualCompilationUnitKey` | `zom.binder.contextual-compilation-unit-key` |
| `ContextualCrateKey` | `zom.binder.contextual-crate-key` |
| `ContextualSourceKey` | `zom.binder.contextual-source-key` |
| `ContextualModuleKey` | `zom.binder.contextual-module-key` |
| `ContextualDefinitionKey` | `zom.binder.contextual-definition-key` |
| `ContextualImplementationKey` | `zom.binder.contextual-implementation-key` |
| `ContextualGenericParameterKey` | `zom.binder.contextual-generic-parameter-key` |
| `ContextualCallableParameterKey` | `zom.binder.contextual-callable-parameter-key` |
| `StableHeaderGenericParameter` | `zom.binder.header-generic-parameter` |
| `StableHeaderCallableParameter` | `zom.binder.header-callable-parameter` |
| `StableDefinitionHeader` | `zom.binder.definition-header` |
| `StableImplementationOccurrenceHeader` | `zom.binder.implementation-occurrence-header` |
| `StableScopeOwnerKey` | `zom.binder.scope-owner-key` |
| `StableNodeSyntaxRoot` | `zom.binder.node-syntax-root` |
| `StableBindingTargetKey` | `zom.binder.binding-target-key` |
| `BoundModuleSkeleton` | `zom.binder.module-skeleton` |
| `StableScopeFact` | `zom.binder.skeleton-scope` |
| `StableNodeScopeFact` | `zom.binder.skeleton-node-scope` |
| `StableDeclarationFact` | `zom.binder.skeleton-declaration` |
| `StableImplementationOccurrenceFact` | `zom.binder.skeleton-implementation-occurrence` |
| `StableGenericParameterDeclarationFact` | `zom.binder.skeleton-generic-parameter-declaration` |
| `StableCallableParameterDeclarationFact` | `zom.binder.skeleton-callable-parameter-declaration` |
| `StableSemanticImportQueryKey`, `StableImportFact` | `zom.binder.semantic-import-query-key`, `zom.binder.skeleton-import` |
| `StableModuleAliasFact` | `zom.binder.skeleton-module-alias` |
| `StableReexportStep` | `zom.binder.skeleton-reexport-step` |
| `StableLocalExportFact` | `zom.binder.skeleton-local-export` |
| `StableFailedLookupOutcome`, `StableFailedLookupFact` | `zom.binder.failed-lookup-outcome`, `zom.binder.failed-lookup` |
| `StableExportedBinding` | `zom.binder.exported-binding` |
| `StableExportedBindingQueryKey` | `zom.query.exported-binding-key` |
| `StableScopeNameBucketQueryKey` | `zom.query.scope-name-bucket-key` |
| `BoundOwnerBody` | `zom.binder.owner-body` |
| `StableBodyScopeFact` | `zom.binder.body-scope` |
| `StableBodyNodeScopeFact` | `zom.binder.body-node-scope` |
| `StableOwnerLocalBindingFact` | `zom.binder.body-owner-local-binding` |
| `StableResolutionFact` | `zom.binder.body-resolution` |
| `StableDeferredMemberFact` | `zom.binder.body-deferred-member` |
| `StableSelfOwner`, `StableSelfTypeFact` | `zom.binder.self-owner`, `zom.binder.body-self-type` |
| `StableThisBindingFact` | `zom.binder.body-this-binding` |
| `StableShadowTargetFact` | `zom.binder.body-shadow-target` |
| `StableLabelKey`, `StableLabelTarget`, `StableLabelFact` | `zom.binder.label-key`, `zom.binder.label-target`, `zom.binder.body-label` |
| `StableControlTarget`, `StableControlTransferFact` | `zom.binder.control-target`, `zom.binder.body-control-transfer` |
| `StableClosureFact` | `zom.binder.body-closure` |
| `StableClosureFreeVariable`, `StableClosureFreeVariableFact` | `zom.binder.closure-free-variable`, `zom.binder.body-closure-free-variables` |
| `StableExplicitCaptureMode`, `StableExplicitCaptureBindingFact`, `StableExplicitClosureCaptureFact` | `zom.binder.explicit-capture-mode`, `zom.binder.explicit-capture-binding`, `zom.binder.body-explicit-closure-capture` |
| `ModuleBindingAllocationPlan` | `zom.binder.module-allocation-plan` |
| `OwnerAllocationRange` | `zom.binder.owner-allocation-range` |
| `BinderQueryResult<StableDefinitionHeader>` | `zom.binder.result-definition-header-syntax` |
| `BinderQueryResult<StableImplementationOccurrenceHeader>` | `zom.binder.result-implementation-occurrence-header-syntax` |
| `BinderQueryResult<BoundModuleSkeleton>` | `zom.binder.result-bind-module-skeleton` |
| `BinderQueryResult<BoundOwnerBody>` | `zom.binder.result-bind-owner-body` |
| `BinderQueryResult<ModuleBindingAllocationPlan>` | `zom.binder.result-module-binding-allocation-plan` |
| `BinderKeyFailure` | `zom.binder.key-failure` |
| `ActiveCompilationUnitMembership` | `zom.binder.active-compilation-unit-membership` |
| `ActiveImplementationMembershipRecord` | `zom.binder.active-implementation-membership` |
| `ImplementationGenericAuthority` | `zom.binder.implementation-generic-authority` |
| `ActiveGenericParameterMembership` | `zom.binder.active-generic-parameter-membership` |
| `ActiveCallableParameterMembershipRecord` | `zom.binder.active-callable-parameter-membership` |
| `CompleteRootIdentityReadiness` | `zom.binder.complete-root-identity-readiness` |
| `BinderIdentifierDiagnosticArguments` | `zom.diagnostic.arguments.binder-identifier` |
| `BinderNamespaceDiagnosticArguments` | `zom.diagnostic.arguments.binder-namespace` |
| `CanonicalCompilationRootRecord` | `zom.input.canonical-compilation-root` |
| `CanonicalTargetSelectionRecord` | `zom.input.canonical-target-selection` |
| `CanonicalLanguageOptionsRecord` | `zom.input.canonical-language-options` |
| `CanonicalPackageCompilationRequest` | `zom.input.canonical-package-compilation-request` |
| `CompleteCompilationContextAuthority` | `zom.input.complete-compilation-context-authority` |
| `VerifiedCoreDistributionInputPayload` | `zom.query.input-transaction.core-distribution` |
| `VerifiedModuleGraphInputPayload` | `zom.query.input-transaction.module-structure` |
| `ContextualIdentityAuthorityInputPayload` | `zom.query.input-transaction.contextual-identity-authority` |
| `StableMaterializedDependencyWitness` | `zom.materialized-module-dependency-witness` |
| `MaterializedModuleGraphWitness` | `zom.materialized-module-graph-witness` |

Stable projection descriptors use these exact key encodings and value domains:

| Query | Exact key encoding | Exact value domain |
|---|---|---|
| `ModuleExportNames` | complete `ModuleKey` | `zom.query.module-export-names-value` |
| `ExportedBinding` | complete `StableExportedBindingQueryKey` | `zom.query.exported-binding-value` |
| `DefinitionBindingHeader` | complete `StableDefinitionQueryKey` | `zom.query.definition-binding-header-value` |
| `ImplementationBindingHeader` | complete `StableImplementationQueryKey` | `zom.query.implementation-binding-header-value` |
| `ScopeNameBucket` | complete `StableScopeNameBucketQueryKey` | `zom.query.scope-name-bucket-value` |
| `ImportTarget` | complete `StableSemanticImportQueryKey` | `zom.query.import-target-value` |
| `BindingVisibility` | complete `StableBindingTargetKey`; body-local alternatives are invalid for this descriptor | `zom.query.binding-visibility-value` |

No descriptor wraps an already canonical key in another query-key record.
Except for the five `BinderQueryResult` domains listed above and this explicit
projection table, every other stable query value domain is its literal query
domain with `-value` appended. No runtime domain construction is permitted.
Revision-local capability queries have no value codec.

`products/zomlang/compiler/binder/stable-binding-schema.def` is the
hand-authored canonical X-macro inventory. It contains each literal domain,
tag, field inventory, maximum-count rule, producer and verifier provenance,
artifact-owner task, and executable mutation test. Preprocessor consumers may
form C++ tables or native test cases from the inventory, but they do not
generate the production declarations, codecs, descriptors, providers, or
verifiers. No second schema source exists. `check-binder-architecture.py`
rejects a schema record, query, or projection without exactly one canonical
row and rejects literal domains outside that inventory.

RFC 0031 defines the complete metamodel consumed by RFC 0030 `R30-11`.
Canonical sums have one explicit `Sum` owner; a `Record` and `Sum` row for the
same type form one permitted composite only when their type, codec, and test
tasks are equal. Runtime-only sums use `RuntimeSum`, have no codec task, and
derive conditional variants from each descriptor row's closed failure list.
`Record.producer` and `Record.verifier` are semantic provenance, not artifact
ownership columns.

### Stable Header Inputs

`DefinitionBodyDisposition` is the closed sum:

```text
NoExecutableBody = 0x01
ExecutableBody   = 0x02
```

`NamedDefinitionInventoryEntry` retains the complete
`DefinitionIdentityRecord` and this disposition. Provider and verifier derive
the disposition from the exact selected declaration syntax and require equal
bytes.

The inventory entry is the following typed record:

```text
NamedDefinitionInventoryEntry {
  key: DefinitionKey,
  record: DefinitionIdentityRecord,
  bodyDisposition: DefinitionBodyDisposition,
}
```

Its canonical inventory encoding contains the key, complete record encoding,
and disposition in that order. The entry exposes `key`, `record`, and
`bodyDisposition`. The inventory constructor accepts complete definition
authorities paired with their derived dispositions, verifies every authority,
key, record, module, duplicate, and disposition, and stores the typed records.
The decoder admits exactly this encoding and reconstructs the same typed
records.

The implementation inventory entry is the following typed record:

```text
NamedImplementationInventoryEntry {
  key: ImplKey,
  record: ImplIdentityRecord,
}
```

Its canonical inventory encoding contains the key followed by the complete
record encoding. The constructor verifies every authority, key, record,
module, and duplicate before storing the typed entries. The decoder
reconstructs the complete record and proves that its derived key equals the
encoded key. The inventory exposes its complete typed `entries`.

`NamedDefinitionInventoryQuery` reads the selected source and parse capability
in addition to `StableIdentityAdmissionQuery`. Its provider and verifier each
walk the exact admitted definition nodes and independently apply this closed
body classification:

- `FunctionDecl`, `ConstructorDecl`, and `DestructorDecl` require one present
  `BlockStmt` and produce `ExecutableBody`; absence or any other child kind is
  an invariant failure.
- `MethodDecl` produces `NoExecutableBody` when its optional body is absent
  and otherwise requires one `BlockStmt` and produces `ExecutableBody`.
- `FieldDecl` and `ClassConstDecl` produce `NoExecutableBody` when the optional
  initializer is absent and otherwise require one expression, literal
  expression, or unsafe-block expression and produce `ExecutableBody`.
- every other stable definition kind produces `NoExecutableBody`.

The admitted definition site must resolve to exactly one retained AST node
whose syntax kind and complete derived identity record match the admission
entry. A missing node, duplicate site, unexpected syntax kind, record
disagreement, missing required child, child-kind mismatch, or malformed
optional child is `InvariantViolation`. Source and parse rejection is
forwarded through the existing typed semantic-failure encoding; key absence
remains semantic absence. Provider and verifier use the same tracked-read
order: selected source, parse capability, stable identity admission, then
candidate construction.

The two query paths do not call a shared body-classification helper. They
construct typed inventory candidates separately and require equal complete
inventory bytes. Every consumer reads the typed `record` accessor.

The staging-safe header schemas are:

```text
StableHeaderSite =
    DefinitionAuthoritySite(IdentitySyntaxSiteKey) // 0x01
  | ImplementationOccurrenceSite(ImplSourceOccurrenceKey) // 0x02

StableHeaderGenericParameter {
  key: GenericParameterKey,
  record: GenericParameterIdentityRecord,
  site: StableHeaderSite,
  name: DeclaredDefinitionName,
  ordinal: uint32,
}

StableHeaderCallableParameter {
  key: CallableParameterKey,
  record: CallableParameterIdentityRecord,
  site: StableHeaderSite,
  name: Optional<DeclaredDefinitionName>,
  position: CallableParameterPosition,
}

StableDefinitionHeader {
  queryKey: StableDefinitionQueryKey,
  record: DefinitionIdentityRecord,
  authoritySite: IdentitySyntaxSiteKey,
  kind: DefinitionKind,
  namespace: Namespace,
  name: DeclaredDefinitionName,
  activation: DefinitionActivation,
  visibility: Optional<MemberVisibility>,
  bodyDisposition: DefinitionBodyDisposition,
  genericParameters: CanonicalSequence<StableHeaderGenericParameter>,
  callableParameters: CanonicalSequence<StableHeaderCallableParameter>,
  declaredScopeRoles: CanonicalSequence<ScopeRole>,
}

StableImplementationOccurrenceHeader {
  queryKey: StableImplementationOccurrenceQueryKey,
  authority: StableImplementationQueryKey,
  record: ImplIdentityRecord,
  genericParameters: CanonicalSequence<StableHeaderGenericParameter>,
  declaredScopeRoles: CanonicalSequence<ScopeRole>,
  sourceForm: ImplementationSourceForm,
}
```

`ImplementationSourceForm` is the closed sum `Ordinary = 0x01` and
`BodylessMarker = 0x02`. `ScopeRole` is:

```text
Declaration    = 0x01
Generic        = 0x02
Parameters     = 0x03
Members        = 0x04
Implementation = 0x05
```

`DefinitionHeaderSyntax(StableDefinitionQueryKey)` is `Semantic`. It reads
exactly the named-definition inventory for the explicit module, the selected
source, its parse capability, and current definition and implementation site
projections. It proves exact `(DefinitionKey, DefinitionIdentityRecord)`
membership, independently selects the RFC 0018 authority occurrence, and
detaches only the complete header.

`ImplementationOccurrenceHeaderSyntax(StableImplementationOccurrenceQueryKey)`
is `Semantic`. It reads exactly the named-implementation inventory for the
explicit module, the selected source, its parse capability, and current
implementation and definition site projections. It proves exact occurrence,
shared `ImplKey`, complete `ImplIdentityRecord`, and source form, and detaches
only that occurrence's complete header.

These providers do not read contextual definition authority, readiness,
`NamedItemSyntax`, `OwnerBodySyntax`, a handle registry, or session state.
Their independent verifiers repeat authority selection, owner/ordinal
classification, scope-role census, parameter record coverage, and canonical
encoding without calling producer traversal helpers.

Definition and implementation header construction consumes
`CanonicalParsedModule`, using retained `ThisKeyword` token provenance for
receiver classification. Definition activation is `ImportSurface` only for
`ModuleAlias` and `ModuleSkeleton` for every other stable definition kind.
Member visibility is present only for method, field, constructor, destructor,
and class-constant syntax. An omitted member visibility is `Public` under an
interface owner and `Private` otherwise; explicit public, private, and
protected spellings map to their matching closed values.

The staging producers use these exact borrowed input records:

```text
DefinitionHeaderInput {
  parsed: &CanonicalParsedModule,
  queryKey: &StableDefinitionQueryKey,
  entry: &NamedDefinitionInventoryEntry,
  authoritySite: &RevisionLocalDefinitionSite,
  definitionSites: &RevisionLocalDefinitionSites,
  implementationSites: &RevisionLocalImplementationSites,
}

ImplementationHeaderInput {
  parsed: &CanonicalParsedModule,
  queryKey: &StableImplementationOccurrenceQueryKey,
  entry: &NamedImplementationInventoryEntry,
  occurrenceSite: &RevisionLocalImplementationSite,
  definitionSites: &RevisionLocalDefinitionSites,
  implementationSites: &RevisionLocalImplementationSites,
}
```

Each producer proves that the query key, module, source, typed record, selected
entry, selected site, node, range, and full current site projections identify
one exact admitted authority or occurrence before traversing syntax. The
definition producer independently applies RFC 0018 authority ordering to the
complete definition-site projection and requires the borrowed authority site
to be that result. The implementation producer requires the occurrence key
and borrowed occurrence site to name the same complete implementation record
and source occurrence. Callers cannot supply a detached identity record,
detached syntax entry, or site outside these records.

The independent verifier uses this complete context:

```text
StableHeaderVerificationContext {
  parsed: &CanonicalParsedModule,
  definitionInventory: &NamedDefinitionInventory,
  implementationInventory: &NamedImplementationInventory,
  definitionSites: &RevisionLocalDefinitionSites,
  implementationSites: &RevisionLocalImplementationSites,
}
```

`verifyDefinition(context, queryKey, candidate)` selects the typed definition
entry and RFC 0018 authority site from the complete context.
`verifyImplementationOccurrence(context, queryKey, candidate)` selects the
typed implementation entry and exact occurrence site from the complete
context. Neither verifier entrypoint accepts a caller-selected inventory entry
or site. Both repeat owner classification, scope-role census, parameter
coverage, and canonical encoding without calling a producer traversal helper.
The `S4`, `S4A`, and `S5` native suites independently mutate the query key,
module, source, complete record, typed inventory entry, site key, `NodeId`,
source range, and caller-selected candidate origin one field at a time. Every
cross-input mismatch is rejected before header publication.

RFC 0018 canonical identity normalization remains owned by
`CanonicalDefinitionHeaderProducer`, `CanonicalImplHeaderProducer`, and
`CanonicalHeaderVerifier`. RFC 0027 staging headers are owned separately by
`DefinitionHeaderProducer`,
`ImplementationHeaderProducer`, and `StableHeaderVerifier`.
The source files and classes for these responsibilities are distinct.

Definition scope-role census is exact and syntax-driven:

- `Declaration` is present exactly once for every definition header;
- `Generic` is present exactly for `EnumDeclaration`, `FunctionDecl`,
  `ClassDecl`, `StructDecl`, `InterfaceDecl`, `AliasDecl`, `MethodDecl`, and
  `AssociatedTypeDecl` when their optional `GenericParams` field is present;
- `Parameters` is present exactly for `ExternDecl`, `FunctionDecl`,
  `MethodDecl`, `ConstructorDecl`, and `DestructorDecl`; and
- `Members` is present exactly for `EnumDeclaration`, `ClassDecl`,
  `StructDecl`, `InterfaceDecl`, and `ErrorDecl`.

An explicit `GenericParams` node declares `Generic` even when its parameter
sequence is empty, including the grammar-authorized where-clause-only form.
Every callable syntax kind above declares `Parameters` even when its parameter
sequence is empty. Every aggregate syntax kind above declares `Members` even
when its member sequence is absent or empty. `ExternVarDecl`, `UnitVariant`,
`TupleVariant`, `FieldDecl`, `ClassConstDecl`, and module-level constant or
static pattern authorities declare only `Declaration`. Any other
admission-to-syntax pairing is an invariant failure.

Implementation-occurrence headers contain `Implementation` exactly once and
contain `Generic` exactly when ordinary standalone-implementation syntax owns
an explicit `GenericParams` node, including an empty parameter sequence.
Marker implementations have no generic parameters and use `BodylessMarker`;
standalone implementations use `Ordinary`. A marker occurrence carrying
generic parameters, a where clause, or a body, or a standalone occurrence
without its required member-body syntax, is an invariant failure. Every role
and parameter sequence is sorted by complete canonical record bytes. Producer
and verifier independently prove the exact generic and callable parameter
occurrence census, owner, ordinal or receiver position, name, header site, and
canonical encoding.

### Stable Scope And Target Keys

```text
StableScopeOwnerKey =
    ModuleScope(ModuleKey) // 0x01
  | DefinitionScope(StableDefinitionQueryKey, ScopeRole) // 0x02
  | ImplementationOccurrenceScope(
        StableImplementationOccurrenceQueryKey,
        ScopeRole) // 0x03
  | BodyScope(StableOwnerBodyQueryKey, LocalSyntaxPath) // 0x04

StableBindingTargetKey =
    Definition(StableDefinitionQueryKey) // 0x01
  | Implementation(StableImplementationQueryKey) // 0x02
  | Module(ModuleKey) // 0x03
  | SemanticImport(StableSemanticImportQueryKey) // 0x04
  | OwnerLocal(StableOwnerBodyQueryKey, OwnerLocalBindingKey) // 0x05
  | AnonymousOwner(StableOwnerBodyQueryKey, AnonymousOwnerLocalKey) // 0x06
  | GenericParameter(StableGenericParameterQueryKey) // 0x07
  | CallableParameter(StableCallableParameterQueryKey) // 0x08
```

`DefinitionScope` requires a definition header that declares the role.
`ImplementationOccurrenceScope` requires an exact admitted occurrence and a
header that declares the role. `GenericParameterIdentityRecord.owner` remains
the RFC 0018 closed
`DefinitionOwner(DefinitionKey) | ImplementationOwner(ImplKey)` sum.
An implementation-owned generic has an occurrence-specific declaration site
and declaring scope but one shared semantic parameter key under the shared
implementation authority.

Cross-module targets always carry a routable module. Body-local and anonymous
targets must carry the same module and an owner visible from the requesting
body. No digest-only target is permitted.

### Stable Module Skeleton

```text
StableScopeFact {
  owner: StableScopeOwnerKey,
  parent: Optional<StableScopeOwnerKey>,
  kind: ScopeKind,
}

StableNodeSyntaxRoot =
    ModuleBody(ModuleKey) // 0x01
  | DefinitionHeader(StableDefinitionQueryKey) // 0x02
  | ImplementationHeader(
        StableImplementationOccurrenceQueryKey) // 0x03

StableNodeScopeFact {
  root: StableNodeSyntaxRoot,
  nodePath: LocalSyntaxPath,
  scope: StableScopeOwnerKey,
}

StableDeclarationFact {
  queryKey: StableDefinitionQueryKey,
  record: DefinitionIdentityRecord,
  declaringScope: StableScopeOwnerKey,
  kind: DefinitionKind,
  namespace: Namespace,
  name: DeclaredDefinitionName,
  activation: DefinitionActivation,
  visibility: Optional<MemberVisibility>,
}

StableImplementationOccurrenceFact {
  occurrence: StableImplementationOccurrenceQueryKey,
  authority: StableImplementationQueryKey,
  record: ImplIdentityRecord,
  declaringScope: StableScopeOwnerKey,
}

StableGenericParameterDeclarationFact {
  queryKey: StableGenericParameterQueryKey,
  record: GenericParameterIdentityRecord,
  headerSite: StableHeaderSite,
  declaringScope: StableScopeOwnerKey,
  name: DeclaredDefinitionName,
}

StableCallableParameterDeclarationFact {
  queryKey: StableCallableParameterQueryKey,
  record: CallableParameterIdentityRecord,
  headerSite: StableHeaderSite,
  declaringScope: StableScopeOwnerKey,
  name: Optional<DeclaredDefinitionName>,
}

StableImportFact {
  queryKey: StableSemanticImportQueryKey,
  declaringScope: StableScopeOwnerKey,
  target: StableBindingTargetKey,
  canonicalTarget: StableBindingTargetKey,
  namespace: Namespace,
  origin: BindingOrigin,
  visibility: Optional<MemberVisibility>,
  exported: bool,
}

StableModuleAliasFact {
  queryKey: StableSemanticImportQueryKey,
  declaringScope: StableScopeOwnerKey,
  alias: StableDefinitionQueryKey,
  canonicalModule: ModuleKey,
  targetSurfaceRevision: ExportSurfaceRevision,
}

StableReexportStep {
  module: ModuleKey,
  exportPath: LocalSyntaxPath,
  binding: StableBindingTargetKey,
  canonicalTarget: StableBindingTargetKey,
}

StableLocalExportFact {
  declaringModule: ModuleKey,
  exportPath: LocalSyntaxPath,
  name: BindingNameKey,
  binding: StableBindingTargetKey,
  canonicalTarget: StableBindingTargetKey,
  visibility: Optional<MemberVisibility>,
  reexportChain: CanonicalSequence<StableReexportStep>,
}

StableFailedLookupOutcome =
    Missing // 0x01
  | NamespaceMismatch {
      availableNamespaces: CanonicalNonEmptySequence<Namespace>,
    } // 0x02
  | Ambiguous {
      candidates: CanonicalNonEmptySequence<StableBindingTargetKey>,
    } // 0x03

StableFailedLookupFact {
  owner: BinderQueryOwner,
  usePath: LocalSyntaxPath,
  namespace: Namespace,
  name: DeclaredDefinitionName,
  outcome: StableFailedLookupOutcome,
}

BoundModuleSkeleton {
  module: ModuleKey,
  scopes: CanonicalSequence<StableScopeFact>,
  nodeScopes: CanonicalSequence<StableNodeScopeFact>,
  declarations: CanonicalSequence<StableDeclarationFact>,
  implementationOccurrences:
      CanonicalSequence<StableImplementationOccurrenceFact>,
  genericParameterDeclarations:
      CanonicalSequence<StableGenericParameterDeclarationFact>,
  callableParameterDeclarations:
      CanonicalSequence<StableCallableParameterDeclarationFact>,
  moduleAliases: CanonicalSequence<StableModuleAliasFact>,
  imports: CanonicalSequence<StableImportFact>,
  localExports: CanonicalSequence<StableLocalExportFact>,
  bodyOwners: CanonicalNonEmptySequence<StableOwnerBodyQueryKey>,
  failedLookups: CanonicalSequence<StableFailedLookupFact>,
}
```

Each sequence sorts by complete encoded record bytes. A generic key may occur
once for a definition authority site and once per equal implementation source
occurrence, but the tuple `(queryKey, headerSite)` is unique. Callable
parameters belong only to definition headers. The module owner is present
exactly once. Definition body owners correspond bijectively to
`ExecutableBody` inventory entries.

Every scope parent exists, remains in the same module, and has lower depth.
There is exactly one module scope. Definition and implementation occurrence
scope roles match their complete headers. Duplicate facts, unequal records for
one key/site, cycles, missing owners, or foreign modules reject publication.
Every non-boundary syntax node in the module skeleton has exactly one
`StableNodeScopeFact`. Its closed `root` identifies the only syntax tree in
which `nodePath` is interpreted. The root module must equal the skeleton
module, the referenced header or module-body provenance must exist exactly
once, and the scope owner must belong to that same root and module. Every local
export and reexport step carries its distinct
`exportPath`; provider and verifier prove that the path resolves to the exact
export occurrence and reconstruct its `NodeId` and source span only during
materialization.

### Stable Lookup Projections

```text
StableExportedBinding {
  name: BindingNameKey,
  binding: StableBindingTargetKey,
  canonicalTarget: StableBindingTargetKey,
  visibility: Optional<MemberVisibility>,
  exported: bool,
}

StableExportedBindingQueryKey {
  module: ModuleKey,
  name: BindingNameKey,
}

StableScopeNameBucketQueryKey {
  scope: StableScopeOwnerKey,
  name: BindingNameKey,
}

ModuleExportNames(ModuleKey)
    -> CanonicalSequence<BindingNameKey>

ExportedBinding(StableExportedBindingQueryKey)
    -> Optional<StableExportedBinding>

DefinitionBindingHeader(StableDefinitionQueryKey)
    -> Optional<StableDeclarationFact>

ImplementationBindingHeader(StableImplementationQueryKey)
    -> Optional<CanonicalSequence<StableImplementationOccurrenceFact>>

ScopeNameBucket(StableScopeNameBucketQueryKey)
    -> CanonicalSequence<StableBindingTargetKey>

ImportTarget(StableSemanticImportQueryKey)
    -> Optional<StableImportFact>

BindingVisibility(StableBindingTargetKey)
    -> Optional<MemberVisibility>
```

Each projection is retained `Semantic`, reads only its exact owning skeleton or
header projection, and rejects cycles. `BindingVisibility` dispatches by the
explicit module carried in the target; module targets read the exact module
export projection, global named targets read the exact header projection, and
body-local and anonymous targets are rejected because this stable projection
has no context roots. `BindOwnerBody` validates their activation and
visibility directly from its current candidate. A `BodyScope` cannot key
`ScopeNameBucket`.

Provider and verifier independently derive dependency keys. A candidate never
supplies a dependency key.

### Stable Owner Body

```text
StableBodyScopeFact {
  owner: StableOwnerBodyQueryKey,
  scope: StableScopeOwnerKey,
  parent: StableScopeOwnerKey,
  kind: ScopeKind,
}

StableBodyNodeScopeFact {
  owner: StableOwnerBodyQueryKey,
  nodePath: LocalSyntaxPath,
  scope: StableScopeOwnerKey,
}

StableOwnerLocalBindingFact {
  owner: StableOwnerBodyQueryKey,
  key: OwnerLocalBindingKey,
  kind: OwnerLocalBindingKind,
  name: DeclaredDefinitionName,
  namespace: Namespace,
  declaringScope: StableScopeOwnerKey,
  activation: DefinitionActivation,
}

StableResolutionFact {
  owner: StableOwnerBodyQueryKey,
  usePath: LocalSyntaxPath,
  namespace: Namespace,
  binding: StableBindingTargetKey,
  canonicalTarget: StableBindingTargetKey,
  origin: BindingOrigin,
}

StableDeferredMemberFact {
  owner: StableOwnerBodyQueryKey,
  usePath: LocalSyntaxPath,
  basePath: LocalSyntaxPath,
  accessKind: MemberAccessKind,
  member: DeclaredDefinitionName,
  expectedNamespaces: CanonicalNonEmptySequence<Namespace>,
  genericArgumentPaths: CanonicalSequence<LocalSyntaxPath>,
}

StableSelfOwner =
    Nominal(StableDefinitionQueryKey) // 0x01
  | Interface(StableDefinitionQueryKey) // 0x02
  | ImplementationOccurrence(StableImplementationOccurrenceQueryKey) // 0x03

StableSelfTypeFact {
  owner: StableOwnerBodyQueryKey,
  syntaxPath: LocalSyntaxPath,
  selfOwner: StableSelfOwner,
}

StableThisBindingFact {
  owner: StableOwnerBodyQueryKey,
  expressionPath: LocalSyntaxPath,
  receiver: StableCallableParameterQueryKey,
}

StableShadowTargetFact {
  owner: StableOwnerBodyQueryKey,
  binding: StableBindingTargetKey,
  shadowed: StableBindingTargetKey,
}

StableLabelKey {
  owner: StableOwnerBodyQueryKey,
  declarationPath: LocalSyntaxPath,
}

StableLabelTarget =
    Block(StableScopeOwnerKey) // 0x01
  | Loop(StableScopeOwnerKey) // 0x02

StableLabelFact {
  key: StableLabelKey,
  name: DeclaredDefinitionName,
  statementPath: LocalSyntaxPath,
  target: StableLabelTarget,
}

StableControlTarget =
    ExplicitLabel(StableLabelKey) // 0x01
  | Loop(StableScopeOwnerKey) // 0x02
  | Match(StableScopeOwnerKey) // 0x03

StableControlTransferFact {
  owner: StableOwnerBodyQueryKey,
  transferPath: LocalSyntaxPath,
  kind: ControlTransferKind,
  target: StableControlTarget,
}

StableClosureFact {
  owner: StableOwnerBodyQueryKey,
  closure: AnonymousOwnerLocalKey,
  scope: StableScopeOwnerKey,
}

StableClosureFreeVariable {
  target: StableBindingTargetKey,
  referencePaths: CanonicalNonEmptySequence<LocalSyntaxPath>,
}

StableClosureFreeVariableFact {
  owner: StableOwnerBodyQueryKey,
  closure: AnonymousOwnerLocalKey,
  variables: CanonicalSequence<StableClosureFreeVariable>,
}

StableExplicitCaptureMode =
    ByValue // 0x01
  | ByReference // 0x02
  | This // 0x03

StableExplicitCaptureBindingFact {
  itemPath: LocalSyntaxPath,
  target: StableBindingTargetKey,
  mode: StableExplicitCaptureMode,
}

StableExplicitClosureCaptureFact {
  owner: StableOwnerBodyQueryKey,
  closure: AnonymousOwnerLocalKey,
  captureListPath: LocalSyntaxPath,
  captures: CanonicalSequence<StableExplicitCaptureBindingFact>,
}

BoundOwnerBody {
  owner: StableOwnerBodyQueryKey,
  scopes: CanonicalSequence<StableBodyScopeFact>,
  nodeScopes: CanonicalSequence<StableBodyNodeScopeFact>,
  bindings: CanonicalSequence<StableOwnerLocalBindingFact>,
  resolutions: CanonicalSequence<StableResolutionFact>,
  deferredMembers: CanonicalSequence<StableDeferredMemberFact>,
  selfTypes: CanonicalSequence<StableSelfTypeFact>,
  thisBindings: CanonicalSequence<StableThisBindingFact>,
  shadowTargets: CanonicalSequence<StableShadowTargetFact>,
  labels: CanonicalSequence<StableLabelFact>,
  controlTransfers: CanonicalSequence<StableControlTransferFact>,
  closures: CanonicalSequence<StableClosureFact>,
  closureFreeVariables: CanonicalSequence<StableClosureFreeVariableFact>,
  explicitClosureCaptures:
      CanonicalSequence<StableExplicitClosureCaptureFact>,
  failedLookups: CanonicalSequence<StableFailedLookupFact>,
}
```

Every path resolves inside the exact pruned owner syntax and never enters a
`StableItemBoundary`. Every target is visible at the use path under the exact
scope, import, header, and visibility projections read by the provider.
Provider and verifier use separate traversal, activation, lookup, capture, and
control-flow implementations.

Every non-boundary node in the pruned owner syntax has exactly one
`StableBodyNodeScopeFact`. Every closure publishes exactly one
`StableClosureFreeVariableFact`; `variables` is empty for a closure with no
implicit free variables. Explicit capture lists remain independently present,
including an empty explicit list.

The hand-authored canonical stable fact inventory is exhaustive and maps the
live Binder domains one-to-one:

| Live fact domain | Stable replacement |
|---|---|
| `SourceFailure` | the sole RFC 0017 `DiagnosticFact` collection described below |
| `Scope` | `StableScopeFact` or `StableBodyScopeFact` |
| `NodeScope` | `StableNodeScopeFact` or `StableBodyNodeScopeFact` |
| `NodeBinding` | `StableResolutionFact`, `StableDeferredMemberFact`, or `StableFailedLookupFact` |
| `SelfType`, `ThisBinding` | `StableSelfTypeFact`, `StableThisBindingFact` |
| `Definition`, `Implementation` | `StableDeclarationFact`, `StableImplementationOccurrenceFact` |
| `ModuleAlias`, `Import`, `LocalExport` | `StableModuleAliasFact`, `StableImportFact`, `StableLocalExportFact` |
| `DeferredMember`, `ShadowTarget` | `StableDeferredMemberFact`, `StableShadowTargetFact` |
| `Label`, `ControlTransfer` | `StableLabelFact`, `StableControlTransferFact` |
| `ClosureFreeVariable`, `ExplicitClosureCapture` | `StableClosureFreeVariableFact`, `StableExplicitClosureCaptureFact` |
| `GenericParameter`, `CallableParameter` | the complete parameter declaration facts above |
| `OwnerLocalBinding` | `StableOwnerLocalBindingFact` |

`binding-fact-schema.def` is replaced by the hand-authored canonical stable
schema inventory with exactly these domains, tags, field order, mutation
classes, producer provenance, verifier provenance, and materialized target. A
zero-reference gate rejects any fact domain outside this canonical inventory.

`StableFailedLookupFact` is a semantic body fact, not a query-runtime failure
or a substitute diagnostic envelope. `Missing` has no candidates.
`NamespaceMismatch` has at least one distinct namespace sorted by tag.
`Ambiguous` has at least two distinct candidates sorted by complete target
bytes. Each failed lookup has one matching RFC 0017 diagnostic occurrence.

### Deterministic Module Allocation Plan

Materializers do not allocate from shared mutable counters.

```text
ModuleBindingAllocationPlan {
  key: ContextualModuleKey,
  skeletonScopeCount: uint32,
  implementationOccurrenceCount: uint32,
  owners: CanonicalSequence<OwnerAllocationRange>,
}

OwnerAllocationRange {
  owner: StableOwnerBodyQueryKey,
  scopeBegin: uint32,
  scopeCount: uint32,
  ownerLocalBegin: uint32,
  ownerLocalCount: uint32,
  anonymousBegin: uint32,
  anonymousCount: uint32,
  labelBegin: uint32,
  labelCount: uint32,
}
```

`ModuleBindingAllocationPlan(ContextualModuleKey)` is retained `Semantic`. It
reads exactly `BindModuleSkeleton(key.module)` and every canonical
`BindOwnerBody(ContextualBodyOwnerKey {
contextRoots: key.contextRoots, body: owner })`.
Skeleton scopes occupy `[0, skeletonScopeCount)`. Owner ranges follow canonical
owner-key order and use checked prefix sums. Each body's facts allocate by
complete stable record order. Implementation occurrence slots allocate by
complete `ImplSourceOccurrenceKey` order.

Counts must fit their handle domains. Ranges are dense, disjoint, gap-free, and
complete. Provider and verifier compute prefix sums and record order
independently. The plan contains no handle, brand, source range, or revision.
`ScopeId`, `ImplOccurrenceId`, `OwnerLocalBindingId`,
`AnonymousOwnerLocalId`, and `LabelId` are distinct module-local handle
domains. Every one is branded by the semantic context and module and validates
both before comparison or lookup. `AnonymousOwnerLocalId` is the dense slot
for one `AnonymousOwnerLocalKey`; none of the five is a global identity or
enters the global interner set. Each owner range covers exactly the body's
scope, owner-local, anonymous-owner, and label facts.

### Query Result And Diagnostic Wire

There is no `BinderFailureKind` or second overlapping failure enum.

```text
BinderQueryResult<T> =
    Value {
      value: T,
      diagnostics: CanonicalSequence<DiagnosticFact>,
    } // 0x01
  | SourceRejected {
      diagnostics: NonEmptyCanonicalSequence<DiagnosticFact>,
    } // 0x02
  | KeyRejected {
      failure: BinderKeyFailure,
    } // 0x03

BinderKeyFailureKind =
    MissingSelectedModuleSource // 0x01
  | InactiveOwner // 0x02
  | ForeignOwner // 0x03
  | DefinitionWithoutBody // 0x04
  | BoundaryMismatch // 0x05
  | NonSelectedSource // 0x06
  | CrossBoundaryPath // 0x07

BinderQueryOwner =
    Module(ModuleKey) // 0x01
  | DefinitionHeader(StableDefinitionQueryKey) // 0x02
  | ImplementationHeader(StableImplementationOccurrenceQueryKey) // 0x03
  | Body(ContextualBodyOwnerKey) // 0x04

BinderKeyFailure {
  kind: BinderKeyFailureKind,
  owner: BinderQueryOwner,
  path: Optional<LocalSyntaxPath>,
}

CapabilityDemandResult<Descriptor> =
    Published(QueryCapabilityLease<const Descriptor::Capability>)
  | descriptor-listed SourceRejected
  | descriptor-listed KeyRejected
  | RuntimeRejected(QueryRuntimeFailure)
```

RFC 0017 `DiagnosticFact` is the sole diagnostic wire. This RFC defines no
Binder-specific fact, note, display argument, occurrence, producer,
materialized diagnostic, or upstream-failure-key record. `Value` applies when
the key is valid and a complete semantic value can be published; its sequence
contains only diagnostics owned by that provider. A value may therefore
contain failed-lookup facts and their matching ordinary Binder diagnostics.

`SourceRejected` applies only when a required upstream source-producing query
already returned RFC 0017 facts and no Binder value can be built. It forwards
those facts byte-for-byte. Module-resolution missing, ambiguity, cycle, and
visibility failures remain the module-system contribution to
`ModuleDiagnosticFacts`; Binder suppresses dependent diagnostics for that
syntax occurrence and never wraps a module-resolution failure.

`KeyRejected` represents only deterministic invalidity of the requested key
and contains no diagnostic. Missing or duplicate provenance, malformed
detached syntax, codec disagreement, missing tracked reads, unequal-record
collision, foreign brand, allocation failure, cancellation, cycle, or
verifier disagreement is `QueryRuntimeFailure` and publishes neither a
semantic result nor an ordinary diagnostic.

The RFC 0017 closed diagnostic enums receive these exact additions:

```text
DiagnosticPhaseOrQueryKind +=
  Binder(BinderDiagnosticProducer) // 0x07

DiagnosticEmitterSite +=
  Binder(BinderDiagnosticEmitter) // 0x07

BinderDiagnosticProducer =
    BindModuleSkeleton // 0x01
  | BindOwnerBody // 0x02

BinderDiagnosticEmitter =
    Declaration // 0x01
  | Lookup // 0x02
  | ControlTransfer // 0x03
  | ContextualSelf // 0x04

BinderIdentifierDiagnosticArguments {
  identifier: DeclaredDefinitionName,
}

BinderNamespaceDiagnosticArguments {
  identifier: DeclaredDefinitionName,
  expectedNamespace: Namespace,
}
```

Outer tags `0x01` through `0x06` retain the synchronized RFC 0017 and RFC
0025 meanings. `BinderIdentifierDiagnosticArguments` uses domain
`zom.diagnostic.arguments.binder-identifier`; the namespace form uses
`zom.diagnostic.arguments.binder-namespace`. They encode the domain, one zero
byte, then their complete fields in declaration order and occupy the existing
RFC 0017 canonical argument-record field. They are code-specific payload
schemas, not a second diagnostic or display-argument wire.

Precedence is `QueryRuntimeFailure`, `KeyRejected`, upstream
`SourceRejected`, then `Value`. An earlier alternative suppresses all later
alternatives. `SourceRejected` suppresses local Binder diagnostics and `Value`
contains no forwarded upstream diagnostics. `BoundaryMismatch`,
`NonSelectedSource`, and `CrossBoundaryPath` require `path`; the other key
failures forbid it.

`VerifyBoundModule` publishes no capability when a child is
`SourceRejected` or a child `Value.diagnostics` contains an error. Its failure
projection returns the exact canonical union of those existing facts.
Successful `VerifiedBoundModule` capabilities contain no diagnostics.
`ModuleDiagnosticFacts(ContextualModuleKey)` is the only Binder collector;
`CompilationDiagnosticFacts` performs RFC 0017 occurrence-plus-payload
deduplication, `MaterializeCompilationDiagnostics` alone resolves provenance,
and only the driver adapter emits.

`CapabilityDemandResult<Descriptor>` is a move-only caller result and has no
canonical codec or semantic equality. Each capability descriptor declares one
closed `CapabilityFailureList`. The generated provider result, storage,
constructors, codecs, independent rejection verifiers, and public observers
exist only for the listed `SourceRejection<DiagnosticFact>` or
`KeyRejection<BinderKeyFailure>` wrappers. No dummy failure type or absent
alternative is instantiated.

Only `Published` corresponds to an installed retained capability memo.
Verified source and key rejections cross type erasure through the canonical
RFC 0028 capability-failure envelope and install neither a semantic memo nor a
capability memo. `RuntimeRejected` is never encoded. Descriptor registration
and key decoding, final admission, cancellation, cycle detection, ordered
dependency failures, provider result, independent rejection or candidate
verification, envelope encoding or candidate publication, and public decoding
follow the exact RFC 0028 precedence.

For failed lookups, `StableFailedLookupFact` is unique by `(owner, usePath)`.
Provider and verifier independently reconstruct name and namespace from
detached syntax. A module-skeleton lookup uses the module diagnostic root,
`Binder::BindModuleSkeleton` phase tag, absent semantic owner,
`Binder::Lookup` emitter, and `usePath`; an owner-body lookup uses the same
module root, `Binder::BindOwnerBody` tag,
`Some(owner.body.owner: StableBodyOwnerKey)`, emitter, and path. Context roots
exist only in the outer `ContextualDiagnosticProvenanceKey` demand. The
primary `DiagnosticLocation::Source(ModuleSite)` carries the same module,
projected stable owner, path, and emitter with occurrence zero because that
tuple is unique.

| Stable lookup outcome | Exact diagnostic mapping |
|---|---|
| `Missing` | `ZOM3001 UndefinedIdentifier`; `BinderIdentifierDiagnosticArguments`; empty secondary sequence; no fix-it |
| `NamespaceMismatch` | `ZOM3002 SymbolNamespaceMismatch`; `BinderNamespaceDiagnosticArguments`; empty secondary sequence; no fix-it |
| `Ambiguous` | `ZOM3028 AmbiguousIdentifier`; `BinderIdentifierDiagnosticArguments`; empty secondary sequence; no fix-it |

The implementation adds exactly
`DIAG(3028, AmbiguousIdentifier, kError, "Identifier '{0}' is ambiguous", 1)`
to the registered diagnostic inventory before any provider can publish that
outcome.

Every failed lookup has exactly one matching Binder lookup `DiagnosticFact`
and every Binder lookup fact has exactly one failed lookup. Code, arguments,
diagnostic root, phase, semantic owner, emitter, occurrence, primary location,
secondary sequence, fix-it absence, owner, path, namespace, name, available
namespaces or ambiguity candidates, and outcome must match this table. Other Binder
diagnostics use the RFC 0017 wire with declaration, control-transfer, or
contextual-self emitter sites and are outside this bijection.

### Query Catalog And Complete Read Sets

Every descriptor declares exactly one kind-specific literal RFC 0028 metadata
record and exactly one explicit production-inventory ordinal.
`registerDescriptor<Descriptor>()` is the only registration path. The
generated inventory binds that ordinal to the descriptor type, literal name,
literal domain, metadata kind, and owning path. Registration order cannot
change `QueryKindId`.

Semantic descriptors declare complete tracked reads, canonical equality,
provider, independent verifier, retention, cycle policy, and cost. Capability
descriptors additionally declare admission and their exact failure-alternative
list. Every row uses `Reject` cycle policy. All new memo rows are `Retained`;
revision-local capabilities have no canonical value codec or structural
equality operation.

| Query | Domain | Key | Exact result; reuse; equality |
|---|---|---|---|
| `CompleteCompilationContextAuthorityInput` | `zom.input.complete-compilation-context-authority` | `CompilationRootSetQueryKey` | `CompleteCompilationContextAuthority`; `Input`; complete canonical value bytes |
| `ActiveDefinitionAuthorityInput` | `zom.query.active-definition-authority` | `ContextualDefinitionKey` | `DefinitionIdentityRecord`; `Input`; complete canonical value bytes |
| `ActiveImplementationAuthorityInput` | `zom.query.active-implementation-authority` | `ContextualImplementationKey` | `ActiveImplementationMembershipRecord`; `Input`; complete canonical value bytes |
| `ActiveGenericParameterAuthorityInput` | `zom.query.active-generic-parameter-authority` | `ContextualGenericParameterKey` | `ActiveGenericParameterMembership`; `Input`; complete canonical value bytes |
| `ActiveCallableParameterAuthorityInput` | `zom.query.active-callable-parameter-authority` | `ContextualCallableParameterKey` | `ActiveCallableParameterMembershipRecord`; `Input`; complete canonical value bytes |
| `CompleteRootIdentityReadinessInput` | `zom.binder.complete-root-identity-readiness` | `CompilationRootSetQueryKey` | `CompleteRootIdentityReadiness`; `Input`; complete canonical value bytes |
| `DefinitionHeaderSyntax` | `zom.query.definition-header-syntax` | `StableDefinitionQueryKey` | `BinderQueryResult<StableDefinitionHeader>`; `Semantic`; complete canonical result bytes |
| `ImplementationOccurrenceHeaderSyntax` | `zom.query.implementation-occurrence-header-syntax` | `StableImplementationOccurrenceQueryKey` | `BinderQueryResult<StableImplementationOccurrenceHeader>`; `Semantic`; complete canonical result bytes |
| `ModuleExportNames` | `zom.query.module-export-names` | `ModuleKey` | canonical name sequence; `Semantic`; complete canonical bytes |
| `ExportedBinding` | `zom.query.exported-binding` | `StableExportedBindingQueryKey` | optional stable exported binding; `Semantic`; complete canonical bytes |
| `DefinitionBindingHeader` | `zom.query.definition-binding-header` | `StableDefinitionQueryKey` | optional stable declaration; `Semantic`; complete canonical bytes |
| `ImplementationBindingHeader` | `zom.query.implementation-binding-header` | `StableImplementationQueryKey` | optional canonical occurrence sequence; `Semantic`; complete canonical bytes |
| `ScopeNameBucket` | `zom.query.scope-name-bucket` | `StableScopeNameBucketQueryKey` | canonical target sequence; `Semantic`; complete canonical bytes |
| `ImportTarget` | `zom.query.import-target` | `StableSemanticImportQueryKey` | optional stable import; `Semantic`; complete canonical bytes |
| `BindingVisibility` | `zom.query.binding-visibility` | `StableBindingTargetKey` | `Optional<MemberVisibility>`; `Semantic`; complete canonical bytes |
| `ModuleBodyOwners` | `zom.query.module-body-owners` | `ContextualModuleKey` | canonical stable owner sequence; `Semantic`; complete canonical bytes |
| `ModuleDiagnosticFacts` | `zom.query.module-diagnostic-facts` | `ContextualModuleKey` | canonical RFC 0017 fact sequence; `Semantic`; complete canonical bytes |
| `ActiveCompilationUnitMembership` | `zom.query.active-compilation-unit-membership` | contextual unit key | complete active-crate authority record or semantic absence; `Semantic`; complete canonical bytes |
| `ActiveCrateMembership` | `zom.query.active-crate-membership` | contextual crate key | complete crate authority record or semantic absence; `Semantic`; complete canonical bytes |
| `ActiveSourceMembership` | `zom.query.active-source-membership` | contextual source key | complete source authority record or semantic absence; `Semantic`; complete canonical bytes |
| `ActiveModuleMembership` | `zom.query.active-module-membership` | `ContextualModuleKey` | complete module authority record or semantic absence; `Semantic`; complete canonical bytes |
| `ActiveDefinitionMembership` | `zom.query.active-definition-membership` | contextual routed definition key | complete definition authority record or semantic absence; `Semantic`; complete canonical bytes |
| `ActiveImplementationMembership` | `zom.query.active-implementation-membership` | contextual routed implementation key | complete implementation authority record or semantic absence; `Semantic`; complete canonical bytes |
| `ActiveGenericParameterMembership` | `zom.query.active-generic-parameter-membership` | contextual routed generic key | complete generic authority record or semantic absence; `Semantic`; complete canonical bytes |
| `ActiveCallableParameterMembership` | `zom.query.active-callable-parameter-membership` | contextual routed callable key | complete callable authority record or semantic absence; `Semantic`; complete canonical bytes |
| `BindModuleSkeleton` | `zom.query.bind-module-skeleton` | `ModuleKey` | `BinderQueryResult<BoundModuleSkeleton>`; `Semantic`; complete canonical result bytes |
| `BindOwnerBody` | `zom.query.bind-owner-body` | `ContextualBodyOwnerKey` | `BinderQueryResult<BoundOwnerBody>`; `Semantic`; complete canonical result bytes |
| `ModuleBindingAllocationPlan` | `zom.query.module-binding-allocation-plan` | `ContextualModuleKey` | `BinderQueryResult<ModuleBindingAllocationPlan>`; `Semantic`; complete canonical result bytes |
| `ModuleDependencyProvenance` | `zom.query.module-dependency-provenance` | `ModuleKey` | `CapabilityDemandResult<ModuleDependencyProvenance>`; payload `ModuleDependencyProvenanceMap`; successful retained revision-local capability |
| `MaterializeModuleGraph` | `zom.query.materialize-module-graph` | complete `CompilationRootSetQueryKey` | `CapabilityDemandResult<MaterializeModuleGraph>`; payload `MaterializedModuleGraph`; successful retained revision-local capability |
| `MaterializeModuleSkeleton` | `zom.query.materialize-module-skeleton` | `ContextualModuleKey` | `CapabilityDemandResult<MaterializeModuleSkeleton>`; payload `MaterializedModuleSkeleton`; successful retained revision-local capability |
| `MaterializeOwnerBody` | `zom.query.materialize-owner-body` | `ContextualBodyOwnerKey` | `CapabilityDemandResult<MaterializeOwnerBody>`; payload `MaterializedOwnerBody`; successful retained revision-local capability |
| `VerifyBoundModule` | `zom.query.verify-bound-module` | `ContextualModuleKey` | `CapabilityDemandResult<VerifyBoundModule>`; payload `VerifiedBoundModule`; successful retained revision-local capability |

Each canonical `CapabilityQuery` row records `resultType`, `capabilityType`,
and the exact `failureAlternatives` in addition to its descriptor, provider,
verifier, and test tasks. `resultType` is always
`CapabilityDemandResult<name>`. When a descriptor task lands, it selects only
its owned row and compiles independent equality checks requiring
`name::Capability` to equal `capabilityType` and
`name::FailureAlternatives` to equal the schema-derived
`CapabilityFailureList<failureAlternatives...>`. The final capability
architecture gate rejects an implemented descriptor without both checks.
Rows owned by later tasks remain inert and are not named by an earlier C++
consumer.

`BoundOwnerBody` remains the sole stable authority for
`StableClosureFact`, `StableClosureFreeVariableFact`, and
`StableExplicitClosureCaptureFact`. `MaterializeOwnerBody` expands those facts
directly from the exact `BindOwnerBody` result. No duplicate closure
projection exists in the descriptor catalog, read sets, schema, codecs,
provider/verifier inventory, memo graph, consumer graph, or architecture
allowlist.

| Query | Complete tracked reads; independent verifier boundary; cost |
|---|---|
| complete-context input | transaction payload only; independent input verifier recomputes roots, package/crate edges, per-crate options/search roots, core distribution digest, and semantic-context fingerprint inputs before commit; linear in complete context |
| header queries | exact owning named inventory, selected source, parse capability, definition and implementation sites; verifier repeats authority/occurrence selection and detachment; linear in one header |
| `ModuleExportNames` | exact `BindModuleSkeleton(module)` value and its local exports; verifier independently filters exported visible names and sorts complete name bytes; linear in module exports |
| `ExportedBinding` | exact owning skeleton, matching local-export facts, and only reached reexport `ExportedBinding` projections; verifier independently resolves the complete reexport chain and visibility; linear in one reexport chain |
| `DefinitionBindingHeader` | exact owning skeleton and exact definition header; verifier independently matches query key, complete record, declaration site, and scope; constant after skeleton demand |
| `ImplementationBindingHeader` | exact owning skeleton and every occurrence header for the shared implementation key; verifier independently derives, sorts, and compares the complete equal-occurrence set; linear in equal occurrences |
| `ScopeNameBucket` | exact owning skeleton selected by the non-body scope tag and only declarations active in that scope; verifier independently applies activation and namespace rules; a `BodyScope` key is rejected; linear in one scope bucket |
| `ImportTarget` | exact owning skeleton, dependency request, module-resolution result, and reached export projection; verifier independently derives requester, target, canonical target, origin, visibility, and export bit; linear in one import chain |
| `BindingVisibility` | exact global target owner, matching header/import/export projection, and only the reached reexport chain; body-local and anonymous alternatives fail descriptor-key validation before memo admission; verifier independently applies the visibility ladder; linear in one target chain |
| `ModuleBodyOwners` | contextual module key, exact skeleton, active-definition memberships for executable definitions, and complete-root readiness only for absent or contradictory authority; verifier independently derives the canonical owner set; linear in body owners |
| `ModuleDiagnosticFacts` | contextual module key, module-resolution diagnostics, skeleton result, every canonical owner-body result, and no materialized capability; verifier independently reconstructs the canonical RFC 0017 occurrence union and failed-lookup bijection; linear in module facts |
| compilation-unit membership | complete context authority and exact `ActiveCrates(contextRoots)`; verifier independently filters the complete non-empty equal-unit subset and checks context-root readiness only on absence; linear in active crates |
| crate membership | exact `ActiveCrates(contextRoots)` and compilation-unit membership; verifier independently proves one complete crate occurrence; linear in active crates |
| source membership | exact owning crate membership and `ActiveSources(crate)`; verifier independently proves one complete source occurrence; linear in active sources |
| module membership | exact owning crate membership and `ActiveModules(crate)`; verifier independently proves one complete module occurrence; linear in active modules |
| definition membership | exact contextual authority input, owning named-definition inventory, exact header, and conditional complete-root readiness only on absent or contradictory authority; verifier independently checks full record, owner, site, and disposition; constant after inventory demand |
| implementation membership | exact contextual implementation-authority input, owning named-implementation inventory, all equal occurrence headers, active module membership, and conditional readiness only on absent or contradictory authority; verifier independently derives authority occurrence and byte-equal complete record; linear in equal occurrences |
| generic-parameter membership | exact contextual generic-parameter-authority input, exact active definition or implementation membership selected by that input, the authority header, every equal implementation occurrence header when applicable, and conditional readiness only on absent or contradictory authority; verifier independently checks owner sum, ordinal, name, record, and occurrence coverage; linear in equal occurrences |
| callable-parameter membership | exact contextual callable-parameter-authority input, exact active definition membership selected by that input, definition header, and conditional readiness only on absent or contradictory authority; verifier independently checks definition owner, position, receiver legality, name, and complete record; linear in callable parameters |
| `BindModuleSkeleton` | module-body syntax, both named inventories, every exact header, dependency requests, each resolution, and only reached export/header projections; verifier independently rebuilds scope, import, export, alias, and owner inventories; expensive linear module skeleton |
| `BindOwnerBody` | contextual owner syntax, owning skeleton, and only reached scope, import, header, export, and visibility projections with byte-equal roots; verifier independently traverses, resolves, and rebuilds every fact domain; expensive linear owner body |
| allocation plan | owning skeleton, contextual module owners, and every contextual body in canonical owner order; verifier independently computes all five dense ranges and checked sums; linear in published facts |
| dependency provenance | selected source, exact dependency sites, final parse capability, and stable requests; verifier rebuilds the total source-and-ordinal map; linear in dependency sites |
| module graph materialization | the complete context authority and every graph, SCC, fingerprint, active-set, selected-source, request, resolution, prelude, final parse, provenance, and four-domain membership read listed below; verifier reconstructs complete roots and all stable/handle edges; expensive whole-context |
| skeleton materialization | exact skeleton and allocation plan, definition/implementation sites, module-body and dependency provenance, graph capability, and every reached eight-domain membership; verifier independently expands facts and current provenance; linear in skeleton facts |
| body materialization | exact contextual body and allocation plan, owner-body provenance, skeleton capability, and every reached eight-domain membership; verifier independently expands facts and current provenance; linear in body facts |
| bound-module verification | contextual owners, skeleton, allocation plan, graph, skeleton capability, stable and materialized bodies, parse and inventory capabilities, dependency/prelude surfaces, imports, aliases, and export projections; verifier independently proves aggregate coverage and lease lineage; expensive linear module aggregate |

`ModuleDependencyProvenance(ModuleKey)` is a final-sealed retained
revision-local capability. Its runtime-only payload is:

```text
ModuleDependencyProvenanceSite {
  schemaPreorderOrdinal: uint32,
  node: NodeId,
  span: SourceSpan,
}

ModuleDependencyProvenanceOrigin =
    Source(CanonicalNonEmptySequence<ModuleDependencyProvenanceSite>)
  | Prelude

ModuleDependencyProvenanceEntry {
  request: ModuleResolutionKey,
  origin: ModuleDependencyProvenanceOrigin,
}

ModuleDependencyProvenanceMap {
  module: ModuleKey,
  source: SourceFileKey,
  sourceDigest: Sha256Digest,
  entries: CanonicalSequence<ModuleDependencyProvenanceEntry>,
}
```

The payload has no canonical value domain, public codec, cross-revision
equality, or persistence contract. Its memo retains the exact final
`ParseSource` capability that owns every `NodeId`, span, AST node, and source
buffer. Complete reads are `SelectedModuleSourceQuery`,
`ModuleDependencySitesQuery`, `ModuleDependencyRequestsQuery`, and that final
parse capability.

Entries are unique and strictly ordered by complete request bytes. Source
origins contain nonempty, strictly increasing preorder sites; the payloadless
prelude origin occurs at most once. Every stable request and detached
non-prelude site is covered exactly once, requester, kind, path, node, source,
digest, and span agree with the retained parse, and all sequence counts obey
`DependencySitesOrGraphEdges`. Provider and verifier independently rebuild
the ordinal-to-node map, request grouping, spans, complete candidate, and
stable witness.

Its exact failure alternatives are source rejection with the final parse
diagnostics and key rejection only for
`BinderKeyFailure { MissingSelectedModuleSource, Module(module), none }`.
Noncanonical key bytes, admission mismatch, malformed provenance, allocation,
cancellation, collision, or verifier disagreement are query-runtime
rejections. `MaterializeModuleGraph` and `MaterializeModuleSkeleton` are the
only direct consumers; their independent verifiers re-demand the capability.

Cancellation, cycles, collision, foreign brand, inactive materialization,
allocation failure, malformed candidate, codec disagreement, or verifier
disagreement maps to `QueryRuntimeFailure`. Only the closed semantic result
alternatives above may enter a semantic memo.

Capability permission is compile-time only:

```text
ActiveMaterializerPermission<
  Descriptor,
  GlobalIdentityKey,
  MembershipDescriptor
>
```

`MaterializeModuleGraph` has permissions only for compilation units, crates,
sources, and modules. `MaterializeModuleSkeleton` and
`MaterializeOwnerBody` have explicit individual rows for all eight global
identity domains. The other descriptors have none. Wildcard permission and
runtime descriptor-name dispatch are forbidden.

| Materializer descriptor | Global key | Membership descriptor |
|---|---|---|
| `MaterializeModuleGraph` | `CompilationUnitIdentity` | `ActiveCompilationUnitMembership` |
| `MaterializeModuleGraph` | `CrateKey` | `ActiveCrateMembership` |
| `MaterializeModuleGraph` | `SourceFileKey` | `ActiveSourceMembership` |
| `MaterializeModuleGraph` | `ModuleKey` | `ActiveModuleMembership` |
| `MaterializeModuleSkeleton` | `CompilationUnitIdentity` | `ActiveCompilationUnitMembership` |
| `MaterializeModuleSkeleton` | `CrateKey` | `ActiveCrateMembership` |
| `MaterializeModuleSkeleton` | `SourceFileKey` | `ActiveSourceMembership` |
| `MaterializeModuleSkeleton` | `ModuleKey` | `ActiveModuleMembership` |
| `MaterializeModuleSkeleton` | `DefinitionKey` | `ActiveDefinitionMembership` |
| `MaterializeModuleSkeleton` | `ImplKey` | `ActiveImplementationMembership` |
| `MaterializeModuleSkeleton` | `GenericParameterKey` | `ActiveGenericParameterMembership` |
| `MaterializeModuleSkeleton` | `CallableParameterKey` | `ActiveCallableParameterMembership` |
| `MaterializeOwnerBody` | `CompilationUnitIdentity` | `ActiveCompilationUnitMembership` |
| `MaterializeOwnerBody` | `CrateKey` | `ActiveCrateMembership` |
| `MaterializeOwnerBody` | `SourceFileKey` | `ActiveSourceMembership` |
| `MaterializeOwnerBody` | `ModuleKey` | `ActiveModuleMembership` |
| `MaterializeOwnerBody` | `DefinitionKey` | `ActiveDefinitionMembership` |
| `MaterializeOwnerBody` | `ImplKey` | `ActiveImplementationMembership` |
| `MaterializeOwnerBody` | `GenericParameterKey` | `ActiveGenericParameterMembership` |
| `MaterializeOwnerBody` | `CallableParameterKey` | `ActiveCallableParameterMembership` |

Contextual keys contain the complete `CompilationRootSetQueryKey` followed by
the stable routed key. Every child key carries byte-equal `contextRoots`.
Provider and verifier derive all dynamic dependency keys from independently
verified upstream values. Owner aggregation remains in canonical owner order,
never completion order.

`ModuleBodyOwners` follows RFC 0020's conditional authority rule. A present
definition authority reads its exact owning inventory and does not read
readiness. An absent or contradictory authority probes readiness. Missing
readiness returns `ProviderRejected`; present readiness plus absent authority
publishes `InactiveOwner`; present readiness plus contradictory authority is
`InvariantViolation`.

No provider or verifier reads `CompilerSession`, an ambient root, an interner
container, a session vector, a handleful Binder graph, a global diagnostic
engine, or an enumeration of database keys.

### Arena-Owned Typed Identity Interner

`SemanticContextCapabilityArena` owns one
`IdentityInternerSet` for its complete refcounted lifetime. The set
contains one typed interner for each global handle:

- `CompilationUnitIdentity -> CompilationUnitId`;
- `CrateKey -> CrateId`;
- `SourceFileKey -> SourceFileId`;
- `ModuleKey -> ModuleId`;
- `DefinitionKey + DefinitionIdentityRecord -> DefId`;
- `ImplKey + ImplIdentityRecord -> ImplId`;
- `GenericParameterKey + GenericParameterIdentityRecord -> GenericParameterId`;
  and
- `CallableParameterKey + CallableParameterIdentityRecord -> CallableParameterId`.

The first four complete keys are their authority records. The last four retain
both digest key and complete record. A typed entry is:

```text
CanonicalIdentityEntry<Key, Record> {
  key: Key,
  record: Record,
  handle: ContextHandle<Tag>,
}
```

`intern(key, record)` validates the semantic-context brand, canonical codec,
`compute(record) == key` when the key is digest-derived, and byte equality of
both key and complete record. Equal key and equal record returns the existing
handle. Equal key and unequal record returns
`IdentityInternerFailure::CanonicalCollision`. Otherwise the exclusive lock
appends one owned entry and issues the next checked slot.
`AllocationFailure`, `SlotOverflow`, `ForeignBrand`, `MalformedRecord`, and
`CanonicalCollision` are the remaining closed failure alternatives.

Reverse lookup requires the same brand and returns the complete immutable key
and record. Entries are never deleted, reused, reordered, serialized, or used
as active-membership authority. Interner locking coalesces concurrent equal
admission.

`ImplOccurrenceId`, `ScopeId`, `OwnerLocalBindingId`, and anonymous or label
handles are module-local materialization handles. They never enter this set.

#### Handle Equality Boundary

Numeric slots and branded handles are arena-local and demand-order-dependent.
Within one `SemanticContextCapabilityArena`, an equal typed key and byte-equal
complete authority record coalesce to the identical handle, including
concurrent demand; repeated demand returns that handle; an equal digest key
with an unequal complete record returns `CanonicalCollision` without
allocation; and inactive current membership fails before interner lookup even
when an older surviving lease can still reverse-resolve its prior handle.

Across distinct arenas or fresh runs with different demand orders, raw handles
and numeric slots are never compared. Determinism compares complete authority
encodings, stable query bytes, typed graph witnesses, provider execution sets,
and coalescing counts. Each individual arena separately proves repeated and
concurrent equal-demand handle identity.

### Typed Active-Membership Matrix

`CapabilityQueryContext::materializeActive<Key>(key, authority)` is available
only to descriptors with an explicit
`ActiveMaterializerPermission<Descriptor, Key, MembershipDescriptor>`. The
permission row binds the exact membership descriptor at compile time. The
operation first validates the inherited RFC 0028 final admission. It then
derives the global key, demands the exact membership descriptor through the
tracked context, records that dependency, returns deterministic absence for
`Inactive` without interner access, compares the complete active authority,
validates context roots, owner and occurrence authority, and only then calls
the typed arena interner. An existing interner entry bypasses none of these
steps. Providers and verifiers independently reconstruct membership keys and
expected authority.

Every membership descriptor admitted by this operation declares its exact
`GlobalKey` and complete `Record`, plus
`projectGlobalKey(contextualKey)` and
`sameAuthority(leftRecord, rightRecord)` and
`validateAuthority(contextualKey, globalKey, record)`. `sameAuthority`
compares complete canonical record bytes; the generic query runtime does not
assume an identity record provides `operator==` or `operator!=`. The
projection returns no value for a malformed routed key. Validation checks the
complete contextual root, routed owner, full authority record, and occurrence
coverage applicable to that descriptor. `materializeActive` first requires
`sameAuthority(actualRecord, expectedRecord)`, then validates the actual
record. It has no descriptor-name switch, wildcard trait, record truncation,
or inactive-key interner lookup.

Final-sealed capability providers also require two immutable properties of
their owning query frame that are not semantic inputs: the current
`DatabaseRevision` and the arena-owned resource interface used by typed
materialization. `CapabilityQueryContext` therefore exposes exactly
`snapshotRevision()` and
`semanticContextResources<Resource>()`. The latter returns
`Maybe<const Resource&>` after one checked downcast from the arena's single
`SemanticContextCapabilityResources` owner to the descriptor's statically
named resource interface. Each `ActiveMaterialization` specialization names
its exact `Resource` type. `materializeActive` maps a missing or foreign
resource to `QueryRuntimeFailure::InvariantViolation` before interner access;
it never selects a fallback resource, session registry, ambient singleton, or
second interner. Interning through the const resource view is logical-const:
the append-only concurrent interner may allocate a handle, but the semantic
key-to-handle relation and resource identity cannot change.

M1 defines `ModuleGraphIdentityMaterializationResources` in
`materialized-module-graph-query.h`. The interface exposes the one semantic
context brand, typed intern operations, and typed reverse lookups for only
`CompilationUnitIdentity`, `CrateKey`, `SourceFileKey`, and `ModuleKey`.
`MaterializeModuleGraph` uses that interface both to issue admitted handles
and to reverse-expand every candidate edge during independent verification.
The production `CompilerSession` resource owner implements the interface when
the session root is migrated; T1's query-runtime fixtures implement the same
interface before the descriptor is registered. The interface owns no
interner, memo, lease, snapshot, or session reference.

`binder::ModuleGraphRevision` remains the only graph-revision value in the
repository. T1 replaces its private friend-only construction with
`fromCanonicalDigest(const Sha256Digest&)`, migrates every builder, verifier,
fixture, and M1 caller to that constructor, and removes construction-only
friends. The existing digest and the independently recomputed digest remain
byte-equal authorities; M1 does not declare a second revision type.
`ContextFingerprint` likewise gains
`fromCanonicalDigest(const Sha256Digest&)` for exact witness decoding. Both
factories accept an already decoded fixed-width digest and create only the
current immutable value; neither is a compatibility adapter, alternate
computation path, authority bypass, or versioned representation.

```text
ActiveCompilationUnitMembership {
  unit: CompilationUnitIdentity,
  activeCrates: CanonicalNonEmptySequence<CrateKey>,
}

ActiveImplementationMembershipRecord {
  queryKey: StableImplementationQueryKey,
  record: ImplIdentityRecord,
  authorityOccurrence: StableImplementationOccurrenceQueryKey,
  equalOccurrences:
      CanonicalNonEmptySequence<StableImplementationOccurrenceQueryKey>,
}

ImplementationGenericAuthority {
  implementation: StableImplementationQueryKey,
  authorityOccurrence: StableImplementationOccurrenceQueryKey,
  equalOccurrences:
      CanonicalNonEmptySequence<StableImplementationOccurrenceQueryKey>,
}

ActiveGenericParameterMembership {
  queryKey: StableGenericParameterQueryKey,
  record: GenericParameterIdentityRecord,
  owner:
      DefinitionOwner {
        definition: StableDefinitionQueryKey,
        headerSite: IdentitySyntaxSiteKey,
      } // 0x01
    | ImplementationOwner {
        authority: ImplementationGenericAuthority,
      } // 0x02,
  ordinal: uint32,
}

ActiveCallableParameterMembershipRecord {
  queryKey: StableCallableParameterQueryKey,
  record: CallableParameterIdentityRecord,
  owner: StableDefinitionQueryKey,
  headerSite: IdentitySyntaxSiteKey,
  position: CallableParameterPosition,
  name: Optional<DeclaredDefinitionName>,
  receiverLegal: bool,
}

CompleteRootIdentityReadiness {
  contextRoots: CompilationRootSetQueryKey,
  definitionAuthorityDigest: Sha256Digest,
  implementationAuthorityDigest: Sha256Digest,
  genericParameterAuthorityDigest: Sha256Digest,
  callableParameterAuthorityDigest: Sha256Digest,
}

ActiveMembershipResult<Record> =
    Active {
      record: Record,
    } // 0x01
  | Inactive // 0x02
```

`CompleteRootIdentityReadiness` uses domain
`zom.binder.complete-root-identity-readiness`; each digest is recomputed from
the complete sorted canonical key-and-record sequence installed in the same
transaction. The record is valid only when all four sequences cover the
complete active module set. An empty authority class uses the SHA-256 of its
domain, one zero byte, and a zero count; absence is not readiness.

The authority transaction installs one exact input for every entry in each
authority sequence. `ActiveDefinitionAuthorityInput`,
`ActiveImplementationAuthorityInput`,
`ActiveGenericParameterAuthorityInput`, and
`ActiveCallableParameterAuthorityInput` use the contextual key and complete
record types shown in the query catalog. Their structural input verifiers
require the routed module to agree with the record, recompute the stable query
key from the complete identity record, and reject an unequal owner, occurrence
set, ordinal, parameter position, or receiver classification before commit.
These input verifiers do not demand semantic queries. The transaction verifier
additionally requires every contextual key to use `payload.contextRoots`,
proves canonical uniqueness and complete active-module coverage, and
independently recomputes all four authority digests from the complete four
sequences. `CompleteRootIdentityReadinessVerifier` verifies only the input
key/value context-root binding and the readiness record's structural codec
invariants; it does not read authority inputs or recompute their digests.
`CompleteRootIdentityReadinessInput` is installed atomically only after the
transaction verifier proves that its four retained digests equal the four
recomputed sequence digests.

Every membership descriptor returns the corresponding
`ActiveMembershipResult<Record>`. Its literal value domain is the descriptor
domain plus `-value`; `Active` contains the complete authority record and
`Inactive` contains no payload. `ProviderRejected` and
`InvariantViolation` remain query-runtime failures and are not wire
alternatives.

| Key | Required authority argument | Exact tracked membership |
|---|---|---|
| `CompilationUnitIdentity` | `ContextualCompilationUnitKey` | exact `ActiveCompilationUnitMembership`: the complete non-empty canonical subset of `ActiveCrates(contextRoots)` whose `crate.unit` equals `unit`; multiple unequal projected-core crates may share one `Toolchain(Core)` unit |
| `CrateKey` | `ContextualCrateKey` | exact key occurs once in `ActiveCrates(contextRoots)` and its unit passes the prior row |
| `SourceFileKey` | `ContextualSourceKey` | exact key occurs once in `ActiveSources(crate)` and its crate passes the prior row |
| `ModuleKey` | `ContextualModuleKey` | exact key occurs once in `ActiveModules(crate)` and its crate passes the prior row |
| `DefinitionKey` | `ContextualDefinitionKey` plus complete record | exact `ActiveDefinitionAuthorityInput` record and exact owning `NamedDefinitionInventory` membership; conditional readiness only on absent or contradictory authority |
| `ImplKey` | `ContextualImplementationKey` plus complete record | exact `ActiveImplementationAuthorityInput` record, owning `NamedImplementationInventory`, and complete canonical implementation-occurrence authority set; conditional complete-root readiness only on absent or contradictory authority |
| `GenericParameterKey` | `ContextualGenericParameterKey` plus complete record and header owner | exact `ActiveGenericParameterAuthorityInput` record followed by the exact active definition or implementation owner and its authority header set, including owner sum, ordinal, record coverage, and active global owner |
| `CallableParameterKey` | `ContextualCallableParameterKey` plus complete record and definition owner | exact `ActiveCallableParameterAuthorityInput` record followed by the exact active definition owner and matching definition header, including position, receiver legality, record coverage, and active definition owner |

`ActiveImplementationMembership`,
`ActiveGenericParameterMembership`, and
`ActiveCallableParameterMembership` are retained `Semantic` narrow
projections. Their provider and verifier first probe the corresponding exact
authority input, then read only the owner and inventory/header named by that
record. They read the same complete-root readiness input used by the
definition authority transaction only when the authority input is absent or
structurally contradictory. Positive exact membership does not read
readiness. They return complete records, not booleans, and no provider scans a
module to discover a parameter owner.

For an implementation-owned generic parameter, provider and verifier
independently read the exact named-implementation inventory, derive the
complete equal-occurrence set, select the RFC 0018 authority occurrence by its
canonical rule, read that exact header and every equal occurrence header, and
require byte-equal parameter key, complete record, owner, name, and ordinal in
every occurrence. The provider cannot select one matching occurrence from the
candidate, and a shared implementation parameter cannot be authorized by an
occurrence-specific partial read.

An absent key with absent readiness returns `ProviderRejected`. An absent key
with present readiness is semantic absence. A present unequal record or
foreign owner is `InvariantViolation`. The membership dependency is recorded
before interning. An interner hit never bypasses membership.

### Materialized Capability Schemas

```text
StableMaterializedDependencyWitness {
  requester: ModuleKey,
  request: ModuleResolutionKey,
  dependency: ModuleKey,
}

MaterializedIdentityEntry<Key, Record, Handle> {
  key: Key,
  record: Record,
  handle: Handle,
}

MaterializedModuleDependencyEdge {
  requester: ModuleId,
  request: ModuleResolutionKey,
  dependency: ModuleId,
}

MaterializedBoundResolution {
  node: NodeId,
  scope: ScopeId,
  namespace: Namespace,
  target: BindingTarget,
  canonicalTarget: BindingTarget,
  origin: BindingOrigin,
}

MaterializedFailedLookupFact {
  node: NodeId,
  namespace: Namespace,
  name: DeclaredDefinitionName,
  outcome: StableFailedLookupOutcome,
}

MaterializedDefinitionEntry {
  node: NodeId,
  site: DefinitionSite,
  definition: DefId,
  key: DefinitionKey,
  record: DefinitionIdentityRecord,
  bindingName: Optional<DeclaredDefinitionName>,
  source: SourceSpan,
}

MaterializedGenericParameterEntry {
  node: NodeId,
  site: DefinitionSite,
  parameter: GenericParameterId,
  key: GenericParameterKey,
  record: GenericParameterIdentityRecord,
  bindingName: DeclaredDefinitionName,
  source: SourceSpan,
}

MaterializedCallableParameterEntry {
  node: NodeId,
  site: DefinitionSite,
  parameter: CallableParameterId,
  key: CallableParameterKey,
  record: CallableParameterIdentityRecord,
  bindingName: Optional<DeclaredDefinitionName>,
  source: SourceSpan,
}

MaterializedOwnerLocalBindingEntry {
  node: NodeId,
  site: DefinitionSite,
  binding: OwnerLocalBindingId,
  key: OwnerLocalBindingKey,
  source: SourceSpan,
}

MaterializedAnonymousEntityEntry {
  node: NodeId,
  site: DefinitionSite,
  entity: AnonymousOwnerLocalId,
  key: AnonymousOwnerLocalKey,
  source: SourceSpan,
}

MaterializedImplAuthorityEntry {
  implementation: ImplId,
  key: ImplKey,
  record: ImplIdentityRecord,
}

MaterializedImplOccurrenceEntry {
  occurrence: ImplOccurrenceId,
  key: ImplSourceOccurrenceKey,
  authority: ImplId,
  node: NodeId,
  source: SourceSpan,
}

ImmutableDefinitionInventory {
  semanticContext: SemanticContextBrand,
  module: ModuleId,
  moduleNode: NodeId,
  definitions: CanonicalSequence<MaterializedDefinitionEntry>,
  genericParameters: CanonicalSequence<MaterializedGenericParameterEntry>,
  callableParameters: CanonicalSequence<MaterializedCallableParameterEntry>,
  ownerLocalBindings:
      CanonicalSequence<MaterializedOwnerLocalBindingEntry>,
  anonymousEntities: CanonicalSequence<MaterializedAnonymousEntityEntry>,
  implementationAuthorities:
      CanonicalSequence<MaterializedImplAuthorityEntry>,
  implementationOccurrences:
      CanonicalSequence<MaterializedImplOccurrenceEntry>,
}

MaterializedModuleGraphWitness {
  contextRoots: CompilationRootSetQueryKey,
  fingerprint: ContextFingerprint,
  graph: ModuleGraphRecord,
  scc: ModuleGraphSccRecord,
  requestEdges: CanonicalSequence<StableMaterializedDependencyWitness>,
  graphRevision: ModuleGraphRevision,
}

MaterializedModuleGraph {
  context: SemanticContextBrand,
  revision: DatabaseRevision,
  witness: MaterializedModuleGraphWitness,
  units:
      CanonicalSequence<MaterializedIdentityEntry<
          CompilationUnitIdentity, CompilationUnitIdentity,
          CompilationUnitId>>,
  crates:
      CanonicalSequence<MaterializedIdentityEntry<
          CrateKey, CrateKey, CrateId>>,
  sources:
      CanonicalSequence<MaterializedIdentityEntry<
          SourceFileKey, SourceFileKey, SourceFileId>>,
  modules:
      CanonicalSequence<MaterializedIdentityEntry<
          ModuleKey, ModuleKey, ModuleId>>,
  requestEdges: CanonicalSequence<MaterializedModuleDependencyEdge>,
}

MaterializedModuleSkeleton {
  context: SemanticContextBrand,
  contextRoots: CompilationRootSetQueryKey,
  revision: DatabaseRevision,
  fingerprint: ContextFingerprint,
  stableWitness: BoundModuleSkeleton,
  module: ModuleId,
  definitions:
      CanonicalSequence<MaterializedIdentityEntry<
          DefinitionKey, DefinitionIdentityRecord, DefId>>,
  implementations:
      CanonicalSequence<MaterializedIdentityEntry<
          ImplKey, ImplIdentityRecord, ImplId>>,
  genericParameterIdentities:
      CanonicalSequence<MaterializedIdentityEntry<
          GenericParameterKey, GenericParameterIdentityRecord,
          GenericParameterId>>,
  callableParameterIdentities:
      CanonicalSequence<MaterializedIdentityEntry<
          CallableParameterKey, CallableParameterIdentityRecord,
          CallableParameterId>>,
  definitionFacts: CanonicalSequence<DefinitionFact>,
  implementationFacts: CanonicalSequence<ImplBindingFact>,
  scopes: CanonicalSequence<ScopeRecord>,
  nodeScopes: CanonicalSequence<NodeScopeFact>,
  moduleAliases: CanonicalSequence<ModuleAliasBindingFact>,
  imports: CanonicalSequence<ImportBindingFact>,
  localExports: CanonicalSequence<LocalExportFact>,
  genericParameters: CanonicalSequence<GenericParameterFact>,
  callableParameters: CanonicalSequence<CallableParameterFact>,
  failedLookups: CanonicalSequence<MaterializedFailedLookupFact>,
  bindingSurface: VerifiedExportSurface,
}

MaterializedOwnerBody {
  context: SemanticContextBrand,
  contextRoots: CompilationRootSetQueryKey,
  revision: DatabaseRevision,
  fingerprint: ContextFingerprint,
  stableWitness: BoundOwnerBody,
  owner: ContextualBodyOwnerKey,
  nodeScopes: CanonicalSequence<NodeScopeFact>,
  nodeBindings: CanonicalSequence<MaterializedBoundResolution>,
  selfTypes: CanonicalSequence<BoundSelfType>,
  thisBindings: CanonicalSequence<BoundThis>,
  ownerLocalBindings: CanonicalSequence<OwnerLocalBindingFact>,
  deferredMembers: CanonicalSequence<DeferredMemberFact>,
  labels: CanonicalSequence<LabelFact>,
  controlTransfers: CanonicalSequence<ControlTransferFact>,
  shadowTargets: CanonicalSequence<ShadowTargetFact>,
  closureFreeVariables: CanonicalSequence<ClosureFreeVariableFact>,
  explicitClosureCaptures: CanonicalSequence<ExplicitClosureCaptureFact>,
  failedLookups: CanonicalSequence<MaterializedFailedLookupFact>,
}

ImmutableBindingMetadata {
  semanticContext: SemanticContextBrand,
  module: ModuleId,
  nodeScopes: CanonicalSequence<NodeScopeFact>,
  nodeBindings: CanonicalSequence<MaterializedBoundResolution>,
  selfTypes: CanonicalSequence<BoundSelfType>,
  thisBindings: CanonicalSequence<BoundThis>,
  definitions: CanonicalSequence<DefinitionFact>,
  implementations: CanonicalSequence<ImplBindingFact>,
  scopes: CanonicalSequence<ScopeRecord>,
  moduleAliases: CanonicalSequence<ModuleAliasBindingFact>,
  imports: CanonicalSequence<ImportBindingFact>,
  localExports: CanonicalSequence<LocalExportFact>,
  deferredMembers: CanonicalSequence<DeferredMemberFact>,
  labels: CanonicalSequence<LabelFact>,
  controlTransfers: CanonicalSequence<ControlTransferFact>,
  shadowTargets: CanonicalSequence<ShadowTargetFact>,
  closureFreeVariables: CanonicalSequence<ClosureFreeVariableFact>,
  explicitClosureCaptures: CanonicalSequence<ExplicitClosureCaptureFact>,
  genericParameters: CanonicalSequence<GenericParameterFact>,
  callableParameters: CanonicalSequence<CallableParameterFact>,
  ownerLocalBindings: CanonicalSequence<OwnerLocalBindingFact>,
  failedLookups: CanonicalSequence<MaterializedFailedLookupFact>,
}

VerifiedBoundModule {
  context: SemanticContextBrand,
  contextRoots: CompilationRootSetQueryKey,
  revision: DatabaseRevision,
  fingerprint: ContextFingerprint,
  compilationUnit: CompilationUnitId,
  crate: CrateId,
  module: ModuleId,
  graph: BorrowedCapability<MaterializedModuleGraph>,
  parsedModule: BorrowedCapability<VerifiedParsedModule>,
  skeleton: BorrowedCapability<MaterializedModuleSkeleton>,
  ownerBodies: CanonicalSequence<BorrowedCapability<MaterializedOwnerBody>>,
  dependencySurfaces:
      CanonicalSequence<BorrowedCapability<VerifiedExportSurface>>,
  preludeSurface:
      Optional<BorrowedCapability<VerifiedExportSurface>>,
  resolvedImports: CanonicalSequence<ResolvedImportEdge>,
  resolvedModuleAliases: CanonicalSequence<ResolvedModuleAlias>,
  definitions: ImmutableDefinitionInventory,
  bindings: ImmutableBindingMetadata,
  bindingSurface: VerifiedExportSurface,
}
```

`StableMaterializedDependencyWitness` uses domain
`zom.materialized-module-dependency-witness`, one zero byte, then framed
complete requester, request, and dependency encodings in that order. Records
sort by complete bytes and reject duplicates.

`MaterializedModuleGraphWitness` uses domain
`zom.materialized-module-graph-witness`, one zero byte, then the framed
complete `CompilationRootSetQueryKey`, the 32-byte semantic-context
fingerprint digest, framed complete graph and SCC value encodings,
`uint64be(requestEdgeCount)` followed by each framed dependency witness, and
the 32-byte graph-revision digest. Decoding requires exact consumption,
canonical unique request edges, graph/SCC membership closure, SCC graph-digest
equality, and independent recomputation of `ModuleGraphRevision`.

Provider and verifier independently execute this canonical total demand order:

1. demand `CompleteCompilationContextAuthority(contextRoots)` and prove that
   the descriptor key is the complete root set;
2. demand the canonical package request, projected core distribution, and all
   fingerprint inputs in their schema order;
3. demand `ActiveCrates`, then visit crates in complete crate-key order and,
   for each crate, demand `ActiveSources(crate)` followed by
   `ActiveModules(crate)`; the nested rejection-order tuple is
   `(complete crate key bytes, family ordinal)` where source family is zero
   and module family is one;
4. demand graph and SCC, then prove closure, SCC graph-digest equality, and
   acyclicity;
5. visit modules in complete module-key order and demand selected source,
   dependency sites, and stable requests in that family order; then demand
   every exact resolution in complete request-key order; collect configured
   prelude input keys reached by those resolutions, deduplicate by complete
   prelude-input key bytes, sort those unique bytes, and demand the configured
   preludes after all resolutions for that module;
6. deduplicate selected sources by complete source-key bytes and demand the
   final `ParseSource` capability in that byte order;
7. demand the retained `ModuleDependencyProvenance` capability for every
   reached module in complete module-key order;
8. materialize units, crates, sources, and modules in that domain order and
   complete key-byte order through their exact membership descriptors; and
9. construct the stable request edges, graph projection, fingerprint,
   revision, stable witness, reverse-expanded handle edges, and candidate.

Provider and verifier may execute independent reads concurrently only when
result collection and rejection selection preserve this order. For every
child result the precedence is runtime rejection, key rejection, source
rejection, then value. Across multiple children, the first eligible rejection
in the canonical order above wins. A child runtime failure retains its exact
`QueryRuntimeFailure`; final-admission failures occur before provider
execution; provenance and parse source rejection and provenance key rejection
are forwarded unchanged. `IdentityInternerFailure::AllocationFailure` and
`IdentityInternerFailure::SlotOverflow` map to `AllocationFailure`.
`IdentityInternerFailure::ForeignBrand`,
`IdentityInternerFailure::MalformedRecord`, and
`IdentityInternerFailure::CanonicalCollision` map to `InvariantViolation`.
Missing tracked reads, inactive membership, authority disagreement, malformed
provenance, graph or SCC closure disagreement, a cyclic SCC observed after
final admission, reverse-lookup disagreement, and revision disagreement also
map to `InvariantViolation`. Cancellation is `Cancelled`; an evaluator
dependency cycle is `Cycle`; the cyclic-SCC case never uses `Cycle`.
Candidate-verifier disagreement is `VerifierRejected`.

The verifier rebuilds the witness without provider graph, edge, ordering,
candidate, or revision helpers and requires complete byte equality. Every
materialized handle edge reverse-expands to the matching stable witness edge.
Context roots, fingerprint, graph, SCC, and graph revision are projected only
from `witness`; no duplicate authority field exists.

`MaterializedIdentityEntry<Key, Record, Handle>` carries exactly one typed
handle plus its complete key and authority record. Definition and implementation provenance comes from
the exact current site capability. Every referenced path has exactly one
current selected-source provenance entry.

`ImmutableDefinitionInventory` is an owned index, not an identity authority.
Provider and verifier independently derive it from the complete materialized
skeleton, every materialized owner body, and exact current provenance. It
provides total handle-to-key-and-record lookup for the four global named
domains, total `NodeId` lookup for definitions, parameters, owner locals,
anonymous entities, and implementation occurrences, and
occurrence-to-authority lookup. Every result is a reference into the owning
`VerifiedBoundModule`; no accessor constructs or clones an entry.

`MaterializedBoundResolution` represents only a successful binding.
`MaterializedFailedLookupFact` retains the syntax occurrence and stable
outcome without a diagnostic reference. The old failed binding alternative is
not admitted into `MaterializedOwnerBody` or `ImmutableBindingMetadata`;
diagnostics remain exclusively in `ModuleDiagnosticFacts`.

Scope allocation follows `ModuleBindingAllocationPlan`. Skeleton scopes use
their depth then complete stable owner bytes. Body scopes and local entities
use the exact assigned range and complete fact bytes. Implementation
occurrence handles use complete occurrence bytes. Provider task order cannot
affect any slot.

`BorrowedCapability<T>` is a lifetime-bound reference whose owning memo is
automatically retained in the parent memo dependency set. It is not a public
standalone type and cannot be constructed without the recorded child lease.
The capability objects own all other arrays and immutable metadata. They
borrow no provider temporary, session vector, registry object, or caller
container.

The independent verifier for each capability re-demands dependencies from its
own key, reconstructs active membership, materialized handle expansion,
provenance coverage, allocation ranges, scope ancestry, binding targets,
diagnostics, export surfaces, and stable witness. It shares only canonical
scalar codecs and closed enum declarations with the provider.

`VerifyBoundModule` additionally proves exact owner coverage, one materialized
body per canonical owner, no cross-module handle, complete aggregate fact
coverage, and byte-equal export surface between skeleton and aggregate
metadata. Only its verified capability may reach downstream consumers.

`CheckerBoundModuleView` owns one `VerifiedBoundModuleLease` and exposes
exactly `semanticContext`, `compilationUnit`, `crate`, `module`,
`semanticFingerprint`, `tree`, `parsedModule`, `definitions`,
`dependencySurfaces`, `preludeSurface`, `resolvedImports`,
`resolvedModuleAliases`, `bindings`, and `bindingSurface`. Every returned
reference is lifetime-bound to the view. The current
`VerifiedBoundModuleInput` type is deleted after every consumer uses this
lease-owning view.

The `definitions` accessor returns
`const ImmutableDefinitionInventory&`. It replaces the complete live lookup
surface: handle-to-key-and-record, node-to-definition/parameter/local/
anonymous/occurrence, and occurrence-to-implementation-authority.

### Module Graph And Session Cutover

Authority staging is handle-free:

```text
CanonicalInputEntry<Key, Value> {
  key: Key,
  value: Value,
}

CanonicalCompilationRootRecord {
  package: PackageKey,
  targetKind: CrateTargetKind,
  targetName: TargetName,
  editionYear: uint32,
  requiresBuildScript: bool,
  sourcePath: CanonicalRelativePath,
}

CanonicalTargetSelectionRecord {
  registryRevision: Sha256Digest,
  profile: RegisteredTargetProfileName,
  semanticProjection: CanonicalTargetSpecificationKey,
  panicStrategy: PackagePanicStrategy,
}

CanonicalLanguageOptionsRecord {
  useUnicode: bool,
  allowDollarIdentifiers: bool,
  supportRegexLiterals: bool,
}

CanonicalPackageCompilationRequest {
  roots: CanonicalNonEmptySequence<CanonicalCompilationRootRecord>,
  hostTarget: CanonicalTargetSelectionRecord,
  target: CanonicalTargetSelectionRecord,
  languageOptions: CanonicalLanguageOptionsRecord,
  lockMode: PackageLockMode,
}

CompleteCompilationContextAuthority {
  contextRoots: CompilationRootSetQueryKey,
  packageRequest: CanonicalPackageCompilationRequest,
  packageRootSet: PackageRootSetKey,
  packageGraph: CanonicalPackageGraph,
  userRootCrates: CanonicalNonEmptySequence<CrateKey>,
  projectedCoreCrates: CanonicalNonEmptySequence<CrateKey>,
  expectedRootCrates: CanonicalNonEmptySequence<CrateKey>,
  completeCrates: CanonicalNonEmptySequence<CrateKey>,
  compilationOptions:
      CanonicalNonEmptySequence<
          CanonicalInputEntry<CrateKey, CanonicalCompilationOptions>>,
  moduleSearchRoots:
      CanonicalNonEmptySequence<
          CanonicalInputEntry<CrateKey, CanonicalModuleSearchRoots>>,
  coreDistributionRecord: CoreDistributionRecord,
  coreDistributionDigest: Sha256Digest,
}

VerifiedCoreDistributionInputPayload {
  contextRoots: CompilationRootSetQueryKey,
  distributionRecord: CoreDistributionRecord,
  distributionDigest: Sha256Digest,
  policyTemplate: CoreStandardMarkerPolicyTemplate,
  projectedCoreSources:
      CanonicalNonEmptySequence<
          CanonicalInputEntry<StableSourceQueryKey, CanonicalSourceSnapshot>>,
  compilationOptions:
      CanonicalNonEmptySequence<
          CanonicalInputEntry<CrateKey, CanonicalCompilationOptions>>,
  moduleSearchRoots:
      CanonicalNonEmptySequence<
          CanonicalInputEntry<CrateKey, CanonicalModuleSearchRoots>>,
  projectedCoreInventory: CanonicalNonEmptySequence<CrateKey>,
  contextAuthority: CompleteCompilationContextAuthority,
}

VerifiedCoreDistributionInputTransaction {
  expectedPreviousRevision: DatabaseRevision,
  verifiedDistribution: VerifiedCoreDistribution,
  verifiedPackageRequest:
      SynchronousBorrow<VerifiedPackageCompilationRequest>,
  payload: VerifiedCoreDistributionInputPayload,
}

VerifiedModuleGraphInputPayload {
  contextRoots: CompilationRootSetQueryKey,
  projectedCoreCrates: CanonicalNonEmptySequence<CrateKey>,
  selectedModuleCatalogs: CanonicalNonEmptySequence<SelectedModuleCatalog>,
  dependencySiteSets:
      CanonicalSequence<DetachedModuleDependencySiteSet>,
  requesterAncestries: CanonicalSequence<RequesterModuleAncestry>,
  catalogBuckets: CanonicalSequence<CanonicalModuleCatalogBucket>,
  searchRoots: CanonicalSequence<CanonicalModuleSearchRoots>,
  dependencyAliases: CanonicalSequence<ConfiguredDependencyAlias>,
  configuredPreludes: CanonicalSequence<ConfiguredCratePrelude>,
}

VerifiedModuleGraphInputTransaction {
  expectedPreviousRevision: DatabaseRevision,
  payload: VerifiedModuleGraphInputPayload,
}

ContextualIdentityAuthorityInputPayload {
  contextRoots: CompilationRootSetQueryKey,
  definitionAuthorities:
      CanonicalSequence<CanonicalInputEntry<
          ContextualDefinitionKey, DefinitionIdentityRecord>>,
  implementationAuthorities:
      CanonicalSequence<CanonicalInputEntry<
          ContextualImplementationKey,
          ActiveImplementationMembershipRecord>>,
  genericParameterAuthorities:
      CanonicalSequence<CanonicalInputEntry<
          ContextualGenericParameterKey,
          ActiveGenericParameterMembership>>,
  callableParameterAuthorities:
      CanonicalSequence<CanonicalInputEntry<
          ContextualCallableParameterKey,
          ActiveCallableParameterMembershipRecord>>,
  completeRootReadiness: CompleteRootIdentityReadiness,
}

ContextualIdentityAuthorityInputTransaction {
  expectedPreviousRevision: DatabaseRevision,
  payload: ContextualIdentityAuthorityInputPayload,
}
```

The package request record is an independent canonical value, not a
`VerifiedPackageCompilationRequest`, clone, reference, pointer, or ownership
handle. The transaction producer projects it field by field through the
verified request getters. A separate verifier repeats the projection without
calling the producer helper, checks root ordering and uniqueness, compares
complete canonical bytes, and proves that the context authority's target,
language, lock, package-root, and package-graph facts agree with that record.
The four canonical package-request records use the literal domains above,
global bounds, exact-consumption codecs, and complete value equality. Q3 owns
those four production types, codecs, the verified-request projection, and its
independent verifier; completed Q3 is not reopened. Their canonical schema
rows use `(typeTask=Q3, codecTask=Q3, testTask=R30_13)`. Each complete record
uses `Bound(CanonicalPackageRecordBytes, UINT32_MAX, CompleteRecordBytes)`, and
the declaration order above is its exact zero-based field ordinal order.
The schema also declares `Bound(TargetProfileBytes, 255, NfcUtf8Bytes)`,
`FieldLimit(CanonicalTargetSelectionRecord.profile, TargetProfileBytes)`, and
`FieldLimit(CanonicalPackageCompilationRequest.roots,
CanonicalInputSequenceRecords)`.
RFC 0030 `R30-13` owns the comprehensive declared-mutation test in
`products/zomlang/tests/unittests/compiler/driver/package-compilation-request-test.cc`
and adds that existing test file to the atomic `R29-12AB` allowlist.

`CompleteCompilationContextAuthority` uses domain
`zom.input.complete-compilation-context-authority`. Its registered input
descriptor is `CompleteCompilationContextAuthorityInput`, keyed by the
complete `CompilationRootSetQueryKey`, with that exact value domain. It uses `Input`
durability, complete canonical value equality, one value per key and revision,
and no fallback. The value codec uses the field order above. Its independent
input verifier recomputes `expectedRootCrates` as the canonical union of user
and projected-core roots, recomputes `contextRoots`, validates every package
and crate edge in `packageGraph` against the canonical package request and its
independently repeated projection from the verified request,
recomputes `completeCrates` as the canonical union of the package graph crates
and projected-core crates, proves one options and search-root record per
complete crate, validates the core record and digest, and independently
recomputes every semantic-context fingerprint input. A partial root key,
missing projected core crate, extra root, unequal graph edge, or unequal
fingerprint input rejects the transaction.

I1A prepares `CanonicalInputEntry<Key, Value>`,
`CompleteCompilationContextAuthority`, its canonical codec, and the
independent live-authority projection verifier in
`products/zomlang/compiler/driver/module-graph-query-input.h` and
`products/zomlang/compiler/driver/module-graph-query-input.cc`. I2A through
I2G prepare the common result and codec surface, the four topology
memberships, complete-root readiness, and the four authority-backed identity
memberships; I2 is their review-only join. M1 prepares the graph and SCC
witnesses. The stable-binding schema keeps the input ownership tuple
`I1A/T1/I1A/I1A`; its producer provenance names the I1A `fromVerified`
construction boundary, while the provider column names the T1 transaction
that installs the verified value. The schema gate checks both the task tuple
and that producer identity. None of I1A, I2A through I2G, the I2 join, or M1
lands independently.

T1 is the sole atomic landing authority for the three prepared partitions. It
adds `CompleteCompilationContextAuthorityInput` only after the graph, SCC,
authority, readiness, and transaction-witness descriptors exist, implements
the static final-authority verifier against those snapshot reads, deletes both
test-only descriptors that shadowed the production fully qualified name,
installs the three input transactions, including all four contextual authority
inputs and the complete-root readiness input in the identity-authority
transaction, and publishes staging, final, and sealed snapshots. Generic
`QueryDatabase` phase-order, race, and irreversibility tests use only
`query::test::TestCompleteContextInput`, a `Frozen` complete-context test
descriptor with the unique domain `test.input.complete-context`. It occupies
test-inventory slot 59, immediately after the complete production prefix, and
does not enter any production library or schema. `QueryCapability` final-seal
integration and the driver session final-seal tests use the real production
descriptor with a valid production root key, complete authority, installed
read set, and final witness. No alias, verifier injection seam, or fallback
connects the generic test descriptor to production. This atomic
assembly prevents an intermediate production descriptor from accepting a
self-authenticated value before its complete final-seal read set exists.
The transaction landing edits the existing
`VerifiedCoreDistributionInputTransaction` in
`core-library-query-provider.{h,cc}` directly. That transaction accepts the
caller's exact expected previous revision, validates the complete canonical
payload before opening the database transaction, installs its transaction
witness in the same committed revision, and returns the closed
`InputCommitResult`. Its native provider test owns stale-revision,
payload-mutation, witness, and failure-atomicity coverage. No wrapper,
follow-up witness transaction, Boolean result, or current-revision lookup is
permitted.
T1 also migrates all three production mutation callers in
`compiler-session.cc`: the existing core-distribution transaction, the
existing module-structure transaction, and the
`activeDefinitionAuthority.refresh(...)` path, which is directly replaced by
the contextual-identity-authority transaction. All three pass their explicitly
retained previous revisions and consume `InputCommitResult`. That edit changes
only transaction construction, commit-result handling, and descriptor
registration required for a buildable cutover. T2A retains sole ownership of
the session input-state machine, named snapshots, semantic resource
implementation, and final capability root. No temporary overload, Boolean
adapter, or retained `refresh` mutation path remains between T1 and T2A.
Because I2 and M1 add production translation units, T1 also owns the exact
additive `products/zomlang/compiler/driver/CMakeLists.txt` rows for
`active-identity-membership-query.cc` and
`materialized-module-graph-query.cc`. The rows cannot land in a preparation
partition, and T1 cannot claim a buildable atomic closure without them. W2 may
later edit the same build file only for its post-T2C deletion and final wiring
scope; it does not own or defer these two T1 source rows.

T1 also owns the narrow `CapabilityQueryContext` accessor additions in
`products/zomlang/compiler/query/query-database.h`, the
`MaterializeModuleGraph` production inventory row in
`products/zomlang/compiler/query/query-descriptor-schema.def`, and the
corresponding test-inventory ordinal migration. The accessors, descriptor,
driver source rows, test resource, final-authority verifier, and session
publication sequence land together. No preparation partition may publish an
unregistered descriptor, a descriptor without its static resource contract,
or a resource accessor without a production consumer and mutation coverage.

The three transaction payload domains are, in order,
`zom.query.input-transaction.core-distribution`,
`zom.query.input-transaction.module-structure`, and
`zom.query.input-transaction.contextual-identity-authority`.
`expectedPreviousRevision`, `verifiedDistribution`, and
`verifiedPackageRequest` are process-local transaction controls and are not
part of a payload, codec, equality, or digest. The package-request borrow is
notation for a lifetime-bound synchronous function input, not a stored
repository type or canonical schema. It is
accepted only by the synchronous transaction constructor, remains valid
through independent verification and commit-or-reject, is never stored by the
transaction or database, and ends before the call returns. `CompilerSession`
owns the live verified request across that call. Before acquiring the input
lock, the independent verifier repeats the canonical package-request
projection from this exact live object without calling the producer projection
helper and requires byte equality with
`payload.contextAuthority.packageRequest`. It also requires the payload
distribution record, digest, policy, and source snapshots to be byte-equal
projections of `verifiedDistribution`; neither process-local source is encoded
or installed as semantic authority. Each payload encodes its domain, one zero
byte, then every field in declaration order using complete nested encodings. Its
digest is
`SHA256("zom.query.input-transaction-digest" || 0x00 ||
framed(transaction-domain) || framed(complete-payload-bytes))`. Transaction
decoders use the global bounds above, reject duplicates and trailing bytes,
and require exact consumption.

`CanonicalInputEntry<Key, Value>` is nested only: framed complete key bytes
followed by framed complete value bytes. Entry sequences sort by complete key
bytes and reject duplicate keys; the enclosing payload domain supplies domain
separation.

The three production transaction-witness descriptors use
`CompilationRootSetQueryKey`, `CanonicalInputPayloadDigest`, and `Frozen`
durability:

| Ordinal | Descriptor | Domain |
|---|---|---|
| 56 | `CoreDistributionTransactionWitnessInput` | `zom.query.core-distribution-transaction-witness` |
| 57 | `ModuleStructureTransactionWitnessInput` | `zom.query.module-structure-transaction-witness` |
| 58 | `ContextualIdentityAuthorityTransactionWitnessInput` | `zom.query.contextual-identity-authority-transaction-witness` |

Each transaction installs exactly its descriptor with its semantic inputs in
the same new revision. No transaction may write another transaction's
descriptor, overwrite an existing witness, or publish a witness through a
later revision. The final-authority verifier probes the three descriptors in
the table order and rejects absence, unequal roots, or a digest that disagrees
with the complete final witness. Static descriptor types provide the phase
partition; no phase enum, tagged key, runtime descriptor-name dispatch, or
aggregate witness input exists.

The query-descriptor generator requires those three exact production rows in
that order, with their declared input kind, domains, `Frozen` durability, key,
and value types. It rejects a missing, duplicate, reordered, renamed, mistyped,
or mutable witness row. The test tail must begin with the unique
`TestCompleteContextInput` at ordinal 59; every later test ordinal is
contiguous. The generator self-test mutates each witness field and the first
test ordinal independently.

Each transaction validates its complete canonical payload before acquiring the
database input lock, compares `expectedPreviousRevision`, and commits every
field in one new revision or commits nothing. A key may occur once; all nested
roots are byte-equal to `contextRoots`; every selected source and module is
covered by the complete context authority; and no transaction contains a
derived graph, SCC, Binder value, semantic handle, registry, capability, or
diagnostic. Provider and verifier code cannot observe a partially installed
transaction.

1. derive the complete user-package and projected-core crate inventory;
2. commit one `VerifiedCoreDistributionInputTransaction`, atomically installing
   the verified distribution, every projected-core `SourceSnapshot`,
   crate-keyed `CompilationOptions`, `ModuleSearchRoots`, projected-core
   inventory, and complete compilation-context authority;
3. acquire `preParseSnapshot`;
4. parse, validate module declarations, discover modules, resolve duplicates,
   and produce structural records only;
5. commit one `VerifiedModuleGraphInputTransaction`, atomically installing
   selected modules and sources, dependency sites, path buckets, requester
   ancestry, configured preludes, and every structural graph prerequisite, but
   no derived graph, SCC, semantic inventory, authority, readiness, or handle;
6. acquire `authorityStagingSnapshot`;
7. demand and independently verify active crates, sources, modules, dependency
   requests, resolutions, graph, SCC, named inventories, staging-safe headers,
   and handle-free Binder skeleton prerequisites;
8. reject every cyclic SCC;
9. commit one RFC 0020
   `ContextualIdentityAuthorityInputTransaction` containing the complete
   authority map and complete-root readiness;
10. acquire `finalCoreSnapshot`;
11. re-demand graph, SCC, authority, readiness, active membership, inventories,
    and staging-safe prerequisites and require byte equality with the staging
    witnesses;
12. call
    `QueryDatabase::sealInputs(finalCoreSnapshot, completeContextRoots,
    finalWitness)`; and
13. construct the matching
    `SealedQuerySnapshot<CompilationRootSetQueryKey, Sha256Digest>` and demand
    `MaterializeModuleGraph` and all later materializers only through it.

```text
SessionInputState =
    Open // 0x01
  | DistributionCommitted(DatabaseRevision) // 0x02
  | StructureCommitted(DatabaseRevision) // 0x03
  | AuthorityCommitted(DatabaseRevision) // 0x04
  | Sealed(FinalSnapshotSeal) // 0x05

FinalSnapshotSeal {
  database: QueryDatabaseIdentity,
  revision: DatabaseRevision,
  contextRoots: CompilationRootSetQueryKey,
  finalWitness: Sha256Digest,
}
```

`sealInputs` creates no revision and returns the move-only seal. The `Sealed`
transition is irreversible. Every later input commit fails with
`InputMutationAfterFinalSeal` and aborts the unpublished session. Every
final materializer requires inherited admission with the same database
identity, revision, canonical context-root key, and witness as the private
immutable admission. Absence or inequality fails before provider execution,
membership demand, memo lookup, interner access, or publication. Nested
capability demands inherit the same admission rather than consulting ambient
sealed state.

`finalWitness` is
`SHA256("zom.query.final-snapshot-witness" || 0x00 || framed
distribution-transaction-digest || framed structure-transaction-digest ||
framed authority-transaction-digest || framed complete-context-authority ||
framed graph-value || framed SCC-value || framed authority-map-value || framed
readiness-value)`. Provider and independent verifier re-demand the values in
that order and compute the digest through separate collection code. A mismatch
aborts the unpublished session.

The RFC 0026 session-owned handleful graph builder is replaced by
`MaterializeModuleGraph`. It materializes the same independently verified
stable graph, request edges, sources, context roots, fingerprint, and revision
inside a retained capability memo. No handle is required to install definition
authority.

`CompleteCompilationContextAuthority(contextRoots)` is the independently
verified handle-free canonical package request, projected-core sequence, expected
complete root set, and all fingerprint inputs. `MaterializeModuleGraph` reads
it; the canonical package-compilation request; complete projected-core and core
distribution inputs; all toolchain, package-edge, crate-edge, and source
content inputs used by the semantic-context fingerprint; `ActiveCrates` and
every reached `ActiveSources` and `ActiveModules`; graph and SCC; every selected
source; every dependency site and request; every exact module-resolution
result; every configured prelude selected by a prelude request; final-snapshot
parse leases for all selected sources; the retained final-sealed
`ModuleDependencyProvenance` capability for every reached module; and exact
unit, crate, source, and module active-membership projections.

Provider and verifier independently reconstruct the complete root set and
active module union, graph and acyclic SCC membership, full semantic-context
fingerprint, request-level edges and parse provenance, configured-prelude
edges, stable graph projection, typed handle expansion, and graph revision. A
candidate key never proves that it is the complete session root set. A legal
partial `CompilationRootSetQueryKey` may produce stable graph and SCC query
values but cannot satisfy `CompleteCompilationContextAuthority` and cannot be
materialized or published.

The final snapshot demands `VerifyBoundModule` in dependency-first stable SCC
order. Production does not call `runBinding()`. The cutover deletes:

- `SemanticIdentityRegistrySet` and every frozen registry or freeze method;
- `frozenInventoryInputs`;
- `frozenInventories`;
- `moduleBodyQueryBindings`;
- `namedItemQueryBindings`;
- `bindingInputs`;
- `bindingOutputs`;
- `boundModules`;
- session-owned parsed/provenance capability clones; and
- the session-owned handleful module-graph candidate and publication root.

`runBinding()` may exist only in native differential tests until the
differential inventory is complete, then is deleted from the repository.

### Lease Closure

Every revision-local capability memo retains:

- its snapshot capability arena;
- the semantic-context capability arena through that snapshot;
- every capability memo read by provider or verifier; and
- its complete immutable stable witness.

The semantic-context arena owns the brand issuer, typed identity interners, and
semantic type store. It owns no query memo, lookup table, flight, lease, or
verified bound module. The dependency graph therefore has no ownership cycle.

`VerifiedBoundModuleLease` is the move-only owning
`QueryCapabilityLease<const VerifiedBoundModule>`. Its explicit `retain()`
operation increments the existing memo reference count and returns another
owning lease to the same memo, key, revision, and capability. It never copies
or reconstructs the capability. A narrow downstream view is valid only when it
stores its own retained lease.

```text
VerifiedCheckedModule.boundModuleLease: VerifiedBoundModuleLease
VerifiedHirModule.boundModuleLease: VerifiedBoundModuleLease
VerifiedBuiltMir.boundModuleLease: VerifiedBoundModuleLease
VerifiedOwnershipEventOverlay.boundModuleLease: VerifiedBoundModuleLease
```

`CheckedModuleBuilder` consumes the final-snapshot lease returned by
`VerifyBoundModule`. Its independent verifier reopens the capability and
checks context roots, revision, fingerprint, module, graph and skeleton
witnesses, owner coverage, and every expanded handle before moving the lease
into `VerifiedCheckedModule`.

`HirBuilder` retains the checked-module lease; its verifier reopens the same
capability, checks all HIR identities and imported-interface lineage, and
moves the retained lease into `VerifiedHirModule`. `BuiltMirBuilder` retains
the HIR lease; its verifier checks module identity, graph witness, semantic
types, definitions, implementations, and HIR lineage before moving the lease
into `VerifiedBuiltMir`. `OwnershipEventOverlayBuilder` retains the Built MIR
lease; its verifier checks the exact Built MIR revision, module,
ownership-event coverage, and bound-module lineage before moving the lease
into `VerifiedOwnershipEventOverlay`.

A missing or released lease, foreign brand or roots, stale revision, unequal
graph witness, unequal module, or handle-expansion disagreement rejects before
dereferencing a handle. Each Pimpl declares `boundModuleLease` before every
field that depends on it, so reverse member destruction releases dependent
views before the lease. Retention is not a capability clone: production owns
one immutable memo capability and contains no detached copy, non-owning root,
session-vector reference, or deep-clone publication.

Session teardown releases session-held downstream leases, then the query
database and lookup tables, then its arena owner. An explicitly permitted
in-flight or caller lease may outlive all three session objects because its
memo retains the snapshot and semantic-context arenas. Reverse handle lookup,
brand validation, semantic types, and all borrowed child capabilities remain
valid until the last lease releases.

### Mechanical Synchronization On Acceptance

Acceptance of this RFC and synchronization of the following accepted RFCs are
one exact-hash transaction. No intermediate accepted state is permitted.

| Order | RFC | Mechanical replacement bound to the RFC 0027 proposal hash |
|---|---|---|
| 1 | RFC 0018 | add `StableDefinitionQueryKey`, `StableImplementationQueryKey`, `StableImplementationOccurrenceQueryKey`, `StableGenericParameterQueryKey`, and `StableCallableParameterQueryKey`; retain complete authority records and occurrence-specific scope ownership; delete `FrozenDefinitionInventoryView`, frozen-registry ownership, and final-frozen-state claims |
| 2 | RFC 0017 | make the semantic-context arena the sole interner owner; add literal descriptors, the eight typed permissions and memberships, result codecs, collision behavior, final-seal admission, memo retention, and surviving-lease teardown; delete database-owned interner lifetime and freeze authority |
| 3 | RFC 0020 | add complete-root compilation-unit, crate, source, module, definition, implementation, generic-parameter, and callable-parameter membership; add conditional readiness and the final-barrier transaction; delete ambient membership, fixed readiness, registry membership, and `UpstreamSourceRejected` |
| 4 | RFC 0019 | replace owner, scope, fact, header, result, allocation, complete-read, diagnostic, materialized-provenance, and Checker handoff schemas; delete `FrozenCallableParameterEntry`, `FrozenGenericParameterEntry`, non-contextual owner keys, `UpstreamSourceRejected`, and the overlapping failure enum |
| 5 | RFC 0026 | replace `SemanticIdentityRegistrySet`, `VerifiedModuleGraphBuilder`, and the handleful authority bridge with the typed graph witness and final-snapshot `MaterializeModuleGraph`; keep every staging transaction handle-free |
| 6 | RFC 0010 | make `VerifiedCheckedModule`, `VerifiedHirModule`, `VerifiedBuiltMir`, and `VerifiedOwnershipEventOverlay` own the exact retained `VerifiedBoundModuleLease`; delete non-owning bound-module references and Binder-to-IR failure conversions |
| 7 | RFC 0025 | synchronize contextual query shapes, arena/interner lifetime, graph and Binder capabilities, core bootstrap dependencies, session transaction order, deletion inventory, diagnostic ownership, and downstream leases; delete registry/session ownership and obsolete key shapes |
| 8 | routing governance | assign `products/zomlang/compiler/ownership/**` to `runtime-memory`; assign `scripts/check-english-only.py` and `scripts/check-spec-alignment.py` to `verification`; synchronize `.agents/subagents/manifest.yaml`, `.agents/subagents/README.md`, `.agents/subagents/task-router.md`, `.agents/subagents/module-system.md`, `.agents/subagents/runtime-memory.md`, `.agents/subagents/verification.md`, and `AGENTS.md`; delete the stale database-owned semantic-identity rule |

All seven RFC trackers record the exact RFC 0027 proposal hash and the same
acceptance transaction identifier. Existing acceptance hashes remain
historical records; the synchronized overlay is a new status-history entry.
The transaction is prepared as one tree change, validated as a whole, and
either accepted in full or returned without retaining any partial approval or
partial synchronization.

The synchronization gate searches live code, trackers' current-state and
implementation sections, and every affected RFC outside `Status History`.
The following exact stale authorities must have zero references after the
transaction:

| Source | Exact zero-reference inventory |
|---|---|
| RFC 0018 | `FrozenDefinitionInventoryView`; frozen-registry admission or final-frozen authority |
| RFC 0017 | database-owned semantic identity interner; materializer admission through registry membership |
| RFC 0020 | `UpstreamSourceRejected`; ambient definition membership; unconditional fixed readiness; registry membership |
| RFC 0019 | `FrozenCallableParameterEntry`; `FrozenGenericParameterEntry`; non-contextual owner-body materialization key; old Binder failure algebra; `BindingResolution::Failed` in materialized capability schemas |
| RFC 0026 | `SemanticIdentityRegistrySet`; `VerifiedModuleGraphBuilder`; frozen-registry expansion; handleful graph revision; session-owned graph publication root |
| RFC 0010 | non-owning bound-module references; Binder-to-IR failure conversion |
| RFC 0025 | registry- or session-owned interner; old non-contextual body key; session capability clones; old graph builder; old downstream lease shape |
| routing | database physically owns semantic identity; missing ownership-analysis owner |

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| Routing and manifest ownership | `.agents/subagents/**`; `AGENTS.md` | `task-router` |
| RFC governance | `docs/rfc/**`; `.agents/skills/rfc/**` | `rfc` |
| Query, identity, source, module graph, session, and module interface | `products/zomlang/compiler/identity/**`; `products/zomlang/compiler/query/**`; `products/zomlang/compiler/source/**`; `products/zomlang/compiler/binder/module-*`; `products/zomlang/compiler/driver/**`; `products/zomcore/Zom.toml` | `module-system` |
| Stable Binder facts, materializers, Checker, and semantic types | `products/zomlang/compiler/binder/**` excluding `binder/module-*`; `products/zomlang/compiler/checker/**` excluding `checker/checker-source-diagnostics.def`; `products/zomlang/compiler/type/**` | `binder-checker` |
| Diagnostic schemas and materialization | `products/zomlang/compiler/diagnostics/**`; `products/zomlang/compiler/checker/checker-source-diagnostics.def` | `error-system` |
| HIR, shared IR, MIR, LIR, backend, and build wiring | `products/zomlang/compiler/hir/**`; `products/zomlang/compiler/ir/**`; `products/zomlang/compiler/mir/**`; `products/zomlang/compiler/lir/**`; `products/zomlang/compiler/backend/**`; the `ir-backend` build paths listed in `.agents/subagents/manifest.yaml` | `ir-backend` |
| Arena, interner concurrency review, ownership analysis, runtime, and core source | `libraries/zc/**`; `products/zomlang/compiler/ownership/**`; non-concurrency `products/zomlang/runtime/**`; `products/zomcore/README.md`; `products/zomcore/src/**` | `runtime-memory` |
| Current-state compiler design notes | `docs/overview.md`; `docs/design/**` excluding `docs/design/tooling/**`; affected `docs/spec/**` audit surfaces | `spec-audit` |
| Native tests, CI, gates, coverage, benchmarks, corpus, and baseline | only the `verification` paths enumerated in `.agents/subagents/manifest.yaml`, including `products/zomlang/tests/**`, `.github/workflows/**`, `scripts/check-english-only.py`, `scripts/check-spec-alignment.py`, the named architecture/codegen/coverage scripts, coverage inputs, benchmark corpus and baseline, and coverage CMake utilities | `verification` |

The acceptance transaction assigns `products/zomlang/compiler/ownership/**`
to `runtime-memory`, assigns both named verification gates to `verification`
in the subagent manifest and verification ownership document, and synchronizes
all routing documentation.

## Security And Safety Impact

Complete authority records prevent digest-only collision admission. Tracked
membership prevents stale interned handles from authorizing current entities.
Arena ownership prevents reverse-lookup use-after-free for surviving leases.
Deterministic checked ranges prevent overlapping or order-dependent local
handles. Capability-owned immutable storage removes references to session
vectors and provider temporaries.

No materializer exposes a raw pointer. Every reverse lookup validates the
semantic-context brand. An inactive key, foreign root set, foreign brand,
unequal authority record, missing provenance entry, or allocation overflow
fails closed.

## Drawbacks And Risks

- The root cutover spans identity, query, Binder, Checker, diagnostics, module
  publication, and IR ownership.
- Stable header and Binder schemas require substantial codec and mutation-test
  coverage.
- Handle slots become demand-order-dependent for global identities. Semantic
  equality never uses a slot.
- The first implementation may demand capabilities serially.

## Alternatives Considered

A session ledger next to frozen registries cannot append a new identity and
creates two success authorities.

A database-owned interner cannot satisfy the accepted surviving-lease
lifetime because the database may be destroyed while a memo remains alive.

One revision-local wrapper around `runBinding()` would retain coarse global
reads and mutable session state without stable reusable facts.

Embedding complete headers in `ModuleBodySyntax` would violate its stable-item
boundary and duplicate named-item syntax. The staging-safe header queries
preserve the boundary and provide exact inputs.

Independent owner-body allocation from mutable counters would make handles
worker-order-dependent. The semantic allocation plan gives every materializer
a deterministic disjoint range.

## Compatibility And Rollout

This is an internal direct replacement. No released API, persisted format, or
external protocol requires compatibility.

Implementation may use reviewable commits while production roots remain
unchanged. The production cutover changes once:

1. land schemas, codecs, independent verifiers, projections, and mutation
   tests;
2. land arena-owned typed interners and membership projections;
3. land semantic Binder queries and the allocation plan;
4. land graph, skeleton, body, and bound-module capabilities;
5. migrate every downstream owner to retained leases; and
6. switch the final session root and delete every replaced authority and
   mirror in the same change.

There is no flag, alias, alternate decoder, compatibility path, or fallback.

## Documentation And Teaching Plan

The acceptance transaction updates the seven affected RFCs, their seven
trackers, and routing governance. The
implementation updates `docs/design/compiler-contracts.md` only after the
production root lands. That design note identifies the live stable builders,
independent verifiers, capability publishers, allocation planner, lease-owning
consumers, and project-native tests, and distinguishes accepted contracts from
implemented paths.

## Operational Readiness

Architecture gates must reject:

- any production `runBinding()` call;
- any frozen registry, `SemanticIdentityRegistrySet`, second interner, or
  session materialization ledger;
- any materialization specialization or call outside the exact production
  descriptor/key allowlist;
- membership demanded after interning;
- materialization before the final current sealed snapshot;
- semantic Binder values containing handles, spans, nodes, brands, revisions,
  arena references, or session references;
- provider/verifier shared traversal or dependency-selection helpers;
- session mirrors or capability clones;
- downstream bound-module references without an owning lease; and
- any internal version suffix or non-English repository artifact.

Each rejection has an adversarial `--self-test` mutation.

### Release Benchmark Contract

The fixed Release runner, corpus, and baseline paths remain:

- `scripts/run-incremental-query-benchmarks.py`;
- `products/zomlang/tests/performance/incremental-query-corpus.json`; and
- `products/zomlang/tests/performance/incremental-query-baseline.json`.

The corpus covers clean binding, equal body-only edits, unrelated body edits,
unrelated module edits, stable-equal provenance-only changes, repeated demand
and single-flight join, definition/implementation/parameter admission,
inactive-after-prior-intern, and projection shielding.

Wall-clock comparison uses exactly the machine-matched worker count recorded
in the baseline. Worker determinism separately runs worker counts `1` and the
machine-matched count with canonical and reversed demand orders and compares
stable bytes, typed graph-witness bytes, provider execution sets, and
coalescing counts. Raw handles are compared only between repeated or
concurrent equal-key demands inside the same arena; fresh arenas and unequal
demand orders never compare slots or branded handles.

The protocol is five warmups, twenty-one measured samples, median absolute
deviation at most three percent, aggregate elapsed ratio at most 105 percent,
and aggregate peak-RSS ratio at most 115 percent. Metadata for machine, build,
compiler, corpus, and worker count must match exactly. Noise above three
percent requires an idle rerun and cannot be waived.

`--record-baseline` is preparation, never release evidence and never bypasses
comparison. A corpus or baseline change requires this exact sequence:

1. start from a clean committed tree and a clean Release build;
2. record the candidate baseline;
3. record the written cause and obtain `verification` approval;
4. commit the approved corpus and baseline change;
5. clean-build Release from that exact commit; and
6. run the mandatory compare mode against the committed baseline.

Every release run uses compare mode, including the first release after a
baseline change.

## Acceptance Criteria

### Design Acceptance Criteria

- Every required owner approves one exact REVIEW proposal hash without editing
  the candidate.
- RFCs 0018, 0017, 0020, 0019, 0026, 0010, and 0025 plus their trackers and
  routing governance synchronize in the same acceptance transaction.
- Staging-safe headers represent definition and implementation scopes,
  implementation-owned generics, parameter sites, and complete body
  disposition.
- Every cross-module target has an explicit routing module.
- Stable Binder schemas have one hand-authored canonical field inventory,
  exact codecs, and structural/domain mutations.
- The query catalog and complete dynamic read sets are enforced.
- All eight global handle domains retain complete authority records and exact
  tracked active membership.
- The semantic-context arena owns the only typed interner set and surviving
  leases retain it.
- Equal-key concurrent admission coalesces; unequal-record digest collision,
  foreign brands, stale snapshots, and inactive current keys fail closed.
- Materialized schemas, allocation plan, independent verifiers, provenance,
  and stable witnesses are complete.

Design acceptance authorizes the implementation DAG below. It does not assert
that any new production path, deletion, test, gate, coverage result, or
benchmark result already exists.

### Implementation Completion Criteria

- Production has no batch Binder call, frozen registry, session mirror, or
  non-owning downstream bound-module reference.
- Sanitizer, unit, lit, diagnostic, lit-root, identity, query, Binder, Checker,
  session, IR, ownership, core, spec-alignment, coverage, English-only,
  versioning, RFC, format, diff, and Release benchmark gates pass.
- RFC implementation states change only after a synchronized evidence audit.

### RFC 0029 Accepted Query And Binder Contract

Acceptance transaction `rfc0029-accept-20260727-8d393a0c` binds this RFC to
RFC 0029 proposal SHA-256
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`.
RFC 0027 remains `ACCEPTED`; this synchronization changes design and
dependency authority without claiming product implementation.

The complete production key shapes are:

```text
ContextualDefinitionKey {
  contextRoots: CompilationRootSetQueryKey,
  definition: StableDefinitionQueryKey {
    module: ModuleKey,
    definition: DefinitionKey,
  },
}

ContextualBodyOwnerKey {
  contextRoots: CompilationRootSetQueryKey,
  body: StableOwnerBodyQueryKey {
    module: ModuleKey,
    owner: StableBodyOwnerKey,
  },
}
```

Every producer, consumer, codec, fixture, authority record, query key, and
generated inventory row changes atomically. Module ownership is always
explicit in the nested stable key.

`IdentitySyntaxSiteInventoryQuery` is a retained revision-local capability
with domain `zom.query.identity-syntax-site-inventory`, key
`StableModuleQueryKey`, capability
`binder::IdentitySyntaxSiteInventory`, `AnySnapshot` admission, `Reject`
cycle policy, and exact failures `SourceRejection<DiagnosticFact>` and
`KeyRejection<BinderKeyFailure>`. Its provider reads
`SelectedModuleSourceQuery`, `ParseSourceQuery`, and
`IdentitySyntaxSiteInventoryProducer` in that order. Its independent verifier
repeats the two query reads and traverses the complete parsed module topology
through `IdentitySyntaxSiteInventoryVerifier`.

The capability owns the complete sorted identity-syntax-site sequence and the
exact parse lease. Its descriptor-private witness is:

```text
IdentitySyntaxSiteInventoryWitness {
  module: ModuleKey,
  source: SourceFileKey,
  sourceDigest: Sha256Digest,
  sites: CanonicalSequence<IdentitySyntaxSiteWitness>,
}

IdentitySyntaxSiteWitness {
  key: IdentitySyntaxSiteKey,
  schemaPreorderOrdinal: uint32,
  source: SourceSpan,
}
```

An empty module publishes an empty sequence. Site keys repeat the outer module
and selected source. Ordinals are unique, in range, and identify the exact
node and source span. The private span decoder reads source key, byte start,
and byte end directly; matches the witness source, retained snapshot source,
and digest; calls `ImmutableSourceSnapshot::span`; requires the ordinal node's
exact span; consumes the complete payload; and verifies byte-identical
re-encoding.

`ResolveDiagnosticProvenance` derives the exact `StableModuleQueryKey` for an
`IdentitySyntaxSiteKey`, demands the inventory capability in the same
snapshot, and requires exactly one equal site. This resolution is independent
of stable-identity admission success.

`StableIdentityAdmissionQuery` is a retained revision-local capability with
domain `zom.query.stable-identity-admission`, key
`StableModuleQueryKey`, capability `binder::StableIdentityAdmission`,
`AnySnapshot` admission, `Reject` cycle policy, and the same two exact failure
alternatives. It owns the verified stable-identity candidate inventory and
retains the exact parse and identity-site-inventory leases.

Its provider reads in this exact order:

1. `SelectedModuleSourceQuery`;
2. `ParseSourceQuery`;
3. `IdentitySyntaxSiteInventoryQuery`;
4. `CandidateProducer`; and
5. `CandidateVerifier`.

Selected-source absence produces
`MissingSelectedModuleSource(Module(key.module), none)`. Parse rejection is
forwarded byte-for-byte. The independent verifier repeats the selected-source
and parse demands, reconstructs candidates without provider state, and checks
the complete descriptor-private witness.

Stable identity admission owns two source diagnostics:

- `ConstantExpressionNotAllowed` publishes `ZOM4079` with empty arguments, the
  rejected identity site as primary location, no secondary location, and no
  fix-it.
- `DuplicateGenericParameter` publishes `ZOM3010` with
  `BinderIdentifierDiagnosticArguments`, the duplicate identity site as
  primary location, exactly one `ZOM3017 PreviousDeclarationHere` secondary
  at the earlier declaration using
  `DiagnosticSecondaryRole::PreviousDeclaration = 0x01`, and no replacement.

`IdentityDiagnosticEmitter` adds
`ConstantExpressionNotAllowed = 0x03` and
`DuplicateGenericParameter = 0x04`. Both facts use
`ModuleDiagnosticRoot(key.module)`,
`IdentityDiagnosticPhase::IdentityAdmission`, no semantic owner, the matching
emitter, and the complete primary identity-site key as the stable occurrence.
The provider maps every verifier `NodeId` and `SourceSpan` to exactly one
inventory entry; the duplicate declaration must map to an earlier entry.
Mapping, payload, source, or candidate disagreement is runtime rejection.

The following five descriptors declare exactly:

```cpp
using FailureAlternatives = query::CapabilityFailureList<
    query::SourceRejection<diagnostics::DiagnosticFact>,
    query::KeyRejection<binder::BinderKeyFailure>>;
```

- `RevisionLocalDefinitionSitesQuery`;
- `RevisionLocalImplementationSitesQuery`;
- `ModuleBodyProvenanceQuery`;
- `NamedItemProvenanceQuery`; and
- `OwnerBodyProvenanceQuery`.

The universal precedence is evaluator runtime failure, descriptor key
rejection, upstream source rejection, then candidate. Each provider returns
the first eligible typed rejection in the exact read order below.

`RevisionLocalDefinitionSitesQuery` reads selected source, parse, stable
identity admission, named definition inventory, and independent site
reconstruction. `RevisionLocalImplementationSitesQuery` uses the same order
with the named implementation inventory. Their only local key rejection is
`MissingSelectedModuleSource(Module(key.module), none)`. They forward stable
admission source rejection unchanged. A semantic inventory failure after
successful admission is `InvariantViolation`.

`ModuleBodyProvenanceQuery` reads selected source, parse, stable identity
admission, revision-local definition sites, revision-local implementation
sites, and module-body syntax in that order. Its only legal key rejection is
`MissingSelectedModuleSource(Module(key.module), none)`. It forwards the first
source rejection from parse, admission, definition sites, or implementation
sites. A semantic module-body-syntax failure after typed provenance succeeds
is `InvariantViolation`.

`NamedItemProvenanceQuery` uses the complete `ContextualDefinitionKey` and
reads:

1. `ActiveDefinitionAuthorityInput`;
2. `ActiveDefinitionAuthorityReadyInput` only when authority is absent or
   contradictory;
3. `SelectedModuleSourceQuery`;
4. `ParseSourceQuery`;
5. `StableIdentityAdmissionQuery`;
6. `NamedDefinitionInventoryQuery`;
7. `RevisionLocalDefinitionSitesQuery`;
8. `RevisionLocalImplementationSitesQuery`; and
9. `NamedItemSyntaxQuery`.

Its legal key rejections are
`InactiveOwner(DefinitionHeader(key.definition), none)` and
`MissingSelectedModuleSource(Module(key.definition.module), none)`. Missing
readiness is `ProviderRejected`; contradictory authority is
`InvariantViolation`. Child source and key rejections are forwarded unchanged.

`OwnerBodyProvenanceQuery` uses the complete `ContextualBodyOwnerKey`. It first
reads exactly one typed provenance branch: module owners demand
`ModuleBodyProvenanceQuery`, and definition owners demand
`NamedItemProvenanceQuery`. Only after that branch succeeds does it read the
corresponding semantic syntax projection. A definition branch uses the
complete derived `ContextualDefinitionKey`. It constructs `OwnerBodySyntax`
directly and does not read `OwnerBodySyntaxQuery`; the independent verifier
repeats the syntax read and direct reconstruction.

Its legal key rejections are
`DefinitionWithoutBody(Body(key), none)`,
`InactiveOwner(DefinitionHeader(definitionKey), none)`, and
`MissingSelectedModuleSource(Module(key.body.module), none)`. Definition
admission applies the executable-root classification directly:
`NoBody` produces `DefinitionWithoutBody`, `Malformed` is
`InvariantViolation`, and `Executable` continues. The verifier repeats that
classification independently. None of the five descriptors constructs
`ForeignOwner`, `BoundaryMismatch`, `NonSelectedSource`, or
`CrossBoundaryPath`.

RFC 0027 `S1`, `S2`, and `S3` landed as the single build-visible RFC 0030
`R29-12AB` transaction at commit
`8885782747e4c863cefcb0d069bc4569cefce9aa`. The complete source-only wire
replacement lands separately through RFC 0042 as `R29-12D`. Runtime work
resumes at `R29-13A` only after that transaction passes its focused and
complete native gates. `S6` moves to `R29-13B`, where identity-site
inventory, stable admission, all five failure contracts, complete-key caller
migration, and focused tests join the single `R29-14` runtime landing.

## Implementation Plan

Each source task is bounded to approximately 400 changed source lines. A task
that exceeds that bound must be split before execution. New files named below
are contractual targets, not optional placement suggestions.

`S1`, `S2`, `S3`, and `S6` below name file partitions, not independent task
state. RFC 0030 `R30-11` through `R30-15`, RFC 0029 `R29-12A`,
`R29-12B`, `R29-12AB`, and `R29-13B` are their sole execution and status
authority. No RFC 0027 tracker row may execute or land those partitions
independently. RFC 0031 is the accepted metamodel authority consumed by
`R30-11`; no earlier task may infer ownership, instantiate a future
descriptor, or replace the hand-authored inventory with generated schema.

| Task | Owner | Depends on | Exact files | Deliverable and focused verification |
|---|---|---|---|---|
| `G1` | `rfc` | none | `docs/rfc/0027-binder-query-and-identity-materialization-closure.md`; `docs/rfc/tracking/0027-review-and-implementation.md`; `docs/rfc/0010-intermediate-representation-pipeline.md`; `docs/rfc/0017-incremental-compiler-query-architecture.md`; `docs/rfc/0018-stable-query-identity-wire-closure.md`; `docs/rfc/0019-stable-body-owner-and-query-closure.md`; `docs/rfc/0020-active-definition-authority-projection.md`; `docs/rfc/0025-source-backed-core-library-architecture.md`; `docs/rfc/0026-module-graph-query-closure.md`; their seven same-number tracker files; `docs/rfc/README.md` | obtain nine exact-hash approvals and prepare all RFC/tracker overlays while RFC 0027 remains REVIEW |
| `G2` | `task-router` | `G1` | `.agents/subagents/manifest.yaml`; `.agents/subagents/README.md`; `.agents/subagents/task-router.md`; `.agents/subagents/module-system.md`; `.agents/subagents/runtime-memory.md`; `.agents/subagents/verification.md`; `AGENTS.md` | assign ownership analysis to `runtime-memory`, assign `scripts/check-english-only.py` and `scripts/check-spec-alignment.py` to `verification`, and synchronize exact gate routing; RFC and English-only gates |
| `G3` | `rfc` | `G1`; `G2` | exactly the RFC and tracker files listed in `G1`, including `docs/rfc/README.md`; no `G2` file | read and validate the completed `G2` tree without editing it, record one transaction identifier and proposal hash in the `G1` files, then change acceptance metadata atomically; `python3 scripts/check-rfc.py` |
| `G4` | `verification` | `G3` | `products/zomlang/tests/coverage/implementation-series-base.txt` | from a clean committed accepted synchronization tree, record that exact commit SHA and reject a moving or non-ancestor base |
| `S1` | `binder-checker` with `verification` review | `G4`; RFC 0031 `R31-09` | `products/zomlang/compiler/binder/stable-binding-schema.def`; `scripts/check-stable-binding-schema.py` | prepare and review the hand-authored closed field, sum, tag, domain, bound, mutation, artifact-task, capability-payload, failure-alternative, and diagnostic-mapping inventory without an independent landing |
| `S2` | `binder-checker` for Binder facts; `module-system` for driver contextual declarations and callers; `verification` for native tests | `S1` review | `products/zomlang/compiler/binder/stable/stable-binding-facts.h`; `products/zomlang/compiler/binder/stable/stable-binding-facts.cc`; `products/zomlang/compiler/driver/contextual-binding-key.h`; `products/zomlang/compiler/driver/contextual-binding-key.cc`; the complete driver caller-cutover files and tests named by RFC 0030 | prepare and review stable facts plus driver-owned contextual keys and the direct deletion and migration of all query-specific declarations without an independent landing |
| `S3` | `binder-checker` for Binder codecs; `module-system` for contextual codecs; `verification` for tests, build discovery, schema, architecture, allowlist, and landing-scope gates | `S2` review | `products/zomlang/compiler/binder/stable/stable-binding-codec.h`; `products/zomlang/compiler/binder/stable/stable-binding-codec.cc`; the matching contextual codecs, Binder build and test wiring, schema and architecture gates, exact allowlist, and landing-scope gate named by RFC 0030 | prepare and review exact-consumption codecs, sequence admission, fixed wire oracles, native tests, mutations, build visibility, and landing-scope proof without an independent landing |
| `S3A` | `binder-checker` with `module-system` integration and `verification` review | `S3`; RFC 0029 `R29-14` | `products/zomlang/compiler/binder/identity/named-identity-inventory.h`; `products/zomlang/compiler/binder/identity/named-identity-inventory.cc`; `products/zomlang/compiler/binder/identity/revision-local-identity-sites.cc`; `products/zomlang/compiler/driver/active-definition-authority-session.cc`; `products/zomlang/compiler/driver/named-item-query.cc`; `products/zomlang/compiler/driver/compiler-session.cc`; `products/zomlang/tests/unittests/compiler/driver/active-definition-authority-session-test.cc`; `products/zomlang/tests/unittests/compiler/driver/incremental-binding-query-adapter-test.cc` | land typed complete definition and implementation inventory entries, the exact codecs and mutation coverage, and the direct production caller cutover |
| `S3B` | `module-system` with `binder-checker` and `verification` review | `S3A` | `products/zomlang/compiler/driver/named-identity-inventory-query.h`; `products/zomlang/compiler/driver/named-identity-inventory-query.cc`; `products/zomlang/tests/unittests/compiler/driver/named-identity-inventory-query-test.cc`; `scripts/check-binder-architecture.py` | read selected parse provenance, derive body disposition independently in provider and verifier, prove exact read ordering and failures, and enforce the architecture boundary |
| `S4` | `binder-checker` with `verification` review | `S3B` | `products/zomlang/compiler/binder/stable/definition/header-producer.h`; `products/zomlang/compiler/binder/stable/definition/header-producer.cc`; `products/zomlang/compiler/binder/CMakeLists.txt`; `products/zomlang/tests/unittests/compiler/binder/stable/definition/header-producer-test.cc`; `products/zomlang/tests/unittests/compiler/binder/CMakeLists.txt` | definition-header production from the closed borrowed input record, retained parse provenance, and complete syntax-role matrix |
| `S4A` | `binder-checker` with `verification` review | `S3B` | `products/zomlang/compiler/binder/stable/implementation/header-producer.h`; `products/zomlang/compiler/binder/stable/implementation/header-producer.cc`; `products/zomlang/compiler/binder/CMakeLists.txt`; `products/zomlang/tests/unittests/compiler/binder/stable/implementation/header-producer-test.cc`; `products/zomlang/tests/unittests/compiler/binder/CMakeLists.txt` | implementation-occurrence header production from the closed borrowed input record with complete identity, source-form, and scope-role coverage |
| `S5` | `binder-checker` with `verification` review | `S4`; `S4A` | `products/zomlang/compiler/binder/stable/header/verifier.h`; `products/zomlang/compiler/binder/stable/header/verifier.cc`; `products/zomlang/compiler/binder/stable-binding-schema.def`; `products/zomlang/compiler/binder/CMakeLists.txt`; `products/zomlang/tests/unittests/compiler/binder/stable/header/verifier-test.cc`; `products/zomlang/tests/unittests/compiler/binder/stable-binding-query-test.cc`; `products/zomlang/tests/unittests/compiler/binder/CMakeLists.txt`; `scripts/check-stable-binding-schema.py`; `scripts/check-binder-architecture.py` | independent full-context selection and header verification, exact schema provenance names, equal-occurrence coverage, caller-selected-entry rejection, and producer/verifier disagreement mutations |
| `S6` | `error-system` with `binder-checker`, `module-system`, and `verification` review | RFC 0029 `R29-13A`; RFC 0042 `R42-16` | RFC 0029 `R29-13B` exact live-producer landing set | directly replace the source-only fact contract with the live Source-and-Module contract; land Binder-owned typed arguments, five factories and mappings, Module provenance, exact native mutation coverage, schema and CTest ownership, `ZOM3028`, provider/verifier use, and failed-lookup bijection |
| `Q3` | `module-system` | `G3` | `products/zomlang/compiler/driver/package/canonical-package-compilation-request.h`; `products/zomlang/compiler/driver/package/canonical-package-compilation-request.cc` | handle-free canonical package request records, exact codecs, verified-request projection, and independent projection verifier; completed production ownership remains closed while RFC 0030 `R30-13` owns the comprehensive schema mutation test |
| `I1` | `module-system` with `runtime-memory` review | RFC 0029 `R29-14` | `products/zomlang/compiler/identity/canonical/identity-interner-set.h`; `products/zomlang/compiler/identity/canonical/identity-interner-set.cc`; `products/zomlang/compiler/query/semantic-context-capability-arena.h`; `products/zomlang/compiler/query/semantic-context-capability-arena.cc` | arena-owned eight-domain typed interner with collision, concurrency, reverse-lookup, and surviving-lease tests |
| `I1A` | `module-system` with `verification` review | `Q3`; RFC 0029 `R29-14` | `products/zomlang/compiler/driver/module-graph-query-input.h`; `products/zomlang/compiler/driver/module-graph-query-input.cc`; `products/zomlang/compiler/binder/stable-binding-schema.def`; `scripts/check-stable-binding-schema.py`; `products/zomlang/tests/unittests/compiler/driver/module-graph-query-input-test.cc` | prepare canonical input entries, the complete compilation-context authority value and exact codec, a producer-independent live-authority verifier, and the full declared mutation matrix; do not register or land the descriptor independently |
| `I2A` | `module-system` | `I1`; `I1A` review; RFC 0029 `R29-14`; `S5` | `products/zomlang/compiler/driver/active-identity-membership-query.h`; `products/zomlang/compiler/driver/active-identity-membership-query.cc` | prepare `ActiveMembershipResult`, its closed codec, record-codec contracts, and `ActiveCompilationUnitMembership`; do not land independently |
| `I2B` | `module-system` | `I2A` review | `products/zomlang/compiler/driver/active-identity-membership-query.h`; `products/zomlang/compiler/driver/active-identity-membership-query.cc` | prepare compilation-unit and crate membership descriptors with independent providers and verifiers; do not land independently |
| `I2C` | `module-system` | `I2B` review | `products/zomlang/compiler/driver/active-identity-membership-query.h`; `products/zomlang/compiler/driver/active-identity-membership-query.cc` | prepare source and module membership descriptors with active-parent validation and independent providers and verifiers; do not land independently |
| `I2D` | `module-system` | `I2C` review | `products/zomlang/compiler/driver/active-identity-membership-query.h`; `products/zomlang/compiler/driver/active-identity-membership-query.cc`; `products/zomlang/compiler/driver/active-definition-authority-query.h`; `products/zomlang/compiler/driver/active-definition-authority-query.cc` | prepare `CompleteRootIdentityReadiness`, the definition-authority input verifier, and definition membership with inventory-backed positive authority and conditional readiness; do not land independently |
| `I2E1` | `module-system` | `I2D` review | `products/zomlang/compiler/driver/active-identity-membership-query.h`; `products/zomlang/compiler/driver/active-identity-membership-query.cc`; `products/zomlang/compiler/driver/active-definition-authority-query.h`; `products/zomlang/compiler/driver/active-definition-authority-query.cc` | prepare the complete implementation membership record, exact codec, `ActiveImplementationAuthorityInput`, and structural input verifier; do not land independently |
| `I2E2` | `module-system` | `I2E1` review | `products/zomlang/compiler/driver/active-identity-membership-query.h`; `products/zomlang/compiler/driver/active-identity-membership-query.cc`; `products/zomlang/compiler/driver/active-definition-authority-query.h`; `products/zomlang/compiler/driver/active-definition-authority-query.cc` | prepare implementation occurrence coverage and independent membership provider and verifier starting from the exact implementation-authority input; do not land independently |
| `I2E` | `module-system` | approved `I2E1` and `I2E2` preparations | exactly the four files listed by `I2E1` and `I2E2`; no additional file | review the complete implementation membership union; no source edits and no independent landing |
| `I2F1` | `module-system` | `I2E` review | `products/zomlang/compiler/driver/active-identity-membership-query.h`; `products/zomlang/compiler/driver/active-identity-membership-query.cc`; `products/zomlang/compiler/driver/active-definition-authority-query.h`; `products/zomlang/compiler/driver/active-definition-authority-query.cc` | prepare implementation-generic authority, the definition/implementation owner sum, complete generic-parameter membership records, exact codecs, `ActiveGenericParameterAuthorityInput`, and its structural verifier; do not land independently |
| `I2F2` | `module-system` | `I2F1` review | `products/zomlang/compiler/driver/active-identity-membership-query.h`; `products/zomlang/compiler/driver/active-identity-membership-query.cc`; `products/zomlang/compiler/driver/active-definition-authority-query.h`; `products/zomlang/compiler/driver/active-definition-authority-query.cc` | prepare definition-owned and implementation-owned generic-parameter membership providers and independent verifiers starting from the exact parameter-authority input and preserving equal-occurrence authority; do not land independently |
| `I2F` | `module-system` | approved `I2F1` and `I2F2` preparations | exactly the four files listed by `I2F1` and `I2F2`; no additional file | review the complete generic-parameter membership union; no source edits and no independent landing |
| `I2G1` | `module-system` | `I2F` review | `products/zomlang/compiler/driver/active-identity-membership-query.h`; `products/zomlang/compiler/driver/active-identity-membership-query.cc`; `products/zomlang/compiler/driver/active-definition-authority-query.h`; `products/zomlang/compiler/driver/active-definition-authority-query.cc` | prepare the complete callable-parameter membership record, exact codec, `ActiveCallableParameterAuthorityInput`, and its structural verifier; do not land independently |
| `I2G2` | `module-system` | `I2G1` review | `products/zomlang/compiler/driver/active-identity-membership-query.h`; `products/zomlang/compiler/driver/active-identity-membership-query.cc`; `products/zomlang/compiler/driver/active-definition-authority-query.h`; `products/zomlang/compiler/driver/active-definition-authority-query.cc` | prepare callable-parameter membership provider and independent verifier starting from the exact parameter-authority input with receiver and position validation; do not land independently |
| `I2G3` | `module-system` | `I2G2` review | `products/zomlang/compiler/driver/active-identity-membership-query.h`; `products/zomlang/compiler/driver/active-identity-membership-query.cc`; `products/zomlang/compiler/driver/active-definition-authority-query.h`; `products/zomlang/compiler/driver/active-definition-authority-query.cc` | prepare registration for all four contextual authority inputs, complete-root readiness input, and eight membership descriptors; do not land independently |
| `I2G` | `module-system` | approved `I2G1` through `I2G3` preparations | exactly the four files listed by `I2G1` through `I2G3`; no additional file | review the complete callable-parameter and registration union; no source edits and no independent landing |
| `I2` | `module-system` with `verification` review | approved `I2A` through `I2G` preparations | exactly the four files listed by `I2A` through `I2G`; no additional file | review the complete eight-membership and readiness union against the mutation inventory; no source edits and no independent landing |
| `B1` | `binder-checker` | RFC 0029 `R29-12AB`; RFC 0029 `R29-12D`; `S5` | `products/zomlang/compiler/binder/graph/module-skeleton-query.h`; `products/zomlang/compiler/binder/graph/module-skeleton-query.cc` | `BindModuleSkeleton`, projections, and independent verifier |
| `B2` | `binder-checker` | `B1` | `products/zomlang/compiler/binder/surface/owner-body-query.h`; `products/zomlang/compiler/binder/surface/owner-body-query.cc` | contextual `BindOwnerBody` and independent traversal/verifier |
| `B3` | `binder-checker` | `B2` | `products/zomlang/compiler/binder/surface/owner-body-syntax.h`; `products/zomlang/compiler/binder/surface/owner-body-syntax.cc` | complete node-scope, capture, control, and provenance detachment |
| `B4` | `binder-checker` | `B2`; `B3` | `products/zomlang/compiler/binder/graph/module-binding-allocation-plan.h`; `products/zomlang/compiler/binder/graph/module-binding-allocation-plan.cc` | deterministic five-domain allocation plan |
| `M1A1` | `module-system` | `I2` review; RFC 0029 `R29-14`; RFC 0028 `R28-16` | `products/zomlang/compiler/driver/materialized-module-graph-query.h` | prepare the public Pimpl declarations for every non-trivial materialized graph value, the explicit identity-entry instantiation aliases, and the descriptor declaration; do not land independently |
| `M1A2` | `module-system` | `M1A1` review | `products/zomlang/compiler/driver/materialized-module-graph-query.cc` | prepare private Pimpl storage, move operations, destructors, clones, and basic accessors for the M1A1 values; do not land independently |
| `M1A3` | `module-system` | `M1A2` review | `products/zomlang/compiler/driver/materialized-module-graph-query.h`; `products/zomlang/compiler/driver/materialized-module-graph-query.cc` | prepare the four-domain identity-resource interface, exact active-materialization specializations, closed interner-failure mapping, and four compile-time permissions; do not land independently |
| `M1A` | `module-system` | approved `M1A1`; `M1A2`; `M1A3` preparations | no new files | review-only join proving the public value layout is opaque and the four-domain resource and permission boundary is exact |
| `M1B` | `module-system` | `M1A` review | `products/zomlang/compiler/driver/materialized-module-graph-query.h`; `products/zomlang/compiler/driver/materialized-module-graph-query.cc` | prepare stable dependency and graph witness construction, exact codecs, comparison, and candidate contract; do not land independently |
| `M1C1A` | `module-system` | `M1B` review | `products/zomlang/compiler/driver/materialized-module-graph-query.cc` | prepare provider-only complete-context and fingerprint-prerequisite reads, active crate/source/module acquisition, and graph/SCC validation in canonical order; do not issue per-module dependency or capability reads and do not land independently |
| `M1C1B` | `module-system` | `M1C1A` review | `products/zomlang/compiler/driver/materialized-module-graph-query.cc` | prepare provider-only per-module selected-source, dependency-site, request, exact-resolution, configured-prelude, parse, and provenance reads plus exact typed child-failure forwarding in canonical order; do not materialize or land independently |
| `M1C1` | `module-system` | approved `M1C1A`; `M1C1B` preparations | no new files | review-only join proving acquisition preserves the complete canonical demand order and exact typed child-failure precedence through provenance |
| `M1C2` | `module-system` | `M1C1` review | `products/zomlang/compiler/driver/materialized-module-graph-query.cc` | prepare provider-only four-domain membership admission, logical-const materialization, and reverse expansion in the accepted domain order; do not publish a candidate or land independently |
| `M1C3` | `module-system` | `M1C2` review | `products/zomlang/compiler/driver/materialized-module-graph-query.cc` | prepare provider-only stable witness, independently recomputed revision, handle-edge, and candidate publication from the approved ordered acquisition and materialization state; do not land independently |
| `M1C` | `module-system` | approved `M1C1`; `M1C2`; `M1C3` preparations | no new files | review-only join proving the provider preserves the complete canonical demand order, typed failure precedence, exact membership admission, reverse expansion, and candidate publication contract |
| `M1D1` | `module-system` | `M1C` review | `products/zomlang/compiler/driver/materialized-module-graph-query.cc` | prepare verifier-only exact context, fingerprint-prerequisite, active-domain, graph, and SCC acquisition with independent canonical ordering; do not issue per-module reads, materialize, or land independently |
| `M1D2` | `module-system` | `M1D1` review | `products/zomlang/compiler/driver/materialized-module-graph-query.cc` | prepare verifier-only per-module resolution, reached prelude, parse, provenance, independent stable-edge ordering, and fingerprint reconstruction; do not materialize or land independently |
| `M1D3` | `module-system` | `M1D2` review | `products/zomlang/compiler/driver/materialized-module-graph-query.cc` | prepare verifier-only four-domain membership materialization, same-resource reverse expansion, independent revision and graph-edge reconstruction, handle-edge validation, and candidate witness comparison; do not issue earlier reads or land independently |
| `M1D` | `module-system` | approved `M1D1`; `M1D2`; `M1D3` preparations | no new files | review-only join proving the verifier independently preserves the complete canonical demand order without provider graph, edge-order, candidate, fingerprint, or revision helpers |
| `M1E` | `module-system` | `M1D` review | `products/zomlang/compiler/driver/materialized-module-graph-query.h`; `products/zomlang/compiler/driver/materialized-module-graph-query.cc` | prepare source/key failure contracts plus schema-derived capability, failure, permission, and descriptor assertions; do not land independently |
| `M1` | `module-system` | approved `M1A`; `M1B`; `M1C`; `M1D`; `M1E` preparations | no new files | review-only join proving the complete materializer union remains prepare-only and satisfies the exact read, rejection-order, witness, opaque-layout, resource, and permission contracts |
| `M2` | `binder-checker` | `B4`; `T1` | `products/zomlang/compiler/binder/graph/materialized-module-skeleton.h`; `products/zomlang/compiler/binder/graph/materialized-module-skeleton.cc` | typed skeleton expansion and provenance after the complete-context atomic landing |
| `M3` | `binder-checker` | `B4`; `M2` | `products/zomlang/compiler/binder/materialized-owner-body.h`; `products/zomlang/compiler/binder/materialized-owner-body.cc` | typed body expansion without diagnostic references |
| `M4` | `binder-checker` | `M2`; `M3` | `products/zomlang/compiler/binder/metadata/immutable-definition-inventory.h`; `products/zomlang/compiler/binder/metadata/immutable-definition-inventory.cc` | complete owned Checker identity and node lookup index |
| `M5` | `binder-checker` | `M4` | `products/zomlang/compiler/binder/verified-bound-module.h`; `products/zomlang/compiler/binder/verified-bound-module.cc` | `VerifyBoundModule`, aggregate coverage, failure projection, and retained children |
| `C1` | `binder-checker` | `M5` | `products/zomlang/compiler/checker/module-interface-contract.h`; `products/zomlang/compiler/checker/module-interface-contract.cc` | lease-owning Checker view and interface contract |
| `C1A` | `binder-checker` | `C1` | `products/zomlang/compiler/checker/body-checker.h`; `products/zomlang/compiler/checker/body-checker.cc` | migrate Checker body consumers |
| `C2` | `module-system` | `C1`; `C1A` | `products/zomlang/compiler/driver/module-interface.h`; `products/zomlang/compiler/driver/module-interface.cc` | module-interface publication from the lease-owning view |
| `L1` | `ir-backend` | `C1A` | `products/zomlang/compiler/hir/checked-module.h`; `products/zomlang/compiler/hir/checked-module.cc` | checked-module retained lease and verifier |
| `L2` | `ir-backend` | `L1` | `products/zomlang/compiler/hir/hir-module.h`; `products/zomlang/compiler/hir/hir-module.cc` | HIR retained lease and lineage verifier |
| `L3` | `ir-backend` | `L2` | `products/zomlang/compiler/mir/built-mir.h`; `products/zomlang/compiler/mir/built-mir.cc` | Built MIR retained lease and lineage verifier |
| `L4` | `runtime-memory` | `L3` | `products/zomlang/compiler/ownership/ownership-event-overlay.h`; `products/zomlang/compiler/ownership/ownership-event-overlay.cc` | ownership-overlay retained lease, verifier, and destruction order |
| `T1` | `module-system` with `verification` and query-runtime review | approved I1A, I2, and M1 preparations; RFC 0029 `R29-14`; RFC 0028 `R28-16` | all files listed by I1A, I2, and M1; `products/zomlang/compiler/binder/binding-input.h`; `products/zomlang/compiler/binder/binding-input.cc`; `products/zomlang/compiler/identity/semantic/context-fingerprint.h`; `products/zomlang/compiler/identity/semantic/context-fingerprint.cc`; `products/zomlang/compiler/query/query-database.h`; `products/zomlang/compiler/query/query-descriptor-schema.def`; `products/zomlang/compiler/driver/CMakeLists.txt`; `products/zomlang/compiler/driver/core-library-query-provider.h`; `products/zomlang/compiler/driver/core-library-query-provider.cc`; `products/zomlang/compiler/driver/active-definition-authority-session.h`; `products/zomlang/compiler/driver/active-definition-authority-session.cc`; `products/zomlang/compiler/driver/compiler-session.cc`; `products/zomlang/tests/unittests/compiler/binder/binding-input-test.cc`; `products/zomlang/tests/unittests/compiler/driver/core-library-query-provider-test.cc`; `products/zomlang/tests/unittests/compiler/driver/active-definition-authority-session-test.cc`; `products/zomlang/tests/unittests/compiler/driver/compiler-session-package-test.cc`; `products/zomlang/tests/unittests/compiler/query/query-test-specs.h`; `products/zomlang/tests/unittests/compiler/query/query-test-descriptor-schema.def`; `products/zomlang/tests/unittests/compiler/query/query-database-test.cc`; `products/zomlang/tests/unittests/compiler/query/query-capability-test.cc`; `products/zomlang/tests/unittests/compiler/query/CMakeLists.txt`; `scripts/check-query-descriptor-architecture.py`; `scripts/generate-query-descriptor-schema.py` | atomically land the complete-context value and descriptor, the unique graph-revision and fingerprint canonical-digest construction boundaries, the narrow capability-context revision and typed-resource accessors, memberships, readiness, graph and SCC witnesses, the production and test descriptor inventories, their exact driver build rows, three static transaction-witness descriptors, three closed input transactions with caller-supplied previous revisions and same-revision witnesses, all three production mutation caller cutovers including direct replacement of authority refresh, both same-name shadow deletions, the slot-59 generic runtime fixture, production descriptor final-seal integration, complete static final-authority verification, full mutation matrix, and staging, final, and sealed snapshot publication |
| `T2A` | `module-system` | `M5`; `T1` | `products/zomlang/compiler/driver/compiler-session.h`; `products/zomlang/compiler/driver/compiler-session.cc` | install the transaction state machine and named snapshots, and make the arena-owned compiler-session semantic resource implement the approved module-graph identity-materialization interface without a second interner or resource fallback |
| `T2B` | `module-system` | `C2`; `L4`; `T2A` | `products/zomlang/compiler/driver/compiler-session.h`; `products/zomlang/compiler/driver/compiler-session.cc` | dependency-first final capability root and irreversible seal |
| `T2C` | `module-system` | `T2B` | `products/zomlang/compiler/driver/compiler-session.h`; `products/zomlang/compiler/driver/compiler-session.cc` | surviving-lease and session teardown order |
| `D1` | `module-system` | `T2C` | `products/zomlang/compiler/identity/semantic-identity-registry-set.h`; `products/zomlang/compiler/identity/semantic-identity-registry-set.cc`; `products/zomlang/compiler/identity/frozen-registry.h` | delete registry/freeze identity authority |
| `D2` | `binder-checker` | `T2C` | `products/zomlang/compiler/binder/frozen-definition-inventory.h`; `products/zomlang/compiler/binder/frozen-definition-inventory.cc` | delete frozen inventory and its lookup view |
| `D3` | `module-system` | `T2C` | `products/zomlang/compiler/driver/incremental-binding-query-adapter.h`; `products/zomlang/compiler/driver/incremental-binding-query-adapter.cc`; `products/zomlang/compiler/driver/module-graph-query.h`; `products/zomlang/compiler/driver/module-graph-query.cc` | delete session ledgers and handleful graph root |
| `D4` | `binder-checker` | `T2C` | `products/zomlang/compiler/binder/binding-run.h`; `products/zomlang/compiler/binder/binding-run.cc`; `products/zomlang/compiler/binder/verified-bound-module-input.h`; `products/zomlang/compiler/binder/verified-bound-module-input.cc` | delete production batch root and non-owning input |
| `D5` | `ir-backend` | `L3` | `products/zomlang/compiler/ir/ir-failure.h`; `products/zomlang/compiler/ir/ir-failure.cc`; `products/zomlang/compiler/ir/ir-diagnostic-adapter.h`; `products/zomlang/compiler/ir/ir-diagnostic-adapter.cc` | delete Binder-to-IR failure conversion and non-owning IR lineage |
| `W1` | `binder-checker` | `C1`; `D4`; `M5` | `products/zomlang/compiler/binder/CMakeLists.txt`; `products/zomlang/compiler/checker/CMakeLists.txt` | Binder and Checker source wiring |
| `W2` | `module-system` | `D1`; `D3`; `T2C` | `products/zomlang/compiler/identity/CMakeLists.txt`; `products/zomlang/compiler/query/CMakeLists.txt`; `products/zomlang/compiler/driver/CMakeLists.txt` | post-T2C identity, query, graph, and session deletion/final wiring without changing the two T1-owned additive source rows |
| `W3` | `ir-backend` | `D5`; `L3` | `CMakeLists.txt`; `CMakePresets.json`; `products/zomlang/compiler/CMakeLists.txt`; `products/zomlang/compiler/hir/CMakeLists.txt`; `products/zomlang/compiler/mir/CMakeLists.txt`; `products/zomlang/compiler/ir/CMakeLists.txt` | compiler and IR build wiring |
| `W4` | `runtime-memory` | `L4` | `products/zomlang/compiler/ownership/CMakeLists.txt` | ownership-overlay source wiring |
| `E1` | `verification` | `W1`; `W2`; `W3`; `W4` | `products/zomlang/tests/unittests/compiler/binder/stable-binding-query-test.cc`; `products/zomlang/tests/unittests/compiler/binder/materialized-binding-capability-test.cc`; `products/zomlang/tests/unittests/compiler/query/query-capability-test.cc`; `products/zomlang/tests/unittests/compiler/driver/compiler-session-test.cc`; `products/zomlang/tests/CMakeLists.txt` | stable, materialized, seal, teardown, and cross-owner lineage tests |
| `E2` | `verification` | `E1` | `products/zomlang/tests/unittests/compiler/hir/hir-module-test.cc`; `products/zomlang/tests/unittests/compiler/mir/built-mir-test.cc`; `products/zomlang/tests/unittests/compiler/ownership/ownership-event-overlay-test.cc`; `products/zomlang/tests/unittests/compiler/ir/ir-failure-test.cc`; `products/zomlang/tests/unittests/compiler/ir/ir-diagnostic-adapter-test.cc`; `products/zomlang/tests/unittests/compiler/driver/incremental-binding-query-adapter-test.cc` | Checked/HIR/MIR/ownership joint lineage and deletion regressions |
| `E3` | `verification` | `E2` | `scripts/check-identity-architecture.py`; `scripts/check-incremental-query-architecture.py`; `scripts/check-binder-architecture.py`; `scripts/check-checker-architecture.py`; `scripts/check-compiler-session-architecture.py`; `scripts/check-ir-architecture.py`; `scripts/check-ownership-architecture.py` | exact architecture allowlists and adversarial self-tests |
| `E4` | `verification` | `E2` | `scripts/check-core-library-architecture.py`; `scripts/check-core-library-spec-alignment.py`; `scripts/check-spec-alignment.py`; `scripts/check-package-architecture.py`; `scripts/check-impl-source-architecture.py`; `scripts/check-lexer-architecture.py`; `scripts/check-parser-coverage.py`; `scripts/check-diagnostic-coverage.py`; `scripts/check-lit-exec-root.py` | core, source, five-way spec, lexer, parser, diagnostic, and lit-root gates; the core alignment gate has a spec-audit-owned `--check --report` publication mode and a write-free `--verify-report` byte-comparison mode |
| `E4A` | `verification` | `G4` | `scripts/check-english-only.py` | implement changed-text English-only enforcement, require an exact forty-lowercase-hex-plus-newline base file naming a commit that is an ancestor of `HEAD`, and provide adversarial self-tests for malformed, moving, non-ancestor, and CJK mutations |
| `E5` | `verification` | `E4` | `scripts/generate-canonical-header-syntax-schema.py`; `scripts/codegen/gen_ast.py`; `scripts/codegen/gen_core_library_inventory.py`; `scripts/codegen/gen_package_oracles.py`; `products/zomlang/compiler/binder/stable-binding-schema.def` | generated header, AST, core-library, and package-oracle drift plus hand-authored stable-binding schema consumer drift and self-tests |
| `E6` | `verification` | `G4`; `E3` | `scripts/run-ownership-coverage.py`; `scripts/check-ownership-coverage.py`; `products/zomlang/tests/coverage/ownership-exemptions.json`; `products/zomlang/tests/coverage/implementation-series-base.txt`; `cmake/utils/coverage.cmake` | frozen-base ownership coverage artifacts and thresholds |
| `E7` | `verification` | `E3`; `E4`; `E4A`; `E5`; `E6`; `W1-W4` | `.github/workflows/CI.yml`; `products/zomlang/tests/CMakeLists.txt`; `products/zomlang/tests/integration/core-library/installed-consumer/Zom.toml`; `products/zomlang/tests/integration/core-library/installed-consumer/src/main.zom`; `products/zomlang/tests/cmake/verify-core-library-install-consumer.cmake`; `cmake/utils/unittests.cmake` | native sanitizer, codegen, gate, coverage, and the unique `core-library-install-consumer` integration |
| `E8` | `verification` | `E7` | `scripts/run-incremental-query-benchmarks.py`; `products/zomlang/tests/performance/incremental-query-corpus.json`; `products/zomlang/tests/performance/incremental-query-baseline.json` | fixed Release compare and worker-determinism evidence |
| `A2` | `spec-audit` | `E8` | `docs/design/compiler-contracts.md`; `docs/reports/zom-core-library-spec-alignment.md` | publish current-state builders, verifiers, publishers, consumers, tests, and the fixed core-library alignment report; run the report-producing alignment command |
| `A3` | `verification` | `A2` | no file edits; read-only inputs are `docs/reports/zom-core-library-spec-alignment.md`, `products/zomlang/tests/coverage/implementation-series-base.txt`, and the repository tree | use `--verify-report` to compare the fixed report without writing it, then rerun English-only, RFC, format, versioning, and diff gates after all spec-audit edits |
| `A1` | `rfc` | `A3` | this RFC; its tracker; the seven synchronized RFCs and trackers; `docs/rfc/README.md` | audit the complete implementation and A2/A3 evidence, then perform the only truthful RFC status transitions |

Every implementation task from `S1` through `W4` additionally depends on
`G4`, even where its local data dependency is shown more narrowly. No source
edit starts before the immutable implementation-series base exists.

`S1`, `S2`, and `S3` are reviewed in the bounded RFC 0030 sequence and cannot
land separately. RFC 0029 `R29-12AB` is their only landing transaction and
contains the exact RFC 0030 allowlist. RFC 0042 `R29-12D` is the independent
source diagnostic prerequisite. `R29-13A` is the first authorized runtime
task, and `S6` lands atomically with the first live Module diagnostic producers
in `R29-13B`.

## Test Plan

The implementation must add every named missing gate to the project-native
CMake/CTest workflow before the production cutover. Passing an unrelated
script is not substitute evidence.

- Clean sanitizer:
  `PATH=/opt/homebrew/bin:$PATH cmake --preset sanitizer`;
  `PATH=/opt/homebrew/bin:$PATH cmake --build --preset sanitizer --clean-first`;
  `PATH=/opt/homebrew/bin:$PATH ctest --preset default --output-on-failure`.
- Generated inventories and package oracles:
  `python3 scripts/codegen/gen_ast.py --check`;
  `python3 scripts/generate-canonical-header-syntax-schema.py --self-test`;
  `python3 scripts/generate-canonical-header-syntax-schema.py --check`;
  `python3 scripts/codegen/gen_core_library_inventory.py --self-test`;
  `python3 scripts/codegen/gen_core_library_inventory.py --check`;
  `PATH=/opt/homebrew/bin:$PATH cmake --build --preset sanitizer --target generate-core-library-inventory`;
  `python3 scripts/codegen/gen_package_oracles.py --self-test`;
  `python3 scripts/codegen/gen_package_oracles.py --check`.
- Diagnostic and lit roots:
  `python3 scripts/check-diagnostic-coverage.py --self-test`;
  `python3 scripts/check-diagnostic-coverage.py --check`;
  `python3 scripts/check-lit-exec-root.py --self-test`;
  `python3 scripts/check-lit-exec-root.py --check`.
- Architecture gates, each invoked once with `--self-test` and once with
  `--check`: `scripts/check-identity-architecture.py`,
  `scripts/check-incremental-query-architecture.py`,
  `scripts/check-binder-architecture.py`,
  `scripts/check-checker-architecture.py`,
  `scripts/check-compiler-session-architecture.py`,
  `scripts/check-impl-source-architecture.py`,
  `scripts/check-ir-architecture.py`,
  `scripts/check-ownership-architecture.py`,
  `scripts/check-core-library-architecture.py`, and
  `scripts/check-package-architecture.py`.
- Registered lexer and parser gates:
  `python3 scripts/check-lexer-architecture.py`;
  `python3 scripts/check-parser-coverage.py`.
- Core and specification alignment:
  `python3 scripts/check-core-library-spec-alignment.py --self-test`;
  `python3 scripts/check-core-library-spec-alignment.py --check`;
  `python3 scripts/check-spec-alignment.py --self-test`;
  `python3 scripts/check-spec-alignment.py --check --report build-sanitizer/reports/spec-alignment/rfc0027.json`;
  `PATH=/opt/homebrew/bin:$PATH ctest --preset default -R '^spec-alignment$' --output-on-failure`.
  The direct gate and native test write exactly
  `build-sanitizer/reports/spec-alignment/rfc0027.json` and fails unless the
  lexical chapter, lexer grammar, grammar reference, expressions chapter, AST,
  Binder, and Checker inventories agree.
- Core install consumer:
  `PATH=/opt/homebrew/bin:$PATH ctest --preset default -R '^core-library-install-consumer$' --output-on-failure`.
- Ownership coverage:
  `python3 scripts/check-ownership-coverage.py --self-test`;
  `python3 scripts/run-ownership-coverage.py`;
  `python3 scripts/check-ownership-coverage.py`.
  The runner writes only
  `build-coverage/coverage/ownership/profiles/`,
  `build-coverage/coverage/ownership/llvm-cov-export.json`,
  `build-coverage/coverage/ownership/report.json`, and
  `build-coverage/coverage/ownership/report.md`. The checker reads those
  artifacts and
  `products/zomlang/tests/coverage/ownership-exemptions.json` and
  `products/zomlang/tests/coverage/implementation-series-base.txt`, requires at
  least 70 percent line coverage for every changed non-test compiler `.cc`,
  and rejects aggregate regression. The base file is exactly forty lowercase
  hexadecimal commit bytes plus newline and names the accepted synchronized
  design commit immediately before implementation.
- Text and governance:
  `python3 scripts/check-english-only.py --self-test`;
  `python3 scripts/check-english-only.py --check --base-file products/zomlang/tests/coverage/implementation-series-base.txt`;
  `python3 scripts/check-no-internal-versioning.py --self-test`;
  `python3 scripts/check-no-internal-versioning.py --check`;
  `python3 scripts/check-rfc.py`;
  `python3 scripts/check-format.py`;
  `git diff --check`.
- Release:
  `PATH=/opt/homebrew/bin:$PATH cmake --preset release`;
  `PATH=/opt/homebrew/bin:$PATH cmake --build --preset release --clean-first`;
  `python3 scripts/run-incremental-query-benchmarks.py --repository . --build-dir build-release --corpus products/zomlang/tests/performance/incremental-query-corpus.json --baseline products/zomlang/tests/performance/incremental-query-baseline.json --worker-count 8 --compare`.
  The checked-in catalog contains independently reviewed worker-count `8`
  entries. An approved baseline transaction updates only the entry whose exact
  machine, compiler, build, corpus, and worker metadata it records; the
  comparison command remains unchanged.
- Spec-audit publication after the complete implementation and Release matrix:
  `python3 scripts/check-core-library-spec-alignment.py --check --report docs/reports/zom-core-library-spec-alignment.md`.
  This is the only command that writes the fixed report and is executed by
  `A2`.
- Final write-free verification after the report and current-state design
  update:
  `python3 scripts/check-core-library-spec-alignment.py --verify-report docs/reports/zom-core-library-spec-alignment.md`;
  `python3 scripts/check-english-only.py --check --base-file products/zomlang/tests/coverage/implementation-series-base.txt`;
  `python3 scripts/check-no-internal-versioning.py --check`;
  `python3 scripts/check-rfc.py`;
  `python3 scripts/check-format.py`;
  `git diff --check`.
  `--verify-report` recomputes the report in memory, compares exact bytes, and
  never creates or changes a file. Only after `A3` passes may `A1` change RFC
  implementation states.

Focused native tests cover all eight typed interners, complete-record
collision, implementation-owned generics, implementation occurrence
materialization, header provenance, every result-wire alternative and invalid
payload combination, allocation range overflow, provider/verifier read sets,
context-root equality, concurrent coalescing, inactive-after-prior-intern,
foreign leases, session/database teardown with a surviving lease, and zero
production mirror or batch-root references.

## Open Questions

None

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-07-27 | DRAFT | Initial Binder-query and identity-materialization closure draft. |
| 2026-07-27 | REVIEW | First complete candidate submitted for required-owner review. |
| 2026-07-27 | RETURNED | Proposal hash `d940570bc31876f1b5b0580ef936bb8b93fcfe4925c21ac3cff2c5e19f7ed44c` was rejected for incomplete implementation scope, header inputs, identity authority, lifetime, failure, synchronization, and verification contracts. No approval was retained. |
| 2026-07-27 | DRAFT | Candidate rewritten from the returned findings. |
| 2026-07-27 | REVIEW | Replacement candidate submitted for a new exact-hash review. |
| 2026-07-27 | RETURNED | Proposal hash `8abe36d734937cf2b51a7f28cc2d3e03a746f0ac96d4e76eb08e96d8630b74b4` was rejected for contextual owner-body keys, session transactions, fact and allocation completeness, diagnostic duplication, codec and descriptor closure, graph materialization authority, Checker and IR lease lineage, synchronization deletion scope, routing, coverage, and benchmark contradictions. No approval was retained. |
| 2026-07-27 | DRAFT | Candidate returned for complete contract replacement. |
| 2026-07-27 | REVIEW | Third complete candidate submitted for a new exact-hash review. |
| 2026-07-27 | RETURNED | Proposal hash `def9c2f82597c4c70a39622a2e6182d26b1fbb70589cdc7eda00c5ccc8f38c4e` was rejected for projection-domain ambiguity, incomplete capability and diagnostic results, lost Binder facts and provenance, undefined context and transaction codecs, inconsistent materializer permissions, synchronization gaps, task-owner conflicts, oversized IR work, and incomplete verification routing. No approval was retained. |
| 2026-07-27 | DRAFT | Candidate returned for complete contract replacement. |
| 2026-07-27 | REVIEW | Fourth complete candidate submitted for a new exact-hash review. |
| 2026-07-27 | RETURNED | Proposal hash `11bc26e10d27407a4f0987a5283953023b6f1e3449923bf1118a2ddecfbdeee6` was rejected for ambiguous syntax-root identity, a lossy materialized binding, a handleful compilation-context record, inverted transaction dependencies, cross-owner synchronization, and incomplete verification ownership. No approval was retained. |
| 2026-07-27 | DRAFT | Candidate returned for complete contract replacement. |
| 2026-07-27 | REVIEW | Fifth complete candidate submitted for a new exact-hash review. |
| 2026-07-27 | RETURNED | Proposal hash `e6e70449f4029345af520b67620dd3522c1290362d535e7c058bd2e5962dd925` was rejected for an unspecified live verified-package-request verifier input and a fixed-report ownership and completion-order contradiction. No approval was retained. |
| 2026-07-27 | DRAFT | Candidate returned for complete contract replacement. |
| 2026-07-27 | REVIEW | Sixth complete candidate submitted for a new exact-hash review. |
| 2026-07-27 | ACCEPTED | All nine required owners approved proposal hash `e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`. Acceptance transaction `rfc0027-accept-20260727-e2f4ba5e` synchronizes the seven dependent RFCs, their trackers, and routing governance atomically. Implementation remains blocked on the immutable implementation-series base. |
| 2026-07-27 | ACCEPTED | Immutable implementation-series base `109947943519ec2d380a3e8d71813b40bc68bde5` was recorded from a clean committed tree and passed the ancestry check. Dependency-ordered implementation is authorized. |
| 2026-07-27 | ACCEPTED | Acceptance transaction `rfc0028-accept-20260727-944b68ff` synchronized the final-seal admission, descriptor-specific capability failures, exact membership ordering, dependency provenance, closure-projection deletion, and corrected query-runtime dependency graph to RFC 0028 proposal SHA-256 `944b68ffc0aff5576d079a243ff092d7d19fba5ffed65551dda8e68adf230db4`; RFC 0027 status remains unchanged. |
| 2026-07-27 | ACCEPTED | Acceptance transaction `rfc0029-accept-20260727-8d393a0c` synchronized complete module-qualified Binder keys, identity-site provenance, stable-identity admission, the five exact typed capability failure contracts, and the atomic schema-plus-facts dependency order to RFC 0029 proposal SHA-256 `8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`; RFC 0027 status remains unchanged and implementation remains incomplete. |
| 2026-07-28 | ACCEPTED | Acceptance transaction `rfc0030-accept-20260728-4ed0e6b8` synchronized the exact build-visible S1-plus-S2-plus-S3 landing set, driver-owned contextual-key cutover, native and mutation gates, and separate S6 diagnostic transaction to RFC 0030 proposal SHA-256 `4ed0e6b885abc87a1c4251855780cf115a85b3623b1d46f774a4b664110f7b6b`; RFC 0027 status remains accepted and implementation remains incomplete. |
| 2026-07-28 | ACCEPTED | Acceptance transaction `rfc0031-accept-20260728-c25fcb18` synchronized the hand-authored schema metamodel, direct `Optional<MemberVisibility>` result, descriptor-parameterized capability results and payloads, dual capability/failure-alternative owner-task checks, exact Q3 versus `R30-13` test ownership, and the complete-context ownership matrix to RFC 0031 proposal SHA-256 `c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5` and tracker SHA-256 `d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d`; RFC 0027 status remains accepted, completed Q3 remains complete, and implementation remains incomplete. |
| 2026-07-29 | ACCEPTED | User-designated independent approval bound the stable-header scope correction to exact pre-evidence Git diff SHA-256 `9af2ae8a4610f578ef14f3975277ba52eda4497cef2843ee7e699fb264d5e756` through transaction `rfc0027-header-scope-20260729-9af2ae8a`; the transaction authorizes only the corrected dependency order and source tasks and completes no implementation row. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-context-foundation-20260730-1214413e` binds the synchronized eight-document complete-context foundation correction to independently approved exact pre-evidence Git diff SHA-256 `1214413eef714da5727a705d68bb9872d47ea78b28b18600ac158c87db63ac61`; `I1A` owns the value, codec, descriptor, verifier, schema row, and tests before `I2`, while `T1` retains provider, transaction, and session publication duties. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-context-atomic-20260730-b25aef90` binds the complete-context atomic landing correction to independently approved exact pre-evidence Git diff SHA-256 `b25aef908d13395fce59151e6e31a9fea2f11f788fdd2806d17fa378b99d8821`; I1A, I2, and M1 are prepare-only, and T1 is the sole atomic landing authority. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-atomic-build-wiring-20260730-d3646785` binds the T1 build-wiring correction to independently approved exact two-document pre-evidence Git diff SHA-256 `d36467858da78500b5cc5bfa47dc350d932ac45bd91a0e8fd97aaa0cdeee58a5`; T1 owns the two additive driver source rows, I2 and M1 remain prepare-only, and W2 retains only post-T2C deletion and final wiring. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-membership-partitions-20260730-872a0791` binds the I2 preparation split to independently approved exact two-document pre-evidence Git diff SHA-256 `872a079129e4f8b74967169252d394a099f3ee9edb02aa4a08e042a25cc000de`; I2A through I2G are sequential symbol-level prepare-only partitions over the same four files, I2 is their review-only join, and T1 remains the sole landing authority. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-parameter-authority-20260730-bf68f2a5` binds exact contextual implementation, generic-parameter, and callable-parameter authority inputs plus the corrected verifier boundaries to independently approved exact two-document pre-evidence Git diff SHA-256 `bf68f2a58bfaf000a11be8e5e06ab12fc44e3e9e45f0baf43a9045bb9e8821e6`; I2E/F/G remain prepare-only and T1 remains the sole atomic landing authority. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-materializer-context-20260730-4c760c36` binds the materializer context, unique revision and fingerprint construction, production inventory, provenance read, and canonical rejection-order correction to independently approved exact two-document pre-evidence Git diff SHA-256 `4c760c36bfab2a8b977b2185efb99a885a0513ffdf6ac73dfc02418897b3348a`; I2 and M1 remain prepare-only, T1 remains the sole atomic landing authority, and T2A retains production resource integration. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-membership-comparison-20260730-50920a7e` binds descriptor-owned complete canonical authority comparison to independently approved exact two-document pre-evidence Git diff SHA-256 `50920a7e5e0aa1cb60b29b96e541c5e48ce9c015a1b7e37b51aaa597ac23b058`; I2 owns the eight comparison implementations, T1 owns runtime use and mutation coverage, and both remain prepare-only before the sole atomic landing. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-materializer-partitions-20260730-2038e76b` binds the sequential M1A through M1E preparation split to independently approved exact two-document pre-evidence Git diff SHA-256 `2038e76b96a580da827456c466d7c1a0ec9aff67b133a26fe040a3e197f46df8`; M1 is a review-only join, every partition remains prepare-only, and T1 remains the sole atomic landing authority. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-materializer-pimpl-20260730-ef91a1a3` binds the M1A1 through M1A3 Pimpl preparation split to independently approved exact two-document pre-evidence Git diff SHA-256 `ef91a1a3a81ef9d8ed84335bc3f9e0629b8b1442d302a55972bd210fdeca56a3`; M1A remains a review-only join, every partition remains prepare-only, and T1 remains the sole registration, build, test, final-publication, and landing authority. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-materializer-provider-20260730-233922df` binds the M1C1 through M1C3 provider preparation split to independently approved exact two-document pre-evidence Git diff SHA-256 `233922df720e961d72a3fd2700885896bf6c19d35e41db09f99690c0a4278b47`; M1C remains a review-only join, every partition remains prepare-only, and T1 remains the sole registration, build, test, final-publication, and landing authority. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-materializer-acquisition-20260730-b29b5965` binds the M1C1A and M1C1B acquisition preparation split to independently approved exact two-document pre-evidence Git diff SHA-256 `b29b596572a00266acd539f7846421ba44136b7b0cf6579f0b2a1a7b493e5cd5`; M1C1 remains a review-only join, every partition remains prepare-only, and T1 remains the sole registration, build, test, final-publication, and landing authority. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-materializer-verifier-20260730-d44bcd5a` binds the M1D1 through M1D3 verifier preparation split to independently approved exact two-document pre-evidence Git diff SHA-256 `d44bcd5ac164f61355507f710886ec645aab6a0c498b6bf43a9a442d35231251`; M1D remains a review-only join, every partition remains prepare-only and independent from provider helpers, and T1 remains the sole registration, build, test, final-publication, and landing authority. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-final-seal-test-boundary-20260730-f6c04155` binds the generic-versus-production final-seal test responsibility correction to independently approved exact four-document pre-evidence Git diff SHA-256 `f6c041551684ac722a7b4e12682d963f65f01cd4557cc06ca0faaa5f07879437`; both same-name shadows are deleted, the generic runtime fixture is the unique first test-only descriptor, production integration retains the real authority and witness, and no source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-transaction-ownership-20260730-d0979738` binds the T1 transaction-owner scope correction to independently approved exact four-document pre-evidence Git diff SHA-256 `d0979738a664312a018922acc7d13fe8aa3fb5efe705c806cc3cef58a3ef7539`; the live core transaction owner and native test enter T1, direct closed replacement and same-revision witness publication are mandatory, and no source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-transaction-witness-inventory-20260730-ddd640c8` binds the static transaction-witness inventory correction to independently approved exact four-document pre-evidence Git diff SHA-256 `ddd640c83235ff8d178b615f8a532f7179588b21d477ae58fe293f0ba5e87b60`; three production input descriptors occupy ordinals 56 through 58, the generic test fixture begins the contiguous test tail at ordinal 59, and the descriptor generator owns the complete negative matrix. No source task is completed. |
| 2026-07-30 | ACCEPTED | Transaction `rfc0027-transaction-callers-20260730-490a96eb` binds the complete production mutation-caller scope correction to independently approved exact four-document pre-evidence Git diff SHA-256 `490a96eba8bbb8b8b1f96008c864fd9d1eb5ef2781771385e2ce74682d57b5cf`; all three compiler-session callers enter T1, the authority refresh path is replaced directly, T2A ownership remains unchanged, and no source task is completed. |
| 2026-08-07 | IMPLEMENTING | Audit confirmed the R27-18 through R27-32A implementation chain in atomic landings `6fac75b4` and `eb1033ef`, current 10-target sanitizer coverage, the full 257-test default CTest matrix, fixed-report verification, and strict Linux Release comparison. RFC 0025 retains independent incomplete tasks, so this transition makes no LANDED claim for the synchronized RFC set. |
