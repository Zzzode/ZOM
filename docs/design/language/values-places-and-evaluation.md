# Values, Places, And Evaluation

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative language design note |
| Coverage | Partial |
| Last verified | 2026-09-14 |
| Normative sources | [Types](../../spec/chapters/03-types.md), [Expressions](../../spec/chapters/04-expressions.md), [Declarations](../../spec/chapters/06-declarations.md), [Ownership, Borrowing, And Cleanup](../../spec/chapters/14-memory-management.md) |
| Governing decisions | [RFC 0005](../../rfc/0005-type-system-architecture.md), [RFC 0007](../../rfc/0007-borrow-lifetime-ownership-checker.md), [RFC 0009](../../rfc/0009-call-dispatch-and-operator-lowering.md), [RFC 0010](../../rfc/0010-intermediate-representation-pipeline.md), [RFC 0013](../../rfc/0013-ownership-analysis-integration-boundary.md) |
| Production evidence | [Checked facts](../../../compiler/checker/inference/checked-facts.h), [body checker](../../../compiler/checker/body/body-checker.cc), [HIR records](../../../compiler/hir/hir-module.h), [recursive HIR builder](../../../compiler/hir/build/), [MIR interface](../../../compiler/mir/built-mir.h), [ownership rail](../../../compiler/ownership/) |
| Verification evidence | [Checked-facts tests](../../../tests/unittests/compiler/checker/checked-facts-test.cc), [HIR tests](../../../tests/unittests/compiler/hir/hir-module-test.cc), [MIR tests](../../../tests/unittests/compiler/mir/built-mir-test.cc), [ownership tests](../../../tests/unittests/compiler/ownership/), [session integration tests](../../../tests/unittests/compiler/driver/compiler-session-package-test.cc) |

The specification defines an affine ownership language in which expressions
produce values and consuming operations copy or move according to verified
`Copy` evidence. It uses the term *place* in mutation, ownership, and indexing
rules but does not yet define one complete source-level value/place taxonomy.
The production compiler implements the model for the admitted constructor set
documented in [the lowerable constructs inventory](../ir/lowerable-constructs.md):
scalar and aggregate initializers, parameter/local reads, primitive binary
expressions with one-level nesting, same-module calls, field projections,
mutable writes, conditionals, reducible loops, and bounded borrows. General
place projections, compound assignment, casts, closures, and complete
ownership typestate remain open gaps.

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
The production HIR emits values for scalar literals, parameter and local
references, aggregate elements, and primitive binary leaves; place-returning
references and field projections emit the `Place` value category. General
place reads (arbitrary index chains, dereferences) are not yet lowered.

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

These data models are implemented for the admitted shapes: the body checker
publishes place facts for local references and aggregate field projections,
HIR consumes them through local-reference and field-projection records, and
the recursive builder emits the `Place` category for those forms. General
projection chains (dynamic index, dereference, downcast, subslice) remain an
accepted target without end-to-end support.

### Mutation And Assignment

The specification permits only `mut` bindings to be reassigned or used as
mutable places. A `let` binding may be read, moved, or immutably borrowed but
not reassigned or mutably borrowed. Assignment to an owning place is also a
consuming context and reinitializes a completely assigned place.

The parser represents plain and compound assignment, including right-associative
assignment syntax. Mutable local writes (`mut x = ...; x = <literal, parameter,
or primitive binary>`) are implemented end to end, including repeated writes,
and field writes land on the admitted aggregate shape. Compound assignment
still has no producer: its fact map is emitted empty and the construct fails
closed. The `MirStatement::Assign` operation emitted for scalar module
initialization is not itself evidence of compound assignment.

### Borrows

A borrow refers to a place without owning its stored value. Shared borrows may
overlap; a mutable borrow excludes overlapping shared and mutable borrows; a
live loan prevents moves from overlapping places; and a reference must not
outlive its referent. These are normative rules in
[Chapter 14](../../spec/chapters/14-memory-management.md#143-references-and-borrows).

The frontend publishes a verified borrow-evidence boundary and carries its
revision through CheckedModule, HIR, and Built MIR. A bounded ownership rail
is in production over admitted reducible CFGs: move paths, flow,
initialization, bounded loans/references/reborrows, conflicting-loan and
use-after-move rejection on the source rail, proof validation, and
drop/coroutine elaboration into `VerifiedExecutableMir` (see the
[ownership note](../ir/ownership-and-executable-mir.md)). General region
liveness, capture/escape completeness, and full typestate remain gaps;
borrow-evidence lineage alone never proved the complete rules.

### Temporaries And Cleanup

The checker and MIR data models represent temporary roots, temporary locals,
storage lifetime statements, deinitialization, and overwrite. Production
lowering synthesizes temporary locals for one-level nested binary operands
(`a + b * c`) and emits `StorageLive`/`StorageDead` for the admitted shapes.
General deinitialization and cleanup for all resources are bounded: drop
elaboration emits drops only for resources live on every exit of admitted
bodies.

The deterministic cleanup rules in
[Chapter 14](../../spec/chapters/14-memory-management.md#145-deinitialization)
remain normative language requirements; their general MIR realization is an
open implementation boundary tracked by the ownership and RFC 0006 work.

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
compound-assignment writeback. Both RFCs are `IMPLEMENTING`. The admitted
place, call, and binary shapes now lower through the recursive HIR builder with
deterministic destination ordering, while general index, compound-assignment,
and generic-dispatch shapes still have no production lowering.

Consequently, this note presents the exact order only for the admitted shapes;
the full RFC order is not yet a current language guarantee and the authority
gap must be closed in the normative specification before user code can rely
on the remainder.

## Compiler Realization

| Stage | Current responsibility | Production boundary |
|---|---|---|
| Parser | Preserves assignment shape, operand structure, calls, indexes, and projections in the immutable AST | Syntax acceptance does not establish semantic evaluation |
| Binder | Resolves definitions and constructs lexical visibility | Traversal order is not runtime evaluation order |
| Body checker | Publishes verified literal, type, declaration, place, binary, call, and borrow facts for the admitted subset | Compound assignment, casts, generic bodies, and the empty fact families fail closed |
| CheckedModule | Binds checked facts and verified borrow evidence into one handoff | Ownership results are produced by the successor rail |
| HIR | Lowers admitted literals, references, binaries, aggregates with projection, local writes, calls, control, and bounded borrows through the recursive and legacy construction paths | Casts, compound assignment, closures, and generics are not admitted |
| Built MIR | Emits single- and multi-block bodies for the inventory, including calls, diamonds, reducible loops, projections, and storage markers | General executable bodies and full ABI lowering are absent |
| Ownership and backend | Derives bounded ownership facts and executable MIR, then reaches LIR/LLVM/object/link/run for admitted Linux x86-64 shapes | Partial; see the IR design notes |

The admitted Built MIR shapes are enumerated construct-by-construct in
[the lowerable constructs inventory](../ir/lowerable-constructs.md); they
include scalar module initializers, parameterized functions with sequential
locals, calls with continuation blocks, four-block conditionals, reducible
loops, aggregates, and borrow scopes.

No broader source form should be inferred from the representational capacity
of the MIR classes.

## Evidence Map

| Claim | Class | Specification or RFC | Implementation | Native verification |
|---|---|---|---|---|
| Affine copy-or-move transfer for admitted bodies | Implemented (partial) | [Chapter 14 §14.2](../../spec/chapters/14-memory-management.md#142-value-transfer) | Ownership move/init facts and use-after-move rejection over admitted reducible CFGs | Ownership fact and overlay suites, lit diagnostics |
| Complete affine transfer for all source forms | Open gap | [Chapter 14 §14.2](../../spec/chapters/14-memory-management.md#142-value-transfer) | General region/capture/typestate incomplete | Not established by the admitted rail |
| Mutable-place requirement | Implemented (partial) | [Chapter 6 value declarations](../../spec/chapters/06-declarations.md#value-declaration) | `CheckedPlaceFact` carries `mutablePlace`; mutable local writes and field writes produced | Checked-facts and HIR/MIR write-body tests |
| Source place algebra for references and field projections | Implemented (partial) | [RFC 0005](../../rfc/0005-type-system-architecture.md) | Checked place facts consumed by recursive HIR place records and MIR projections | HIR module and ownership tests |
| General source place algebra (index, deref, downcast, subslice) | Accepted target | [RFC 0005](../../rfc/0005-type-system-architecture.md) | No end-to-end producer for general chains | Checked-facts tests exercise constructed records only |
| Exact assignment evaluation order on admitted shapes | Implemented (partial) | [RFC 0005](../../rfc/0005-type-system-architecture.md), [RFC 0009](../../rfc/0009-call-dispatch-and-operator-lowering.md) | Destination-driven lowering for local and field writes | HIR parity and MIR assertion tests |
| General assignment order and compound assignment | Accepted target and open normative gap | RFC 0005, RFC 0009 | Compound fact map empty; generic shapes fail closed | Parser tests cover shape only |
| Literal/reference/binary/aggregate HIR | Implemented | [RFC 0010](../../rfc/0010-intermediate-representation-pipeline.md), [RFC 0048](../../rfc/0048-recursive-ir-construction-and-structural-verification.md) | Recursive HIR builder families 1-4 plus legacy arms | [HIR tests](../../../tests/unittests/compiler/hir/hir-module-test.cc), corpus parity |
| Multi-block Built MIR for the inventory | Implemented | [RFC 0010](../../rfc/0010-intermediate-representation-pipeline.md) | Calls, conditionals, reducible loops, aggregates, borrow scopes | [MIR tests](../../../tests/unittests/compiler/mir/built-mir-test.cc), session integration tests |
| Ownership proofs and executable MIR for admitted shapes | Implemented (partial) | [RFC 0007](../../rfc/0007-borrow-lifetime-ownership-checker.md), [RFC 0013](../../rfc/0013-ownership-analysis-integration-boundary.md) | Overlay, validation, drop/coroutine elaboration, `VerifiedExecutableMir` | Ownership overlay and lineage mutation suites |
| General ownership checking and complete cleanup | Open gap | RFC 0007 | Region/escape/capture/typestate and general drop coverage incomplete | Not established end to end |
| Native execution of admitted scalar shapes | Implemented (host slice) | [RFC 0043](../../rfc/0043-platform-link-and-executable-publication.md) | LIR/LLVM/link/run on Linux x86-64 | `native-run-cli`, object-emission fixtures |

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
- The production body checker does not publish general place-projection or
  compound-assignment facts; compound assignment fails closed.
- HIR and MIR admit the construct inventory, not general expressions: casts,
  closures, generics in bodies, destructuring patterns, and exhaustiveness
  remain unlowered.
- Complete ownership typestate, general region and capture analysis, and full
  cleanup elaboration are open; the live rail covers admitted reducible CFGs.
- The backend consumes the model only for admitted shapes on Linux x86-64;
  general ABI and target coverage do not exist.
