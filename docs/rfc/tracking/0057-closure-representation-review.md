# RFC 0057 Review Record: Closure Representation and Non-Capturing Closures

## Discussion Record

### Round 1: empirical scope survey (2026-11-03)

Measured before drafting:

- parser: anonymous functions, generic closures, `use [x, &y, this]`, `raises`;
- binder: `DefinitionKind::Closure`, inferred free-variable and explicit
  capture facts (`ByValue` / `ByReference` / `This`);
- checker: `CheckedCaptureFact` with `CaptureMode { SharedReference,
  MutableReference, Move, Copy }`; closure bodies resolve capture requirements;
- HIR/MIR/LIR: no closure value, no function-address rvalue; LIR
  `IndirectCall` models only the dyn dispatch shape (interface/method/receiver);
- empirical probes: `let f = fn(n: i32) -> i32 { return n; }; f(42)` drains
  ZOM4099 at the lambda, and a call through a function-typed parameter drains
  ZOM4099 at the call.

Conclusion recorded in
[`docs/design/language/closure-representation-decision-points.md`](../../design/language/closure-representation-decision-points.md):
slice 1 is bare code pointer for non-capturing direct-called closures; the
unboxed code/environment pair and heap escape promotion are later slices.

## Owner Reviews

| Owner | Decision | Date |
|---|---|---|
| binder-checker | APPROVE (2026-11-03) | 2026-11-03 |
| ir-backend | APPROVE (2026-11-03) | 2026-11-03 |
| runtime-memory | APPROVE (2026-11-03) | 2026-11-03 |
| spec-audit | APPROVE (2026-11-03) | 2026-11-03 |
| verification | APPROVE (2026-11-03) | 2026-11-03 |

Round 1 review basis (agent owner review per the repo's owner-approval
contract; no human reviews fabricated):

- **binder-checker**: the checker gate uses only existing facts
  (`DefinitionKind::Closure`, empty inferred + explicit capture sets,
  structural `FunctionTypeData`); no binder schema change is needed for slice
  1. The callable-value call shape mirrors the admitted generic-call
  positions (direct return / single-local initializer), so the position
  matrix stays consistent.
- **ir-backend**: a new MIR function-address value is the minimal primitive
  and an additive codec tag; the plain code-pointer indirect call does not
  alter the existing dyn-dispatch terminator semantics when implemented as a
  parallel minimal form. LIR already resolves global symbols. Accepted with
  the implementation-time requirement that the choice between extending
  `MirIndirectCallTerminator` and a parallel terminator minimize the dyn
  validator surface.
- **runtime-memory**: slice 1 performs zero allocations and needs no runtime
  ABI change; the allocator dependence appears only at the explicitly
  deferred escape slice. Accepted.
- **spec-audit**: closure syntax is already specified and parsing today;
  slice 1 changes semantic admission only, with the rejection boundary stated
  per construct. No grammar/spec chapter change is required for slice 1; the
  admitted-behavior sentence is required with the implementation.
- **verification**: the plan independently re-derives callee ownership and
  pointer slot carriers, adds a malformed-bytes codec rejection, and keeps
  fail-closed behavior for every listed boundary. Accepted.

## Decision Record

Accepted 2026-11-03 by agent owner approval on all five axes. Slice 1 is the
non-capturing direct-called closure via a bare function pointer as specified;
capturing closures (unboxed code/environment pair), callable traits, escaping
closures/function-typed parameters, and generic closures are follow-up
slices, with mutable/move captures blocked on RFC 0007 and escaping blocked
on the allocator ABI. Implementation begins in a fresh focused session given
the cross-layer codec and calling-convention surface.

## Spec Change Record

Pending spec audit; the closure syntax is already specified and parses today,
so slice 1 changes semantic admission (checker/backend), not grammar.

## Implementation Tracker

| Slice | Change | Status | Verification |
|---|---|---|---|
| 1 | Non-capturing annotated lambda value (function pointer), direct in-body call; MIR function-address value and code-pointer indirect call; LIR function symbol materialization and plain indirect call; native exit | Proposed - not started | checker/HIR/MIR-codec/LIR/LLVM ztests, non-capturing-closure-consumer native/object cases, conformance pos/neg, parity, full sanitizer ctest |
| 2 | Unboxed code/environment pair; by-value scalar captures at call-site-local closures | Not scheduled | depends on slice 1 |
| 3 | Shared/mutable-reference and move captures | Not scheduled | blocked on RFC 0007 ownership completion |
| 4 | Escaping closures and function-typed parameters | Not scheduled | blocked on allocator ABI / backend general lowering |
| 5 | Generic closures through RFC 0056 instances; callable traits | Not scheduled | |
