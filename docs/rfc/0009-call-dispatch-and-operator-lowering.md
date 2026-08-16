---
rfc: 9
title: Call Dispatch And Operator Lowering
type: compiler
status: IMPLEMENTING
author: ZOM Compiler Team
review-manager: rfc
required-owners: [rfc, binder-checker, module-system, error-system, ir-backend, spec-audit, verification]
approvers: [rfc, binder-checker, module-system, error-system, ir-backend, spec-audit, verification]
created: 2026-07-08
updated: 2026-07-17
area: compiler
requires: [4, 5, 8, 11]
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0009-review-and-implementation.md#discussion-record
decision: docs/rfc/tracking/0009-review-and-implementation.md#decision-record
implementation: docs/rfc/tracking/0009-review-and-implementation.md#implementation-tracker
tracking-issue: docs/rfc/tracking/0009-review-and-implementation.md#implementation-tracker
---

# RFC 0009: Call Dispatch And Operator Lowering

## Summary

This RFC defines the post-type-check contract that turns RFC 0005's immutable
semantic callable selection for methods, qualified interface calls, ordinary
calls, operators, indexing, and compound assignment into one verified dispatch
plan consumed by RFC 0010. RFC 0005 proves type correctness and selects the
logical callable and semantic receiver adjustment once. This RFC owns the final
static, impl, witness, dyn, or primitive dispatch classification and packages
the checked receiver role and plan without repeating lookup, normalization, or
target ABI slot assignment.

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

- Define one `DispatchTarget` model for direct, concrete-method, impl-method,
  generic-witness, dyn-method, and primitive dispatch.
- Define how receiver arguments are normalized for methods and operators.
- Define the operator-to-interface-method lowering table for `+`, `-`, `*`,
  `/`, `%`, `**`, `==`, `!=`, `<`, `<=`, `>`, `>=`, prefix `-`, `!`, and
  indexing.
- Define a total mapping from RFC 0005 `SelectedCallable` without repeating
  symbol, member, trait, or projection lookup.
- Define verified dispatch facts, their canonical revision, and the RFC 0010
  handoff.
- Define registered invariant diagnostics for missing or malformed successful
  dispatch facts.
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
- `shape.draw()` on a concrete `Sprite` records concrete-method dispatch.
- `shape.draw()` on `dyn Drawable` records logical dyn-method dispatch; target
  lowering assigns the vtable slot.
- `Drawable::draw(shape)` selects the `Drawable` interface method statically
  and passes `shape` as the receiver argument.

When RFC 0005 cannot select one callable, it reports a source diagnostic and
publishes no verified checked facts. RFC 0009 therefore never carries an error
target.

## Reference-Level Design

### Dispatch Input And Ownership

RFC 0005 publishes one `TypedCallFact` or `CompoundAssignmentFact` containing
one complete `CheckedCallEnvelope` for every successful call-like site. RFC
0009 uses `TypedCallFact` for ordinary calls, members, operators, and indexes;
only compound assignment embeds its envelope in the specialized fact. RFC 0009
never
performs member lookup, trait solving, overload ranking, associated projection,
substitution construction, witness construction, or source-name lookup. It
converts that semantic selection into one dispatch plan.

RFC 0009 is the sole owner of `DispatchTarget`:

```text
DispatchTarget =
  Direct { callee: DefId }
  | ConcreteMethod { method: DefId }
  | ImplMethod { impl: ImplId, method: DefId }
  | WitnessMethod { witnessParameter: DefId, interface: DefId,
                    method: DefId }
  | DynMethod { interface: DefId, method: DefId }
  | Primitive { operation: PrimitiveOperation }

DispatchReceiverRole =
  ExplicitFirstArgument | ImplicitSelf
  | OperatorLeftHandSide | OperatorOperand | IndexBase

OrderingRelation = Less | LessEqual | Greater | GreaterEqual

DispatchResultTransform =
  Identity | BooleanNot | CompareOrdering(OrderingRelation)

DispatchArgumentPlan {
  sourceNode: NodeId,
  sourceType: SemanticTypeId,
  parameterType: SemanticTypeId,
  adjustment: Maybe<CoercionAdjustment>,
}

DispatchReceiverPlan {
  role: DispatchReceiverRole,
  passing: ReceiverMode,
  value: CheckedArgumentFact,
  adjustment: ReceiverAdjustment,
}

DispatchFact {
  node: NodeId,
  target: DispatchTarget,
  resultTransform: DispatchResultTransform,
  receiver: Maybe<DispatchReceiverPlan>,
  arguments: Sequence<DispatchArgumentPlan>,
  successType: SemanticTypeId,
  resultType: SemanticTypeId,
  substitutions: Maybe<CanonicalSubstitutionId>,
  witnesses: Maybe<WitnessArgumentsId>,
  raises: Maybe<SemanticTypeId>,
  sourceSpan: SourceSpan,
}
```

`DispatchTarget` tags are `Direct = 0x01`, `ConcreteMethod = 0x02`,
`ImplMethod = 0x03`, `WitnessMethod = 0x04`, `DynMethod = 0x05`, and
`Primitive = 0x06`. `DispatchReceiverRole` tags are `0x01` through `0x05` in
declaration order. `OrderingRelation` tags are `0x01` through `0x04`, and
`DispatchResultTransform` tags are `0x01` through `0x03`. Record fields encode
in declaration order. Every identity expands through RFC 0011; every semantic
type, coercion, substitution, and witness expands through the RFC 0005
canonical codec.

`successType`, `resultType`, and `raises` copy the exact RFC 0005 call envelope.
When `raises` is absent, `resultType == successType`. When `raises` is present,
`resultType` is the canonical normalized union of the disjoint `successType`
and `raises` components and the same node has the matching RFC 0005
`ErrorUnionShapeFact`. `resultTransform` applies only to the successful payload
of the selected callable. It never transforms, reorders, or reconstructs the
residual component.

Substitutions and witnesses occur exactly once in `DispatchFact`, not inside a
target alternative. Their stores remain owned by the exact
`VerifiedCheckedFacts` input. RFC 0008 adopts that value into the session
`CheckedFactsRepository`. `VerifiedDispatchFacts` records the checked revision;
RFC 0010 resolves its handles only through the matching
`CheckedEvidenceLease`. Dispatch facts cannot outlive the session entry or
replace either store.

There is no error target. RFC 0005 source rejection publishes no verified input,
and malformed or incomplete successful input is an invariant failure. There is
also no compiler-intrinsic target because the accepted language and RFC 0011
definition inventory have no semantic intrinsic definition. Adding one requires
an accepted owner RFC, an identity kind, a closed registry, type semantics, and
lowering tests before this algebra changes.

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
| mutable access for `a[i] = value` or `a[i] op= value` | `IndexMut` | `index_mut` | `index_mut(idx: Idx) -> &mut Output` |
| `a in b` | `Contains` | `contains` | `contains(value: Value) -> bool` on `b` |

The remaining accepted operator forms are primitive-only in this RFC:
unary `+`, bitwise not, reference and dereference, pre/post update, shifts,
bitwise binary operations, short-circuit logical operations, strict equality,
and null coalescing. They must carry the matching RFC 0005
`PrimitiveOperation`; no implementation may invent an interface name for them.
Every non-plain assignment operator carries the exact RFC 0005
`CompoundAssignmentOperation` and the normative mapping to its same-stem,
short-circuit, or null-coalescing primitive operation. Plain assignment has no
assignment-operation dispatch target; an indexed or otherwise overloaded place
may have its own child access target.

Primitive numeric, boolean, and built-in array operations use
`DispatchTarget::Primitive`. User-defined operations arrive from RFC 0005 as an
`ImplMethod` or `WitnessMethod`; RFC 0009 does not look up the interface names in
this table. User-defined `==` uses `Identity`; user-defined `!=` invokes the
same `Eq.eq` method and uses `BooleanNot`. User-defined ordering invokes
`Ord.cmp` and uses the exact `CompareOrdering` relation. Each transform runs on
the successful payload before the final success/result contract is published.
Primitive targets encode the complete operation and use `Identity`. Compound
assignment uses the selected operation, reads the place
once, applies the typed argument adjustment, invokes the dispatch target, and
applies the RFC 0005 writeback adjustment once.

Index access and assignment operation are distinct sites. An rvalue index child
has `IndexAccessMode::Read` and an `Index` target. An index child used as an
assignment place has `MutablePlace` and an `IndexMut` target whose result is the
checked mutable reference to the element. The enclosing plain assignment has no
second target. The enclosing compound assignment has one separate operation
target at its own node; it reads and writes through the already-acquired mutable
place and never emits another `Index` or `IndexMut` dispatch. Thus
`SortedMap<NodeId, DispatchFact>` still contains at most one target per node.

Dispatch-plan order is collection, index, one mutable-place acquisition, then
right-hand side. Non-short-circuit compound assignment reads the place before
the right-hand side, invokes the parent operation, and stores once. Logical-and,
logical-or, and null-coalescing assignment evaluate the right-hand side only on
their selected branch and store at most once. HIR and MIR consume these two
distinct node-keyed facts in this order; neither layer clones a target or
repeats place evaluation.

### Call Target Selection

Dispatch construction is a total mapping over RFC 0005 selections:

1. `Direct`, `ConcreteMethod`, `ImplMethod`, `WitnessMethod`, `DynMethod`, and
   `Primitive` map to the same-named target alternative.
2. Receiver role is derived from the checked syntax category. Receiver passing
   and its complete adjustment are copied from RFC 0005 and are never derived
   again; direct and static calls require no receiver plan.
3. Receiver and argument plans copy the exact RFC 0005 types and adjustments
   from the site's `CheckedCallEnvelope`. A compound assignment copies its
   receiver, sole right-hand argument, success, result, substitution, witness,
   and raises fields from the envelope inside `CompoundAssignmentFact`; it
   joins no second fact and performs no lookup.
   For indexed assignment, the index child independently copies its `IndexMut`
   envelope and the assignment parent copies only its operation envelope.
4. The fact copies the one substitution and witness handle from the checked
   call envelope and validates their issuers against the frozen stores.
5. The verifier proves the raw callable success matches its substituted
   signature, the declared result transform produces the copied success type,
   the raises value matches the signature, and the copied result type and
   error-union shape satisfy the RFC 0005 contract.

`DynMethod` carries logical interface and method `DefId` values. Vtable layout
and slot assignment occur in RFC 0010 LIR target lowering after
monomorphization and ABI selection. HIR and MIR contain no vtable slot.

### Determinism

RFC 0005 owns deterministic candidate enumeration. RFC 0009 sorts dispatch
facts by expanded module key and RFC 0005 `CheckedNodeKey`, then compares target,
receiver, argument, substitution, and witness canonical bytes. Hash-map order,
numeric slots, AST `NodeId` values, worker completion, and presentation names
never enter a revision or dump.

### Verified Dispatch Facts

```text
DispatchFactsCandidate {
  semanticContext: SemanticContextBrand,
  contextFingerprint: ContextFingerprint,
  module: ModuleId,
  checkedFactsRevision: CheckedFactsRevision,
  facts: SortedMap<NodeId, DispatchFact>,
}

VerifiedDispatchFacts {
  revision: DispatchFactsRevision,
  semanticContext: SemanticContextBrand,
  module: ModuleId,
  checkedFactsRevision: CheckedFactsRevision,
  facts: SortedMap<NodeId, DispatchFact>,
}

DispatchInvariantKind =
  InputMismatch | MissingFact | AdditionalFact | InvalidFact
  | CanonicalCodecMismatch

DispatchInvariantStage = Input | Construction | Verification | Encoding

DispatchInvariantFact {
  kind: DispatchInvariantKind,
  stage: DispatchInvariantStage,
  module: ModuleId,
  owner: Maybe<DefId>,
  node: Maybe<NodeId>,
  sourceSpan: Maybe<SourceSpan>,
  structuralFieldPath: Sequence<uint32>,
  expectedCheckedRevision: Maybe<CheckedFactsRevision>,
  actualCheckedRevision: Maybe<CheckedFactsRevision>,
  traversalOrdinal: uint32,
}

DispatchVerificationFailure =
  Identity { fact: IdentityInvariant }
  | Dispatch { fact: DispatchInvariantFact }

DispatchVerificationResult =
  Verified { facts: VerifiedDispatchFacts }
  | InvariantRejected {
      failures: SortedNonEmptySequence<DispatchVerificationFailure>,
    }
```

`DispatchInvariantKind` tags are `0x01` through `0x05`, stage tags are `0x01`
through `0x04`, and verification-failure tags are `Identity = 0x01` and
`Dispatch = 0x02`. Record fields encode in declaration order.

The generated RFC 0005 fact-requirement inventory determines the exact call,
operator, index, and compound-assignment sites. Verification rejects a missing,
additional, wrong-kind, wrong-tree, wrong-owner, stale-revision, foreign-store,
incomplete-target, duplicated-substitution, early-vtable-slot, or textual target
fact. It returns no source rejection because all user-correctable selection
errors were diagnosed before `VerifiedCheckedFacts` existed.

Classification is single-valued in this order: invalid context, registry, tag,
or slot is the exact RFC 0011 `IdentityInvariant`; wrong semantic context,
module, tree, checked revision, or required store is `InputMismatch`; malformed
canonical bytes are `CanonicalCodecMismatch`; an absent generated-site record
is `MissingFact`; an extra or duplicate record is `AdditionalFact`; and a
present wrong-kind, wrong-owner, invalid endpoint, early-slot, textual, or
duplicated-envelope field is `InvalidFact`. Classification stops at the first
row.

`DispatchVerificationFailure` sorts by its union tag. Identity facts then use
the exact RFC 0011 identity-invariant order. Dispatch facts compare kind, stage,
expanded module and optional owner keys, optional checked node key and validated
span with none first, structural field path, expected and actual revisions with
none first, then traversal ordinal. Invalid identities are never dereferenced
for sorting.

`DispatchFactsRevision` is SHA-256 over this exact stream:

```text
ASCII("zom.dispatch-facts-revision")
0x00
ContextFingerprint
uint64be(expandedModuleKeyByteLength)
expandedModuleKeyBytes
CheckedFactsRevision
uint64be(recordCount)
for each complete dispatch record in canonical order:
  uint64be(encodedRecordByteLength)
  encodedRecordBytes
```

Canonical record order is the expanded RFC 0005 `CheckedNodeKey`, followed by
the complete encoded `DispatchFact` bytes. Two records with the same expanded
checked-node key, two byte-identical complete records, or any record whose
embedded node key disagrees with its map entry are `AdditionalFact` and are
rejected before revision construction. Records are always individually
byte-framed; direct concatenation and RFC 0011 ordinary sequence framing are
invalid for this revision domain.

The independent oracle uses a zero fingerprint, module bytes `a1`, 32 checked-
revision bytes `22`, and one canonical record `b3`. Its complete 118-byte
preimage is:

```text
7a6f6d2e64697370617463682d66616374732d7265766973696f6e0000000000000000000000000000000000000000000000000000000000000000000000000000000001a1222222222222222222222222222222222222222222222222222222222222222200000000000000010000000000000001b3
```

Its SHA-256 is
`b50df7fb9f580a517707e385c6a90d91860031cd018bf9a2057a8a5be3f038bf`.

### Diagnostics

RFC 0005 owns all user-correctable call-selection diagnostics. RFC 0009 emits
only typed invariant facts. The driver maps them exhaustively to registered
fatal diagnostics in `diagnostics-lowering.def`:

| Invariant | Registered diagnostic |
|---|---|
| `InputMismatch` | `ZOM9937 DispatchInputMismatch`, fatal, `Internal dispatch input is inconsistent ({0} occurrence(s))`, arity 1 |
| `MissingFact` | `ZOM9938 DispatchMissingFact`, fatal, `Internal dispatch fact is missing ({0} occurrence(s))`, arity 1 |
| `InvalidFact` | `ZOM9939 DispatchInvalidFact`, fatal, `Internal dispatch fact is invalid ({0} occurrence(s))`, arity 1 |
| `CanonicalCodecMismatch` | `ZOM9940 DispatchCanonicalCodecMismatch`, fatal, `Internal dispatch canonical encoding is invalid ({0} occurrence(s))`, arity 1 |
| `AdditionalFact` | `ZOM9941 DispatchAdditionalFact`, fatal, `Internal dispatch fact is not authorized ({0} occurrence(s))`, arity 1 |

Failure facts carry module, checked revision, owner `DefId`, checked node key,
target tag, and structural field path. Display strings are not failure values.
Every code must exist in the `.def` registry before implementation and must
have an injected invariant conformance fixture. The location is the validated
source span or none; invalid or foreign ranges are retained only in the bug
bundle and are never passed to the diagnostic engine.

After sorting `DispatchVerificationFailure`, the adapter groups only adjacent
dispatch facts with the same mapped diagnostic and validated location, passes
their exact count as the sole argument, and retains every complete fact in the
compiler bug bundle. Identity facts retain RFC 0011's own mapping and grouping.
No worker-local count, hash iteration order, or first-arrival fact affects the
number or contents of emitted diagnostics.

```text
DispatchInvariantInjection {
  kind: DispatchInvariantKind,
  stage: DispatchInvariantStage,
  target: GeneratedDispatchFieldPath,
  occurrence: uint32,
}
```

The test-only `verifyDispatchWithInjection(CompleteDispatchCandidate,
DispatchInvariantInjection)` API uses a generated candidate field path, one
closed invariant kind, and an occurrence index. It is absent from production
libraries. Each fixture mutates one valid candidate field and asserts the exact
failure variant, code, location, sort key, and absence of verified facts.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| RFC governance | `docs/rfc/**` | `rfc` |
| Semantic selections and verified dispatch construction | `products/zomlang/compiler/checker/**`, `products/zomlang/compiler/type/**` | `binder-checker` |
| Module-qualified lookup | `products/zomlang/compiler/symbol/**`, `products/zomlang/compiler/driver/**` | `module-system` |
| Dispatch invariant registry | `products/zomlang/compiler/diagnostics/**` | `error-system` |
| Checked-module and HIR dispatch consumer | `products/zomlang/compiler/hir/**` | `ir-backend` |
| Type, expression, and interface specs | `docs/spec/chapters/03-types.md`, `docs/spec/chapters/04-expressions.md`, `docs/spec/chapters/09-interfaces.md` | `spec-audit` |
| Conformance and unit tests | `products/zomlang/tests/**` | `verification` |

## Security And Safety Impact

Incorrect dispatch can call the wrong implementation for a receiver, which is a
semantic safety violation and can become a memory-safety issue once unsafe
interfaces, raw pointers, or FFI wrappers are involved. Dispatch facts are
derived once from verified checked types and then remain immutable. Dynamic
dispatch through `dyn` carries only object-safe logical method identity; LIR
assigns a slot from the verified target layout. RFC 0005 object-safety
diagnostics prevent an invalid dyn target from reaching this RFC.

## Drawbacks And Risks

- Complete verified dispatch facts increase frontend metadata and revision
  cost. The benefit is that HIR lowering cannot repeat semantic lookup.
- Qualified interface calls can be confused with module-qualified functions.
  Binder metadata and type-directed lookup must distinguish the two before
  lowering.
- Operator interfaces are easy to overgeneralize. This RFC keeps the mapping
  fixed to the standard prelude traits and rejects implicit overload ranking.
- Dispatch facts depend on RFC 0005 store lifetime. The checked-module assembly
  must own both verified values and reject mismatched revisions before HIR.

## Alternatives Considered

### Lower Calls Directly In The Checker

Rejected. The checker publishes semantic selections, not HIR. Verified
dispatch facts preserve one resolved logical target while separating semantic
selection from receiver lowering.

### Re-run Lookup In IR Lowering

Rejected. Re-running method lookup duplicates semantic logic and can diverge
from checker diagnostics, especially for generic instantiation and `dyn`
vtable calls.

### C++-Style Overload Sets

Rejected. ZOM has no implicit overload ranking in canonical. Overload-like behavior is
expressed through named interfaces and explicit generic bounds.

### Treat Operators As Syntax-Only Builtins

Rejected. RFC 0005 already supports user-defined operator interfaces. The
remaining task is to make their lowering explicit.

## Compatibility And Rollout

This is a direct replacement. Implement the verified RFC 0005 input, dispatch
builder, verifier, revision, HIR consumer, dumps, and tests on one branch. Cut
every producer and consumer to `VerifiedDispatchFacts`, then delete the current
mutable `TypeEnv` dispatch records and all local-ID/name/impl-node/slot fields in
the same change. No adapter, flag, or dual dump grammar remains. Rollback before
landing is a source-control revert of the complete cutover.

## Documentation And Teaching Plan

- Update `docs/spec/chapters/03-types.md` with the exact operator-interface
  inventory, primitive-only forms, indexed mutable-place contract, and raising
  call success/result distinction.
- Update `docs/spec/chapters/04-expressions.md` with the operator-to-interface
  lowering table and dispatch categories.
- Update `docs/spec/chapters/09-interfaces.md` with qualified interface method
  call semantics and `dyn` vtable call semantics.
- Document the RFC 0005 semantic-selection versus RFC 0009 dispatch-plan
  boundary and the RFC 0010 LIR slot-assignment boundary.
- Add injected invariant snapshots for `ZOM9937-ZOM9941`; user-correctable call
  selection remains in the RFC 0005 diagnostic matrix.

## Operational Readiness

- CI must run checker unit tests, dispatch unit tests, diagnostics conformance,
  and relevant IR lowering tests before this RFC can move to `LANDED`.
- Debug builds should be able to dump dispatch records for a source file so
  dispatch mismatches can be diagnosed without stepping through the checker.
- Performance budget: dispatch construction is linear in the number of
  call-like facts because candidate enumeration has already completed.

## Acceptance Criteria

1. RFC 0005 publishes one complete `SelectedCallable` and typed envelope for
   every call, every live primitive/operator variant, index, contains,
   null-coalescing, and compound-assignment site.
2. RFC 0009 is the only owner of `DispatchTarget`; RFC 0005 and RFC 0010 do
   not declare another target algebra.
3. Every `SelectedCallable` alternative maps totally to the matching direct,
   concrete, impl, witness, dyn, or primitive target without semantic lookup.
4. Every dispatch fact records exact receiver role and checked passing mode,
   receiver adjustment, receiver and argument
   adjustments, result transform, success type, canonical result type, raises
   type, substitution, witness, and source span.
5. Substitution and witness handles occur once in the dispatch envelope and
   validate through the exact RFC 0008 checked-evidence lease, immutable RFC
   0005 stores, and checked revision through backend completion.
6. No successful fact contains a name-based target, `SymbolId`, AST impl node,
   error target, intrinsic placeholder, vtable slot, target layout, or ABI fact.
7. Dyn dispatch carries logical interface and method `DefId` values; RFC 0010
   LIR lowering alone assigns concrete vtable slots.
8. Compound assignment evaluates its place once and records one operation,
   typed RHS adjustment, selected callable, result, and writeback adjustment.
   Indexed plain assignment records one child `IndexMut` access and no parent
   target; indexed compound assignment records that child access plus one
   parent operation target, evaluates collection/index/RHS once each, and never
   repeats `Index` or `IndexMut` dispatch.
9. `DispatchFactsVerifier` enforces exact generated site coverage and returns
   only `Verified` or `InvariantRejected`.
10. `DispatchFactsRevision` uses the exact codec, group ordering, non-empty
    framing oracle, and canonical identity expansion in this RFC.
11. `ZOM9937-ZOM9941` are registered before use and injected conformance tests
    cover every invariant kind without raw display-string failures.
12. RFC 0010 checked-module construction consumes matching
    `VerifiedCheckedFacts` and `VerifiedDispatchFacts` revisions and never
    repeats member, trait, projection, or operator resolution.
13. Deterministic dumps and revisions are unchanged by numeric handle slots,
    hash insertion order, or worker count.
14. Architecture search finds no duplicate call-target algebra, string target,
    `implNode`, early vtable slot, or mutable dispatch overwrite API.
15. `python3 scripts/check-rfc.py`, format, sanitizer, focused dispatch,
    invariant conformance, and default CTest gates pass before `LANDED`.

### Current Implementation Reality

The current `TypeEnv` dispatch table is not implementation evidence for this
contract. It uses local `TypeId` and `SymbolId`, presentation names, AST impl
nodes, early dyn-vtable slots, and a mutable side-table shape. The direct
cutover deletes that surface and migrates every producer, dump, test, and
consumer to verified dispatch facts; no adapter or compatibility target remains.
## Implementation Plan

1. Land RFC 0005 `SelectedCallable`, canonical substitution and witness stores,
   typed call, index, and compound-assignment facts.
2. Implement `DispatchTarget`, `DispatchFact`, the private candidate builder,
   canonical codec, and non-empty revision oracle.
3. Generate the exact dispatch-site inventory from the RFC 0005 checked-fact
   requirements table.
4. Implement total selection-to-target mapping and receiver-plan copying with
   no checker, binder, coherence, or normalization dependency.
5. Implement `DispatchFactsVerifier`, immutable
   `VerifiedDispatchFacts`, and `ZOM9937-ZOM9941` adapter mapping.
6. Update RFC 0010 checked-module assembly and HIR construction to consume the
   matching checked and dispatch revisions.
7. Replace the debug dump with canonical identities and logical dyn targets.
8. Delete `TypeEnv` dispatch mutation, local-ID/name/impl-node targets, early
   vtable slots, and all callers in one cutover.
9. Run codec, verifier, invariant, dispatch, HIR, sanitizer, default, RFC, and
   format gates.

## Test Plan

- Build: `cmake --preset sanitizer` and
  `cmake --build --preset sanitizer`.
- Unit tests: every `SelectedCallable -> DispatchTarget` mapping, all receiver
  modes, direct/concrete/impl/witness/dyn/primitive targets, compound
  assignment, rvalue index, indexed plain assignment, indexed compound
  assignment, single-evaluation order, substitutions, witnesses, non-raising
  and raising success/result/error-shape records, result transforms that leave
  residuals unchanged, and store-lifetime validation.
- Verifier tests: exact missing/additional/wrong-kind/stale/foreign/duplicate
  mutations from a complete candidate and exact `ZOM9937-ZOM9941` results.
- Codec tests: every tag and field order, numeric-slot independence, the
  118-byte non-empty oracle, zero/one/two-record framing, record-order reversal,
  duplicate expanded keys, duplicate complete records, direct concatenation,
  ordinary sequence framing, real composite records, and worker permutations.
- Lit and conformance: successful ordinary, member, qualified-interface,
  operator, index, compound-assignment, generic witness, and dyn calls; source
  failures remain RFC 0005 diagnostics and publish no dispatch facts.
- HIR integration: verified checked and dispatch revisions must match; no
  semantic lookup or vtable slot appears before LIR.
- Architecture: reject `SymbolId`, local `TypeId`, target names, `implNode`,
  `ErrorTarget`, intrinsic placeholders, early vtable slots, mutable
  overwrites, and duplicate target algebras.
- Format: `python3 scripts/check-format.py` and `git diff --check`.
- RFC: `python3 scripts/check-rfc.py`.
- Full suite: `ctest --preset default --output-on-failure`.

## Open Questions

None
## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-07-08 | DRAFT | Initial draft defining call dispatch, operator lowering, dispatch side-table records, and IR-lowering ownership. |
| 2026-07-08 | REVIEW | The proposal has complete motivation, reference-level dispatch records, follow-up implementation plan, and local tracking anchors. Approval remains blocked on owner review, non-empty approvers, a recorded decision, and implementation evidence. |
| 2026-07-08 | REVIEW | Started implementation by adding `TypeEnv` dispatch records and recording user-defined binary operator and index method dispatch targets. RFC remains blocked on the remaining dispatch target variants, debug dumping, IR-lowering consumption, owner approval, and decision metadata. |
| 2026-07-08 | REVIEW | Added user-defined unary `Neg` and `Not` operator dispatch records with `OperatorOperand` receiver mode. |
| 2026-07-08 | REVIEW | Added primitive operator dispatch records for built-in arithmetic, comparison, unary, array-index, and tuple-index expressions. |
| 2026-07-08 | REVIEW | Added `FreeFunction` dispatch records for identifier-based function calls. |
| 2026-07-08 | REVIEW | Audited concrete member-call dispatch and recorded the current binder/member lookup blocker before `InstanceMethod` and `StaticMethod` records can be implemented. |
| 2026-07-08 | REVIEW | Added concrete instance method dispatch records after binder defers unresolved member expressions and checker resolves class-scope method symbols. `StaticMethod`, qualified interface, and dyn-vtable call targets remain open. |
| 2026-07-09 | REVIEW | Added static class method dispatch records for `Counter.make()`-style calls, preserving `ReceiverMode::None` and omitting implicit receiver arguments. |
| 2026-07-09 | REVIEW | Added struct instance method dispatch coverage for `Point.norm()`-style calls, including ztest dispatch assertions and CLI conformance coverage. |
| 2026-07-09 | REVIEW | Added missing concrete member-call diagnostics conformance coverage and suppressed the cascading non-function-call diagnostic after a member lookup failure. |
| 2026-07-09 | REVIEW | Clarified that ambiguous concrete method dispatch is not a canonical checker target because duplicate same-scope methods are binder errors; imported method dispatch remains blocked on RFC 0008 module signature publication. |
| 2026-07-09 | REVIEW | Added direct-interface `dyn` receiver method lookup and `DynVTable` dispatch records for calls such as `drawable.draw()`, with unit and CLI conformance coverage. |
| 2026-07-09 | REVIEW | Added parser/spec support and checker dispatch records for qualified interface calls such as `Drawable::draw(sprite)`, with unit and CLI conformance coverage. |
| 2026-07-09 | REVIEW | Added an explicit checker-completion freeze boundary for dispatch records so later phases cannot mutate checked dispatch metadata. |
| 2026-07-09 | REVIEW | Added inherited dyn receiver method lookup with parent-interface slots flattened before direct interface slots, with unit and CLI conformance coverage. |
| 2026-07-09 | REVIEW | Added a deterministic `TypeEnv::dumpDispatch()` debug stream for dispatch records, with unit coverage for ordering and field rendering. |
| 2026-07-09 | REVIEW | Added `zomc compile --dump-dispatch` and `--emit=dispatch` CLI support with diagnostics conformance coverage for a static method dispatch record. |
| 2026-07-09 | REVIEW | Added dispatch dump diagnostics conformance coverage for dyn-vtable and qualified-interface call records. |
| 2026-07-09 | REVIEW | Added dispatch dump diagnostics conformance coverage for operator-method and index-method records. |
| 2026-07-09 | REVIEW | Added dispatch dump diagnostics conformance coverage for free-function call records. |
| 2026-07-09 | REVIEW | Added unit and CLI conformance evidence that `!=` selects the same `Eq.eq(rhs) -> bool` operator method contract as `==`, with lowering responsible for negating the result. |
| 2026-07-09 | REVIEW | Added dispatch dump diagnostics conformance coverage proving `!=` records an `OperatorMethod` target for `Eq.eq`. |
| 2026-07-09 | REVIEW | Added dispatch dump diagnostics conformance coverage for user-defined unary `Neg` and `Not` operator-method records. |
| 2026-07-09 | REVIEW | Added dispatch dump diagnostics conformance coverage for primitive-operator records. |
| 2026-07-09 | REVIEW | Added dispatch dump diagnostics conformance coverage for instance-method records. |
| 2026-07-09 | REVIEW | Verified the core dispatch table and dump snapshots under the sanitizer build for body-checker, type-env, dyn-vtable, qualified-interface, index-method, instance-method, operator-method, static-method, primitive-operator, and free-function records. |
| 2026-07-09 | REVIEW | Added dispatch dump diagnostics conformance coverage for primitive array index, tuple index, unary minus, and logical-not records. |
| 2026-07-09 | REVIEW | Verified the current repository state with `ctest --preset default --output-on-failure`; RFC remains blocked on an IR-lowering consumer and governance metadata. |
| 2026-07-11 | RETURNED | Acceptance review found that the proposed `TypeId`, `SymbolId`, interface/method name, AST `implNode`, early vtable-slot, and `ErrorTarget` contract conflicts with RFC 0011 context identities and the verified handoff in RFCs 0005 and 0010. Revision must use `DefId`, `ImplId`, `SemanticTypeId`, canonical substitutions and witnesses, remove incomplete targets, assign dyn slots only during target lowering, and remove the RFC 0009/RFC 0010 dependency cycle before re-entering review. |
| 2026-07-11 | DRAFT | Replaced the returned side-table model with one verified dispatch contract over RFC 0005 semantic selections, removed the RFC 0010 dependency cycle, local IDs, names, AST impl nodes, error and intrinsic placeholders, and early vtable slots, and added exact target, receiver, store-lifetime, revision, verifier, and registered invariant contracts. |
| 2026-07-11 | DRAFT | Responded to focused re-review with the complete dispatch invariant algebra and stable sort contract, exhaustive `ZOM9937-ZOM9941` mapping, generated test-only injection, copied RFC 0005 receiver normalization, and RFC 0008 checked-evidence lease lifetime requirements. |
| 2026-07-11 | DRAFT | Closed compound-assignment dispatch by consuming one complete RFC 0005 checked call envelope and defined deterministic adjacent invariant aggregation that retains every full failure fact. |
| 2026-07-11 | DRAFT | Separated index-place access from assignment operations: `IndexMut` returns one checked mutable place at the index child, plain assignment has no parent target, and indexed compound assignment adds only its parent operation target with single evaluation. |
| 2026-07-11 | DRAFT | Responded to spec-audit re-review by copying distinct success, canonical result, raises, and error-union-shape facts from RFC 0005, limiting result transforms to successful payloads, and adding Chapter 3 plus the exact primitive-only operator inventory to the documentation contract. |
| 2026-07-11 | REVIEW | Entered formal review after exact-hash governance, semantic, and invariant reviewers approved the coordinated type, module, dispatch, error-lowering, and IR contracts. Approvers and decision remain open. |
| 2026-07-11 | ACCEPTED | All seven required owners approved proposal hash `c4b9206b117fe4ecd40f1b58a7f79126c4a5bf416051807a99e9ff31db814c10` after semantic selection, logical dispatch, revision framing, evidence lifetime, diagnostic, codec, and verifier review. Implementation has not started. |
| 2026-07-17 | IMPLEMENTING | Started the direct immutable dispatch-facts series over canonical checked facts, independent inventory and candidate verification, exact checked-evidence lineage, Semantic HIR consumption, and RFC 0010 target lowering. No TypeEnv dispatch table or repeated lookup path is retained. |
