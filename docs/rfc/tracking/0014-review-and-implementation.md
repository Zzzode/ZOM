# RFC 0014 Review And Implementation Tracker

This document records review, decisions, and implementation evidence for RFC
0014. The proposal frontmatter remains authoritative for status and approvers.

## Discussion Record

### 2026-07-15 Design Intake

The receiver implementation audit found four coupled gaps:

- explicit lexical `Self` has no contextual binding fact;
- interface `Self` has no canonical semantic type identity;
- source receiver forms have no total mapping to RFC 0005 `ReceiverMode`; and
- lifecycle callables are represented as special definitions but still accept
  ordinary receiver parameters.

The task router blocked checker implementation until one contract assigns
these facts to binder, signature checking, ownership analysis, and lowering.
The initial draft follows mature contextual receiver designs and separates
lifecycle places from ordinary receivers.

### 2026-07-15 Task Router Return

The first routing review kept the proposal in `DRAFT`. It required RFC 0010 to
be an explicit dependency, reordered implementation so semantic support
precedes syntax cutover, corrected manifest-scoped repository ownership,
moved contextual-`Self` failure into the binder diagnostic family, defined
receiver diagnostic precedence, and limited this RFC to the lifecycle-place
boundary. Complete field, inheritance, borrowing, call, move, exit, and cleanup
permissions remain assigned to the RFC 0007 redesign and block lifecycle
implementation.

The routing re-review confirmed that dependency ordering, owner paths,
diagnostic ownership, and lifecycle scope were corrected. It required exact
codec framing oracles before acceptance rather than production implementation;
the RFC now records that gate without blocking entry to formal review.

### 2026-07-15 Technical Return And Revision

The binder-checker review returned the draft for receiver signature census,
interface-Self inheritance and projection semantics, staged codec cutover, and
exact canonical tags. The revision now nests receiver identity in
`ReceiverSignature`, defines transitive interface rebasing and `Self::Item`
projection selection, separates binding metadata v1 receiver cutover from v2
lifecycle cutover, and fixes receiver, semantic-type, and module-interface
codec contracts.

The spec-audit review returned the draft for owner-complete lifecycle grammar,
modifier legality, exact diagnostics and boundary tests, and premature
lifecycle permission claims. The revision now closes owners to class, struct,
and error, replaces the complete lifecycle element productions, allocates
`ZOM2095-ZOM2099`, supplies the exact precedence matrix, and leaves all field,
borrow, call, move, and escape permissions to RFC 0007.

The spec-audit re-review passed the revised lifecycle boundary without further
findings. Binder-checker re-review passed after the interface closure and
associated projection census were keyed by complete canonical interface
instantiations rather than interface definition alone.

### 2026-07-15 Formal Review Return

Formal owners reviewed RFC snapshot
`d8e71472a485b9b301ffe27d4c999da192819dafe090a6e7a2a1d9c5429660a1`.
`runtime-memory` approved the lifecycle permission boundary. Every other
technical group returned at least one blocker:

- `lexer-parser` and `error-system` required total lifecycle diagnostic
  precedence, closed display vocabularies, and the complete RFC 0005 checker
  diagnostic algebra for `ZOM4086-ZOM4087`;
- `binder-checker` required exact codec oracles and a total `Self::Item` rule
  for nominal owners;
- `module-system` required module-interface oracles and driver publication path
  ownership;
- `ir-backend` required the mandatory checked-module and Semantic HIR boundary
  before Built MIR;
- `spec-audit` and `verification` required the acceptance-mandatory preimages
  and SHA-256 values to exist rather than remain described as future work.

The revised snapshot closes those returns by adding exact semantic-type,
module-interface, and binding-fact framing vectors; removing the nonexistent
full binding-metadata revision; rejecting unqualified nominal `Self::Item`;
defining total parser precedence and checker diagnostic facts; adding driver
ownership; and requiring a dedicated accepted RFC 0010 Semantic HIR
lifecycle-place overlay. Because every approval is hash-bound, all owners must
re-review the revised snapshot.

### 2026-07-15 Formal Owner Approval

All nine required owners approved exact REVIEW proposal
`6b57a4ffdfde1e4adc37c858d6c263d4d1266d6de146538975e16c0de9030915`.

- `rfc` approved frontmatter, required-owner coverage, prior art, exact base
  hashes, acceptance readiness, and the mechanical status transition;
- `lexer-parser` and `error-system` approved lifecycle grammar, total parser
  precedence, closed display vocabularies, registry rows, and the complete
  receiver-normalization checker diagnostic algebra;
- `binder-checker` and `module-system` approved contextual identity, complete
  interface-instantiation closure, total projections, receiver census, driver
  ownership, and all codec/framing contracts;
- `runtime-memory` and `ir-backend` approved the non-permission lifecycle
  boundary, mandatory checked-module and Semantic HIR path, and explicit RFC
  0010 overlay prerequisite; and
- `spec-audit` and `verification` approved cross-RFC consistency, independently
  reproduced all nine byte vectors, matched all six bound hashes, and passed
  the RFC and diff-hygiene gates.

No owner recorded a blocking or non-blocking objection.

### Review Queue

| Owner | State | Evidence |
|---|---|---|
| `rfc` | Approved | Governance, owner coverage, acceptance readiness, and transition |
| `lexer-parser` | Approved | Lifecycle grammar, receiver forms, and total parser precedence |
| `binder-checker` | Approved | Contextual identity, substitution, projections, census, and fact framing |
| `module-system` | Approved | Impl/interface identity, module-interface v2, and driver publication |
| `error-system` | Approved | Closed display, registry, checker diagnostic algebra, and precedence |
| `runtime-memory` | Approved | Lifecycle non-permission boundary and implementation blockers |
| `ir-backend` | Approved | Checked-module, Semantic HIR, Built MIR, and cleanup boundaries |
| `spec-audit` | Approved | Grammar, semantics, diagnostics, codecs, and cross-RFC alignment |
| `verification` | Approved | Nine byte vectors, six bound hashes, RFC gate, and diff hygiene |

## Decision Record

Decision: ACCEPTED on 2026-07-15.

Final accepted proposal SHA-256:
`d6b2975af9a91c4fd9b7d06cb90911ccb7f0cd57968ab43c6da6398cc762f659`.

All nine required owners approved the exact REVIEW proposal
`6b57a4ffdfde1e4adc37c858d6c263d4d1266d6de146538975e16c0de9030915`
with no objections. `rfc` authorized the mechanical transition that changed
only frontmatter, the RFC index, the status history, and this decision record.

The RFC 0014 design is accepted and the proposal is now `IMPLEMENTING` through
the local tracker below. Ordinary receiver and contextual-`Self` implementation
may proceed. Lifecycle syntax and implementation remain blocked by the RFC
0007 redesign and the accepted RFC 0010 Semantic HIR lifecycle-place overlay
required by the proposal.

## Implementation Tracker

| Slice | State | Required evidence |
|---|---|---|
| Contextual Self binding | Implemented | Closed facts, independent AST verifier reconstruction, nominal/interface/impl tests |
| Receiver source shape and binding | Implemented | Unique first receiver, no default, synthetic bare type exclusion, special receiver identity, and focused tests |
| Receiver normalization | Blocked by the RFC 0005 signature direct replacement | Verified checker input, exhaustive matrix, canonical callable signatures, parameter-census verifier, and diagnostic tests |
| Lifecycle syntax migration | Blocked by RFC 0007 redesign and an accepted RFC 0010 HIR lifecycle-place overlay | Grammar/spec alignment, verified HIR-to-MIR lifecycle-place semantics, and regenerated repository fixtures |
| Lifecycle ownership analysis | Blocked by RFC 0007 and the RFC 0010 overlay implementation | Built MIR path-sensitive facts and cleanup tests |
| Production cutover | Blocked by prior slices | Deleted spelling and AST fallbacks, full sanitizer and determinism evidence |

### 2026-07-15 Receiver And Contextual Self Slice

The parser now restricts receiver parameters to direct member callable
contexts, enforces the unique-first and no-default shape, and reports ZOM2095
for module, block, extern, function-expression, and lambda receivers. Lifecycle
parameter syntax remains unchanged until its accepted atomic prerequisites are
available.

The binder publishes successful `ThisExpr` resolutions only through
`thisBindings`, preserves receiver capture and closure-free-variable behavior,
and rejects cross-map duplication. It also publishes closed `selfTypes` rows
for nominal, interface, and impl owners and reports ZOM3025 outside those
owners. The verifier independently reconstructs every contextual `Self` root,
its nearest active declaration body, canonical owner identity, and exact token
span from the AST and frozen inventory. Mutation tests reject missing rows,
wrong owner variants, and wrong source spans without consulting the producer.
The production candidate codec delegates the adjacent `selfTypes` and
`thisBindings` sequences to one typed extension codec before `currentSurface`.
An independent raw-record framing oracle matches all three RFC 0014 fixed
preimages and SHA-256 digests, while a production-record composition test
checks the same sequence boundary with complete `BoundSelfType` and `BoundThis`
records.

An end-to-end compile check confirmed that migrating lifecycle declarations
before lifecycle-place binding would leave `this` unresolved inside an
initializer. The lifecycle parser/spec cutover was therefore not included in
this implementation slice.

### 2026-07-15 Receiver Normalization Dependency Audit

Receiver normalization cannot be attached to the current checker without
creating a temporary signature model. The production checker still consumes
`ast::BindingMetadata`, writes mutable `Type` trees into `TypeEnv`, and
places every method parameter, including the receiver, into a structural
`FunctionType`. It cannot consume the verified `selfTypes` and `thisBindings`
facts implemented by the preceding slice.

The RFC 0005 implementation tracker states that its direct replacement has not
started. The repository consequently has no canonical `CallableSignature`,
`ReceiverSignature`, `ValueSignature`, signature candidate, independent
signature verifier, or structured checker failure algebra. The existing
`type::ReceiverMode` classifies call-site dispatch sources such as implicit
receivers and index bases; it is not RFC 0014's `Shared | Mutable | Move`
permission algebra and cannot be reused.

The parser also diagnoses declaration modifier combinations but the current
`MethodDecl` payload retains only `is_static` and visibility. Adding a
receiver-specific `is_mutating` field would not solve the missing normalized
modifier set, exact signature diagnostic provenance, or verified parameter
census. Such a field would remain unused until the RFC
0005 checker cutover and is therefore not introduced.

This slice becomes implementable only after RFC 0005 enters `IMPLEMENTING`
with a named direct-replacement series and provides all of the following in the
production checker path:

- verified parsed-module and binding-metadata inputs;
- canonical semantic type and callable-signature facts;
- nested receiver identity with receiver/ordinary-parameter partitioning;
- candidate-to-independent-verifier publication; and
- structured signature diagnostics and exact source provenance.

Receiver normalization must land in that signature cutover. Patching the
current `DeclSignatureComputer` or adding a parallel receiver side table is not
an accepted implementation path.
# RFC 0015 Accepted Overlay

RFC 0015 was approved at exact review SHA-256
`642836225d54f6fa28f8c27e9985972081dbd221c2e8f3e61a0aafd04fe9bb1e`.
Its accepted-file SHA-256 is
`9704d5651606e8a74034c8af4be5172b4007a6c9f0ee8ea2f5ee183223401c01`.
The overlay directly replaces the RFC 0014 impl-owner, contextual `Self`, and
projection-consumer contracts named by RFC 0015.

# RFC 0018 Occurrence Bridge Overlay

RFC 0018 was accepted after all nine owners approved exact REVIEW SHA-256
`bdcbee8761d5476822cbe5bb2548332ad36e4d5f507c38e74d06751c6f444379`.
The contextual-`Self` contract now carries the ordinary source occurrence in
`SelfOwner::Impl`, retains tag `0x03`, and expands that handle to the complete
`ImplSourceOccurrenceKey`. Bodyless marker occurrences publish no impl
`SelfOwner`. Implementation evidence remains tracked by RFC 0018.
