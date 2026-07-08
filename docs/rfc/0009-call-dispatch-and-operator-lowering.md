---
rfc: 9
title: Call Dispatch And Operator Lowering
type: compiler
status: REVIEW
author: ZOM Compiler Team
review-manager: rfc
required-owners: [rfc, binder-checker, module-system, spec-audit, verification]
approvers: []
created: 2026-07-08
updated: 2026-07-08
area: compiler
requires: [4, 5, 8]
supersedes: []
superseded-by: []
discussion: docs/rfc/0009-call-dispatch-and-operator-lowering.md#status-history
decision: TBD
implementation: TBD
tracking-issue: docs/rfc/0009-call-dispatch-and-operator-lowering.md#acceptance-criteria
---

# RFC 0009: Call Dispatch And Operator Lowering

## Summary

This RFC defines the post-type-check contract that turns method calls,
qualified interface calls, ordinary function calls, and operator trait
selection into an explicit callable target model for later IR lowering. RFC
0005 proves that the operations are type-correct and that user-defined
operators map to the expected interface methods. This RFC owns the next layer:
which callable symbol each call site selects, how receiver passing is
normalized, how operator expressions become method-call lowering records, and
which call sites require dynamic dispatch through a `dyn` vtable.

## Motivation

The checker currently validates many operator and call semantics, but the
compiler still needs a durable lowering contract before IR/codegen can be
sound:

1. Does `x + y` lower to primitive arithmetic, a static `Add::add` impl call,
   or a dynamic call through a `dyn Add` vtable?
2. Which receiver argument is passed for `obj.method(arg)` and
   `Interface::method(obj, arg)`?
3. How does the compiler distinguish module-qualified function calls from
   interface-qualified method calls?
4. What metadata does later IR lowering consume so it does not repeat trait
   resolution or method lookup?
5. How are diagnostics kept stable when a call cannot select exactly one
   callable target?

Leaving these choices implicit would make backend lowering re-run semantic
analysis and would couple codegen to checker internals. The compiler needs one
explicit dispatch record per call-like expression.

## Goals

- Define a `CallTarget` model for function, static method, instance method,
  qualified interface method, operator method, primitive operator, index
  operator, and dynamic vtable dispatch.
- Define how receiver arguments are normalized for methods and operators.
- Define the operator-to-interface-method lowering table for `+`, `-`, `*`,
  `/`, `%`, `**`, `==`, `!=`, `<`, `<=`, `>`, `>=`, prefix `-`, `!`, and
  indexing.
- Define when a call target is selected by symbol binding, type-directed member
  lookup, trait resolution, or vtable layout.
- Define the side-table contract consumed by IR lowering.
- Define deterministic diagnostics for ambiguous, missing, or invalid call
  targets.
- Keep this layer separate from borrow checking, error lowering, and
  cross-module session scheduling.

## Non-Goals

- This RFC does not change expression syntax or operator precedence.
- This RFC does not change RFC 0005's type inference, trait solving, or
  object-safety rules.
- This RFC does not define the final IR instruction set or backend ABI.
- This RFC does not implement `CompilerSession`; cross-module target
  visibility and imported signature publication remain owned by RFC 0008.
- This RFC does not define borrow/lifetime checks for receivers or arguments;
  those checks are owned by RFC 0007.
- This RFC does not lower `?!` or `!!`; those operators are owned by RFC 0006
  after the checker accepts their type semantics.

## Prior Art

Rust separates type checking from later THIR/MIR construction. Method calls and
overloaded operators are resolved to concrete `DefId` targets before MIR
lowering. ZOM should copy the separation: type checking decides the callable
target once, and later lowering consumes a stable record instead of repeating
method lookup.

Swift lowers operator syntax through ordinary static functions declared by
protocol-constrained overload sets, while witness tables handle protocol
dispatch. ZOM should copy the explicit witness-table boundary for `dyn`
interface calls and avoid Swift's global overload ranking complexity.

Go uses static selector resolution for methods and interface table dispatch for
interface values. ZOM should copy the distinction between concrete receiver
calls and interface-value calls, while preserving ZOM's explicit `dyn` type
surface.

C++ overload resolution is expressive but complex and context-sensitive. ZOM
should avoid implicit conversion ranking and overload sets; each call site must
select exactly one callable target after RFC 0005 type checking.

Zig keeps operator semantics mostly primitive and explicit. ZOM should copy
Zig's preference for predictable lowering, but ZOM deliberately supports
operator interfaces for user-defined types, so the mapping table must be
specified.

## Guide-Level Explanation

Users do not write new syntax. They already write calls and operators:

```zom
let c = a + b;
let ok = point1 == point2;
let item = bag[0];
shape.draw();
Drawable::draw(shape);
```

After type checking, each expression has a dispatch classification:

- `i32 + i32` is a primitive arithmetic operation.
- `Number + Number` selects the `impl Add for Number { fun add(rhs: Number)
  -> Number; }` method.
- `point1 == point2` selects `Eq.eq(rhs: Point) -> bool`.
- `bag[0]` selects `Index.index(idx: i32) -> Output`.
- `shape.draw()` on a concrete `Sprite` selects the concrete method.
- `shape.draw()` on `dyn Drawable` selects a vtable slot.
- `Drawable::draw(shape)` selects the `Drawable` interface method statically
  and passes `shape` as the receiver argument.

When selection cannot produce one target, the compiler reports a diagnostic
before IR lowering starts.

## Reference-Level Design

### Dispatch Record

The checker publishes a side table keyed by expression `NodeId`:

```text
CallDispatchTable {
  NodeId expr;
  CallTarget target;
  ReceiverMode receiver;
  Array<TypeId> argumentTypes;
  TypeId resultType;
}
```

`CallTarget` is a tagged record:

```text
PrimitiveOperator(op)
FreeFunction(SymbolId)
StaticMethod(type: TypeId, method: SymbolId)
InstanceMethod(type: TypeId, method: SymbolId)
QualifiedInterfaceMethod(interface: TypeId, method: SymbolId)
OperatorMethod(interfaceName: Name, methodName: Name, implNode: NodeId)
IndexMethod(interfaceName: "Index", methodName: "index", implNode: NodeId)
DynVTable(interface: TypeId, slot: uint32)
ErrorTarget
```

`ReceiverMode` is one of:

- `None` — ordinary free function.
- `ExplicitFirstArgument` — qualified interface call, e.g.
  `Drawable::draw(shape)`.
- `ImplicitSelf` — method syntax, e.g. `shape.draw()`.
- `OperatorLeftHandSide` — binary operator method receiver.
- `OperatorOperand` — unary operator method receiver.
- `IndexBase` — indexing base receiver.

The side table stores resolved type IDs and node IDs rather than strings where
possible. Strings are used only for stable interface method names that map to
standard prelude interfaces.

### Operator Mapping

| Source form | Interface | Method | Required signature |
|---|---|---|---|
| `a + b` | `Add` | `add` | `add(rhs: Rhs) -> Output` |
| `a - b` | `Sub` | `sub` | `sub(rhs: Rhs) -> Output` |
| `a * b` | `Mul` | `mul` | `mul(rhs: Rhs) -> Output` |
| `a / b` | `Div` | `div` | `div(rhs: Rhs) -> Output` |
| `a % b` | `Rem` | `rem` | `rem(rhs: Rhs) -> Output` |
| `a ** b` | `Pow` | `pow` | `pow(rhs: Rhs) -> Output` |
| `a == b`, `a != b` | `Eq` | `eq` | `eq(rhs: Rhs) -> bool` |
| `a < b`, `a <= b`, `a > b`, `a >= b` | `Ord` | `cmp` | `cmp(rhs: Rhs) -> i32` |
| `-a` | `Neg` | `neg` | `neg() -> Output` |
| `!a` | `Not` | `not` | `not() -> bool` |
| `a[i]` | `Index` | `index` | `index(idx: Idx) -> Output` |

Primitive numeric, boolean, and built-in array operations keep
`PrimitiveOperator` targets. User-defined nominal types use `OperatorMethod` or
`IndexMethod`.

### Call Target Selection

Selection runs after RFC 0005 has assigned expression types:

1. Resolve the callee expression's symbol binding if it is a free function.
2. For member syntax, inspect the receiver type.
3. If the receiver is concrete, select the method from the concrete class,
   struct, or impl table.
4. If the receiver is `dyn I`, select a `DynVTable` slot from `I` after
   object-safety validation.
5. If the call is `I::method(receiver, args...)`, select
   `QualifiedInterfaceMethod` and validate that the receiver type implements
   `I`.
6. For operators, first try the primitive path. If not primitive, select the
   mapped interface method through the trait resolver.
7. If no target exists, record `ErrorTarget` and emit one diagnostic.

### Determinism

Candidate enumeration must be deterministic:

- Search concrete members in source order.
- Search impl records in `(interface name, type key, source order)` order.
- Search vtable slots in interface declaration order, after inherited
  superinterface slots are flattened by object-safety order.
- Never depend on hash-map iteration order for user-visible diagnostics.

### Diagnostics

The initial implementation may reuse existing checker diagnostics where they
already match the user error:

- `ZOM4012 CannotCallNonFunction`
- `ZOM4018 CheckerTraitNotImplemented`
- `ZOM4019 OperatorTraitSignatureMismatch`
- `ZOM4021 AmbiguousAssociatedTypeProjection` only for projection ambiguity,
  not call ambiguity

Additional call-dispatch diagnostics must be added to
`diagnostics-checker.def` before use and must be covered by diagnostics
conformance snapshots.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| RFC governance | `docs/rfc/**` | `rfc` |
| Type checker dispatch | `products/zomlang/compiler/checker/**` | `binder-checker` |
| Type environment dispatch side tables | `products/zomlang/compiler/type/**` | `binder-checker` |
| Module-qualified lookup | `products/zomlang/compiler/symbol/**`, `products/zomlang/compiler/driver/**` | `module-system` |
| Expression and interface specs | `docs/spec/chapters/04-expressions.md`, `docs/spec/chapters/09-interfaces.md` | `spec-audit` |
| Conformance and unit tests | `products/zomlang/tests/**` | `verification` |

## Security And Safety Impact

Incorrect dispatch can call the wrong implementation for a receiver, which is a
semantic safety violation and can become a memory-safety issue once unsafe
interfaces, raw pointers, or FFI wrappers are involved. The side table must be
computed once from checked types and then treated as immutable by lowering.
Dynamic dispatch through `dyn` must use object-safe vtable slots only; the
object-safety diagnostics from RFC 0005 remain the gate that prevents invalid
vtable layouts.

## Drawbacks And Risks

- Adding a dispatch side table increases the amount of checker-owned metadata.
  The benefit is that IR lowering becomes deterministic and does not repeat
  semantic lookup.
- Qualified interface calls can be confused with module-qualified functions.
  Binder metadata and type-directed lookup must distinguish the two before
  lowering.
- Operator interfaces are easy to overgeneralize. This RFC keeps the mapping
  fixed to the standard prelude traits and rejects implicit overload ranking.
- Vtable slot ordering can become an ABI commitment. This RFC requires source
  declaration order and inherited-slot flattening to be documented before
  `LANDED`.

## Alternatives Considered

### Lower Calls Directly In The Checker

Rejected. The checker should publish semantic facts, not emit backend IR. A
side table keeps checker and lowering separated while preserving one resolved
target per call.

### Re-run Lookup In IR Lowering

Rejected. Re-running method lookup duplicates semantic logic and can diverge
from checker diagnostics, especially for generic instantiation and `dyn`
vtable calls.

### C++-Style Overload Sets

Rejected. ZOM has no implicit overload ranking in v1. Overload-like behavior is
expressed through named interfaces and explicit generic bounds.

### Treat Operators As Syntax-Only Builtins

Rejected. RFC 0005 already supports user-defined operator interfaces. The
remaining task is to make their lowering explicit.

## Compatibility And Rollout

This is a new implementation contract for an existing checker surface. Rollout:

1. Add `CallDispatchTable` or equivalent side table to the type environment.
2. Populate dispatch records for ordinary function calls and primitive
   operators.
3. Populate records for concrete method calls and qualified interface calls.
4. Populate records for user-defined operator and index methods.
5. Add `DynVTable` records for `dyn I` receiver calls.
6. Add IR lowering tests that consume the side table without re-running lookup.

Rollback before `LANDED` is simple: disable the side-table consumer and keep the
checker-only behavior. After IR lowering consumes dispatch records, rollback
requires reverting the lowering integration in the same change.

## Documentation And Teaching Plan

- Update `docs/spec/chapters/04-expressions.md` with the operator-to-interface
  lowering table and dispatch categories.
- Update `docs/spec/chapters/09-interfaces.md` with qualified interface method
  call semantics and `dyn` vtable call semantics.
- Add developer notes near the type environment explaining when to write a
  dispatch record and when to leave `ErrorTarget`.
- Add diagnostics conformance snapshots for missing, ambiguous, and invalid
  dispatch targets when new diagnostic codes are introduced.

## Operational Readiness

- CI must run checker unit tests, dispatch unit tests, diagnostics conformance,
  and relevant IR lowering tests before this RFC can move to `LANDED`.
- Debug builds should be able to dump dispatch records for a source file so
  dispatch mismatches can be diagnosed without stepping through the checker.
- Performance budget: dispatch record construction should be linear in the
  number of call-like expressions plus candidate impls reachable from the
  current module.

## Acceptance Criteria

1. A dispatch side table exists and is keyed by expression `NodeId`.
2. Function calls record `FreeFunction` targets.
3. Concrete method calls record `InstanceMethod` or `StaticMethod` targets.
4. Qualified interface calls record `QualifiedInterfaceMethod` targets.
5. Primitive arithmetic, comparison, unary, and index operations record
   `PrimitiveOperator` targets where applicable.
6. User-defined `Add`, `Sub`, `Mul`, `Div`, `Rem`, `Pow`, `Eq`, `Ord`, `Neg`,
   `Not`, and `Index` operations record operator or index method targets.
7. `dyn I` receiver calls record deterministic `DynVTable` slots.
8. Dispatch records are immutable after checker completion.
9. IR lowering consumes dispatch records and does not re-run trait or method
   lookup.
10. Ambiguous, missing, or invalid dispatch targets emit stable registered
    diagnostics.
11. Diagnostics conformance snapshots cover every new dispatch diagnostic.
12. Unit tests cover dispatch record construction for primitive, concrete,
    interface-qualified, operator, index, and dyn-vtable cases.
13. Lit or integration tests cover user-visible successful and failing call
    dispatch cases.
14. `python3 scripts/check-rfc.py` passes.
15. `python3 scripts/check-format.py` passes after implementation changes.
16. `ctest --preset default --output-on-failure` passes before `LANDED`.

### Implementation Evidence

This RFC remains in `REVIEW`; the implementation is intentionally partial.

| AC | Status | Evidence | Remaining Work |
|---|---|---|---|
| 1 | Partial | `type::CallDispatchRecord`, `CallTargetKind`, and `ReceiverMode` are stored in `TypeEnv` by expression `NodeId`. `type-env-test.cc` covers set/get and clear behavior. | Extend coverage beyond operator/index records and freeze mutation after checker completion. |
| 6 | Partial | `BodyChecker` records `OperatorMethod` dispatch targets for user-defined binary arithmetic, comparison, `Neg`, and `Not` operators, and `IndexMethod` targets for user-defined `Index.index`. `body-checker-test.cc` verifies interface/method names, impl node IDs, argument type IDs, result type IDs, and receiver mode, including `OperatorOperand` for unary operators. | Later integrate function, member, qualified-interface, and dyn-vtable call records. |
| 8 | Partial | Dispatch records are exposed through immutable `getDispatch()` references after insertion. | Add an explicit checker-completion immutability boundary if a later mutable dispatch builder is introduced. |
| 14 | Complete | `python3 scripts/check-rfc.py` passes. | None. |
| 15 | Complete | `python3 scripts/check-format.py` passes after implementation changes. | None. |
| 16 | Partial | Focused debug and sanitizer builds plus focused dispatch-related tests pass for the implemented operator/index slice. | Full `ctest --preset default --output-on-failure` is still required before `LANDED`. |

## Implementation Plan

1. Define the dispatch record data model in the type environment or checker
   side-table module. **Started:** the first implementation stores records in
   `TypeEnv`.
2. Add construction APIs and immutable read APIs. **Started:** `TypeEnv`
   exposes `setDispatch`, `hasDispatch`, and `getDispatch`.
3. Populate records for direct function calls.
4. Populate records for concrete member calls.
5. Populate records for qualified interface calls.
6. Populate records for primitive operators.
7. Populate records for user-defined operator and index methods. **Started:**
   binary user-defined operator and index records are populated.
8. Populate records for `dyn` receiver vtable calls.
9. Add diagnostics for missing or ambiguous call dispatch when existing
   diagnostics are insufficient.
10. Teach IR lowering to consume dispatch records.
11. Add debug dumping for dispatch records.
12. Add unit, lit, diagnostics, and lowering tests.

## Test Plan

- Build: `cmake --build --preset debug` and `cmake --build --preset sanitizer`.
- Unit tests: dispatch side-table tests plus checker tests for every
  `CallTarget` variant.
- Lit tests: conformance sources for ordinary calls, qualified interface calls,
  operator calls, index calls, and `dyn` calls.
- Conformance: diagnostics snapshots for missing method, ambiguous method,
  missing operator impl, invalid operator signature, and dyn vtable rejection.
- Generated files: none expected unless AST or schema metadata changes.
- Format: `python3 scripts/check-format.py`.
- RFC check: `python3 scripts/check-rfc.py`.

## Open Questions

- What exact debug dump flag should expose dispatch records?
- Should `DynVTable` slots be flattened during checker completion or delayed
  until ABI lowering?

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-07-08 | DRAFT | Initial draft defining call dispatch, operator lowering, dispatch side-table records, and IR-lowering ownership. |
| 2026-07-08 | REVIEW | The proposal has complete motivation, reference-level dispatch records, follow-up implementation plan, and local tracking anchors. Approval remains blocked on owner review, non-empty approvers, a recorded decision, and implementation evidence. |
| 2026-07-08 | REVIEW | Started implementation by adding `TypeEnv` dispatch records and recording user-defined binary operator and index method dispatch targets. RFC remains blocked on the remaining dispatch target variants, debug dumping, IR-lowering consumption, owner approval, and decision metadata. |
| 2026-07-08 | REVIEW | Added user-defined unary `Neg` and `Not` operator dispatch records with `OperatorOperand` receiver mode. |
