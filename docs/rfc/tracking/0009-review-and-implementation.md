# RFC 0009 Review And Implementation Tracker

This document is the local discussion and tracking record for RFC 0009. It
does not approve the proposal. RFC status, approvers, and the recorded decision
remain authoritative in the proposal frontmatter.

## Discussion Record

### 2026-07-11 Acceptance Review Return

The review returned the previous dispatch side-table design because it used
local `TypeId` and `SymbolId` values, presentation names, AST impl nodes, an
incomplete error target, and target-assigned dyn-vtable slots. It also formed a
dependency cycle with RFC 0010 and allowed mutable dispatch facts to outlive
the checker stores that issued their substitution and witness handles.

The replacement must consume RFC 0005 canonical semantic selections, use RFC
0011 identities, give one RFC a sole final dispatch-target algebra, keep dyn
method targets logical until LIR, freeze and verify the complete fact set, and
define registered invariant diagnostics before returning to review.

### 2026-07-11 Draft Revision Response

RFC 0009 now owns one closed `DispatchTarget` algebra built from RFC 0005
`SelectedCallable` facts. It copies the already-checked receiver role, mode,
and normalization plan; packages canonical substitutions and witnesses exactly
once; keeps dyn targets slot-free; and publishes immutable
`VerifiedDispatchFacts` tied to the exact RFC 0005 checked revision and RFC
0008 checked-evidence lease.

The verifier has a closed invariant algebra, deterministic precedence and
sorting, a non-empty revision oracle, exhaustive registered
`ZOM9937-ZOM9941` destinations, and generated test-only injection. RFC 0010
consumes the verified output and does not repeat lookup, normalization, trait
selection, or operator resolution.

This response resolves the written return but records no approval. RFC 0009
remains `DRAFT` until every required owner reviews the exact coordinated RFC
0005, RFC 0008, RFC 0009, and RFC 0010 proposal hashes.

The exact synchronized hashes submitted for that review are:

- RFC 0005: `318b56973fec2bf4dcd80eda192ba19037a8c7c12591196a4d6436a052a3cbed`;
- RFC 0008: `a9a45572111ef1646050591ced5402562136fbb2120b421a0a89167ebbbe873a`;
- RFC 0009: `1c2234867f4a654424435e6f76ad53a9763dae7ec86c22d8540a21422db7f335`;
- RFC 0010: `593358797ce415eae0c608850afcd3a1c593bceb7746389b91a4b06bbcb273b8`.

### 2026-07-11 Complete Call-Envelope Response

Semantic exact-hash review returned the preceding proposal because compound
assignment lacked the complete receiver, substitution, witness, and raises
input required by `DispatchFact`. Error review also required deterministic
occurrence aggregation.

The corrected coordinated hashes are RFC 0005
`76fa033bbfe845a0b4000d8a42ac7035b997813929c47628fb3520786f9027ee`,
RFC 0008
`f9f54af941d4a103dfafe37c36ac975bc68b03ae9be378843ce7ef5b26843b79`,
RFC 0009
`6be4a5cad81f7696b0187361e72ff4d3a85e066f576f55f37deab1943733262f`,
and RFC 0010
`568b462f6d07cc3f68cf69c76bffd7313581c40359ad2ed124bfe66130b3b44f`.

RFC 0009 now consumes exactly one complete RFC 0005 checked call envelope at
every dispatch site and defines adjacent code/location aggregation with every
complete fact retained. No target signature, coherence, receiver, or witness
query is repeated.

This response records no approval or status transition.

### 2026-07-11 Governance Bookkeeping Response

Governance review returned the response because stale exact-hash approvals
remained in the checklist and the Repository Impact table omitted Chapter 3.
The impact row now includes Chapters 3, 4, and 9, and every owner is reset for
fresh review. The current coordinated proposal hashes are RFC 0005
`cc29da0e4d93e24236f0502010f67b3376d8035ef6b337e938d6ad5642aa9cec`,
RFC 0008
`b7eadb7f6a75c5863ea5379e473200fa96432918e6816b0035070d3c80525732`,
RFC 0009
`7e2a04895309bffd8c7fd49c5fb421aac21bc0615f53d9be0076aa50d02af02c`,
and RFC 0010
`3b679a21f7f38f653b4e47cea17b0a779551ec54c9ed3f9c7e08ab01244b6465`.

The coordinated set after dependent visibility and MIR-wrapper responses is
RFC 0005
`b0ad5070498ceefe29eea26ad724e8234a0c6ca82691c22a69a069694ae50699`,
RFC 0008
`5a4ecd27455a94fe00c0905a509ab5f397b4e20ac33a90b0e9a4cd5631934331`,
RFC 0009
`7e2a04895309bffd8c7fd49c5fb421aac21bc0615f53d9be0076aa50d02af02c`,
and RFC 0010
`da1b53b447314d39350549aec88caa03431a4b160d78868e137fd662d9dfa540`.

After the Result identity and RFC 0006 error-role response, the current
coordinated proposal hashes are RFC 0005
`62bcb71b6971481cf030d9aac438bf9117c74d05f44f865dbb5659e6b8c2f695`,
RFC 0006
`ae064ead3fe27dee5a5121bb284aac87623677d1b8a792f52e9ebefcb48cc961`,
RFC 0008
`5a4ecd27455a94fe00c0905a509ab5f397b4e20ac33a90b0e9a4cd5631934331`,
RFC 0009
`7e2a04895309bffd8c7fd49c5fb421aac21bc0615f53d9be0076aa50d02af02c`,
and RFC 0010
`da1b53b447314d39350549aec88caa03431a4b160d78868e137fd662d9dfa540`.
All checklist states remain pending for exact-hash review.

### 2026-07-11 Spec-Alignment Return And Response

Spec review returned the indexed-assignment hash because Chapter 3 still
disagreed with the exact operator-interface inventory, the documentation plan
omitted that chapter, and dispatch carried only one result type even though a
raising call has a distinct successful payload, canonical union value, and
residual effect.

RFC 0009 is resubmitted at
`6bebc5a230e498d799176cbe89c3862461cbeea5a9b4e1fddb25b3442d0291be`.
Its coordinated hashes are RFC 0005
`ac6b518d4c900daf3e1d64c2abc5e26475b24aa5bda5fe1da3573f65cd4d13bb`,
RFC 0008
`8f0aa6c1ad3f223c71247523cf2d5031b348c3d9d5a7c8e3110510f1429ad1f2`,
and RFC 0010
`aa383df086896793af8d87fae0fe41aa345c02f4ccd699c001d71fd7ee30cda5`.

`DispatchFact` now copies the exact success type, canonical result type,
raises type, and matching RFC 0005 error-union shape. Result transforms apply
only to the successful payload and cannot modify or reconstruct residuals.
Chapter 3 is part of the documentation contract and contains the same selected
interface forms, primitive-only forms, and `IndexMut -> &mut Output` place
contract. The field addition increments the revision domain to
`zom.dispatch-facts-revision.v1`; the corrected executable 121-byte oracle
hashes to
`25ca384dcdb9cd5225d8ec8abfb25c68865c2c6ca4518c8f30ffdc359a51835c`.

All earlier entry verdicts are stale; no approval or status transition is
recorded.

### 2026-07-11 Exact-Hash Entry Review Verdict

Binder-checker, error-system, and coordinated module/IR review approved RFC
0009 hash
`b368fa69ca03afcb360ed7145edb94ee918cc19906bbd0cd350dcf65accd93cd`
as formal-review input. The verdict confirms one target per node, child
`IndexMut` versus parent operation ownership, complete call envelopes, stable
revision and invariant mapping, and no repeated semantic lookup. This is not an
RFC approval; `rfc`, `spec-audit`, and `verification` remain pending.

### 2026-07-11 Indexed-Assignment Dispatch Response

Governance review returned the preceding RFC 0005/RFC 0009 pair because plain
indexed assignment had contradictory target ownership and indexed compound
assignment could not represent all access and operation work without lookup.

The corrected exact set is RFC 0005
`3f38d165cfad83a3cc53cb53f8a3323766b340a9e8c79d55e11a48c82ea64c95`,
RFC 0008
`f9f54af941d4a103dfafe37c36ac975bc68b03ae9be378843ce7ef5b26843b79`,
RFC 0009
`b368fa69ca03afcb360ed7145edb94ee918cc19906bbd0cd350dcf65accd93cd`,
and RFC 0010
`568b462f6d07cc3f68cf69c76bffd7313581c40359ad2ed124bfe66130b3b44f`.

The index child now owns one read or mutable-place access target. `IndexMut`
returns a checked mutable reference; plain assignment adds no parent target;
compound assignment adds only its operation target. The dispatch map remains
one target per node, and HIR/MIR receive an exact left-to-right,
single-evaluation plan.

This response records no approval or status transition.

### 2026-07-11 Current Coordinated Error-Role Set

The Result identity and RFC 0006 role-bearing lowering responses supersede the
earlier coordinated hashes in this tracker. The current proposal hashes are RFC
0005
`62bcb71b6971481cf030d9aac438bf9117c74d05f44f865dbb5659e6b8c2f695`,
RFC 0006
`ae064ead3fe27dee5a5121bb284aac87623677d1b8a792f52e9ebefcb48cc961`,
RFC 0008
`5a4ecd27455a94fe00c0905a509ab5f397b4e20ac33a90b0e9a4cd5631934331`,
RFC 0009
`7e2a04895309bffd8c7fd49c5fb421aac21bc0615f53d9be0076aa50d02af02c`,
and RFC 0010
`da1b53b447314d39350549aec88caa03431a4b160d78868e137fd662d9dfa540`.
All checklist states remain pending for exact-hash review.

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
tag to the FFI proof, defines the complete target-spec codec and 111-byte
oracle, defines non-zero numeric gate IDs and the registry snapshot codec with
a 49-byte oracle, binds every proof to context, module, executable MIR, target,
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
uses the matching `VerifiedTargetSelection`. The descriptor domain advances
to v2 with a 423-byte oracle. The coordinated formal-review hashes are RFC 0005
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

### 2026-07-11 Forced Cast Dependency Reopen

RFC 0005 restores `ForcedChecked` and advances the checked-facts codec to v2;
RFC 0006 and RFC 0010 now carry the corresponding cast-or-panic contract. The
coordinated proposal hashes are RFC 0005
`f382b82aaa055fb3676a1578fcf73e1ba1ca030671b96e97294bbc55db8c19c1`,
RFC 0006
`aea15335a11da7d59a579d713abfb30267d72a8043f90988dfb598a8cfb06bda`,
RFC 0008
`3f77e6d785611f8f828d0fc6865606898af8e6a0e22b628b279bc4823aca691d`,
RFC 0009
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`,
and RFC 0010
`367bcf4f5ae4ba783e564f1dee5813f14d63ee831a21ce3a654211f429c7d0a8`.

### 2026-07-11 Checked Cast Dependency Response

RFC 0005 AC 15 and cast precedence now close the three-mode source contract at
`ed71e363082d35c7738fc3f529a619f70645262cd2987e17e1bb82f9f71b14a4`.
RFC 0010 closes the checked-cast handoff at
`d850ab5b536584f87ffc3e5ee3299e439206705d2221aff23b81d9bf6bb3c964`.
All dependency-aware approvals based on the preceding hashes are superseded.

IR review required AST identity projection at the handoff. RFC 0010 now maps
the frontend `NodeId` to `HirNodeId` and deterministic `MirOperationSite` at
`857ce8361c684b9a7e10e9ccca66d6a4d01e415a0619b0c049ad5648bc745d87`.

### 2026-07-11 Dispatch Framing And Current Dependency Return

Fresh exact-hash review returned RFC 0009 proposal hash
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`.
The dispatch-revision oracle contained a count and per-record length but the
normative text had not defined the multi-record framing or duplicate rules.
The tracker also retained obsolete checked-facts v2 language, stale dependency
state, and cast/panic ownership that belongs to RFC 0005 and RFC 0010 rather
than logical dispatch.

The proposal now fixes the exact revision stream as record count followed by
one length-framed canonical record at a time, rejects duplicate expanded keys
and complete records before hashing, and tests zero/one/two records, ordering,
duplicate, direct-concatenation, and ordinary-sequence mutations. RFC 0009
continues to consume an opaque `CheckedFactsRevision` and does not own cast or
unsafe facts.

The current dependency and consumer hashes are RFC 0004
`cabcfb6b4e1ade93f09398b9a2c1ecf092931a4d012976ebb495eda68eed1843`,
RFC 0005
`5c61e7a993867385f9a895054d25e4a9fe6f891b1c26d55fd1a4dfb3b3bb7d35`,
RFC 0008
`eb0173bb6d69b6425bfc2379e8eb2b70c841c96acb07e12342ff03b85c91cf9c`,
and RFC 0010
`715ae992a29e7ff83e4abf6e6c91d979bffccf7cae55ded450d80dfc730d70fe`.
The repaired RFC 0009 proposal hash is
`c4b9206b117fe4ecd40f1b58a7f79126c4a5bf416051807a99e9ff31db814c10`.
Every prior exact-hash approval is superseded; all owners must review the new
bytes. This response is not an approval.

### 2026-07-11 Exact-Hash Acceptance Review Approval

Formal semantic and invariant review approved RFC 0009 proposal hash
`c4b9206b117fe4ecd40f1b58a7f79126c4a5bf416051807a99e9ff31db814c10`
with tracker hash
`e88f7d76e1cb3468b2fb064c054bf7b91e4672197edc67081b8f647713af9a65`.
Semantic review approved the binder/checker, module-system, error-system,
IR/backend, and spec-audit surfaces. Invariant review approved the dispatch
revision framing, evidence lifetime, deterministic ordering, and verification
surfaces. The 121-byte oracle recomputes exactly, and zero/one/two-record,
duplicate, illegal-framing, selection, residual, and permutation matrices are
closed. `scripts/check-rfc.py`, parser coverage, lexer architecture, and
`git diff --check` pass.

The `rfc` governance owner remains pending. This technical approval does not
populate proposal frontmatter, record an acceptance decision, or authorize
implementation until governance approves the tracker and atomic transition.

## Owner Review Checklist

| Owner | Review State | Blocking Surface |
|---|---|---|
| `rfc` | APPROVED at `c4b9206b...` | Accepted dependency set, owner parity, decision, and atomic transition |
| `binder-checker` | APPROVED at `c4b9206b...` | Semantic selection, normalization, index operations, and complete call facts |
| `module-system` | APPROVED at `c4b9206b...` | Canonical module identities and opaque checked-evidence lifetime |
| `error-system` | APPROVED at `c4b9206b...` | Dispatch invariant registry, ordering, aggregation, and diagnostics |
| `ir-backend` | APPROVED at `c4b9206b...` | Logical dispatch and matching checked revision through target lowering |
| `spec-audit` | APPROVED at `c4b9206b...` | Exact operator inventory, qualified and dyn calls, and raising result roles |
| `verification` | APPROVED at `c4b9206b...` | Dispatch codec, selection mapping, success/residual, lease/revision, and determinism matrices |

All historical owner approvals are superseded by the dispatch-framing change.
Dependencies and technical owner gates are satisfied, but the governance
decision gate is also satisfied.

## Decision Record

Decision: ACCEPTED.

On 2026-07-11, all seven required owners approved RFC 0009 proposal hash
`c4b9206b117fe4ecd40f1b58a7f79126c4a5bf416051807a99e9ff31db814c10`.
RFC 0004, RFC 0005, RFC 0008, and RFC 0011 were accepted. The accepted design
freezes RFC 0005 semantic selection ownership, RFC 0009 final logical dispatch
ownership, RFC 0010 target-lowering ownership, complete receiver/argument and
raises facts, exact dispatch revision framing, opaque checked-evidence
lifetime, diagnostics, and no-repeat-resolution boundaries. The proposal
entered `IMPLEMENTING` through the direct replacement series below on
2026-07-17. RFC 0010 remains independently governed by its own tracker.

## Implementation Tracker

The direct replacement series started on 2026-07-17. Deleted `TypeEnv`
dispatch records, name-based targets, AST impl nodes, error placeholders, and
early ABI slots are not accepted inputs or compatibility rails.

| Slice | State | Required evidence |
|---|---|---|
| Dispatch requirement inventory | Implemented | Complete body-site census, canonical ordering, duplicate/missing rejection, and focused sanitizer tests |
| Verified logical dispatch facts | In progress | Closed target and receiver algebras, candidate codec, independent verifier, exact revision lineage, complete deterministic failure retention, and mutation matrix |
| Cross-module evidence | In progress | Checked-evidence lease, authorized imported signature/interface surfaces, canonical provenance, and stale/swap rejection |
| Semantic HIR consumption | Pending | One-to-one call, operator, index, and compound-assignment nodes with no repeated lookup or source-name recovery |
| MIR and target lowering | Pending | Single evaluation, success/residual transforms, dyn slot assignment at target lowering, ABI verification, and layer snapshots |
| Production cutover | Pending | Full call/operator language surface, sanitizer/default CTest, determinism, conformance, architecture, format, and diff-hygiene evidence |

No implementation slice may consult mutable checker tables or repeat lexical,
member, trait, impl, witness, or dyn-target resolution after dispatch-facts
verification.
# RFC 0015 Accepted Overlay

RFC 0015 was approved at exact review SHA-256
`642836225d54f6fa28f8c27e9985972081dbd221c2e8f3e61a0aafd04fe9bb1e`.
Its accepted-file SHA-256 is
`9704d5651606e8a74034c8af4be5172b4007a6c9f0ee8ea2f5ee183223401c01`.
The overlay directly replaces the RFC 0009 checker diagnostic argument and
operator rendering contracts named by RFC 0015.
