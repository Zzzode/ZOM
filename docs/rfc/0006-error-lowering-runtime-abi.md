---
rfc: 6
title: Error Lowering And Runtime ABI
type: compiler
status: REVIEW
author: ZOM Compiler Team
review-manager: rfc
required-owners: [rfc, binder-checker, error-system, runtime-memory, spec-audit, verification]
approvers: []
created: 2026-07-08
updated: 2026-07-08
area: compiler
requires: [3, 5]
supersedes: []
superseded-by: []
discussion: docs/rfc/0006-error-lowering-runtime-abi.md#status-history
decision: TBD
implementation: TBD
tracking-issue: docs/rfc/0006-error-lowering-runtime-abi.md#acceptance-criteria
---

# RFC 0006: Error Lowering And Runtime ABI

## Summary

This RFC defines how ZOM lowers typed error unions, `?!`, `!!`, `raises`, and
panic boundaries after type checking. RFC 0005 owns the checker-side type rules:
`?!` is legal only when the residual error type fits the enclosing `raises`
set, and `!!` is legal only on error unions. This RFC owns the downstream
contract: the ABI shape of error unions, the control-flow lowering for
propagation and unwrap-or-panic, deterministic cleanup on early returns, and
the runtime boundary where panics are printed, caught, or aborted.

## Motivation

The checker can prove that `expr?!` and `expr!!` are well typed, but codegen
still needs an exact target-independent contract:

1. Which tag represents success and which tags represent recoverable errors?
2. Where are the tag and payload stored in an error-union value?
3. How does `?!` run destructors before returning an error from the enclosing
   function?
4. What does `!!` call on the error path, and what source information reaches
   the panic handler?
5. Which panic boundaries may unwind, abort, or convert a panic into a typed
   value?

Leaving these questions implicit would make each backend choose its own ABI,
which would break cross-module calls, FFI boundaries, tests, and future runtime
optimization. The design must be explicit before backend work starts.

## Goals

- Define a canonical in-memory ABI for structural error unions.
- Define target-independent IR lowering for `?!` and `!!`.
- Preserve normal RAII cleanup for `?!` early returns.
- Define the panic call boundary for `!!`, `panic!`, `todo!`, and
  `unreachable!`.
- Define how `panic = "abort"` and `panic = "unwind"` affect generated code.
- Define a stable metadata contract for source span, error payload, and
  backtrace capture at panic sites.
- Define verification gates for unit, lit, runtime, and ABI tests before this
  RFC can move beyond `DRAFT`.

## Non-Goals

- This RFC does not change the `?!`, `!!`, `raises`, or union syntax.
- This RFC does not change checker diagnostics `ZOM0460` or `ZOM0461`.
- This RFC does not implement the backend, IR builder, or runtime panic
  functions.
- This RFC does not define full borrow checking, move checking, or lifetime
  analysis.
- This RFC does not decide cross-module `CompilerSession` scheduling or module
  metadata serialization beyond the error-union ABI fields named here.
- This RFC does not define target-specific calling conventions for every CPU;
  it defines the target-independent lowering contract that target ABI lowering
  must preserve.

## Prior Art

Rust lowers `Result<T, E>` and `?` through ordinary enum layout, MIR
branching, and cleanup blocks. ZOM should copy the explicit success/error tag
branching and cleanup-block discipline, while avoiding Rust's nominal `Result`
requirement for the language surface.

Swift represents `throws` as an ABI-level error result convention distinct from
the normal return path. ZOM should copy Swift's rule that error propagation is
part of function type and call ABI, while keeping ZOM's source-level model as a
structural union so ordinary matching and assignment remain uniform.

Zig error unions (`T!E`) lower to a payload plus error code and make `try`
syntactic sugar for early return. ZOM should copy the zero-cost branch shape and
the rule that the happy path is explicit, while keeping ZOM's richer union
payloads for error values that carry data.

C++ exception ABIs define unwind personalities and cleanup landing pads. ZOM
should avoid hidden exception-table control flow for recoverable errors, but
must still define a panic unwind boundary for unrecoverable programmer faults
when a crate opts into `panic = "unwind"`.

Go returns ordinary values for errors and uses `panic`/`recover` for exceptional
faults. ZOM should copy the separation between expected errors and panic
faults, while rejecting untyped error values and implicit panic recovery as the
default programming model.

## Guide-Level Explanation

User-facing behavior does not change. A function that returns a value or an
error still writes `raises`, `?!`, `!!`, `match`, and explicit union types as
specified in Chapter 11.

```zom
fun read_config(path: str) -> Config raises IoError | ParseError {
    let text = fs::read_to_string(path)?!;
    parse_config(text)?!
}
```

After type checking, the compiler lowers this as a sequence of tag tests. Each
call result is a union value. Tag `0` means success. A non-zero tag means one of
the declared error variants. `?!` checks the tag:

- tag `0`: unwrap the success payload and continue;
- non-zero tag: run the same cleanup that an explicit `return` would run, then
  return the error union from the enclosing function.

`!!` is intentionally different. It means the author asserts the error branch
is impossible or accepts a panic if the assertion is wrong:

```zom
let cfg = read_config("dev.toml")!!;
```

This lowers to the same tag test, but the non-zero path calls the runtime panic
entry point with the source span and error payload. It never silently discards
the error.

## Reference-Level Design

### Pipeline Boundary

The post-checker pipeline treats `FunctionType.raises` and `UnionType`
canonicalization as already solved by RFC 0005. Lowering receives:

- typed AST nodes for calls, postfix operators, returns, matches, and
  destructors;
- `TypeEnv` entries for every expression;
- coercion records that introduce `T -> T | E`, `E -> T | E`, and `never -> T`
  conversions;
- source ranges for operator spans and panic metadata.

Lowering must not redo type inference. If required type information is missing,
it is a compiler invariant violation, not a user diagnostic.

### Error-Union ABI

Every canonical error union has a layout descriptor:

```text
ErrorUnionLayout {
  type_id: TypeId,
  tag_type: u8 | u16 | u32 | u64,
  tag_offset: 0,
  payload_offset: align_up(sizeof(tag_type), payload_align),
  payload_size: max(sizeof(alt_i)),
  payload_align: max(alignof(alt_i)),
  alternatives: [AlternativeLayout],
}

AlternativeLayout {
  tag: integer,
  type_id: TypeId,
  kind: success | error,
  payload_layout: LayoutId,
}
```

Tag `0` is always the unique success alternative. Error alternatives use
tags `1..n` in canonical union order. Canonical order is the same order used by
the type interner for `UnionType`; it must be stable across compilation units.
An error union with zero error alternatives is lowered as the success type
itself and has no tag.

The in-memory representation is:

```text
offset 0:             tag
offset payload_offset: payload storage for the active alternative
```

Padding bytes are unspecified. The active payload is initialized exactly once
and destroyed exactly once.

### Construction Lowering

When a value of alternative type `A` coerces to union `U`, lowering emits:

1. allocate destination storage for `U`;
2. write the canonical tag for `A`;
3. initialize the payload with the value, using move construction when the
   source value is owned;
4. mark the source as moved if ownership rules require it.

If `A` is the success type, the tag is `0`. If `A` is an error type, the tag is
the matching non-zero alternative. If multiple alternatives erase to the same
layout but have different type identities, tag assignment still follows type
identity, not layout identity.

### `?!` Lowering

For `expr?!` with type `T | E`, lowering emits target-independent control flow:

```text
tmp = lower(expr)
tag = load_tag(tmp)
if tag == 0:
  result = move_payload(tmp, success_alternative)
  destroy_inactive_union_shell(tmp)
  continue(result)
else:
  residual = move_payload(tmp, tag)
  out = construct_enclosing_error_union(residual)
  run_scope_cleanups_until_enclosing_function_return()
  return out
```

For a call that raises, the call result is already an error union; `?!` consumes
that union. For a plain union expression, the same lowering applies. For future
`Try` implementors, lowering first dispatches to the monomorphized
`Try::branch` path and then applies the same residual return shape.

The early-return edge must enter the function's normal cleanup graph. This is
the same cleanup graph used for explicit `return`, `break` out of scopes, and
fallible initialization failure. No backend may bypass destructors on the
recoverable-error path.

### `!!` Lowering

For `expr!!` with type `T | E`, lowering emits:

```text
tmp = lower(expr)
tag = load_tag(tmp)
if tag == 0:
  result = move_payload(tmp, success_alternative)
  destroy_inactive_union_shell(tmp)
  continue(result)
else:
  residual_ref = borrow_payload_for_panic(tmp, tag)
  panic_info = build_panic_info(operator_span, residual_ref, PanicKind::ForcedUnwrap)
  call __zom_panic(panic_info)
  unreachable
```

`__zom_panic` has return type `never`. Under `panic = "abort"`, it prints or
records panic information according to runtime policy and terminates the
process. Under `panic = "unwind"`, it starts unwinding after constructing
runtime panic metadata. If a target has no unwind support, `panic = "unwind"`
is rejected before codegen.

The panic path does not convert the residual into the enclosing `raises` set.
It is unrecoverable unless an explicit panic boundary such as `catch_unwind`
exists and the crate is compiled with unwind support.

### Cleanup And Drop Discipline

Every function lowering builds a cleanup stack. Each initialized local value
registers exactly one cleanup action unless it is moved. `?!` early return and
explicit `return` share the same cleanup unwinding path.

Cleanup order is lexical reverse initialization order. If a cleanup panics while
another panic is already unwinding, the runtime aborts. If a cleanup panics on a
recoverable-error `?!` early return, the panic supersedes the recoverable error
because the recoverable return cannot complete safely.

### Panic Boundary ABI

The runtime exposes these target-independent entry points:

```text
__zom_panic(info: PanicInfo) -> never
__zom_begin_panic_unwind(info: PanicInfo) -> never
__zom_abort_panic(info: PanicInfo) -> never
__zom_catch_unwind(fn_ptr, ctx_ptr, out_panic_info) -> bool
```

`PanicInfo` contains:

- panic kind: forced unwrap, explicit panic, unreachable, todo, assertion,
  bounds, overflow, runtime;
- source file, line, column, and byte span;
- optional message;
- optional borrowed debug view of the residual payload;
- optional backtrace handle;
- task/thread identity when the runtime has one.

`__zom_panic` dispatches to abort or unwind according to crate panic strategy.
`__zom_catch_unwind` is only meaningful under `panic = "unwind"`; under
`panic = "abort"` it is compiled as a call boundary that cannot catch.

### Main And Task Boundaries

At `main` return:

- `()` exits with code `0`;
- `T | E` with tag `0` follows the success exit path for `T`;
- `T | E` with a non-zero tag prints the error payload through `Error` or
  `Debug` formatting and exits with code `1`;
- an uncaught panic exits with code `101`.

Task boundaries inherit the same distinction: recoverable errors are typed
task results; panics are task faults. A scoped task may aggregate recoverable
errors according to the concurrency RFC, but a panic follows the panic strategy.

### FFI Boundary Rule

No panic may unwind across an `extern "C"` boundary. Lowering for exported C ABI
functions must either:

- prove the function cannot panic;
- insert `catch_unwind` and convert panic to the declared ABI error result; or
- reject the declaration until the function is annotated with an explicit
  panic boundary policy.

Recoverable error unions crossing FFI require an explicit `repr(C)` wrapper.
The structural union layout defined here is a compiler ABI, not a C ABI.

### Mermaid Lowering Diagram

```mermaid
flowchart TD
  A[Typed AST expr with T | E] --> B[Lower expression to union value]
  B --> C[Load canonical tag]
  C -->|tag 0| D[Move success payload]
  D --> E[Continue expression]
  C -->|tag non-zero and operator ?!| F[Move residual payload]
  F --> G[Construct enclosing error union]
  G --> H[Run normal return cleanups]
  H --> I[Return error union]
  C -->|tag non-zero and operator !!| J[Build PanicInfo]
  J --> K[Call __zom_panic]
  K --> L[Unreachable]
```

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| RFC governance | `docs/rfc/0006-error-lowering-runtime-abi.md`, `docs/rfc/README.md` | `rfc` |
| Error handling spec | `docs/spec/chapters/11-error-handling.md` | `error-system` |
| Type checker contract | `products/zomlang/compiler/checker/**`, `products/zomlang/compiler/type/**` | `binder-checker` |
| Runtime panic ABI | `products/zomlang/runtime/**`, `libraries/zc/**` | `runtime-memory` |
| Spec alignment | `docs/spec/**`, `docs/design/**` | `spec-audit` |
| Tests and verification | `products/zomlang/tests/**` | `verification` |

## Security And Safety Impact

This RFC affects safety directly. `?!` must run destructors on every early
return so resources are not leaked on expected failures. `!!` must surface panic
metadata and must not let a panic cross a C ABI boundary. The structural union
ABI must initialize and destroy exactly one active payload to avoid double-drop,
use-after-move, or leaked linear resources.

FFI boundaries are the main security-sensitive surface. A panic crossing an
unprepared C frame is undefined behavior, so backends must insert or require an
explicit panic boundary before exporting C ABI functions that can panic.

## Drawbacks And Risks

- A fixed tag-at-offset-zero ABI may be suboptimal for some targets compared
  with niche optimization. The benefit is deterministic lowering and easier
  conformance testing before the optimizer grows.
- Requiring `catch_unwind` policy for FFI exports adds implementation work to
  the backend and runtime.
- `panic = "unwind"` requires target support that may be unavailable on embedded
  and WebAssembly targets.
- The compiler must maintain a precise cleanup graph before codegen can safely
  implement `?!`.

## Alternatives Considered

- **Nominal `Result<T, E>` only.** Rejected because ZOM already specifies
  structural unions and `raises` sugar. Making `Result` nominal would duplicate
  the error model and force conversions at every boundary.
- **Hidden exception unwinding for `?!`.** Rejected because recoverable errors
  must be explicit, typed, and cheap on the happy path. Hidden unwinding would
  make expected errors behave like panics.
- **Backend-specific union layouts.** Rejected because cross-module code and
  conformance tests need a stable compiler ABI before target-specific ABI
  lowering is added.
- **Always abort on `!!`.** Rejected because development builds and FFI
  boundaries need an unwind-capable panic mode for controlled debugging and
  isolation.
- **C ABI exposure for structural error unions.** Rejected because structural
  union layout is a compiler ABI; FFI must use explicit `repr(C)` wrappers.

## Compatibility And Rollout

No existing source syntax changes. The rollout is an implementation sequence:

1. Add IR/layout data structures for canonical error-union ABI.
2. Lower union construction and extraction.
3. Lower `?!` to tag tests plus cleanup edges.
4. Lower `!!` to tag tests plus `__zom_panic`.
5. Add runtime panic entry points.
6. Add FFI panic-boundary enforcement.
7. Add conformance tests and ABI snapshots.

Rollback cost is moderate. If the ABI must change before `LANDED`, only backend
experiments and generated snapshots should be affected. Once cross-module
artifacts are emitted, layout changes require regenerating all ABI fixtures and
module metadata.

## Documentation And Teaching Plan

- Update Chapter 11 with any accepted wording that differs from this RFC.
- Add a design document for backend lowering once IR code exists.
- Add examples showing `?!` early return, `!!` panic, and FFI panic boundaries.
- Add diagnostics documentation for any new runtime or FFI boundary codes.
- Cross-link RFC 0005 so readers understand checker semantics and lowering are
  separate contracts.

## Operational Readiness

Before landing, CI must run the error-lowering conformance tests under both
abort and unwind panic strategies on supported host platforms. Runtime panic
printing must produce deterministic enough output for tests after stripping
absolute paths and backtrace addresses. Unsupported unwind targets must have a
clear configure-time or compile-time rejection path.

## Acceptance Criteria

1. Error-union layout descriptors exist and are deterministic for identical
   canonical union types.
2. Tag `0` is success and non-zero tags are error alternatives in canonical
   order.
3. `?!` lowering emits tag tests, success extraction, residual construction,
   cleanup edges, and an enclosing-function return.
4. `!!` lowering emits tag tests, success extraction, panic metadata, a call to
   `__zom_panic`, and an unreachable terminator.
5. Explicit return and `?!` early return share the same cleanup graph.
6. Runtime panic entry points exist for abort and unwind strategies.
7. `catch_unwind` behavior is defined under both abort and unwind strategies.
8. FFI exports cannot let panic unwind through C ABI frames.
9. `main` returns exit code `0` on success, `1` on recoverable error, and `101`
   on uncaught panic.
10. Tests cover nested local cleanup on `?!` and panic during cleanup.
11. Tests cover union ABI tag/payload layout for scalar, aggregate, and
    zero-sized alternatives.
12. Tests cover `!!` source-span metadata and payload debug metadata.
13. Tests cover panic strategy selection and unsupported unwind targets.
14. `python3 scripts/check-rfc.py` passes.
15. `python3 scripts/check-format.py` passes after implementation changes.
16. `ctest --preset default --output-on-failure` passes before `LANDED`.

## Implementation Plan

1. Add backend-independent layout descriptors for canonical union types.
2. Add IR nodes or lowering helpers for union construction, tag load, payload
   move, and payload borrow.
3. Build function cleanup graphs before lowering returns and postfix error
   operators.
4. Lower `?!` using the cleanup graph.
5. Lower `!!`, `panic!`, `todo!`, and `unreachable!` through `PanicInfo` and
   `__zom_panic`.
6. Add runtime panic ABI entry points in `products/zomlang/runtime/**` and any
   needed `zc` support.
7. Add FFI export checks and `catch_unwind` lowering.
8. Add ABI and runtime conformance tests.
9. Update Chapter 11 and backend design docs.
10. Move this RFC through REVIEW, ACCEPTED, IMPLEMENTING, and LANDED only after
    the corresponding process gates are satisfied.

## Test Plan

- Build: `cmake --preset sanitizer` and `cmake --build --preset sanitizer -j`.
- Unit tests: backend-independent layout tests for error-union tag and payload
  offsets; cleanup graph tests for early returns; runtime tests for panic entry
  points.
- Lit tests: `.zom` fixtures for `?!` lowering, nested cleanup, `!!` panic
  metadata, `main` recoverable-error exit, and FFI panic-boundary rejection.
- Conformance: diagnostics and AST tests continue to cover checker legality for
  `ZOM0460` and `ZOM0461`; new backend conformance covers emitted IR/ABI once
  the backend exists.
- Generated files: update any IR snapshot or ABI metadata snapshot generated by
  the implementation.
- Format: `python3 scripts/check-format.py`.
- RFC check: `python3 scripts/check-rfc.py`.

## Open Questions

- Should the first landed implementation include unwind support, or should
  `panic = "unwind"` remain rejected until the runtime unwinder is present?
- Which concrete test harness should own backend IR/ABI snapshots once the
  backend directory exists?

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-07-08 | DRAFT | Initial draft defining error-union ABI, `?!` lowering, `!!` panic lowering, cleanup discipline, and runtime panic boundaries. |
| 2026-07-08 | REVIEW | The proposal now has a complete backend/runtime ABI contract, ordered implementation plan, concrete acceptance criteria, and local discussion/tracking anchors. Approval remains blocked on owner review, non-empty approvers, a recorded decision, and follow-on backend implementation evidence. |
