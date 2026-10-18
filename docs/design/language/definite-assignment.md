# Definite Assignment For Local Bindings

Status: straight-line and conditional-diamond late initialization implemented
Date: 2026-10-17
Last implementation update: 2026-10-18 (RFC 0055 slices 1-4)

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

## Current production behavior

Straight-line late initialization and the conditional four-block diamond are
implemented (RFC 0055 slices 1-4). Observed against the sanitizer build:

| Program | Current | Spec |
|---|---|---|
| `mut v: i32; v = 1; return v;` | `--check` passes; lowered natively | Accept, lower |
| `mut v: i32; return v;` | ZOM4093 + ZOM4094 | Reject (matches) |
| `let v: i32; v = 1; return v;` | accepted; lowered natively | Accept (single init) |
| `let v: i32; if (c) { v = 1; } else { v = 2; } return v;` | accepted; per-path `Initialize`, lowered | Accept (single init per path) |
| `let v: i32; if (1 < 2) { v = 41; } else { v = 7; } return v;` | accepted; folds to `zom.module_init`, exit 41 | Accept (foldable condition) |
| `let v: i32; if (c) { v = 1; v = 2; } else { v = 2; } return v;` | ZOM4024 on the second arm write | Reject (immutable re-write) |
| `let v: i32; if (c) { v = 1; } else { } return v;` | ZOM4093 + ZOM4094 at the join read | Reject (missing-arm path) |
| `let v: i32; if (c) { v = 1; v = 2; } else { } return v;` | ZOM4024 on the arm re-write and ZOM4093 + ZOM4094 at the join read in one rejection | Reject (both violations) |
| `mut value: i32; return value;` | ZOM4093 + ZOM4094 | Reject (matches) |

The bounded-diamond pre-pass in the body checker answers "initialized on every
path" for the admitted shape and emits ZOM4093/ZOM4094 itself so the join read
lands in the same rejection batch as an arm ZOM4024 (the pipeline halts at the
first source-rejecting phase, so diagnostics split across the checker and the
MIR ownership rail could never co-occur). The MIR ownership rail remains the
independent backstop for shapes beyond this bounded slice; its per-block AND
meet and error-preserving empty-arm transport stay in place for wider lowering.

## Implemented surfaces

1. **Body checker admission.** `buildLocalWriteRegistry`
   (`compiler/checker/body/body-checker.cc`) classifies each write to an
   initializer-less scalar local: the first write on a reaching path is
   `FirstInitialization` and a later write is `ImmutableReinit`. It scans
   straight-line statements and every statement of a bounded if/else diamond
   arm independently, so the path-scoped classification does not see the
   sibling arm write as a second write. `ImmutableReinit` emits ZOM4024;
   `FirstInitialization` is admitted as `BodyProductionKind::LocalWrite`.
   The same diamond scan records each arm's extra immutable writes and, when
   one arm is empty and the trailing return reads the target, the uninitialized
   join read; a pre-pass rejects the body with the batched ZOM4024 and
   ZOM4093/ZOM4094 diagnostics before production sites are analyzed.
2. **HIR builder.** `ConditionalLocalWriteReturn` plus the
   `HirConditionalLocalWriteStatement` block-listed node represent a
   binding without an initializer followed by a per-arm if/else. The node
   uses a fixed-id layout (11 ids for a bare bool condition, 13 for a
   foldable comparison) and an independent per-node verifier arm.
3. **MIR builder.** The recursive MIR builder walks the
   `ConditionalLocalWrite` statement and emits the four-block CFG: a
   SwitchInt on a bool parameter or a comparison temporary, one
   per-path-`Initialize` arm, a Goto join, and the trailing Return. A
   missing arm is an empty block so the error-preserving MIR reaches the
   ownership rail.
4. **Ownership checker.** The bounded conditional-local-write diamond is
   rejected in a body-checker pre-pass that batches every definite-assignment
   violation of the shape: ZOM4024 for each extra immutable write on an arm
   path and ZOM4093 (with a ZOM4094 note at the binding) for the trailing join
   read an empty arm leaves uninitialized. The MIR ownership rail's path
   analysis is the independent backstop for shapes the bounded checker pass
   does not name; the admitted reducible-CFG slice is straight-line plus the
   four-block diamond, and loop-header joins use the pre-existing
   StorageLive/Overwrite encoding outside the new per-block discipline.

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
