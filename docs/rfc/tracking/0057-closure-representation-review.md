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
| binder-checker | pending | |
| ir-backend | pending | |
| runtime-memory | pending | |
| spec-audit | pending | |
| verification | pending | |

## Decision Record

Pending owner approval. Proposed: accept RFC 0057 slice 1 (non-capturing
direct-called closures via a bare function pointer) as specified, with
capturing/trait/escape work tracked as follow-up RFCs.

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
