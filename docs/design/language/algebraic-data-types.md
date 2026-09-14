# Algebraic Data Types

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative language design note |
| Coverage | Parser surface and one admitted aggregate slice; enum declarations are syntax-only |
| Last verified | 2026-09-14 |
| Normative sources | [Classes And Structures](../../spec/chapters/08-classes-and-structures.md), [Enumerations](../../spec/chapters/10-enumerations.md), [Patterns](../../spec/chapters/07-patterns.md), [Generics](../../spec/chapters/12-generics.md) |
| Governing decisions | [RFC 0002](../../rfc/0002-parser-architecture.md) (parser, LANDED); [RFC 0049](../../rfc/0049-algebraic-data-type-design.md) (complete ADT model, DRAFT, open proposal) |
| Production evidence | [AST schema](../../../compiler/ast/schema.yml), [parser implementation](../../../compiler/parser/), [surface admission](../../../compiler/ownership/admission/surface-admission.cc), [HIR aggregate records](../../../compiler/hir/hir-module.h) |
| Verification evidence | Conformance corpus aggregate cases, [HIR tests](../../../tests/unittests/compiler/hir/hir-module-test.cc) |

This note describes which ADT surface exists in the production compiler today.
The comprehensive product/sum model (record variants, newtype, exhaustive
matching, recursive boxing, variance, derives, niches, repr controls) is an
open proposal in DRAFT [RFC 0049](../../rfc/0049-algebraic-data-type-design.md);
it is not an accepted target and none of its diagnostics are registered. A
2026-06 design note that presented that proposal as a "current canonical
design" was superseded by RFC 0049 and replaced by this current-state note.

## Question

Which parts of the algebraic data type surface parse, which are admitted to
semantic analysis and lowering, and which are grammar acceptance without a
semantic implementation?

## Current Model

### Structs and classes

- The grammar accepts named-field `struct` and `class` declarations per
  chapter 08; the parser entry points and AST schema nodes exist.
- A struct-literal expression `T { field: value, .. }` with scalar literal
  field values parses, passes aggregate surface admission
  (`isAdmittedAggregateInitializer`), checks, lowers through the recursive HIR
  aggregate arm (`HirNominalAggregateExpression` and field elements), reaches
  Built MIR as a `NominalAggregate` rvalue, and is included in the construct
  inventory.
- Field projection returns (`return cell.field`) and admitted field writes
  lower for the aggregate local shape.
- Class aggregate bodies are class-agnostic through checker/HIR/MIR/LIR; the
  class aggregate is refused at the function-declaration anchor by the
  capability legality inventory (the `class_literal_body_shape_neg_03`
  conformance case pins this). Class declarations are therefore parsed but do
  not have a general aggregate-construction production path.
- Methods, inheritance, initializers/deinitializers, encapsulation behavior,
  and visibility on members are not implemented beyond what chapter 08 states
  and the admitted checker surface enforces.

### Enums

- The grammar accepts unit variants and parenthesized tuple variants per
  chapter 10; the parser produces an `EnumDeclaration` schema node and a
  variant list (`parseEnumVariantList`, with an empty-variant-list factory).
- Enum declarations are syntax-only: there is no enum semantic admission, no
  variant constructor resolution, no discriminant or payload type checking,
  no HIR/MIR lowering, and no code generation for enums.
- Record (brace) variants are not in the grammar.

### Pattern matching

Chapter 07 describes pattern forms; binding patterns in admitted declarations
work. `match`, refutable patterns in `if let`/`let-else`, guards,
or-patterns, exhaustiveness checking, and pattern diagnostics are not
implemented; the relevant surface is rejected fail-closed rather than
silently accepted.

### Generics, layout, and markers on ADTs

Generic ADT declarations parse, but generic function and method bodies are
rejected by the checker; variance attributes, derive, niche optimization,
`#[zom::repr(...)]` controls, automatic recursive boxing, and linear
destructuring from RFC 0049 do not exist in grammar attributes, the diagnostic
catalog, or lowering.

## Semantic Invariants

Only invariants with both normative and production backing apply today:

1. A parsed declaration is not a semantic type; enum and most class surface
   acceptance establishes no implementation.
2. An admitted struct aggregate must agree field-by-field with the checked
   declaration; uninitialized aggregate fields and sibling-field escapes are
   rejected on the source rail (the `uninitialized_aggregate_*` conformance
   cases).
3. Class aggregate construction outside the admitted path is a capability
   rejection, not an internal error.

## Compiler Realization

| Stage | Structs/classes | Enums |
|---|---|---|
| Lexer/parser/CST | Chapters 08 grammar accepted; event stream emitted | Unit and tuple variants accepted |
| AST | Struct/class declaration nodes | `EnumDeclaration` node and variant lists |
| Binder/checker | Named-field struct aggregates admitted for the literal shape; class aggregate capability rejection | No semantic admission |
| HIR/MIR | Aggregate initializer, field projection, admitted field writes | None |
| LIR/backend | Admitted aggregate-field shape reaches the Linux x86-64 slice | None |

## Evidence Map

| Claim | Class | Evidence |
|---|---|---|
| Struct/class/enum surface parses | Implemented | Chapter 08/10 grammar, ANTLR oracle matrix, parser entry points, AST schema |
| Struct-literal aggregates reach Built MIR | Implemented | Aggregate HIR arm, `NominalAggregate` rvalue, HIR tests, lowerable inventory |
| Class aggregate refusal | Implemented | `class_literal_body_shape_neg_03` capability diagnostic |
| Enum semantics, matching, variants with codegen | Open gap | No checker/HIR/MIR producer beyond parsing |
| Record variants, newtype, variance, derive, niches, repr, boxing, exhaustiveness | Open proposal | DRAFT RFC 0049 only; no registered diagnostics |

## Known Gaps

- Enum semantic admission and all match/exhaustiveness machinery (RFC 0049).
- General aggregate shapes: positional and tuple products, methods called on
  aggregate values beyond the one receiver shape, class construction.
- The full RFC 0049 proposal set; its codes are unregistered and its grammar
  deltas are not accepted.
- Examples in chapter 10 that show tuple-variant enums and matching are
  syntax-only illustrations until the semantics land.
