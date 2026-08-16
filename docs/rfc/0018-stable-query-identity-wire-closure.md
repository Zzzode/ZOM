---
rfc: 18
title: Stable Query Identity Wire Closure
type: compiler
status: IMPLEMENTING
author: ZOM Compiler Team
review-manager: rfc
required-owners: [task-router, rfc, lexer-parser, binder-checker, module-system, error-system, ir-backend, spec-audit, verification]
approvers: [task-router, rfc, lexer-parser, binder-checker, module-system, error-system, ir-backend, spec-audit, verification]
created: 2026-07-18
updated: 2026-07-27
area: compiler
requires: [5, 14, 15, 17]
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0018-review-and-implementation.md#discussion-record
decision: docs/rfc/tracking/0018-review-and-implementation.md#decision-record
implementation: docs/rfc/tracking/0018-review-and-implementation.md#implementation-tracker
tracking-issue: docs/rfc/tracking/0018-review-and-implementation.md#implementation-tracker
---

# RFC 0018: Stable Query Identity Wire Closure

## Summary

This RFC closes the remaining wire-level gaps that block RFC 0017 stable
identity implementation. It defines the recursive enclosing-owner sum used by
named definitions and implementations, the overload-header digest preimage,
the complete module-resolution policy and request key, and the boundary between
stable named items and owner-local syntax. It does not change the query runtime
architecture accepted by RFC 0017.

## Motivation

The first RFC 0017 implementation slice successfully separated build producer
identity from current output, made crate identity plan-derived, removed content
from generated source identity, and reduced `ModuleKey` to crate plus canonical
path. Implementation cannot safely continue to `DefinitionKey`, `ImplKey`, or
`ModuleResolutionKey`, however, because three correctness-critical preimages
remain incomplete:

- the element encoding and recursion constraints of an enclosing named-owner
  sequence are not specified;
- the overload-header digest has no exact domain-separated preimage; and
- the module-resolution policy key is named but its fields, tags, alias
  behavior, and prelude behavior are not defined.

The current implementation therefore still encodes source positions and
traversal ordinals in definition and implementation identity, and it still
encodes request-site provenance plus a whole-environment revision in module
request identity. Choosing missing bytes inside implementation would turn the
compiler into the design authority and make producer/verifier agreement
accidental. A small reviewed follow-up is required before those replacements
can be implemented.

## Goals

- Define complete, deterministic canonical preimages for stable named
  definitions, implementations, overload headers, and module resolution.
- Make lexical ownership recursive, unambiguous, module-local, and cycle-free.
- Keep source provenance, request sites, environment revisions, and current
  alias or prelude targets out of semantic query identity.
- Define which syntax entities may become persistent query roots.
- Provide mutation and architecture gates that prevent position-derived or
  environment-wide identity from returning.

## Non-Goals

- This RFC does not change ZOM source syntax or language semantics.
- This RFC does not redesign RFC 0017 memoization, red-green validation,
  persistence, concurrency, diagnostics, or Binder projections.
- This RFC does not approve implementation of the query runtime before the
  stable identity replacement is complete.
- This RFC does not retain superseded identity decoders, aliases, registries, or
  alternate constructors.

## Prior Art

Rust separates stable definition paths from process-local handles and hashes a
domain-specific canonical path rather than source positions. ZOM adopts the
same requirement that lexical parents and disambiguating semantic header data,
not spans or allocation order, determine persistent identity.

Clang USRs and Swift symbol identity encode declaration context and semantic
signature information while keeping source locations out of ordinary named
symbol identity. ZOM adopts their context-plus-header shape but uses closed
canonical records and semantic domains so producer and verifier can reject
incomplete encodings.

Salsa, rustc queries, and Skyframe distinguish a computation key from the
tracked inputs read while computing it. ZOM applies that separation to module
resolution: alias targets, configured prelude targets, catalog buckets,
generated source revisions, and requester ancestry are dependencies, not bytes
inside `ModuleResolutionKey`.

Content-addressed build systems distinguish action identity from current
artifact digests. RFC 0017 already applies that rule to build scripts; this RFC
extends the same semantic-versus-current-state discipline to named items and
module resolution.

## Guide-Level Explanation

A contributor editing only whitespace, comments, a private body, or source
positions must not replace the stable identity of an unchanged named item. A
named method inside an implementation remains distinct from an identical method
inside another implementation because its final lexical owner is the complete
implementation key. A local variable or closure never becomes a persistent
query root; it remains local to the nearest stable body query.

Two import sites that ask the same semantic question share one module
resolution query. Each site keeps separate revision-local provenance for
diagnostics, but neither span nor traversal ordinal enters the query key.
Changing an alias target or configured prelude invalidates the query through a
tracked input without replacing the request key.

## Reference-Level Design

### Normative Contract Boundary

On acceptance, this RFC is the sole normative wire contract for
`DefinitionKey`, `ImplKey`, `EnclosingStableOwnerKey`,
`GenericParameterKey`, `CallableParameterKey`,
`ModuleResolutionPolicyKey`, and `ModuleResolutionKey`. The corresponding
identity, type-parameter, signature, and request-key records in RFC 0004, RFC
0005, RFC 0008, and RFC 0011 are replaced in full. RFC 0014 continues to own
receiver semantics. RFC 0015 remains the sole owner of marker source
classification, signature-fact construction, `ZOM4088` through `ZOM4092`, and
their exact precedence. This RFC owns only stable identity admission and the
stable diagnostic lineage supplied to that contract. RFC 0017 continues to own
the query runtime and uses the records defined here wherever its
stable-identity text is less specific.

The acceptance change synchronizes those seven RFC documents and the RFC 0005,
RFC 0014, RFC 0015, RFC 0017, and RFC 0018 trackers in the same documentation
series. A producer, verifier, or test may not select between the
position-bearing and semantic records. Every implementation path uses the
canonical semantic records and tags defined here.

### Enclosing Stable Owners

`DefinitionKey` and `ImplKey` are 32-byte SHA-256 digests of the complete
canonical records defined below. The identity inventory retains both the
digest and complete record. Equal digests with unequal records are a hash
collision and fail with the compiler-invariant path before handle admission;
digest equality alone never proves record equality.

`EnclosingStableOwnerKey` is the closed sum
`DefinitionOwner(DefinitionKey) = 0x01` and
`ImplementationOwner(ImplKey) = 0x02`. An enclosing-owner sequence is ordered
from outermost to innermost. Its RFC 0011 sequence count is followed by exactly
33 bytes per element: the one-byte alternative tag and the referenced key's
raw 32-byte digest. There is no nested record, length prefix, or recursive byte
expansion inside an element. An owner chain therefore occupies `8 + 33 * n`
bytes under the RFC 0011 64-bit sequence count.

The owner relation is structural lexical containment only. A declaration's
self type, implemented trait, resolved target, or referenced definition does
not implicitly become an owner. A module-level definition or implementation
has an empty owner sequence.

The owner relation is validated against the retained identity records. For
owner element `i`, the referenced record must use the same `ModuleKey` as the
record being constructed and its enclosing-owner sequence must be
byte-identical to elements `[0, i)` of the containing sequence. An unknown
digest, unequal collision record, different module, skipped prefix, repeated
owner, self-owner, or cycle is invalid. Keys are constructed and validated
from outermost owner to innermost child, so every owner record is already
admitted before a child is encoded. Pointer identity and lazy recursive
references do not participate in canonical encoding.

### Stable Definition Identity

`DefinitionIdentityRecord` contains these fields in order: the expanded stable
`ModuleKey`, the `EnclosingStableOwnerKey` sequence, declaration-kind tag,
namespace tag, NFC declared name, overload-header-presence tag, and, when
present, the raw 32-byte `OverloadHeaderDigest`. `DefinitionKey` is exactly:

```text
SHA-256(
  ASCII("zom.named-item-header")
  || 0x00
  || Encode(DefinitionIdentityRecord)
)
```

Declaration-kind tags remain RFC 0011 `DefinitionKind` tags `0x01` through
`0x17`. The admitted closed set and namespace mapping are:

| Namespace | Admitted definition kinds |
|---|---|
| `Value = 0x01` | `Function`, `Method`, `Constructor`, `Destructor`, `Field`, `EnumVariant`, `Constant`, `Static` |
| `Type = 0x02` | `Class`, `Struct`, `Interface`, `Enum`, `Error`, `TypeAlias`, `AssociatedType` |
| `Module = 0x03` | `ModuleAlias` |

`Parameter`, `TypeParameter`, `Local`, `PatternBinding`, and `Closure` are
invalid in `DefinitionIdentityRecord`. `ImportAlias` and `ReexportAlias` are
also invalid: RFC 0017 `ImportBindingKey` is their sole stable semantic
identity, so pre-admission never creates an alias `DefinitionKey`. A
whole-module alias uses `ModuleAlias` in the module namespace. Before target
lookup, an unqualified selected import or re-export creates one
`ImportBindingKey` slot for each syntactically permitted namespace:
`Value`, `Type`, and `Module`. `ImportTarget` returns a verified target or
deterministic absence for each stable slot. Equal spelling exported in value
and type namespaces therefore makes two slots present while the module slot
remains absent; a later export-surface change changes slot values without
changing slot keys. All slots absent produces the existing unresolved-import
source diagnostic. A future explicit namespace syntax may restrict the slot
set but requires its own accepted syntax contract. Label and attribute
namespaces are not stable definition namespaces.

A nested named member declared directly by an implementation uses that
implementation as the final owner element. If the implementation is nested,
its complete owner sequence precedes the implementation element.

Only `Function`, `Method`, and `Constructor` carry an overload header; their
callable-kind tags are respectively `0x01` through `0x03`. `ExternDecl` uses
`Function`. `Destructor` and every non-overloadable definition encode the
absent overload-header tag. Receiver-shape tags are RFC 0014
`Shared = 0x01`, `Mutable = 0x02`, and `Move = 0x03`; free functions, static
methods, constructors, and lifecycle callables encode an absent receiver.
`GenericParameterKey.kind` has the closed value `Type = 0x01`. The overload
header encodes neither a redundant kind nor variance because every current
user generic parameter is an invariant type parameter. Rejected `in` and
`out` annotations remain only in the token snapshot used to produce the
registered syntax diagnostics; they do not produce an AST field, identity
candidate, or canonical header. Ordinary callable
parameters encode their complete type syntax directly; there is no separate
parameter-mode field because reference shape is already represented by
`CanonicalHeaderTypeSyntax` and the current language has no ordinary
move-parameter producer. External ABI tags are the current frontend set
`Cdecl = 0x01`, `Stdcall = 0x02`, and `ZomNative = 0x03`. Variadicness and
backend calling conventions are not ABI variants and are not admitted by canonical.

`OverloadHeader` encodes these fields in order:

```text
OverloadHeader {
  callableKind: CallableHeaderKind,
  name: NfcDeclaredName,
  receiver: Maybe<ReceiverShape>,
  genericParameters: Sequence<CanonicalGenericParameter>,
  obligations: SortedUniqueSequence<CanonicalBoundObligation>,
  parameters: Sequence<CanonicalCallableParameter>,
  result: CanonicalCallableResult,
  raises: Maybe<SortedUniqueSequence<CanonicalHeaderTypeSyntax>>,
  externalAbi: Maybe<ExternalAbi>,
}

CanonicalGenericParameter {
  defaultType: Maybe<CanonicalHeaderTypeSyntax>,
}

CanonicalBoundObligation {
  subject: CanonicalHeaderTypeSyntax,
  bound: CanonicalHeaderTypeSyntax,
}

CanonicalCallableParameter {
  label: NfcName,
  type: CanonicalHeaderTypeSyntax,
  hasDefault: bool,
}
```

Callable parameter default expression bodies are excluded; only their presence
bit is encoded. `CanonicalCallableResult` tags are `Unit = 0x01`,
`ConstructorSelf = 0x02`, and `Type = 0x03` followed by canonical type syntax.
An omitted function or method result and an explicit `unit` result both encode
`Unit`; a constructor encodes `ConstructorSelf`. The raises sequence is
obtained by recursively flattening union syntax, normalizing every member,
sorting by complete member bytes, and removing duplicate bytes. A present
raises sequence must be non-empty.

RFC 0014 receiver normalization applies before header encoding. Bare `this`
and `this: Self` are the `Base` receiver form: either encodes `Shared` on a
non-mutating method and `Mutable` on a `mutating` method. `this: &Self` encodes
`Shared`; combining it with `mutating` is a source error. `this: &mut Self`
encodes `Mutable` with or without `mutating`. A move-marked `Base` receiver
encodes `Move`; combining move with `mutating`, `&Self`, or `&mut Self` is a
source error. A `mutating` method without a receiver and a static method with a
receiver are also rejected before identity admission. The receiver is removed
from the ordinary parameter sequence, so it never has two encodings.

A non-external callable has absent `externalAbi`; every `ExternDecl` has a
present ABI. An omitted ABI, `"C"`, and `"Cdecl"` normalize to `Cdecl`;
`"system"` normalizes to `Stdcall`; and `"zom-cdecl"` normalizes to
`ZomNative`. Every other spelling is rejected by the parser and cannot enter a
header. The canonical contract has no ordinary callable `async` or `unsafe`
producer, so neither field exists.

Inline generic bounds and where-clause bounds normalize into the single
`obligations` set. Before creating obligations, producer and verifier each
walk the ordered `TypeParameterBoundList` children independently and retain
every member's source site. The bound-list conjunction is never represented or
flattened as a structural `IntersectionTypeExpr`. An inline atomic bound on
generic ordinal `i` becomes
`CanonicalBoundObligation(GenericReference(0, i), member)`. A where-clause
atomic bound uses its normalized subject and member directly. The current
language admits only positive `Implements` bounds, so the record has no dormant
relation tag. Thus `T: A + B` and `where T: A, T: B` both produce the same two
obligations, and `T: Eq + Eq + Eq` produces one obligation plus two duplicate
occurrences. Duplicate obligation bytes are removed only after those sites are
recorded for `W1204`.

Generic declaration names never enter a stable header. Every name path uses the
closed record:

```text
CanonicalNameReference {
  root: Absolute = 0x01
      | Relative = 0x02
      | Generic { binderDepth: uint32, ordinal: uint32 } = 0x03,
  suffix: Sequence<NfcName>,
}
```

Absolute and relative roots require a non-empty suffix. A generic root permits
an empty suffix for `T` and a non-empty suffix for projections such as
`T::Item`. Depth zero is the current declaration or implementation binder,
depth one is the immediately enclosing stable owner's binder, and larger
depths proceed outward. The parser stores the absolute-versus-relative root in
`ModulePath` before consuming a leading `::`; neither producer nor verifier
infers the root from the remaining segments. Therefore
`fun f<T>(x: T)` and `fun f<U>(x: U)` have identical overload-header bytes,
while `::Trait` and `Trait` remain distinct. Shadowing changes binder depth
according to lexical resolution, not spelling.

`CanonicalHeaderTypeSyntax` uses a dedicated semantic wire schema. It does not
encode AST node ids or inherit the AST schema's unused enum alternatives. Its
closed one-byte tags and fields are:

```text
CanonicalHeaderTypeSyntax =
    Named = 0x01 {
      name: CanonicalNameReference,
      arguments: Sequence<CanonicalHeaderTypeSyntax>,
    }
  | Predefined = 0x02 { kind: PredefinedTypeKind }
  | Function = 0x03 {
      parameters: Sequence<CanonicalHeaderTypeSyntax>,
      result: CanonicalHeaderTypeSyntax,
      raises: Maybe<SortedUniqueSequence<CanonicalHeaderTypeSyntax>>,
    }
  | Union = 0x04 {
      members: SortedUniqueNonEmptySequence<CanonicalHeaderTypeSyntax>,
    }
  | Intersection = 0x05 {
      members: SortedUniqueNonEmptySequence<CanonicalHeaderTypeSyntax>,
    }
  | FixedArray = 0x06 {
      element: CanonicalHeaderTypeSyntax,
      length: uint64,
    }
  | DynamicArray = 0x07 { element: CanonicalHeaderTypeSyntax }
  | Slice = 0x08 { element: CanonicalHeaderTypeSyntax }
  | Optional = 0x09 {
      element: CanonicalHeaderTypeSyntax,
      depth: uint8,
    }
  | Reference = 0x0a {
      mutability: Shared = 0x01 | Mutable = 0x02,
      element: CanonicalHeaderTypeSyntax,
    }
  | RawPointer = 0x0b {
      mutability: Const = 0x01 | Mutable = 0x02,
      element: CanonicalHeaderTypeSyntax,
    }
  | TypeQuery = 0x0c { name: CanonicalNameReference }
  | Object = 0x0d {
      members: SortedUniqueSequence<CanonicalObjectTypeMember>,
    }
  | Tuple = 0x0e {
      elements: Sequence<CanonicalHeaderTypeSyntax>,
    }
  | AssociatedProjection = 0x0f {
      base: CanonicalHeaderTypeSyntax,
      interface: Maybe<CanonicalHeaderTypeSyntax>,
      member: NfcName,
    }
  | Dynamic = 0x10 {
      principal: CanonicalNamedHeaderType,
      markers: SortedUniqueSequence<CanonicalNameReference>,
      associatedBindings: SortedUniqueSequence<CanonicalAssociatedBinding>,
    };

CanonicalNamedHeaderType {
  name: CanonicalNameReference,
  arguments: Sequence<CanonicalHeaderTypeSyntax>,
}

CanonicalObjectTypeMember {
  name: NfcName,
  type: CanonicalHeaderTypeSyntax,
  mutable: bool,
  optional: bool,
}

CanonicalAssociatedBinding {
  name: NfcName,
  type: CanonicalHeaderTypeSyntax,
}
```

`PredefinedTypeKind` tags are `I8 = 0x01`, `I16 = 0x02`, `I32 = 0x03`,
`I64 = 0x04`, `U8 = 0x05`, `U16 = 0x06`, `U32 = 0x07`, `U64 = 0x08`,
`F32 = 0x09`, `F64 = 0x0a`, `Bool = 0x0b`, `Str = 0x0c`,
`Char = 0x0d`, `Null = 0x0e`, `Unit = 0x0f`, `Never = 0x10`, and
`Any = 0x11`. `Optional.depth` is exactly `0x01` for `T?` and `0x02` for
`T??`; every other value is invalid.

Union and intersection normalization recursively flattens the same variant,
sorts members by complete canonical bytes, removes duplicate bytes, and
collapses a singleton to its member. Dynamic canonical has exactly one named
principal, including its generic arguments, and no lifetime field because
those are the only current parser semantics. A non-named principal is an
invalid record and has no producer. Dynamic markers, associated bindings, and
object members are sorted-unique by complete element bytes.
Ordered tuple, function-parameter, path-suffix, and type-argument children
retain source order. `FixedArrayTypeExpr` normalizes to `FixedArray`,
`ArrayTypeExpr` to `DynamicArray`, and `SliceArrayTypeExpr` to `Slice`.
`DynamicArray` and `Slice` are never aliases and must remain unequal in
overload identity. A fixed-array length is admitted only when the current canonical
constant evaluator accepts the syntax as an unsigned 64-bit integer. The
normalized integer is encoded directly; expression syntax never enters the
identity record. Failure to evaluate the length rejects the candidate with the
existing source diagnostic before stable identity publication.

The parser AST is the sole authority for semantic syntax distinctions.
`MethodDecl.mode` is the closed set `Instance`, `Static`, `Mutating`, and
`StaticMutating`; the last value preserves rejected source until the existing
modifier diagnostic is materialized. `ModulePath.root` is `Relative` or
`Absolute`, recorded before a leading `::` is consumed. Literal syntax uses
distinct `StringLiteralExpr`, `CharacterLiteralExpr`, and
`NoSubstitutionTemplateLiteralExpr` variants; unused raw and prefix fields are
deleted. `DynTypeExpr` retains one `principal` directly and removes unproduced
multi-principal and lifetime fields. `GenericTypeParam` contains only `name`,
optional `bounds_id`, and optional `default_ty`; `bounds_id` names a
`TypeParameterBoundList` whose ordered `TypeExpr` children retain distinct
member source ranges. `AssociatedTypeDecl.bounds_id` analogously names an
`AssociatedTypeBoundList`; neither bound-list node is an
`IntersectionTypeExpr`. `GenericTypeParam` has no variance field. The
parser may recover past rejected `in` or `out` tokens after diagnosing them,
but no semantic AST or identity candidate records those annotations.
`ArrayTypeExpr` contains only `elem` and is produced only by postfix `T[]`;
`SliceArrayTypeExpr` is produced only by `[T]`, and `FixedArrayTypeExpr` only by
`[T; N]`. Postfix `T[N]` is rejected by the parser and produces no AST node or
identity candidate.

The AST `Abi` enum is the normalized semantic set `Cdecl`, `Stdcall`, and
`ZomNative`. Original ABI spelling remains only in the parsed token snapshot
for diagnostics and independent parser verification; source spelling never
enters stable identity. The where-bound relation enum contains only the
produced `Implements` value. Literal expressions are unreachable from the wire
schema because fixed-array lengths encode only an evaluated `uint64`; the
distinct literal nodes do not become dormant identity variants.

No `HeaderLexicalFacts` record is introduced. A parser tree that loses one of
these semantic distinctions is invalid input to identity discovery. Producer
and verifier walk the AST independently and may use the retained token snapshot
to cross-check parser normalization, but neither reconstructs semantic syntax
through a second fact inventory.

The machine-readable wire inventory lives in
`products/zomlang/compiler/identity/canonical-header-syntax-schema.yml` and is
generated into
`products/zomlang/compiler/identity/canonical/canonical-header-syntax-schema.def` by
`scripts/generate-canonical-header-syntax-schema.py`. The source schema lists
exactly the tags and fields above; it contains no AST ids. The identity
architecture gate regenerates to a temporary file and requires byte equality,
and it rejects a schema value not named by this RFC. Changing a tag, field,
normalization rule, or admitted variant requires a new identity domain.

All counts, optionals, booleans, fixed-width integers, names, and recursive
records use the RFC 0011 canonical codecs. Source ranges, `NodeId`, arena
positions, token trivia, recovery objects, grouping-only parentheses, raw
literal spelling, and parser handles do not encode. The producer and verifier
share the dedicated schema and canonical codec but independently walk and
normalize the AST.

The overload header excludes body syntax, default expression bodies,
visibility, export state, non-semantic attributes, source provenance, and
parser or arena identity.

`OverloadHeaderDigest` is exactly:

```text
SHA-256(
  ASCII("zom.overload-header")
  || 0x00
  || Encode(OverloadHeader)
)
```

`Encode(OverloadHeader)` is the RFC 0011 canonical encoding of those
header fields in schema order. It has no outer optional tag, length wrapper,
source provenance, or second hash layer. The optional presence tag belongs only
to `DefinitionIdentityRecord`, which embeds the digest's 32 raw bytes.
Unordered header members use the normalization rules above before hashing.

The pre-admission inventory and admitted identity registry retain
`OverloadHeaderAuthority { digest: OverloadHeaderDigest,
header: OverloadHeader }` for every present digest. The verifier
recomputes the digest from the complete retained header. Equal digest bytes with
unequal header bytes are an invariant collision before source-redeclaration
grouping; a present definition digest without exactly one equal authority
record is invalid. The digest is an index, never the sole equality authority.

### Stable Implementation Identity

`ImplIdentityRecord` contains, in order, expanded stable `ModuleKey`, the
`EnclosingStableOwnerKey` sequence, the ordered canonical generic-parameter
sequence, polarity tag, unsafe tag, canonical trait reference, self-type
syntax, and the sorted-unique canonical obligation sequence. `ImplKey`
is exactly:

```text
SHA-256(
  ASCII("zom.impl-header")
  || 0x00
  || Encode(ImplIdentityRecord)
)
```

Polarity tags are `Positive = 0x01` and `Negative = 0x02`; unsafe tags are
`Safe = 0x01` and `Unsafe = 0x02`. A trait reference is
`CanonicalTraitReference { name: CanonicalNameReference,
arguments: Sequence<CanonicalHeaderTypeSyntax> }`. It never contains a
resolved `DefinitionKey` or current lookup result. Its name root must be
`Absolute` or `Relative`; a `Generic` root is invalid for an implemented trait.

Marker-implementation syntax admission is independent of the positive-marker
`unsafe` requirement. Both `impl Marker for T;` and
`impl path::Marker for T;` produce a positive `MarkerImplDecl` candidate with
the `Safe` tag; neither the recursive parser nor the ANTLR oracle may reject a
candidate solely because `unsafe` is absent. Short and qualified marker paths
follow this single path; path segment count cannot select a different
admission rule.

A syntactically complete `Safe` candidate proceeds through canonical
`ImplIdentityRecord` construction, complete-record occurrence grouping, and
ordinary `ImplKey` admission. The first candidate in a unique authority group
receives its revision-local `ImplId` from the semantic-context arena after
tracked active-membership validation and establishes the shared identity
authority for the group. Stable identity admission is not semantic marker
validity. Every marker-shaped source
occurrence in the group, including an occurrence after the authority, reaches
RFC 0015 signature classification with the shared `ImplKey` and its own exact
`IdentitySyntaxSiteKey`. A later occurrence receives no distinct `ImplId`; its
revision-local `ImplOccurrenceId` and site, rather than a fabricated second
semantic handle, distinguish its binding facts, scope, classification, and
diagnostic lineage.

RFC 0015 classification and suppression are then authoritative. A bodyless
candidate resolving to a behavior interface emits `ZOM4089` before any unsafe
check. A positive `Safe` candidate that has successfully classified as
marker-only emits `ZOM4091 PositiveMarkerImplRequiresUnsafe`. The admitted
`ImplKey`, retained identity record, revision-local site, and diagnostic fact
remain available for lineage, but that candidate publishes no `MarkerFact`, no
explicit-marker module-interface entry, and no coherence input. Marker
candidates never publish an ordinary `ImplHead`. A valid positive `Unsafe`
candidate continues through RFC 0015 builtin-conflict, orphan, local-conflict,
module-interface, and global-coherence stages and publishes the corresponding
`MarkerFact` only when all earlier checks succeed. Negative candidates retain
their existing parser and signature rules.

The generic-parameter, alpha-normalized generic-reference, structural type,
collection-normalization, and dedicated schema contracts are exactly those in
Stable Definition Identity. Inline generic bounds and where-clause bounds both
become `CanonicalBoundObligation` values in one field, so equivalent forms have
one encoding. Obligations are sorted by complete canonical bytes and duplicate
bytes are removed.
Duplicate source occurrences remain in revision-local candidate provenance and
produce the specification's suppressed-by-default `W1204 DuplicateBound`
warning; they do not change the key or reject otherwise valid source.

The implementation body, source provenance, parser handles, traversal order,
and current resolution results are excluded. Byte-identical complete
implementation records form one identity occurrence group before handle
admission; they are not a compiler identity invariant. Source-level conflict is
decided only among successfully classified ordinary or marker survivors.

### Stable And Owner-Local Entity Boundary

Only a declaration with an NFC declared semantic name and a complete stable
lexical-owner chain may receive a `DefinitionKey`. Locals, pattern bindings,
labels, anonymous closures, function expressions, expression nodes, and every
entity whose immediate semantic owner is owner-local or anonymous are excluded
from `DefinitionKey`, `ImplKey`, stable identity interners, and public query
keys. They are represented only by `OwnerLocalBindingKey`, `LocalSyntaxPath`,
and revision-local provenance inside the nearest enclosing stable named-item
query.

A named declaration directly contained by an `ImplKey` remains a stable named
definition. Its owner sequence is the implementation's enclosing-owner sequence
followed by `ImplementationOwner(theImplKey)`. A named declaration below an
anonymous or owner-local entity does not skip that entity to claim a distant
stable owner; it remains owner-local and cannot become a persistent query root.

### Stable Subordinate Parameter Keys

Generic and callable parameters are stable subordinate entities, not named
definitions. Their keys may occur inside semantic values, but cannot key
syntax, scope, export, name-resolution, Binder-body, Checker-body, or other
provider roots and are never inserted into the global `DefinitionKey`
inventory.

`StableGenericParameterOwnerKey` is the closed sum
`DefinitionOwner(DefinitionKey) = 0x01` and
`ImplementationOwner(ImplKey) = 0x02`. It encodes as its one-byte tag followed
by the referenced key's raw 32-byte digest. `GenericParameterIdentityRecord`
contains that owner, `Type = 0x01`, and a zero-based unsigned 32-bit declaration
ordinal, in that order. `GenericParameterKey` is exactly:

```text
SHA-256(
  ASCII("zom.generic-parameter")
  || 0x00
  || Encode(GenericParameterIdentityRecord)
)
```

Its record encoding is exactly the owner tag, raw 32-byte owner digest,
one-byte `Type` kind, and big-endian `uint32` ordinal, without a length prefix.

`CallableParameterPosition` is the closed sum `Receiver = 0x01` and
`Ordinary { ordinal: uint32 } = 0x02`. `CallableParameterIdentityRecord`
contains the owning `DefinitionKey` raw 32-byte digest followed by that
position. The receiver never consumes an ordinary ordinal.
`CallableParameterKey` is exactly:

```text
SHA-256(
  ASCII("zom.callable-parameter")
  || 0x00
  || Encode(CallableParameterIdentityRecord)
)
```

Its record encoding is exactly the raw 32-byte owner digest, one-byte position
tag, and, only for `Ordinary`, the big-endian `uint32` ordinal, without a length
prefix.

Parameter-key admission occurs only after the owning definition or
implementation has been selected as its collision-group authority. Every
admissible generic ordinal, ordinary callable ordinal, and present receiver
position produces exactly one retained record. Missing or duplicate positions,
out-of-range ordinals, and a receiver position for a receiver-less header are
invariant failures. The identity inventory retains each digest with its
complete parameter record; equal digest bytes with unequal records are
invariant collisions. No parameter handle is issued before owner authority,
record coverage, and current verified-header membership all succeed.

Names, source ranges, parser ids, and parameter properties do not occur
directly in a subordinate identity record. They may nevertheless affect its
key transitively by changing the owner key. A callable `DefinitionKey` contains
its overload-header digest, so every identity-relevant callable-header edit
changes the owner key and therefore all of that callable's generic, receiver,
and ordinary parameter keys. When the owner key remains unchanged, insertion
or reordering changes only positional keys whose ordinals change. Alpha-
renaming a generic parameter or renaming an internal callable parameter leaves
the subordinate key unchanged only when the edit also leaves the owner key
unchanged. Owner headers encode generic references by binder depth and ordinal,
not `GenericParameterKey`, to avoid an owner-key cycle.

RFC 0005 `SemanticTypeKey` represents a type parameter with its
`GenericParameterKey`, never a revision-local `DefId`. Public callable
signature values identify both ordinary and receiver parameters with
`CallableParameterKey` and carry position-appropriate label, mode, type, and
default-presence values separately. Materialization issues revision-local,
context-branded `GenericParameterId` and `CallableParameterId` handles only
after the complete retained key record and current owner header have been
independently verified. `DefId(Parameter)` and `DefId(TypeParameter)` are
deleted; no parameter identity is reconstituted as a named definition.
Provider dispatch remains at the owning `DefinitionKey` or `ImplKey`.

### Module Resolution Policy

`ModuleResolutionPolicyKey` is the following closed record:

```text
ModuleResolutionPolicyKey {
  unicodeNormalization: Nfc,
  caseComparison: CaseSensitive,
  symlinkHandling: ResolveThenConfine,
  containment: DeclaredRootsOnly,
  localLookup: RequesterAncestryAndCrateRoot,
  dependencyAliasLookup: ExactFirstSegment,
  preludeLookup: ConfiguredCratePrelude,
  candidateSelection: AllDistinctMatchesNoPrecedence,
}
```

Each currently admitted enum value has tag `0x01`; every other tag is invalid.
Its canonical bytes are:

```text
ASCII("zom.module-resolution-policy")
0x00
0x01
0x01
0x01
0x01
0x01
0x01
0x01
0x01
```

For `RequesterAncestryAndCrateRoot`, the tracked requester-ancestry input is a
non-empty sequence containing the requester first, then each strict lexical
module ancestor from inner to outer, and ending at the crate's declared root
module. Every adjacent pair must have the same crate and remove exactly one
canonical path segment. For a normalized requested path `p`, local lookup
forms `requester.path + p`, then `ancestor.path + p` for every strict ancestor
in that order, and finally `p` as the crate-root-relative path. Equal paths are
deduplicated before catalog reads. All matching `ModuleKey` values enter one
set sorted by complete canonical key bytes; lookup order does not select a
winner.

### Module Resolution Dependency Queries

This RFC extends RFC 0017's closed initial query inventory with two narrow
Semantic input-query descriptors. They replace any implementation that reads
the complete `ActiveModules` value from `ResolveModuleRequest`:

```text
RequesterModuleAncestry {
  key: ModuleKey,
  value: NonEmptySequence<ModuleKey>,
  equality: CompleteCanonicalBytes,
  dependencyAuthority: VerifiedModuleGraphInputTransaction,
  verifier: IndependentRequesterAncestryVerifier,
  durability: Low,
  retention: Pinned,
  provider: NoneInput,
  cyclePolicy: NotApplicable,
  cost: CheapInput,
}

ModuleCatalogPathBucket {
  key: ModuleCatalogPathBucketKey {
    crate: CrateKey,
    path: CanonicalModulePath,
  },
  value: Maybe<ModuleKey>,
  equality: CompleteCanonicalBytes,
  dependencyAuthority: VerifiedModuleGraphInputTransaction,
  verifier: IndependentModuleCatalogBucketVerifier,
  durability: Low,
  retention: Pinned,
  provider: NoneInput,
  cyclePolicy: NotApplicable,
  cost: CheapInput,
}
```

`CanonicalModulePath` is a non-empty RFC 0011 sequence of
`ModulePathSegment`, with the same segment normalization and encoding used by
`ModuleKey.path`. `ModuleCatalogPathBucketKey` has domain
`zom.module-catalog-path-bucket`, one zero byte, expanded `CrateKey`, then
that complete path sequence. It contains no source, root, target, or revision
field.

Both inputs have Semantic reuse class and `Low` durability because editable or
generated source can change the verified module graph. They are cheap input
queries with no provider execution and no possible query cycle, and produce no
source diagnostics. An
admission-verifier disagreement is a compiler invariant and publishes no input
transaction. `RequesterModuleAncestry` verifies the exact non-empty chain
defined above, requires its first value to equal the key, and requires the
requester and final declared crate-root module to be active in the same
verified module graph. A strict intermediate prefix may be a structural
`ModuleKey` without a source or active handle. Such a record exists only to
form lexical candidate paths; it is not admitted to the active identity
registry. Active candidate membership is determined exclusively by the
corresponding `ModuleCatalogPathBucket`. A
`ModuleCatalogPathBucket` is absent or contains exactly the active `ModuleKey`
whose crate and complete canonical path equal its key; duplicate structural
modules are rejected before the input transaction.

The verified module-graph transaction compares and publishes each ancestry
and each exact path bucket independently. Adding, removing, or replacing a
module at another path does not advance a bucket's `changedAt`.
`ResolveModuleRequest` reads one ancestry input for its requester and one
path-bucket input for every deduplicated local, ancestor, crate-root, or alias
candidate path that it tests. It additionally reads only the existing exact
`DependencyAliasRoot`, `ConfiguredPrelude`, and `ModuleSearchRoots`
projections required by its key. Its independent verifier repeats candidate
formation and demands the same narrow inputs through its verifier dependency
frame. An unrelated catalog-path edit may execute the affected input admission
but cannot execute a resolution provider whose demanded buckets remain equal.

### Semantic Module Resolution Key

`ModuleResolutionKey` uses domain `zom.module-resolution`, one zero byte,
and these fields in order: expanded requester `ModuleKey`, one-byte
`ModuleDependencyKind`, optional non-empty normalized module path, optional
`DependencyAlias`, and the complete `ModuleResolutionPolicyKey` canonical bytes
as an RFC 0011 length-prefixed byte string. `ModuleDependencyKind` tags are
`Import = 0x01`, `ForeignReexport = 0x02`, `ModuleAlias = 0x03`, and
`Prelude = 0x04`; no other tag is valid.

`Import`, `ForeignReexport`, and `ModuleAlias` require a present non-empty
normalized path. The path retains the complete normalized source path,
including its first segment. `dependencyAlias` is present exactly when
`DependencyAliasRoot(requester.crate(), firstPathSegment)` exists and its
canonical alias equals that first segment. Local lookup tests the complete path.
Alias lookup starts at the selected alias target and appends the normalized path
suffix after the first segment. Local and alias candidates enter one sorted
distinct candidate set; neither source has precedence.

A `Prelude` key has absent normalized path and absent dependency alias.
`ModuleDependencyRequests(ModuleKey)` emits exactly one `Prelude` key when
`ConfiguredPrelude(requester.crate())` is present and emits none when it is
absent. `ResolveModuleRequest` reads that projection to obtain the selected
`ModuleKey`. The selected prelude target is not encoded in
`ModuleResolutionKey`; changing it invalidates the query through the tracked
`ConfiguredPrelude` dependency.

The dependency-alias target is not encoded in `ModuleResolutionKey`. A request
with a present alias reads exactly
`DependencyAliasRoot(requester.crate(), alias)`, so changing the target
invalidates the result without replacing the request identity. Adding or
removing the alias changes the deduplicated request key produced by
`ModuleDependencyRequests`.

Requester ancestry, module-catalog buckets, module-search roots, configured
prelude selection, dependency-alias targets, generated-module inputs, and
current source or environment revisions are tracked query dependencies, not
fields of `ModuleResolutionKey`. The key contains no import site, `SourceSpan`,
`SourceFileKey`, `NodeId`, schema preorder ordinal, requested target,
environment fingerprint, or resolution receipt revision.

### Duplicate And Verification Rules

Identity discovery first publishes a revision-local pre-admission inventory:

```text
IdentitySyntaxSiteKey {
  module: ModuleKey,
  source: SourceFileKey,
  moduleSyntaxPath: Sequence<uint32>,
}

IdentitySyntaxSite {
  key: IdentitySyntaxSiteKey,
  range: SourceRange,
}

DuplicateBoundOccurrence {
  obligation: CanonicalBoundObligation,
  first: IdentitySyntaxSiteKey,
  duplicate: IdentitySyntaxSiteKey,
}

PreAdmissionIdentityCandidate {
  identity: DefinitionIdentityRecord | ImplIdentityRecord,
  overloadHeader: Maybe<OverloadHeaderAuthority>,
  site: IdentitySyntaxSiteKey,
  duplicateBounds: Sequence<DuplicateBoundOccurrence>,
}

ImplSourceOccurrenceKey {
  implementation: ImplKey,
  site: IdentitySyntaxSiteKey,
}

ImplIdentityOccurrenceGroup {
  implementation: ImplKey,
  authority: ImplId,
  occurrences: NonEmptySequence<ImplSourceOccurrenceKey>,
}
```

`moduleSyntaxPath` is the structural child-index path rooted at the detached
module syntax tree, so it addresses declarations and individual bound
occurrences, including module-level syntax that has no named-item owner. Every
key in a candidate resolves through the revision-local site inventory to
exactly one current range. It is never a public semantic query key and does not
enter the definition or implementation digest.

Obligation normalization retains the first source occurrence of each complete
canonical obligation. Every subsequent equal occurrence emits one
`DuplicateBoundOccurrence`; `T: Eq + Eq + Eq` therefore records two `W1204`
warnings. Each warning is primary at `duplicate`, has one
`PreviousDeclarationHere` secondary at `first`, and orders by the duplicate
site's canonical source order. Suppression affects rendering only, not the
revision-local occurrence inventory.

Candidates group by complete canonical identity record after digest collision
checks. Within a group, canonical source order is the tuple of encoded
`SourceFileKey`, `range.byteStart`, `range.byteEnd`, and encoded
`moduleSyntaxPath`. Grouping completes before handle admission. A definition
group with more than one record is source redeclaration: its first candidate is
the authority and later candidates receive no `DefId`. Every implementation
group, including a group with one record, admits exactly one semantic `ImplId`
for its first identity authority; no later candidate receives a distinct
`ImplId`. After that semantic freeze, every implementation source occurrence
receives its own revision-local `ImplOccurrenceId` under the shared authority.

For every definition candidate after the first, the diagnostic primary is that
later candidate and its sole secondary is the first candidate, matching RFC
0004 for groups of every size. Definition groups produce the applicable
`ZOM3003` through `ZOM3010` diagnostic plus
`ZOM3017 PreviousDeclarationHere`. NFC-equivalent definition spelling
collisions follow this source-diagnostic path.

Every implementation group publishes exactly one
`ImplIdentityOccurrenceGroup`. Its `implementation` and `authority` are the
single stable key and revision-local `ImplId` admitted for the complete identity
record. `occurrences` retains the first and every later site in canonical source
order; every element repeats the same `implementation`. The group is
revision-local, is not a `DiagnosticFact`, and cannot enter a coherence view.
Its verifier requires a non-empty, sorted, duplicate-free, same-module
occurrence sequence, one equal retained identity record, and exactly one active
authority handle. Source form does not select a group variant. An ordinary
`StandaloneImplDecl` and bodyless `MarkerImplDecl` with equal complete identity
records therefore coexist in one heterogeneous group independent of which form
appears first.

Each source occurrence receives a context-branded revision-local
`ImplOccurrenceId`. It is a dense materialization handle, not a semantic
identity, persistence key, provider root, or field of any stable codec. The
materialized implementation occurrence and Binder fact schemas are:

```text
MaterializedImplOccurrenceEntry {
  occurrence: ImplOccurrenceId,
  key: ImplSourceOccurrenceKey,
  authority: ImplId,
  node: NodeId,
  source: SourceSpan,
}

ImplBindingFact {
  occurrence: ImplOccurrenceId,
  authority: ImplId,
  node: NodeId,
  scope: ScopeId,
  members: SortedSequence<DefId>,
  source: SourceSpan,
}
```

`ImmutableDefinitionInventory.implementationOccurrences` is a bijection
between implementation syntax `NodeId` values and `ImplOccurrenceId` values
plus the dense occurrence entry inventory.
`ImmutableBindingMetadata.implementations` stores the corresponding
`ImplBindingFact` sequence. `ScopeOwner::ImplOccurrence(ImplOccurrenceId)`
owns every implementation-body scope and every nested scope that inherits that
source owner. Ordinary-body `ImplSelfOwner` carries the occurrence handle; the
verified occurrence entry supplies the shared `ImplId` only when a
successfully classified ordinary header is published. A bodyless occurrence
has one independently allocated empty `ImplBody` scope with its own occurrence
owner and never becomes an `ImplSelfOwner`. Two occurrence facts may neither
share a scope nor exchange their node, source, owner, or authority.

The `ScopeOwner::ImplOccurrence` alternative uses tag `0x03` and expands its
occurrence handle to the complete `ImplSourceOccurrenceKey` payload.
`SelfOwner::Impl` also uses tag `0x03` and expands its occurrence handle to
that complete key. Module and definition owner tags and payloads use the
canonical occurrence-owner codec.

Occurrence handles are issued independently within each frozen module after
identity grouping, in the canonical source order already defined for
`IdentitySyntaxSiteKey`; their zero-based slots are dense and gap-free. A handle
is branded by the binding input's semantic context and module and cannot be
compared across either boundary. Candidate canonical encoding never writes the
slot: every occurrence handle in an impl fact, scope owner, inherited scope
owner, or `ImplSelfOwner` expands to its complete `ImplSourceOccurrenceKey`.
This makes producer order irrelevant while preserving cheap revision-local
joins. A missing expansion, two handles expanding to one site, or one handle
expanding differently in two fact domains is an invariant.

The Binder verifier performs an independent schema traversal and requires a
bijection among source implementation nodes, occurrence entries, binding facts,
and impl-body scopes. It recomputes every `ImplSourceOccurrenceKey`, checks the
shared complete identity record and authority, and rejects missing, extra,
reused, swapped, cross-node, cross-site, or cross-module rows. Candidate fields
never select source form: the exact AST node independently proves ordinary
braced form or bodyless marker form. RFC 0015 signature reconstruction consumes
these occurrence facts and replaces both `VerifiedSourceImplHeader.impl` and
`VerifiedSourceMarkerHeader.impl` with
`source: ImplSourceOccurrenceKey, authority: ImplId`; their exact nodes,
bindings, scopes, members, and spans come from the occurrence fact and are
independently reconstructed from the tree.

Classification precedes collision materialization. Each occurrence that fails
interface signature construction, resolves an ordinary braced form to a marker
interface (`ZOM4088`), resolves a bodyless form to a behavior interface
(`ZOM4089`), or violates marker safety (`ZOM4091` or `ZOM4092`) emits its own
earlier RFC 0015 diagnostic and is removed. A successfully classified ordinary
occurrence enters the ordinary temporary header sequence; a successfully
classified marker occurrence enters the marker temporary header sequence. A
producer that places one occurrence in both sequences is invalid.

Ordinary temporary headers first group by expanded `ImplKey`, then canonical
source order. One surviving occurrence publishes the single `ImplHead` under
the shared `ImplId`. A group with more than one surviving occurrence emits
`ZOM4017` for every occurrence after the first, with exactly one `ZOM4071` note
at the first, and no member publishes an `ImplHead`. The complete verified trait
`DefinitionKey` and self `SemanticTypeKey` materialize the typed diagnostic
arguments; source text may not substitute. If header construction fails, only
its earlier source diagnostic is produced. A missing authority or failed typed
argument materialization is a fail-closed invariant.

Marker temporary headers sort by complete `MarkerFactKey`, expanded `ImplKey`,
and canonical source order of `IdentitySyntaxSiteKey`; they retain multiple
source occurrences sharing one stable key without inventing semantic
identities. RFC 0015's marker-only `ZOM4092`, orphan, and local-conflict stages
remain the sole classification authority. For every surviving same-
`MarkerFactKey` group, each occurrence after the first emits `ZOM4017` with
exactly one `ZOM4071` note at the first, and no member publishes a `MarkerFact`.
A unique survivor publishes explicit marker evidence under its shared
authority, using that survivor's source site for provenance.

For a heterogeneous identity group, these rules are applied per occurrence and
never by the first source form. Exactly one source form can survive interface-
kind classification for a fixed resolved interface: an ordinary form survives
only for a behavior interface, and a bodyless form only for a marker-only
interface. The survivor may publish under the shared authority even when the
first source occurrence failed classification; its node, scope, bindings, and
diagnostic or evidence provenance remain those of the surviving occurrence.
Producing both an `ImplHead` and a `MarkerFact` for one identity group is an
invariant.

The RFC 0018 identity diagnostic enum domains and canonical tags are:

| Enum domain | Variant | Tag |
|---|---|---|
| `IdentityDiagnosticPhase` | `IdentityAdmission` | `0x01` |
| `IdentityDiagnosticEmitter` | `DuplicateBound` | `0x01` |
| `IdentityDiagnosticEmitter` | `DefinitionIdentityCollision` | `0x02` |

The Checker coherence-emitter subdomain uses
`GeneralOverlap = 0x01`, `ExactIdentityCollision = 0x02`, and
`MarkerLocalConflict = 0x03`. It is encoded only
after `CheckerDiagnosticProducer::Coherence = 0x04`; the same numeric value in
another producer or emitter domain has no relation to these variants.

Each rejected definition candidate's `DiagnosticOccurrenceKey` is the
canonical tuple `ModuleDiagnosticRoot(candidate.site.module)`,
`IdentityDiagnosticPhase::IdentityAdmission = 0x01`, absent semantic owner,
`IdentityDiagnosticEmitter::DefinitionIdentityCollision = 0x02`, and the
complete `candidate.site` `IdentitySyntaxSiteKey` as its occurrence. Its
primary is
`DiagnosticLocation::Source(DiagnosticProvenanceKey::IdentitySyntaxSite(candidate.site))`;
its only secondary is
`DiagnosticLocation::Source(DiagnosticProvenanceKey::IdentitySyntaxSite(first.site))`.
Each `W1204` uses
the corresponding module diagnostic root, the same identity-admission phase and absent owner,
`IdentityDiagnosticEmitter::DuplicateBound = 0x01`, and the complete
`duplicate` `IdentitySyntaxSiteKey` as its occurrence. Its primary is
`DiagnosticLocation::Source(DiagnosticProvenanceKey::IdentitySyntaxSite(duplicate))`;
its only secondary is
`DiagnosticLocation::Source(DiagnosticProvenanceKey::IdentitySyntaxSite(first))`.
The
embedded `SourceFileKey` therefore prevents equal module syntax paths in
different source files from deduplicating. An implementation group is identified
by its complete revision-local `ImplIdentityOccurrenceGroup`. It has no
identity-diagnostic emitter because classification has not yet produced a
diagnostic occurrence.

Each eventual ordinary exact-collision `DiagnosticOccurrenceKey` is the canonical tuple
`ModuleDiagnosticRoot(first.site.module)`,
`CheckerDiagnosticStage::Coherence = 0x02`, absent semantic owner,
`CheckerDiagnosticProducer::Coherence = 0x04`,
`CoherenceDiagnosticEmitter::ExactIdentityCollision = 0x02`, and the complete
`ImplSourceOccurrenceKey` of that later candidate as its occurrence. Its primary
is `DiagnosticLocation::Source(DiagnosticProvenanceKey::IdentitySyntaxSite(later.site))`;
its only secondary is
`DiagnosticLocation::Source(DiagnosticProvenanceKey::IdentitySyntaxSite(first.site))`.
General overlap uses
`CoherenceDiagnosticEmitter::GeneralOverlap = 0x01` and its RFC 0005 canonical
impl-pair occurrence, so the two sources cannot deduplicate. Equal numeric tags
in different enum domains do not denote equal variants. Thus multiple later
candidates and duplicate bounds cannot collide or depend on worker order.

Each marker local-conflict occurrence uses the same module root, coherence
stage, absent semantic owner, and coherence producer, with
`CoherenceDiagnosticEmitter::MarkerLocalConflict = 0x03` and the later
`ImplSourceOccurrenceKey` as its occurrence. Its primary resolves through the
later occurrence's `IdentitySyntaxSiteKey`; its sole `ZOM4071` secondary
resolves through the first source occurrence's `IdentitySyntaxSiteKey`. This contract applies both
when the two source headers have different `ImplKey` values that resolve to one
`MarkerFactKey` and when an identity-collision group shares one `ImplKey`.

Only malformed records, an unequal outer or overload-header SHA-256 collision,
an impossible owner prefix, or verifier disagreement uses `ZOM9916` or another
compiler-invariant path.

RFC 0017 `DiagnosticProvenanceKey` gains
`IdentitySyntaxSite = 0x05 { key: IdentitySyntaxSiteKey }`.
`ResolveDiagnosticProvenance` reads the revision-local pre-admission inventory,
not `RevisionLocalDefinitionSites`, for this variant. This gives rejected
candidates unambiguous primary and secondary ranges without fabricating a
`DefinitionKey`, `ModuleSite.owner`, or named-item `LocalSyntaxPath`.

For each RFC 0015 `ZOM4091` projection, the classified source occurrence's
`IdentitySyntaxSite` resolves to the exact `impl` token range. Its RFC 0017
`DiagnosticOccurrenceKey` is the canonical tuple
`ModuleDiagnosticRoot(candidate.site.module)`,
`CheckerDiagnosticStage::Signature = 0x01`, semantic owner
`ImplementationOwner(the admitted ImplKey)`,
`CheckerDiagnosticProducer::SignatureClassification = 0x15`, and the complete
`candidate.site` `IdentitySyntaxSiteKey` as the occurrence. Its primary is
`DiagnosticLocation::Source(DiagnosticProvenanceKey::IdentitySyntaxSite(candidate.site))`.
It has zero display arguments, no notes, no recovery, and `itemOrdinal = 0`.
RFC 0015 remains authoritative for the source classification and exact order:
failed interface signature, then `ZOM4089`, then `ZOM4091`, then `ZOM4092`,
then marker orphan and conflict checks. A producer or verifier that emits
`ZOM4091` before marker-only classification, changes its anchor, or retains a
semantic marker publication after the error is invalid.

Module dependency requests deduplicate equal semantic keys while retaining a
non-empty ordered provenance sequence for every contributing site.

The producer and independent verifier may share the record type, field enums,
and canonical codec. They must not call the same AST normalization algorithm.
Mutation inventories cover every canonical field, every excluded provenance
field, owner-prefix validation, duplicate handling, and definition-collision
and `W1204` occurrences whose sites have equal `moduleSyntaxPath` values but
different `SourceFileKey` values.

### Current Core-Library Identity Contract

| RFC 0018 Surface | Current Contract |
|---|---|
| Module catalog and requester keys | `ModuleCatalogPathBucketKey`, `RequesterModuleAncestry`, `ModuleResolutionKey`, `IdentitySyntaxSiteKey`, and every value that expands `CrateKey`, `SourceFileKey`, or `ModuleKey` use the complete expanded identity bytes. |
| Compilation options | `CompilationOptions` is keyed by complete expanded `CrateKey`; every `ParseSource` request selects options through its source key's crate. |
| Definition and implementation query roots | Every query key, value record, retained collision record, occurrence key, import binding key, and provider root uses the current complete `DefinitionKey`, `ImplKey`, `EnclosingStableOwnerKey`, `GenericParameterKey`, `CallableParameterKey`, and derived `SemanticTypeKey` bytes. |
| Contextual named-item and body roots | Encode the complete context `CompilationRootSetQueryKey` before the stable definition, module, or body-owner key in every RFC 0019/0020 contextual query key; retain stable semantic identities inside values and regenerate all nested provider roots. |
| Root-set and graph keys | `ActiveCrates`, `ModuleGraph`, and `ModuleGraphScc` use exhaustive `CompilationRootSetQueryKey`. A `UserPackage` root uses tag `0x01` and its complete package payload; a `ToolchainCore` root uses tag `0x02` and its complete projected core crate. Package resolver queries remain package-keyed. |
| Dependency projections | `ToolchainCore` participates only in the semantic crate graph. Dependency-alias, lockfile, release, and package-resolution queries accept only user-package edges. `ConfiguredPrelude`, module dependencies, path buckets, and resolution queries consume exact projected-core keys without package fallback. |
| Core diagnostic identity | Encode `CoreLibraryDiagnosticRoot` as diagnostic-root tag `0x05` followed by the embedded expected distribution digest and canonical optional context fingerprint; encode exact producer, issue, coordinate, causes, emitter, and sorted occurrence index with no observed digest, host path, span, handle, or candidate-carried field. |
| Stable wire dumps and traces | Canonical query-key dumps, dependency records, collision fixtures, query traces, and fixed vectors cover both user-package and toolchain-core branches. Ordering uses complete current bytes, and traces never print local handles. |
| Mutation and architecture gates | Independent producer/verifier mutations cover both compilation-unit tags, missing or extra payloads, crate-parent substitution, source-origin substitution, dependency-origin substitution, root-set branch substitution, and every transitive query key. |

### Current Module-Topology Wire Contract

RFC 0026 defines the complete wire closure for selected-module catalogs, detached
dependency sites, dependency requests and failures, stable graph edges and
failures, SCC components, cycle failures, and the module-input ledger.

Each registered query kind owns its domain. Its typed `encodeKey` emits only
the complete canonical `CrateKey`, `ModuleKey`, or
`CompilationRootSetQueryKey`; it does not repeat the query-kind domain.
Standalone value decoders enforce only self-contained domains, framing,
ordering, uniqueness, bounds, tags, and embedded-value invariants.
Transaction verifiers and keyed query verifiers perform outer-key equality,
tracked-result equality, graph membership, and cross-input ownership checks.

`SelectedModuleCatalogInput` is the sole selected module-to-source input.
`SelectedModuleSource` is its derived narrow projection and retains the
accepted RFC 0019 consumer boundary. `ModuleDependencySiteInput` stores stable
source coordinates and no `NodeId`, span, handle, pointer, or borrowed AST
state. The exact domains, tags, sequence order, decode limits, and mutation
oracles are those in RFC 0026.

### Binder Query Identity Closure

The RFC 0027 acceptance transaction
`rfc0027-accept-20260727-e2f4ba5e` binds this contract to proposal SHA-256
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`.

The canonical Binder query keys are:

```text
StableDefinitionQueryKey {
  module: ModuleKey,
  definition: DefinitionKey,
}

StableImplementationQueryKey {
  module: ModuleKey,
  implementation: ImplKey,
}

StableImplementationOccurrenceQueryKey {
  module: ModuleKey,
  occurrence: ImplSourceOccurrenceKey,
}

StableGenericParameterQueryKey {
  module: ModuleKey,
  parameter: GenericParameterKey,
}

StableCallableParameterQueryKey {
  module: ModuleKey,
  parameter: CallableParameterKey,
}
```

Each key retains its complete authority record at verification and
materialization boundaries. Implementation generic parameters retain the
shared implementation authority and the complete equal-occurrence set.
Implementation-body scopes, declaration sites, and revision-local occurrence
handles remain occurrence-specific. The semantic-context arena admits global
handles only after exact tracked active-membership validation.

### Query Descriptor And Authority Identity Closure

Acceptance transaction `rfc0029-accept-20260727-8d393a0c` binds this current
contract to exact RFC 0029 proposal SHA-256
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`.

Every descriptor inventory row carries an explicit contiguous `uint32`
ordinal, and `QueryKindId` is exactly that ordinal within its target-specific
inventory. Registration sequence, hash, address, and link order have no
identity authority. A test target preserves the complete production inventory
as its prefix and appends one contiguous test-only tail.

`QueryDatabaseIdentity` is an opaque retained token whose equality is token
object identity. It has no canonical encoding and never contributes bytes to a
stable key, descriptor row, witness, diagnostic, trace, or persisted record.
The database-bound descriptor inventory is immutable. Registration proves one
descriptor-to-ordinal binding before evaluation, so capability decoding checks
only the independently reachable descriptor ordinal, database token, and
revision coordinates.

Each descriptor name and query domain is a non-empty printable-ASCII
`zc::LiteralStringConst` expressed with `_zcc` in its kind-specific metadata.
The capability failure envelope uses the literal domain
`zom.query.capability-failure`, frames the demanded descriptor's literal
domain, failure kind, and canonical payload, and requires exact canonical
re-encoding. The framed descriptor domain must equal the demanded descriptor's
domain.

Identity admission and active membership compare the complete canonical
authority bytes, including contextual roots, global key, owner, and occurrence
authority. Digest equality alone cannot authorize admission. Equal keys with
unequal complete authority records are canonical collisions and fail before
interner access.

### Identity-Site Provenance And Stable Admission

`IdentitySyntaxSiteInventoryQuery(StableModuleQueryKey)` is the independent
revision-local authority for every identity root, generic parameter, bound
occurrence, constant header expression, and other syntax node that
stable-identity production or verification may cite. Its provider reads
`SelectedModuleSource`, `ParseSourceQuery`, and
`IdentitySyntaxSiteInventoryProducer` in that order. Its verifier repeats the
two query reads and traverses the complete parsed topology through the separate
`IdentitySyntaxSiteInventoryVerifier`.

The descriptor-private witness contains the complete module and selected
source, the source digest, and a canonical sequence of
`{IdentitySyntaxSiteKey, schemaPreorderOrdinal, SourceSpan}` records. A legal
module with no sites encodes an empty sequence and has no fabricated root site.
The decoder reads the span's source key and byte bounds, requires agreement
with the outer witness and retained immutable source snapshot, requires exact
digest agreement, and reconstructs the span only through
`ImmutableSourceSnapshot::span`. Decode is bounded and fully consumed, and
re-encoding is byte-identical.

`ResolveDiagnosticProvenance(IdentitySyntaxSite(key))` demands the inventory in
the same snapshot and resolves exactly one matching entry. This lookup does
not depend on successful stable-identity admission. The inventory is published
before local stable-identity validation can reject source.

`StableIdentityAdmissionQuery(StableModuleQueryKey)` reads, in order,
`SelectedModuleSource`, `ParseSourceQuery`,
`IdentitySyntaxSiteInventoryQuery`, `CandidateProducer`, and
`CandidateVerifier`. Its published capability retains the parse
and identity-site leases. It is the sole source-diagnostic authority for
stable-identity validation and publishes no capability on rejection.

Selected-source absence is exactly
`MissingSelectedModuleSource(Module(key.module), none)`. Parse rejection is
forwarded unchanged. Candidate disagreement, malformed topology, witness
failure, source or digest disagreement, and provider/verifier disagreement are
runtime failures. The stable-admission diagnostics use
`DiagnosticProvenanceKey::IdentitySyntaxSite`; the independent rejection
verifier resolves every cited key through the already published inventory.

RFC 0027 `S1` and `S2` land atomically as one buildable schema-and-facts
transaction. `S3` then establishes bounded exact-consumption codecs and fixed
wire oracles. `S6` establishes the canonical Binder diagnostic payload after
the `S1` plus `S2` transaction. Query-runtime implementation begins only after
both `S3` and `S6` pass their focused gates. RFC 0018 remains
`IMPLEMENTING`; this synchronization records design authority only.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| Routing validation | `.agents/subagents/README.md`, `.agents/subagents/manifest.yaml`, `.agents/subagents/task-router.md`, `.agents/subagents/ir-backend.md`, `.agents/subagents/module-system.md`, `.agents/subagents/spec-audit.md`, `.agents/subagents/verification.md`, `AGENTS.md` | `task-router` |
| RFC governance | `docs/rfc/**` | `rfc` |
| Header syntax preservation | `products/zomlang/compiler/ast/**`, `products/zomlang/compiler/parser/**` | `lexer-parser` |
| Binder identities, semantic types, and Checker conversion | `products/zomlang/compiler/binder/binding-*`, `products/zomlang/compiler/binder/body-binding.*`, `products/zomlang/compiler/binder/closure-free-variables.*`, `products/zomlang/compiler/binder/definition-inventory.*`, `products/zomlang/compiler/binder/frozen-definition-inventory.*`, `products/zomlang/compiler/binder/import-binding.*`, `products/zomlang/compiler/binder/scope-arena.*`, `products/zomlang/compiler/binder/verified-bound-module-input.*`, `products/zomlang/compiler/binder/internal/binding-skeleton.h`, `products/zomlang/compiler/checker/**`, `products/zomlang/compiler/type/**` | `binder-checker` |
| Stable identity and module/session queries | `products/zomlang/compiler/identity/**`, `products/zomlang/compiler/query/**`, `products/zomlang/compiler/binder/module-*`, `products/zomlang/compiler/driver/**` | `module-system` |
| Diagnostic provenance | `products/zomlang/compiler/diagnostics/**` | `error-system` |
| Compiler build graph | `products/zomlang/compiler/CMakeLists.txt` | `ir-backend` |
| Design and spec alignment | `docs/design/**`, `docs/spec/chapters/03-types.md`, `docs/spec/chapters/06-declarations.md`, `docs/spec/chapters/09-interfaces.md`, `docs/spec/chapters/12-generics.md`, `docs/spec/chapters/17-grammar-reference.md`, `docs/spec/chapters/22-orphan-rule-and-coherence.md` | `spec-audit` |
| Tests, schema generators, and architecture gates | `products/zomlang/tests/**`, `scripts/generate-canonical-header-syntax-schema.py`, `scripts/generate-query-descriptor-schema.py`, `scripts/check-query-descriptor-architecture.py`, `scripts/check-identity-architecture.py`, `scripts/check-incremental-query-architecture.py` | `verification` |

## Security And Safety Impact

The proposal adds no unsafe execution or external data exposure. Its safety
impact is compiler correctness: false identity equality can reuse facts for the
wrong entity, while unstable identity can invalidate unrelated computations.
Closed variants, canonical validation, duplicate rejection, and independent
verification fail closed before semantic publication.

## Drawbacks And Risks

The replacement touches identity, Binder inventory, module resolution,
diagnostics, and tests in one coordinated series. Structural type and header
normalization are substantial and must be independently understandable by the
producer and verifier. Owner records add 33 bytes per lexical depth, and the
inventory must retain complete records alongside their compact digests to
detect collisions and validate owner prefixes.

The all-distinct/no-precedence module policy may expose ambiguous candidates
that a precedence-based resolver would select silently. This is intentional:
ambiguity is deterministic and safer than search-order semantics.

## Alternatives Considered

Embedding source spans or traversal ordinals would avoid structural header
normalization but would preserve false invalidation and violate RFC 0017.

Hashing source text slices would be simpler but would make formatting, comments,
body edits, and grouping artifacts part of semantic identity.

Encoding current alias or prelude targets in `ModuleResolutionKey` would make
key replacement duplicate the dependency graph. Tracked narrow inputs provide
the same invalidation with a stable semantic request.

RFC 0018 records this identity closure under its own acceptance hash and
review history.

## Compatibility And Rollout

The definition, implementation, and module-request key models form one
coherent contract. Every producer, verifier, registry, diagnostic, fixture,
canonical vector, and caller uses that contract.

RFC 0017 implementation proceeds only after the identity tests in this RFC
pass. The implementation transaction updates every producer, verifier, codec,
fixture, and caller together.

## Documentation And Teaching Plan

On acceptance, the identity, type-parameter, signature, receiver,
module-resolution, and diagnostic contract sections in RFC 0004, RFC 0005,
RFC 0008, RFC 0011, RFC 0014, RFC 0015, and RFC 0017 are synchronized with
this RFC. The RFC 0005, RFC 0014, RFC 0015, RFC 0017, and RFC 0018 trackers
record the dependency and replacement boundary. RFC 0005 names
`GenericParameterKey` in
`SemanticTypeKey`, receiver and ordinary `CallableParameterKey` positions in
public signatures, and the delayed typed coherence diagnostic. RFC 0014 remains
the sole receiver-semantics owner and records the retained `MethodDecl.mode`
AST input, receiver exclusion from ordinary parameter ordinals, and absent
lifecycle receiver encoding.

Identity and module-system design documents describe stable keys as semantic
records and provenance as revision-local data. The declarations, generics, and
coherence chapters are checked for semantic alignment; duplicate bounds remain
warnings and duplicate declarations or implementations remain source errors.
No language syntax change is required.

## Operational Readiness

The identity architecture gate must reject position-derived fields and
must run on the repository Python baseline. Sanitizer build, full unit and lit
tests, deterministic canonical vectors, producer/verifier mutation inventories,
and clean-build differential checks are mandatory before landing.

Schema and parser gates use the interpreter selected by CMake's
`find_package(Python3 COMPONENTS Interpreter REQUIRED)`. Configure performs an
`import yaml` probe with that exact interpreter and fails with an actionable
PyYAML installation instruction when the dependency is absent. CTest commands
invoke `${Python3_EXECUTABLE}` rather than a PATH-selected `python3`, so schema
generation, parser coverage, and architecture checks cannot silently run under
different Python environments.

## Acceptance Criteria

- All required owners approve one exact proposal snapshot.
- Owner-sum tags, ordering, length encoding, module equality, prefix recursion,
  cycle rejection, and linear `8 + 33 * n` growth have fixed canonical vectors
  and negative tests.
- `DefinitionKey`, `ImplKey`, and `OverloadHeaderDigest` implement the exact
  domains and field order defined here without spans or ordinals.
- `GenericParameterKey` and `CallableParameterKey` implement their exact
  domains, owner and position tags, record retention, ordinal or
  receiver-presence validation, semantic-type and signature use, narrow
  non-provider-root boundary, and dedicated revision-local handle
  materialization.
- Generic renaming is alpha-equivalent by binder depth and ordinal; header
  schema tags, field order, collection normalization, and ABI tags match the
  dedicated canonical inventory.
- Dynamic arrays and slices have distinct header-schema variants, canonical
  vectors, semantic types, and overload identities.
- Parser, AST, and specification expose exactly `T[]` as dynamic array, `[T]`
  as slice, and `[T; N]` as fixed array; postfix `T[N]` publishes no AST or
  identity candidate.
- Receiver spellings, omitted and explicit `unit`, default external `Cdecl`,
  and inline-versus-where obligations normalize to one representation; no
  unproduced semantic variant is admitted.
- Generic and associated-type `+` bounds retain distinct ordered bound-list
  nodes with per-member source ranges; ordinary type expressions use `&` for
  structural intersection and reject `+`.
- Positive bodyless marker implementations with short and qualified paths
  both retain `MarkerImplDecl` candidates in the recursive parser and ANTLR
  oracle. Each identity group retains one `ImplIdentityRecord`, `ImplKey`, and
  `ImplId` authority, while every source occurrence retains exact diagnostic
  provenance and reaches classification; only a successfully marker-only
  `Safe` occurrence emits `ZOM4091`, after `ZOM4089` classification and before
  `ZOM4092`, orphan, and conflict checks. The error retains no `MarkerFact`,
  explicit-marker module-interface entry, or coherence input. A valid
  `Unsafe` control publishes the RFC 0015 marker outputs.
- Every overload digest retains one equal complete header authority record;
  unequal inner or outer digest collisions fail as invariants before source
  collision grouping.
- Stable identity interners contain only eligible named items; owner-local and
  anonymous syntax never becomes a persistent query root.
- Every implementation source node has one independently verifiable
  `ImplOccurrenceId`, `ImplSourceOccurrenceKey`, binding fact, and impl-body
  scope under exactly one shared stable `ImplKey` and authority `ImplId`.
  Occurrence handles expand to complete occurrence keys in every Binder codec;
  no raw dense slot, source-form discriminator, span, or first-candidate choice
  enters stable identity.
- Heterogeneous ordinary and bodyless occurrences are classified independently
  before collision grouping. Interface kind selects the only valid survivor
  stream, not source order; one valid survivor may publish under the shared
  authority even when the authority site failed, and dual ordinary/marker
  publication is invalid.
- Duplicate bounds are key-neutral and produce one warning per later bound
  occurrence. Duplicate definitions produce one primary per later candidate;
  surviving ordinary identity occurrences produce one typed coherence primary
  per later occurrence after complete header reconstruction and publish no
  `ImplHead`. Marker source occurrences independently reach RFC 0015
  classification and produce one marker-local `ZOM4017` per surviving later
  occurrence after a valid marker header exists. Both conflict paths use only
  the first surviving declaration as secondary.
- `ModuleResolutionPolicyKey` and `ModuleResolutionKey` have fixed canonical
  vectors, fixed dependency-kind tags, exact requester/ancestor/root lookup,
  and exclude all current-state and provenance fields.
- Equal module requests deduplicate across sites while retaining independent
  ordered provenance records.
- Selected import and re-export namespace slots are created before target
  lookup, so export-surface changes update `ImportTarget` values without
  changing `ImportBindingKey` identities.
- Alias and prelude target changes invalidate through narrow tracked inputs
  without changing the semantic request key.
- `RequesterModuleAncestry` and `ModuleCatalogPathBucket` implement the exact
  key, value, equality, dependency-authority, verifier, reuse, and retention
  contracts defined here; unrelated catalog paths do not execute a resolution
  provider.
- Producer and verifier use independent normalization algorithms and pass the
  complete mutation inventory.
- Identity and incremental-query architecture gates reject every removed field
  and compatibility surface.
- RFC 0004, RFC 0005, RFC 0008, RFC 0011, RFC 0014, RFC 0015, RFC 0017, and the
  RFC 0005, RFC 0014, RFC 0015, RFC 0017, and RFC 0018 implementation trackers
  name one
  non-contradictory normative identity, parameter, receiver, diagnostic, and
  module-resolution contract.
- Sanitizer build, full tests, format, and scoped diff checks pass.

## Implementation Plan

1. Correct the parser AST to retain method mutability and absolute type-path
   roots, split string, character, and no-substitution-template literals,
   retain the single named dynamic principal directly, and remove producerless
   ABI and where-relation variants. Delete `GenericTypeParam.variance` and its
   parser producer while retaining registered rejection diagnostics before
   identity admission. Replace the singular generic bound with an ordered
   `TypeParameterBoundList`, retain associated-type bounds in a distinct
   `AssociatedTypeBoundList`, parse `+` only as bound conjunction in those
   contexts, and keep structural `&` intersections distinct. Delete
   `ArrayTypeExpr.len_expr`, reject postfix `T[N]`, and synchronize the
   dynamic-array, slice, fixed-array, existential-type, marker-implementation,
   and grammar-reference contracts. Positive marker implementations without
   `unsafe` must remain AST candidates for the checker-owned `ZOM4091`
   diagnostic for both short and qualified paths. Complete this closure before
   generating the dedicated header-syntax canonical wire inventory.
2. Add canonical structural syntax, overload-header, owner-sum, subordinate
   parameter, policy, module-resolution record types, and the two narrow module
   resolution dependency queries with fixed vectors.
3. Replace `DefinitionKey` and `ImplKey` and delete path, span, ordinal, and
   anonymous identity alternatives.
4. Split stable named inventories from owner-local and revision-local
   pre-admission candidate, occurrence-group, and provenance inventories. Add
   context-branded `ImplOccurrenceId`, occurrence entries, occurrence-keyed
   binding facts, occurrence-owned scopes, and their independent Binder
   verifier in the same replacement.
5. Replace CompilerSession identity production and the independent Binder
   verifier, then migrate RFC 0015 ordinary and marker reconstruction to
   per-occurrence classification before either survivor stream publishes.
6. Replace semantic module request keys and split site provenance from
   deduplicated requests.
7. Update registries, RFC 0005 semantic type and signature consumers, delayed
   Checker coherence diagnostics, and all fixtures.
8. Synchronize RFC 0004, RFC 0005, RFC 0008, RFC 0011, RFC 0014, RFC 0015,
   RFC 0017, their affected trackers, and identity/module-system design
   documentation.
9. Extend architecture gates and mutation inventories.
   The identity gate rejects restoration of singular generic bounds,
   reconstruction of `+` bounds as `IntersectionTypeExpr`, removal or merging
   of `AssociatedTypeBoundList`, and deletion of the ordinary `A + B` negative
   fixture or either of its AST and grammar expectations. The impl-source gate
   retains its marker-implementation and single-principal dynamic-type
   adversaries. Conformance CMake uses the configured Python interpreter and
   probes PyYAML during configure before registering schema-dependent tests.
10. Run sanitizer, full tests, format, and clean-build differential validation.

## Test Plan

- Build: `cmake --preset sanitizer` and `cmake --build --preset sanitizer`.
- Unit tests: identity, typed interner, Binder inventory, module dependency,
  module resolution, CompilerSession, diagnostics, and Checker fact suites.
- Identity mutations: alpha-renamed generic parameters, callable-label owner
  replacement, whole-subordinate-set replacement after an identity-relevant
  owner-header edit, ordinal changes under an unchanged owner, subordinate
  owner and provider-root rejection, owner depth,
  owner tag and prefix, header body and grouping edits, bound ordering and duplication,
  inline-versus-where equivalence, receiver spelling, omitted-versus-explicit
  unit, default ABI, absolute-versus-relative paths, parameter type changes,
  inner and outer record/digest collisions, two- and three-candidate declaration
  and implementation groups, invalid impl-header suppression, typed trait-
  conflict arguments, per-occurrence `W1204`, and every dedicated header-schema
  field. Definition-collision and `W1204` mutation cases place equal
  `moduleSyntaxPath` values in two distinct `SourceFileKey` values and verify
  that neither diagnostic occurrence is deduplicated.
- Header type mutations: `T[]` and `[T]` produce different canonical vectors,
  overload digests, and definition keys; producer and verifier mutations may
  not substitute either variant for the other. `T[N]` produces the registered
  syntax diagnostic and cannot publish a canonical header or definition key.
- Rejected syntax: `in` and `out` generic annotations both produce registered
  diagnostics and neither can publish a definition key or canonical header.
- Marker candidate admission: positive bodyless marker implementations without
  `unsafe` retain a `MarkerImplDecl` AST for both short and qualified marker
  paths, are accepted by the ANTLR grammar oracle, enter stable identity
  admission with the `Safe` tag, and reach the real signature checker. A
  marker-only interface produces exactly `ZOM4091` at the `impl` token with the
  specified occurrence and provenance and no marker, module-interface, or
  coherence publication. A bodyless behavior-interface control produces only
  `ZOM4089`; valid short and qualified `Unsafe` controls retain distinct
  `Unsafe` identity records and publish their expected marker,
  module-interface, and coherence outputs. Mutations that restore parser or
  ANTLR rejection, make path length select admission, suppress identity rather
  than semantic publication, emit `ZOM4091` before `ZOM4089`, alter the exact
  occurrence or provenance, remove either diagnostic fixture, or suppress
  valid `Unsafe` publication must fail the architecture inventory.
- Marker identity collisions: two and three byte-identical marker headers share
  one `ImplKey` and one authority `ImplId` but retain every source occurrence.
  Every occurrence has a distinct `ImplOccurrenceId`, binding fact, impl-body
  scope, node, source, and independently verified binding join; no scope or fact
  is shared or exchanged.
  Safe-positive and behavior-interface occurrences independently produce their
  earlier signature diagnostics. Surviving valid marker occurrences reach the
  marker temporary sequence, emit one `ZOM4017` per later source site with one
  `ZOM4071` at the first, and publish no `MarkerFact`. Mutations that route the
  group through an ordinary `ImplHead`, discard a later occurrence, issue a
  second `ImplId`, or sort equal keys without the complete source site fail the
  architecture inventory.
- Heterogeneous implementation identity groups: ordinary `{}` and bodyless `;`
  forms with the same complete `ImplIdentityRecord` are tested in both source
  orders against marker-only and behavior interfaces. The invalid form emits
  `ZOM4088` or `ZOM4089`; the valid later or earlier survivor alone publishes
  the correct `MarkerFact` or `ImplHead` under the shared authority with its own
  provenance. Missing, swapped, reused, or wrong-owner occurrence binding facts
  and scopes, first-form group routing, dual publication, and survivor
  provenance copied from the authority site are mandatory negative mutations.
- Bound-list syntax: `T: A + B` retains two ordered member nodes and source
  sites, `T: Eq + Eq + Eq` retains all three occurrences, permutation changes
  syntax order but not canonical obligation bytes, and inline `T: A + B`
  matches `where T: A, T: B`. Producer and verifier independently extract the
  members, and neither may substitute `IntersectionTypeExpr` for the bound
  list. Associated-type `type Item: A + B` retains the same per-member evidence
  in its distinct `AssociatedTypeBoundList` and likewise never produces an
  `IntersectionTypeExpr`.
- Bound-list rejection: an ordinary type expression `alias Bad = A + B;`
  must fail in the parser ztest, the real `zomc --dump-ast` negative fixture,
  and the ANTLR grammar matrix with its registered source diagnostic. Removing
  the source, AST expectation, or grammar expectation fails the architecture
  mutation inventory.
- Module mutations: all four dependency-kind tags, requester/self/strict
  ancestor/crate-root candidates, duplicate candidate paths, alias addition,
  alias retargeting, prelude addition and retargeting, and provenance-only site
  changes. A sparse ancestry fixture has only `app` and
  `app::area::child` as active modules, contains structural prefix
  `app::area`, accepts the chain without materializing an `app::area` handle,
  and continues to resolve candidates through exact catalog buckets. Mutations
  of an adjacent path, crate, declared root, or requester are rejected. An
  unrelated module-catalog path edit does not execute the resolution provider.
  Implementation occurrence-group verification rejects any occurrence whose
  module differs from the authority module. A `Low` durability ancestry or bucket
  change cannot be skipped by a `Medium` or `High` validation fast path, while
  an unrelated bucket change remains shielded from the resolution provider.
- Lit tests: `ctest --preset default -L lit --output-on-failure`.
- Conformance: full `ctest --preset default --output-on-failure`.
- Project-native schema, parser-coverage, and impl-source gates: `ctest --preset
  default -R
  '^(ast-generated-schema|parser-coverage|impl-source-architecture|impl-source-architecture-negative)$'
  --output-on-failure`. CTest invokes the CMake-selected interpreter after the
  configure-time PyYAML probe.
- Direct schema and parser equivalents in that configured environment:
  `python3 scripts/codegen/gen_ast.py --check` and `python3
  scripts/check-parser-coverage.py`.
- AST conformance: `ctest --preset default -L conformance-ast
  --output-on-failure`.
- ANTLR grammar conformance: `ctest --preset default -L conformance-grammar
  --output-on-failure`.
- Impl-source architecture: `python3
  scripts/check-impl-source-architecture.py --check` and `python3
  scripts/check-impl-source-architecture.py --self-test` using the configured
  interpreter.
- Generated files: the dedicated header schema, identity producer manifests, and
  mutation inventories are regenerated or validated by their architecture
  gates.
- Identity architecture: `python3 scripts/check-identity-architecture.py
  --check` and `python3 scripts/check-identity-architecture.py --self-test`.
- Incremental-query architecture: `python3
  scripts/check-incremental-query-architecture.py --check` and `python3
  scripts/check-incremental-query-architecture.py --self-test`.
- Format: `python3 scripts/check-format.py` and scoped `git diff --check`.

## Open Questions

None

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-07-18 | DRAFT | Initial follow-up draft closes RFC 0017 stable identity wire gaps. |
| 2026-07-18 | REVIEW | Complete stable identity wire contracts are ready for exact-snapshot owner review. |
| 2026-07-18 | RETURNED | Exact-snapshot review found owner-growth, schema, normalization, duplicate-provenance, resolution, precedence, and routing blockers. |
| 2026-07-18 | DRAFT | The returned snapshot was reopened for complete wire and diagnostic repair. |
| 2026-07-18 | REVIEW | Locked DRAFT audits closed subordinate identity, AST authority, import-slot, and delayed-diagnostic blockers. |
| 2026-07-18 | RETURNED | Exact-snapshot review found incomplete cross-source diagnostic occurrence identity. |
| 2026-07-18 | DRAFT | Complete identity syntax sites now distinguish equal structural paths in different source files. |
| 2026-07-18 | REVIEW | All required owners approved exact repaired DRAFT snapshot `65b168eb96e04e24ab3cfd1955589d5088a66b1750cb2d3298b232dc93f74361` for formal review re-entry. |
| 2026-07-18 | RETURNED | Exact REVIEW snapshot `c2ca364699af5c2166e37649f5949791d6789dc851f672b6fbfa5c27c569ce11` lacked an executable generic-bound-list AST and retained marker-impl and dyn specification drift. |
| 2026-07-18 | DRAFT | The returned snapshot reopened for generic-bound-list and specification closure. |
| 2026-07-18 | REVIEW | All required owners approved exact repaired DRAFT snapshot `e3b388a4b4258cc0f80441601136f0497ab2741c2037120c2e04fc83a5526b79` for formal review re-entry. |
| 2026-07-18 | RETURNED | Exact REVIEW snapshot `a5a211b4d52093aa2ba151f7144493d27e014b62ef76c701359b69e310d4a7cb` retained inconsistent parser, ANTLR, and specification admission for positive marker implementations without `unsafe`. |
| 2026-07-18 | DRAFT | Positive short and qualified marker candidates now share checker-owned `ZOM4091` validation. |
| 2026-07-18 | REVIEW | All required owners approved exact repaired DRAFT snapshot `58826663bacaf7dba9aca97c8a86d0a34549133af5b7f47560f73e0e4580e43b` for formal review re-entry. |
| 2026-07-18 | ACCEPTED | All required owners approved exact REVIEW snapshot `bdcbee8761d5476822cbe5bb2548332ad36e4d5f507c38e74d06751c6f444379`. |
| 2026-07-18 | IMPLEMENTING | Stable identity, occurrence binding, and module-resolution direct replacement started. |
| 2026-07-25 | IMPLEMENTING | Synchronized the accepted RFC 0025 compilation-unit identity expansion, contextual roots, semantic core graph, diagnostic identity, regenerated wire, and no-fallback mutation contracts from exact proposal SHA-256 `4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`; implementation completion is tracked only by the RFC 0025 R25 tasks. |
| 2026-07-26 | IMPLEMENTING | Synchronized the accepted RFC 0026 selected-module, dependency-site, request, failure, graph, SCC, cycle, and ledger wire closure plus standalone-versus-keyed verification boundary from exact proposal SHA-256 `39df5d3f11dbdcb2e95056b1cd14fd5220a19688f31a3e3180230ad465a3f84d`; implementation completion remains tracked by RFC 0026 and RFC 0025. |
| 2026-07-27 | IMPLEMENTING | Synchronized the RFC 0027 Binder query-key, complete authority-record, occurrence-specific scope, arena admission, and materialized occurrence contracts through transaction `rfc0027-accept-20260727-e2f4ba5e` at proposal SHA-256 `e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`; implementation status is unchanged. |
| 2026-07-27 | IMPLEMENTING | Synchronized the RFC 0028 explicit descriptor ordinal, literal query and capability-failure domains, complete canonical authority equality, collision, and target-inventory identity contracts through transaction `rfc0028-accept-20260727-944b68ff` at proposal SHA-256 `944b68ffc0aff5576d079a243ff092d7d19fba5ffed65551dda8e68adf230db4`; implementation status is unchanged. |
| 2026-07-27 | IMPLEMENTING | Acceptance transaction `rfc0029-accept-20260727-8d393a0c` synchronized opaque database-token identity, descriptor-ordinal/database/revision decoder coordinates, independently published identity-site provenance, retained-snapshot `SourceSpan` decoding, stable-identity admission, and the schema-before-runtime dependency order to proposal SHA-256 `8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`; implementation status is unchanged. |
