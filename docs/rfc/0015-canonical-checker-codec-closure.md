---
rfc: 15
title: Canonical Checker Codec Closure
type: language
status: LANDED
author: ZOM Compiler Team
review-manager: rfc
required-owners: [rfc, lexer-parser, binder-checker, module-system, error-system, spec-audit, verification]
approvers: [rfc, lexer-parser, binder-checker, module-system, error-system, spec-audit, verification]
created: 2026-07-16
updated: 2026-07-20
area: language
requires: [4, 5, 8, 9, 11, 13, 14]
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0015-review-and-implementation.md#discussion-record
decision: docs/rfc/tracking/0015-review-and-implementation.md#decision-record
implementation: docs/rfc/tracking/0015-review-and-implementation.md#implementation-tracker
tracking-issue: docs/rfc/tracking/0015-review-and-implementation.md#implementation-tracker
---

# RFC 0015: Canonical Checker Codec Closure

## Summary

This RFC closes two canonical checker algebras left open by RFC 0005. It
defines the exact `OperatorKind` tags, encodings, source spellings, and
diagnostic validation rules. It also replaces the prose-only
`ImplTypePattern` contract with a complete recursive `TypeKeyPattern` algebra
that includes RFC 0014 `InterfaceSelf` and a distinct impl-parameter tag. The
same parameter space now covers both the implemented interface arguments and
the self type through one canonical `ImplPattern`.

It also closes the source-to-fact identity boundary for impl declarations.
Every source occurrence is reconstructed and classified independently under
one shared stable authority per equal identity group. A unique ordinary
survivor publishes one `ImplHead`; a unique bodyless marker survivor publishes
one `MarkerFact` and never an `ImplHead`. Conflicting survivor groups publish
neither semantic fact. Generic or conditional marker declarations are not
admitted by this revision because RFC 0005 defines only concrete
`(marker, subject)` marker fact identity and no generic marker-pattern selection
algebra.

This RFC is a hash-bound additive normative overlay. It does not edit an
accepted or implementing proposal in place. Implementation remains blocked
until every required owner approves the exact proposal snapshot and the RFC
moves to `ACCEPTED`.

## Motivation

RFC 0005 declares `CheckerDisplayArgument::Operator(OperatorKind)` and assigns
the outer display-argument tag, but never defines `OperatorKind`. Four
diagnostics therefore have no canonical payload tag, no total renderer, and no
independent verifier contract. An implementation could cast an AST enum,
encode a source string, or select a different spelling without violating the
written design.

RFC 0005 also says `TypeKeyPattern` mirrors `TypeKeyNode`, but it does not
enumerate variants, child records, tags, framing, or the exact parameter
replacement algorithm. RFC 0014 later added `InterfaceSelf = 0x10` to the
semantic type algebra. The prose mirror is now observably incomplete, and the
impl overlap and orphan algorithms have no versioned canonical input.

RFC 0005 stores the implemented interface as a semantic instantiation but
describes overlap only over the self pattern. Generic parameters can occur in
both positions. Coherence must unify both positions in one variable space or
it either misses `impl<T> I<T> for S` against `impl I<i32> for S`, or rejects
the disjoint pair `impl I<i32> for S` and `impl I<bool> for S`.

These omissions block production implementation of RFC 0005 signature facts,
coherence, canonical checker diagnostics, and the combined module interface.
They must be closed before those facts can be published or compared across
modules.

## Goals

- Define one closed diagnostic operator algebra using explicit semantic tags.
- Define one closed recursive impl pattern algebra over every semantic type
  variant in RFC 0005 plus RFC 0014 `InterfaceSelf`.
- Define one complete impl pattern over interface arguments and self type with
  a shared, declaration-ordered parameter space.
- Specify construction, validation, ordering, unification, rendering, and
  invariant failure classification without AST numeric casts or source text in
  canonical identity.
- Define a publication well-formedness boundary that keeps every admitted impl
  pattern stable under substitution and semantic-type normalization.
- Define one source impl identity per behavior conformance, with independent
  bodyless marker evidence and an exact source-to-fact provenance proof.
- Version every revision that directly embeds the changed impl-head codec.
- Provide independently reproducible byte and SHA-256 oracles.
- Preserve accepted-proposal bytes through an additive overlay and a local
  review tracker.

## Non-Goals

- Changing expression precedence, operator dispatch, or overload semantics.
- Adding specialization, negative reasoning from where-constraints, or a new
  coherence algorithm.
- Adding a semantic type variant or changing `SemanticTypeKey` v1.
- Defining an AST-to-semantic compatibility layer.
- Implementing compiler code before this RFC is accepted.
- Preserving decoders or constructors for replaced revision domains.
- Defining generic or conditional marker-impl selection before a canonical
  marker-pattern and coherence algebra exists.

## Prior Art

### Rust interned types and parameter nodes

Rust models canonical compiler types through a closed `TyKind` algebra with a
dedicated `Param` node, and interns types through the compiler type context.
ZOM adopts the closed tagged algebra, explicit parameter node, and centralized
canonicalization boundary. ZOM keeps RFC 0011 identities and its own byte
codec rather than depending on compiler-local enum discriminants.

The Rust Reference also gives a trait implementation exactly one trait path
and one target type. This preserves a one-to-one identity for coherence,
associated items, and witness selection. ZOM adopts that source-to-conformance
cardinality for ordinary impls.

Rust requires an `unsafe` implementation for an unsafe trait, while negative
impl syntax cannot grant a capability. RFC 0005 currently places safety-
critical markers such as `Sendable` and ordinary markers in one fact algebra
without a per-marker safety classifier. ZOM therefore uses the conservative
closed rule in this RFC: every source-authored positive marker fact is an
`unsafe` assertion, while a negative marker fact is safe and cannot use
`unsafe`. Structural and builtin positive evidence remain compiler-proven.

Rust also makes reference evidence conditional across marker identities:
`&T: Send` requires `T: Sync`, while raw pointers have explicit negative
`Send` impls. This is the baseline for ZOM's per-mutability map from a result
marker to the required referent marker and for failing raw pointers closed.
Swift models callable sendability as the distinct `@Sendable` function-type
capability and validates captures. ZOM therefore does not infer any marker from
bare `FunctionTypeData`, which has no capture or callable-capability field.

References:

- <https://doc.rust-lang.org/nightly/nightly-rustc/rustc_middle/ty/type.TyKind.html>
- <https://doc.rust-lang.org/nightly/nightly-rustc/rustc_middle/ty/struct.Ty.html>
- <https://doc.rust-lang.org/reference/items/implementations.html>
- <https://doc.rust-lang.org/reference/special-types-and-traits.html#auto-traits>
- <https://doc.rust-lang.org/std/marker/trait.Send.html>
- <https://doc.rust-lang.org/std/marker/trait.Sync.html>
- <https://docs.swift.org/swift-book/ReferenceManual/Attributes.html#ID616>
- <https://docs.swift.org/compiler/documentation/diagnostics/sendable-closure-captures/>

The Rust Reference publishes a closed operator inventory and separates source
operator syntax from trait selection. ZOM similarly gives diagnostic spelling
an explicit total table over the semantic operation enums.

Reference:

- <https://doc.rust-lang.org/reference/expressions/operator-expr.html>

### Swift ABI mangling

Swift's official ABI mangling grammar assigns explicit productions to generic
parameters, associated types, and constrained existential forms. The grammar
is versioned and structural rather than derived from presentation text. ZOM
adopts explicit generic-parameter positions and a structural recursive codec;
it does not reuse Swift's textual mangling.

Reference:

- <https://github.com/swiftlang/swift/blob/main/docs/ABI/Mangling.rst>

### Zig operator and intern-pool closure

Zig's language reference defines operators as a closed language table, while
the official compiler `InternPool` uses explicit tagged interned values and
resolved indices. ZOM adopts the separation between a closed operator spelling
table and interned semantic identity. ZOM retains overload semantics from RFC
0009 rather than Zig's non-overloadable operator policy.

References:

- <https://ziglang.org/documentation/master/#Operators>
- <https://github.com/ziglang/zig/blob/master/src/InternPool.zig>

### LLVM bitcode evolution

LLVM bitcode uses explicit block and record codes, version records, and stable
numeric assignments. LLVM's bitcode declarations treat assigned numeric values
as durable and append new values rather than renumbering existing ones. ZOM
adopts explicit permanent tags, domain-version changes for codec changes, and
golden byte vectors.

References:

- <https://llvm.org/docs/BitCodeFormat.html>
- <https://llvm.org/doxygen/LLVMBitCodes_8h_source.html>

## Guide-Level Explanation

Contributors no longer infer an operator diagnostic from an AST enum or a
string. A failed strict-inequality comparison carries the semantic payload
`Primitive(StrictNe)`. The renderer obtains `!==` from the closed table in this
RFC. The verifier reconstructs the semantic operation from the checked syntax
site and requires exact equality.

Impl patterns are equally explicit. Given an impl with one declared type
parameter:

```zom
impl<T> Interface for Box<T> { ... }
```

the canonical self pattern is structurally equivalent to:

```text
Nominal(BoxDefinitionKey, [Parameter(0)])
```

The source name `T`, an AST parameter number, and a semantic-store slot do not
enter the pattern. A repeated occurrence of `T` reuses `Parameter(0)`, so
overlap unification must bind every occurrence to the same complete pattern.

`InterfaceSelf` has a real codec branch because it is part of the combined
semantic type algebra. It is nevertheless invalid in a published impl head:
RFC 0014 requires contextual interface `Self` to be substituted with the exact
impl self type before publication. This distinction closes decoding while
preserving the semantic phase boundary.

Each behavior conformance has its own ordinary declaration:

```zom
impl Reader for File {}
impl Writer for File {}
```

A concrete marker assertion has no body:

```zom
unsafe impl core::marker::Sendable for File;
impl !core::marker::Linear for SharedFile;
```

This cardinality lets one shared `ImplId` remain the semantic authority for a
successfully published conformance or witness while `ImplOccurrenceId` retains
each source declaration. A marker assertion remains a concrete `MarkerFactKey`
rather than an ordinary dispatchable impl head.

Only source-authored marker assertions are frozen into coherence maps. When a
checker asks whether an arbitrary semantic type satisfies a marker, one
context-bound proof engine first consults that explicit map, then the builtin
policy, then the type's structural components. This works for types created
after signature publication and avoids pretending that the compiler can
pre-enumerate every tuple, array length, or inferred aggregate type. Proofs are
recomputed from immutable semantic types and signatures and are not added to a
second global fact map.

## Reference-Level Design

### Normative Overlay And Base Hashes

RFC 0015 is an additive normative overlay. It binds these exact repository
proposal snapshots:

| Base | Bound proposal SHA-256 |
|---|---|
| RFC 0004 | `38d3f37b33df75ebc9cf67d1f1cae850cfc770f836cbaa4d7ce4be820cad2dfa` |
| RFC 0005 | `678444e94acf2cec4aa28371f8d7ee98dcf54f37e6a05770f54cd5a3d65dd805` |
| RFC 0008 | `f1169871ea0e983bcf69b13ead94093522fff7c25c37187787d9a2fb6b003854` |
| RFC 0009 | `d29bac1e9cad25cee673e17c6b922ba935b669549dc8c44f05eba5900e75f362` |
| RFC 0011 | `383dc8905ae389949008f47f3b501d812a26d91769460d7e41731283b2f8cc03` |
| RFC 0013 | `e3909d5caad48a1c0255ee57d2a8fcc327e046945f20a586e0c6bf0115a237c3` |
| RFC 0014 | `cd9b1d39b2d243040a3ee2b56f7c6a0bcb94cfcfbc1f2f7bace23165262ea3ed` |

The combined contract is each base plus this RFC. A base hash mismatch
invalidates this overlay and requires a revised proposal snapshot or a later
RFC. This RFC has precedence only for these clauses:

RFC 0018 is the accepted follow-up for implementation occurrence binding. Its
`ImplOccurrenceId`, `ImplSourceOccurrenceKey`, occurrence-local Binder facts
and scopes, per-occurrence source reconstruction, mixed-form classification,
and post-classification survivor streams are authoritative for the clauses
below. The bound proposal hashes continue to identify the accepted authority
for every unaffected clause.

- RFC 0004 impl source-shape and binding clauses are replaced by the singular
  ordinary-impl and bodyless marker-impl contract below. `StandaloneImplDecl`
  stores one interface node directly. `MarkerImpl` stores one marker path,
  `is_unsafe` and `is_negated` syntax flags, and closed target type and has no
  generic parameters, where clause, or body. `ImplIfaceList.n_ifaces` is
  removed; every remaining interface-bound list uses only its canonical
  `NodeList.size`. Every ordinary or marker source node has one RFC 0004
  `ImplOccurrenceBindingFact` and one independently owned `ImplBody` scope.
  A `MarkerImpl` scope is empty, is owned by its `ImplOccurrenceId`, and has an
  empty `members` sequence. Its marker path and target type are bound in that
  scope, but there is no body traversal, member definition, receiver, or
  contextual `Self`.
- RFC 0005 `ImplTypePattern`, `TypeKeyPattern`, `ImplHead.interface`,
  `ImplHead.selfPattern`, outer head derivation, and first-order
  pattern-unification clauses are replaced by the complete pattern contract
  below. Its orphan rule consumes the independently reconstructed interface
  and self pattern defined here. Its `implHeads` fields remain
  `SortedMap<ImplId, ImplHead>` because one ordinary singleton survivor group
  publishes at most one behavior conformance under its shared authority.
- RFC 0005 `OperatorKind` and operator display-rendering clauses are replaced
  by the complete diagnostic operator contract below.
- RFC 0005 `SignatureFactsRevision` v0 and `CoherenceViewRevision` v0 are
  replaced by the v1 domains and oracles below.
- RFC 0005's undefined derivable-marker classification and primitive-marker
  table are replaced by the context-bound `VerifiedMarkerPolicyRegistry`
  below. The registry revision is a direct parent field of signature facts,
  module-interface v3, and coherence-view v1. `MarkerComponentStep` appends
  exact reference-referent and enum-variant-payload forms; every other marker
  evidence tag remains as stated below. RFC 0005 eager structural and builtin
  publication, finite marker-fact coverage, key-precedence support validation,
  dangling-support rejection, and support-cycle rejection are replaced in full
  by the demand-driven `MarkerProofEngine` below. Persisted signature,
  module-interface, coherence-input, and frozen-coherence marker maps contain
  explicit evidence only. Builtin and structural `MarkerFact` values are
  ephemeral verified query proofs and never enter a revision preimage, cache
  artifact, or cross-module projection.
- RFC 0005 cross-module coherence-only `CheckerFailureRef` collections and the
  `CheckerEmitterOrdinal` requirement for cross-module `ZOM4017` and ordinary
  impl `ZOM4054` are replaced by the span-addressed `CoherenceFailureRef`
  contract below. The per-module explicit-marker orphan phase, signature, and
  body failures retain tree-local nodes unchanged.
- RFC 0008 module-interface projection and verification consume only the
  combined module-interface v3 contract below. Its coherence input, frozen
  view, interface projection, and derived index consume the singular
  source-matched `ImplHead` and complete reconstructed `ImplPattern`; bucket
  entries remain verified `ImplId` map keys.
- RFC 0014 module-interface v2 is replaced by module-interface v3. Its impl
  owner `Self::Item` rule consumes the exact independently reconstructed
  implemented-interface instantiation for the singular ordinary impl, not the
  removed `ImplHead.interface` field. Marker impls have no body and never
  create contextual `Self`. All other RFC 0013 v1 and RFC 0014 semantic-type
  v1 fields and ordering remain exact.

RFC 0009 remains authoritative for operator semantics and dispatch. RFC 0011
remains authoritative for strong scalars, expanded identities, byte strings,
sequences, big-endian integers, and SHA-256. Every other base clause remains
unchanged. Acceptance must record the accepted RFC 0015 proposal hash in the
affected base trackers; no accepted proposal file is rewritten.

### Closed Diagnostic Operator Algebra

`OperatorKind` is:

```text
OperatorKind =
  Primitive(operation: PrimitiveOperation)
  | CompoundAssignment(operation: CompoundAssignmentOperation)
  | Assignment
  | Error(kind: ErrorOperatorKind)
```

Outer tags are `Primitive = 0x01`, `CompoundAssignment = 0x02`,
`Assignment = 0x03`, and `Error = 0x04`. `Primitive` encodes its RFC 0005
`PrimitiveOperation` tag immediately after the outer tag.
`CompoundAssignment` and `Error` do the same with their RFC 0005 tags.
`Assignment` has no payload. Unknown, missing, or trailing tags are invalid.

`OperatorKind` is a diagnostic identity, not a dispatch target. It cannot
contain an AST enum value, token ID, source range, source text, interface name,
method name, or target-layout value. Conversion from syntax and semantic facts
uses an explicit exhaustive symbolic mapping. Numeric casts between AST and
semantic enums are forbidden.

### Primitive Operator Tags And Spellings

The primitive tags are the already accepted RFC 0005 declaration-order tags.
The final column is the exact deterministic diagnostic rendering.

| Tag | `PrimitiveOperation` | Rendering |
|---|---|---|
| `0x01` | `UnaryPlus` | `+` |
| `0x02` | `Neg` | `-` |
| `0x03` | `LogicalNot` | `!` |
| `0x04` | `BitNot` | `~` |
| `0x05` | `Dereference` | `*` |
| `0x06` | `BorrowShared` | `&` |
| `0x07` | `BorrowMutable` | `&mut` |
| `0x08` | `PreIncrement` | `++` |
| `0x09` | `PreDecrement` | `--` |
| `0x0a` | `PostIncrement` | `++` |
| `0x0b` | `PostDecrement` | `--` |
| `0x0c` | `Add` | `+` |
| `0x0d` | `Sub` | `-` |
| `0x0e` | `Mul` | `*` |
| `0x0f` | `Div` | `/` |
| `0x10` | `Rem` | `%` |
| `0x11` | `Pow` | `**` |
| `0x12` | `Shl` | `<<` |
| `0x13` | `Shr` | `>>` |
| `0x14` | `UShr` | `>>>` |
| `0x15` | `BitAnd` | `&` |
| `0x16` | `BitOr` | `|` |
| `0x17` | `BitXor` | `^` |
| `0x18` | `LogicalAnd` | `&&` |
| `0x19` | `LogicalOr` | `||` |
| `0x1a` | `Eq` | `==` |
| `0x1b` | `Ne` | `!=` |
| `0x1c` | `StrictEq` | `===` |
| `0x1d` | `StrictNe` | `!==` |
| `0x1e` | `Lt` | `<` |
| `0x1f` | `Le` | `<=` |
| `0x20` | `Gt` | `>` |
| `0x21` | `Ge` | `>=` |
| `0x22` | `Index` | `[]` |
| `0x23` | `IndexMut` | `[]` |
| `0x24` | `Contains` | `in` |
| `0x25` | `NullCoalesce` | `??` |

Prefix and postfix update share a source spelling but remain distinct semantic
operations. Read and mutable index access also share a diagnostic spelling but
remain distinct semantic operations. Rendering therefore is not an inverse
codec and never participates in identity.

### Assignment And Error Operator Tags And Spellings

| Tag | `CompoundAssignmentOperation` | Rendering |
|---|---|---|
| `0x01` | `AddAssign` | `+=` |
| `0x02` | `SubAssign` | `-=` |
| `0x03` | `MulAssign` | `*=` |
| `0x04` | `DivAssign` | `/=` |
| `0x05` | `RemAssign` | `%=` |
| `0x06` | `PowAssign` | `**=` |
| `0x07` | `ShlAssign` | `<<=` |
| `0x08` | `ShrAssign` | `>>=` |
| `0x09` | `UShrAssign` | `>>>=` |
| `0x0a` | `BitAndAssign` | `&=` |
| `0x0b` | `BitOrAssign` | `|=` |
| `0x0c` | `BitXorAssign` | `^=` |
| `0x0d` | `LogicalAndAssign` | `&&=` |
| `0x0e` | `LogicalOrAssign` | `||=` |
| `0x0f` | `NullCoalesceAssign` | `??=` |

`Assignment` renders `=`. `Error(Propagate)` uses inner tag `0x01` and renders
`?!`; `Error(ForcedUnwrap)` uses inner tag `0x02` and renders `!!`.

### Operator Validation And Oracle Envelope

The independent operator oracle envelope is:

```text
ASCII("zom.checker-operator-kind.v0")
0x00
Encode(OperatorKind)
```

It is a test envelope, not a second product identity. Its vectors are:

| Input | Complete preimage hex | SHA-256 |
|---|---|---|
| `Primitive(StrictNe)` | `7a6f6d2e636865636b65722d6f70657261746f722d6b696e642e763000011d` | `4db7a174e931636649fbd2048307af559227fcca76633fbef1c886fab5d2c15c` |
| `CompoundAssignment(NullCoalesceAssign)` | `7a6f6d2e636865636b65722d6f70657261746f722d6b696e642e763000020f` | `36bab6dd63d2441ea47135f4770dda334eb1194ad110100c4f98a19f1b226d8e` |
| `Assignment` | `7a6f6d2e636865636b65722d6f70657261746f722d6b696e642e76300003` | `6d835cdc1c147b13b4c563647489c8e35aae1dd0dcb14c3bc303d9fb48482578` |
| `Error(Propagate)` | `7a6f6d2e636865636b65722d6f70657261746f722d6b696e642e7630000401` | `a6025127e0e9d9ad368360cc5421f9f3d1e7694824925654ef6ceef8b91061e2` |

The diagnostic verifier reconstructs the exact operator from the verified
primary syntax node and the semantic operation fact:

- `ZOM4019` accepts only `Primitive(Neg)`, `Primitive(LogicalNot)`,
  `Primitive(Add)`, `Primitive(Sub)`, `Primitive(Mul)`, `Primitive(Div)`,
  `Primitive(Rem)`, `Primitive(Pow)`, `Primitive(Eq)`, `Primitive(Ne)`,
  `Primitive(Lt)`, `Primitive(Le)`, `Primitive(Gt)`, `Primitive(Ge)`,
  `Primitive(Index)`, `Primitive(IndexMut)`, or `Primitive(Contains)`, because
  those are exactly the interface-mapped RFC 0009 operations.
- `ZOM4028` accepts only the operation reconstructed from the failing binary
  expression: `Add` through `LogicalOr`, `Contains`, or `NullCoalesce`,
  excluding comparison operations and non-binary primitive operations.
- `ZOM4029` accepts only `Eq`, `Ne`, `StrictEq`, `StrictNe`, `Lt`, `Le`, `Gt`,
  or `Ge` reconstructed from the comparison expression.
- `ZOM4081` accepts only the exact `PrimitiveOperation` reconstructed from its
  failing constant unary or binary syntax node. A disallowed constant
  expression emits `ZOM4079` before this rule.

A known `OperatorKind` used by the wrong diagnostic or node is `InvalidFact`.
An unknown tag, missing payload, trailing payload, numeric enum cast, or byte
oracle mismatch is `CanonicalCodecMismatch`. Rendering occurs only after
successful validation.

### Closed Type-Key Pattern Algebra

The complete pattern child records are:

```text
PatternObjectField {
  name: SemanticIdentifier,
  type: TypeKeyPattern,
  mutability: Mutability,
  presence: FieldPresence,
}

PatternFunctionType {
  parameters: Sequence<TypeKeyPattern>,
  success: TypeKeyPattern,
  raises: Maybe<TypeKeyPattern>,
}

PatternInterfaceInstantiation {
  interface: DefId,
  arguments: Sequence<TypeKeyPattern>,
}

PatternExistentialInterface {
  definition: DefId,
  arguments: Sequence<TypeKeyPattern>,
}

PatternAssociatedTypeBinding {
  associated: DefId,
  type: TypeKeyPattern,
}

PatternExistentialType {
  principal: PatternExistentialInterface,
  additionalInterfaces: SortedUniqueSequence<PatternExistentialInterface>,
  markers: SortedUniqueSequence<DefId>,
  associatedBindings: SortedUniqueSequence<PatternAssociatedTypeBinding>,
}
```

`TypeKeyPattern` is:

```text
TypeKeyPattern =
  Primitive(PrimitiveKind)
  | Tuple(Sequence<TypeKeyPattern>)
  | Object(SortedUniqueSequence<PatternObjectField>)
  | DynamicArray(element: TypeKeyPattern)
  | Slice(element: TypeKeyPattern)
  | FixedArray(element: TypeKeyPattern, length: uint64)
  | Function(PatternFunctionType)
  | Nominal(definition: DefId, arguments: Sequence<TypeKeyPattern>)
  | TypeParameter(parameter: DefId)
  | Union(SortedUniqueSequence<TypeKeyPattern>)
  | Intersection(SortedUniqueSequence<TypeKeyPattern>)
  | Reference(mutability: Mutability, referent: TypeKeyPattern)
  | RawPointer(mutability: Mutability, pointee: TypeKeyPattern)
  | Existential(PatternExistentialType)
  | InterfaceBound(PatternInterfaceInstantiation)
  | InterfaceSelf(interface: DefId)
  | Parameter(index: uint32)

ImplPattern {
  interface: PatternInterfaceInstantiation,
  self: TypeKeyPattern,
}

ImplHead {
  impl: ImplId,
  pattern: ImplPatternKey,
  selfType: SemanticTypeId,
  head: CanonicalTypeHead,
  genericParameters: Sequence<DefId>,
  whereConstraints: SortedUniqueSequence<CanonicalConstraint>,
  safety: ImplSafety,
  associatedBindings: SortedUniqueSequence<AssociatedTypeBindingData>,
  declarationSpan: SourceSpan,
}
```

Tags `0x01` through `0x10` exactly match the combined RFC 0005 and RFC 0014
`TypeData` declaration order. `Parameter = 0x11`. The child records preserve
the corresponding `TypeData` field order and replace every
`SemanticTypeId` child recursively with `TypeKeyPattern`. All identity values
expand through RFC 0011. Integers are unsigned big-endian. Sequences,
optionals, strong scalars, sorting, and duplicate rejection use RFC 0011.

RFC 0005 `ImplTypePattern` is removed. `ImplHead.interface` and
`ImplHead.selfPattern` are replaced by one `ImplHead.pattern` field of type
`ImplPatternKey`. The decoded `ImplPattern.interface.interface` is the
implemented interface `DefId`; its arguments and `ImplPattern.self` share the
same `Parameter` indices. No wrapper variant tag remains.

The standalone canonical key is:

```text
ASCII("zom.type-key-pattern.v1")
0x00
Encode(TypeKeyPattern)
```

A `TypeKeyPatternKey` is an RFC 0011 byte string wherever a standalone child
pattern key is required. The complete impl pattern key is:

```text
ASCII("zom.impl-pattern.v1")
0x00
Encode(ImplPattern.interface)
Encode(ImplPattern.self)
```

`ImplHead.pattern` encodes this complete `ImplPatternKey` as an RFC 0011 byte
string. Impl-pattern equality and canonical ordering use the complete key
bytes. A semantic-store slot, context brand, AST ID, source name, source
spelling, object address, or presentation string never enters either key.

The `ImplHead` declaration above completely replaces the RFC 0005 record and
fixes its field order. `pattern` stores the complete framed `ImplPatternKey`
byte string, including its domain and NUL byte; it does not encode the
unframed `ImplPattern` inline. `impl` and every `DefId` expand through RFC 0011.
`selfType` expands to its root `TypeKeyNode` without the semantic-type domain,
matching the RFC 0005 fact encoding. Every remaining field uses its unchanged
RFC 0005 tag, field order, sequence framing, sorting, and expanded identity
rules. Omitting, moving, or encoding `pattern` inline is
`CanonicalCodecMismatch`.

The pattern domain is v1 because it closes the combined semantic-type v1
algebra. Tag `0x11` is reserved only for `Parameter` in this domain. A future
semantic-type variant cannot silently reuse it: adding a semantic type requires
a later overlay and a new pattern domain.

### Pattern Construction And Validation

Pattern construction receives exactly one independently reconstructed source
interface, one independently reconstructed semantic self
`TypeKeyNode` v1, and the impl's source-verified unique ordered
`genericParameters` definition keys. It performs these steps:

1. Expand the complete semantic interface instantiation, self node, and every
   identity through RFC 0011.
2. Rebuild every interface argument and the self type in one traversal domain.
   Replace every `TypeParameter(parameter)` whose definition equals
   `genericParameters[i]` with `Parameter(i)` in both positions.
3. Reject any other `TypeParameter` in either position as `InvalidFact`.
4. Reject parameter counts or indices that exceed `uint32`, and reject an index
   that is not the exact verified declaration-order position.
5. Recursively rebuild every record. Preserve tuple, generic-argument, and
   function-parameter order.
6. Re-sort object fields by canonical field-name bytes, union and intersection
   members by complete pattern bytes, existential additional interfaces by
   complete interface bytes, existential markers by expanded `DefId`, and
   associated bindings by expanded associated `DefId`. Reject duplicates by
   those same keys. The semantic-node order is not reused when replacement
   changes bytes.
7. Reject a published impl pattern when any `Parameter` occurs recursively in
   either interface arguments or self beneath a `Union` or `Intersection`, or
   beneath an argument of an `Existential.additionalInterfaces` entry. Also
   reject a `Parameter` recursively beneath
   `Existential.principal.arguments` when any additional interface has the
   same interface definition as the principal. These are
   normalization-sensitive positions: substitution can otherwise change a
   collection's order, uniqueness, arity, outer semantic-type tag, or the
   required distinction between principal and additional interfaces.
8. Reject every `InterfaceSelf` in a published impl pattern. RFC 0014
   substitution must replace it with the exact canonical impl self type before
   construction.
9. When the outer self pattern is `Tuple`, `Function`, `Union`, or
   `Intersection`, require its element or parameter count to fit `uint32` before
   head derivation. No narrowing, wrapping, saturation, or truncation is
   permitted.
10. Require every declared impl generic parameter to occur at least once in the
   complete interface-argument or self pattern. A parameter that occurs only
   in where-constraints or associated bindings cannot be selected and is
   `InvalidFact`.
11. Derive `ImplHead.head` from the validated outer self pattern and require
    exact equality with the stored head.

The rule in step 7 is recursive. For example, `Union([Bool, Parameter(0)])`,
`Nominal(Box, [Intersection([I32, Parameter(0)])])`, an existential additional
interface `I<Parameter(0)>`, and principal `I<Parameter(0)>` with any additional
`I<Rigid>` are invalid published impl patterns. An outer `Parameter`, an
ordered nominal argument `Box<Parameter(0)>`, principal
`I<Parameter(0)>` when every additional interface has a different definition,
and an associated-binding value may contain parameters because substitution
cannot change the enclosing collection key, order, uniqueness, arity, tag, or
principal/additional distinction.

This publication restriction is part of impl-head well-formedness, not the
standalone codec. The codec remains total over every pattern variant. A
well-formed codec node that violates the publication restriction is
`InvalidFact`.

`InterfaceSelf` remains decodable and testable so the algebra is closed. A
well-formed `InterfaceSelf` in a published impl head is `InvalidFact`; malformed
bytes are `CanonicalCodecMismatch`.

### Singular Impl Source Contract And Provenance

The source grammar has two disjoint declaration forms:

```text
OrdinaryImplDeclaration =
  Unsafe? "impl" TypeParameters? InterfaceInstantiation "for" Type
  WhereClause? ImplBody

MarkerImplDeclarationCandidate =
  Unsafe? "impl" "!"? MarkerPath "for" ClosedType ";"
```

An ordinary impl has exactly one interface and a body. `+` is not admitted in
an ordinary impl header. `ImplIfaceList` is not part of
`StandaloneImplDecl`; its generated AST field is one `interface` `NodeId`.
Marker impls have no type parameters, where clause, associated bindings,
members, or body. Their target may contain concrete type arguments but no
impl-owned or unresolved type parameter. The parser emits:

| Source failure | Diagnostic | Primary site |
|---|---|---|
| `+` after an ordinary impl interface | `ZOM2100 ImplRequiresSingleInterface` | first `+` |
| type parameters on a bodyless marker impl | `ZOM2101 MarkerImplCannotBeGeneric` | opening `<` |
| a where clause on a bodyless marker impl | `ZOM2102 MarkerImplCannotHaveWhereClause` | `where` |
| negative marker impl with `unsafe` | `ZOM2103 NegativeMarkerImplCannotBeUnsafe` | `unsafe` |
| negative marker impl with a body | `ZOM2104 MarkerImplCannotHaveBody` | opening `{` |

All five parser diagnostics are errors with zero display arguments. Their
messages are, respectively, `An impl declaration implements exactly one
interface`, `A marker implementation cannot declare type parameters`, `A
marker implementation cannot have a where clause`, and `A marker
implementation cannot be unsafe`, and `A marker implementation cannot have a
body`. They suppress recovery diagnostics for the same declaration and
publish no declaration node. When one declaration matches several rows, the
diagnostic whose primary token has the smallest source byte offset wins; a tie
uses the smaller numeric diagnostic ID. Only that diagnostic is emitted for
the declaration. For example, `unsafe impl<T> !M for Box<T> where T: I {}`
emits only `ZOM2103` at `unsafe`.

The signature checker resolves the singular interface definition and requires
the source form to agree with its verified `InterfaceSignature.markerOnly`
classification. An ordinary body targeting a marker-only interface emits
`ZOM4088 MarkerInterfaceRequiresBodylessImpl`. A bodyless marker declaration
targeting a non-marker interface emits `ZOM4089 BehaviorInterfaceRequiresImplBody`.
Either error publishes neither an impl head nor a marker fact. The parser may
use the final body delimiter to distinguish a positive short marker name from
an ordinary interface; semantic classification remains checker authority.
Both checker diagnostics are signature-stage errors with zero display
arguments and `itemOrdinal = 0`. Their messages are `Marker interfaces require
a bodyless marker implementation` and `Behavior interfaces require an impl
body`. `CheckerErrorId` appends `ZOM4088` and `ZOM4089` in that order.

After the verified acyclic parent closure is available, the
`VerifiedMarkerShapeInventory` classifies an interface as marker-shaped exactly
when its own declaration and every interface in that
closure contain zero methods, accessors, associated types, or other behavior
requirements. Every parent must itself have marker shape. Direct emptiness is
insufficient: an empty interface inheriting one behavior-bearing interface is
behavior-bearing. The signature checker sets
`InterfaceSignature.markerOnly = true` only from an inventory `ClosedMarker`;
a marker-shaped interface becomes closed only when its generic-parameter
sequence is empty. A marker-shaped interface with one or more generic
parameters emits `ZOM4090 GenericMarkerInterfaceNotAllowed`,
an interface-signature-stage error with zero display arguments, message
`Marker interfaces cannot declare generic parameters`, primary node equal to
the first generic parameter, and `itemOrdinal = 0`. It publishes no interface
signature. `CheckerErrorId` appends `ZOM4090` after `ZOM4089`. This keeps
`MarkerFactKey.marker` sufficient: marker identity is one zero-arity `DefId`,
not an unrecorded generic instantiation.

### Context-Bound Marker Policy Registry

Interface emptiness classifies marker shape; it does not silently grant
compiler auto-derivation. One pre-signature inventory is the sole marker-shape
authority, and one immutable registry is the sole authority for structural
subjects, builtin primitives, and reference propagation:

```text
InterfaceMarkerShape = Behavior | GenericMarkerShape | ClosedMarker

VerifiedMarkerShapeInventory {
  semanticContext: SemanticContextBrand,
  contextFingerprint: SemanticContextFingerprint,
  revision: MarkerShapeInventoryRevision,
  shapes: SortedMap<DefId, InterfaceMarkerShape>,
}

MarkerStructuralSubject =
  Tuple | Object | FixedArray | NominalStruct | NominalEnum

MarkerPolicy {
  structuralSubjects: SortedUniqueSequence<MarkerStructuralSubject>,
  builtinPrimitives: SortedUniqueSequence<PrimitiveKind>,
  referenceRequirements: SortedMap<Mutability, DefId>,
}

VerifiedMarkerPolicyRegistry {
  semanticContext: SemanticContextBrand,
  contextFingerprint: SemanticContextFingerprint,
  configurationRevision: Sha256Digest,
  shapeInventoryRevision: MarkerShapeInventoryRevision,
  revision: MarkerPolicyRegistryRevision,
  entries: SortedMap<DefId, MarkerPolicy>,
}
```

`InterfaceMarkerShape` tags are `Behavior = 0x01`,
`GenericMarkerShape = 0x02`, and `ClosedMarker = 0x03`.
`MarkerStructuralSubject` tags are `Tuple = 0x01`, `Object = 0x02`,
`FixedArray = 0x03`, `NominalStruct = 0x04`, and `NominalEnum = 0x05`.
Record fields encode in declaration order. An absent registry entry is exactly
three empty collections and therefore permits explicit evidence only. A map
entry does not repeat its marker key in the payload. This permits explicit
marker assertions for a local marker-shaped interface without making source
spelling or the accidental absence of members an auto-derivation switch.

After identities, binding inputs, and the acyclic interface-parent closure are
verified, the session constructs `VerifiedMarkerShapeInventory` from every
exact `VerifiedBoundModuleInput`. Its one pure classifier reads the immutable
interface AST and verified parent bindings. An interface is `Behavior` when it
or any transitive parent has a behavior requirement; otherwise it is
`GenericMarkerShape` when it has a non-empty generic-parameter sequence and
`ClosedMarker` when that sequence is empty. The inventory covers every
interface `DefId` in the frozen definition inventory exactly once. The registry
builder and signature checker consume this same private capability; neither
re-walks source to implement a second shape classifier. The signature checker
uses the local generic-parameter node only as the already-classified
`ZOM4090` anchor.

`MarkerShapeInventoryRevision` is SHA-256 over:

```text
ASCII("zom.marker-shape-inventory.v0")
0x00
SemanticContextFingerprint
EncodeSortedRecordBytes(shapes)
```

The non-empty oracle uses a zero context fingerprint and one two-byte record
expanding interface `a1` as `ClosedMarker`. Its complete 80-byte preimage is:

```text
7a6f6d2e6d61726b65722d73686170652d696e76656e746f72792e763000000000000000000000000000000000000000000000000000000000000000000000000000000000010000000000000002a103
```

Its SHA-256 is
`1594af0c3d3f1cd1c3d5e58ce672673855b6924ceced83e78bf4306c77dc7e7b`.

The compiler distribution supplies a canonical `MarkerPolicyConfiguration`
beside the RFC 0004 configured-prelude requests. Configuration entries identify
markers and reference-rule requirements by complete expanded RFC 0011
`DefinitionKey`, never by identifier text, path text, ordinal, or store slot.
Its entry shape is one expanded marker `DefinitionKey` followed by
`MarkerPolicy`, with every reference requirement's `DefId` replaced by its
expanded `DefinitionKey`. `configurationRevision` is SHA-256 over:

```text
ASCII("zom.marker-policy-configuration.v0")
0x00
EncodeSortedRecordBytes(configuration entries)
```

The 30-byte configuration entry used by the registry oracle expands marker
`a1`, structural subjects `Tuple` and `NominalStruct`, builtin primitive `I32`,
and one shared-reference requirement for marker `b2`. It produces this complete
81-byte preimage:

```text
7a6f6d2e6d61726b65722d706f6c6963792d636f6e66696775726174696f6e2e7630000000000000000001000000000000001ea100000000000000020104000000000000000103000000000000000101b2
```

Its SHA-256 is
`7b17b923e4931f81d8fc06e17db18786d8e665623e83c87093aeba9493fc1dba`.
Every configured-prelude request must use this exact configuration revision in
its RFC 0004 `Prelude` site and provenance. After RFC 0011 freezes identities
and the marker-shape inventory freezes, the session resolves every configured
key through the frozen definition registry and constructs the only
`VerifiedMarkerPolicyRegistry`. Every configured marker and reference
requirement must resolve to `ClosedMarker` in the same inventory and be owned
by a module reached through one of those verified prelude edges. A missing,
foreign, behavior, generic-marker, duplicate, or cross-context entry is an
invariant and publishes no registry or signature facts. There is no mutable
registration API, package-supplied configuration, or per-module override.
Marker facts and `SignatureFactsRevision` cannot be constructed before the
inventory and registry are both frozen.

`MarkerPolicyRegistryRevision` is SHA-256 over this exact RFC 0011 stream:

```text
ASCII("zom.marker-policy-registry.v0")
0x00
SemanticContextFingerprint
configurationRevision
MarkerShapeInventoryRevision
EncodeSortedRecordBytes(entries)
```

The independent non-empty oracle uses a zero context fingerprint, the exact
configuration and marker-shape inventory revisions above, and the same 30-byte
entry. Its complete 172-byte preimage is:

```text
7a6f6d2e6d61726b65722d706f6c6963792d72656769737472792e76300000000000000000000000000000000000000000000000000000000000000000007b17b923e4931f81d8fc06e17db18786d8e665623e83c87093aeba9493fc1dba1594af0c3d3f1cd1c3d5e58ce672673855b6924ceced83e78bf4306c77dc7e7b0000000000000001000000000000001ea100000000000000020104000000000000000103000000000000000101b2
```

Its SHA-256 is
`15329853e2faae147a2f5ca73c85a58c4084c70faa3d1faef278c856fd75067b`.
Mutation tests cover every tag, field order, key, required marker, set order,
duplicate, context, configuration revision, and prelude-ownership edge.

The standalone policy and component records have these exact vectors. The
hash is SHA-256 over the displayed record bytes without another envelope:

| Fixture | Bytes | SHA-256 |
|---|---|---|
| Empty policy, therefore explicit-only | `000000000000000000000000000000000000000000000000` | `9d908ecfb6b256def8b49a7c504e6c889c4b0e41fe6ce3e01863dd7b61a20aa0` |
| Primitive `I32` builtin policy | `00000000000000000000000000000001030000000000000000` | `b9847e699ac4b297893d2e911fd333e33840cbdd474879379670908e89f5256b` |
| Cross-marker shared-reference policy requiring `b2` | `00000000000000000000000000000000000000000000000101b2` | `d05c3fb4dd99fc019c4fe3c26505f7efd951a26ad7070174cd479856c9378ed7` |
| Nominal-enum structural policy | `00000000000000010500000000000000000000000000000000` | `e161c3bd9c0f40a111b9a9f035aa233ab64d9098c9ca1813ae3e30a8e341507c` |
| Cross-marker reference component over `Primitive(I32)` | `0000000000000001050103b20103` | `ab165f9a332ccdbcdcf8ce4173475f7cf96aed692d541af24d199ba451df9444` |
| Enum variant `a1` payload index two over `Primitive(I32)` | `000000000000000106a1000000020103b20103` | `ea293fa9a7782c78e3fdf138e370f7316e85d18a3f08f01195185050be6f03f6` |

RFC 0005 `MarkerComponentStep` appends `ReferenceReferent = 0x05` and
`EnumVariantPayload(variant: DefId, index: uint32) = 0x06`.
`MarkerEvidence::Builtin { primitive: PrimitiveKind }` remains unchanged. A
builtin proof is valid exactly when its subject is the same primitive kind and
that kind occurs in the marker's policy entry. Its polarity is positive and
its declaration span is absent. Structural and builtin facts retain their
canonical record encodings so independently recomputed proofs can be compared
byte-for-byte, but they are ephemeral query results rather than persisted
marker-map entries.

`TupleElement.index` and `EnumVariantPayload.index` remain `uint32`. The
authoritative tuple-element and enum-variant-payload sequences retain RFC
0011's `uint64` count. A structural query first validates the complete
`uint64` declaration-order ordinal and converts it only when the value is at
most `0xffffffff`. If any required tuple element or enum payload element has a
larger ordinal, that query is unsatisfied. There is no truncation, saturation,
modulo conversion, partial component list, or alternate path spelling. An
ephemeral proof that omits the unrepresentable component or contains a narrowed
index is rejected as `InvalidFact` by the proof verifier.

Structural evidence is authorized only when the subject's exact closed class
occurs in `MarkerPolicy.structuralSubjects`: every tuple element uses
`TupleElement`; every object field uses `ObjectField`; a fixed array has exactly
one `ArrayElement` obligation for its element type; a nominal struct has every
declared field exactly once through `NominalField`; and a nominal enum has every
variant payload element exactly once through `EnumVariantPayload`, using the
verified variant `DefId` and declaration-order payload index. Empty products
and sums have an empty component sequence. Every component query must resolve
positively for the same marker. A reference structural proof is authorized
only by the one `referenceRequirements` map entry matching its mutability; it
has exactly one
`ReferenceReferent` component whose positive supporting fact uses
the entry's marker and the normalized referent type. Reference lifetime and
borrow legality remain RFC 0007 obligations and are never inferred from marker
support. Function values acquire no
marker fact because `FunctionTypeData` carries neither capture identity nor a
verified capture-capability summary. Raw pointers, union, intersection,
dynamic array, slice, nominal class, error type, existential, interface-bound,
type-parameter, and unresolved subjects likewise acquire no structural or
builtin proof. The demand-driven proof algorithm below never treats a cycle as
proof; a recursive support cycle therefore requires explicit unsafe evidence.
Negative facts remain explicit only.
These fail-closed rules replace every RFC 0005 reference to an undefined closed
primitive-marker table or derivability classifier. A later function-capability,
raw-pointer, aliased-container, class, or error marker design requires its own
semantic data and accepted codec revision; it cannot be inferred from a bare
shape.

`SignatureCheckingInput` receives the same
`const VerifiedMarkerShapeInventory` and
`const VerifiedMarkerPolicyRegistry`. `CoherenceBuildingInput` receives the
registry to validate exact policy lineage but no semantic-type store, AST, or
nominal-signature inventory. It accepts each `CoherenceModuleInput` only as the
exact projection of one already verified `VerifiedModuleInterface`, never as
caller-assembled records. Persisted marker maps at both boundaries contain
only verified explicit facts.

One demand-driven checker service is the sole builtin and structural proof
authority:

```text
MarkerProofInput {
  semanticContext: SemanticContextBrand,
  contextFingerprint: SemanticContextFingerprint,
  policy: const VerifiedMarkerPolicyRegistry,
  semanticTypes: const SemanticTypeStore,
  componentInterner: SemanticTypeInterningCapability,
  localSignatures: const VerifiedSignatureFacts,
  importedSignatures: const ImportedSignatureView,
  coherence: const FrozenCoherenceView,
}

MarkerProofResult =
    Positive { proof: MarkerFact }
  | Negative { explicitFact: MarkerFact }
  | Unsatisfied
  | InvariantRejected {
      failures: SortedNonEmptySequence<CheckerVerificationFailure>
}
```

`MarkerProofInput` is an unforgeable non-owning capability constructed only
from one already verified RFC 0005 `BodyCheckingInput` plus the exact policy
registry whose revision is carried by its local signatures and frozen
coherence view. `SemanticTypeInterningCapability` is a non-copyable,
non-storable session borrow of that input's exact semantic store. It accepts
only RFC 0005 `CanonicalTypeData`, delegates to the store's linearizable
`intern`, and exposes no second store, registry, signature lookup, or unchecked
type constructor. The proof input verifier requires its context, fingerprint,
and store identity to equal `semanticTypes`. Callers cannot combine a semantic
store, interner, signature view, coherence view, or registry from different
sessions or revisions. The store is append-only: later interning cannot mutate
an earlier type or proof input.

The engine answers any requested `(marker, subject)` key. It does not enumerate
a finite subject universe, pre-publish candidates, or require a type to have
appeared in a module signature. Before resolving a key it validates every
context brand, fingerprint, policy revision, signature parent, coherence
revision, identity, and semantic-type handle. A mismatch returns
`InvariantRejected` before evidence lookup.

Resolution order is exact:

1. An exact explicit fact in `FrozenCoherenceView.markerFacts` wins. Positive
   explicit evidence returns `Positive`; negative explicit evidence returns
   `Negative` and suppresses builtin or structural proof.
2. Otherwise, a primitive authorized by the marker's `builtinPrimitives`
   returns a positive ephemeral builtin proof.
3. Otherwise, the engine reads the subject from `SemanticTypeStore`, reads
   nominal struct fields or enum variants and payloads only from the exact
   authorized local or imported signature, checks the marker policy, constructs
   the complete canonical component sequence, and recursively resolves every
   required component. For a nominal subject, the engine requires the
   signature definition kind to match the selected structural class, requires
   subject argument count to equal generic-parameter count, and verifies every
   generic parameter is unique, definition-kind `TypeParameter`, owned by that
   nominal definition, and in declaration order. It then constructs the exact
   substitution `genericParameters[i].parameter -> subject.arguments[i]`.
   Every field type and enum-variant payload type is recursively rebuilt from
   immutable `TypeData`, replacing exactly those `TypeParameter` nodes. Each
   rebuilt node is normalized through the RFC 0005 `TypeCanonicalizer` and
   atomically interned through `componentInterner` before its resulting
   `SemanticTypeId` enters `componentType` or `supportingFact.subject`.
   Parameters not owned by the verified substitution, unresolved nodes, and
   `InterfaceSelf` are invariants. A reference component queries the exact
   required marker selected by its mutability. The result is a positive
   ephemeral structural proof only when every recursive result is `Positive`.
4. An absent policy, unsupported outer form, negative or unsatisfied component,
   or unrepresentable component ordinal returns `Unsatisfied`. A component type
   that is not already interned never selects `Unsatisfied`; the engine creates
   its canonical identity as part of step 3. Malformed inputs, foreign
   identities, incomplete or inconsistent nominal signatures, substitution or
   interning failure, wrong proof reconstruction, or non-canonical records
   return `InvariantRejected`.

Depth-first resolution checks exact explicit positive and negative facts before
consulting completed memo entries or pushing a key onto the current root
invocation's private active-key stack. Only re-entry of a key already on that
same stack yields `Unsatisfied` for a cyclic dependency. The active stack and
its `Visiting` state are never visible to another root invocation, worker,
producer, or verifier, so another thread's in-flight proof cannot be mistaken
for a cycle. A pure cycle, self-cycle, or cycle reached through a different
marker therefore never proves itself, while an exact explicit positive fact can
seed a recursive proof.

An optional lineage-bound shared memo contains only immutable completed
`Positive(proof)` or stable `NotPositive` entries. Concurrent misses compute
with independent active stacks and may duplicate work; publication is
linearizable, and two completed positive computations for one key must have
byte-identical proofs or select `InvariantRejected`. No in-flight sentinel is a
memo result. `NotPositive` is published only after a stable structural
`Unsatisfied` result; `InvariantRejected`, a failed interning operation, and
explicit positive or negative results are never cached there. Producer and
independent verifier use distinct active stacks and distinct memo storage; the
verifier never consumes a producer cache entry. A required generic component
is substituted, canonicalized, and interned before structural resolution, so
later unrelated store growth cannot change `NotPositive` for an existing key.
Clearing all memo storage and recomputing must produce the same result and
byte-identical proof. Memo state is never serialized, shared across policy or
coherence revisions, or treated as semantic authority. Concurrent equal
component interning is linearizable and returns one semantic identity; unequal
insertion slot order never enters a proof encoding or comparison.

`SignatureFactsCandidate` and
`VerifiedSignatureFacts` add `markerPolicyRegistryRevision` immediately after
`bindingSurfaceRevision`. RFC 0013's runtime interface schema is replaced in
full by:

```text
VerifiedModuleInterface {
  semantic_context_brand: SemanticContextBrand,
  revision: ModuleInterfaceRevision,
  package_id: PackageId,
  crate_id: CrateId,
  module_id: ModuleId,
  source_content_digest: Sha256Digest,
  binding_surface: VerifiedExportSurface,
  signature_facts_revision: SignatureFactsRevision,
  marker_policy_registry_revision: MarkerPolicyRegistryRevision,
  imported_signature_view_revision: ImportedSignatureViewRevision,
  borrow_surface: VerifiedBorrowInterfaceSurface,
  signatures: AuthorizedSignatureBundle,
  visible_bindings: Sequence<VisibleBinding>,
  exported_bindings: Sequence<ExportedBinding>,
  coherence_impl_heads: SortedMap<ImplId, ImplHead>,
  marker_facts: SortedMap<MarkerFactKey, MarkerFact>,
}

CoherenceModuleInput {
  module: ModuleId,
  interfaceRevision: ModuleInterfaceRevision,
  markerPolicyRegistryRevision: MarkerPolicyRegistryRevision,
  implHeads: SortedMap<ImplId, ImplHead>,
  markerFacts: SortedMap<MarkerFactKey, MarkerFact>,
}
```

Every `marker_facts` or `markerFacts` field in persisted signature,
module-interface, coherence-input, coherence-candidate, and frozen-coherence
records is an explicit-only `SortedMap`. Its key is unique after tree-local and
cross-module conflict processing. A `Structural` or `Builtin` evidence tag in
one of those maps is `InvalidFact` before interface freezing and
`InvalidProjection` at a frozen boundary. The demand-driven proof result is not
inserted back into any of these maps.

Module-interface v3 encodes the policy revision immediately after
`signature_facts_revision`. `CoherenceCandidate` and
`FrozenCoherenceView` add it immediately after `contextFingerprint`, and
coherence-view v1 encodes it in that position. Every module submitted to one
coherence build must carry and project the exact same revision as the registry
and root candidate. Missing, stale, or mixed policy lineage is an input
invariant before evidence, orphan, overlap, or diagnostic processing.

Source reconstruction and classification operate independently on every RFC
0018 implementation occurrence. A failed interface signature, an ordinary
braced occurrence targeting a marker-only interface (`ZOM4088`), a bodyless
occurrence targeting a behavior interface (`ZOM4089`), or a marker occurrence
rejected by `ZOM4091` or `ZOM4092` emits only its earlier source diagnostic and
is removed. Each successfully classified ordinary occurrence enters the
ordinary temporary header sequence; each successfully classified marker
occurrence enters the marker temporary header sequence. One occurrence in both
sequences is an invariant failure.

The tree-local coherence-admission phase is the final substage of signature
verification. It runs after every source occurrence has been reconstructed and
classified but before an impl-head map, marker map,
`SignatureFactsCandidate`, or `VerifiedSignatureFacts` is constructed. The
complete survivor sequences, exact bound tree, semantic-type store, and
ownership registries are therefore still available. For an explicit marker
survivor, its orphan predicate is legal
exactly when the marker definition is owned by the candidate's current module,
or the normalized subject has outer head `Nominal(definition)` and that
definition is owned by the candidate's current module. A primitive, tuple,
object, fixed array, reference, raw pointer, function, union, intersection,
dynamic array, slice, existential, interface-bound, type-parameter, unresolved,
or blanket outer head is non-local even when a nested component is local.
Aliases are normalized before this test; nested local arguments, constraints,
and presentation text confer no locality. Positive and negative explicit
marker candidates use the same predicate. Structural and builtin candidates
are not persisted; their ephemeral compiler proofs never undergo an orphan
test.

Within signature verification, input lineage and source reconstruction run
first, followed by interface-kind classification, `ZOM4091`, `ZOM4092`,
explicit-marker `ZOM4054`, and local survivor conflicts in that order. Only
unique survivors construct candidate impl heads, the candidate marker map, and
verified signature facts. Module-interface verification follows. Global coherence
then validates frozen projection lineage, performs cross-module same-key
explicit conflicts, ordinary orphan checking, and ordinary overlap. The later
demand-driven proof engine owns builtin and structural evidence precedence.
`ZOM4054 OrphanImpl` for an explicit marker uses the marker definition and
normalized subject as its two display arguments and the exact source
declaration span through a tree-local `CheckerFailureRef`. An orphan candidate
is removed before local conflict grouping, so it cannot also emit `ZOM4017` and
cannot enter a verified module interface. This is the executable marker
specialization of RFC 0005's orphan rule. No signature producer,
module-interface verifier, or global frozen-interface pass makes a second
marker orphan decision outside this final signature-verification substage.

A positive bodyless declaration that resolves to a marker-only interface must
have `is_unsafe = true`. Otherwise the checker emits `ZOM4091
PositiveMarkerImplRequiresUnsafe`, a signature-stage error with zero display
arguments, message `A positive marker implementation requires unsafe`, primary
span equal to its exact `impl` token, and `itemOrdinal = 0`. Its RFC 0017
`DiagnosticOccurrenceKey` uses the occurrence's module diagnostic root,
signature stage `0x01`, `ImplementationOwner(source.implementation)`,
`SignatureClassification = 0x15`, and the complete
`source.site` `IdentitySyntaxSiteKey` as its occurrence. Primary provenance is
`IdentitySyntaxSite(source.site)`. It has no notes or recovery and publishes no
marker fact, module-interface marker entry, or coherence input. `CheckerErrorId`
appends `ZOM4091` after `ZOM4090`. Interface classification precedes this rule,
so a safe bodyless declaration targeting a behavior interface emits only
`ZOM4089`, not `ZOM4091`.

An explicit source marker whose normalized subject is one builtin primitive
authorized by its verified marker-policy entry emits `ZOM4092
ExplicitImplConflictsWithBuiltinMarker`, a
signature-stage error with zero display arguments, message `Builtin marker
evidence cannot be replaced by an explicit implementation`, primary node equal
to the `MarkerImpl` declaration, primary span equal to its complete source
span, and `itemOrdinal = 0`. It publishes no marker fact. `CheckerErrorId`
appends `ZOM4092` after `ZOM4091`.

RFC 0005 `CheckerDiagnosticProducer` appends
`SignatureClassification = 0x15` after RFC 0014
`ReceiverNormalization = 0x14`. The exact checker registry rows are:

| ID | Stage and producer | Primary source authority and span | Recovery and notes |
|---|---|---|---|
| `ZOM4088 MarkerInterfaceRequiresBodylessImpl` | `Signature`, `SignatureClassification` | ordinary impl interface node and its complete source span | `None`; no notes |
| `ZOM4089 BehaviorInterfaceRequiresImplBody` | `Signature`, `SignatureClassification` | marker path node and its complete source span | `None`; no notes |
| `ZOM4090 GenericMarkerInterfaceNotAllowed` | `Signature`, `SignatureClassification` | first generic-parameter node and its complete source span | `None`; no notes |
| `ZOM4091 PositiveMarkerImplRequiresUnsafe` | `Signature`, `SignatureClassification` | complete source occurrence and its `impl` token span | `None`; no notes |
| `ZOM4092 ExplicitImplConflictsWithBuiltinMarker` | `Signature`, `SignatureClassification` | `MarkerImpl` declaration node and its complete source span | `None`; no notes |

All five have error severity, no display arguments, and `itemOrdinal = 0`.
`ZOM4091` uses the complete RFC 0017 occurrence and provenance contract above.
The other rows use `CheckerEmitterOrdinal` with signature stage tag `0x01`; the
owning ordinary impl, marker impl, or interface declaration's verified
schema-preorder index is `ownerSchemaPreorder`; the table's primary node
schema-preorder index is `siteSchemaPreorder`; and `itemOrdinal` is zero.

Parser rejection suppresses every checker row for that declaration. A failed
interface signature, including `ZOM4090`, suppresses all dependent impl rows.
For an admitted interface signature, `ZOM4088` or `ZOM4089` interface-kind
classification precedes `ZOM4091`; `ZOM4091` precedes `ZOM4092`; `ZOM4092`
precedes `ZOM4054` orphan checking and `ZOM4017` conflict checking. A
mismatched stage, producer, node, span, ordinal, recovery handle, note list, or
precedence result is an invalid checked fact.

The signature verifier derives expected impl facts from one exact RFC 0005
`SignatureCheckingInput`. Candidate fields are never source authority. It
performs a separate schema-preorder traversal of the immutable tree in
`VerifiedBoundModuleInput` and joins it only with the same verified binding
metadata, imported signature view, frozen identity registries, and semantic
type store carried by that input.

For each ordinary RFC 0004 `ImplOccurrenceBindingFact`, the verifier
reconstructs:

```text
VerifiedSourceImplHeader {
  source: ImplSourceOccurrenceKey,
  authority: ImplId,
  declarationNode: NodeId,
  interfaceNode: NodeId,
  interface: InterfaceInstantiation,
  selfNode: NodeId,
  selfType: SemanticTypeId,
  genericParameters: Sequence<DefId>,
  whereConstraints: SortedUniqueSequence<CanonicalConstraint>,
  safety: ImplSafety,
  associatedBindings: SortedUniqueSequence<AssociatedTypeBindingData>,
  declarationSpan: SourceSpan,
}
```

For each marker RFC 0004 `ImplOccurrenceBindingFact`, it reconstructs:

```text
VerifiedSourceMarkerHeader {
  source: ImplSourceOccurrenceKey,
  authority: ImplId,
  declarationNode: NodeId,
  markerNode: NodeId,
  marker: DefId,
  selfNode: NodeId,
  subject: SemanticTypeId,
  polarity: Polarity,
  safety: ImplSafety,
  declarationSpan: SourceSpan,
}
```

These are verifier-local proofs with no persistence codec. Their `NodeId`
fields never enter signature, coherence, or module-interface bytes.
An RFC 0018 identity occurrence group is source-form-neutral. Ordinary and
bodyless occurrences with one equal `ImplIdentityRecord` are reconstructed and
classified independently, regardless of which form supplied the shared
authority. For one resolved interface kind, only the matching source form may
survive. A survivor publishes under the shared authority even when the
authority site failed classification, while retaining its own node, scope,
bindings, source key, and diagnostic or evidence provenance. Publishing both
an `ImplHead` and a `MarkerFact` for one identity group is an invariant.
Reconstruction is exact:

1. `ImplOccurrenceBindingFact.node` identifies exactly one
   `StandaloneImplDecl` or `MarkerImpl` whose frozen inventory issued
   `occurrence`. Its frozen occurrence entry must repeat the fact's node,
   complete source key, and shared authority.
2. Every node, token-derived flag, list, body, and span is read from the exact
   generated AST fields. No candidate node, key, or span selects them.
3. Every interface, marker, and self-type name has one exact RFC 0004 binding.
   The verifier interprets immutable type syntax through those `DefId`
   bindings and imported canonical signatures; it performs no textual lookup
   or scope traversal.
4. The verifier independently canonicalizes the complete interface and self
   type through the same context-bound `SemanticTypeStore`. It never accepts a
   candidate pattern, subject, head, or semantic type as reconstruction input.
5. Ordinary generic parameters, constraints, safety, associated bindings, and
   provenance are reconstructed from exact source nodes and verified semantic
   signatures. The source interface must be non-marker.
6. Marker declarations require an empty generic-parameter sequence, no where
   clause, no body, a zero-generic-arity marker-only interface, and a closed
   subject containing no impl-owned or unresolved `TypeParameter`. They produce
   exactly one temporary marker header whose key is `(marker, subject)`, whose
   polarity and explicit evidence authority exactly equal the source header,
   and whose
   `declarationSpan` is `some(source declaration span)`. Positive explicit
   evidence requires `polarity = Positive` and `safety = UnsafeAssertion`;
   negative explicit evidence requires `polarity = Negative` and
   `safety = Safe`. There is no safe explicit positive assertion and no unsafe
   negative assertion. Marker declarations produce no `ImplHead` and no
   contextual `Self` owner. Signature reconstruction retains each
   source-shape-valid marker header in the temporary sequence; the final tree-local
   coherence-admission substage is the sole owner of its orphan and local
   conflict decisions before any marker map is published.
7. A marker `ImplOccurrenceBindingFact.scope` must be the exact schema-preorder
   `ImplBody` scope owned by the same `ImplOccurrenceId`, with the declaration's lexical
   parent, and `members` must be empty. The scope contains no definitions and
   cannot become an RFC 0014 `ImplSelfOwner`. A missing, reused, non-empty, or
   differently owned scope is RFC 0004 `InvalidBindingFact`.

Ordinary headers remain a canonically sorted temporary sequence until
classification completes. They group by expanded `ImplKey` and then canonical
source order of the complete `IdentitySyntaxSiteKey`. A group with one survivor
publishes one `ImplHead` under its shared `ImplId` authority. A group with more
than one survivor emits RFC 0005 `ZOM4017 ConflictingImpl` for every occurrence
after the first, attaches exactly one `ZOM4071 PreviousImplHere` note at the
first survivor, and publishes no `ImplHead`. The typed interface and self
arguments are reconstructed from the complete verified header; source text is
never a substitute.

Marker headers remain a canonically sorted temporary sequence until the final
tree-local admission substage completes. They sort by complete `MarkerFactKey`,
expanded `ImplKey`, and canonical source order of the complete
`IdentitySyntaxSiteKey`. That substage applies `ZOM4092`, then the marker orphan
predicate, and removes every rejected header. Every remaining same-key group
with more than one survivor emits `ZOM4017` for every occurrence after the
first, with exactly one `ZOM4071` note at the first survivor, and publishes no
`MarkerFact`. This rule covers positive/positive, negative/negative, and
positive/negative groups, including multiple occurrences under one shared
authority. A unique survivor publishes explicit marker evidence under its
shared authority and retains that survivor's source site as provenance.

Each local ordinary exact-collision diagnostic uses coherence stage `0x02`,
coherence producer `0x04`, `ExactIdentityCollision = 0x02`, and the complete
later `ImplSourceOccurrenceKey` as its RFC 0017 occurrence. Each local marker
conflict uses `MarkerLocalConflict = 0x03` and the complete later occurrence
key. Both are primary at the later survivor and have exactly one `ZOM4071`
secondary at the first survivor. The span-addressed record below applies when
the coherence builder consumes frozen module interfaces without their trees.

Cross-module coherence performs a grouped check over a stable sorted merge
stream of all verified explicit module marker records before constructing
`CoherenceCandidate.markerFacts`. Module or import order is not a tie breaker.
Every explicit projection is already marker-orphan-legal by the final
tree-local signature admission substage and exact module-interface projection;
global coherence does not re-run a subject-shape test. Within one key group,
multiple explicit records use the source conflict rule above. A unique explicit
record enters the frozen map unchanged. A `Structural` or `Builtin` record at
this boundary is `InvalidProjection`; there is no competing-evidence stream,
eager support graph, or finite structural-candidate coverage obligation.

The proof engine applies evidence precedence later for each requested key:
explicit positive or negative first, then builtin, then structural. Because
only one exact explicit record can survive coherence, the unique-key map can
represent every persisted input required by that query. The engine reconstructs
the requested subject directly from its context-bound semantic type and
signatures, so omitting an unrequested or not-yet-created type cannot change a
future query result.

### Span-Addressed Coherence Failures

RFC 0005 `CoherenceModuleInput` contains frozen impl and marker facts but no AST
tree. A tree-local `NodeId` therefore cannot be required for cross-module
orphan or overlap diagnostics. This RFC replaces
`CoherenceCandidate.sourceFailures` and
`CoherenceBuildResult::SourceRejected.failures` with:

```text
CoherenceFailureRef {
  diagnostic: CheckerErrorId,
  primaryImpl: ImplId,
  relatedImpl: Maybe<ImplId>,
  primarySpan: SourceSpan,
  arguments: Sequence<CheckerDisplayArgument>,
  notes: Sequence<CheckerNoteRef>,
  producer: Coherence | Orphan,
}
```

The record's stage is intrinsically `Coherence`, recovery policy is
intrinsically `None`, and it has no `primaryNode`, schema-preorder ordinal,
recovery handle, or advisory form. It is an ephemeral verified result record,
not a module-interface or coherence-view field, so it has no persistence
codec. The renderer accepts it only from `CoherenceBuildResult::SourceRejected`
and anchors directly at the verified `SourceSpan`.

For ordinary overlap, `primaryImpl` is the later expanded `ImplKey`,
`relatedImpl` is the earlier key, `primarySpan` is the later
`ImplHead.declarationSpan`, and the one `ZOM4071` note uses the earlier head's
span. For explicit marker conflict, the same fields come from the later and
earlier `MarkerEvidence::Explicit.impl` values and their required present
declaration spans. Distinct conflict pairs are identified by expanded later and
earlier impl keys; no bounded pair index is encoded.

For ordinary impl orphan failure, `primaryImpl` is the rejected source impl,
`relatedImpl = none`, `primarySpan` is its verified impl-head declaration span,
there are no notes, producer is `Orphan`, and `ZOM4054` arguments are
reconstructed from the verified pattern, never from presentation text.
Explicit-marker orphan failures were already emitted by the final tree-local
signature admission substage and never enter this record. Cross-module marker
`ZOM4017` arguments are reconstructed from the verified marker key.

The internal deduplication identity is numeric diagnostic ID, expanded
`primaryImpl`, optional expanded `relatedImpl` with none first, and
`primarySpan`. Observable rendering preserves the RFC 0005 and RFC 0008 order:
expanded package key, expanded crate key, expanded module key, primary source
file and range, then numeric diagnostic ID. Those ownership keys are obtained
from the expanded `primaryImpl`; a missing or inconsistent ownership projection
is an invariant. Because `CoherenceFailureRef` intentionally has no
`CheckerEmitterOrdinal`, its deterministic final tie-break is expanded
`primaryImpl` followed by optional expanded `relatedImpl` with none first.
Wall-clock order, worker order, module input order, map seed, and discovery order
never participate. Invalid or
cross-context impl identities, absent explicit-marker conflict spans, wrong spans,
wrong producer, wrong related key, wrong note span, or wrong argument selects
`CoherenceBuildResult::InvariantRejected`. No verifier attempts
to recover a `NodeId` from source coordinates, and no module interface persists
one.

Every singleton ordinary survivor group requires exactly one entry under its
shared authority in `SortedMap<ImplId, ImplHead>`. An ordinary occurrence
removed by source classification or belonging to a survivor-conflict group
authorizes no entry. Every marker header that survives the complete final
tree-local admission sequence -- `ZOM4092`, marker `ZOM4054`, and local
same-key `ZOM4017` -- and is the unique remaining header for its key requires
exactly one entry under its shared authority in the independently filtered
`MarkerEvidence::Explicit` projection of
`SortedMap<MarkerFactKey, MarkerFact>`. The verifier compares that explicit
projection and the impl-head map with the independently reconstructed survivor
maps and then compares every field. A header rejected by any admission step
authorizes no entry, including when its key also occurred in a rejected
conflict group. A
`Structural` or `Builtin` record in this persisted
map is `InvalidFact`; those evidence classes exist only as proof-engine results.
A missing final-survivor key is `MissingRequiredFact`; a fact for a rejected
header or any other additional explicit key is `AdditionalFact`; a field
mutation is `InvalidFact` or
`CanonicalCodecMismatch` under the existing invariant table. Replacing both a
candidate key and its payload cannot evade the source-derived projection
comparison.

Each proof-engine call has exactly one total `MarkerProofResult`, so there is no
producer-selected candidate universe and no missing or additional query-fact
classification. For `Positive`, the proof verifier branches on the evidence
tag. An explicit-positive proof must be the exact same-key positive fact in
`FrozenCoherenceView.markerFacts`, with every field and canonical byte equal. A
builtin or structural proof is independently reconstructed from the requested
key and immutable input capabilities and compared field by field and
byte-for-byte. For a generic nominal proof the verifier independently rebuilds
the parameter-to-argument substitution, recursively canonicalizes every field
or payload type, and interns it through the same-store capability before
comparing the resulting component identity; the producer's `componentType` is
never an interning or substitution authority. For `Negative` the verifier
requires the exact same-key verified
explicit negative fact. For `Unsatisfied` it
independently repeats the ordered resolution and proves that no earlier branch
succeeds. A producer cannot turn an omitted structural proof into
`Unsatisfied`, because the verifier executes the same query from the requested
key rather than comparing an eagerly supplied map.

The signature producer and verifier may share closed codecs and the pure
semantic-type canonicalizer. They may not share candidate records, memoized
producer results, a mutable type-resolution table, or a callback that returns
the producer's selected interface, marker, or self type. Architecture negative
fixtures enforce this boundary.

### Impl-Head Consistency Verification

An impl head is independently verified after decoding and source-requirement
reconstruction, and before orphan or overlap processing:

1. Validate `impl`, `selfType`, every generic-parameter `DefId`, every identity
   nested in constraints and associated bindings, and `declarationSpan` against
   the candidate context and frozen registries.
2. Decode `pattern` as exactly one `ImplPatternKey`, require the v1 domain,
   reject trailing bytes, and reproduce byte-identical key bytes from the
   decoded value.
   If its outer self is `Tuple`, `Function`, `Union`, or `Intersection`, inspect
   the sequence count prefix and reject a value greater than `UINT32_MAX`
   before allocating or traversing children.
3. Require `pattern.interface.interface` to identify an interface definition.
   Resolve that interface's verified semantic signature and require the pattern
   argument count to equal its declared generic arity.
4. Require `genericParameters` to be unique, in impl declaration order, owned
   by this impl, and definition-kind `TypeParameter`. Validate every
   `Parameter(i)` against that sequence and require every index to occur in the
   complete impl pattern.
5. In both interface arguments and self, replace each `Parameter(i)` with
   `TypeParameter(genericParameters[i])`. Rebuild and canonicalize each complete
   semantic type through the candidate's `SemanticTypeStore`; every
   publication restriction above is checked before canonicalization.
6. From the canonicalized interface arguments and self type, replace the same
   impl-owned `TypeParameter(genericParameters[i])` nodes with `Parameter(i)`
   and rebuild the complete `ImplPatternKey`. Require its bytes to equal the
   decoded key exactly. Any change of tag, arity, member set, order,
   principal/additional relation, or nested structure is
   `CanonicalCodecMismatch` and publishes no coherence bucket entry.
7. Require the canonical reconstructed interface and self type to equal the
   exact independently reconstructed `VerifiedSourceImplHeader.interface` and
   `VerifiedSourceImplHeader.selfType` whose `authority` equals the same
   `ImplId` map key.
   Compare complete `TypeKeyNode` structure, not handle slots. Replacing
   `I<T>` with a same-arity `J<T>`, or replacing both `pattern.self` and
   `selfType` consistently, is `InvalidFact` and publishes no signature or
   coherence entry.
8. Require the canonical reconstructed self root `TypeKeyNode` to equal the
   complete root `TypeKeyNode` looked up from `selfType`. Recompute `head` only
   from the byte-identical canonical reconstructed self pattern and require
   exact equality. Require generic parameters, where constraints, safety,
   associated bindings, and span to equal that same source requirement under
   the unchanged RFC 0005 semantic rules.
9. Mark the source map entry matched. After all records are verified, require
   every singleton ordinary survivor group to be matched exactly once and every
   candidate entry to match exactly one survivor. Rejected occurrences and
   ordinary survivor-conflict groups must remain unmatched because they
   authorize no impl head.

For example, `selfType = Primitive(Bool)`,
`pattern.self = Primitive(I32)`, and `head = Primitive(I32)` is `InvalidFact`
even though the pattern and head agree. No imported interface or coherence
candidate may bypass this reconstruction.

The coherence index and unifier consume only the byte-identical reconstructed
pattern admitted by steps 6 through 9. For example, an encoded interface argument
`Union([Never, Bool])` is rejected even when it is sorted and decodable, because
semantic canonicalization reconstructs `Bool`. The same rule rejects a self
`Union([Never, Bool])` before it can enter a `Union(2)` head bucket.

A standalone nested pattern or interface argument keeps RFC 0011 `uint64`
sequence framing and is not narrowed merely because it appears inside an impl
pattern. The `uint32` admission rule applies only to the outer self count that
is copied into `CanonicalTypeHead`; independent decoder resource limits still
apply to every sequence.

The outer head mapping retains RFC 0005 tags and fields:

| Outer pattern | Required `CanonicalTypeHead` |
|---|---|
| `Parameter` | `Blanket` |
| `Primitive(kind)` | `Primitive(kind)` |
| `Tuple(elements)` | `Tuple(elements.size)` |
| `Object` | `Object` |
| `DynamicArray` | `DynamicArray` |
| `Slice` | `Slice` |
| `FixedArray` | `FixedArray` |
| `Function(value)` | `Function(value.parameters.size, value.raises.isSome)` |
| `Nominal(definition, _)` | `Nominal(definition)` |
| `Union(elements)` | `Union(elements.size)` |
| `Intersection(elements)` | `Intersection(elements.size)` |
| `Reference(mutability, _)` | `Reference(mutability)` |
| `RawPointer(mutability, _)` | `RawPointer(mutability)` |
| `Existential(value)` | `Existential(value.principal.definition)` |

`TypeParameter`, `InterfaceBound`, and `InterfaceSelf` cannot be a published
outer impl head. They are `InvalidFact` after successful decoding. Nested
`InterfaceBound` values retain RFC 0005 bound-position validity rules.

For every admitted published pattern, replacing parameters and applying the
RFC 0005 semantic-type canonicalizer preserves the outer head tag and every
head field. A self `Parameter` maps to `Blanket`; a `Blanket` candidate is
compared against every concrete self head for the same implemented interface
`DefId`. No other head bucket comparison is omitted.

### Pattern Unification

Overlap testing first requires equal implemented interface `DefId` values and
renames each complete impl pattern's parameters into disjoint `(side, index)`
spaces. In the shared substitution environment it unifies interface argument
patterns in declaration order, then unifies the self patterns. It applies
deterministic first-order unification to every compared child:

- a parameter may bind one complete pattern node;
- every repeated occurrence of that parameter must equal the first binding;
- the occurs check rejects a binding that would contain the same renamed
  parameter;
- concrete variants unify only when tags match, scalar and expanded identity
  fields match, sequence lengths match, and corresponding children unify in
  canonical order;
- rigid `TypeParameter` and `InterfaceSelf` nodes compare their exact expanded
  definitions when exercising the total codec, although published impl heads
  reject them before overlap insertion;
- `Parameter` indices must already be in range for their side; and
- substitutions are applied structurally and never by source spelling,
  semantic-store slot, or rendered type.

This positional rule is valid only after both published patterns pass the
construction and publication checks above. Parameters cannot occur in any
sort key whose bytes, uniqueness, or enclosing semantic-type normalization can
change after substitution. Object fields are ordered and made unique by field
name, existential markers by their rigid definition identities, and associated
bindings by their rigid associated-definition identities. Every remaining
admitted parameter-bearing collection is an ordered sequence. The unifier
therefore compares a substitution-stable structure rather than an associative,
commutative, idempotent union or intersection theory.

Before binding or comparison, the unifier dereferences each renamed variable
through the current substitution to a fixed point. A variable-to-variable
equation binds the lexicographically greater `(side, index)` to the smaller
one. The occurs check examines the complete fixed-point term. Repeated
variables compare against their dereferenced first binding. These rules make
the result independent of traversal and allocation order.

The overlap index is partitioned by
`ImplPattern.interface.interface` and the self `CanonicalTypeHead`. When a
concrete-head impl is inserted, it compares with the same concrete bucket and
the `Blanket` bucket. When a `Blanket` impl is inserted, it compares with every
bucket for that interface `DefId`. Ordinary impl heads are positive behavior
conformances. Candidates are sorted by expanded `ImplId` map key before
unification, and the stored head must repeat that exact key.
Because admitted non-blanket self heads are substitution-stable, different
concrete self heads are provably disjoint and require no other cross-head
search. A rejected pattern never enters a coherence bucket and publishes no
partial impl fact.

Where-constraints are validated but cannot prove two patterns disjoint. ZOM
still has no specialization. Conflict ordering compares expanded `ImplKey`,
then complete `ImplPatternKey` bytes. Explicit positive and negative marker
conflicts are handled only by the separate RFC 0005 `MarkerFactKey` coherence
rule.

The orphan test uses only the source-matched reconstructed complete pattern. It
is legal when `pattern.interface.interface` is owned by the current module, or
when the reconstructed self outer head is `Nominal(definition)` and that
definition is owned by the current module. Nested local arguments, constraints,
aliases, and presentation text confer no locality. This rule replaces RFC
0005's complete orphan paragraph that reads the removed `selfPattern` field.

RFC 0008's coherence index retains sorted, duplicate-free `ImplId` bucket
entries. Each bucket key resolves through the frozen `implHeads` map to exactly
one source-matched `ImplHead`. Candidate matching uses that head's complete
`ImplPattern`, not the removed RFC 0005 `ImplTypePattern`. A dangling,
duplicate, wrong-bucket, or wrong-record key is an invariant.

RFC 0014 impl-owner `Self::Item` lookup uses the exact source-matched singular
`ImplPattern.interface` instantiation and its verified transitive parent
closure. It does not read the removed `ImplHead.interface` field. Marker impls
have no body and never enter `ImplSelfOwner`, so they cannot require an absent
impl head to materialize contextual `Self`.

### Pattern Golden Vectors

The nominal and interface fixtures use already-canonical expanded definition
bytes `a1`, matching RFC 0014's semantic-type oracle convention.

| Input | Complete preimage hex | SHA-256 |
|---|---|---|
| `Parameter(0)` | `7a6f6d2e747970652d6b65792d7061747465726e2e7631001100000000` | `0c9d8c3a3d5ccff890dbed8dad7b7270cf580bee6a024e610107aa99dfb5a022` |
| `InterfaceSelf(a1)` | `7a6f6d2e747970652d6b65792d7061747465726e2e76310010a1` | `2386a3fdf91952907a4e8a486bca5763fda467aa4d4fd52bd50fe2c20ffc4fc5` |
| `Reference(Const, Parameter(0))` | `7a6f6d2e747970652d6b65792d7061747465726e2e7631000c011100000000` | `71cd89aa34ed4343c7f092700dba2b313f817348b542acbd33db33c4c880fdcd` |
| `Nominal(a1, [Parameter(0)])` | `7a6f6d2e747970652d6b65792d7061747465726e2e76310008a100000000000000011100000000` | `c4951246c3831b62b19f245d604801351fc2545a6abb4a0136a8551ee1963e18` |

Implementation tests must add non-empty object, function, existential,
interface-bound, union, and intersection fixtures. They must mutate every tag,
length, count, index, field order, identity, optional tag, sorted order, and
duplicate condition independently.

### Impl-Pattern Golden Vectors

These fixtures use already-canonical expanded definition bytes `a1` for the
interface and `b2` for the nominal self definition. The second fixture proves
that one parameter index is shared by interface arguments and self.

| Input | Complete preimage hex | SHA-256 |
|---|---|---|
| `ImplPattern(I<>, Parameter(0))` | `7a6f6d2e696d706c2d7061747465726e2e763100a100000000000000001100000000` | `f81b1b7dbbb67d1c9bb9209864d4cf36166ffdf2ce3e0b83fb4033d92dae4703` |
| `ImplPattern(I<Parameter(0)>, Nominal(b2, [Parameter(0)]))` | `7a6f6d2e696d706c2d7061747465726e2e763100a10000000000000001110000000008b200000000000000011100000000` | `89cd999fa5df60dc6bd7a811213b5c2f954fef780624ec82e0e94059a138107e` |

Implementation tests must independently mutate the interface definition,
interface argument count and order, shared parameter index, self tag, and all
framing bytes. Decoding a valid type-key pattern does not authorize it as an
impl pattern until the complete publication validator succeeds.

### Impl-Head Golden Vector

The standalone impl-head test envelope is:

```text
ASCII("zom.impl-head.v1")
0x00
Encode(ImplHead)
```

It is a test envelope, not an additional field inside signature or module
records. The fixture uses expanded impl bytes `a1`, the second impl-pattern
vector above, self `Nominal(b2, [TypeParameter(c3)])`, head `Nominal(b2)`, one
generic parameter `c3`, empty where-constraints and associated bindings,
`Safe`, and expanded declaration-span bytes `d4`. The exact 99-byte
`ImplHead` record is:

```text
a100000000000000317a6f6d2e696d706c2d7061747465726e2e763100a10000000000000001110000000008b20000000000000001110000000008b2000000000000000109c309b20000000000000001c30000000000000000010000000000000000d4
```

The complete 116-byte envelope preimage is:

```text
7a6f6d2e696d706c2d686561642e763100a100000000000000317a6f6d2e696d706c2d7061747465726e2e763100a10000000000000001110000000008b20000000000000001110000000008b2000000000000000109c309b20000000000000001c30000000000000000010000000000000000d4
```

Its SHA-256 is
`9b223c2528e292b03af2fabc463afb61a281b62ff40f82944376b421b916064e`.
Mutation tests move `pattern` before and after every adjacent field, encode it
inline, omit it, replace its framing length, mismatch `selfType`, alter generic
ownership or arity, and require the exact codec or fact invariant.

### Revision Cutover

The exact type-key and impl-pattern codecs change every revision that directly
embeds `ImplHead`. `SemanticTypeKey` remains v1.

`SignatureFactsRevision` replaces v0 with the exact RFC 0005 field order and
framing under:

```text
ASCII("zom.signature-facts-revision.v1")
0x00
SemanticContextFingerprint
EncodeByteString(expanded owning ModuleKey)
sourceContentDigest
bindingSurfaceRevision
MarkerPolicyRegistryRevision
... remaining RFC 0005 record groups with v1 ImplHead encoding ...
```

The RFC 0005 one-signature fixture contains no impl head. It uses 32 policy
registry revision bytes of `77`; its complete 204-byte preimage is:

```text
7a6f6d2e7369676e61747572652d66616374732d7265766973696f6e2e76310000000000000000000000000000000000000000000000000000000000000000000000000000000001a122222222222222222222222222222222222222222222222222222222222222223333333333333333333333333333333333333333333333333333333333333333777777777777777777777777777777777777777777777777777777777777777700000000000000010000000000000003b2010300000000000000000000000000000000
```

Its SHA-256 is
`dac5b3c2ce95be20cf3c42028d5a05a042c504dc8d37a4d45f7ec97b7955b4a4`.

The non-empty impl-head integration fixture uses zero context fingerprint,
expanded module bytes `a1`, source digest bytes `22`, binding-surface revision
bytes `33`, no signature records, the complete 99-byte impl-head record above,
and no marker records. Its complete 300-byte preimage is:

```text
7a6f6d2e7369676e61747572652d66616374732d7265766973696f6e2e76310000000000000000000000000000000000000000000000000000000000000000000000000000000001a1222222222222222222222222222222222222222222222222222222222222222233333333333333333333333333333333333333333333333333333333333333337777777777777777777777777777777777777777777777777777777777777777000000000000000000000000000000010000000000000063a100000000000000317a6f6d2e696d706c2d7061747465726e2e763100a10000000000000001110000000008b20000000000000001110000000008b2000000000000000109c309b20000000000000001c30000000000000000010000000000000000d40000000000000000
```

Its SHA-256 is
`9c61c46865ef039be32944a276c95c6b30858429f177d6421ba9ef6a15aa018c`.

The explicit-marker integration fixture uses the same context, module, source
digest, and binding-surface bytes, no signature or impl-head records, and one
marker record. The record expands marker `a1`, subject `Primitive(I32)` as
`0103`, `Positive`, `Explicit` evidence with impl `c3`, and present declaration
span `d4`; its exact bytes are `a101030101c301d4`. The complete 201-byte
preimage is:

```text
7a6f6d2e7369676e61747572652d66616374732d7265766973696f6e2e7631000000000000000000000000000000000000000000000000000000000000000000a12222222222222222222222222222222222222222222222222222222222222222333333333333333333333333333333333333333333333333333333333333333377777777777777777777777777777777777777777777777777777777777777770000000000000000000000000000000000000000000000010000000000000008a101030101c301d4
```

Its SHA-256 is
`44a3bcf4d52bd7d78375815a323ae8d6faa07240e4c028e2be745c0c3722e214`.
The source-provenance oracle pairs this candidate with one exact positive
`unsafe` bodyless marker declaration and requires mutation tests for the marker node,
subject node, polarity token, evidence `ImplId`, and declaration span.

`CoherenceViewRevision` replaces v0 with the exact RFC 0005 field order and
framing under `ASCII("zom.coherence-view.v1")`, NUL. It encodes
`SemanticContextFingerprint`, `MarkerPolicyRegistryRevision`, then the remaining
RFC 0005 fields. The independent fixture uses 32 policy revision bytes of `77`
and is 137 bytes:

```text
7a6f6d2e636f686572656e63652d766965772e7631000000000000000000000000000000000000000000000000000000000000000000777777777777777777777777777777777777777777777777777777777777777700000000000000010000000000000001c300000000000000010000000000000001d400000000000000010000000000000001e5
```

Its SHA-256 is
`f14947d682f1992fc62eb0f4688265013138a8749015202e2fbe46817a2d2975`.

The coherence integration fixture uses zero context fingerprint, one
already-canonical module-interface revision-entry record `c3`, the complete
99-byte impl-head record, and no marker facts. Its complete 226-byte preimage
is:

```text
7a6f6d2e636f686572656e63652d766965772e7631000000000000000000000000000000000000000000000000000000000000000000777777777777777777777777777777777777777777777777777777777777777700000000000000010000000000000001c300000000000000010000000000000063a100000000000000317a6f6d2e696d706c2d7061747465726e2e763100a10000000000000001110000000008b20000000000000001110000000008b2000000000000000109c309b20000000000000001c30000000000000000010000000000000000d40000000000000000
```

Its SHA-256 is
`f71392f504dd043ee1fd476fec8ae1885fe122b1563d91f2c99e27877256cea3`.

The combined module interface replaces v2 with v3. It inherits RFC 0013 v1's
complete field order and RFC 0014's semantic-type v1 expansion, uses the new
signature and impl-head codec, and changes the root domain to
`ASCII("zom.module-interface-revision.v3")`. It adds
`MarkerPolicyRegistryRevision` immediately after the signature-facts revision. The
independent fixture uses 32 policy revision bytes of `77` and is this complete
314-byte preimage:

```text
7a6f6d2e6d6f64756c652d696e746572666163652d7265766973696f6e2e7633000000000000000000000000000000000000000000000000000000000000000000a12222222222222222222222222222222222222222222222222222222222222222333333333333333333333333333333333333333333333333333333333333333344444444444444444444444444444444444444444444444444444444444444447777777777777777777777777777777777777777777777777777777777777777555555555555555555555555555555555555555555555555555555555555555566666666666666666666666666666666666666666666666666666666666666660000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
```

Its SHA-256 is
`177d6def235eaa6d88c17aef910af44fb2dded53e83559ae5741c232bbfd9f7e`.

RFC 0014's 309-byte `InterfaceSelf(a1)` module-interface v2 vector is replaced,
not retained. The v3 vector inserts the same policy revision bytes after the
signature revision and has this complete 341-byte preimage:

```text
7a6f6d2e6d6f64756c652d696e746572666163652d7265766973696f6e2e7633000000000000000000000000000000000000000000000000000000000000000000a1222222222222222222222222222222222222222222222222222222222222222233333333333333333333333333333333333333333333333333333333333333334444444444444444444444444444444444444444444444444444444444444444777777777777777777777777777777777777777777777777777777777777777755555555555555555555555555555555555555555555555555555555555555556666666666666666666666666666666666666666666666666666666666666666000000000000000000000000000000017a6f6d2e73656d616e7469632d747970652d6b65792e76310010a100000000000000000000000000000000000000000000000000000000000000000000000000000000
```

Its SHA-256 is
`1bf456d1f0b1b5a29f1d918b08a0f10fa78b3170e4592f6db0a570fb72466347`.

The module-interface integration fixture uses the same component bytes as the
empty fixture, leaves roots, definitions, support definitions, visible
bindings, and exported bindings empty, inserts the complete 99-byte impl-head
record into `coherence_impl_heads`, and leaves marker facts empty. Its complete
421-byte preimage is:

```text
7a6f6d2e6d6f64756c652d696e746572666163652d7265766973696f6e2e7633000000000000000000000000000000000000000000000000000000000000000000a12222222222222222222222222222222222222222222222222222222222222222333333333333333333333333333333333333333333333333333333333333333344444444444444444444444444444444444444444444444444444444444444447777777777777777777777777777777777777777777777777777777777777777555555555555555555555555555555555555555555555555555555555555555566666666666666666666666666666666666666666666666666666666666666660000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010000000000000063a100000000000000317a6f6d2e696d706c2d7061747465726e2e763100a10000000000000001110000000008b20000000000000001110000000008b2000000000000000109c309b20000000000000001c30000000000000000010000000000000000d40000000000000000
```

Its SHA-256 is
`f13c5bb0cc30c9a82963ccd09f90f8b3e45813322a03db8202d14134cc987ef4`.

The end-to-end lineage oracle replaces synthetic policy bytes with the actual
registry revision above. Its signature preimage is 204 bytes and hashes to
`c665ba5bf2be40edc425a063842bee3ce7a6774efbbe64acb8d3824ecf4ec85b`:

```text
7a6f6d2e7369676e61747572652d66616374732d7265766973696f6e2e76310000000000000000000000000000000000000000000000000000000000000000000000000000000001a12222222222222222222222222222222222222222222222222222222222222222333333333333333333333333333333333333333333333333333333333333333315329853e2faae147a2f5ca73c85a58c4084c70faa3d1faef278c856fd75067b00000000000000010000000000000003b2010300000000000000000000000000000000
```

The module preimage replaces its signature and policy parents with that
signature revision and the actual registry revision. It is 314 bytes and hashes
to `701f41323c3e469b94012bfb98191c9b2b68bdd7be4f52697d2178227c37dd9f`:

```text
7a6f6d2e6d6f64756c652d696e746572666163652d7265766973696f6e2e7633000000000000000000000000000000000000000000000000000000000000000000a122222222222222222222222222222222222222222222222222222222222222223333333333333333333333333333333333333333333333333333333333333333c665ba5bf2be40edc425a063842bee3ce7a6774efbbe64acb8d3824ecf4ec85b15329853e2faae147a2f5ca73c85a58c4084c70faa3d1faef278c856fd75067b555555555555555555555555555555555555555555555555555555555555555566666666666666666666666666666666666666666666666666666666666666660000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
```

The coherence preimage uses one exact module-interface revision entry whose
expanded module key is `a1` and whose revision is that module hash. Impl and
marker maps are empty. It is 151 bytes and hashes to
`7a76db8d220726a014782b28ced02e9bd5ea64724829798e3e83c6679858d1f5`:

```text
7a6f6d2e636f686572656e63652d766965772e763100000000000000000000000000000000000000000000000000000000000000000015329853e2faae147a2f5ca73c85a58c4084c70faa3d1faef278c856fd75067b00000000000000010000000000000021a1701f41323c3e469b94012bfb98191c9b2b68bdd7be4f52697d2178227c37dd9f00000000000000000000000000000000
```

Mutating the configuration, shape inventory, registry, signature, module, or
coherence parent independently must reject the first direct consumer before
marker evidence, orphan, overlap, or diagnostics are processed.

Revisions that contain `SignatureFactsRevision`, `CoherenceViewRevision`, or
`ModuleInterfaceRevision` only as child digests do not change their own domain:
the domain-separated child digest already changes. A revision that directly
embeds `ImplHead` must use a new domain or be returned during owner review.

### Failure Precedence

Decoding and verification use the existing RFC 0005 and RFC 0008 invariant
families. The first applicable class wins:

1. an invalid RFC 0011 identity, context, handle, or source range uses the
   exact RFC 0011 invariant;
2. a wrong input receipt, marker-shape inventory revision, marker-policy
   registry revision, or other parent revision is `InputReceiptMismatch` in
   the checker or proof engine, `InputMismatch` in module-interface
   verification, or the corresponding coherence input invariant; this row
   precedes evidence, orphan, overlap, query resolution, and diagnostic
   processing;
3. an unknown tag, malformed field, non-canonical order or normalized form,
   duplicate canonical member, canonical round-trip mismatch, outer self arity
   above `uint32` in an encoded impl head, out-of-range encoded integer,
   trailing byte, or hash mismatch is `CanonicalCodecMismatch`;
4. a well-formed but unauthorized node, forbidden `InterfaceSelf`, unmatched or
   unconstrained type parameter, normalization-sensitive parameter position,
   in-memory producer outer self arity above `uint32`, inconsistent
   interface/self pattern, wrong derived head, wrong
   diagnostic/operator pairing, invalid parameter ownership, non-closed marker
   subject, source-form/interface-kind mismatch, or a returned builtin or
   structural proof that does not exactly reconstruct is `InvalidFact` in the
   checker or proof engine or
   `InvalidProjection` at the module-interface boundary;
5. a missing required record is `MissingRequiredFact` or
   `MissingProjection`; and
6. an additional or duplicate record is `AdditionalFact` or
   `AdditionalProjection`.

No rejected branch publishes a partial signature, coherence view, diagnostic
set, or module interface.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| RFC overlay, review, and accepted-base tracking | `docs/rfc/**` | `rfc` |
| Singular ordinary impl and bodyless marker grammar, AST shape, and parser diagnostics | `docs/spec/ZomParser.g4`, `products/zomlang/compiler/lexer/**`, `products/zomlang/compiler/parser/**`, `products/zomlang/compiler/ast/**` | `lexer-parser` |
| Impl binding facts, pattern algebra, impl heads, overlap, signature revisions, and diagnostic fact verification | `products/zomlang/compiler/binder/**`, `products/zomlang/compiler/checker/**`, `products/zomlang/compiler/type/**` | `binder-checker` |
| Module-interface v3 publication and consumption | `products/zomlang/compiler/driver/**` | `module-system` |
| Closed operator arguments and invariant diagnostics | `products/zomlang/compiler/diagnostics/**` | `error-system` |
| Cross-RFC and specification consistency evidence | `docs/design/**`, `docs/spec/**`, `docs/reports/**` | `spec-audit` |
| Codec, negative, determinism, architecture, and repository gates | `products/zomlang/tests/**`, `scripts/**` | `verification` |

No runtime, HIR, MIR, LIR, or backend contract changes.

## Security And Safety Impact

The proposal does not change runtime memory or concurrency semantics. It
reduces compiler integrity risk by rejecting malformed canonical bytes,
cross-context identities, unmatched generic parameters, non-canonical orders,
and forged operator display payloads before publication. Structured operator
rendering prevents untrusted source text from entering diagnostic format
strings. Existing RFC 0005 escaping and truncation remain mandatory.

Explicit positive marker evidence is caller-proven and requires the visible
`unsafe` source acknowledgement because the shared marker algebra does not yet
classify which markers can affect memory, ownership, or concurrency safety.
Negative evidence cannot grant a capability and rejects `unsafe`.
Compiler-proven structural and builtin query results retain independent
reconstruction and policy-registry verification. Only compiler-distribution configuration reached
through verified prelude provenance can authorize implicit evidence; package
content cannot register policy or redirect an entry by spelling. The single
marker-shape inventory prevents producer/verifier classifier drift. Function,
raw-pointer, aliased-container, class, and recursive-cycle cases fail closed
until their proof data is represented. A future safe explicit marker form requires a
separate accepted classifier and proof rule; it cannot weaken this default.

## Drawbacks And Risks

- Marker-shape, policy-configuration, policy-registry, signature, coherence,
  and module-interface revision domains must cut over atomically before the RFC
  0005 direct replacement can publish production artifacts.
- The recursive pattern codec and unifier require exhaustive negative tests;
  an omitted child field would make cross-module coherence unsound.
- Interface arguments and self type use one shared pattern and substitution
  space. This enlarges the impl-head codec and its verification surface.
- Impl parameters are not admitted inside union or intersection members or
  existential additional-interface arguments. This keeps coherence exact for
  admitted patterns but intentionally excludes generic impl heads whose
  substitutions can change normalized set structure.
- A closed operator renderer duplicates source spellings already present in
  lexical and grammar artifacts. A generated or exhaustive alignment gate is
  required to prevent drift.
- Bound proposal hashes can become stale while another overlay changes a base.
  Any mismatch requires a new exact proposal snapshot and fresh owner review
  rather than a silent patch.

## Alternatives Considered

### Keep the prose mirror

A prose rule cannot assign stable tags, prove recursive field coverage, define
versioning, or force `InterfaceSelf` handling. It is insufficient for a
canonical codec.

### Derive tags from AST enums

AST enums are syntax representation details and may have different ordering,
aliases, or recovery cases. Numeric casts would couple canonical semantic bytes
to parser implementation layout.

### Store operator source strings

Strings are not semantic identities and can retain invalid spelling, Unicode,
or source-buffer lifetime. A closed semantic enum plus a total rendering table
is smaller and independently verifiable.

### Reuse `TypeKeyNode::TypeParameter` for impl variables

A definition-keyed semantic parameter and a locally quantified unification
variable have different equality rules. Reusing one tag would make alpha
renaming and repeated-variable binding ambiguous.

### Unify parameterized unions and intersections modulo normalization

Complete overlap for normalization-sensitive parameters requires
associative, commutative, idempotent unification with identity and absorber
elements, substitution-time flattening and deduplication, and cross-head
candidate enumeration. That algorithm is substantially broader than RFC
0005's first-order coherence contract and has a larger complexity and
verification surface. This RFC instead rejects those parameter positions at
publication and retains exact first-order unification for the admitted
language.

### Unify only the self pattern

Self-only unification misses a generic implemented-interface argument against
a concrete argument. Grouping by complete interface instantiation avoids that
miss but separates patterns that become equal after substitution; grouping by
interface definition and ignoring arguments rejects disjoint instantiations.
The complete interface-and-self `ImplPattern` is the smallest exact coherence
input.

### Publish several behavior conformances from one impl declaration

Swift source syntax can declare several protocol conformances together, but
the compiler still gives each conformance its own witness and substitution
identity. ZOM's accepted algebra gives `ImplResolution`, witness arguments,
associated projections, dyn evidence, and dispatch one `ImplId`, with no
conformance-clause identity. A list of several behavior interfaces would make
those consumers ambiguous. Introducing a second clause identity and migrating
every consumer would be a separate end-to-end design. The current contract
instead follows the Rust implementation model: one behavior interface per
impl identity.

### Encode marker clauses in an ordinary impl header

RFC 0005 marker facts are keyed by one concrete `(marker, subject)` pair and do
not carry generic parameters, where constraints, or a selectable impl pattern.
Attaching marker clauses to a generic ordinary impl would publish evidence that
the marker selector cannot soundly instantiate. A distinct closed bodyless
marker declaration keeps the admitted model complete. Generic marker impls
require a later pattern-based marker coherence RFC.

### Edit RFC 0005 and RFC 0014 in place

Both proposals have advanced beyond draft design. Silent edits would invalidate
hash-bound approvals and hide a material codec change from owners. The additive
overlay preserves the governance record.

## Compatibility And Rollout

This is a direct replacement with no compatibility path:

1. approve RFC 0015 against the exact bound hashes;
2. append its accepted proposal hash to affected base tracking records;
3. implement `OperatorKind`, the exhaustive symbolic mapping, rendering, and
   verifier before producing structured checker diagnostics;
4. implement `TypeKeyPatternKey`, `ImplPatternKey`, construction, head
   derivation, unification, and independent verification before producing any
   impl head;
5. replace every signature producer and consumer with v1;
6. replace every coherence producer and consumer with v1;
7. replace every combined module-interface producer and consumer with v3; and
8. delete v0/v2 constructors, decoders, test helpers, fixtures, and cached
   artifacts in the same cutover.

There is no dual schema, compatibility decoder, feature flag, adapter, or
fallback. Rollback before landing reverts the complete implementation series.
Rollback after publication requires another accepted overlay and new domains.

## Documentation And Teaching Plan

- Record the accepted overlay hash and implementation state in the affected
  RFC tracking documents.
- Update checker architecture documentation with the direct pattern and
  operator codec boundaries.
- Keep language operator spelling and overload documentation aligned without
  adding migration or historical prose to the normative specification.
- Document how to regenerate byte vectors and how to add a semantic type or
  operator without reusing tags.

## Operational Readiness

CI must run exhaustive codec, oracle, negative, determinism, and architecture
gates. Cache and interface artifact version mismatches must fail closed and
identify the invariant family without rendering unvalidated payloads. Release
artifacts cannot contain signature v0, coherence v0, or combined module
interface v2 after the cutover. They also cannot synthesize a marker shape or
policy registry after signature publication or accept an unbound policy
revision from a package.

## Acceptance Criteria

1. All seven bound proposal hashes match the exact proposal reviewed by every
   required owner.
2. `OperatorKind` has the exact outer and inner tags, spelling table, symbolic
   mapping, validation subsets, and independent byte vectors in this RFC. The
   lexical chapter, expressions chapter, error-handling chapter, lexer grammar,
   grammar reference, hand parser, generated AST, and checker mapping pass one
   exhaustive spelling and precedence alignment gate.
3. `TypeKeyPattern` covers all sixteen combined semantic variants plus
   `Parameter = 0x11`, with complete recursive child records and field order.
4. `ImplPattern` contains the implemented interface arguments and self pattern
   in one parameter space and reproduces both independent golden vectors;
   `ImplHead` has the exact complete field order above and reproduces its
   standalone envelope and three non-empty impl-head integration vectors. The
   explicit `MarkerFact` independently reproduces its 201-byte integration and
   source-provenance oracle.
5. Pattern construction replaces only verified impl-owned generic parameters,
   rejects unconstrained parameters and unresolved `InterfaceSelf`, re-sorts
   after replacement, and rejects parameters in every
   normalization-sensitive position. Independent verification reproduces the
   complete pattern byte-for-byte after inverse substitution and semantic
   canonicalization and rejects an unrepresentable outer self arity without
   narrowing.
6. Head derivation and first-order unification are total over admitted
   published patterns, unify interface arguments before self, compare
   `Blanket` against every concrete self head for the interface `DefId`, and use
   no spelling, AST ID, store slot, or hash iteration order.
7. The verifier independently reconstructs every occurrence's singular
   interface and self type from the exact verified bound tree, proves exact
   bijections from unique survivor groups to published impl-head and marker
   candidates,
   and rejects a substituted interface, jointly substituted self pattern and
   type, wrong generic list, wrong constraints, wrong safety, wrong associated
   bindings, or wrong span.
8. Every implementation source occurrence is independently reconstructed and
   classified. A singleton ordinary survivor group publishes exactly one
   positive impl head under its shared `ImplId`; an ordinary survivor-conflict
   group publishes none. A unique closed bodyless marker survivor publishes
   exactly one explicit marker fact under its shared authority only after
   source-form, per-module orphan, and pre-map conflict validation;
   positive evidence requires `unsafe`, negative evidence forbids it. The
   marker retains its own exact empty RFC 0004 occurrence-owned impl scope but
   publishes no impl head or contextual `Self`. Mixed ordinary and bodyless
   identity groups are classified by each source form rather than source order;
   a valid survivor retains its own provenance even when the authority site
   fails. RFC 0005 orphan checking,
   RFC 0008 coherence indexing, and RFC 0014 impl-owner projection consume only
   source-matched reconstructed values; no removed field name, multi-head
   collection, or private clause index remains in a production consumer.
9. Marker shape comes only from one verified inventory; structural subjects,
   builtin primitives, and reference propagation come only from one
   context-bound, revisioned, prelude-authorized policy registry. The 80-byte
   inventory, 81-byte configuration, 172-byte registry, standalone policy and
   component vectors, 341-byte `InterfaceSelf` replacement, and end-to-end
   revision chain reproduce exactly. The same policy revision is a direct
   parent of signature, module-interface, and coherence revisions. Persisted
   marker maps contain explicit evidence only. One demand-driven proof engine
   resolves any requested semantic type with exact explicit, builtin, then
   structural precedence, independently verifies its result, and rejects
   cycles without requiring a finite candidate universe. Generic nominal
   components use verified parameter-to-argument substitution and same-store
   linearizable canonical interning, so proof does not depend on prior type
   insertion history.
10. Cross-module marker `ZOM4017` and ordinary impl `ZOM4054` failures use
   verified impl identities and declaration spans through
   `CoherenceFailureRef`; they never require or persist a tree-local `NodeId`,
   and the same failure order is reproduced when imported ASTs are unavailable.
   Explicit-marker `ZOM4054` remains in the final tree-local signature
   admission substage.
11. Signature v1, coherence v1, and module-interface v3 reproduce the exact
   preimage lengths and SHA-256 values in this RFC.
12. Every replaced constructor and decoder is absent after implementation; repository
   search proves no dual revision path remains.
13. Generated or exhaustive gates fail when a semantic type or operator is
   added without updating its canonical closure.
14. Owner review records exact snapshot hashes. No approval is inferred from
   authorship, implementation activity, or a passing local test.
15. `python3 scripts/check-rfc.py`, authoritative-link validation, English-only
    validation, `python3 scripts/check-format.py`, relevant CTest presets, and
    `git diff --check` pass before `LANDED`.

## Implementation Plan

1. Obtain hash-bound approval from `rfc`, `lexer-parser`, `binder-checker`,
   `module-system`, `error-system`, `spec-audit`, and `verification`.
2. Cut ordinary impl syntax and AST to one interface, cut marker impl syntax to
   a closed bodyless form, remove the redundant list count, and add exact parser
   diagnostics and conformance fixtures.
3. Add the closed operator algebra, total symbolic conversion, renderer, and
   independent verifier tests.
4. Add the closed type-key and impl-pattern algebras, key codecs, construction
   validator, head derivation, and unifier with negative mutation tests.
5. Add the single marker-shape inventory plus immutable marker-policy
   configuration and registry, bind their lineage into every direct consumer,
   retain only explicit facts in persisted maps, and add the demand-driven
   builtin and structural proof engine with reference propagation.
6. Cut signature facts and coherence views to v1 atomically.
7. Cut the combined module interface to v3 atomically.
8. Remove all replaced constructors, decoders, fixtures, and cached
   artifacts.
9. Run the full sanitizer, lit, unit, determinism, format, RFC, link, and
   architecture gates and attach evidence to the tracker.

## Test Plan

- Build: `cmake --preset sanitizer` and `cmake --build --preset sanitizer`.
- Unit tests: every operator tag/spelling, every pattern variant, repeated
  variables, occurs check, head derivation, alpha renaming, out-of-range
  indices, forbidden `InterfaceSelf`, every byte mutation, and rejection of
  parameters recursively inside union members, intersection members, and
  existential additional-interface arguments or a same-definition principal
  interface.
- Operator-alignment tests: compare the complete operator and punctuation
  tables in `docs/spec/chapters/02-lexical-structure.md`, precedence and
  overload spellings in `docs/spec/chapters/04-expressions.md`,
  error postfix spellings and `PostfixSuffix` rules in
  `docs/spec/chapters/11-error-handling.md`,
  `docs/spec/chapters/17-grammar-reference.md`, every matching token rule in
  `docs/spec/ZomLexer.g4`, the corresponding `docs/spec/ZomParser.g4` rules,
  hand-lexer and hand-parser token mapping, generated AST operator variants,
  and the exhaustive `OperatorKind` conversion and renderer. A missing, extra,
  differently spelled, or differently associated operator at any layer fails
  the gate before lit tests.
- Impl-head tests: every field-order mutation, inline-versus-framed pattern,
  interface kind and arity mismatch, foreign or reordered generic parameter,
  unused generic parameter, self reconstruction mismatch, head mismatch, and
  non-empty signature, coherence, and module-interface integration oracle.
- Source-provenance tests: replace the exact source interface with another
  same-arity interface, replace both the self pattern and `selfType`
  consistently, swap interfaces between two impls, mutate generic
  parameters, constraints, safety, associated bindings, or span, and
  require invariant rejection before signature or coherence publication.
- Source-shape tests: reject the first `+` in an ordinary impl, generic or
  conditional bodyless marker impls, a marker-only interface with an impl body,
  a behavior interface with a marker semicolon, a safe positive marker
  assertion, an unsafe negative assertion, and a negative marker body. Remove
  `ImplIfaceList.n_ifaces` and test
  `NodeList.size` with more than 255 heritage bounds without narrowing. Test
  every pair and the all-invalid combination of negative polarity, `unsafe`,
  marker type parameters, marker where clauses, and a body and require only the
  earliest-source parser diagnostic.
- Marker-interface tests: reject a generic marker-shaped interface with
  `ZOM4090` at its first generic parameter and prove it publishes neither an
  interface signature nor a marker identity; accept a zero-arity marker-shaped
  interface; accept a generic directly empty interface whose verified parent
  closure contains behavior; and reject a generic interface inheriting only
  marker-shaped parents.
- Marker-policy tests: reproduce the inventory, configuration, registry,
  standalone policy/component, `InterfaceSelf`, and end-to-end lineage oracles;
  prove complete one-to-one interface inventory coverage and a single shape
  classifier; reject name-based resolution, package policy injection,
  non-prelude or behavior interfaces, mixed revisions, duplicate policy
  subjects, and structural proof under an empty policy. Query primitive,
  tuple, object, fixed-array, nominal-struct, nominal-enum, and reference
  subjects directly; independently reconstruct every complete component path;
  and reject a wrong outer form, incomplete component sequence, wrong enum
  variant or payload index, wrong reference mutability or required marker, and
  structural or builtin negative proof. Prove that dynamic arrays, slices,
  classes, functions, raw pointers, unavailable supports, and unseeded cycles
  are `Unsatisfied`. Clear memoization, reverse configuration and signature
  order, and require byte-identical results. Mutate each lineage parent
  independently and require `InvariantRejected` before evidence lookup.
- Marker-query-universe tests: freeze coherence with explicit facts only, then
  intern and query a tuple type that never appeared in any module signature;
  require the same structural result as an equivalent earlier-interned type.
  Query arbitrary valid semantic type IDs without pre-registration. Prove there
  is no candidate enumeration, missing-candidate classification, or mutation of
  signature, module-interface, coherence-input, or frozen-coherence maps.
- Marker-generic-component tests: query `Box<I32>` when its verified field type
  is `Vec<T>` and `Vec<I32>` has never been interned. Require exact nominal
  parameter ownership and arity validation, recursive substitution,
  normalization, one linearizable component identity, and a positive proof.
  Repeat with nested generic fields and enum payloads, reverse query and worker
  order, run with workers `1, 2, 4, 8`, clear the memo, and require
  byte-identical proofs. Reject an unowned parameter, wrong argument mapping,
  producer-selected component ID, foreign interner, and failed canonical
  interning; never cache those invariant results or classify a fresh component
  as `Unsatisfied`.
- Marker-proof concurrency tests: use barriers to make two workers query the
  same positive key concurrently and to make two root queries traverse one
  shared dependency in opposite schedules. Require independent computation or
  completed-result reuse, never a false cycle. Separately query real self and
  mutual cycles and require `Unsatisfied`. Repeat across worker counts, query
  orders, and cleared shared memo state; results and canonical proof bytes must
  remain identical. Run producer and verifier simultaneously with distinct
  active stacks and memo storage and prove neither observes the other's
  in-flight state.
- Implementation-occurrence tests: two and three byte-identical headers share
  one `ImplKey` and `ImplId` authority while every source node has a distinct
  `ImplOccurrenceId`, complete occurrence key, binding fact, impl-body scope,
  node, source, and verified join. Reject missing, additional, reused, swapped,
  cross-node, cross-site, cross-module, wrong-authority, or inconsistently
  expanded rows. Require ordinary and marker survivor-conflict groups to emit
  one `ZOM4017` per later occurrence with one `ZOM4071` at the first survivor
  and no semantic publication. Test ordinary and bodyless forms with one equal identity record
  in both source orders against behavior and marker-only interfaces. Require the
  invalid form's `ZOM4088` or `ZOM4089`, the valid occurrence's sole publication
  under the shared authority with its own provenance, and rejection of
  first-form routing or dual impl-head and marker publication.
- Marker-provenance tests: mutate marker identity, closed subject, polarity,
  explicit evidence `ImplId`, or span independently; replace both key
  and payload together; require source-derived map rejection and prove that no
  marker source publishes an impl head or contextual `Self`. Exercise local
  marker, local outer nominal, foreign/foreign, aliased outer nominal,
  primitive, aggregate, and nested-local marker orphan cases in both
  polarities. Require the final tree-local signature admission substage to own
  every explicit-marker orphan decision, reject orphan candidates before
  same-key conflict grouping, construct the marker map only from surviving
  unique facts, publish only that map through frozen module interfaces, and
  prove that structural and builtin facts never undergo an orphan test. Omit
  one final survivor and require `MissingRequiredFact`; publish a fact for a
  `ZOM4092`, marker-orphan, or local-conflict rejection and require
  `AdditionalFact`, including a whole rejected same-key group. Replace both a
  survivor key and payload and require rejection against the independently
  reconstructed survivor map.
- Marker-conflict tests: reject local positive/positive, negative/negative, and
  positive/negative same-key survivor groups before map construction with one
  `ZOM4017` per later complete occurrence key and one `ZOM4071` at the first
  survivor; reject the same three cross-module collisions in
  both module and import orders before `CoherenceCandidate.markerFacts` exists.
  Reject every persisted `Structural` or `Builtin` record as `InvalidFact` or
  `InvalidProjection`. For a key eligible for structural proof, require an
  explicit positive fact to win and an explicit negative fact to return
  `Negative`; for an authorized primitive require `ZOM4092` before explicit
  publication and builtin proof only when no explicit fact exists. Mutate an
  explicit-positive result's key, polarity, payload, span, or canonical bytes,
  and mutate an ephemeral builtin or structural proof's key, outer shape,
  component, enum, reference, support, or index. Require proof-verifier
  rejection without mutating the frozen explicit map.
- Marker-component-width tests: use tuple and enum-payload sequences with
  ordinals `0xffffffff` and `0x0000000100000000`; admit the former query, return
  `Unsatisfied` for the latter, and prove there is no narrowing, partial path
  sequence, projection, persisted fact, or memoized positive result.
- Coherence-diagnostic tests: reproduce marker `ZOM4017` and ordinary impl
  `ZOM4054` from frozen module interfaces after all imported ASTs and `NodeId`
  values are unavailable; reproduce explicit-marker `ZOM4054` only in the
  final tree-local signature admission substage. Mutate primary and related impl identities,
  spans, notes, arguments, and producers independently; require
  `InvariantRejected`; and prove no `NodeId` enters an interface, view, cache,
  or coherence failure. Permute package, crate, module, source, diagnostic, and
  impl-key ties and require the observable base ordering plus the exact
  `CoherenceFailureRef` replacement tie-break.
- Canonical round-trip tests: in both interface arguments and self, reject union
  `Never` elimination, `Any` absorption, singleton reduction, nested flattening,
  and deduplication; reject the corresponding intersection `Any` elimination,
  `Never` absorption, reduction, flattening, and deduplication; and prove no
  rejected key enters a coherence bucket.
- Head-width tests: inject the `0x0000000100000000` outer count into tuple,
  function, union, and intersection self patterns; reject before child
  allocation or traversal; prove no wrap, saturation, or bucket publication;
  and prove the same count in a nested pattern or interface argument is not
  subject to this head-specific narrowing rule.
- Coherence tests: reject `Union([Bool, Parameter(0)])` and the corresponding
  intersection and nested forms; prove that duplicate collapse, `Never` and
  `Any` identities and absorbers, nested flattening, canonical reordering, and
  existential additional-interface duplication cannot enter the overlap
  index; compare a valid outer `Parameter` against every concrete head; and
  exercise parameters in every admitted ordered position.
- Complete impl-pattern tests: `impl<T> I<T> for S` conflicts with
  `impl I<i32> for S`; `impl I<i32> for S` is disjoint from
  `impl I<bool> for S`; `impl<T> I<T> for Box<T>` conflicts with
  `impl I<i32> for Box<i32>` but is disjoint from
  `impl I<i32> for Box<bool>`; and interface-argument parameters in every
  normalization-sensitive position are rejected before overlap insertion.
- Existential tests: reject parameterized principal `I<P>` with rigid
  additional `I<Bool>` before unification; admit principal `I<P>` with rigid
  additional `J<Bool>` when `I` and `J` differ; and reject every parameterized
  additional-interface reordering or duplication case.
- Model tests: enumerate a bounded set of canonical ground substitutions and
  cross-check the admitted-pattern unifier for soundness and completeness in
  both candidate insertion orders.
- Lit tests: exact structured messages for `ZOM4019`, `ZOM4028`, `ZOM4029`,
  and `ZOM4081` without source-string payloads; exact primary spans and
  messages for `ZOM2100-ZOM2104` and `ZOM4088-ZOM4092`; and the combined-invalid
  marker declaration proving earliest-source parser precedence.
- AST conformance: positive ordinary, valid unsafe positive short and qualified
  marker, and negative marker dumps prove the singular generated fields; a
  heritage list with more than 255 entries proves the retained `ImplIfaceList` uses only
  `NodeList.size` without narrowing. Binder facts prove each marker owns one
  deterministic empty impl scope with no members, body traversal, receiver, or
  contextual `Self`.
- Conformance: same impl set under reversed source order, worker counts
  `1, 2, 4, 8`, fixed map-seed permutations, aliases, and imported module
  order produces byte-identical patterns, revisions, conflicts, and dumps.
- Generated files: operator, semantic-type-pattern, and impl-pattern closure
  gates report no missing or extra tag or field.
- Architecture: a source-schema gate and repository search prove that
  `StandaloneImplDecl.ifaces_id`, marker impl where/type-parameter fields,
  impl-header `ImplIfaceList` construction,
  `ImplIfaceList.n_ifaces`, multi-head
  collections, and `ImplHead.polarity` have no production consumer, while the
  singular ordinary field, `MarkerImpl.is_unsafe`, and heritage-only
  `ImplIfaceList` remain reachable.
- Spec alignment: run the repository `spec-alignment` gate over chapters 02,
  03, 04, 06, 09, 11, 14, 16, and 17, both `ZomLexer.g4` and `ZomParser.g4`, the
  hand lexer and parser, generated AST, binder, checker operator mapping, and
  conformance expectations. The gate must prove both source-shape and
  marker-policy alignment and the complete operator spelling and precedence
  closure; any drift blocks landing.
- RFC and links: `python3 scripts/check-rfc.py` and successful retrieval of
  every official prior-art URL.
- English: changed repository artifacts contain no CJK text.
- Format: `python3 scripts/check-format.py` and `git diff --check`.

## Open Questions

None

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-07-16 | DRAFT | Added a hash-bound overlay that closes canonical operator and impl-pattern codecs, defines direct revision cutovers, and leaves implementation blocked pending exact owner approval. |
| 2026-07-16 | REVIEW | Entered formal hash-bound review with every design question closed and implementation still blocked pending all required-owner approvals. |
| 2026-07-16 | RETURNED | Binder-checker review found that positional unification and pre-substitution head buckets were unsound for parameters in normalization-sensitive sorted collections, including union, intersection, and existential additional-interface counterexamples. |
| 2026-07-16 | DRAFT | Added a complete interface-and-self impl pattern with one parameter space, an exact ImplHead field order and reconstruction verifier, non-empty integration oracles, publication rules for every normalization-sensitive position, substitution-stable self heads, and blanket-to-all-self-head comparison before a fresh exact-hash review. |
| 2026-07-16 | REVIEW | Re-entered formal review after six adversarial draft audits closed normalization, complete interface-and-self unification, ImplHead framing, canonical round-trip, and self-head width blockers. |
| 2026-07-16 | RETURNED | RFC review found a stale draft-state statement, and binder-checker review found missing source-header provenance plus dangling base-RFC consumers after the impl-pattern replacement. |
| 2026-07-16 | DRAFT | Added exact source-header reconstruction, source-to-candidate bijection, complete multi-interface-list publication, sorted impl-head sequences, derived coherence indices, and explicit replacement of every dangling base-RFC consumer before a new exact-hash review. |
| 2026-07-16 | DRAFT | Replaced multi-conformance impl publication with one behavior interface per `ImplId`, isolated closed bodyless marker evidence, removed impl-head polarity and private clause indices, bound RFC 0004 source shape, and closed marker/source provenance before a fresh exact-hash review. |
| 2026-07-17 | DRAFT | Closed marker safety acknowledgement, generic marker classification, empty binding scopes, local and cross-module pre-map conflicts, explicit versus derived evidence projection, exact diagnostics, parser precedence, source-shape gates, and all revised codec oracles before formal review. |
| 2026-07-17 | DRAFT | Unified marker orphan ownership in frozen-interface coherence, restored base diagnostic ordering, added one marker-shape inventory and prelude-authorized policy registry, closed structural and enum/reference evidence, bound policy lineage through module and coherence inputs, and added complete synthetic and end-to-end oracles. |
| 2026-07-17 | DRAFT | Assigned subject and component reconstruction exclusively to module-interface verification, defined least positive global structural closure, closed explicit-marker orphan execution and precedence, and prohibited marker-component index narrowing. |
| 2026-07-17 | REVIEW | Entered a fresh exact-hash formal review after three locked draft audits approved the repaired ownership, admission-order, support-closure, and width contracts. |
| 2026-07-17 | RETURNED | A final pre-ballot audit proved that an eager finite structural-candidate map had no complete subject universe and could not carry same-key explicit and structural evidence. |
| 2026-07-17 | DRAFT | Replaced eager structural and builtin publication with one demand-driven marker proof engine over frozen explicit coherence facts, semantic types, signatures, and marker policy. |
| 2026-07-17 | DRAFT | Added independently verified explicit-positive query results and exact generic nominal component substitution, canonicalization, linearizable interning, and stable memoization. |
| 2026-07-17 | DRAFT | Made cycle detection root-invocation-local and limited shared memoization to linearly published completed proofs or stable negative results. |
| 2026-07-17 | REVIEW | Entered a fresh exact-hash formal review after three locked draft audits approved demand-driven proof authority, generic component interning, and concurrent cycle isolation. |
| 2026-07-17 | RETURNED | Spec-audit review found that the operator alignment gate omitted the lexical chapter, expressions chapter, and lexer grammar. |
| 2026-07-17 | DRAFT | Expanded operator alignment across lexical and expression specifications, both ANTLR grammars, the hand frontend, generated AST, checker mapping, and conformance. |
| 2026-07-17 | DRAFT | Added the error-handling chapter's `?!`, `!!`, and `PostfixSuffix` definitions to the exhaustive operator and spec-alignment gates. |
| 2026-07-17 | REVIEW | Re-entered exact-hash formal review after three locked draft audits approved the complete operator and specification alignment matrix. |
| 2026-07-17 | RETURNED | Binder-checker review found that explicit-marker projection coverage was defined over source-valid headers rather than the exact final admission survivors. |
| 2026-07-17 | DRAFT | Defined the explicit marker map as a bijection with headers surviving builtin conflict, marker orphan, and local same-key conflict admission. |
| 2026-07-17 | REVIEW | Re-entered exact-hash formal review after three locked draft audits approved the final-survivor marker projection bijection. |
| 2026-07-17 | ACCEPTED | All required owners approved exact review SHA-256 `642836225d54f6fa28f8c27e9985972081dbd221c2e8f3e61a0aafd04fe9bb1e`: `rfc`, `lexer-parser`, `binder-checker`, `module-system`, `error-system`, `spec-audit`, and `verification`. |
| 2026-07-17 | IMPLEMENTING | Began the accepted direct checker-codec cutover through the implementation tracker. The transition changes status metadata only and preserves the approved design; no prior codec producer or consumer may remain. |
| 2026-07-18 | IMPLEMENTING | Synchronized the accepted RFC 0018 occurrence bridge, per-occurrence mixed-form classification, post-classification ordinary and marker survivor streams, and occurrence-specific diagnostic lineage. |
| 2026-07-20 | LANDED | The direct implementation, RFC records, architecture guidance, and complete verification evidence landed on `develop` through commits `0bba7e34`, `f86b5660`, and `76e73196`; no replaced codec producer or consumer remains. |
