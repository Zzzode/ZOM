# Definite Assignment For Local Bindings

Status: specified in the normative text, not fully implemented
Date: 2026-10-17

This note synthesizes an already-specified rule and records where the
production compiler diverges. It does not introduce a new rule; the
contract is normative in
[Chapter 6](../../spec/chapters/06-declarations.md). Any normative change
must go through the RFC process.

## Specified contract

The `VariableDecl` grammar accepts an initializer-less binding for both
keywords, with a semantic gate (Chapter 6, `VariableDecl` EBNF comment):

- `mut` without an initializer requires a type annotation.
- `let` without an initializer is accepted only where definite assignment
  proves exactly one write before the first read.

Chapter 6 also states only `mut` bindings may be *reassigned*; the single
write that initializes an initializer-less `let` is an initialization, not
a reassignment. The distinction is therefore:

- **`let`** — at most one assignment (the initialization), which may occur
  textually after the declaration as long as every use is dominated by it
  on every reaching path.
- **`mut`** — any number of assignments; the same definite-assignment rule
  governs the first use.

Initialization is a property of control-flow paths, not of the binding
keyword. A `mut` binding used before its first assignment is rejected the
same way as a `let` binding.

## Specified examples

```zom
// Straight-line late initialization.
let a: i32;
a = 1;
return a;

// Chapter 6 mut example: every branch initializes before the use.
mut result: str;
if (condition) {
    result = "success";
} else {
    result = "failure";
}
```

Negative cases follow from the same contract:

- Use with no reaching assignment: ZOM4093 (`UninitializedPlaceUse`) with
  ZOM4094 at the binding. Already enforced on the ownership rail for both
  `let` and `mut` (verified: `mut v: i32; return v;` produces ZOM4093).
- `let` assigned on only one branch then read: use is not dominated, ZOM4093.
- `let` assigned a second time on any path: immutable mutation rejection
  (ZOM4024 family); the first write remains its single initialization.

## Current production behavior (gap)

Observed on 2026-10-17 against the sanitizer build:

| Program | Current | Spec |
|---|---|---|
| `mut v: i32; v = 1; return v;` | `--check` passes; no end-to-end lowering strategy | Accept, lower |
| `mut v: i32; return v;` | ZOM4093 + ZOM4094 | Reject (matches) |
| `let v: i32; v = 1; return v;` | ZOM4099 at the assignment | Accept (single init) |
| `let v: i32; if (c) { v = 1; } else { v = 2; } return v;` | ZOM4099 in the branch | Accept (single init per path) |

The ownership rail already answers "initialized on every path" via its
init-state tracking and emits ZOM4093/ZOM4094. The gap is upstream: the
body-checker admission gate and the HIR/MIR lowering path.

## Implementation surfaces

1. **Body checker admission.** `isSimpleLocalWrite`
   (`compiler/checker/body/body-checker.cc`) admits a scalar local write
   only when `isMutableOwnerLocal` holds; the late-init shapes drain as
   ZOM4099. A write to an initializer-less `let` local must be admitted as
   the dominating first assignment, and a subsequent write to that `let`
   rejected via the immutable-mutation drain. The checker must classify the
   first dominating write as `Initialize` and only a write to an already
   initialized place as `Overwrite`.
2. **HIR builder.** The shape classifier must represent a binding without
   an initializer followed by a later `Initialize` write. Today only
   `mut x = <init>; x = ...; return x;` (`ScalarLocalOverwrite`, which
   requires an initializer) and the uninitialized-return arms lower; the
   initializer-less plus late-init combination reaches the GenericShape
   catch-all or drains.
3. **MIR builder.** The recursive MIR builder (RFC 0048 Phase 3) must walk
   the `LocalWrite` statement for an initializer-less binding and emit
   `StorageLive` followed by `Assign(Initialize)`. The shape strategy table
   has no such entry; this lands as the recursive builder gains the write
   family.
4. **Ownership checker.** The path-sensitive init state must accept a read
   dominated by the late `Initialize` on every path while keeping ZOM4093
   for uses that are not. The admitted reducible-CFG slice (straight-line,
   four-block diamond, reducible loops) covers the current production
   shapes.

## Bounds (non-goals)

- Partial field-by-field initialization of aggregates from separate
  statements (`let p: Point; p.x = 1; p.y = 2;`). The initializer-less
  aggregate must be wholly assigned in one statement for now.
- Loop assignments that initialize only on some iterations without a prior
  assignment; these stay rejected until loop-header join state is complete.
- Module-scope bindings: ZOM4087 (`ModuleBindingInitializerRequired`)
  remains the contract; this note is body-local only.
- Interior mutability: this note covers binding/place assignment, not
  mutation behind a shared reference.
