# RFC 0008 Review And Implementation Tracker

This document is the local discussion and tracking record for RFC 0008. It
does not approve the proposal. RFC status, approvers, and the recorded decision
remain authoritative in the proposal frontmatter.

## Discussion Record

### 2026-07-10 RFC 0010 Dependency Review

The review returned RFC 0008 for the following blocking issues:

- identity names and ownership are incomplete across RFC 0008 and RFC 0010:
  Chapter 21 requires both `PackageId` and `CrateId`, followed by `ModuleId`,
  `DefId`, `ImplId`, and `SemanticTypeId`; neither proposal currently defines
  that complete hierarchy;
- `ModuleInterface` and `SignatureStore` do not define immutable canonical
  payloads precisely enough to serve as the verified frontend handoff;
- the rollout introduces `CompilerSession` beside `CompilerDriver` and keeps a
  wrapper or rollback path, violating the repository's atomic replacement and
  no-compatibility rules;
- module-cycle legality, metadata representation, and driver replacement are
  still blocking open questions;
- the proposal links discussion and tracking back to itself instead of using a
  review artifact that can record owner findings and their resolution;
- live code still has no `CompilerSession`, `ModuleGraph`, `ModuleInterface`,
  `SignatureStore`, metadata store, or global coherence index, so no
  implementation evidence may be claimed.

Before re-entering review, the proposal must use the shared RFC 0011 identity
model, define one immutable interface publication contract, choose a cycle
policy and metadata contract, require one direct `CompilerDriver` replacement,
and make every acceptance criterion observable through exact tests or files.

### 2026-07-10 Draft Revision Response

The draft now consumes RFC 0011
`SemanticContextBrand -> PackageId -> CrateId -> ModuleId -> DefId/ImplId`,
specifies verified immutable module interfaces and a
context-checked signature store, rejects every import cycle, limits the first
implementation to source dependencies in one session, removes persisted
metadata stubs, and requires one direct `CompilerDriver` replacement. The
formal dependencies now record RFC 0004, RFC 0005, RFC 0011, and RFC 0012
explicitly. RFC 0011 owns identity, and RFC 0012 owns the package graph and
target inputs consumed by module discovery.

These edits resolve the written blockers but do not constitute owner approval.
All owners must re-review the draft before it may return to `REVIEW`.

### 2026-07-11 RFC 0005 Coherence Synchronization

RFC 0008 hash
`e53971c10d9308215197504993653113f14b7e1c771084ced91328eb0721f211`
reuses RFC 0005 hash
`e2508d39440416a0530abc1b5a1c206160734a7aeecb15cc8b4841d8d0e4f131`
for the exact signature access, authorization closure, impl head, marker fact,
structural evidence, coherence input, and revision contracts. Module interface
revisions now include signature-facts revision and the root, lookup-support,
and layout-support closure. Coherence revision entries carry `ModuleId`; head
buckets contain sorted sequences because several complete non-overlapping
patterns may share one outer head. Only RFC 0005
`CoherenceBuildResult::Frozen` can construct the session index.

This synchronization is a draft response, not owner approval. Structural RFC
and diff checks pass; module, checker, diagnostic, IR, spec, and verification
owners must re-review the exact hashes.

### 2026-07-11 Alias Authorization And Evidence-Lifetime Response

Coordinated owner review returned the preceding interface contract because a
foreign re-export could not prove its exact imported-interface revision,
marker buckets used the wrong identity, and canonical substitution and witness
stores had no owner after body checking.

The revised RFC 0008 proposal hash is
`a9a45572111ef1646050591ced5402562136fbb2120b421a0a89167ebbbe873a`.
Its coordinated dependency hashes are RFC 0005
`318b56973fec2bf4dcd80eda192ba19037a8c7c12591196a4d6436a052a3cbed`,
RFC 0009
`1c2234867f4a654424435e6f76ad53a9763dae7ec86c22d8540a21422db7f335`,
and RFC 0010
`593358797ce415eae0c608850afcd3a1c593bceb7746389b91a4b06bbcb273b8`.

Each root authorization now distinguishes binding identity from canonical
definition identity and proves local or exact imported-interface origin.
Marker buckets use RFC 0005 `MarkerFactKey`. The session-owned append-only
checked-facts repository issues validated leases that preserve substitution
and witness stores through HIR, MIR, monomorphization, LIR, and backend work.

This response records no approval or status transition. Exact-hash owner
re-review remains required.

### 2026-07-11 Interface Failure-Algebra Response

Error-system review returned the preceding hash because interface publication
had no closed source/invariant result, classification, ordering, injection, or
registered mapping, and module source-diagnostic ownership remained open.

RFC 0008 is resubmitted at
`f9f54af941d4a103dfafe37c36ac975bc68b03ae9be378843ce7ef5b26843b79`
with coordinated RFC 0005
`76fa033bbfe845a0b4000d8a42ac7035b997813929c47628fb3520786f9027ee`,
RFC 0009
`6be4a5cad81f7696b0187361e72ff4d3a85e066f576f55f37deab1943733262f`,
and RFC 0010
`568b462f6d07cc3f68cf69c76bffd7313581c40359ad2ed124bfe66130b3b44f`.

Publication now forwards exact RFC 0004/RFC 0005 source and invariant results,
defines its own closed interface invariant facts and sorting, maps them to
`ZOM9950-ZOM9954`, and exposes generated test-only injection. RFC 0004 remains
the sole owner of `ZOM3011-ZOM3016` and `ZOM3018-ZOM3019` module source
diagnostics.

This is a draft response, not an owner approval.

### 2026-07-11 Coordinated Hash Refresh

RFC 0008 remains byte-identical at
`f9f54af941d4a103dfafe37c36ac975bc68b03ae9be378843ce7ef5b26843b79`.
The coordinated semantic hashes changed while closing indexed assignment: RFC
0005 is `3f38d165cfad83a3cc53cb53f8a3323766b340a9e8c79d55e11a48c82ea64c95`,
RFC 0009 is `b368fa69ca03afcb360ed7145edb94ee918cc19906bbd0cd350dcf65accd93cd`,
and RFC 0010 remains
`568b462f6d07cc3f68cf69c76bffd7313581c40359ad2ed124bfe66130b3b44f`.
Any coordinated approval must use this complete exact set.

### 2026-07-11 Exact-Hash Entry Review Verdict

Binder-checker, error-system, and coordinated module/IR review approved RFC
0008 hash
`f9f54af941d4a103dfafe37c36ac975bc68b03ae9be378843ce7ef5b26843b79`
as formal-review input. The verdict covers alias-safe authorization, marker
keys, closed interface publication failures, module diagnostic ownership, and
checked-evidence lease lifetime. This is not an acceptance decision; `rfc`,
`spec-audit`, and `verification` remain pending.

### 2026-07-11 Verification And Visibility Return Response

Verification returned the entry hash because the interface revision oracle was
not required as executable golden data, same-package multi-target identity was
not isolated in one fixture, and no architecture gate proved that
`CompilerDriver` and every secondary scheduler disappear. Spec review also
returned the proposal because requester filtering relied on private/protected
member access and subclass contexts that Chapter 23 does not define or enforce.

RFC 0008 is resubmitted at
`8f0aa6c1ad3f223c71247523cf2d5031b348c3d9d5a7c8e3110510f1429ad1f2`.
Its coordinated hashes are RFC 0005
`ac6b518d4c900daf3e1d64c2abc5e26475b24aa5bda5fe1da3573f65cd4d13bb`,
RFC 0009
`6bebc5a230e498d799176cbe89c3862461cbeea5a9b4e1fddb25b3442d0291be`,
and RFC 0010
`aa383df086896793af8d87fae0fe41aa345c02f4ccd699c001d71fd7ee30cda5`.

The response makes the 250-byte interface preimage executable, adds explicit
same-package library/binary target and worker-permutation fixtures, and requires
`scripts/check-compiler-session-architecture.py` to reject every surviving
driver, wrapper, alias, or second scheduler. Module export visibility remains
enforced through RFC 0004 surfaces. Every directly name-addressable member of
an exported root is published with its retained `Public`, `Protected`, or
`Private` metadata, but no member is filtered and no subclass requester context
is constructed.

This response records no approval. The new exact hash requires fresh owner
review.

### 2026-07-11 Governance Bookkeeping Response

Governance review returned the response because stale exact-hash approvals
remained in the checklist, the dependency order omitted RFC 0009, and the
Repository Impact table omitted this tracker. Those records are corrected and
all owners are reset for fresh review. The current coordinated proposal hashes
are RFC 0005
`cc29da0e4d93e24236f0502010f67b3376d8035ef6b337e938d6ad5642aa9cec`,
RFC 0008
`b7eadb7f6a75c5863ea5379e473200fa96432918e6816b0035070d3c80525732`,
RFC 0009
`7e2a04895309bffd8c7fd49c5fb421aac21bc0615f53d9be0076aa50d02af02c`,
and RFC 0010
`3b679a21f7f38f653b4e47cea17b0a779551ec54c9ed3f9c7e08ab01244b6465`.

After aligning Chapter 6 and adding its explicit retained-member gate, the
current coordinated hashes are RFC 0005
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

### 2026-07-11 Forced Cast Dependency Reopen

RFC 0005 restores `ForcedChecked` and uses the canonical checked-facts codec;
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

### 2026-07-11 Module Graph Ownership Reopen

Formal RFC 0004 review proved that a single-module `BindingInputVerifier` could
not receive already-filtered surfaces while also owning complete graph SCCs,
module-path resolution, and missing-versus-invisible classification. RFC 0008
now implements the repaired two-stage boundary: global `VerifiedModuleGraphVerifier`
publishes only acyclic graph slices and owns path/cycle/ambiguity diagnostics;
then RFC 0004 `BindingInputVerifier` receives complete dependency surfaces and
owns selected-member absence and visibility. The current RFC 0008 proposal hash
is `dfa32ab93b45a5ab926758a8acbd353582c13ea1a6fcf290427f918f1e6057c1`.
Every approval on `3f77e6d...` is superseded and all owners must re-review the
new exact bytes.

### 2026-07-11 Current Dependency And Exact-Hash Rebase

Formal semantic and invariant review confirmed that RFC 0008 proposal hash
`4a299be3aa1c89d61bfeb679edcf96636e506d0d752997f0853040e4a9a0a67a`
is technically closed, but returned this tracker because its owner rows still
bound the earlier `dfa32ab...` proposal and described an obsolete checked-facts
canonical dependency.

The current dependency and consumer hashes are RFC 0004
`cabcfb6b4e1ade93f09398b9a2c1ecf092931a4d012976ebb495eda68eed1843`,
RFC 0005
`5c61e7a993867385f9a895054d25e4a9fe6f891b1c26d55fd1a4dfb3b3bb7d35`,
RFC 0012
`42c4190969c009b95be79ca741176b6cb5eee3315f2195486f5913bb76e3eb8b`,
and RFC 0010
`715ae992a29e7ff83e4abf6e6c91d979bffccf7cae55ded450d80dfc730d70fe`.
RFC 0008 stores the complete `VerifiedCheckedFacts` behind an opaque
`CheckedFactsRevision` and verified evidence lease; it does not own or encode
the RFC 0005 codec. All seven owners must review the current exact
proposal and tracker bytes; this rebase is not an approval.

### 2026-07-11 Exact-Hash Acceptance Review Approval

Formal semantic and invariant review approved RFC 0008 proposal hash
`4a299be3aa1c89d61bfeb679edcf96636e506d0d752997f0853040e4a9a0a67a`
with tracker hash
`e061d5b0b676023973a14ec3b1b329ea0cdc5ee00916c075419999e7cfc38d86`.
Semantic review approved the module-system, binder/checker, error-system,
IR/backend, and spec-audit surfaces. Invariant review approved the graph,
interface, opaque evidence-lease, deterministic scheduling, and verification
surfaces. The 250-byte interface, 43-byte environment, 68-byte receipt, and
97-byte graph oracles recompute exactly. `scripts/check-rfc.py`, parser
coverage, lexer architecture, and `git diff --check` pass.

The `rfc` governance owner remains pending. This technical approval does not
populate proposal frontmatter, record an acceptance decision, or authorize
implementation until governance approves the tracker and atomic transition.

## Owner Review Checklist

| Owner | Review State | Blocking Surface |
|---|---|---|
| `rfc` | APPROVED at `4a299be3...` | Accepted dependency set, owner parity, decision, and atomic transition |
| `module-system` | APPROVED at `4a299be3...` | Global graph verification, session, authorization, marker, interface, and checked-facts repository contracts |
| `binder-checker` | APPROVED at `4a299be3...` | Graph slices, complete surfaces, identities, signature store, and complete verified checked-facts handoff |
| `error-system` | APPROVED at `4a299be3...` | Unique graph/member diagnostics, source forwarding, cast and unsafe facts, and invariants |
| `ir-backend` | APPROVED at `4a299be3...` | Registered-target handoff and opaque checked-evidence lifetime through backend completion |
| `spec-audit` | APPROVED at `4a299be3...` | Module graph, cast and unsafe facts, export enforcement, and member visibility |
| `verification` | APPROVED at `4a299be3...` | Cycle/ambiguity matrices, interface oracle, opaque checked-revision evidence lease, multi-target fixture, and single-scheduler gate |

No approval from before the module-graph ownership change remains current. The
exact-hash technical approvals recorded above supersede those historical
states. Dependencies and technical owner gates are satisfied, but the
governance decision gate is also satisfied.

## Decision Record

Decision: ACCEPTED.

### RFC 0025 Acceptance Synchronization

On 2026-07-25, the accepted RFC 0025 proposal at SHA-256
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`
atomically synchronized RFC 0008's orchestrator-owned preparatory/final
schedule, verified core-library set, toolchain-core search root, structural
catalog admission, three-transaction phase order, complete contextual roots,
host closure, and configured prelude. RFC 0008 remains `IMPLEMENTING`.
Product implementation and executable evidence remain tracked by RFC 0025's
`R25` tasks; this decision note marks no implementation slice complete.

### RFC 0013 Additive Overlay

RFC 0013 was accepted on 2026-07-11 at proposal SHA-256
`e3909d5caad48a1c0255ee57d2a8fcc327e046945f20a586e0c6bf0115a237c3`.
It supplies the complete borrow-surface and canonical module-interface revision
replacement clauses; this accepted RFC 0008 proposal remains byte-identical.

On 2026-07-11, all seven required owners approved RFC 0008 proposal hash
`4a299be3aa1c89d61bfeb679edcf96636e506d0d752997f0853040e4a9a0a67a`.
RFC 0002 and RFC 0003 had landed, and RFC 0004, RFC 0005, RFC 0011, and RFC
0012 were accepted. The accepted design freezes the global module graph,
deterministic structural discovery, one `CompilerSession` scheduler, immutable
module interfaces, signature/coherence ordering, complete checked-facts
repository, opaque evidence-lease lifetime, diagnostic, codec, and no-rebinding
boundaries. RFC 0009 and RFC 0010 may advance through their own gates.

### RFC 0025 Acceptance Synchronization Evidence

- Acceptance authority is bound to RFC 0025 proposal SHA-256
  `4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`.
- `python3 scripts/check-rfc.py` and scoped `git diff --check` passed for this
  documentation transaction.
- Root codec, structural catalog, snapshot phase order, host closure,
  configured prelude, publication bijection, and no-registry-rollback evidence
  remains assigned to RFC 0025's `R25` tasks.

## Implementation Tracker

Status: IMPLEMENTING.

The direct replacement series started on 2026-07-11 with these completed
changes:

- `CompilerSession` directly replaces `CompilerDriver` in the driver library,
  CLI, and unit-test entry points;
- the session requires a process-root `SemanticContextFactory`, claims one
  process-unique `SemanticContextBrand`, and owns the sole RFC 0011 registry
  family for that context;
- focused session tests prove distinct contexts and registry families for two
  sessions created by one process-root factory;
- context-brand exhaustion and duplicate singleton registry construction map
  to registered `ZOM9919` and `ZOM9920` fatal diagnostics; no raw session
  assertion remains;
- `scripts/check-compiler-session-architecture.py --check` proves the direct
  cutover, exact driver surface, unique process-root context factory, sole
  registry-family claim path, and single frontend scheduler;
- the registered negative architecture target rejects the old driver, a
  compatibility alias, a wrapper class, a second scheduler, missing registry
  ownership, a second context factory, and a raw session assertion; both
  architecture CTest targets pass;
- finalized compilation roots are admitted only from session-owned
  `DigestVerifiedSourceSnapshot` bytes; package path rereads, direct-source
  installation, and the resolution-to-compilation TOCTOU boundary are absent;
- the sole frontend scheduler directly runs `Parser`, retains its single-use
  token snapshot, and publishes canonically ordered `VerifiedParsedModule`
  records through `ParsedModuleVerifier`; `performParse`, raw AST storage, and
  `getASTs` are absent;
- positive and negative session and package architecture tests reject package
  filesystem rereads, raw AST maps, and parse wrappers; focused sanitizer tests
  cover verified source identity, parser receipt publication, and package
  snapshot byte admission;
- the session now runs structural dependency discovery to a deterministic fixed
  point before freezing source identities, admits provider-library roots from
  the verified crate closure, and never rereads a package path;
- selected source-module paths, exact declaration-name validation, canonical
  module, definition, and impl identity freeze, and one retained frozen
  definition inventory per parsed module precede graph publication;
- the production `StructuralModuleResolver` and `VerifiedModuleGraphVerifier` now
  publish the frozen global graph from exact package edges, crate edges,
  dependency aliases, verified source revisions, target-relative search roots,
  and complete structural requester ancestry, including parents without an
  intermediate source file;
- `ZOM3026 ModuleDeclarationNameMismatch` is emitted at the complete module
  declaration range without degrading to an identity invariant, while verified
  parser results remain available to AST tooling after later semantic failure;
- focused sanitizer tests cover provider-root admission, imported-source fixed
  points, missing intermediate source modules, declaration mismatch, and exact
  graph edges; the compiler-session, package, and binder architecture gates
  pass with the fixed-point scheduler;
- the session executes Binder modules in dependency order, projects imports,
  module aliases, local and foreign re-exports from completed verified export
  surfaces, and publishes only `VerifiedBindingOutput` values;
- the raw Binder, compiler symbol rail, AST `BindingMetadata`, polymorphic type
  rail, old Checker passes, and AST-to-IR lowering entry are absent, with
  positive and negative architecture gates preventing reintroduction; and
- the current sanitizer configure and complete build pass after the direct
  replacement; complete repository test evidence is still required.

The following RFC 0008 implementation surfaces remain open:

- signature, interface, coherence, and body phases after dependency-ordered
  Binder scheduling;
- immutable `VerifiedModuleInterface` publication and requester views;
- the context-checked `SignatureStore` and checked-facts repository;
- session-global impl coherence construction;
- production construction of RFC 0005 signatures and checked facts, blocked by
  RFC 0015 while its canonical operator and impl-pattern codecs remain under
  unapproved `REVIEW`;
- extend the architecture gate when module stores exist so negative fixtures
  also reject mutable foreign type environments, direct foreign AST reads,
  and bypasses around `SignatureStore` or `CheckedFactsRepository`;
- same-package multi-target, worker-permutation, interface codec, signature
  authorization, and global coherence conformance;
- sanitizer build, complete test matrix, format, RFC, and architecture gates
  after the full implementation.
# RFC 0015 Accepted Overlay

RFC 0015 was approved at exact review SHA-256
`642836225d54f6fa28f8c27e9985972081dbd221c2e8f3e61a0aafd04fe9bb1e`.
Its accepted-file SHA-256 is
`9704d5651606e8a74034c8af4be5172b4007a6c9f0ee8ea2f5ee183223401c01`.
The overlay directly replaces the RFC 0008 impl, explicit-marker,
coherence-input, and module-interface contracts named by RFC 0015.
