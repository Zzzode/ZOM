---
rfc: 49
title: Enum Variants And Exhaustive Matching
type: language
status: DRAFT
author: ZOM Compiler Team
review-manager: rfc
required-owners: [binder-checker, error-system, ir-backend, lexer-parser, module-system, rfc, spec-audit, verification]
approvers: []
created: 2026-09-14
updated: 2026-09-15
area: language
requires: [5, 7, 9, 10]
supersedes: []
superseded-by: []
discussion: TBD
decision: TBD
implementation: TBD
tracking-issue: TBD
---

# RFC 0049: Enum Variants And Exhaustive Matching

## Summary

This RFC adds record (brace) enum variants to the existing unit/tuple
surface, defines exhaustive `match` and the single-pattern conditional
binding forms (`if let`, `let ... else`), a real or-pattern, and the
cross-module `#[zom::non_exhaustive]` rule. It is scoped to sum types and
pattern matching only. Products, classes, struct methods/init/deinit,
newtype, recursive-type boxing, declaration-site variance, derives, niche
optimization, repr(C) aggregate layout, and a size/align intrinsic are
explicitly out of scope and deferred to later RFCs because their
prerequisites (a language box type and allocator, an aggregate LIR layout
store, and a subtype/coercion engine) do not exist. Existing registered
diagnostics are reused; only genuinely new codes are allocated.

## Motivation

The parser already accepts unit and tuple enum variants, and the AST carries
an `EnumDeclaration` with variant lists, but enums have no semantic
admission and no matching: there is no variant constructor resolution,
payload checking, exhaustiveness, or HIR/MIR lowering. Record variants are
hard-rejected by both the hand-written parser and the ANTLR oracle today.
Meanwhile checker fact structures for exhaustiveness, variant signatures,
and orphan coherence already exist (`ExhaustivenessFact`,
`EnumVariantSignature`, coherence facts), so this is a bounded vertical
slice from grammar through MIR rather than new machinery. Downstream error
unions (RFC 0006) need a complete sum substrate first.

## Goals

- Admit record variants alongside unit and tuple variants, with parser,
  oracle, AST, binder, and checker parity.
- Define exhaustive matching: coverage and unreachable-arm analysis, guards,
  or-patterns, `@` binding, and the refutable/irrefutable placement rules.
- Add `if let` and `let ... else` as new statement forms and define their
  divergence requirements.
- Define the cross-module meaning of `#[zom::non_exhaustive]`.
- Specify the variant and payload export surface in module interfaces.
- Reuse the already-registered match, orphan, and linear diagnostics and
  allocate only the genuinely new codes.

## Non-Goals

- Struct/class product changes: structs keep their existing accepted methods,
  accessors where specified, and `init`/`deinit` surface from chapter 08; this
  RFC changes nothing about them. `structMember` remains shared with `error`
  declarations and is not modified.
- Newtype, positional products, variance attributes, derives, and the
  `Clone/Eq/Hash/Debug/Ord/Default` derive set (separate marker/derive RFC;
  Copy stays governed by RFC 0024 automatic structural policy).
- Recursive-type boxing, a language-level box/heap type, a runtime allocator,
  and non-trivial drop glue (separate ownership/runtime RFC; no
  `zom::repr(box|unboxed|no_niche)` here).
- Niche optimization, discriminant-width overrides beyond the default,
  repr(C) aggregate layout, and `size_of`/`align_of` (separate layout RFC
  consuming RFC 0016 target selection and the future aggregate LIR store).
- GADTs, custom patterns, procedural macros, and generic function bodies
  (generic enums are accepted by the grammar; bodies remain governed by the
  existing generic-body rejection).
- Spawn-boundary marker enforcement: `spawn`/`suspend` continue to fail
  closed under ZOM4095 per RFC 0007; no `concurrency` surface is touched.

## Prior Art

- **rustc exhaustiveness (`usefulness`).** Constructor-coverage and
  uncovered-witness computation with guarded arms kept non-total is the
  mature answer; ZOM adopts the conceptual coverage matrix without mandating
  the literal algorithm. Pitfall avoided: a guarded arm is never total for
  its variant, so an unguarded fallback is required.
- **Rust enums and `non_exhaustive`.** Unit/tuple/record variants in one enum
  and the local-vs-foreign-crate visibility of `non_exhaustive` are
  production proven; ZOM copies the cross-module rule (foreign modules must
  wildcard and cannot construct hidden payloads).
- **Swift enums.** Payload variants and `if case let`/`guard case let`
  demonstrate the single-pattern binding forms; ZOM uses the conventional
  `if let` / `let ... else` spelling.
- **OCaml/ML.** Established named sums and or-patterns with consistent
  binding sets; the binding-consistency rule for or-patterns is the pitfall
  ZOM must enforce.
- **Hand-written parser plus grammar oracle.** New productions must change
  `ZomParser.g4`, the hand-written parser, recovery, and the AST schema
  together, per RFC 0002 and the repository spec-alignment rules.

## Guide-Level Explanation

All forms below are new semantics over the existing enum surface.

```zom
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

Single-form binding does not require a full match:

```zom
if let Shape.Rect { w, h } = shape {
    return w;
}

let Shape.Circle(r) = shape else {
    return 0;
};
```

These examples are syntax-only until the matching stage lands; the cast
spelling in expressions is the existing `expr as T` form.

## Reference-Level Design

### 1. Variant forms and identity

Enums keep nominal identity per RFC 0005. Variants remain constructors, not
types. Unit and tuple variants are unchanged. The new record form is
`Name { f: T, g: U }` with named payload fields. A variant is exactly one of
the three forms; mixed-form enums are allowed. Variant payload types are
resolved by the binder and checked as nominal signature members.

The default discriminant is a compiler-internal tag with a smallest-width
rule (one byte minimum); its in-memory representation and width-override
attributes are layout-RFC work and are not observable in this RFC. The
checker uses an abstract discriminant for match analysis without promising a
byte layout.

### 2. Grammar and AST deltas (complete inventory)

All deltas change `docs/spec/ZomParser.g4`, the hand-written parser
(`compiler/parser/syntax/declaration-parser.cc`, `statement-parser.cc`,
`type-parser.cc`), CST recovery codecs, and `compiler/ast/schema.yml`
together, with grammar-oracle and corpus parity.

1. `enumVariant` gains the brace alternative; delete the current ZOM2076
   rejection and the open-brace variant drop; variant-boundary recovery
   learns a record-field brace inside an enum body.
2. New record-variant AST node and record-field list (free schema slots in
   the variant family), distinct from a struct declaration node.
3. Record-variant *pattern*: add the qualified dotted-path-plus-brace form
   (`Shape.Rect { w, h }`). This reconciles an existing divergence where the
   hand-written parser accepts dotted type paths but the ANTLR `patStruct`
   accepts only a bare identifier.
4. A real `OrPattern` production `Pattern '|' Pattern` with a new schema node
   (free pattern slots), requiring identical binding names and types across
   alternatives. The current accidental parse of `|` as a bitwise-or
   expression inside a pattern position must no longer occur.
5. `if (let Pattern = Expression)` as a new `ifStatement` alternative with an
   optional `else`, and a `let ... else` tail on the variable declaration
   production whose block must diverge (`return`, `raise`, `break`/`continue`
   in scope, or diverging call).
6. `#[zom::non_exhaustive]` added to the closed built-in single-segment
   attribute set (grammar rule, hand-parser allowlist, chapter 16). Per-
   variant attributes remain rejected.

### 3. Exhaustiveness

- The checker builds constructor coverage over the enum's complete variant
  set from the canonical signature inventory (`NominalSignature.variants`),
  recursing into tuple and record payload fields; wildcard covers the
  remainder.
- A guarded arm is non-total for its variant; exhaustiveness requires an
  unguarded arm or wildcard that covers the residual witnesses.
- Missing coverage projects the uncovered witness list through the existing
  ZOM4022 `CheckerNonExhaustiveMatch` (already reserved for this feature).
  Unreachable arms project ZOM4023 `CheckerUnreachableMatchArm` (warning),
  driven by `ExhaustivenessFact.unreachableArms`.
- An irrefutable pattern is required in `let`, function parameters, and
  `for` binding position; a refutable pattern there without an `else` is
  rejected. A refutable pattern in a condition without fallback is rejected.
  These two placement errors need new codes (see section 8); they are not
  the same diagnostic as non-exhaustiveness.
- `let ... else` requires the else block to diverge; non-divergence needs a
  new code.

### 4. Cross-module `#[zom::non_exhaustive]`

- In the enum's defining module, all variants are visible and exhaustive
  matching without a wildcard is allowed.
- In an importing module, the enum is treated as having an unknown residual
  variant: matching requires a wildcard, pattern construction of hidden
  payloads is rejected, and the attribute contributes to the published
  module interface (section 6).
- The attribute is recorded in the signature and export surface; its absence
  preserves the current closed-enum behavior. This fills the cross-crate
  purpose rather than imposing an extra local wildcard.

### 5. Destructuring and ownership interaction

Destructuring a non-`Copy` binding moves the bound fields using the existing
RFC 0007 move-path machinery; `Copy`-evidence fields are copied (the language
term is `Copy` per chapter 14, not a separate "Shared binding mode").
Partial moves are tracked per field with the existing `MovePathFact` and
three-state initialization facts; the parent is unavailable whole while
sibling fields remain live. Existing linear obligations, if a moved field is
linear in the future, ride ZOM4063/ZOM4065; this RFC introduces no new
ownership diagnostics. General closure capture and full typestate remain out
of scope.

### 6. Binder and module export surface

- Variant constructors (unit, tuple, record) and record field names become
  members of the enum's module-visible declaration and are included in
  `VerifiedBindingOutput` resolution.
- `VerifiedModuleInterface` / `VerifiedExportSurface` publishes each enum's
  variant set, payload field names and types, and the `non_exhaustive` flag,
  so cross-module matching and construction resolve from the export revision
  (RFC 0039). This is an additive interface change staged with the semantic
  implementation; nothing is exported before the checker admits enums.

### 7. HIR and MIR realization

HIR gains match expression/statement, enum-variant construction, and
record/tuple destructuring records. MIR lowering requires two vocabulary
additions that this RFC specifies and allocates codec bytes for, rather than
silently assuming:

1. a discriminant-read rvalue (reading the abstract enum tag of a place),
   complementing the existing write-only `SetDiscriminant`; and
2. match dispatch lowers through the existing `SwitchInt` terminator with a
   new discriminant operand; arm joins reuse the current
   temporary/slot-and-branch lowering for the admitted control shapes.

Full multi-arm match with arbitrary joins is an RFC 0048 construct family;
this RFC coordinates the vocabulary delta with that work. Until the
discriminant-read node and structural MIR admission exist, match bodies
remain fail-closed capabilities. There is no heap allocation, box, or
aggregate layout requirement for plain C-style and payload-bearing enums in
this scope.

### 8. Diagnostics

Existing codes are reused and must not be duplicated:

| Meaning | Code | State |
|---|---|---|
| Non-exhaustive match with uncovered witnesses | ZOM4022 `CheckerNonExhaustiveMatch` | Registered, reserved for this feature; gains its producer |
| Unreachable match arm | ZOM4023 `CheckerUnreachableMatchArm` | Registered warning; gains its producer |
| Orphan impl for enum marker/interface impls | ZOM4054 `OrphanImpl` | Already produced by coherence |
| Linear unconsumed / consumed twice in destructuring | ZOM4063 / ZOM4065 | Already produced on the ownership rail |

New codes, allocated from sparse checker slots at implementation time and
entered in `tests/coverage/diagnostic-reservations.json` before any test
asserts them: pattern-in-irrefutable-position, pattern-refutable-needs-
fallback, non-diverging `let ... else` block, record-pattern field mismatch,
foreign-module construction of a `non_exhaustive` payload. Candidate slots
include 4027/4034/4042/4043/4053/4067/4068/4086/4087; exact binding is part of
the implementing change, never the 3000-3999 binder/module range. No
spawn-boundary code is added (ZOM4095 owns that rejection).

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| Lexer and grammar oracle | `docs/spec/ZomLexer.g4`, `docs/spec/ZomParser.g4`, grammar runner | lexer-parser |
| Parser, CST recovery, AST schema | `compiler/parser/**`, `compiler/cst/**`, `compiler/ast/schema.yml` | lexer-parser |
| Binder variant resolution, scopes, export surface | `compiler/binder/**` | module-system |
| Checker exhaustiveness, pattern typing, coherence, module visibility | `compiler/checker/**`, `compiler/type/**` | binder-checker |
| Match/variant HIR and MIR discriminant vocabulary | `compiler/hir/**`, `compiler/mir/**` (coordinated with RFC 0048) | ir-backend |
| Destructuring move facts | `compiler/ownership/**` | ir-backend |
| Module interface publication | `compiler/driver/interface/**` | module-system |
| Diagnostics and reservations | `compiler/diagnostics/defs/**`, `tests/coverage/diagnostic-reservations.json` | error-system |
| Normative spec | chapters 03, 05, 06, 07, 10, 12, 13, 16, 22, 23 and `17-grammar-reference.md` | spec-audit |
| Tests and gates | `tests/conformance/**`, `tests/unittests/**`, `scripts/` | verification |
| RFC governance | `docs/rfc/README.md`, `scripts/check-rfc.py` | rfc |

## Security And Safety Impact

Exhaustiveness is the safety contribution: a sum value cannot be silently
handled as the wrong variant, and payload access is only possible through a
matching constructor. Destructuring moves preserve the existing move and
linear guarantees; no unsafe or allocation behavior is introduced. Foreign
`non_exhaustive` rules prevent cross-module construction of variants the
defining crate may change.

## Drawbacks And Risks

- Match lowering needs the new discriminant-read node and codec byte;
  coordinating with RFC 0048 is required and a sequencing slip would leave
  matching fail-closed (the acceptable fallback, never silent acceptance).
- The or-pattern bitwise-or misparse must be removed or existing code could
  silently change meaning; corpus parity and a dedicated negative test guard
  it.
- Record-variant grammar changes touch enum-boundary recovery; the parser
  and oracle must change together or the grammar-runner matrix fails.
- Export-surface additions must be additive; publishing variants before
  checker admission would create a hollow interface, so the stages land in
  one transaction per the rollout.

## Alternatives Considered

- **Sealed-class dispatch instead of enum matching.** Rejected: it adds a
  second sum mechanism; enums are already in the grammar and signatures.
- **Keep only unit/tuple variants.** Rejected: record payloads are the common
  named-field case and the parser/CST already structure record-style fields;
  forcing tuple payloads hurts readability and exhaustiveness witness
  clarity.
- **Encode matching through existing comparison rvalues only.** Rejected: a
  discriminant read is a distinct operation; overloading comparison would
  mis-type tag access and block payload downcast.
- **Fold `non_exhaustive` into visibility without an attribute.** Rejected:
  it needs a residual-variant semantics distinct from private fields.

## Compatibility And Rollout

Pre-1.0; no shims. Each stage fail-closed and independently tested:

1. Grammar/AST/recovery/oracle for record variants, record patterns,
   or-patterns, `if let`, `let ... else`, the attribute; invert the current
   negative record-variant fixtures to positive and add new negatives.
2. Binder variant/field resolution and additive export-surface publication.
3. Checker exhaustiveness, refutability placement, guards, cross-module
   `non_exhaustive`, wiring ZOM4022/ZOM4023 producers and the new codes.
4. HIR match records and the MIR discriminant-read vocabulary as an RFC 0048
   family, byte-parity gated.
5. Destructuring move integration.

Spec chapters (03, 05, 06, 07, 10, 12, 13, 16, 22, 23) and the grammar
reference change with their stages; unimplemented stages remain typed
capability rejections.

## Documentation And Teaching Plan

Normative prose lands in the listed chapters and the grammar reference.
Chapter 07 already contains brace-variant illustrations the parser currently
rejects; those become accurate in stage 1. Chapter 08/14 product content is
untouched. A current-state design note records progress per evidence class.

## Operational Readiness

No runtime, allocation, or target dependency is introduced; existing Linux
x86-64 CI lanes suffice. No release readiness is implied by DRAFT.

## Acceptance Criteria

- Both grammars, the hand-written parser, CST recovery, and AST schema admit
  record variants, real or-patterns, `if let`, and `let ... else` with
  grammar-runner parity and inverted/added fixtures.
- Exhaustiveness and unreachable-arm producers fire ZOM4022/ZOM4023; the new
  placement/divergence codes are registered and reserved; no duplicate of an
  existing code exists.
- `non_exhaustive` behaves identically in the defining module and forces
  wildcard plus construction rejection in an importing module.
- Variant and payload entries appear in the verified module interface and
  resolve cross-module.
- Admitted match bodies reach VerifiedBuiltMir through the discriminant-read
  vocabulary; non-admitted shapes fail closed; the 923/64 parity channels
  stay byte-green.
- All listed spec chapters and the grammar reference agree with the parser.

## Implementation Plan

1. Resolve open questions and move through REVIEW to ACCEPTED.
2. Land the grammar/AST/recovery stage with oracle and fixture inversions.
3. Land binder/export then checker semantics behind typed rejections.
4. Land HIR/MIR match lowering coordinated with RFC 0048.
5. Register diagnostics and reservations per stage.

## Test Plan

- Build: `cmake --preset sanitizer && cmake --build --preset sanitizer`.
- Unit tests: checker exhaustiveness witness/unreachable tests, binder
  variant resolution, interface export, and MIR discriminant lowering under
  the existing checker/hir/mir ztest targets.
- Lit tests: one `.check` per new code asserting the `ZOMxxxx` id, plus
  positive record-variant, or-pattern, if-let, let-else, and cross-module
  cases under `tests/conformance/`; negative fixtures for misparse guards,
  field mismatch, and foreign construction.
- Conformance: grammar runner matrix; process and IR parity via
  `python3 scripts/check-ir-parity.py --check --ir --zomc <built zomc>
  --snapshot tests/coverage/corpus-ir-parity.json` (the script takes an
  explicit binary and is also run by the CI wrapper, not registered as a
  standalone CTest label).
- Generated files: regenerated AST headers and coverage inventories with
  their generators in the same change.
- Format: `python3 scripts/check-format.py`; `python3 scripts/check-rfc.py`;
  the spec-alignment skill gates.

## Open Questions

- Do record-variant field names share the module namespace with enum
  constructors, or live only under the variant path?
- Should `if let` without an `else` be a scoped binding statement or desugar
  to an explicit match?
- Does `for` destructuring stay limited to irrefutable tuple patterns in this
  RFC, or take enum patterns with a divergence rule?

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-09-14 | DRAFT | Initial broad ADT draft. |
| 2026-09-14 | REVIEW | Round 1 owner review. |
| 2026-09-15 | RETURNED | Round 1 found scope and prerequisite blockers; decision to split to enum and match only. |
| 2026-09-15 | DRAFT | Revised scope: record variants, exhaustive matching, if-let/let-else, or-patterns, non_exhaustive; products/boxing/layout/variance/derive deferred; existing diagnostics reused. |
