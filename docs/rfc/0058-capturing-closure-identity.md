---
rfc: 58
title: Capturing Closure Unboxed Pair and Closure Identity
type: compiler
status: DRAFT
author: ZOM Compiler Team
review-manager: ZOM Compiler Team
required-owners: [binder-checker, ir-backend, runtime-memory, verification, rfc]
approvers: []
created: 2026-11-03
updated: 2026-11-03
area: compiler
requires: [7, 55, 56, 57]
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0058-capturing-closure-identity-review.md
decision: TBD
implementation: TBD
tracking-issue: TBD
---

# RFC 0058: Capturing Closure Unboxed Pair and Closure Identity

## Summary

Admit a call-site-local closure whose body refers to an outer variable, using
Rust's unboxed code/environment pair. The change is delivered in two ordered
parts: a content-addressed closure identity layer that gives every closure
expression a stable `DefId` (also the missing layer 0 of RFC 0057 slice 1),
then a minimal by-value scalar-capture slice behind a default-off feature
boundary. Reference and move captures stay blocked on RFC 0007; escaping
closures stay blocked on the allocator ABI. The minimal slice allocates
nothing and reuses the RFC 0057 direct-call edge.

## Motivation

RFC 0057 slice 1 delivers only the non-capturing closure:

```zom
fn entry() -> i32 {
    let x: i32 = 40;
    let f = fn() -> i32 {
        return x + 2; // captures x; rejected as ZOM4099 today
    };
    return f();
}
```

Implementing slice 1 exposed a false premise in its prerequisite map. A
closure does not carry an `identity::DefId` today:

- `DefinitionKind::Closure` (`0x15`) is a non-stable kind;
  `definitionNamespaceFor` returns none and `DefinitionIdentityRecord::from`
  rejects it, so no definition key or handle is interned.
- binder routes closures into `anonymousEntities` with a dense per-owner
  `AnonymousOwnerLocalId`, not a `DefId`.
- `mir::MirFunction.owner` and `mir::MirCallableIdentity::Source` are
  hard-typed to `identity::DefId`, and `hir::HirBuilder::build` has no access
  to an interner. The reused Source edge therefore cannot name a closure.

A capturing closure additionally needs an environment: storage for the outer
variables its body reads, without allocating when it never escapes the
defining frame. The capture fact model already exists
(`CheckedCaptureFact`, four modes, two origins) but has no producer or
backend consumer. This RFC supplies the identity floor and the smallest safe
environment slice, gated so ownership-dependent capture modes fail closed.

## Goals

- Give every closure expression a deterministic, content-addressed `DefId`
  available by binder/driver materialization, shared by RFC 0057 slice 1.
- Represent a non-escaping capturing closure as an unboxed
  `{ code, environment }` pair with a frame-relative environment pointer and
  zero heap allocation.
- Admit, behind a default-off feature boundary, call-site-local direct-called
  closures capturing `Copy`-admitted scalar locals by value.
- Keep the checker/HIR/MIR/LIR capture set and environment layout in exact
  agreement, independently verified at each layer.
- Leave reference, move, escaping, generic, and trait-object closures
  explicitly rejected with unchanged diagnostics.

## Non-Goals

- `Move` and mutable/shared-reference captures; their soundness requires the
  completed RFC 0007 ownership overlay.
- Escaping closures and heap environment promotion; these require the
  allocator ABI.
- The `Fn`/`FnMut`/`FnOnce` trait hierarchy and trait-object dispatch.
- Generic closures through the RFC 0056 monomorphization pipeline.
- The standalone function-address rvalue and code-pointer indirect call (RFC
  0057 slice 1.5), except where the pair representation requires an
  environment operand.

## Prior Art

- **Rust** closures: an unboxed pair of a monomorphized code item and an
  environment struct; capture modes `Fn`/`FnMut`/`FnOnce` gate shared,
  mutable, and by-value use; non-escaping closures monomorphize to direct
  calls with no allocation. This RFC copies the pair and the mode-gated
  admission, restricting the first slice to the by-value scalar case.
- **Swift** closure context pointers: a leading context parameter whose
  layout is private to the closure. Mirrors the leading environment parameter
  local chosen here, without Swift's escaping/retention conventions.
- **C++** stateless lambdas: a value convertible to a bare function pointer.
  RFC 0057 slice 1 already takes the zero-cost stateless form; the pair only
  appears when a capture exists.
- **Go** escaping closures heap-allocate an environment record. Deliberately
  not copied: call-site-local closures here never allocate; heap promotion is
  a later escape-proven slice behind the allocator ABI.

## Guide-Level Explanation

A closure that reads an outer local captures it. In the first capturing
slice only the simplest form is accepted: a closure declared and called
inside one function, never returned or stored, that copies scalar locals
(such as `i32`, `i64`, `bool`) by value.

```zom
fn entry() -> i32 {
    let x: i32 = 40;
    let f = fn() -> i32 {
        return x + 2;
    };
    return f(); // 42
}
```

Conceptually the closure value is two words: the address of the anonymous
closure function and a pointer to its environment. Because the closure never
leaves `entry`, its environment is simply a view into `entry`'s stack frame,
so nothing is allocated. The closure body reads `x` from its environment at
a fixed slot.

Everything else keeps today's clear rejection. Capturing by reference or by
move, returning or storing the closure, passing it as an argument, making it
generic, or annotating `raises` all report the existing unsupported-construct
diagnostic until their respective later slice. Disabling the feature flag
restores RFC 0057 slice-1 behavior exactly.

## Reference-Level Design

### Part A: content-addressed closure identity

`DefinitionIdentityRecord::from` admits `DefinitionKind::Closure` in
`DefinitionNamespace::Value` with no overload header (the existing
callable/overload-presence condition is extended so Closure is the one
Value-namespace header-less kind). The record owner chain is the closure's
`AnonymousOwnerLocalKey.owner()`; the name is a deterministic
canonical-scalar-valid synthetic identifier derived from
`AnonymousOwnerLocalKey.path()` components (for example `__closure_0_3`),
lowercase/underscore/digit only. It is identity-bearing, never user-facing.

Interning runs during owner-body materialization
(`MaterializedOwnerBody::from` / `ImmutableDefinitionInventory::from`), which
already holds the identity interner and the `StableClosureFact`s. One closure
`DefId` is interned per `StableClosureFact` and merged into the module's
materialized definition inventory as a `Closure` entry at the closure
expression node (`DeclarationDefinitionSite`). The dense
`AnonymousOwnerLocalId` mapping is retained: capture and free-variable facts
continue to key on the anonymous key, while definition ownership and call
edges use the `DefId`. The materialized-inventory size assertion is relaxed
to include closure `DefId`s in deterministic order (owner then
anonymous-local order).

### Part B: non-escaping by-value scalar capture

The closure value is a concrete two-slot carrier `{ code: DefId, env }`. The
structural `FunctionTypeData` remains the call signature; the pair is the
value representation and is not a user-facing generic or trait. The
environment field set is canonical syntax-path order and is part of the
capture identity, so two closures with different capture sets never share a
carrier.

HIR gives the closure body a leading environment parameter; captured reads
project a fixed field from it instead of resolving an owner local. The
direct callee-position call passes the environment as an implicit leading
context argument. The `returnsClosureAliasCall` groundwork shape extends to
the zero- and one-capture cases and still allocates no standalone slot on the
non-capturing path.

MIR makes the closure an ordinary post-elaboration `MirFunction` owned by the
closure `DefId` with a leading environment parameter local. The caller, for
the non-escaping case, materializes a frame-relative environment pointer. The
call remains `MirCallableIdentity::Source` named by the closure `DefId`; no
code-pointer `IndirectCall` and no `FunctionAddress` rvalue are required for
this slice.

LIR emits the closure symbol like a named function with one extra leading
pointer parameter and projects the captured scalar slots from it. The zomc
Source lowering ladder enumerates the closure function by `DefId` and derives
a symbol, as for the non-capturing slice.

### Invariants

1. Identity determinism: identical closure source interns the same `DefId`;
   distinct sibling sites intern distinct `DefId`s.
2. Exactly one `DefId` per closure expression; duplicate or missing
   interning rejects the owner-body capability.
3. Capture-set agreement across checker facts, closure value, and
   environment layout (canonical field order).
4. Mode gate: only `Copy` scalar carriers lower; `Move`/`MutableReference`/
   `SharedReference` drain until RFC 0007 completes.
5. No escape without proof: return, struct/array storage, function-typed
   parameters, and arguments remain rejected.
6. Zero allocation for call-site-local closures.
7. Generic, raising, ABI, and variadic closures always drain.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| Identity | `compiler/identity/key/definition-key.cc` (admit Closure in Value namespace, header-less), identity interner and canonical codec round-trip coverage | `binder-checker` |
| Binder/driver materialization | `compiler/binder/metadata/definition-inventory.cc`, `binder/graph/materialized-module-skeleton.cc`, `driver/query/module-graph/materialized-module-graph-query.cc` (intern closure DefIds from owner bodies, merge into definition inventory, relax size assertion, deterministic order) | `binder-checker` |
| Checker | `compiler/checker/body/body-checker.cc` (capture mode gate, capture-set production), `compiler/checker/inference/checked-facts.h` capture fact consumption, `compiler/checker/facts/signature-facts.cc` | `binder-checker` |
| Feature boundary | compiler-session feature flag plumbing, default-off capturing-closure gate | `ir-backend` |
| HIR | `compiler/hir/build/hir-shape.cc`, `hir-builder.cc`, `hir-module.cc`, `build/` (environment parameter, field projection, implicit context argument, independent verification) | `ir-backend` |
| Built MIR | `compiler/mir/build/**` (closure MirFunction owned by DefId, leading environment local, frame-relative env operand on the Source edge) | `ir-backend` |
| LIR/LLVM | `compiler/lir/mir-to-lir.{h,cc}`, backend/llvm, `utils/zomc/zomc.cc` (closure symbol with leading pointer parameter, environment projection, Source ladder enumeration) | `ir-backend` |
| Runtime memory | environment is a frame-relative view; no allocator ABI change and no new runtime surface; documents the zero-allocation boundary and the escape block | `runtime-memory` |
| Conformance and integration | capturing-closure-consumer native/object fixture, conformance positive and negative corpus with AST/grammar/diagnostics expectations, identity/checker/HIR/MIR ztests | `verification` |
| RFC process | this RFC, RFC 0057 layer-0 tracker update | `rfc` |

## Security And Safety Impact

Positive safety impact when complete: the mode gate makes unsound capture
forms unrepresentable in the lowered surface until the ownership overlay
exists, and every layer independently verifies the capture set, so a
mismatch cannot silently read the wrong slot. The environment pointer is
always frame-relative in this slice and never escapes, eliminating dangling
environment risk by construction; escape is rejected rather than promoted.
Implementing identity interning wrong could merge two closure sites and
mislink a call, which is why Part A requires deterministic synthetic names,
one-DefId-per-site checks, and canonical codec round-trip tests. No raw
pointer FFI, concurrency, or sandbox boundary changes.

## Drawbacks And Risks

- Extending the stable definition domain changes identity materialization
  counts and canonical encodings; a mistake in synthetic-name derivation or
  ordering can invalidate stable witnesses. Mitigated by content-addressed
  syntax-path names, deterministic ordering, and codec/count tests.
- Splitting identity between the anonymous key and the new `DefId` adds a
  mapping every capture consumer must respect. Mitigated by keeping capture
  facts on the anonymous key and reserving the `DefId` for ownership/calls.
- A two-word pair risks ABI mismatch between direct and future indirect
  calls. Mitigated by fixing the leading environment parameter convention now
  and validating it in the translation validator.
- The feature flag creates two admitted surfaces; tests must cover both the
  default-off (slice 1) and flagged (slice 2) behavior.

## Alternatives Considered

- Boxed environment for every capture (Go style). Rejected: forces an
  allocation on call-site-local closures and depends on the allocator ABI.
- Introduce `Fn`/`FnMut`/`FnOnce` traits now. Rejected: couples the value
  representation to a trait hierarchy before dispatch needs it; structural
  function types suffice for the direct-call slice.
- Reuse the dense `AnonymousOwnerLocalId` on the MIR call edge instead of a
  `DefId`. Rejected: `MirFunction.owner` and the Source edge are `DefId`-typed
  across the whole pipeline; a parallel identity would fork call resolution,
  codec, and translation validation.
- Session-local interning at HIR time. Rejected: the HIR builder has no
  interner and revision-local handles would break MIR/LIR identity
  bijectivity.

## Compatibility And Rollout

Internal, unversioned, pre-1.0 contracts: no compatibility shim, deprecation
marker, or persisted-data migration. Part A is additive to the definition
identity domain and leaves existing named definition keys unchanged; golden
identity fixtures and stable-witness codec tests update in the same change.
Part B is default-off; rollout is the feature flag flip after the vertical
slice and full gates are green. User-facing behavior is additive: programs
that drained before still drain unless they match the flagged capture shape.

## Documentation And Teaching Plan

- Update the closure chapter (`docs/spec/chapters/04-expressions.md`) with
  the admitted capture form, the two-word pair concept, and the rejection
  boundary; keep migration/changelog-style prose out of the spec.
- Add a `docs/design/language/` note describing the closure identity scheme
  and the frame-relative environment, linked from RFC 0057.
- The compiler-book-style explanation in this RFC's Guide-Level section is the
  teaching reference; the conformance fixtures are the executable examples.
- No paper or roster regeneration, per Tech Lead decision D-001.

## Operational Readiness

No new operational surface: no flag/metric beyond the internal compiler
feature boundary, no runtime service, no package or build-system change.
Operational readiness is the standard compiler gate: sanitizer build, format
check, full lit and unittest suites, native integration tests, and parity
re-recording. Part B merges inert behind the default-off flag, so it can ship
without changing default behavior.

## Acceptance Criteria

- Every closure expression resolves to a deterministic `DefId`; slice 1 and
  slice 2 builds share it.
- With the flag on, the capturing fixture compiles and `zomc run` exits with
  the expected value; the environment is frame-relative (no allocation).
- With the flag off, behavior is byte-identical to RFC 0057 slice 1.
- Every rejected mode/escape/generic/raising form reports the existing
  unsupported diagnostic (no new internal incident path).
- Checker/HIR/MIR/LIR independently re-derive and agree on the capture set.
- Corpus IR and process parity re-recorded and byte-verified; full sanitizer
  ctest, object emission, and deterministic-object cases pass.

## Implementation Plan

1. Part A identity: namespace/record admission; synthetic name helper;
   owner-body interning and inventory merge; size/order assertion updates;
   identity and stable-witness tests.
2. Part B capture producer: checker mode gate and capture-set production
   behind the flag; capture-set agreement facts.
3. Part B lowering: HIR environment parameter and projection; MIR closure
   function and frame-relative env; LIR symbol and projection; zomc Source
   ladder closure enumeration.
4. Fixtures, conformance pos/neg, registration, docs, format.
5. Parity re-record and full sanitizer gates; commit locally; await Tech Lead
   confirmation before any push.

## Test Plan

- Identity ztests: same-source DefId convergence, sibling-site divergence,
  canonical encode/decode of the synthetic-name Closure record, duplicate and
  missing interning rejection.
- Checker ztests: admitted `Copy` scalar capture; rejected
  Move/mutable/shared/reference, escape, generic, and raising boundaries;
  capture-set equality.
- hir-module tests: environment projection and implicit context argument with
  independent owner/environment re-derivation; malformed capture rejection.
- MIR/LIR translation-validator tests: leading environment parameter, field
  projection reads, Source edge to the closure DefId; codec and revision-fold
  coverage.
- End to end: capturing-closure-consumer fixture, native-run expected exit,
  object emission and deterministic object, registered with the package
  harness.
- Conformance: positive flagged fixture plus negative fixtures per rejected
  mode/escape form, with AST `.check`, grammar `.yml`, and diagnostics
  `.check` expectations.
- Gates: `check-format.py`; IR and process corpus parity re-recorded and
  byte-verified on both channels; full sanitizer lit + unittest ctest. The
  completion gate is `zomc run` with the expected exit code, not `--check`.

## Open Questions

- Exact multi-capture field-set cap for the first flagged slice (one scalar
  versus a small bounded set), resolved during implementation against the
  environment layout verifier.
- Whether the environment pair gets a dedicated structural type tag now or
  only when slice 1.5 materializes it as a value, decided to minimize the new
  rvalue surface.
- Synthetic-name format final spelling (path join convention), fixed before
  Part A identity fixtures are recorded.

## Status History

| Date | Status | Explanation |
|---|---|---|
| 2026-11-03 | DRAFT | Proposed under Tech Lead decision D-001 as the design-first capturing-closure ADR; includes the closure DefId identity floor measured during RFC 0057 slice 1. |
