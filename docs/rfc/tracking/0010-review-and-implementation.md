# RFC 0010 Review And Implementation Tracker

This document is the local discussion and tracking record for RFC 0010. It does
not approve the proposal. RFC status, approvers, and the recorded decision
remain authoritative in the proposal frontmatter.

## Discussion Record

### 2026-07-10 Initial RFC Governance Review

The `rfc` owner returned the initial review draft for these blocking issues:

- discussion and tracking links pointed back to proposal sections instead of a
  real tracking artifact;
- Repository Impact omitted agent-routing, CLI, CMake, and actual path owners;
- implementation work preceded the acceptance decision in the ordered plan;
- dependency acceptance order was not explicit;
- deterministic identities, MIR typestate, generic ownership checking, and
  CLI file naming needed precise decisions.

The review confirmed that the Rust, Swift, MLIR, and LLVM prior art is relevant
and that a three-layer direction is appropriate review input.

### 2026-07-10 Initial IR Backend Review

The `ir-backend` technical review returned the draft for these blocking issues:

- Built MIR did not contain every real exit edge before ownership analysis;
- semantic identities were not defined independently from local interners;
- the frontend handoff did not guarantee complete, frozen semantic facts;
- Built MIR, ownership facts, and executable MIR lacked revision-safe typestate;
- LIR lacked concrete type, layout, function ABI, and runtime symbol contracts;
- monomorphization identity, roots, worklist ordering, and recursion handling
  were incomplete;
- place execution semantics and coroutine elaboration were underspecified;
- the rollout could expose two lowering entry points on the main branch;
- RFCs 0004, 0005, 0006, 0007, 0008, and 0009 require coordinated amendments.

### 2026-07-10 Initial Spec Alignment Review

The `spec-audit` review found that `docs/design/architecture.md` describes
nonexistent CompilerSession, plugin, LLVM, object, and concurrency behavior;
several normative spec chapters also describe unsupported HIR, MIR, LLVM, GPU,
module artifact, and backend contracts. Those claims must be removed or made
representation-independent before RFC 0010 can be accepted.

### 2026-07-10 Lowering Diagnostic Review

User review identified raw lowering errors outside the diagnostic definition
registry. Inspection at review time confirmed that `irgen::LoweringError`
carried a display string and `zomc` prefixed it with `IR lowering failed:`.

RFC 0010 now requires typed failure facts. User-correctable and target/CLI
capability failures must map to registered diagnostics with source spans;
missing checked facts, stale proofs, and malformed IR must map to structured
`ZOM99xx` compiler invariant diagnostics. Raw assertion text is not a
user-visible diagnostic contract.

The implementation response is complete for the mixed prototype:
`diagnostics-lowering.def` registers `ZOM6001-ZOM6008` capability failures and
`ZOM9901-ZOM9903` invariant failures; `LoweringFailure` carries a closed kind,
phase, and AST node; `IrDumpFailure` carries a closed verifier site and the
relevant symbol, block, value, type, and table index. The driver owns
source-location mapping and preserves that complete structured context in the
registered invariant diagnostic. Target profiles and scalar widths are closed
types, layout-table access returns checked
results, lowering validates binding-metadata capacity and source-buffer
ownership, and IR dumping preflights every type, layout, function symbol,
block, SSA value, instruction, terminator, and panic metadata reference before
writing output. Legal empty returns remain capability failures rather than
compiler invariant failures. No `ZC_IREQUIRE` or `Vector<Own<Type>>` remains in
`compiler/irgen`. The sanitizer build and focused diagnostic, lowering,
interner, and layout unit tests pass. The RFC 0010 layer-verifier diagnostic
matrix remains future implementation.

The `ir-backend` owner re-reviewed this repair after the final enum-closure
tests. The owner approved the mixed-prototype failure boundary: malformed
layout-kind and tag-type values now return typed `InvalidLayout` failures with
zero output, and no remaining blocker was found in the pre-output verifier,
checked-input validation, empty-return classification, or `zc` ownership
surface. This approval is limited to the disposable prototype safety repair;
it does not resolve the RFC's crate-identity or dependency blockers and is not
an RFC acceptance decision.

### 2026-07-10 Package And Crate Identity Review

Chapter 21 defines a package as a distributable container that may own several
crate targets, while each crate is a separate compilation root. The reviewed
RFC identity `ModuleId { PackageId, ModuleIndex }` therefore permits a library,
binary, or build-script crate in the same package to allocate the same module
identity.

This is a semantic identity collision, not a naming preference. RFC 0008 and
RFC 0010 must define `PackageId`, then `CrateId`, then `ModuleId`; definitions,
impls, semantic types, persisted fingerprints, dumps, and deterministic order
must all include the crate boundary. The finding returns RFC 0010 to
`RETURNED` and supersedes the earlier no-P0 spec review result.

The first revision response delegates canonical identity to RFC 0011, makes
every IR identity crate-qualified, extends deterministic ordering through
package and crate targets, and adds same-package multi-crate acceptance
coverage. RFC 0010 is now `DRAFT`; module, binder, IR, and spec owners must
re-review the coordinated RFC 0011 and RFC 0008 contracts before this finding
can be marked resolved.

### 2026-07-11 Verified Dispatch And Failure Ownership Response

RFC 0010 hash
`1950d1ee3903c8f3b43fef9fd120e69a444c5d801c222879fdc3dfbcd2126c6f`
now requires RFC 0009 hash
`e2503601c6ef60ee67987fc7e6318bbd75a3a3dae58f5dd28c7fc935bc0d5b9a`.
The checked-module builder consumes matching RFC 0005 checked facts and RFC
0009 dispatch facts; RFC 0010 no longer declares a second call-target or
intrinsic algebra. Dyn methods remain logical through HIR and MIR, and only LIR
assigns target slots.

The diagnostic contract now gives every failure a session, module,
definition, or instance owner plus a layer-specific structural site. A
definition-local block, value, instruction, terminator, cleanup, or call
failure cannot be emitted with only an ownerless node or raw display string.
Internal failure enums remain typed facts; every visible code, severity,
headline, and placeholder schema remains in a diagnostics `.def` registry with
generated exhaustive mapping tests.

This response does not approve RFC 0010. `python3 scripts/check-rfc.py` and
`git diff --check` pass; all required owners must re-review the exact proposal
and dependency hashes.

### 2026-07-11 Closed IR Failure And Evidence-Lifetime Response

Focused error-system and coordinated module/IR review returned the preceding
contract because failure kinds and phases were still placeholders, verifier
ordering was incomplete, foreign checked-store handles could outlive their
issuer, and later IR wrappers did not prove the store revision they consumed.

The revised RFC 0010 proposal hash is
`593358797ce415eae0c608850afcd3a1c593bceb7746389b91a4b06bbcb273b8`.
Its exact dependency hashes are RFC 0005
`318b56973fec2bf4dcd80eda192ba19037a8c7c12591196a4d6436a052a3cbed`,
RFC 0008
`a9a45572111ef1646050591ced5402562136fbb2120b421a0a89167ebbbe873a`,
and RFC 0009
`1c2234867f4a654424435e6f76ad53a9763dae7ec86c22d8540a21422db7f335`.

The proposal now closes every IR failure kind, phase, owner, site, and backend
operation; defines single-valued classification and stable ordering; maps
capability failures to `ZOM6008-ZOM6009` and invariants to
`ZOM9942-ZOM9949`; and requires generated test-only injection without a
production free-form error API. The same RFC 0008 checked-evidence lease now
survives checked-module construction, every verified IR layer,
monomorphization, and backend translation.

This is a draft response, not an acceptance decision. Required owners must
re-review this exact coordinated hash set.

### 2026-07-11 Operation-Result And Aggregation Response

Error-system exact-hash review returned the preceding proposal because it had
failure facts but no closed operation-result branches, no unified identity/IR
ordering, no registered typed monomorphization recursion or budget failure, and
no occurrence aggregation contract.

RFC 0010 is resubmitted at
`568b462f6d07cc3f68cf69c76bffd7313581c40359ad2ed124bfe66130b3b44f`.
Its exact dependency hashes are RFC 0005
`76fa033bbfe845a0b4000d8a42ac7035b997813929c47628fb3520786f9027ee`,
RFC 0008
`f9f54af941d4a103dfafe37c36ac975bc68b03ae9be378843ce7ef5b26843b79`,
and RFC 0009
`6be4a5cad81f7696b0187361e72ff4d3a85e066f576f55f37deab1943733262f`.

Every IR operation now returns a mutually exclusive verified, capability,
identity-invariant, or IR-invariant result. Recursive expansion and deterministic
budget exhaustion retain canonical roots and expansion chains and map to
`ZOM6010-ZOM6011`. Identity and IR facts have one merged order; adjacent
invariants aggregate by code/location while retaining every complete fact.

This response records no approval or status transition.

### 2026-07-11 Coordinated Hash Refresh

RFC 0010 remains byte-identical at
`568b462f6d07cc3f68cf69c76bffd7313581c40359ad2ed124bfe66130b3b44f`.
Its coordinated frontend hashes after indexed-assignment closure are RFC 0005
`3f38d165cfad83a3cc53cb53f8a3323766b340a9e8c79d55e11a48c82ea64c95`,
RFC 0008
`f9f54af941d4a103dfafe37c36ac975bc68b03ae9be378843ce7ef5b26843b79`,
and RFC 0009
`b368fa69ca03afcb360ed7145edb94ee918cc19906bbd0cd350dcf65accd93cd`.
Any coordinated approval must use this complete exact set.

### 2026-07-11 Exact-Hash Entry Review Verdict

Binder-checker, error-system, and coordinated module/IR review approved RFC
0010 hash
`568b462f6d07cc3f68cf69c76bffd7313581c40359ad2ed124bfe66130b3b44f`
as formal-review input. The verdict confirms the checked-evidence lifetime,
sole dispatch ownership, closed operation and failure results, typed
monomorphization rejections, registered `.def` mapping, and deterministic
aggregation. This is not an acceptance decision; task-router, `rfc`,
concurrency, runtime-memory, spec-audit, and verification reviews remain open.

### 2026-07-11 Verification And Ownership-Spec Return Response

Verification returned the preceding proposal because the four-phase MIR
revision lacked an executable byte oracle, stale/foreign/swapped proof lineage
mutations were incomplete, and worker/input permutations plus same-package
multi-target identity were not explicit. Spec review additionally found that
raising-call success/residual roles vanished at the checked-module handoff and
Chapter 14 claimed ARC, weak references, and manual allocation while RFC 0007
and the MIR design require affine ownership.

RFC 0010 is resubmitted at
`aa383df086896793af8d87fae0fe41aa345c02f4ccd699c001d71fd7ee30cda5`.
Its coordinated hashes are RFC 0005
`ac6b518d4c900daf3e1d64c2abc5e26475b24aa5bda5fe1da3573f65cd4d13bb`,
RFC 0008
`8f0aa6c1ad3f223c71247523cf2d5031b348c3d9d5a7c8e3110510f1429ad1f2`,
and RFC 0009
`6bebc5a230e498d799176cbe89c3862461cbeea5a9b4e1fddb25b3442d0291be`.

The response defines the four `MirRevisionPhase` values, canonical revision
inputs and proof lineage, the executable 114-byte Built-MIR oracle, and the
complete stale, foreign-context, foreign-module, swapped-function,
wrong-phase, wrong-origin, and wrong-certificate negative matrix. The checked
handoff and HIR/MIR now preserve RFC 0005 success, canonical result, residual,
and error-union-shape facts. Chapter 14 and MIR use only copy, move, borrow,
logical drop, and drop-and-replace; no retain, release, weak-reference, or
implicit ARC operation exists.

This response records no approval. Task routing, governance, concurrency,
runtime-memory, spec, and verification owners must review the new exact hash.

### 2026-07-11 Governance And MIR Wrapper Response

Governance review returned the preceding response because stale exact-hash
approvals remained in the checklist and the implementation sequence omitted
direct dependencies RFC 0009 and RFC 0011. Verification additionally found
that transformed MIR wrappers carried proofs but not the transformed modules
needed for revision recomputation, and that the revision input excluded a
valid zero-function module.

The implementation sequence now resolves RFC 0011, RFC 0004, RFC 0005, RFC
0008, and RFC 0009 before RFC 0010 acceptance. Every owner is reset for fresh
review. Each Built, drop-elaborated, coroutine-elaborated, and executable
wrapper owns its MIR module and recomputable revision. The function sequence
may be empty; the empty Built module has a 105-byte oracle with SHA-256
`f866a41f5ebc10d5c61110941c29350050c54114aa569f44038bd380b3fc013b`.

The current coordinated proposal hashes are RFC 0005
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

RFC 0005 restores `ForcedChecked` and advances the checked-facts codec to v2.
RFC 0010 now requires Built MIR to execute the recorded check once and branch
to a logical `ForcedCast` panic terminator without fabricating a residual
payload. The coordinated proposal hashes are RFC 0005
`f382b82aaa055fb3676a1578fcf73e1ba1ca030671b96e97294bbc55db8c19c1`,
RFC 0006
`aea15335a11da7d59a579d713abfb30267d72a8043f90988dfb598a8cfb06bda`,
RFC 0008
`3f77e6d785611f8f828d0fc6865606898af8e6a0e22b628b279bc4823aca691d`,
RFC 0009
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`,
and RFC 0010
`367bcf4f5ae4ba783e564f1dee5813f14d63ee831a21ce3a654211f429c7d0a8`.

### 2026-07-11 Checked Cast Handoff Return And Response

Semantic review returned the preceding proposal because MIR consumed an RFC
0005 `CheckedCastFact` that was absent from the verified frontend handoff and
HIR expression contract. The response adds the complete checked-cast map to
`VerifiedCheckedModule`, preserves every cast field on HIR cast expressions,
copies the fact unchanged into MIR input, includes it in deterministic dumps
and revisions, and adds field-complete golden and negative verification gates.
The updated RFC 0010 proposal SHA-256 is
`d850ab5b536584f87ffc3e5ee3299e439206705d2221aff23b81d9bf6bb3c964`.
The returned hash
`367bcf4f5ae4ba783e564f1dee5813f14d63ee831a21ce3a654211f429c7d0a8`
and every approval recorded for it are superseded by the updated proposal.

### 2026-07-11 Cast Identity Projection Return And Response

IR and semantic review returned
`d850ab5b536584f87ffc3e5ee3299e439206705d2221aff23b81d9bf6bb3c964`
because copying the complete RFC 0005 cast fact into MIR would leak its AST
`NodeId`, and because the proposal referenced an undefined HIR revision. The
response defines `HirCheckedCastFact` keyed by `HirNodeId` and
`MirCheckedCast` keyed by deterministic `MirOperationSite`. `HirBuilder`
discards the frontend association `NodeId`; all non-identity semantic fields
remain equal across layers and participate in `MirRevisionId`. Verification
tests cover the identity projection and every semantic field. The updated RFC
0010 proposal SHA-256 is
`857ce8361c684b9a7e10e9ccca66d6a4d01e415a0619b0c049ad5648bc745d87`.
All approvals on the returned hash are superseded.

### 2026-07-11 Registered Target Selection Reopen

Formal RFC 0012 review found a second incompatible target codec in the package
proposal. The repair deletes backend target reconstruction from RFC 0012 and
publishes only `RegisteredTargetSelection`. RFC 0010 is now the sole owner of
`CanonicalTargetSpec`, its v1 codec, `TargetSpecId`, target capability failures,
and target invariants. `VerifiedTargetSelection` consumes the exact package
selection, verifies registry revision, profile, semantic projection, and panic
strategy, then publishes the canonical backend profile and target ID. RFC 0010
now directly requires RFC 0012. The current proposal hash is
`373ca47a7f0d28734435819af0ba84ab948748c332587cb68aa664f1023e1959`;
all approvals on `857ce83...` are superseded.

### 2026-07-11 Raw-Pointer Cast Handoff Reopen

Fresh RFC 0005 semantic review found that its closed cast fact could not encode
the source language's unsafe raw-pointer reinterpret operation. RFC 0005 now
defines `RawPointerReinterpret`, the exact pointer mutability matrix,
`Guaranteed` mode, `RawPointerBoundary`, and checked-facts codec v3. RFC 0010
now preserves this kind and unsafe requirement through HIR and MIR, lowers it
without a failure continuation, forbids downstream pointee/mutability
reclassification, and requires complete matrix fixtures.

The coordinated proposal hashes are RFC 0005
`b81dd2239fea6d147d83a46ee3dcb4d6b6d345044bf5e4270b21768508d83ddf`
and RFC 0010
`715ae992a29e7ff83e4abf6e6c91d979bffccf7cae55ded450d80dfc730d70fe`.
Every exact-hash approval on preceding RFC 0010 bytes is superseded. All owners
must review the new bytes; this response is not an approval.

### 2026-07-11 Current Dependency And Exact-Hash Rebase

Fresh semantic and invariant review approved RFC 0010 proposal hash
`715ae992a29e7ff83e4abf6e6c91d979bffccf7cae55ded450d80dfc730d70fe`
technically but returned this tracker because it did not record the now-accepted
dependency set and still described the dependency gate as blocked.

The current accepted dependency hashes are RFC 0004
`cabcfb6b4e1ade93f09398b9a2c1ecf092931a4d012976ebb495eda68eed1843`,
RFC 0005
`5c61e7a993867385f9a895054d25e4a9fe6f891b1c26d55fd1a4dfb3b3bb7d35`,
RFC 0008
`eb0173bb6d69b6425bfc2379e8eb2b70c841c96acb07e12342ff03b85c91cf9c`,
RFC 0009
`d29bac1e9cad25cee673e17c6b922ba935b669549dc8c44f05eba5900e75f362`,
RFC 0011
`c699bee455adc2c2bafcfaef24edc20ee87d6a4c6f86756ae50ec2e87c565ade`,
and RFC 0012
`42c4190969c009b95be79ca741176b6cb5eee3315f2195486f5913bb76e3eb8b`.
All ten owners must review the current proposal and tracker bytes; this rebase
is not an approval.

### 2026-07-11 Exact-Hash Acceptance Review Approval

Formal semantic and invariant review approved RFC 0010 proposal hash
`715ae992a29e7ff83e4abf6e6c91d979bffccf7cae55ded450d80dfc730d70fe`
with tracker hash
`5fd3a6d4e90dc6c036a6c391f39e849ed4c67b8afc24ebfd952c733fa847bb39`.
Semantic review approved task routing, binder/checker, module-system,
error-system, concurrency, IR/backend, runtime-memory, and spec-audit surfaces.
Invariant review approved target, feature-gate, MIR lineage, evidence,
diagnostic mapping, deterministic codec, and verification surfaces. The 111-,
52-, 49-, 146-, and 137-byte oracles recompute exactly. Internal IR failure
facts remain closed typed algebras and map exhaustively to registered `.def`
diagnostics; no raw failure string is a public diagnostic path.
`scripts/check-rfc.py` and `git diff --check` pass.

The `rfc` governance owner remains pending. This technical approval does not
populate proposal frontmatter, record an acceptance decision, or authorize
implementation until governance approves the tracker and atomic transition.

## Owner Review Checklist

| Owner | Review State | Blocking Surface |
|---|---|---|
| `task-router` | APPROVED at `715ae992...` | Registered-target plus complete cast ownership and escalation completeness |
| `rfc` | APPROVED at `715ae992...` | Accepted dependency set, owner parity, decision, and atomic transition |
| `binder-checker` | APPROVED at `715ae992...` | Complete verified checked-module, cast facts, and dispatch inputs |
| `module-system` | APPROVED at `715ae992...` | Registered target, package, crate, module, interface, and lease identities |
| `error-system` | APPROVED at `715ae992...` | Target capability/invariant ownership, cast failure mapping, unsafe facts, and panic handoff |
| `concurrency` | APPROVED at `715ae992...` | Unsafe facts and forced-cast panic across task boundaries and coroutine elaboration |
| `ir-backend` | APPROVED at `715ae992...` | Target selection, cast legality, raw-pointer reinterpret, identity projection, failure continuation, and evidence lifetime |
| `runtime-memory` | APPROVED at `715ae992...` | Target/runtime capability, pointer reinterpret, forced-cast panic, drop, FFI, and runtime ABI boundaries |
| `spec-audit` | APPROVED at `715ae992...` | Target ownership, identity, cast matrix, error-role handoff, and Chapter 14 alignment |
| `verification` | APPROVED at `715ae992...` | Target mapping, cast matrices, MIR wrappers, oracles, lineage, identity, and permutations |

No historical owner approval remains current after the registered-target and
raw-pointer handoff changes. Current exact-hash owner approvals, dependencies,
and the governance decision gate are satisfied.

## Decision Record

Decision: ACCEPTED.

### RFC 0013 Additive Overlay

RFC 0013 was accepted on 2026-07-11 at proposal SHA-256
`e3909d5caad48a1c0255ee57d2a8fcc327e046945f20a586e0c6bf0115a237c3`.
It supplies the ownership source-rejection seam, borrow-evidence handoff, MIR
revision `v2`, and complete successor evidence-lineage replacement clauses;
this accepted RFC 0010 proposal remains byte-identical.

On 2026-07-11, all ten required owners approved RFC 0010 proposal hash
`715ae992a29e7ff83e4abf6e6c91d979bffccf7cae55ded450d80dfc730d70fe`.
RFC 0004, RFC 0005, RFC 0008, RFC 0009, RFC 0011, and RFC 0012 were accepted.
The accepted design freezes semantic HIR, target-independent MIR,
target-dependent LIR, verified evidence and proof lineage, target selection,
cast and error lowering, ownership/drop/coroutine ordering, runtime and ABI
boundaries, typed internal failure facts, exhaustive registered diagnostic
mapping, deterministic codecs, and backend cutover. The Canonical IR Direct
Replacement Series started on 2026-07-16. RFC 0010 therefore advanced from
`ACCEPTED` to `IMPLEMENTING` without changing the accepted semantic contract.
RFC 0006 and RFC 0007 may advance through their own gates.

## Implementation Tracker

### Canonical IR Direct Replacement Series

This series replaces the mixed `compiler/irgen` prototype with the accepted
three-layer pipeline. It introduces no adapter, compatibility decoder, second
lowering entry point, or source-visible placeholder mode. RFC 0013's MIR
revision v2 and ownership-evidence clauses apply together with RFC 0010.

| Slice | State | Required evidence |
|---|---|---|
| Extract canonical target registry and verified selection into `compiler/ir` | In progress | Context-bound selection, exact target codecs, complete negative matrix, and deterministic target oracles |
| Remove the mixed `compiler/irgen` prototype and non-producing IR CLI surface | In progress | No `zom.ir.v0`, `OutputType::IR`, `--emit=ir`, empty IR conformance target, or prototype diagnostic remains |
| Publish the verified checked-module handoff | Pending RFC 0005 and RFC 0008 checked-fact closure | Exact input leases, revisions, identities, and failure algebra |
| Implement semantic HIR and verifier | Pending checked-module handoff | Complete field projection, deterministic revision, mutation matrix, and dump coverage |
| Implement Built MIR and RFC 0013 ownership integration | Pending HIR | MIR revision v2, complete exits, ownership facts, drop and coroutine lineage, and permutation evidence |
| Implement target LIR, ABI lowering, and monomorphization | Pending executable MIR | Target legality, layout, ABI, instance identity, worklist, verifier, and deterministic artifacts |
| Implement LLVM translation and native artifacts | Pending verified LIR | Total translation, runtime and FFI boundaries, object and link outputs, and registered failures |
| Complete repository cutover | Pending all production layers | Layer diagnostics, conformance, architecture gates, documentation, sanitizer, and full default suite |

The first two slices form one removal-first cut: target selection moves to its
canonical owner before the complete `compiler/irgen` directory and every
non-producing consumer are deleted. No HIR or MIR placeholder is permitted to
stand in for unavailable checked facts.

## Verification Evidence

- `python3 scripts/check-rfc.py`: passed for 11 proposal RFCs on 2026-07-10
  before the first owner review.
- `git diff --check`: passed for the initial RFC and routing changes.
- `ir-backend` prototype failure-boundary re-review: approved after the
  complete dump preflight, invalid-enum negative tests, checked metadata/source
  handoff, empty-return classification, and ownership repair.
- The registered IR diagnostic architecture gate and four negative fixtures
  prevent raw assertion/string failures, unmapped closed facts, and missing
  diagnostic definitions from re-entering the mixed prototype. This does not
  implement RFC 0010's HIR/MIR/LIR verifier matrix.
- Exact-hash semantic, invariant, routing, verification, and RFC governance
  reviews are approved. Structural checks alone are not an acceptance decision.
