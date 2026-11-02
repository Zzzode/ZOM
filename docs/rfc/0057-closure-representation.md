---
rfc: 57
title: Closure Representation and Non-Capturing Closures
type: compiler
status: ACCEPTED
author: ZOM Compiler Team
review-manager: rfc
required-owners: [binder-checker, ir-backend, runtime-memory, spec-audit, verification]
approvers: []
created: 2026-11-03
updated: 2026-11-03
area: compiler
requires: [10, 21, 53, 56]
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0057-closure-representation-review.md
decision: docs/rfc/tracking/0057-closure-representation-review.md#decision-record
implementation: docs/rfc/tracking/0057-closure-representation-review.md#implementation-tracker
tracking-issue: docs/rfc/tracking/0057-closure-representation-review.md#implementation-tracker
---

# RFC 0057: Closure Representation and Non-Capturing Closures

## Summary

Closure expressions already parse, bind as `DefinitionKind::Closure`, and
carry checker-validated capture facts. Nothing downstream exists: a closure
expression never reaches HIR, there is no closure or function-pointer value
in MIR or LIR, and both closure literals and calls through function-typed
parameters drain as ZOM4099. This RFC fixes the representation decision for
callable values and delivers the first vertical slice: a **non-capturing
closure assigned to a local and called directly in the same body**, which
lowers to a plain function-pointer materialization and a direct indirect
call with zero allocations. Later slices add capturing environments
(unboxed code/environment pair), callable traits, escaping closures,
reference and move captures, and generic closures through the RFC 0056
instance pipeline.

The non-normative decision input is
[`docs/design/language/closure-representation-decision-points.md`](../design/language/closure-representation-decision-points.md).

## Motivation

```zom
fn entry() -> i32 {
    let f = fn(n: i32) -> i32 {
        return n;
    };
    return f(42); // ZOM4099 today at both the lambda literal and the call
}
```

A non-capturing lambda is semantically an anonymous function and, when called
directly, is observationally indistinguishable from a named-function call.
The language already needs every layer this slice adds: the existing LIR
`IndirectCall` terminator models only the dynamic dispatch shape
(interface, method, receiver, argument bundle), and MIR has no rvalue that
produces a function code address. Without those primitives, callable values
cannot exist at all; with them, the direct non-capturing call is the smallest
end-to-end consumer that proves the representation.

This RFC deliberately does not introduce environments, traits, or
allocations. Every decision beyond the code pointer is sequenced so a later
slice cannot invalidate this one.

## Best-Practices Checklist

- **Surveyed industry winners.** Rust closures (`Fn`/`FnMut`/`FnOnce` with an
  unboxed code/environment pair and monomorphized direct calls), C++ stateless
  lambdas (a value that converts to a bare function pointer), Go escaping
  closures (a heap-allocated environment record), and Swift context
  pointers. The staged plan copies Rust's representation for eventual
  capturing closures and C++'s zero-cost stateless form for slice 1.
- **Checklist per language feature.**
  - *Problem solved*: callable values in the language cannot be built or
    invoked; both drain.
  - *Pitfalls*:
    1. Forcing an allocation for a non-escaping closure. Avoided: slice 1
       carries no environment at all; later slices keep the unboxed pair and
       allocate only on escape.
    2. Coupling callable types to a trait hierarchy before dispatch needs it.
       Avoided: slice 1 uses the existing structural `FunctionTypeData`.
    3. Inventing a second specialization path for generic closures. Avoided:
       later generic closures reuse the RFC 0056 instance pipeline.
  - *Bugs and risks*: function-pointer type identity across modules (a code
    pointer is a concrete `fn(...)` type, not an existential); call argument
    ABI mismatch between direct and indirect call lowering; capture soundness
    is explicitly out of scope and remains blocked at HIR.
- **Open question resolution**: see Decision Points; the owner approves the
  staged representation before implementation.

## Representation Decision

A callable value has two eventual parts: a code pointer and an environment
pointer. This RFC commits only to the code pointer.

1. **Slice 1 (this RFC): no environment.** A non-capturing closure value is a
   bare code pointer whose type is the structural function type
   (`FunctionTypeData { parameters, success, raises }`). No aggregate, no
   allocation, no trait.
2. **Later: unboxed pair.** Capturing closures become a concrete two-slot
   value `{ code pointer, environment pointer }`, with the capture mode
   determining borrow requirements. This RFC reserves that direction but
   changes no carrier for it.
3. **Later: boxed escape.** A closure that outlives its defining frame is
   promoted to a heap environment only when escape is proven. This requires
   the allocator ABI from the backend work and is not scheduled here.

Call-site-local non-escaping closures will never allocate.

## Detailed Design

### Measured prerequisite map (2026-11-03 survey)

The layer boundaries were verified against the source before slice scoping:

- `signature-facts.cc` hard-excludes `DefinitionKind::Closure` from callable
  signature publication, and the body checker's `ownsExecutableBody` includes
  only `Function`, `Method`, `Constructor`, and `Destructor`. A closure is
  therefore not a callable today: it binds and accumulates capture
  requirements, but it gets no signature, no body check, no HIR, and no MIR
  function.
- Consequently the direct-called slice needs closure signature publication and
  closure body ownership before any call resolution matters. These are
  additive, but they are the real floor of the change; they are not a gate
  tweak.
- The chosen slice-1 call lowering deliberately adds no new MIR rvalue or
  terminator: because the admitted closure is non-escaping and callable only
  in callee position, its local binding resolves to the anonymous closure
  `DefId` and the call emits the existing `MirCallableIdentity::Source` direct
  call edge. The function-address value and code-pointer indirect call remain
  necessary for later slices where the closure is usable as a value, and are
  documented below so the early slices do not paint them into a corner.

### Checker

- A `LambdaExpression` with a declared (non-inferred) parameter and return
  type and a zero-capture body produces a value node typed with
  `FunctionTypeData` over its declared signature. The existing
  `DefinitionKind::Closure` requirement and the (empty) capture facts cover
  the body; the new gate admits the literal only when the inferred and
  explicit capture sets are both empty.
- A call whose callee resolves to an owner-local of structural function type
  produces a new callable-value call shape: argument count and per-argument
  types match the function type, the success type is the local's function
  type success, and the call is in the admitted positions (direct return or
  single-local initializer). Calls through function-typed *parameters* stay
  rejected in slice 1: they are escaping values and need the indirect-call
  ABI arguments below without an in-module callee to verify against.
- Capturing lambdas, generic lambdas, `raises` lambdas, and inference of an
  unannotated return type keep draining with the existing ZOM4099 behavior.

### HIR

- A closure value node references the anonymous closure owner and carries no
  captured slots. The HIR structural verifier gains the closure value shape
  (owner must be a `DefinitionKind::Closure` with zero captures) and the
  callable-value call shape (callee is an in-body owner local of function
  type whose owning closure is non-capturing).

### MIR

- For slice 1 no new rvalue or terminator is required: the admitted call site
  resolves its local binding to the anonymous closure `DefId` and emits a
  normal `MirCallableIdentity::Source` call edge, exactly as a named-function
  call does. The closure is an ordinary post-elaboration `MirFunction` keyed by
  its anonymous owner local key.
- Slice 1.5 (closure used as a value but still non-escaping, or passed
  locally) introduces a new rvalue kind `FunctionAddress`: one `ir::DefId`
  materializing a code address, with a new canonical codec tag, independent
  re-decode verifier slot, and revision fold. It pairs with a plain
  code-pointer indirect call: either a minimal form of
  `MirIndirectCallTerminator` carrying `{ pointer, arguments, destination,
  normalTarget }` or a parallel terminator chosen to leave the dyn-dispatch
  validator surface untouched.
- The closure-local assignment stores the function address only at slice 1.5;
  at slice 1 the binding exists for name resolution but carries no value slot
  in the caller.

### LIR and LLVM

- Slice 1 needs neither: the direct `Source` call edge lowers through the
  existing named-function call path, with the closure function symbol
  replacing a named callee symbol in the same positional two-function (or
  joined) module layout.
- Slice 1.5 adds a statement materializing a function symbol into an
  integer-pointer slot (an internal function pointer constant using the
  existing pointer carrier) and a plain indirect call over that slot without
  the dyn receiver/vtable prefix. The structural and translation validators
  gain function-address and plain-indirect-call matchers that independently
  re-derive the closure owner and slot carrier.

### Scope Boundaries

Admitted end to end in slice 1:

- non-capturing lambda, fully annotated signature, no `raises`;
- assigned to exactly one owner local in the defining body;
- called directly from that body in the direct-return or
  returned-local-initializer position;
- scalar carriers already admitted by the lowering ladders.

Rejected (unchanged user-facing behavior, ZOM4099):

- any capture (explicit or inferred), generic closures, `raises`;
- storing the closure in a struct/array/field, returning it, or passing it as
  an argument (escaping values);
- calling through a function-typed parameter;
- inferred unannotated closure return type.

## Verification Plan

- checker ztests for the admitted lambda value/call and each rejection arm;
- HIR module tests for the two new shapes;
- slice 1 reuses the existing MIR direct-call codec and LIR call path, so the
  new codec/terminator tests belong to slice 1.5;
- LIR translation-validator tests proving the direct edge resolves to the
  anonymous closure function and failing closed on owner mismatch;
- an LLVM translation test producing and calling the function pointer;
- a native integration fixture `non-capturing-closure-consumer` exiting 42,
  plus object-emission and deterministic-object cases;
- conformance positives/negatives under `12-generics`/closures with AST,
  grammar, and diagnostics expectations;
- full sanitizer ctest and the process/IR corpus parity gates.

## Alternatives Considered

- **Boxed environment for every closure.** Uniform convention but mandates an
  allocator ABI that does not exist and silently heap-allocates
  call-site-local closures. Rejected for slice 1, revisited for escapes.
- **Desugar non-capturing direct calls to named functions at the checker.**
  Hides the value semantics the language needs next and leaves function
  pointers unrepresentable, so every later slice reopens this one. Rejected.
- **Adopt callable traits immediately.** Correct endpoint, but the mode
  distinctions only matter once captures exist; pulling trait dispatch into
  this slice couples two independent systems. Deferred.

## Migration And Compatibility

No existing source changes behavior: closures are rejected today. New admitted
programs are strictly additive. Slice 1 adds no codec bytes; the function
address rvalue in slice 1.5 is a new tag on the extensible rvalue domain with a
revision fold, not a reinterpretation of existing bytes.
