# RFC 0058 Review Record: Capturing Closure Unboxed Pair and Closure Identity

## Discussion Record

### Draft under Tech Lead decision D-001 (2026-11-03)

Drafted design-first on direction that capturing closures follow a minimal
vertical slice within the existing RFC 0057 boundaries and behind a feature
flag, without changing Work limits or regenerating planning artifacts.

Key measured input carried over from RFC 0057 slice 1:

- A closure has no `identity::DefId`; it is a dense per-owner
  `AnonymousOwnerLocalId`. `MirFunction.owner` and the Source call edge are
  `DefId`-typed, so the content-addressed closure identity is the floor for
  both the non-capturing and capturing slices.
- The capture fact model already exists (`CheckedCaptureFact` with
  `SharedReference`, `MutableReference`, `Move`, `Copy` and Explicit/Inferred
  origins) but has no producer or backend consumer.
- `Move` and reference captures depend on the incomplete RFC 0007 ownership
  overlay; escaping closures depend on the allocator ABI. The minimal slice is
  therefore frame-relative, non-escaping, by-value scalar capture only.

The DRAFT proposes Part A (closure DefId identity) plus Part B (flagged
unboxed code/environment pair for `Copy` scalar capture).

### Part A timing constraint (measured)

Closure DefIds cannot be interned at skeleton identity materialization:
`MaterializedModuleSkeletonIdentities::from` sees only module-level
`skeleton.declarations()` and the `bodyOwners()` keys, while the
`StableClosureFact`s live inside each owner body's `BoundOwnerBody`, which is
materialized after the skeleton it depends on. Interning happens in
`VerifyBoundModule::provide` (fetch interner via
`context.semanticContextResources<ModuleGraphIdentityMaterializationResources>()`),
which enumerates every materialized owner body; the DefIds thread into
`VerifiedBoundModule::from` and then `ImmutableDefinitionInventory::from`.
The owner-body capability witness encodes only stable evidence, so no
capability schema change is required: `VerifyBoundModule::verify` re-derives
the same closure DefIds deterministically and compares.

Implementation shape (function level):

1. `closureSyntheticName(path)` builds
   `"__closure_" + underscore-joined decimal path components` and wraps it in
   `DeclaredDefinitionName::fromSource`.
2. For each owner body and each `stableWitness.closures().values()`, read
   `closure.closure()` (AnonymousOwnerLocalKey). Owners chain: for a Definition
   owner clone the owner definition record's `record().owners()` and append
   `EnclosingStableOwnerKey::definition(owner)`; for a Module owner use an empty
   chain. Build `DefinitionIdentityRecord::from(module, owners, Closure, Value,
   syntheticName, none)` and `interners.internDefinition`; reject on
   none/duplicate.
3. Thread the ordered list (owner-body order then stable closure order) as a
   new parameter of `VerifiedBoundModule::from` and
   `ImmutableDefinitionInventory::from`; append one
   `MaterializedDefinitionInventoryEntry` per closure DefId at the closure
   expression node (DeclarationDefinitionSite) and extend the
   `definitions.size() == identities.definitions().size()` gate (~line 175) to
   `+ closureDefIds.size()`, preserving `definitionNodes` uniqueness.
4. `VerifyBoundModule::verify` independently rebuilds and requires identity
   equality. The anonymous key mapping is unchanged for capture facts.

## Owner Reviews

| Owner | Decision | Date |
|---|---|---|
| binder-checker | Pending | |
| ir-backend | Pending | |
| runtime-memory | Pending | |
| verification | Pending | |
| rfc | Pending | |

## Decision Record

Pending owner review. Author scope: content-addressed closure identity and a
default-off non-escaping by-value scalar-capture slice; reference/move
captures, escaping closures, callable traits, and generic closures deferred.

## Implementation Tracker

| Part | Change | Status | Verification |
|---|---|---|---|
| A0 | Closure Value-namespace header-less identity record admission | Landed (a3a06714) | key-definition-key-test 15; behavior-preserving (no producer yet) |
| A1 | Intern one closure DefId per StableClosureFact in the owner-body provider (`MaterializeOwnerBody::provide`, fetch `identityInterners` via `semanticContextResources`); synthetic name from `AnonymousOwnerLocalKey.path()` (e.g. `__closure_0_3`); owner chain from `key.owner()` | Next | identity interning ztests (same-source convergence, sibling divergence, duplicate/missing rejection) |
| A2 | Thread closure DefIds from `VerifyBoundModule::provide` through `VerifiedBoundModule::from` into `ImmutableDefinitionInventory::from`; append Closure `MaterializedDefinitionInventoryEntry` rows at the closure node; extend the ~line 175 size gate by the closure count and keep node uniqueness; verify independently re-derives | Next | materialized-module-skeleton/identity-membership tests, inventory count and node-uniqueness tests |
| A3 | Re-record corpus IR/process parity and full sanitizer ctest over the new closure definition surface | Next | byte-parity both channels, lit + unittest |
| B | Flagged unboxed code/environment pair for Copy scalar capture; HIR/MIR/LIR environment lowering; native exit fixture | Proposed - not started | checker/HIR/MIR/LIR ztests, capturing-closure-consumer, conformance pos/neg, parity |
| Later | Reference/move captures | Blocked | RFC 0007 ownership completion |
| Later | Escaping/boxed closures | Blocked | allocator ABI |
| Later | Callable traits, generic closures | Not scheduled | trait dispatch; RFC 0056 instances |
