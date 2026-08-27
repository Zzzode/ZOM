# Values, Places, And Evaluation

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative language design note |
| Coverage | Partial |
| Last verified | 2026-07-24 |
| Normative sources | [Types](../../spec/chapters/03-types.md), [Expressions](../../spec/chapters/04-expressions.md), [Declarations](../../spec/chapters/06-declarations.md), [Ownership, Borrowing, And Cleanup](../../spec/chapters/14-memory-management.md) |
| Governing decisions | [RFC 0005](../../rfc/0005-type-system-architecture.md), [RFC 0007](../../rfc/0007-borrow-lifetime-ownership-checker.md), [RFC 0009](../../rfc/0009-call-dispatch-and-operator-lowering.md), [RFC 0010](../../rfc/0010-intermediate-representation-pipeline.md), [RFC 0013](../../rfc/0013-ownership-analysis-integration-boundary.md) |
| Production evidence | [Checked facts](../../../compiler/checker/inference/checked-facts.h), [body checker](../../../compiler/checker/body/body-checker.cc), [HIR interface](../../../compiler/hir/hir-module.h), [HIR lowering](../../../compiler/hir/hir-module.cc), [MIR interface](../../../compiler/mir/built-mir.h), [MIR lowering](../../../compiler/mir/built-mir.cc) |
| Verification evidence | [Checked-facts tests](../../../tests/unittests/compiler/checker/checked-facts-test.cc), [HIR tests](../../../tests/unittests/compiler/hir/hir-module-test.cc), [MIR tests](../../../tests/unittests/compiler/mir/built-mir-test.cc), [session integration tests](../../../tests/unittests/compiler/driver/compiler-session-package-test.cc) |

The specification defines an affine ownership language in which expressions
produce values and consuming operations copy or move according to verified
`Copy` evidence. It uses the term *place* in mutation, ownership, and indexing
rules but does not yet define one complete source-level value/place taxonomy.
The production compiler does not implement the intended model end to end. It
currently admits scalar literal initializers and a narrow scalar-return
function form into HIR and Built MIR. General places, source assignments,
temporaries, ownership analysis, and cleanup are outside that production
subset.

## Question

When source code names, reads, writes, borrows, or consumes data, what is the
semantic object being operated on, and which parts of that model are enforced
by the compiler today?

This distinction is foundational. Type checking, evaluation order, borrowing,
move analysis, cleanup, and backend lowering must agree on whether an operation
uses a computed value or an addressable storage location. A compiler data type
that can represent a place is insufficient unless the production pipeline
constructs, verifies, and consumes it.

## Current Model

### Values

A value is the typed result of evaluating an expression. Literals directly
produce values. Reading a place also produces a value, but that read has a
transfer mode:

- a type with verified `Copy` evidence leaves the source initialized; and
- every other consumed value moves from the source and leaves the source
  uninitialized.

The normative transfer contexts and reinitialization rules are defined by
[Chapter 14](../../spec/chapters/14-memory-management.md#142-value-transfer).
The current production HIR represents only scalar literal values. It does not
yet represent a general read from a source place.

### Places

A place is a typed storage location, not the value currently stored there. The
checker data model describes a place as:

- one root: a definition, a dereference result, or a temporary;
- an ordered sequence of field, tuple-index, or dynamic-index projections;
- a semantic type; and
- independent mutable and movable capabilities.

Built MIR uses a lower-level form: a function-local root plus field, index,
dereference, downcast, or subslice projections. The two forms serve different
stages. A checked source place carries source semantic authority; a MIR place
identifies storage after lowering.

These data models are implemented, but the production body checker currently
publishes no place facts and HIR rejects any non-empty place-fact set. They are
therefore an accepted compiler contract, not current end-to-end language
support.

### Mutation And Assignment

The specification permits only `mut` bindings to be reassigned or used as
mutable places. A `let` binding may be read, moved, or immutably borrowed but
not reassigned or mutably borrowed. Assignment to an owning place is also a
consuming context and reinitializes a completely assigned place.

The parser represents plain and compound assignment, including right-associative
assignment syntax. The body fact inventory requests semantic facts for these
forms, but the current producer does not construct the required place and
compound-assignment facts. It fails closed instead. The `MirStatement::Assign`
operation emitted for compiler-generated scalar module initialization is not
evidence that source assignment is implemented.

### Borrows

A borrow refers to a place without owning its stored value. Shared borrows may
overlap; a mutable borrow excludes overlapping shared and mutable borrows; a
live loan prevents moves from overlapping places; and a reference must not
outlive its referent. These are normative rules in
[Chapter 14](../../spec/chapters/14-memory-management.md#143-references-and-borrows).

The frontend publishes a verified borrow-evidence boundary and carries its
revision through CheckedModule, HIR, and Built MIR. Production ownership
analysis and ownership-proof publication are not implemented. Borrow-evidence
lineage proves that the admitted frontend evidence is the evidence consumed by
later stages; it does not prove the complete ownership rules by itself.

### Temporaries And Cleanup

The checker and MIR data models can represent temporary roots, temporary locals,
storage lifetime statements, deinitialization, and overwrite. The current
production lowering does not create source temporaries, emit general
deinitialization, or elaborate cleanup paths.

The deterministic cleanup rules in
[Chapter 14](../../spec/chapters/14-memory-management.md#145-deinitialization)
remain normative language requirements. Their MIR realization is an open
implementation boundary.

## Semantic Invariants

The following invariants are normative:

1. Mutation requires a mutable place; mutability is not inferred from the value
   type alone.
2. A consuming operation copies only with verified `Copy` evidence and otherwise
   moves.
3. Moving invalidates the affected place until a complete reassignment
   reinitializes it.
4. Borrow legality is defined over overlapping places and referent regions.
5. Cleanup follows the live ownership obligation, including after moves and
   partial initialization.

The following working model and invariants are accepted compiler targets but
are not yet complete normative or production guarantees:

1. Values and the places storing them are distinct semantic concepts.
2. Checked place roots and projections are lowered once into MIR places.
3. MIR operands preserve explicit `Copy`, `Move`, and constant uses.
4. Assignment acquires its destination once, evaluates the required value, and
   stores at most once.
5. Ownership analysis consumes verified Built MIR and borrow evidence rather
   than reconstructing decisions from AST syntax.

## Evaluation Order Boundary

The specification identifies short-circuit operators and defines control-flow
order for loop conditions, but it does not currently state a complete
language-wide operand, argument, projection, and assignment evaluation order.

RFC 0005 and RFC 0009 define an exact left-to-right target contract for place
roots, projections, index acquisition, assignment right-hand sides, and
compound-assignment writeback. Both RFCs are `IMPLEMENTING`. The current HIR
rejects the relevant place, call, index, and compound-assignment facts, so this
target contract has no general production lowering.

Consequently, this note does not present a complete evaluation order as a
current language guarantee. The authority gap must be closed in the normative
specification before user code can rely on the full RFC order.

## Compiler Realization

| Stage | Current responsibility | Production boundary |
|---|---|---|
| Parser | Preserves assignment shape, operand structure, calls, indexes, and projections in the immutable AST | Syntax acceptance does not establish semantic evaluation |
| Binder | Resolves definitions and constructs lexical visibility | Traversal order is not runtime evaluation order |
| Body checker | Publishes verified scalar literal, type, declaration, and related facts for the admitted subset | Place and compound-assignment requirements have no production producer |
| CheckedModule | Binds checked facts and verified borrow evidence into one handoff | Does not publish ownership results |
| HIR | Publishes scalar declarations, scalar literals, and a narrow scalar-return function form | Rejects places, calls, indexes, compound assignments, and other unsupported fact families |
| Built MIR | Publishes verified module initializers and narrow scalar-return bodies; its algebra represents places, operands, storage, and initialization | General executable bodies, ownership analysis, cleanup elaboration, and backend consumption are absent |

The currently admitted Built MIR shapes are:

- a scalar module declaration lowered to one local, `StorageLive`, one
  initializing assignment from a constant, and a return that moves the local;
  and
- a parameterless, receiverless, non-raising function whose body is exactly one
  scalar-literal return, lowered to a constant return operand.

No broader source form should be inferred from the representational capacity of
the MIR classes.

## Evidence Map

| Claim | Class | Specification or RFC | Implementation | Native verification |
|---|---|---|---|---|
| Affine copy-or-move transfer | Normative | [Chapter 14 §14.2](../../spec/chapters/14-memory-management.md#142-value-transfer) | Production ownership analysis absent | Ownership syntax and diagnostic conformance cases do not establish the complete production rail |
| Mutable-place requirement | Normative | [Chapter 6 value declarations](../../spec/chapters/06-declarations.md#value-declarations) | `CheckedPlaceFact` carries `mutablePlace`; no live producer | Checked-facts tests verify record invariants |
| Source place algebra | Accepted target | [RFC 0005](../../rfc/0005-type-system-architecture.md) | [Checked-facts representation](../../../compiler/checker/inference/checked-facts.h); HIR rejects non-empty place facts | [Checked-facts tests](../../../tests/unittests/compiler/checker/checked-facts-test.cc) exercise constructed facts |
| Exact assignment evaluation order | Accepted target and open normative gap | [RFC 0005](../../rfc/0005-type-system-architecture.md), [RFC 0009](../../rfc/0009-call-dispatch-and-operator-lowering.md) | No general HIR or MIR lowering | Parser tests cover shape only |
| Scalar literal HIR | Implemented | [RFC 0010](../../rfc/0010-intermediate-representation-pipeline.md), [RFC 0013](../../rfc/0013-ownership-analysis-integration-boundary.md) | [HIR lowering](../../../compiler/hir/hir-module.cc) | [HIR tests](../../../tests/unittests/compiler/hir/hir-module-test.cc), [session integration tests](../../../tests/unittests/compiler/driver/compiler-session-package-test.cc) |
| Scalar module initializer Built MIR | Implemented | [RFC 0010](../../rfc/0010-intermediate-representation-pipeline.md), [RFC 0013](../../rfc/0013-ownership-analysis-integration-boundary.md) | [MIR lowering](../../../compiler/mir/built-mir.cc) | [MIR tests](../../../tests/unittests/compiler/mir/built-mir-test.cc), [session integration tests](../../../tests/unittests/compiler/driver/compiler-session-package-test.cc) |
| Production ownership checking and cleanup | Open gap | [RFC 0007](../../rfc/0007-borrow-lifetime-ownership-checker.md) | No ownership-result producer or cleanup elaboration | No end-to-end ownership publication test |

## Known Gaps

- The specification does not define a complete classification of
  value-producing, place-producing, assignable, movable, or borrowable
  expressions, nor the rules for converting a place to a value.
- The specification does not define one complete evaluation-order rule for
  operands, arguments, places, assignment, and temporary destruction.
- The grammar reference restricts assignment to a left-hand-side expression,
  while the ANTLR grammar parses a conditional expression before the assignment
  operator. The normative assignable-place set is therefore not closed.
- The expression chapter describes `&` as borrowing a value, while ownership
  rules operate on places and the grammar admits a general unary operand. The
  normative temporary-borrow boundary is not closed.
- The type-system operator table assigns `in` a `Contains` operation, while the
  expression chapter reserves `in` for loop headers and the expression grammar
  has no binary `in` production.
- The production body checker does not publish general place facts or
  compound-assignment facts.
- HIR admits only scalar literals, scalar module declarations, and one narrow
  scalar-return function form.
- Built MIR represents more operations than the production builder emits.
- Production ownership analysis, ownership-proof publication, temporary
  lifetime lowering, and cleanup elaboration are absent.
- There is no target LIR or backend that consumes the execution model.
