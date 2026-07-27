# RFC 0005 Review And Implementation Tracker

This document is the local discussion and tracking record for RFC 0005. It
does not approve the proposal. RFC status, approvers, and the recorded decision
remain authoritative in the proposal frontmatter.

## Discussion Record

### 2026-07-10 RFC 0010 Dependency Review

The type-system owner review returned RFC 0005 for the following blocking
issues:

- the documented `TypeId -> TypeData` store does not exist; the current
  interner maps canonical-looking strings to insertion-ordered local integers
  and cannot recover immutable payloads;
- local numeric IDs have no `SemanticContextBrand`, so handles from independent
  stores can compare equal accidentally;
- nominal types, impl lookup, coherence caches, and associated projections use
  names, rendered strings, or AST nodes instead of `DefId`, `ImplId`, and
  canonical structural identity;
- mutable inference variables, error recovery types, owned type trees, and
  purported semantic IDs share one unfrozen domain;
- `TypeEnv` stores both owned type trees and IDs, permits concrete node types to
  be overwritten, and freezes only dispatch instead of the complete successful
  checked fact set;
- coercion and dispatch facts are incomplete for RFC 0010: they retain names,
  AST impl nodes, early vtable slots, and only coarse coercion kinds;
- several acceptance criteria are marked complete despite missing canonical
  payload storage, incomplete expression semantics, and compound assignment
  operators that the checker does not inspect;
- the current architecture uses non-owning type pointers, `const_cast`, and
  vectors of owned polymorphic type trees contrary to repository ownership
  rules.

Before re-entering review, the RFC must separate function-local mutable
inference types from the immutable successful semantic type domain, define a
context-checked `SemanticTypeStore`, use structural and definition identity
instead of spelling, freeze complete checked facts through a verifier, and
replace unsupported completion claims with exact current evidence.

### 2026-07-10 Draft Revision Response

The proposal was rewritten around RFC 0011 branded identities, separate
function-local inference and immutable semantic domains, online canonical
`SemanticTypeId -> TypeData` interning, definition-based nominal and impl
identity, explicit constraints and coercion steps, complete RFC 0009 call facts,
write-once checked facts, and a whole-fact verifier. Old implementation links
and complete claims were removed.

These edits address the written blockers but do not constitute approval. RFC
0005 remains `DRAFT` until RFC 0011 and RFC 0004 re-enter review and all owners
review the new contract.

### 2026-07-11 Entry Review Returns

The `binder-checker`, `error-system`, and `module-system` owners reviewed RFC
0005 at proposal hash
`2832aaeca7c82f567535dbfdaf1ace970bba9463f1c83a652934fa974ee60375`.
All three owners returned the draft because the reference-level contract still
requires implementers to invent observable behavior.

The `binder-checker` return requires the next draft to:

- close every `TypeData` payload and remove a distinct nullable variant because
  `T?` normalizes to `T | null`;
- define the complete canonical key codec, store issuance, inference-handle
  ownership, materialization result, and deterministic solver algorithm;
- define every coercion, checked-call, pattern, effect, marker, unsafe,
  substitution, witness, and obligation fact consumed downstream;
- split marker classification, impl coherence, signature publication, and body
  checking without a stage cycle; and
- define closed verifier results and an executable no-rebinding gate.

The `error-system` return requires the next draft to:

- define private `TypeErrorId` issuance, recovery ownership, attachment,
  deduplication, suppression, and destruction validation;
- define closed source-failure and invariant-failure algebras for signature and
  body checking, including deterministic precedence;
- allocate exact checker diagnostic ownership, producers, anchors, argument
  schemas, notes, and emitter ordinals; and
- keep RFC 0007 ownership diagnostics separate from RFC 0005 type diagnostics.

The `module-system` return requires the next draft to:

- make the semantic type store a non-copyable context singleton issued only by
  `CompilerSession`, with linearizable interning and stable concurrent reads;
- define byte-exact semantic-type and signature-fact codecs and non-empty
  revision oracles;
- project every definition in the RFC 0004 binding surface while keeping module
  targets, member visibility, and exports distinct;
- define requester-filtered imported signature views and a frozen global
  coherence view, each with exact revision and stale-view rejection rules;
- split signature checking from body checking so global coherence is built only
  after all signature interfaces are frozen; and
- prove that the AST, binding metadata, imported interfaces, semantic handles,
  and module identity belong to one semantic context and source snapshot.

No approval survives these returns. The RFC remains `DRAFT`; owner re-review
starts only after the proposal and this tracker record the complete response.

### 2026-07-11 Reference-Level Rewrite Response

RFC 0005 was rewritten at proposal hash
`bc0a4944d3e569c1da112f9ddf7059e82a91f2b46f70354812ad78c9c01e5be4`.
The response is reference-level rather than an implementation claim:

- the checker now has separate verified signature and body inputs, both tied to
  one RFC 0004 parsed-module, binding-metadata, and export-surface result;
- the context-global semantic type store has single-use construction,
  non-copyable pinned lifetime, linearizable interning, stable concurrent reads,
  closed payloads and tags, an exact codec, and a non-empty golden vector;
- inference has exact owners, private issuer-branded variables and recovery
  IDs, a frozen recovery ledger, deterministic worklist and representative
  rules, cycle behavior, materialization results, and finish validation;
- signatures, impl patterns, markers, substitutions, witnesses, body facts,
  imported requester views, global coherence views, and all revisions have
  closed structures and canonical encoding rules;
- module-private definition visibility and global private-impl coherence are
  separate capabilities, removing the signature/coherence/body phase cycle;
- signature and checked verifiers return closed verified, source-rejected, or
  invariant-rejected values with an exhaustive negative mapping; and
- RFC 0005 now owns exact typed source, warning, note, and invariant diagnostic
  contracts, deletes binder-owned or capability-style checker errors, and
  assigns RFC 0007 sole ownership of `ZOM4056-ZOM4070`.

Structural checks pass, but this response does not change status or record an
approval. Every required owner must review this exact proposal hash before the
RFC can enter `REVIEW`.

### 2026-07-11 Focused Re-review Returns

The `binder-checker` and `error-system` owners re-reviewed proposal hash
`bc0a4944d3e569c1da112f9ddf7059e82a91f2b46f70354812ad78c9c01e5be4`.
Both returned it. They confirmed the singleton store, base type/signature codec
oracles, inference ownership, phase split, closed verifier shape, and
no-rebinding direction, but found further reference-level blockers.

The second `binder-checker` return requires:

- one non-cyclic owner and one codec for checked call targets, substitutions,
  witnesses, primitive operators, compound assignments, dyn calls, and
  intrinsics across RFC 0005, RFC 0009, and RFC 0010;
- complete capture, normalized-attribute, constant, literal, aggregate,
  pattern-constructor, and exhaustiveness facts;
- exact live modifier, impl-safety, cast, object-safety, effect, and orphan-rule
  contracts;
- self-contained imported signature authorization closure with member
  visibility, and identical impl/marker/coherence records in RFC 0008;
- structural marker evidence for anonymous and aggregate types; and
- canonical view/store/revision fields and non-empty framing oracles with no
  brands or ambiguous digest-only module association.

The second `error-system` return requires:

- rejected results to retain recovery ledgers or remove recovery handles before
  return;
- an exact diagnostic-to-recovery-class allocation table, deterministic
  multi-child recovery join, and ledger ordering;
- a closed global coherence build result carrying `ZOM4017` source failures;
- per-diagnostic severity, stage, producer, unique anchor, item ordinal, and
  recovery policy;
- payload-complete pattern and display arguments plus note provenance;
- an exclusive `ZOM4043` versus RFC 0007 `ZOM4069` unsafe-operation matrix;
- mutually exclusive invariant classification and a merged identity/checker
  invariant ordering; and
- executable injection APIs and lifecycle/single-emission tests for every
  observable branch.

No approval is recorded. The RFC remains `DRAFT` and requires another exact-
hash review after these returns and the affected RFC 0008/0009/0010 contracts
are synchronized.

### 2026-07-11 Focused Re-review Response

The coordinated response is fixed at these proposal hashes:

- RFC 0005: `e2508d39440416a0530abc1b5a1c206160734a7aeecb15cc8b4841d8d0e4f131`;
- RFC 0008: `e53971c10d9308215197504993653113f14b7e1c771084ced91328eb0721f211`;
- RFC 0009: `e2503601c6ef60ee67987fc7e6318bbd75a3a3dae58f5dd28c7fc935bc0d5b9a`;
- RFC 0010: `1950d1ee3903c8f3b43fef9fd120e69a444c5d801c222879fdc3dfbcd2126c6f`.

RFC 0005 now retains recovery ledgers in rejected results, specifies
deterministic multi-child joins and per-diagnostic recovery classes, gives
coherence its own frozen/source/invariant result, and defines disjoint
invariant classification and merged ordering. Constants, arbitrary-precision
integers, literals, aggregates, captures, normalized attributes, exact object-
safety causes, impl safety, orphan legality, structural marker evidence, and
authorization closure are closed facts with canonical codecs. Imported,
coherence, checked, and dispatch revisions have non-empty framing oracles.

Call ownership is non-cyclic: RFC 0005 publishes one typed semantic selection,
RFC 0009 alone owns verified dispatch targets, and RFC 0010 only assembles and
consumes matching revisions. RFC 0008 reuses the exact RFC 0005 impl and marker
schemas and permits several non-overlapping patterns in one outer-head bucket.
Type-valid raw-pointer boundaries are facts consumed by RFC 0007; RFC 0005 no
longer owns `ZOM4043`. Every retained source diagnostic now has an exact stage,
producer, anchor, item order, recovery policy, typed arguments, and note cause.

`python3 scripts/check-rfc.py` and `git diff --check` pass for this response.
These edits answer the written returns but do not record approval or advance
RFC 0005 beyond `DRAFT`; owner re-review must use the exact hashes above.

### 2026-07-11 Second Exact-Hash Re-review Return And Response

The binder-checker, error-system, and coordinated module/IR reviews returned
the preceding hash set. The remaining blockers were exact recovery-ledger
ordering; alias-safe re-export authorization; one marker-fact identity; complete
live operator and receiver-normalization algebras; store lifetime through IR;
and closed dispatch and IR invariant algebras with deterministic sorting.

The synchronized response is fixed at these proposal hashes:

- RFC 0005: `318b56973fec2bf4dcd80eda192ba19037a8c7c12591196a4d6436a052a3cbed`;
- RFC 0008: `a9a45572111ef1646050591ced5402562136fbb2120b421a0a89167ebbbe873a`;
- RFC 0009: `1c2234867f4a654424435e6f76ad53a9763dae7ec86c22d8540a21422db7f335`;
- RFC 0010: `593358797ce415eae0c608850afcd3a1c593bceb7746389b91a4b06bbcb273b8`.

RFC 0005 now defines exact ledger and verifier ordering, canonical signature
scope plus per-binding authorization provenance, the complete live AST operator
inventory, total receiver adjustment, one `MarkerFactKey` identity, and typed
constant-evaluation failures. RFCs 0008 through 0010 preserve the frozen stores
with checked-evidence leases and use closed, registered invariant adapters.

This is a response to the returned findings, not an approval. Every required
owner must re-review the exact hashes above before any status transition.

### 2026-07-11 Third Coordinated Return And Response

Exact-hash semantic review found that compound assignment could not supply the
complete dispatch receiver, substitution, witness, and raises envelope without
re-resolution. Exact-hash error review also required closed module-interface
publication results, a closed IR operation result, typed monomorphization
recursion and budget failures, and deterministic invariant aggregation.

The corrected coordinated proposals are fixed at:

- RFC 0005: `76fa033bbfe845a0b4000d8a42ac7035b997813929c47628fb3520786f9027ee`;
- RFC 0008: `f9f54af941d4a103dfafe37c36ac975bc68b03ae9be378843ce7ef5b26843b79`;
- RFC 0009: `6be4a5cad81f7696b0187361e72ff4d3a85e066f576f55f37deab1943733262f`;
- RFC 0010: `568b462f6d07cc3f68cf69c76bffd7313581c40359ad2ed124bfe66130b3b44f`.

RFC 0005 now has one reusable checked call envelope. Compound assignment embeds
it directly; ordinary calls, members, operators, and indexes publish one typed
call fact, while the index-shape fact duplicates no selection or witness data.
The coordinated RFCs close interface and IR result branches, registered failure
ownership, typed monomorphization rejection, and stable aggregation.

This response records no approval. Exact-hash re-review is required again.

### 2026-07-11 Indexed-Assignment Return And Response

Governance and module/IR review returned RFC 0005 and RFC 0009 because indexed
plain assignment simultaneously required and denied a dispatch target, while an
indexed compound assignment could not represent read, operation, and writeback
without repeated lookup.

The revised exact hashes are:

- RFC 0005: `3f38d165cfad83a3cc53cb53f8a3323766b340a9e8c79d55e11a48c82ea64c95`;
- RFC 0008: `f9f54af941d4a103dfafe37c36ac975bc68b03ae9be378843ce7ef5b26843b79`;
- RFC 0009: `b368fa69ca03afcb360ed7145edb94ee918cc19906bbd0cd350dcf65accd93cd`;
- RFC 0010: `568b462f6d07cc3f68cf69c76bffd7313581c40359ad2ed124bfe66130b3b44f`.

Indexing now distinguishes read access from mutable-place access. The index
child owns one `Index` or `IndexMut` call; `IndexMut` returns the checked
mutable element place. Plain assignment has no parent operation target.
Compound assignment reuses that acquired place and adds one parent operation
envelope. Collection, index, and right-hand side have an exact single-evaluation
order, including short-circuit assignment.

This response records no approval or status transition.

### 2026-07-11 Exact-Hash Entry Review Verdict

The binder-checker, error-system, and coordinated module/IR reviewers approved
RFC 0005 hash
`3f38d165cfad83a3cc53cb53f8a3323766b340a9e8c79d55e11a48c82ea64c95`
as ready to enter formal review. They confirmed the complete call envelope,
indexed assignment child/parent dispatch split, alias authorization, marker
identity, recovery and invariant algebras, checked-evidence lifetime, and
single-evaluation order. Structural RFC, dependency, English, and diff checks
passed on the same bytes.

This is an entry-review verdict, not an RFC approval or decision. The `rfc`,
`spec-audit`, and `verification` owners remain pending; frontmatter therefore
stays `DRAFT`, `approvers: []`, and `decision: TBD`.

### 2026-07-11 Spec-Alignment Return And Response

Spec review returned the entry hash because Chapter 3 still described `as!`
although the parser rejects it, labelled tuple elements were accepted and then
discarded despite the semantic tuple algebra being positional, raising-call
success and residual roles were recoverable only from canonical union order,
and the operator inventory disagreed with RFC 0009 `IndexMut` place access.

RFC 0005 is resubmitted at
`ac6b518d4c900daf3e1d64c2abc5e26475b24aa5bda5fe1da3573f65cd4d13bb`.
The coordinated hashes are RFC 0008
`8f0aa6c1ad3f223c71247523cf2d5031b348c3d9d5a7c8e3110510f1429ad1f2`,
RFC 0009
`6bebc5a230e498d799176cbe89c3862461cbeea5a9b4e1fddb25b3442d0291be`,
and RFC 0010
`aa383df086896793af8d87fae0fe41aa345c02f4ccd699c001d71fd7ee30cda5`.

The response removes `as!`, makes tuple elements positional in the grammar and
parser, adds a labelled-tuple rejection fixture, separates function success
from raising-call value type, and introduces verified `ErrorUnionShapeFact`
records for raising calls, unambiguous binding flow, identical joins, and
component-preserving coercions. Error operators now consume those facts and
never assign roles by union alternative order. The checked-facts codec is
`zom.checked-facts-revision`; its executable 643-byte oracle hashes to
`09e8335be64649f47e44e18672852ec1e9a1669f9d142a806d6a58fceb7c1b62`.

This response invalidates every earlier exact-hash entry verdict. All required
owners must review the new coordinated set before any status transition.

### 2026-07-11 Governance Bookkeeping Response

Governance review returned the preceding response because the checklist still
carried approvals from stale hashes and the proposal changed parser and grammar
surfaces without declaring `lexer-parser` ownership. The proposal now includes
that owner and the exact parser/grammar impact row. Every checklist approval is
reset for fresh review.

The current coordinated proposal hashes are RFC 0005
`cc29da0e4d93e24236f0502010f67b3376d8035ef6b337e938d6ad5642aa9cec`,
RFC 0008
`b7eadb7f6a75c5863ea5379e473200fa96432918e6816b0035070d3c80525732`,
RFC 0009
`7e2a04895309bffd8c7fd49c5fb421aac21bc0615f53d9be0076aa50d02af02c`,
and RFC 0010
`3b679a21f7f38f653b4e47cea17b0a779551ec54c9ed3f9c7e08ab01244b6465`.

After aligning Chapters 6 and 23 and removing the contradictory decision
appendix, the current coordinated hashes are RFC 0005
`b0ad5070498ceefe29eea26ad724e8234a0c6ca82691c22a69a069694ae50699`,
RFC 0008
`5a4ecd27455a94fe00c0905a509ab5f397b4e20ac33a90b0e9a4cd5631934331`,
RFC 0009
`7e2a04895309bffd8c7fd49c5fb421aac21bc0615f53d9be0076aa50d02af02c`,
and RFC 0010
`da1b53b447314d39350549aec88caa03431a4b160d78868e137fd662d9dfa540`.

### 2026-07-11 Result Identity Return And Response

Spec review returned the preceding RFC 0005 hash because Chapter 6 defined
`Result<T, E>` twice: once as a nominal enum and once as a transparent union
alias. The concurrency design also treated the alias and a raising signature as
the same type, contradicting the checked error-union role contract.

The corrected coordinated proposal hashes are RFC 0005
`1fa1219ff45ded8343bc3de0e910735f05d60b327377744cd0c3032e99207266`,
RFC 0008
`5a4ecd27455a94fe00c0905a509ab5f397b4e20ac33a90b0e9a4cd5631934331`,
RFC 0009
`7e2a04895309bffd8c7fd49c5fb421aac21bc0615f53d9be0076aa50d02af02c`,
and RFC 0010
`da1b53b447314d39350549aec88caa03431a4b160d78868e137fd662d9dfa540`.

`Result<T, E>` now has one identity: the nominal enum with `Success(T)` and
`Failure(E)` variants. It supports ordinary enum matching and never acquires an
error-union shape fact from its arguments or variants. Raising signatures keep
success and raises distinct, and raising calls alone publish checked
success/residual roles. This response records no approval or status transition;
all required owners must review the exact hashes above.

### 2026-07-11 Error-Lowering Dependency And Ownership Response

Further review found that RFC 0006 still inferred error roles from structural
unions, while governance found that the concurrency owner was missing from RFC
0005. RFC 0005 now uses the closed `TypeData::Nominal` variant for nominal enum
examples, declares the concurrency owner and impact surface, and keeps ordinary
unions and nominal enums outside implicit error-role inference. RFC 0006 now
consumes RFC 0005 role facts through RFC 0010 verified MIR.

The current coordinated proposal hashes are RFC 0005
`62bcb71b6971481cf030d9aac438bf9117c74d05f44f865dbb5659e6b8c2f695`,
RFC 0006
`ae064ead3fe27dee5a5121bb284aac87623677d1b8a792f52e9ebefcb48cc961`,
RFC 0008
`5a4ecd27455a94fe00c0905a509ab5f397b4e20ac33a90b0e9a4cd5631934331`,
RFC 0009
`7e2a04895309bffd8c7fd49c5fb421aac21bc0615f53d9be0076aa50d02af02c`,
and RFC 0010
`da1b53b447314d39350549aec88caa03431a4b160d78868e137fd662d9dfa540`.

This response records no approval or status transition. Every required owner
must review the exact proposal bytes.

### 2026-07-11 Dispatch-Lineage And ABI Closure Set

The descriptor review added exact dispatch revision lineage to RFC 0010 MIR,
replaced target-specific semantic-interface publication with an independent
target-artifact ABI manifest, and introduced a separate FFI boundary verifier.
The current coordinated proposal hashes are RFC 0005
`62bcb71b6971481cf030d9aac438bf9117c74d05f44f865dbb5659e6b8c2f695`,
RFC 0006
`2f7e0e8c225fc986514925db291fcab8ab2f252f0cb7fed02bb1d84ea207173e`,
RFC 0008
`5a4ecd27455a94fe00c0905a509ab5f397b4e20ac33a90b0e9a4cd5631934331`,
RFC 0009
`7e2a04895309bffd8c7fd49c5fb421aac21bc0615f53d9be0076aa50d02af02c`,
and RFC 0010
`9d7138f09754bb2b1335bfe71f377b52405c21218dd850644008751fafeedb8a`.
All checklist states remain pending for exact-hash review.

### 2026-07-11 Formal Review Entry

Governance, semantic, and invariant entry reviewers approved the preceding
coordinated DRAFT hashes. The proposals therefore moved legally from `DRAFT`
to `REVIEW`; no approver or decision is recorded by this transition. The
current formal-review proposal hashes are RFC 0005
`2e25177ce1d38cfe9fc5d83508ac449cea57b69ff394537462d7fb1324aafe69`,
RFC 0006
`0e08f8e83c43b3771befbabb28d6a87d980fc8df3b7fdeebd670678a07fda3ed`,
RFC 0008
`3b200c0a8bbdb7f29cbe2104fb6de93049c5ebf182ab9087282467e9d267d661`,
RFC 0009
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`,
and RFC 0010
`386b4633440df9580f04f0225a7dcbbfb4bf574dec6ca91f54afce221cca87d1`.
Every required owner must review these exact REVIEW bytes before acceptance.

### 2026-07-11 Formal Verification Return And Response

Formal verification returned RFC 0006 and RFC 0008 because stale readiness
prose still described their state as `DRAFT`, and returned RFC 0010 because
the `TargetSelection` failure phase lacked a closed legality matrix and
generated mapping coverage. Those defects are corrected. RFC 0005 and RFC
0009 retain their preceding exact bytes; RFC 0006, RFC 0008, and RFC 0010 now
require fresh review because their proposal bytes changed. The coordinated
formal-review hashes are RFC 0005
`2e25177ce1d38cfe9fc5d83508ac449cea57b69ff394537462d7fb1324aafe69`,
RFC 0006
`e772e4ee8d709f86b13228394060125689f370b792b73d239060202a779f7da0`,
RFC 0008
`3f77e6d785611f8f828d0fc6865606898af8e6a0e22b628b279bc4823aca691d`,
RFC 0009
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`,
and RFC 0010
`e10dc01b7d2198e2fcfaab042d1062f198b97c4d677e913c769ecdb72eac0283`.
No approver or acceptance decision is recorded by this response.

### 2026-07-11 Semantic And Invariant Return Response

Formal invariant review returned RFC 0010 because only target selection had a
closed failure matrix. Formal semantic review returned RFC 0005 for
function-type and empty-tuple parser drift, and returned RFC 0006 and RFC 0010
because the target-dependent FFI source gate was neither bound into its proof
revision nor structurally required before LIR. The response closes every IR
phase over result branch, kind, owner, site, and detail; rejects function-type
parameter labels in both parsers; accepts the empty unit tuple in both parsers;
adds the sole source-rejecting feature-boundary seam and required proof set;
and binds the FFI proof to checked facts, executable MIR, and `TargetSpecId`.
The coordinated formal-review hashes are RFC 0005
`2e25177ce1d38cfe9fc5d83508ac449cea57b69ff394537462d7fb1324aafe69`,
RFC 0006
`d58749b64fd1ff08a5f5c0911f42e4dd451485d1113da676805aa77ab47f264c`,
RFC 0008
`3f77e6d785611f8f828d0fc6865606898af8e6a0e22b628b279bc4823aca691d`,
RFC 0009
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`,
and RFC 0010
`39f89297534d7abd3acf5ef899dd659df239c79a477ef6e56b6f690df5aa400f`.
All required owners must review these exact bytes. No approver or acceptance
decision is recorded by this response.

### 2026-07-11 Codec Closure Return And Response

Formal verification returned RFC 0006 because its FFI oracle encoded only the
digest portion of `MirRevisionId`, and returned RFC 0010 because
`TargetSpecId` and the feature-gate registry lacked canonical codecs and a
verifiable completeness snapshot. The response adds the executable MIR phase
tag to the FFI proof, defines the complete target-spec codec and 108-byte
oracle, defines non-zero numeric gate IDs and the registry snapshot codec with
a 46-byte oracle, binds every proof to context, module, executable MIR, target,
and registry revision, and fixes deterministic multi-gate rejection ordering.
The coordinated formal-review hashes are RFC 0005
`2e25177ce1d38cfe9fc5d83508ac449cea57b69ff394537462d7fb1324aafe69`,
RFC 0006
`0b67ae71096f4d760ef9358fbb689343eaf245ed017585a29b34db87bcfa6759`,
RFC 0008
`3f77e6d785611f8f828d0fc6865606898af8e6a0e22b628b279bc4823aca691d`,
RFC 0009
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`,
and RFC 0010
`a14f28c16222aa811c49e913b3437fbe3a303d06f62cf44ea6f9cf219da649ad`.
All required owners must review these exact bytes. No approver or acceptance
decision is recorded by this response.

### 2026-07-11 Canonical Target Identity Return And Response

Formal verification found that RFC 0006 maintained target-profile strings and
panic/object tags separately from RFC 0010 `TargetSpecId`. The response
deletes that second identity codec. Error-union descriptors and target-artifact
manifests now carry only RFC 0010's 32-byte `TargetSpecId`; layout construction
uses the matching `VerifiedTargetSelection`. The descriptor uses the canonical domain with a 405-byte oracle. The coordinated formal-review hashes are RFC 0005
`2e25177ce1d38cfe9fc5d83508ac449cea57b69ff394537462d7fb1324aafe69`,
RFC 0006
`e294b9706636863788f2dcf8a72b1bbbde116f2f09464f9f8990121a0700e29f`,
RFC 0008
`3f77e6d785611f8f828d0fc6865606898af8e6a0e22b628b279bc4823aca691d`,
RFC 0009
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`,
and RFC 0010
`a14f28c16222aa811c49e913b3437fbe3a303d06f62cf44ea6f9cf219da649ad`.
All required owners must review these exact bytes. No approver or acceptance
decision is recorded by this response.

### 2026-07-11 Target Profile Dataflow Return And Response

Formal semantic review found that a digest-only `VerifiedTargetSelection`
could prove target identity but could not supply the data layout and runtime
profile needed by lowering. The response makes the token own the immutable
`CanonicalTargetSpec` and its recomputed `TargetSpecId`; consumers use no
reverse digest lookup or parallel target record. Governance review also
corrected RFC 0006's Decision Record to include RFC 0009 in RFC 0010's complete
dependency set. The coordinated formal-review hashes are RFC 0005
`2e25177ce1d38cfe9fc5d83508ac449cea57b69ff394537462d7fb1324aafe69`,
RFC 0006
`e294b9706636863788f2dcf8a72b1bbbde116f2f09464f9f8990121a0700e29f`,
RFC 0008
`3f77e6d785611f8f828d0fc6865606898af8e6a0e22b628b279bc4823aca691d`,
RFC 0009
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`,
and RFC 0010
`7d1598b361626676ab5e1b43c864e9f97b60cca9f381fa897a15cc660c5b639c`.
All required owners must review these exact bytes. No approver or acceptance
decision is recorded by this response.

### 2026-07-11 Ownership Routing And Panic Lifetime Return And Response

Required-owner review returned RFC 0006 because caught panic metadata could
retain a borrow into an unwound residual frame, and returned RFC 0010 because
routing contracts left `compiler/ir/**` unowned, named stale error-system
paths, and assigned Chapter 14 inconsistently. The response introduces a
call-scoped borrowed `PanicInfoView`, eagerly materializes an opaque owned
panic record before unwinding, defines exact catch transfer and destruction
rules, and adds sanitizer lifetime gates. Routing now assigns
`compiler/ir/**` to `ir-backend`, uses the real diagnostics and Chapter 11
paths, and makes `runtime-memory` the sole primary owner of Chapter 14 while
`concurrency` owns Chapter 15. The coordinated formal-review hashes are RFC
0005
`2e25177ce1d38cfe9fc5d83508ac449cea57b69ff394537462d7fb1324aafe69`,
RFC 0006
`a6154c1167e2b39d4512095a43af3ed6e48049851144f3086917f54f2450d9d1`,
RFC 0008
`3f77e6d785611f8f828d0fc6865606898af8e6a0e22b628b279bc4823aca691d`,
RFC 0009
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`,
and RFC 0010
`682f034ad6c58a0b68c5bee17914bf9da07bf6b2fb0056898da2ed5389dbbc46`.
All required owners must review these exact bytes. No approver or acceptance
decision is recorded by this response.

### 2026-07-11 Cast Grammar And Error Ownership Return And Response

Lexer-parser review found two prose-only cast drifts: Chapter 4 still listed a
generic-angle cast and Chapter 17 still admitted forced-cast punctuation. Both
now expose only `as T` and `as? T`, matching RFC 0005, ANTLR, the recursive
parser, and negative conformance. Error-system review found RFC 0006 duplicated
checker ownership in its Repository Impact; that row now owns only diagnostics
and Chapter 11 while binder-checker remains the sole checker/type owner. The
coordinated formal-review hashes are RFC 0005
`2e25177ce1d38cfe9fc5d83508ac449cea57b69ff394537462d7fb1324aafe69`,
RFC 0006
`4e422693b94f2fdb5b9fa8cca45fc5e59be7b4c2b150c07033ca25b8422f12b0`,
RFC 0008
`3f77e6d785611f8f828d0fc6865606898af8e6a0e22b628b279bc4823aca691d`,
RFC 0009
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`,
and RFC 0010
`682f034ad6c58a0b68c5bee17914bf9da07bf6b2fb0056898da2ed5389dbbc46`.
All required owners must review these exact bytes. No approver or acceptance
decision is recorded by this response.

### 2026-07-11 Forced Cast Restoration

The language contract retains `as! T` as the panic-on-failure counterpart to
`as? T`. The previous response incorrectly treated a parser rejection as the
intended language surface. RFC 0002, the centralized parser mode mapping, and
the prior normative cast semantics establish the three-mode contract.

RFC 0005 now defines `Guaranteed`, `OptionalChecked`, and `ForcedChecked` cast
modes. The parser, ANTLR grammar, generated AST schema, checker result typing,
specification, and positive conformance fixture expose the same three modes.
The checked-facts codec domain is `zom.checked-facts-revision`; its 643-byte
framing oracle hashes to
`09e8335be64649f47e44e18672852ec1e9a1669f9d142a806d6a58fceb7c1b62`.
The resubmitted RFC 0005 proposal SHA-256 is
`f382b82aaa055fb3676a1578fcf73e1ba1ca030671b96e97294bbc55db8c19c1`.
The coordinated dependency-review hashes are RFC 0006
`aea15335a11da7d59a579d713abfb30267d72a8043f90988dfb598a8cfb06bda`,
RFC 0008
`3f77e6d785611f8f828d0fc6865606898af8e6a0e22b628b279bc4823aca691d`,
RFC 0009
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`,
and RFC 0010
`367bcf4f5ae4ba783e564f1dee5813f14d63ee831a21ce3a654211f429c7d0a8`.
All exact-hash approvals for the preceding coordinated set are superseded.

### 2026-07-11 Cast Precedence And Handoff Return And Response

Semantic review found that Chapter 4 placed cast above exponentiation while
Chapter 17, ANTLR, and the recursive parser place cast in the relational tier.
Chapter 4 now follows the implemented relational tier, and an exact AST fixture
proves that `base ** exponent as! i32` casts the completed power expression.
Chapter 3 now names both `as?` and `as!` for checked downcasts from `any`.

Invariant review returned RFC 0005 because AC 15 still named only two cast
modes. The corrected acceptance gate requires `as`, `as?`, and `as!`, all
three `CastMode` tags, and one complete canonical record per mode. The returned
RFC 0005 hash `f382b82aaa055fb3676a1578fcf73e1ba1ca030671b96e97294bbc55db8c19c1`
is superseded by
`ed71e363082d35c7738fc3f529a619f70645262cd2987e17e1bb82f9f71b14a4`.

The same review found that RFC 0010 required MIR to consume `CheckedCastFact`
without carrying the cast map through `VerifiedCheckedModule` and HIR. RFC 0010
now preserves every cast field through the frontend handoff, HIR, MIR, dumps,
revisions, verification, acceptance criteria, and implementation plan. Its
current proposal SHA-256 is
`d850ab5b536584f87ffc3e5ee3299e439206705d2221aff23b81d9bf6bb3c964`.
All approvals on the returned coordinated hashes are superseded.

IR review then required AST identity projection rather than copying RFC 0005's
`NodeId` into MIR. RFC 0010 now maps the frontend association to `HirNodeId`
and deterministic `MirOperationSite`; its current SHA-256 is
`857ce8361c684b9a7e10e9ccca66d6a4d01e415a0619b0c049ad5648bc745d87`.

### 2026-07-11 Raw-Pointer Cast Closure Return And Response

Fresh exact-hash semantic review returned RFC 0005 proposal hash
`ed71e363082d35c7738fc3f529a619f70645262cd2987e17e1bb82f9f71b14a4`.
The normative type contract accepts raw-pointer reinterpret casts under an
unsafe boundary, but the closed `CastKind` algebra had no representable kind
for them. Treating the cast as invalid contradicted the specification and
`ZOM4069` ownership; treating it as valid could not publish the complete fact
required by RFC 0005 and RFC 0010.

The response adds `RawPointerReinterpret`, an exact const/mutability matrix,
`Guaranteed` mode, `RawPointerBoundary`, and the rule that `*const -> *mut`
remains invalid. The checked-facts codec uses the canonical contract; its
643-byte
oracle hashes to
`09e8335be64649f47e44e18672852ec1e9a1669f9d142a806d6a58fceb7c1b62`.
RFC 0010 preserves this kind and unsafe requirement through HIR and MIR without
reclassifying pointee or mutability facts and requires complete lowering and
negative fixtures.

The repaired proposal hash is
`b81dd2239fea6d147d83a46ee3dcb4d6b6d345044bf5e4270b21768508d83ddf`;
the coordinated RFC 0010 hash is
`715ae992a29e7ff83e4abf6e6c91d979bffccf7cae55ded450d80dfc730d70fe`.
Every approval on the returned hash is superseded. All required owners must
review these exact bytes again; this response is not an approval.

### 2026-07-11 Runtime-Memory Owner Routing Return And Response

Invariant review approved the raw-pointer cast and codec contracts in proposal
hash `b81dd2239fea6d147d83a46ee3dcb4d6b6d345044bf5e4270b21768508d83ddf`
but returned its owner surface. Raw-pointer reinterpret, const-correctness,
unsafe acknowledgement, and Chapter 14 memory-safety semantics require the
`runtime-memory` owner. The proposal frontmatter, Repository Impact table, and
this checklist had omitted that owner together, so the mechanical parity check
could not detect the routing gap.

The response adds `runtime-memory` to all three owner surfaces and assigns the
Chapter 14 and runtime memory boundary explicitly. The repaired proposal hash
is `31e8ff83dc535f3af5a91c00122277a108af41540233d4f6a06b0a2a4c9fb25c`.
Every required owner, including `runtime-memory`, must review this exact hash;
this response is not an approval.

### 2026-07-11 Exact-Hash Acceptance Review Approval

Formal semantic and invariant re-review approved RFC 0005 proposal hash
`31e8ff83dc535f3af5a91c00122277a108af41540233d4f6a06b0a2a4c9fb25c`.
The coordinated RFC 0010 consumer hash is
`715ae992a29e7ff83e4abf6e6c91d979bffccf7cae55ded450d80dfc730d70fe`.

Semantic review approved the lexer/parser, binder/checker, error-system,
module-system, runtime-memory, concurrency, and spec-audit surfaces. Invariant
review approved the IR/backend, runtime-memory, and verification surfaces. The
reviews independently recomputed the 643-byte checked-facts canonical oracle and
confirmed all three cast modes, the closed raw-pointer matrix,
`RawPointerReinterpret`, `ZOM4013` versus `ZOM4069` ownership, evidence leases,
HIR/MIR preservation, deterministic ordering, and no downstream
reclassification. Parser coverage, lexer architecture, `scripts/check-rfc.py`,
and `git diff --check` pass.

The `rfc` governance owner remains pending. This technical approval does not
populate proposal frontmatter, record an acceptance decision, or authorize
implementation until governance approves the tracker and atomic transition.

## Owner Review Checklist

| Owner | Review State | Blocking Surface |
|---|---|---|
| `rfc` | APPROVED at `31e8ff83...` | Governance, dependencies, owner parity, and atomic transition |
| `lexer-parser` | APPROVED at `31e8ff83...` | Positional tuple grammar, cast precedence, and three-mode cast syntax |
| `binder-checker` | APPROVED at `31e8ff83...` | Complete semantic facts, raw-pointer matrix, cast modes, and error-union role handoff |
| `error-system` | APPROVED at `31e8ff83...` | Cast-invalid versus unsafe-boundary ownership, invariants, registry, panic, and error roles |
| `module-system` | APPROVED at `31e8ff83...` | Authorization, marker, interface, and store lifetime |
| `ir-backend` | APPROVED at `31e8ff83...` | Checked facts, raw-pointer reinterpret, failure continuations, lease, and HIR/MIR handoff |
| `runtime-memory` | APPROVED at `31e8ff83...` | Raw-pointer reinterpret, const-correctness, unsafe acknowledgement, and memory-safety boundary |
| `concurrency` | APPROVED at `31e8ff83...` | Error-role, unsafe fact, and forced-cast panic preservation across task boundaries |
| `spec-audit` | APPROVED at `31e8ff83...` | Positional tuples, cast matrix, error-union roles, and operator alignment |
| `verification` | APPROVED at `31e8ff83...` | Store, verifier, canonical codec, cast matrix, parser, and cross-module gates |

All required owner approvals are current at the exact proposal hash. The
dependency and governance decision gates are satisfied.

## Decision Record

Decision: ACCEPTED.

### RFC 0025 Acceptance Synchronization

On 2026-07-25, the accepted RFC 0025 proposal at SHA-256
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`
atomically synchronized RFC 0005's core bootstrap/final-interface split,
exhaustive imported interface and binding-surface revision sums,
whole-session marker lineage, failure mapping, and one-step codec cutover. RFC
0005 remains `IMPLEMENTING`. Product implementation and executable evidence
remain tracked by RFC 0025's `R25` tasks; this decision note marks no
implementation slice complete.

### RFC 0013 Additive Overlay

RFC 0013 was accepted on 2026-07-11 at proposal SHA-256
`e3909d5caad48a1c0255ee57d2a8fcc327e046945f20a586e0c6bf0115a237c3`.
It overlays only the checked-body diagnostic registry and production matrix for
`ZOM4085`; this accepted RFC 0005 proposal remains byte-identical.

On 2026-07-11, all ten required owners approved RFC 0005 proposal hash
`31e8ff83dc535f3af5a91c00122277a108af41540233d4f6a06b0a2a4c9fb25c`
after RFC 0001, RFC 0002, and RFC 0003 landed and RFC 0004 and RFC 0011 were
accepted. The accepted design freezes the constraint-based checker, canonical
semantic type store, complete verified facts, three cast modes, raw-pointer
reinterpret and unsafe-boundary matrix, error-union roles, evidence leases,
diagnostics, codecs, and downstream no-reclassification boundary.
The named direct replacement series started on 2026-07-16. RFC 0005 therefore
advanced from `ACCEPTED` to `IMPLEMENTING` without changing the accepted
semantic contract.

### RFC 0025 Acceptance Synchronization Evidence

- Acceptance authority is bound to RFC 0025 proposal SHA-256
  `4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`.
- `python3 scripts/check-rfc.py` and scoped `git diff --check` passed for this
  documentation transaction.
- Bootstrap isolation, exhaustive alternatives, failure mapping, transitive
  revision vectors, downstream consumers, and architecture mutation evidence
  remains assigned to RFC 0025's `R25` tasks.

## Implementation Tracker

### Canonical Semantic Foundation Direct Replacement Series

This series replaces the polymorphic type trees, rendered-string keys, mutable
`TypeEnv` facts, and partial checker publication described by the accepted
implementation plan. The canonical store and verified facts are the sole
semantic path. Existing checker behavior remains partial evidence and is not
proof of the accepted canonical contract.

| Slice | State | Required evidence |
|---|---|---|
| Closed semantic type value algebra | Implemented | Exact `0x01-0x10` branch tags, primitive and field tags, complete value payload coverage, move-only semantics, and focused unit tests |
| Semantic type canonical key codec | Implemented | RFC 0014 fixed vectors, recursive canonical union oracle, complete branch encoding, canonical ordering validation, and malformed-input rejection |
| Canonical semantic type store | In review | Closed admission capability, context-owned singleton construction, canonical key lookup, linearizable interning, stable reads, malformed and foreign-context rejection, focused sanitizer tests, and architecture gates are implemented; complete repository evidence remains pending |
| Signature facts and verifier scaffold | Implemented | Closed candidate, canonical fact algebra, revision, typed invariant failures, diagnostic adapter, verified production construction, and native verifier tests are live |
| Direct checker rail replacement | In review | The polymorphic type tree, mutable `TypeEnv`, old Checker passes, AST `BindingMetadata`, compiler symbol rail, and AST-to-IR lowering entry are deleted; `CompilerSession` emits a typed fatal `MissingRequiredFact` at the signature stage instead of publishing partial checked facts |
| Formal signature, coherence, and body checking | Implemented | The RFC 0015 phase order, codecs, `VerifiedSignatureFacts`, module interfaces, frozen coherence, and verified checked facts are live; standard `Copy` and `Linear` role publication is tracked separately by RFC 0024 |
| Downstream checked-facts handoff | Implemented | `CompilerSession` publishes verified checked facts, checked modules, semantic HIR, and Built MIR; standard marker authority and ownership overlay completion remain tracked by RFC 0024 and RFC 0007 |
# RFC 0015 Accepted Overlay

RFC 0015 was approved at exact review SHA-256
`642836225d54f6fa28f8c27e9985972081dbd221c2e8f3e61a0aafd04fe9bb1e`.
Its accepted-file SHA-256 is
`9704d5651606e8a74034c8af4be5172b4007a6c9f0ee8ea2f5ee183223401c01`.
The overlay directly replaces the RFC 0005 impl-pattern, marker-evidence,
signature, coherence, and diagnostic codec contracts named by RFC 0015.

# RFC 0018 Occurrence Bridge Overlay

RFC 0018 was accepted after all nine owners approved exact REVIEW SHA-256
`bdcbee8761d5476822cbe5bb2548332ad36e4d5f507c38e74d06751c6f444379`.
The type-system contract now reconstructs and classifies every source impl
occurrence independently under one shared stable authority. Only unique RFC
0015 ordinary and marker survivors publish semantic facts; occurrence handles,
facts, scopes, nodes, and dense slots remain outside signature and coherence
identity. This record changes no implementation slice state or evidence.
