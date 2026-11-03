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
materialized after the skeleton it depends on. Interning therefore happens in
`MaterializeOwnerBody::provide` / `MaterializedOwnerBody::from` (which can
fetch `identityInterners` through
`context.semanticContextResources<ModuleGraphIdentityMaterializationResources>()`,
the same pattern as the skeleton provider), and the resulting DefIds flow into
the module inventory at `VerifiedBoundModule::from`.

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
| A2 | Carry closure DefIds on the MaterializedOwnerBody capability (canonical schema), merge into `ImmutableDefinitionInventory` Closure entries at `VerifiedBoundModule::from`; relax/extend the definitions size and order assertions (owner then anonymous-local order); retain AnonymousOwnerLocalId mapping for capture facts | Next | stable-witness codec, materialized-module-skeleton/identity-membership tests, schema counts |
| A3 | Re-record corpus IR/process parity and full sanitizer ctest over the new closure definition surface | Next | byte-parity both channels, lit + unittest |
| B | Flagged unboxed code/environment pair for Copy scalar capture; HIR/MIR/LIR environment lowering; native exit fixture | Proposed - not started | checker/HIR/MIR/LIR ztests, capturing-closure-consumer, conformance pos/neg, parity |
| Later | Reference/move captures | Blocked | RFC 0007 ownership completion |
| Later | Escaping/boxed closures | Blocked | allocator ABI |
| Later | Callable traits, generic closures | Not scheduled | trait dispatch; RFC 0056 instances |
