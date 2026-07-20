# RFC 0015 Review And Implementation Tracker

This document records discussion, decisions, and implementation evidence for
RFC 0015. It does not approve the proposal. RFC 0015 frontmatter remains
authoritative for status and approvers. The most recent formal review returned
exact proposal SHA-256
`a85a948901ecd95e4ceefe7a729f9ef4f0e006498e1875b050260c578569ab4b`.
The accepted proposal was approved in `REVIEW` at exact SHA-256
`642836225d54f6fa28f8c27e9985972081dbd221c2e8f3e61a0aafd04fe9bb1e`.
The metadata-only acceptance transition produced exact accepted-file SHA-256
`9704d5651606e8a74034c8af4be5172b4007a6c9f0ee8ea2f5ee183223401c01`.
Implementation is authorized without a compatibility path or partial schema
cutover.

## Discussion Record

### 2026-07-16 Codec Closure Audit

The RFC 0005 implementation audit confirmed two normative blockers:

- `CheckerDisplayArgument::Operator(OperatorKind)` references an undefined
  algebra, so four checker diagnostics lack canonical tags, encoding,
  rendering, and verification; and
- `TypeKeyPattern` is described only as a prose mirror of `TypeKeyNode`, with
  no closed records or tags, and it does not account for RFC 0014
  `InterfaceSelf`.

The repository process forbids silently rewriting an accepted or implementing
proposal. The selected path is RFC 0015, a hash-bound additive overlay over the
exact RFC 0004, RFC 0005, RFC 0008, RFC 0009, RFC 0011, RFC 0013, and RFC 0014
proposal snapshots.

The draft follows official Rust compiler and language references, the Swift
ABI mangling grammar, the Zig language reference and compiler intern pool, and
LLVM bitcode versioning. It defines complete algebras, explicit numeric tags,
direct revision cutovers, independent preimages, and fail-closed validation.

### 2026-07-16 Formal Review Entry

RFC 0015 entered formal review at proposal SHA-256
`1528352ccdcc84bbc44cb9efeda2b70ece8401a63655a6eaf775f68a2b40439c`.
The six bound base proposal hashes were reproduced from the current repository
and match the exact values in the proposal. `python3 scripts/check-rfc.py`
passes for all fifteen proposal RFCs. Every cited official Rust, Swift, Zig,
and LLVM prior-art link was retrieved successfully. Independent shell
reproduction matched all four operator vectors, all four type-key pattern
vectors, and the 172-byte signature v1, 105-byte coherence v1, and 282-byte
module-interface v3 SHA-256 oracles. No compiler implementation is authorized
until every
required owner approves this exact snapshot and the RFC moves to `ACCEPTED`.

### 2026-07-16 Formal Review Return

The `binder-checker` owner returned exact proposal snapshot
`1528352ccdcc84bbc44cb9efeda2b70ece8401a63655a6eaf775f68a2b40439c`.
The other in-flight reviews were stopped because every approval is
snapshot-bound and could not authorize a revised proposal.

The return identified two blocking counterexample families:

- parameters inside sorted collections can reorder or deduplicate after
  substitution, so positional comparison can miss overlaps in unions,
  intersections, and existential additional-interface sets; and
- RFC 0005 union and intersection normalization can flatten, remove identity
  elements, apply absorbers, collapse duplicates and singletons, and therefore
  change the pre-substitution outer tag, arity, and candidate head.

The revised draft rejects parameters recursively from union and intersection
members and existential additional-interface arguments. It also rejects a
parameterized existential principal when an additional interface has the same
interface definition, because substitution could make the two complete
instantiations equal. For every admitted published pattern, substitution now
preserves normalized structure, collection uniqueness, and the derived
concrete head. Outer `Parameter` remains `Blanket`, and `Blanket` must be
compared against every concrete head. A fresh formal review requires a new
exact proposal hash; the returned snapshot has no approvals.

### 2026-07-16 Draft Repair Audit

An independent counterexample audit found that principal `I<P>` and rigid
additional `I<Bool>` can unify at `P = Bool` but then violate the canonical
principal/additional distinction. The draft now rejects that same-definition
case before head derivation or overlap insertion. It also names the exact sort
and uniqueness key for object fields, union and intersection members,
additional interfaces, markers, and associated bindings; defines fixed-point
variable dereferencing and deterministic variable-to-variable binding; and
defines both `Blanket` insertion directions and canonical candidate ordering.
The test plan requires bounded ground-substitution model checking of the
admitted-pattern unifier.

### 2026-07-16 Complete Impl-Pattern Repair

A second draft counterexample audit found that self-only pattern unification
cannot distinguish or relate implemented-interface arguments correctly.
`impl<T> I<T> for S` must conflict with `impl I<i32> for S`, while
`impl I<i32> for S` and `impl I<bool> for S` are disjoint.

The revised proposal replaces the separate `ImplHead.interface` and
`ImplHead.selfPattern` fields with one canonical `ImplPattern` containing
interface arguments and self in a shared parameter space. Coherence groups by
interface `DefId` and substitution-stable self head, then unifies interface
arguments and self under one substitution. Two independently reproduced
impl-pattern vectors bind the new codec. The publication validator also rejects
generic parameters that occur only in where-constraints or associated
bindings, because selection cannot infer them.

### 2026-07-16 Complete Impl-Head Codec Repair

A third draft audit found that replacing two non-adjacent RFC 0005 fields did
not uniquely determine the complete `ImplHead` field order, and that the empty
or opaque integration fixtures could not detect an omitted or misplaced
pattern. It also found that imported facts needed an exact reconstruction check
between `pattern.self` and `selfType`.

The proposal now redeclares every `ImplHead` field in exact order and stores a
framed `ImplPatternKey` byte string. The independent verifier decodes and
reproduces that key, validates interface kind and arity, replaces shared pattern
parameters with the declaration-ordered generic `DefId` values, canonicalizes
the result, and requires structural equality with `selfType` before recomputing
the self head. One 117-byte standalone impl-head oracle and non-empty 269-byte
signature, 195-byte coherence, and 390-byte module-interface integration
oracles were independently reproduced from their listed preimages.

### 2026-07-16 Canonical Pattern Round-Trip Repair

A fourth draft audit supplied a decodable but semantically non-canonical
interface argument `Union([Never, Bool])`. Decode and byte reproduction alone
preserved that key even though semantic canonicalization reduces it to `Bool`,
which could miss overlap or place a self pattern in the wrong head bucket.

The independent verifier now inverse-substitutes the complete impl pattern,
canonicalizes every interface argument and self type through the semantic type
store, replaces the same impl-owned parameters, and requires the reconstructed
`ImplPatternKey` to be byte-identical. A tag, arity, order, member-set,
principal/additional, or nested-structure change is
`CanonicalCodecMismatch`. Only that byte-identical reconstructed value may
enter coherence.

### 2026-07-16 Self-Head Width Repair

A fifth draft audit found that RFC 0011 sequence counts are `uint64`, while
tuple, function, union, and intersection self-head arities are `uint32`. The
proposal now rejects an outer self count above `UINT32_MAX` before head
derivation. An encoded impl head fails with `CanonicalCodecMismatch` before
child allocation; an in-memory producer candidate fails with `InvalidFact`.
No narrowing, wrapping, saturation, partial fact, or coherence bucket is
permitted, while nested and interface-argument sequences retain their RFC 0011
width subject to independent resource limits.

### 2026-07-16 Formal Review Re-entry

RFC 0015 re-entered formal review at proposal SHA-256
`89c13512641a72035bf4a0ca79e23b97d9ae066eb81faa12362489584942d940`.
The independent draft audit approved the immediately preceding substantive
snapshot after confirming complete impl-pattern closure, canonical round-trip
verification, exact self reconstruction, width-safe head derivation, and the
`Blanket` comparison rules. The transition to `REVIEW` changed only RFC process
metadata and produced the exact review snapshot above.

Independent reproduction matched the complete operator and pattern vectors,
the 100-byte impl-head record, the 117-byte impl-head envelope with SHA-256
`a504e3a755a6951b4d3b09a71e5de17751452cd8549f314fd49ca90f654a1e19`,
the 269-byte signature integration envelope with SHA-256
`d4f64e0e3b4f1916abfb1eea5856bacb1e5f00f6b5ede1d9906a61e6c498e420`,
the 195-byte coherence integration envelope with SHA-256
`191ea6d0307def473535056bb9fc23b760c3e73ebddcb158e8814193e24c551e`,
and the 390-byte module-interface integration envelope with SHA-256
`040e3cc2bf2f076f38310a966eb455890e8b5e6816210ef12a7da4d9270f8882`.
`python3 scripts/check-rfc.py` and `git diff --check` pass. No implementation
is authorized until all six required owners approve this exact review
snapshot.

### 2026-07-16 Second Formal Review Return

The `rfc` owner returned exact proposal snapshot
`89c13512641a72035bf4a0ca79e23b97d9ae066eb81faa12362489584942d940`
because the Open Questions section still called the proposal a draft while its
frontmatter and status history said `REVIEW`.

The `binder-checker` owner returned the same exact snapshot with two semantic
blockers. First, the independent `ImplHead` verifier proved internal agreement
among `pattern`, `selfType`, and `head`, but did not reconstruct and compare the
exact implemented interface and self type from the verified bound source impl
header. A candidate could therefore replace `I<T>` with a same-arity `J<T>` or
replace both self representations consistently and enter the wrong coherence
bucket. Second, the overlay removed `ImplHead.interface`,
`ImplHead.selfPattern`, and `ImplTypePattern` without replacing their normative
consumers in RFC 0005 orphan checking, RFC 0008 coherence indexing, and RFC
0014 `Self::Item` projection.

The `module-system` approval of the returned snapshot is void because every
approval is exact-hash bound and the proposal must change. All required owners
must review a later exact snapshot after source-header provenance and every
cross-RFC consumer are closed.

### 2026-07-16 Source Provenance And Interface-List Repair

The revised draft makes the signature verifier independently traverse the
exact verified bound tree and reconstruct every impl interface clause, self
type, generic list, constraint set, safety value, associated binding, and span.
It requires a source-to-candidate bijection and rejects a same-arity interface
substitution or a jointly forged self pattern and semantic type.

The repair also closes the source grammar's complete interface bound list. One
source `ImplId` may publish several non-marker impl-head records and several
marker facts. Every directly embedded impl-head collection is now a canonically
sorted unique sequence rather than a map keyed only by `ImplId`; RFC 0008's
derived coherence index uses private verified sequence indices. The RFC 0005
orphan rule, RFC 0008 pattern matcher, and RFC 0014 impl-owner projection now
consume only source-matched reconstructed values.

### 2026-07-16 Singular Conformance And Marker Closure Repair

Two adversarial audits returned draft snapshot
`661b3ffefa4112c90b76cac18f3ad013f641811f890d425b6f35cbe288e30f21`.
The proposed multi-head sequence solved storage cardinality but not semantic
identity. `ImplResolution`, witness arguments, associated projections, dyn
evidence, selected callables, and contextual impl `Self` all retained one
`ImplId` with no interface-clause identity. The same draft also lacked a
generic marker pattern and could not reconstruct a marker fact's subject from
its exact source declaration.

The repair follows the mature one-trait-per-impl coherence model. An ordinary
source impl has exactly one non-marker behavior interface and publishes one
positive `ImplHead` under its `ImplId`. A marker impl is a separate bodyless
semicolon declaration, has no type parameters, where clause, body, associated
bindings, and publishes one explicit `MarkerFact` under its concrete
`(marker, subject)` key. Its AST retains `is_unsafe` solely for independent
signature classification and provenance; safety does not enter marker-fact
identity or payload. Positive explicit marker evidence requires
the `unsafe impl Marker for Type;` source form; negative evidence requires the
safe `impl !Marker for Type;` source form. Generic and conditional marker
evidence is rejected until a future RFC defines a pattern-based marker
selector.

The repair binds the exact RFC 0004 proposal snapshot, adds the `lexer-parser`
owner, removes `ImplHead.polarity`, retains every `implHeads` field as
`SortedMap<ImplId, ImplHead>`, retains RFC 0008 `ImplId` coherence buckets,
prevents marker impls from entering RFC 0014 `ImplSelfOwner`, removes the
redundant `ImplIfaceList.n_ifaces` field, and defines exact parser and checker
diagnostics for every source-shape mismatch. Independent recomputation produced
the 99-byte impl-head record, 116-byte impl-head envelope with SHA-256
`9b223c2528e292b03af2fabc463afb61a281b62ff40f82944376b421b916064e`,
268-byte signature integration envelope with SHA-256
`79abfad1573526c4994a967b8abb863a64b717d09739796488bab32567d77e32`,
194-byte coherence integration envelope with SHA-256
`f93f7bf3fedaed249cc4c9598840b05a3f871323f584eecd45f37b775776de02`,
and 389-byte module-interface integration envelope with SHA-256
`1a9616a65e070d7a592648f89f599b12267ce87d3b91a3d8c7be6e4c1ec5b04a`.
The repaired proposal remains `DRAFT` at exact SHA-256
`1c5d491009c70abc060e16d68f748be62b71b6995012e5ff42938cc5fc34a443`.

### 2026-07-17 Marker Safety, Evidence, And Diagnostic Closure

Final adversarial passes found that the preceding draft still lacked exact
rules for marker safety acknowledgement, generic marker classification,
duplicate same-key source facts, empty binder scopes, structural/builtin
evidence projection, body rejection, and canonical checker diagnostic facts.

The repaired draft retains `MarkerImpl.is_unsafe` as source provenance.
Positive explicit marker evidence requires `unsafe`; negative evidence rejects
it. Marker shape is computed over the verified transitive parent closure, and
generic marker-shaped interfaces fail with `ZOM4090`. Local explicit marker
headers and cross-module marker projections remain sorted streams until
same-key conflict and evidence precedence are resolved. Explicit evidence
suppresses provisional structural evidence, builtin evidence rejects explicit
source replacement through `ZOM4092`, and only one verified record per key may
enter the final map. Marker impls retain one exact empty RFC 0004 impl scope and
no contextual `Self`.

The parser and checker registries now close `ZOM2100-ZOM2104` and
`ZOM4088-ZOM4092`, including exact source precedence, producer tags, anchors,
emitter ordinals, recovery policy, and cross-family precedence. The language
RFC classification, lexer-parser owner, spec-alignment matrix, AST removal
gates, explicit-marker oracle, and all revised impl-head oracles are included.
Cross-module orphan and conflict diagnostics now use an ephemeral
span-addressed `CoherenceFailureRef`, so frozen module interfaces never need or
persist tree-local `NodeId` values.
`python3 scripts/check-rfc.py` and `git diff --check` pass. The quiescent draft
snapshot submitted for final pre-review audit is SHA-256
`170266253c10744c4b157eb3c0a93b55816b83d6ede11de7d14b9c5980be589d`.

### 2026-07-17 Marker Policy, Coherence Ownership, And Lineage Closure

Two independent audits returned the preceding draft because its marker orphan
decision was split between signature and coherence, its span-addressed
coherence failures replaced the base observable order, and RFC 0005's
derivability and builtin-marker tables had no authoritative producer.

The repaired draft makes the frozen-interface coherence builder the sole owner
of ordinary and marker orphan decisions. `CoherenceFailureRef` keeps an
internal impl-key deduplication identity while preserving package, crate,
module, source-span, and diagnostic ordering before its deterministic impl-key
tie-break.

Marker shape now has one pre-signature `VerifiedMarkerShapeInventory`; the
registry builder and signature checker consume that same capability. A
compiler-distribution configuration reached only through verified prelude
provenance authorizes exact structural subject classes, primitive builtin
facts, and one required marker per reference mutability. Function, raw pointer,
dynamic array, slice, class, and recursive support cycles fail closed. Exact
component steps cover references and enum payloads. The policy revision is a
direct parent of signature facts, complete module-interface v3, every
`CoherenceModuleInput`, and coherence view v1.

Independent recomputation matched the 80-byte shape inventory, 81-byte policy
configuration, 172-byte registry, standalone policy/component records,
204/300/201-byte signature vectors, 137/226-byte coherence vectors,
314/341/421-byte module-interface vectors, and the actual
configuration-to-coherence revision chain. `python3 scripts/check-rfc.py` and
`git diff --check` pass. The exact draft snapshot submitted for locked final
pre-review audit is
`63501e6a461c3ddbcf0b186062e6876e4d8f3047c81bb9cd34130bd329e5b484`.

### 2026-07-17 Structural Reconstruction And Marker Orphan Closure

Two locked draft audits returned the preceding snapshot. The frozen global
coherence input had no semantic-type store, nominal-layout inventory, or AST,
so it could not independently reconstruct structural and builtin marker
records. The same snapshot referred to marker orphan diagnostics without an
executable marker locality predicate, and its `uint32` component paths did not
define admission from RFC 0011 `uint64` tuple and enum-payload sequences.

The repaired draft assigns subject-shape and complete component reconstruction
exclusively to module-interface verification, which has the verified semantic
type store and authorized nominal signatures. Global coherence consumes only
exact verified projections, applies evidence precedence, and computes a
deterministic least positive support closure. Canonical candidates with
unavailable support or an unseeded cycle remain underived; corrupt projection
fields still reject at the module-interface boundary.

The final tree-local coherence-admission substage of signature verification now
owns the complete explicit-marker orphan predicate before local same-key
conflict grouping and before any unique-key marker map is constructed. It
admits only a locally owned marker or a normalized outer nominal owned by the
current module; structural and builtin evidence never undergo orphan checking.
Tuple and enum payload ordinals above `0xffffffff` produce no structural
candidate and are never narrowed. Existing wire records and golden vectors are
unchanged.

A fresh audit returned snapshot
`cf8b3738d0c5a01059f985fb5af2ba05e281aff93ea0948a39d17eb59ff551fb`
because the overlay precedence list did not replace RFC 0005's support-cycle
rejection and because the tree-local admission phase was described after a
unique-key verified signature map already existed. The final repair explicitly
replaces those RFC 0005 support clauses and places source reconstruction,
`ZOM4092`, marker `ZOM4054`, local `ZOM4017`, map construction, and verified
signature publication in executable order.

`python3 scripts/check-rfc.py` and `git diff --check` pass. The exact repaired
draft snapshot submitted for a fresh locked audit is SHA-256
`faf82e0379bc2f6f7683ceb77f3b03dcfba64d8549e6195881c8e90057fd5f71`.

### 2026-07-17 Demand-Driven Marker Proof Cutover

A final pre-ballot audit returned snapshot
`faf82e0379bc2f6f7683ceb77f3b03dcfba64d8549e6195881c8e90057fd5f71`.
An eager finite structural map had no deterministic complete subject universe:
a producer could omit a structurally eligible tuple and no verifier could
distinguish that omission from a type that had not yet been interned. The same
unique-key map could not carry an explicit and provisional structural record
for one key so that a later global phase could apply precedence.

The repaired draft replaces eager structural and builtin publication with one
demand-driven `MarkerProofEngine`. Signature, module-interface,
coherence-input, candidate, and frozen-coherence marker maps contain explicit
facts only. The engine is constructed from one verified body-checking input and
the matching marker-policy registry, and resolves any requested semantic type
in exact explicit, builtin, then structural order. It reconstructs nominal and
aggregate components from immutable semantic types and authorized signatures,
uses tri-state depth-first cycle rejection, and independently verifies every
positive, negative, or unsatisfied result. Query types need not appear in a
module signature or any pre-registration inventory.

Structural and builtin `MarkerFact` encodings remain byte-comparable ephemeral
proof records but never enter a revision preimage or cache artifact. Therefore
the v1/v3 record shapes and existing golden vectors remain unchanged while the
invalid finite coverage and same-key transport requirements disappear.
`python3 scripts/check-rfc.py` and `git diff --check` pass. The exact repaired
draft snapshot submitted for a fresh locked audit is SHA-256
`2c9dd02e5a2b34152ee8c3425b2618d096783d00ae14390a09b5fd25ec70c916`.

That locked audit returned the snapshot because a generic nominal component
such as `Vec<T>` inside `Box<I32>` might not already have a semantic identity.
The repaired engine now validates the nominal parameter-to-argument mapping,
recursively substitutes every field and enum-payload type, canonicalizes it,
and interns it through one same-session linearizable capability before proof
construction. The independent verifier repeats that work and never trusts a
producer-selected component ID. Explicit positive results now also require an
exact frozen-fact match. Cache rules exclude explicit and invariant results and
make `NotPositive` stable under later unrelated interning.

`python3 scripts/check-rfc.py` and `git diff --check` pass. The exact repaired
draft snapshot submitted for a fresh locked audit is SHA-256
`a40aa3686390be48284bc243de6ef6414e37b04cefae63bc27af77b4d9f5daa0`.

That audit approved generic component instantiation but returned the shared
`Visiting` state because another worker's in-flight proof could be mistaken for
a recursive cycle. The repaired algorithm gives each root invocation a private
active-key stack. An optional shared memo publishes only immutable completed
positive proofs or stable `NotPositive` results, never in-flight state.
Concurrent misses may recompute independently, while producer and verifier use
separate stacks and memo storage. Barrier-driven tests cover equal keys,
overlapping dependencies, and genuine self or mutual cycles.

`python3 scripts/check-rfc.py` and `git diff --check` pass. The exact repaired
draft snapshot submitted for a fresh locked audit is SHA-256
`2dd6abf934b0f4a9097faa96f9aa5799293d3c9d2b0e2203c430d672149fb6ac`.

### 2026-07-17 Formal Review Entry For Demand-Driven Proof

Three locked draft audits approved exact draft snapshot
`2dd6abf934b0f4a9097faa96f9aa5799293d3c9d2b0e2203c430d672149fb6ac`.
RFC 0015 then entered formal review at exact proposal SHA-256
`a530db93fbee7c8567189af185e83f998df08a228658dc2bf4701ba327c2f714`.
All seven required owners must approve that exact review snapshot. Any return
voids every approval and requires a new draft and review hash. Implementation
remains blocked until the frontmatter moves to `ACCEPTED`.

### 2026-07-17 Formal Review Return For Operator Alignment

Owners `rfc`, `lexer-parser`, `binder-checker`, `module-system`, and
`error-system` approved exact review snapshot
`a530db93fbee7c8567189af185e83f998df08a228658dc2bf4701ba327c2f714`.
The `spec-audit` owner returned it because the operator drift gate omitted the
lexical chapter, expressions chapter, and lexer grammar. Every preceding
approval is void; `verification` was not asked to review the returned snapshot.

The repaired draft adds an exhaustive operator-alignment gate across spec
chapters 02, 04, and 17, both ANTLR grammars, the hand lexer and parser,
generated AST operators, the closed checker conversion and renderer, and
conformance. It also expands the overall spec-alignment matrix to chapters 02,
03, 04, 06, 09, 14, 16, and 17. `python3 scripts/check-rfc.py` and
`git diff --check` pass. The exact repaired draft snapshot is SHA-256
`9d1a1edc580a428bf5555c6bf73bef693862b96814240d242c0322a4144f8004`.

A locked draft audit then found that Chapter 11's normative `?!`, `!!`, and
`PostfixSuffix` definitions were absent from the new operator matrix. The final
repair adds `docs/spec/chapters/11-error-handling.md` to the acceptance
criterion, dedicated operator gate, and complete spec-alignment matrix. The
exact repaired draft snapshot is SHA-256
`dd39f9f4715b87052ddea4619e794f167802b50eb119a7b68311e491d3ad1407`.

Three locked draft audits approved that exact repaired snapshot. RFC 0015
re-entered formal review at exact proposal SHA-256
`a85a948901ecd95e4ceefe7a729f9ef4f0e006498e1875b050260c578569ab4b`.
Every required owner must review this new hash; no approval from the returned
`a530db93...` snapshot survives.

### 2026-07-17 Formal Review Return For Marker Survivor Coverage

Owners `rfc` and `lexer-parser` approved exact review snapshot
`a85a948901ecd95e4ceefe7a729f9ef4f0e006498e1875b050260c578569ab4b`.
The `binder-checker` owner returned it because the persisted explicit-marker
map was compared with source-valid headers rather than the exact headers that
survived `ZOM4092`, marker `ZOM4054`, and local `ZOM4017`. Every approval is
void; the other owners were not asked to review the returned snapshot.

The repaired draft defines an independent final-survivor map and requires an
exact bijection with persisted explicit facts. A missing survivor is
`MissingRequiredFact`; a fact for a builtin-conflict, orphan, or local-conflict
rejection is `AdditionalFact`. Mutation tests cover missing survivors, rejected
whole same-key groups, and jointly replaced keys and payloads. The exact
repaired draft snapshot is SHA-256
`529f97352aa2d52c07232a7c4bce16fce531c551e4e89f88048c8eac9a384cc7`.

Three locked draft audits approved that exact repaired snapshot. RFC 0015
re-entered formal review at exact proposal SHA-256
`642836225d54f6fa28f8c27e9985972081dbd221c2e8f3e61a0aafd04fe9bb1e`.
All seven owners must approve this new snapshot; no earlier approval survives.

### 2026-07-17 Acceptance

The `rfc`, `lexer-parser`, `binder-checker`, `module-system`, `error-system`,
`spec-audit`, and `verification` owners independently approved exact review
snapshot
`642836225d54f6fa28f8c27e9985972081dbd221c2e8f3e61a0aafd04fe9bb1e`.
The verification owner reproduced all seven bound base hashes and all 31
declared vectors, lengths, and SHA-256 values. The metadata-only transition to
`ACCEPTED` produced exact accepted-file SHA-256
`9704d5651606e8a74034c8af4be5172b4007a6c9f0ee8ea2f5ee183223401c01`.

### Review Queue

| Owner | State | Required review evidence |
|---|---|---|
| `rfc` | Approved at `64283622...` | Exact base hashes, overlay legality, owner completeness, and internally consistent status |
| `lexer-parser` | Approved at `64283622...` | Singular ordinary impl grammar, bodyless marker grammar, AST field removal, diagnostics, and conformance cutover |
| `binder-checker` | Approved at `64283622...` | Final-survivor provenance, complete ImplHead order, marker proof queries, publication restrictions, unification, signature v1, and coherence v1 |
| `module-system` | Approved at `64283622...` | Module-interface v3 field inheritance, explicit-only marker projection, cutover, source-header provenance, and publication validation |
| `error-system` | Approved at `64283622...` | Operator tags, diagnostic subsets, rendering, and invariant precedence |
| `spec-audit` | Approved at `64283622...` | Operator spelling, semantic-type closure, and cross-RFC consumer closure |
| `verification` | Approved at `64283622...` | Independent reproduction of all base hashes, 31 preimages and hashes, provenance mutations, and the complete gate plan |

## Decision Record

Decision: ACCEPTED.

All seven required owners approved exact review SHA-256
`642836225d54f6fa28f8c27e9985972081dbd221c2e8f3e61a0aafd04fe9bb1e`.
The accepted-file SHA-256 is
`9704d5651606e8a74034c8af4be5172b4007a6c9f0ee8ea2f5ee183223401c01`.
The decision authorizes the direct RFC 0015 cutover for canonical impl heads,
structured operator diagnostic facts, explicit marker facts, demand-driven
marker proofs, signature publication, coherence publication, and combined
module-interface publication. It authorizes no compatibility path, partial
schema cutover, or preserved v0/v2 producer or consumer.

On 2026-07-17 the proposal moved mechanically from `ACCEPTED` to
`IMPLEMENTING` and began the direct cutover recorded below. The transition did
not alter the accepted technical contract or revive any superseded codec.

## Implementation Tracker

| Slice | State | Required evidence |
|---|---|---|
| Singular ordinary and marker source forms | Complete | The parser, AST schema, Binder inventory, and specification expose one ordinary behavior interface per impl and one bodyless marker assertion per marker impl. Focused parser coverage, grammar expectations, AST snapshots, source-shape diagnostics, the 20-fixture impl-source architecture negative gate, and the complete conformance AST and grammar runners pass. |
| Operator algebra and renderer | Complete | `operator-kind.{h,cc}` defines all exact tags, symbolic AST mappings, encodings, and spellings; four normative oracle hashes (`4db7a174...`, `36bab6dd...`, `6d835cdc...`, `a6025127...`), shared-spelling identity tests, diagnostic-subset validation for all four `OperatorDiagnostic` kinds, the full AST-to-semantic operator alignment gate in `check-checker-architecture.py::check_operator_closure`, sanitizer build, focused CTest (4 cases), and 60-fixture Checker architecture negative gate pass. |
| Type-key pattern algebra and codec | Complete | The complete seventeen-variant recursive algebra, symbolic tags, canonical v1 key construction, full raw decoder, re-encoding verification, identity-free and identity-bearing vectors, both impl-pattern vectors, framing and trailing-byte mutations, malformed child records, order and duplicate rejection, and complete signature-record replacement coverage pass in the 19-case signature facts suite. |
| Pattern construction and unification | Complete | Interface arguments and self share one parameter space. Publication validates generic ownership and substitution-stable heads; coherence performs alpha-renamed unification with fixed-point dereferencing, occurs checks, blanket comparison in both directions, canonical ordering, and deterministic rejection. |
| Marker shape, policy, and proof authority | Complete | One verified shape inventory and one prelude-authorized policy registry feed explicit-only persisted marker maps. `BodyCheckingInput` issues the proof capability, and a private semantic-type interning capability supports generic nominal substitution. Production source-to-signature tests cover generic struct and enum reconstruction, builtin and structural proof, self and mutual cycles, precedence, and fail-closed unsupported forms. |
| Signature v1 and coherence v1 cutover | Complete | Signature publication uses only revision v1 with complete canonical requirement records and an independent source census. Coherence uses only revision v1, consumes verified module-interface projections, carries policy lineage, and publishes structured failures. The focused cross-module suite passes 9 cases. No v0 production domain or producer/consumer remains. |
| Module-interface v3 cutover | Complete | Module publication, import reconstruction, coherence projection, and framing use only module-interface revision v3. The focused module-interface suite passes 8 cases. No v2 production domain or producer/consumer remains. |
| Documentation and repository gates | Complete | The sanitizer build passes. All 202 distinct CTest targets have passing evidence after regenerating and rerunning the one missing marker AST expectation; this includes 124 unit targets, all lit runners, and 27 architecture targets. Checker architecture passes with 100 negative fixtures, RFC validation passes, format and diff gates pass, and no compatibility rail is retained. |

Implementation is authorized by the accepted decision. States advance only
when the named evidence is attached; acceptance alone is not implementation
evidence.

### 2026-07-20 Landing Evidence

Commits `0bba7e34`, `f86b5660`, and `76e73196` landed every RFC 0015
implementation slice, its governance records, and its current architecture
guidance on `develop` with one production rail. Signature facts use revision
v1, coherence uses revision v1, and module interfaces use revision v3.
Imported signature view revision v0 is an independent projection protocol and
is not an alternate signature-facts, coherence, or module-interface producer.

The final marker-proof integration removes the manual verified-signature test
fixture. Its source is parsed and bound, `SignatureFactsBuilder` reconstructs
generic nominal, field, and enum-variant facts, module-interface and coherence
verification publish them, and `MarkerProofEngine` proves or rejects the
result. The checker architecture gate rejects removal of this producer path or
restoration of a manual candidate rail.

The sanitizer build, all 204 CTest targets, RFC validation, format validation,
architecture gates, and diff hygiene passed for the landed repository state.
RFC 0015 is therefore `LANDED`.

# RFC 0018 Occurrence Bridge Overlay

RFC 0018 was accepted after all nine owners approved exact REVIEW SHA-256
`bdcbee8761d5476822cbe5bb2548332ad36e4d5f507c38e74d06751c6f444379`.
Signature reconstruction now consumes one occurrence-local Binder fact per
source node. Ordinary and bodyless forms are classified independently before
survivor conflict grouping; a unique survivor publishes under the shared
authority with its own provenance, while conflicting survivor groups publish
no impl head or marker fact. Implementation evidence remains tracked by RFC
0018.
