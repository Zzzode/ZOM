# RFC 0006 Review And Implementation Tracker

This document is the local discussion and tracking record for RFC 0006. It does
not approve the proposal. RFC status, approvers, and the recorded decision
remain authoritative in the proposal frontmatter.

## Discussion Record

### 2026-07-10 Governance And IR Boundary Review

The proposal was returned for these blocking issues:

- discussion and tracking fields pointed back to proposal sections instead of
  a review artifact;
- `ir-backend` was absent from required owners even though the proposal owns
  `irgen`, target layout, CLI emission, and backend contracts;
- substantial lowering and runtime experiments preceded an acceptance
  decision;
- RFC 0010 and RFC 0011 do not yet provide accepted IR and semantic identity
  dependencies;
- cleanup graphs, multi-residual propagation, runtime panic calls, FFI panic
  containment, main exit behavior, and cross-module ABI identity remain open.

The structured lowering diagnostic repair is valid as a local safety fix:
user-reachable capability failures use registered `ZOM60xx` diagnostics and
compiler invariant failures use registered `ZOM99xx` diagnostics. This repair
does not approve the surrounding IR architecture.

The follow-up raw-error audit classified every current failure site by who can
trigger it:

- every `LoweringFailureKind` value is exhaustively mapped by `zomc` to
  registered `ZOM6001` through `ZOM6004` or `ZOM9901` diagnostics;
- CLI emission preconditions and capabilities use registered `ZOM6005` through
  `ZOM6008` and `ZOM9902` diagnostics;
- an output-path filesystem exception that previously escaped the diagnostic
  boundary is caught and reported as `ZOM6008`;
- checked but currently unsupported return, propagation, and forced-unwrap
  type shapes report `ZOM6002` rather than being misclassified as `ZOM9901`;
- target profiles and scalar widths are closed types; layout-table operations
  return checked results; lowering validates binding-metadata capacity and panic
  source-buffer ownership; the dumper validates every type, layout, symbol,
  block, value, instruction, terminator, and panic metadata reference before
  writing output; no `ZC_IREQUIRE` remains in `compiler/irgen`.

The final architecture still requires typed verifier failures at every IR
boundary. The current assertion count is evidence about this disposable
prototype, not an accepted allowance for unstructured release failures.

### 2026-07-11 Role-Bearing Error-Union Response

Semantic review returned RFCs 0005 and 0010 because this proposal still treated
ordinary structural unions as sufficient input for `?!` and `!!` lowering and
described nominal `Result<T, E>` as an error-model alternative. The proposal now
requires RFC 0005 `ErrorUnionShapeFact`, RFC 0009 role-bearing dispatch, and RFC
0010 verified MIR. Layout descriptors retain exact success and residual keys;
ordinary unions and nominal enums cannot enter error lowering without the
verified shape.

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
RFC 0006 has legally re-entered `DRAFT`, and every owner is reset for exact-hash
review. No approval or further status transition is recorded.

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

RFC 0005 restores `ForcedChecked` as the semantic mode for `as! T` and advances
the checked-facts codec to canonical. RFC 0006 now defines the corresponding logical
cast-or-panic control flow, `ForcedCast` panic metadata, absence of a residual
payload, and cleanup behavior. The coordinated proposal hashes are RFC 0005
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

### 2026-07-11 ABI Framing, Error-Role Spec, And Dependency Return

Fresh exact-hash review returned RFC 0006 proposal hash
`aea15335a11da7d59a579d713abfb30267d72a8043f90988dfb598a8cfb06bda`.
The target-artifact ABI revision had no exact multi-layout stream or oracle,
the FFI facts revision left definition count and per-key framing implicit, and
Chapter 4 incorrectly inferred error success from the first canonical union
alternative. The tracker also retained checked-facts canonical and obsolete dependency
state after declaring the preceding coordinated approvals superseded.

The proposal now defines count-plus-length framing, canonical order, duplicate
rejection, and illegal-framing matrices for both artifact layouts and FFI
definitions. The 146-byte target-artifact oracle hashes to
`290d95e132c99dba891dd3519927363c33800346b055e1dcbca340f45183f9b9`;
the 405-byte descriptor and 178-byte FFI oracles reproduce their canonical
vectors.
Chapter 4 now requires the same verified error-union role fact as Chapter 11,
uses `shape.successType` and `shape.residualType`, and never assigns roles from
canonical union order.

The current accepted dependency hashes are RFC 0005
`5c61e7a993867385f9a895054d25e4a9fe6f891b1c26d55fd1a4dfb3b3bb7d35`,
RFC 0008
`eb0173bb6d69b6425bfc2379e8eb2b70c841c96acb07e12342ff03b85c91cf9c`,
RFC 0009
`d29bac1e9cad25cee673e17c6b922ba935b669549dc8c44f05eba5900e75f362`,
RFC 0010
`c244a3ebae5b35e974a4b19331d218d0a1c6a1b9814729407d9afad1dd806124`,
and RFC 0011
`c699bee455adc2c2bafcfaef24edc20ee87d6a4c6f86756ae50ec2e87c565ade`.
RFC 0003 is LANDED. The repaired RFC 0006 proposal hash is
`016b0fd5d6011104b8e4e16951ae8be3b6f4d06000fcb4af9f79e242434baf4f`.
All owners must review the new exact bytes; this response is not an approval.

### 2026-07-11 Rejection Classification Return And Response

Invariant review returned proposal hash
`016b0fd5d6011104b8e4e16951ae8be3b6f4d06000fcb4af9f79e242434baf4f`
because its new artifact and FFI framing mutations were all described as
generic rejection or `InvalidFact`. That contradicted RFC 0010's mandatory
single-valued classification and would bypass `AdditionalFact` and
`CanonicalCodecMismatch` precedence.

The response binds target-artifact failures to `ObjectEmission` with `Session`
owner: duplicate role keys are `AdditionalFact`, authorized input revision
mismatches are `InputRevisionMismatch`, invalid present fields are
`InvalidFact`, and framing/order/recomputation errors are
`CanonicalCodecMismatch`. FFI duplicates are `AdditionalFact`, inventory-key
disagreement is `InvalidFact`, and framing/order/length errors are
`CanonicalCodecMismatch`. Tests assert exact `ZOM9948`, `ZOM9949`, or `ZOM9955`,
sort position, and no publication.

The repaired proposal hash is
`0b8915df3a7d5a49a52b3980bd8063edff7b24c4d0bc08a18697048e567d9ebc`.
All owners must review this exact hash; this response is not an approval.

### 2026-07-11 Exact-Hash Acceptance Review Approval

Formal semantic and invariant review approved RFC 0006 proposal hash
`0b8915df3a7d5a49a52b3980bd8063edff7b24c4d0bc08a18697048e567d9ebc`
with tracker hash
`6b4e63502736e115cd1617194744680bbad81c2dad0b86e27e64164bfdbe4e68`.
Semantic review approved binder/checker, error-system, module-system,
IR/backend, runtime-memory, and spec-audit surfaces. Invariant review approved
descriptor, artifact, FFI, panic-lifetime, failure-classification, and
verification surfaces. The 405-, 146-, and 178-byte oracles recompute exactly;
Chapter 4 and Chapter 11 use the same verified error-role facts. RFC, parser,
lexer, and diff checks pass.

The `rfc` governance owner remains pending. This technical approval does not
populate proposal frontmatter, record an acceptance decision, or authorize
implementation until governance approves the tracker and atomic transition.

### 2026-08-24 Error-Operator Lowering Blocker Verification (no status change)

This entry records the verified current-code state of the `?!`, `!!`, and `as!`
operators against the "Built MIR control flow", "Cleanup and drop elaboration",
and "Runtime and CLI cutover" tracker rows, all of which remain `Pending`. It
authorizes no implementation and changes no status; it documents why an
end-to-end error-operator lowering slice is not frontend-reachable today.

Confirmed present (front end, already landed):

- Lexing/parsing of `?!` (`ErrorPropagate`), `!!` (`ErrorUnwrap`), and `?:`
  (`ErrorDefault`) as postfix/`ast::PostfixExpression` productions, and `as!`
  as an `ast::CastExpression` with forced mode; all AST-tested under
  `tests/conformance/corpus/11-error/` and `.../04-expressions/`.
- Checker type model for `raises` (`type::FunctionTypeData.raises`, signature
  and dispatch facts), error-union shape/operator fact types
  (`checker/inference/checked-facts.h`), and all four diagnostic codes
  (`ErrorPropagateOutsideRaises`, `ErrorUnwrapNonUnion`, `ErrorPropagateNonUnion`,
  `ErrorUnionEmpty`).
- The RFC 0005 error-union layout descriptor, codec, and revision
  (`compiler/ir/error-union-layout*`) as verified pure data with an exact oracle.

Confirmed blocking (each gates the next):

1. The checker's `BodyProductionKind::ErrorOperator` production stage currently
   emits no success-path `ErrorOperatorFact`/`ErrorUnionShapeFact`: the single
   handler unconditionally calls `rejectNonUnionErrorOperator`
   (`checker/body/body-checker.cc`), so a well-formed `?!`/`!!` produces no
   positive facts. Positive fact emission is unbuilt.
2. Semantic HIR fails closed on any error-union fact and on any `raises`
   function or call: `noUnsupportedFacts` requires zero `errorOperators`,
   `errorUnionShapes`, and `casts`, and every function/invocation acceptance
   predicate rejects `callable.raises != zc::none` /
   `invocation.raises != zc::none`. A raising function cannot reach HIR, and
   `?!` requires a raising enclosing function, so `?!` cannot lower without an
   HIR error-union return representation.
3. Built MIR has no panic/abort terminator or statement (only `Unreachable`),
   and RFC 0006 Chapter 11.3.2 intentionally leaves panic formatting, unwind,
   and abort undefined at this stage. `!!` and `as!` are cast-or-panic /
   unwrap-or-panic forms; lowering them requires the panic ABI and unwind
   contract that this RFC's own `Cleanup and drop elaboration` and
   `Runtime and CLI cutover` rows own. Implementing a panic edge now would
   invent an unsigned contract, which the spec-alignment rules forbid.

Conclusion: the reachable frontend surface for the error operators is already
landed; the remaining work is the RFC 0006 Built-MIR / panic-ABI slice, which
depends on the RFC 0013 MIR ownership integration (advanced separately) and the
accepted panic-lifetime contract. No partial error-operator MIR lowering may be
published ahead of those, and none is by this entry.

## Owner Review Checklist

| Owner | Review State | Blocking Surface |
|---|---|---|
| `rfc` | APPROVED at `0b8915df...` | Accepted dependency set, owner parity, decision, and atomic transition |
| `binder-checker` | APPROVED at `0b8915df...` | Complete verified cast and error-union role facts with opaque checked revision |
| `error-system` | APPROVED at `0b8915df...` | Propagation, unwrap, forced-cast panic, residual, FFI, and diagnostic rules |
| `module-system` | APPROVED at `0b8915df...` | Cross-module descriptor publication, interface binding, and identity |
| `ir-backend` | APPROVED at `0b8915df...` | Error and cast lowering, cleanup graph, target ABI, artifact framing, and backend ownership |
| `runtime-memory` | APPROVED at `0b8915df...` | Panic ABI, borrowed/owned lifetime, drops, unwind, FFI containment, and main boundary |
| `spec-audit` | APPROVED at `0b8915df...` | Chapters 3, 4, 11, and 18 role-based representation-independent semantics |
| `verification` | APPROVED at `0b8915df...` | Descriptor, artifact, FFI codecs, cleanup, panic, ABI, CLI, and conformance matrices |

All historical owner approvals are superseded by the ABI framing and spec
changes. Dependencies and technical owner gates are satisfied, but the
governance decision gate is also satisfied.

## Decision Record

Decision: ACCEPTED.

On 2026-07-11, all eight required owners approved RFC 0006 proposal hash
`0b8915df3a7d5a49a52b3980bd8063edff7b24c4d0bc08a18697048e567d9ebc`.
RFC 0003 was LANDED and RFC 0005, RFC 0008, RFC 0009, RFC 0010, and RFC 0011
were ACCEPTED. The accepted design freezes role-bearing error-union lowering,
`?!`, `!!`, and `as!` control flow, cleanup and drop ordering, target-specific
layout and artifact identity, abort/unwind panic lifetime, FFI and main
containment, registered diagnostics, exact codecs, and rejection
classification. The proposal entered `IMPLEMENTING` through the direct
replacement series below on 2026-07-17. RFC 0007 may advance only through its
own independent gate.

## Implementation Tracker

The direct replacement series started on 2026-07-17. The removed mixed
`compiler/irgen` prototype, AST/TypeEnv lowering, and fake `--emit=ir` are not
part of the compiler architecture.

| Slice | State | Required evidence |
|---|---|---|
| Verified frontend handoff | In progress | Canonical checked facts, dispatch facts, module interfaces, borrow evidence, Semantic HIR, and exact revision lineage |
| Error-role descriptors | Pending | Closed role-bearing descriptor codec, independent verifier, exact oracle, cross-module publication, and mutation matrix |
| Built MIR control flow | Pending | RFC 0013 MIR, `?!`, `!!`, and `as!` terminators, all residual variants, calls, and source-to-MIR conformance |
| Cleanup and drop elaboration | Pending | Path-complete drops on every normal, residual, panic, and unwind exit with certificate verification |
| Target layout and LIR | Pending | Verified target selection, complete layout records, ABI lowering, capability rejection, and deterministic snapshots |
| Backend artifacts | Pending | LLVM/object emission, artifact codec, symbol and relocation verification, FFI containment, and executable publication |
| Runtime and CLI cutover | Pending | Abort and supported unwind boundaries, panic metadata lifetime, main containment, real output modes, and no fake emission |
| Production cutover | Pending | Full sanitizer/default CTest, layer runners, conformance, determinism, architecture, format, RFC, and diff-hygiene evidence |

No slice may reconstruct semantics from AST nodes, mutable type tables, source
names, or the deleted `irgen` prototype.

## Verification Evidence

- Structured lowering diagnostics, lowering unit tests, layout unit tests, and
  the focused IR conformance slice pass under the sanitizer build.
- The invalid output-path IR conformance command reports only `ZOM6008`; it
  contains no uncaught exception, stack trace, or raw emission-failure summary.
- The lowering unit suite covers unsupported coercion and direct-layout forced
  unwrap as `UnsupportedExpression`, while missing and invalid dispatch facts
  have distinct invariant kinds and lowering phases.
- `scripts/check-ir-diagnostic-boundary.py --check` verifies that the mixed
  prototype returns typed lowering/dump failure facts, exhaustively maps every
  failure kind, phase, and verifier site, and registers the complete current
  diagnostic set. Its four negative fixtures reject a raw assertion, an
  unmapped failure kind, a string failure field, and a missing `.def` entry.
- `python3 scripts/check-rfc.py` validates document structure but does not
  constitute owner approval.
