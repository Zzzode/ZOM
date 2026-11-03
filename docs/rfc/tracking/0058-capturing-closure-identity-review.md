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
| A | Closure content-addressed DefId (namespace/record admission, owner-body interning, inventory merge) | Proposed - not started | identity ztests, stable-witness codec, schema counts, full sanitizer ctest |
| B | Flagged unboxed code/environment pair for Copy scalar capture; HIR/MIR/LIR environment lowering; native exit fixture | Proposed - not started | checker/HIR/MIR/LIR ztests, capturing-closure-consumer, conformance pos/neg, parity |
| Later | Reference/move captures | Blocked | RFC 0007 ownership completion |
| Later | Escaping/boxed closures | Blocked | allocator ABI |
| Later | Callable traits, generic closures | Not scheduled | trait dispatch; RFC 0056 instances |
