---
rfc: 49
title: Algebraic Data Type Design
type: language
status: DRAFT
author: ZOM Compiler Team
review-manager: rfc
required-owners: [binder-checker, error-system, ir-backend, lexer-parser, module-system, rfc, runtime-memory, spec-audit, verification]
approvers: []
created: 2026-09-14
updated: 2026-09-14
area: language
requires: [5, 7, 9, 10]
supersedes: []
superseded-by: []
discussion: TBD
decision: TBD
implementation: TBD
tracking-issue: TBD
---

# RFC 0049: Algebraic Data Type Design

## Summary

This RFC proposes the complete algebraic data type (ADT) model: nominal
products (`struct`), encapsulated products (`class`), tagged disjoint sums
(`enum`) with unit, tuple, and record variants, single-field newtype
wrappers, exhaustive pattern matching, automatic boxing of recursive types,
declaration-site variance for generic ADTs, a closed built-in derive set
governed by the existing coherence and orphan rules, partial moves and the
Linear marker interaction, and a deterministic target layout contract
including niche optimization and `#[zom::repr(...)]` ABI controls. It
consolidates a 2026-06 design note that presented unapproved decisions and
diagnostic codes as current. Nothing in this RFC beyond the existing parser
surface is implemented; every diagnostic code below is a PROPOSAL and must be
registered in the diagnostic catalog and reservation ledger before any test
may assert it.

## Motivation

The parser currently accepts named-field struct declarations and unit/tuple
enum declarations, and one struct-literal aggregate shape reaches Built MIR
through the admitted aggregate slice. There is no enum semantic admission, no
match lowering, no derive, no layout engine, and no exhaustiveness checking.
Downstream features (error unions in RFC 0006, richer dispatch in RFC 0009,
FFI in chapter 18) need a coherent product/sum model rather than a sequence of
ad hoc additions. Deciding the ADT model now fixes the questions that
otherwise get re-litigated per feature: nominal identity, variant payload
forms, recursive-type representation, marker propagation, and the exact
layout/ABI guarantees users can rely on across targets.

## Goals

- Define nominal products, encapsulated products, sums with all three variant
  forms, and newtype as one closed model with one identity rule.
- Define exhaustive matching semantics and the refutable/irrefutable pattern
  placement rules.
- Fix recursive-type handling (implicit boxing) with explicit overrides.
- Fix generic variance defaults and declaration-site annotations.
- Define the closed derive set and its coherence interaction with RFC 0005.
- Define destructuring moves, partial moves, and Linear propagation.
- Define deterministic default layout, niche optimization, and the stable
  `repr(C)` contract for FFI.
- Assign every proposed diagnostic a code from the catalog's sparse ranges
  and a producer stage.

## Non-Goals

- Generalized algebraic data types (per-variant type refinement) and
  inductive families.
- Impl specialization (already excluded by RFC 0005).
- Custom user-defined derives or procedural macros (separate macro RFC).
- Incremental specialization ABI, cross-language name mangling, and unstable
  field-reordering promises beyond what this RFC states.
- The full RFC 0006 error type; this RFC only provides the sum and layout
  substrate it uses.

## Prior Art

- **Rust enums and layout.** Nominal sums with payload variants,
  `#[repr(...)]`, niche-filling using forbidden bit patterns (`NonZero`,
  null references), and layout-unstable default ordering are production
  proven. ZOM copies the niche principle and the "default repr is unstable,
  C repr is the ABI promise" split. The common pitfalls ZOM must avoid:
  silent repr dependence in FFI (fixed by the repr(C) gate), niche
  surprises (documented canonical niche table), and discriminant overflow
  (compile-time checked).
- **rustc exhaustiveness (`usefulness`).** Field-usefulness and uncovered
  witness computation is the mature answer for pattern coverage; ZOM adopts
  the same conceptual coverage matrix without requiring the literal
  algorithm. Common pitfall: guards making an arm non-total, which ZOM keeps
  fail-closed by requiring an unguarded fallback.
- **Swift enums and resilience.** Tagged enums with payloads and
  compiler-synthesized no-payload layout; `indirect` boxes recursive cases
  explicitly. ZOM differs by implicit boxing with a note and an explicit
  opt-out, chosen so beginners never write an infinite-size type, while
  keeping `#[zom::repr(unboxed)]` to force the error.
- **OCaml/ML and Haskell.** Established named sums and deriving; ZOM keeps
  nominal (not structural) sums and a small closed derive set rather than
  Haskell's open deriving class.
- **C and C++ ABI rules.** The `repr(C)` contract follows the platform C ABI
  per target triple; ZOM must use its target registry (RFC 0016) rather than
  host assumptions for layout, and the verifier-driven LLVM backend (RFC
  0021) for emission.
- **Kotlin/Scala sealed hierarchies.** Considered as an alternative to
  enums; rejected because ZOM already has nominal enums in the grammar and
  sealed-class dispatch would add a second sum mechanism.

## Guide-Level Explanation

A user declares data, not memory strategy:

```zom
struct Point { x: f64, y: f64 }

enum Shape {
    Empty,
    Circle(f64),
    Rect { w: u32, h: u32 },
}

fun area(shape: Shape) -> f64 {
    match (shape) {
        when Shape.Empty => { return 0.0; }
        when Shape.Circle(r) => { return 3.14159 * r * r; }
        when Shape.Rect { w, h } => { return f64(w) * f64(h); }
    }
}
```

Recursive data just works; the compiler inserts an owned box where an
infinite-size field would otherwise occur and says so:

```zom
enum Bst {
    Leaf,
    Node { left: Bst, right: Bst, key: u64 },
}
```

Byte-level interop requires an explicit promise; ordinary ZOM layout may
reorder fields, `repr(C)` never does:

```zom
#[zom::repr(C)]
struct Packet { seq: u32, flags: u8 }
```

## Reference-Level Design

### 1. Identity and declarations

Every ADT declaration introduces a fresh nominal identity; structurally
identical declarations at different sites are incompatible types (consistent
with RFC 0005 nominal interning). Variants are constructors, never types.
GADT-style per-variant refinement is rejected; the effect is built with a
single payload variant plus an out-of-band witness.

Current parser surface (chapter 08/10 grammar): structs accept named fields
only; enums accept unit and parenthesized tuple variants. This RFC proposes
adding: record (brace) enum variants, single-parenthesized-field newtype
structs, and multi-field positional struct products. Proposed grammar deltas
against `docs/spec/ZomParser.g4` and chapters 08 and 10:

```mermaid
flowchart LR
    D[ADT declaration] --> S[struct: named product]
    D --> NW[struct(T): newtype]
    D --> C[class: encapsulated product]
    D --> E[enum: tagged sum]
    E --> U[unit variant]
    E --> T[tuple variant]
    E --> R[record variant - proposed]
```

### 2. Products and newtype

`struct` fields are public by default; structs carry no methods, accessors,
or init/deinit (behavior uses `class`). A `struct Name(T)` with exactly one
unnamed parenthesized field is a newtype that is layout-equivalent to `T`
(`size_of` and `align_of` identical, zero added bytes). Newtype exposes
explicit use-site conversions only (`as_ref()` for a borrow of the inner
value, `into()` for a linear consume); there is no implicit coercion, so a
function expecting `T` does not accept `Name`. Multi-field parentheses form a
positional tuple product. Zero-sized products have size 0 and alignment 1
and remain distinct nominal types.

### 3. Sums, discriminants, and niches

Unit, tuple, and record variants may coexist in one enum. The discriminant
defaults to the smallest unsigned width holding the variant count, clamped to
a minimum width of one byte; an uninhabited enum has no storage. Explicit
`= N` discriminants must fit the chosen width.

| Variants | Default tag width |
|---|---|
| 1 through 256 | u8 |
| 257 through 65,536 | u16 |
| 65,537 through 4,294,967,296 | u32 |
| Larger | u64 |

A two-variant sum whose single-payload variant holds a niche type folds the
tag into the niche bits with no size increase. Canonical niches: the all-zero
pattern of `Own<T>`, raw non-null pointers, and references; an uninhabited
variant; and a single-variant enum (tag eliminated). A signed integer niche
and floating NaN niches are not used implicitly. `#[zom::repr(...)]` may set
the tag width (`u8`..`u64`, signed forms for C interop) and opt out of niche
folding. A tag+payload value stores the tag first, then the payload union
sized and aligned to the largest variant under the default ABI; padding bytes
are indeterminate.

### 4. Pattern matching

`match` evaluates the scrutinee once and runs the first matching arm;
`if let`, `let ... else`, and irrefutable destructuring in `for` and `let`
follow chapter 07. Patterns support wildcards, literals, tuple/struct/enum
destructuring, `@` binding, or-patterns, and `if` guards. A guarded arm is
not total for its variant, so exhaustiveness requires an unguarded fallback.
The coverage analysis computes covered variant heads and recursively covered
product fields, wildcard-subsumed remainder, and reports uncovered variants
with a suggested arm; `#[zom::non_exhaustive]` enums require a wildcard arm
even when all variants are covered.

### 5. Recursive types

If a non-static field's type unifies with the enclosing type with no
intervening indirection, the field is rewritten semantically to `Own<T>`
once, allocating exactly one owned box per construction; both recursive arms
of a product box independently. `#[zom::repr(box)]` forces boxing silently;
`#[zom::repr(unboxed)]` suppresses it and raises a compile error if the
resulting layout is provably infinite.

### 6. Generics and variance

Generic ADT type parameters default to invariant. Declaration-site
attributes `#[zom::variance(cov|inv|contra)]` set variance; the effective
variance combines across occurrence positions (mixed occurrences collapse to
invariant; covariant composed with contravariant is invariant). Declaring
covariance or contravariance on a parameter appearing in both input and
output positions of the public API is rejected. Variance composes with
marker `where` bounds from RFC 0005. Phantom/unused parameters are accepted
without coercion effect.

### 7. Derive and coherence

The built-in derive set is closed: `Clone`, `Copy`, `Eq`, `Hash`, `Debug`,
`Ord`, and `Default` (products only), using outer `#[derive(...)]` expanded
in attribute order. `Copy` requires all fields `Copy` and implies `Clone`.
Derive coherence follows RFC 0005 orphan rules: derive is coherent when the
type or every derived trait is local; there is no specialization and no
user-added blanket circumvention. Auto marker propagation (separate from
derive) holds a positive field-propagated marker iff every field (products)
or every variant payload (sums) satisfies it; `Linear` and task-bound
markers are never inferred, only declared; negative impls override evidence.
`deinit()` is an ordinary class method, not a marker, and its presence
removes implicit copy eligibility.

### 8. Destructuring, moves, and Linear

Destructuring a non-`Shared` binding moves the bound fields; `Shared`
fields are copied. Partial moves are tracked per field: the parent cannot be
used whole afterward while sibling fields remain usable, and drop glue at
scope end runs only for live fields, consistent with the RFC 0007 move-path
model. A `Linear` field propagates an exactly-once obligation to each
destructured binding; the wildcard pattern is an explicit consume.

### 9. Layout and ABI

- Default product layout: offsets round each field up to its alignment within
  an aggregate aligned to the maximum field alignment; the default repr is
  layout-unstable and fields may be reordered.
- Default sum layout: tag first (or niche-eliminated), then the aligned
  payload union.
- `#[zom::repr(C)]`: declaration field order, platform C ABI padding per the
  verified target registry, and FFI eligibility; nested products are not
  recursively forced to C. For classes it governs the data portion only.
- Layout computation belongs to the LIR/layout substrate of RFC 0021 and
  must consume verified target selection (RFC 0016), never the host triple.

### 10. Proposed diagnostics

All codes below are PROPOSED, are not registered in
`compiler/diagnostics/defs/`, and must be allocated from sparse catalog
ranges and recorded in `tests/coverage/diagnostic-reservations.json` before
use. The exact numbers in the source design note are placeholders: the
pattern-checking placeholders `ZOM3010` through `ZOM3014` collide with the
already-registered binder range and must be re-picked; letter suffixes are
not allowed.

| Placeholder | Proposed meaning | Stage |
|---|---|---|
| PAT-UNCOVERED | Non-exhaustive match; lists uncovered witnesses | checker |
| PAT-IRREFUTABLE-POS | Irrefutable pattern required in this position | checker |
| PAT-REFUTABLE-POS | Refutable pattern requires a fallback | checker |
| PAT-NONEXH-WILDCARD | `non_exhaustive` enum match lacks wildcard | checker |
| PAT-ELSE-DIVERGES | `let ... else` block must diverge | checker |
| LAY-DISCR-OVERFLOW | Explicit discriminant exceeds repr width | checker |
| LAY-INFINITE | Unboxed recursive field gives infinite layout | checker |
| VAR-VARIANCE | Invalid variance for parameter occurrence positions | checker |
| COH-ORPHAN | Orphan-rule violation for derive/impl | checker |
| LIN-UNCONSUMED | Linear binding missing a consumption path | checker (RFC 0007 rail) |
| LIN-DOUBLE-CONSUME | Linear binding consumed more than once | checker (RFC 0007 rail) |
| FFI-REPR-C | Product crosses C FFI without `repr(C)` | checker |
| MK-SPAWN-BOUND | Marker bound violated across spawn boundary | checker |

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| Lexer and grammar oracle | `docs/spec/ZomLexer.g4`, `docs/spec/ZomParser.g4`, `compiler/lexer/**` | lexer-parser |
| Parser, CST, AST schema, patterns | `compiler/parser/**`, `compiler/cst/**`, `compiler/ast/schema.yml`, spec chapters 07/08/10/12/16/18 | lexer-parser |
| Binder scopes, pattern bindings, variant resolution, module visibility | `compiler/binder/**`, chapter 13 | module-system |
| Type checking, generics, variance, coherence, exhaustiveness, moves | `compiler/checker/**`, `compiler/type/**`, spec chapters 09/12/22/23 | binder-checker |
| HIR/MIR aggregate and match lowering, drop | `compiler/hir/**`, `compiler/mir/**`, `compiler/ownership/**` | ir-backend |
| LIR layout, niche, repr, LLVM emission | `compiler/lir/**`, `compiler/ir/layout/**`, `compiler/backend/llvm/**` | ir-backend |
| zc ownership primitives used by boxing | `libraries/zc/**`, `runtime/**` | runtime-memory |
| Diagnostic codes, projectors, reservations | `compiler/diagnostics/defs/**`, `tests/coverage/diagnostic-reservations.json` | error-system |
| Normative spec chapters and grammar alignment | `docs/spec/chapters/**`, `docs/spec/grammar-reference.md` | spec-audit |
| Conformance corpus, lit/FileCheck, ztest, gates | `tests/conformance/**`, `tests/unittests/**`, `scripts/` | verification |
| RFC governance | `docs/rfc/README.md`, `scripts/check-rfc.py` | rfc |

## Security And Safety Impact

The model strengthens memory safety: infinite-size layouts are rejected or
boxed rather than accepted, partial moves prevent dropped-but-moved fields,
Linear obligations reject resource leaks and double consumption, and the
FFI gate prevents passing layout-unstable values across a C boundary. Niche
encoding must never interpret an invalid payload as valid; padding is
indeterminate and `repr(C)` is the only byte-stable guarantee. Unsafe
transmutation between sums remains subject to the existing unsafe boundary.

## Drawbacks And Risks

- Implicit boxing can surprise users about allocation counts; mitigation is
  the informational note at the rewrite site and the explicit override
  attributes.
- Niche optimization complicates LIR layout and MIR discriminant lowering;
  it must land behind byte-parity gates and can be deferred while the
  tagged representation ships first.
- Record variants and positional products are grammar additions with
  recovery implications for the hand-written parser and the ANTLR oracle;
  both must change together per the spec-alignment rules.
- Variance annotations add user-facing complexity; the invariant default
  limits it, and invalid declarations fail at the checker, not via coercion
  surprises.
- Exhaustiveness checking depends on a complete variant inventory, coupling
  to binder coherence; partial implementations must fail closed rather than
  accept non-exhaustive matches.

## Alternatives Considered

- **Structural ADTs.** Rejected: nominal identity is already canonicalized
  in RFC 0005 and structural sums would reintroduce coherence ambiguity.
- **Always-explicit boxing (Swift `indirect`).** Rejected for the default
  because it makes beginners write impossible types; implicit boxing with an
  opt-out preserves the escape hatch.
- **Inferred variance (Scala-style).** Rejected: mixed-position unsoundness
  is a classic source of bugs; invariant-by-default with explicit annotation
  is the conservative point.
- **Open user derives now.** Rejected until the macro/attribute execution
  model exists; a closed set serves all current language needs.

## Compatibility And Rollout

Pre-1.0, no compatibility shims. Recommended land order, each independently
tested and fail-closed: (1) record variants + complete aggregate semantic
admission; (2) exhaustiveness and match lowering on the existing MIR
terminator vocabulary, extending it if needed through an RFC 0048 family;
(3) recursive boxing with owned allocation; (4) layout engine in LIR with
verified target selection and tagged representation; (5) niche optimization
behind parity; (6) derive and marker propagation; (7) variance; (8) newtype
and positional products. Each stage updates spec, grammar oracle, AST
schema, and conformance corpus in the same change. Unimplemented stages
remain typed capability rejections, never silent acceptance.

## Documentation And Teaching Plan

Normative prose lands in chapters 07, 08, 10, 12, 14, 16, and 18 plus the
grammar reference; a current-state language design note
(`docs/design/language/algebraic-data-types.md`) records only implemented
evidence; this RFC carries proposals. Teaching material needs one product/
sum guide and one layout/FFI guide. All examples must be labeled
syntax-only until they reach the stated stage.

## Operational Readiness

Layout must be target-driven through RFC 0016 target selection; CI layout
tests need at least Linux x86-64 initially, with additional target lanes
before `repr(C)` cross-target claims. No release readiness is implied by
DRAFT.

## Acceptance Criteria

- Spec chapters and both grammar files describe the accepted variant,
  newtype, pattern, variance, derive, and repr forms with parser/oracle
  parity.
- Every proposed diagnostic is registered in a `.def` catalog partition and
  `diagnostic-reservations.json` with a producer and a lit test; no
  placeholder code or letter suffix remains.
- Struct literals, all variant forms, and destructuring reach VerifiedBuiltMir
  for admitted shapes; non-exhaustive and refutability errors fire on the
  checker source rail.
- Recursive types construct through owned boxes and the unboxed-infinite case
  is rejected.
- A layout test suite asserts size, alignment, offsets, niche folding, and
  repr(C) bytes against verified target specs.
- The derive set obeys coherence/orphan gates and Linear propagation has
  ownership-fact tests.
- Full corpus parity stays byte-green and the sanitizer preset passes.

## Implementation Plan

1. Resolve open questions and move this RFC through REVIEW to ACCEPTED.
2. Land grammar/AST/binder additions for record variants and newtype with
   spec alignment.
3. Land checker semantics (variants, patterns/exhaustiveness, variance,
   coherence, moves) behind typed capability rejections.
4. Land HIR/MIR match and aggregate lowering as RFC 0048 families.
5. Land the LIR layout engine with target selection, tagged layout first and
   niche folding second.
6. Land derive, marker propagation, and repr(C) FFI gating.
7. Register diagnostics and reservations per stage.

## Test Plan

- Build: `cmake --preset sanitizer && cmake --build --preset sanitizer`.
- Unit tests: ztest suites for layout size/alignment/offsets, niche
  decisions, variance lattice, coverage witnesses, partial-move state.
- Lit tests: parser admission/recovery for new forms; diagnostics for every
  registered code under `tests/conformance/`.
- Conformance: grammar runner matrix; 923-source process parity and 64-dump
  IR parity via `scripts/check-ir-parity.py`.
- Generated files: regenerated AST headers and coverage inventories with
  their generators in the same change.
- Format: `python3 scripts/check-format.py`; `scripts/check-rfc.py`;
  spec-alignment skill gates.

## Open Questions

- Should multi-field positional struct products be included in the first
  acceptance or deferred until FFI needs them?
- Does `Default` derive belong in the closed set given enum defaults are
  forbidden, or should it be removed until a standard prelude exists?
- Is an informational boxing diagnostic desirable as a default, or should
  implicit boxing be silent with only the explicit attributes visible?
- Should niche folding be observable through a `size_of` intrinsic before a
  const-evaluation contract exists?

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-09-14 | DRAFT | Initial draft consolidated from the 2026-06 ADT design note; all diagnostic codes marked proposed. |
