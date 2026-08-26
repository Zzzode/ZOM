---
rfc: 7
title: Borrow Lifetime And Ownership Checker
type: compiler
status: IMPLEMENTING
author: ZOM Compiler Team
review-manager: rfc
required-owners: [task-router, rfc, binder-checker, module-system, error-system, concurrency, ir-backend, runtime-memory, spec-audit, verification]
approvers: [task-router, rfc, binder-checker, module-system, error-system, concurrency, ir-backend, runtime-memory, spec-audit, verification]
created: 2026-07-08
updated: 2026-07-25
area: compiler
requires: [5, 6, 10, 11, 13, 15]
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0007-review-and-implementation.md#discussion-record
decision: docs/rfc/tracking/0007-review-and-implementation.md#decision-record
implementation: docs/rfc/tracking/0007-review-and-implementation.md#implementation-tracker
tracking-issue: docs/rfc/tracking/0007-review-and-implementation.md
---

# RFC 0007: Borrow Lifetime And Ownership Checker

## Summary

This RFC defines the ownership analysis that consumes RFC 0013 Built MIR with
the `zom.mir-revision` lineage and its exact
`VerifiedBorrowEvidenceLease` through one live repository capability, plus the
RFC 0007 ownership event overlay. The
analysis proves initialization, moves, loans, non-lexical regions, reborrows,
reference escape, `Copy` and `Linear` marker obligations, checked-cast causal
transfer, and unsafe boundaries over one verified target-independent CFG.

Invalid source returns the fixed RFC 0013 `OwnershipAnalysisResult` source
branch. Successful analysis publishes immutable `VerifiedOwnershipFacts`
keyed by MIR identities and the exact Built MIR and borrow-evidence revisions.
The facts also bind the exact event-overlay revision.
Only matching facts can construct `OwnershipCheckedMir`. AST identities,
binder tables, type-environment trees, name lookup, and a separately rebuilt
CFG are not ownership inputs.

## Motivation

Chapter 14 defines affine ownership, deterministic cleanup, shared and mutable
loans, non-null references, normal-path linear consumption, and unsafe raw
pointer boundaries. These rules are path-sensitive. They cannot be proven by
local type inference or by following source syntax with a bounded traversal.

RFC 0010 supplies the required place-based CFG. RFC 0013 supplies the source
rejection branch and the revision-bound direct-reference relation needed at
calls and returns. RFC 0015 supplies one verified marker-proof authority and
the exact marker lineage carried by checked facts. RFC 0007 must now define one
complete proof contract that answers:

1. which projected places are definitely initialized at every CFG and event
   cutpoint;
2. whether a use observes a moved or partially moved place;
3. whether an issued, reserved, active, or reborrowed loan conflicts with an
   operation on an overlapping place;
4. whether every reference origin remains valid at every point where it is
   live or escapes to another storage region;
5. whether a mutable parent loan is restored precisely after its child region
   ends;
6. whether every positive `Linear` obligation is transferred and consumed
   exactly once on every normal exit;
7. whether a borrow escapes through a return, store, closure, raw carrier, or
   checked-cast result without an explicit proof; and
8. whether every unsafe operation has the verified acknowledgement required by
   its checked fact.

The output also drives RFC 0006 cleanup elaboration. A downstream pass must be
able to distinguish initialized, uninitialized, maybe-initialized, moved,
borrowed, and linearly obligated places without repeating semantic analysis.

## Goals

- Define `analyzeOwnership(VerifiedBuiltMir, VerifiedOwnershipEventOverlay,
  VerifiedBorrowEvidenceLease, BorrowEvidenceRepositoryCapability)` as the only
  source-rejecting ownership operation.
- Define a complete typed place, projection, move-path, point, checked-cast
  carrier, loan, region, escape, marker, unsafe-occurrence, drop-obligation,
  and linear-obligation model over Built MIR plus the RFC 0007 event overlay.
- Use path-sensitive fixed-point dataflow over every normal, residual, panic,
  unwind, and return edge present in admissible Built MIR.
- Define immediate and deferred loan activation, an authoritative checker-time
  activation projection in the event overlay, plus reborrow suspension and
  restoration without ownership-side checker lookup.
- Validate local, returned, stored, closure, raw-carrier, and checked-cast
  reference flows against RFC 0013 borrow evidence.
- Consume exact revision-bound `Copy` and `Linear` decisions plus checked
  capture, receiver-adjustment, and unsafe facts without resolving marker names
  or source syntax again.
- Define a closed `OwnershipSourceFailure` algebra with exact diagnostic
  mapping, deterministic suppression, ordering, and complete secondary facts.
- Define immutable `VerifiedOwnershipFacts`, its independent verifier, exact
  canonical codec, revision oracles, and proof-lineage mutation matrix.
- Define non-bypassable `OwnershipCheckedMir` construction and successor
  suppression on every rejected branch.
- Define deterministic parallel execution and finite monotone work budgets.

## Non-Goals

- This RFC does not add lifetime, region, or higher-ranked source syntax.
- Except for the direct deletion of RFC 0005's two raw-to-reference cast kinds
  defined below, this RFC does not change type inference, coercion, dispatch,
  marker proof, or coherence semantics owned by RFCs 0005, 0009, and 0015.
- This RFC does not define target layout, ABI, LLVM IR, stack placement, or
  coroutine frame layout.
- This RFC does not define task handles, task-scope evidence, scheduling,
  cancellation delivery, wake events, suspension semantics, or a memory model.
- This RFC does not admit `spawn`, `suspend`, resume, task-scope, cancellation,
  or coroutine-capture operations into ownership analysis while Chapter 15
  remains frontend-only.
- This RFC does not prove raw-pointer interior aliasing or pointer validity and
  therefore does not admit any raw-pointer-to-safe-reference conversion.
- This RFC does not persist MIR or ownership facts as a public artifact.
- This RFC does not add a second ownership CFG or a source-recovery path after
  ownership rejection.
- This RFC does not change RFC 0006 panic and cleanup semantics.

## Prior Art

### Rust MIR Borrow Checking

The Rust compiler performs move analysis and non-lexical borrow checking over
MIR places and CFG locations. Its move paths, loan facts, region constraints,
and two-phase borrow activation demonstrate that ownership legality belongs on
a normalized control-flow representation. ZOM adopts that phase boundary,
place orientation, and monotone dataflow discipline.

References:

- <https://rustc-dev-guide.rust-lang.org/borrow_check.html>
- <https://rustc-dev-guide.rust-lang.org/mir/index.html>
- <https://rustc-dev-guide.rust-lang.org/borrow-check/moves-and-initialization/move-paths.html>
- <https://rust-lang.github.io/rfcs/2094-nll.html>

### Polonius

Polonius separates origin, loan, subset, liveness, and invalidation facts. Its
fact-oriented model is valuable for an independent oracle and for testing
region inference separately from presentation diagnostics. ZOM uses a finite
point-set solution and explicit reborrow relations while keeping the initial
implementation within one compiler pass.

Reference: <https://rust-lang.github.io/polonius/>

### Swift Ownership And Exclusivity

Swift SIL makes ownership operations explicit and verifies exclusivity of
overlapping accesses. ZOM adopts the rule that mutable access is exclusive and
that verifier-backed ownership information must survive lowering. ZOM keeps
source borrow failures static and does not rely on a dynamic exclusivity check
for safe code.

References:

- <https://www.swift.org/documentation/swift-compiler/>
- <https://www.swift.org/blog/swift-5-exclusivity/>

### Move Reference Safety And Abilities

The Move bytecode verifier tracks resource moves, borrows, and abilities over
control flow. It demonstrates the benefit of separating value abilities from
borrow state and of rejecting invalid resource use before execution. ZOM uses
the analogous separation between checked marker proofs and MIR ownership
dataflow.

References:

- <https://move-language.github.io/move/references.html>
- <https://move-language.github.io/move/abilities.html>

### Common Failure Modes

The design explicitly prevents these recurring failures:

1. lexical block end is treated as loan end even though the reference remains
   live on another CFG path;
2. two distinct source expressions create a second place or CFG model and
   disagree with lowering about evaluation order or exits; and
3. copyability, linearity, two-phase activation, unsafe multiplicity, or
   reference origin is inferred from a name or bare location instead of a
   verified semantic fact.

## Guide-Level Explanation

Moving a non-`Copy` value makes its source place unavailable until that exact
place is reinitialized:

```zom
let first = make_buffer();
let second = first;
use(first); // ZOM4056 UseAfterMove
```

Disjoint fields remain independently usable when their place projections are
proven disjoint:

```zom
let left = pair.left;
use(pair.right);
```

A shared loan permits other shared loans but excludes mutation. A mutable loan
excludes every overlapping loan and consuming operation:

```zom
let read_ref = &value;
mutate(&mut value); // ZOM4058 MutableBorrowConflicts
read(read_ref);
```

A reborrow suspends the mutable parent only for the child region. Non-lexical
liveness restores the parent at the first point that is outside every child
region:

```zom
let exclusive = &mut value;
let shared = &*exclusive;
read(shared);
write(exclusive); // valid after the last use of shared
```

Reference results follow RFC 0013 direct-root evidence. A returned reference
may originate only from the selected receiver or parameter. A local reference
cannot escape:

```zom
fun invalid() -> &i32 {
    let value = 1;
    return &value; // ZOM4061 BorrowDoesNotLiveLongEnough
}
```

Positive `Linear` evidence creates an exactly-once normal-path obligation.
Moving transfers the obligation; returning, passing it to a consuming MIR
operand, or an accepted logical drop consumes it. Every reachable normal exit
must observe exactly one consumption.

An `unsafe` block acknowledges a specific unsafe operation. It does not disable
move, loan, region, or linear checks. A raw pointer derived from a reference
does not extend the referent lifetime, and no raw pointer can be converted into
a safe reference under this RFC.

## Reference-Level Design

### Normative Dependencies And Accepted Overlays

This proposal consumes the dependency contracts in this order:

1. RFC 0005 checked facts and RFC 0015 marker-proof closure;
2. RFC 0011 canonical semantic identity and ordering;
3. RFC 0006 cleanup and panic semantics;
4. RFC 0010 Built MIR and ownership/drop/coroutine artifact order; and
5. RFC 0013 borrow evidence, MIR revision, fixed ownership result, and
   successor lineage.

The current canonical text of each dependency is authoritative. RFC 0015 owns
the marker-proof and module-interface lineage clauses it names. RFC 0013 owns
the RFC 0010 ownership result, MIR evidence lease, and encoded wrapper fields
it names. This RFC defines the repository-capability ownership operation and
successor-constructor signatures below. It also defines the RFC 0005
checked-cast algebra without `RawConstToReferenceChecked` and
`RawMutableToReferenceChecked`. The remaining `CastKind` cases retain
declaration order and are tagged contiguously from `0x01` through `0x0d`.
The checker rejects both source/target shapes with `ZOM4013
CheckerInvalidCast` before publishing `CheckedCastFact` or `UnsafeScopeFact`.
The coordinated enablement transaction updates RFC 0005's checked-fact codec,
revision, oracles, implementation, and tests atomically before RFC 0007 input is
admitted. It also replaces, for the currently accepted `spawn` and
`suspend` AST nodes only, RFC 0010's claim that concurrency syntax reaches HIR
and Built MIR: those nodes fail at the bound-module admission gate defined
below because Chapter 15 publishes no checked semantic contract for them. The
result algebra, lease encoding, MIR domain, framing, and lineage fields do
not change; RFC 0007 completes the closed MIR statement tag set with the
direct-replacement unsafe-scope variant defined below and adds its event
overlay to the ownership wrapper and fact lineage. It also
supplies the qualified `RFC0007::OwnershipSourceFailure`,
ownership facts, algorithms, and proof construction required by those clauses.

RFC 0011 is `LANDED`. RFC 0006 and RFC 0013 recorded their independent
`ACCEPTED -> IMPLEMENTING` transitions on 2026-07-17. Those transitions
authorize only the slices named by their own trackers. They do not authorize
RFC 0007 analysis or publication before the complete coordinated gate defined
under Compatibility And Rollout.

### Ownership Operation And Input Validation

RFC 0013's lease identifies evidence but contains no authority that can resolve
its repository key. This RFC therefore defines one opaque, non-encodable
capability whose private constructor belongs to the live session repository:

```text
BorrowEvidenceRepositoryCapability
```

The capability exposes only verified lease resolution. Internally it binds the
exact `SemanticContextBrand`, repository `RegistryBrand`, and live session
epoch; callers cannot construct, clone across sessions, serialize, hash, or
inspect its repository reference. A capability becomes unusable before its
repository begins teardown.

The public operation directly replaces the RFC 0013 two-argument declaration:

```text
analyzeOwnership(
  built: Borrowed<const RFC0013::VerifiedBuiltMir>,
  overlay: Borrowed<const VerifiedOwnershipEventOverlay>,
  evidence: Borrowed<const RFC0013::VerifiedBorrowEvidenceLease>,
  repository: Borrowed<const BorrowEvidenceRepositoryCapability>,
) -> OwnershipAnalysisResult
```

The accepted `VerifiedBuiltMir` is valid input only when:

- `revision.phase` is `Built` and its codec domain is
  `zom.mir-revision`;
- the wrapper, module, revision preimage, and resolved evidence all contain the
  same `SemanticContextBrand`, `ContextFingerprint`, `ModuleId`,
  `CheckedFactsRevision`, `DispatchFactsRevision`, and
  `BorrowEvidenceRevision`;
- the overlay has the same context, module, checked-facts revision, and Built
  revision, and its revision recomputes under the RFC 0007 overlay codec;
- the supplied lease is byte-for-byte the wrapper lease and resolves through
  the supplied capability in the same live session-owned repository;
- every function, block, local, place, operation, source scope, and source span
  passes RFC 0010 Built MIR verification; and
- every ownership-relevant checked fact has exactly one MIR projection and no
  additional MIR fact exists.

### Pre-Checker Ownership-Surface Admission

Chapter 15 is frontend-only and publishes no checked task, suspension,
cancellation, join, or coroutine-capture fact. The checker cannot construct a
complete `VerifiedCheckedModule` for `SpawnExpression` because no task-handle
type or checked task fact exists. RFC 0010 `FeatureBoundaryVerification`
occurs after executable MIR and is not legal for this rejection. RFC 0007
therefore defines one binder-checker/session-owned source operation after
`RFC0005::VerifiedBoundModuleInput` construction and before signature or body
checking requires complete expression types:

```text
ConcurrencySyntaxKind = Spawn | Suspend

OwnershipSurfaceFailure {
  kind: ConcurrencySyntaxKind,
  primarySpan: SourceSpan,
  traversalOrdinal: uint32,
}

OwnershipAdmittedBoundModule {
  input: RFC0005::VerifiedBoundModuleInput,
}

OwnershipAdmittedCheckedModule {
  admission: OwnershipAdmittedBoundModule,
  checked: RFC0010::VerifiedCheckedModule,
}

OwnershipSurfaceAdmissionResult =
    Verified { module: OwnershipAdmittedBoundModule }
  | SourceRejected {
      failures: SortedNonEmptySequence<OwnershipSurfaceFailure>,
    }

admitOwnershipSurface(
  input: Moved<RFC0005::VerifiedBoundModuleInput>,
) -> OwnershipSurfaceAdmissionResult

buildHir(
  input: Borrowed<const OwnershipAdmittedCheckedModule>,
) -> RFC0010::IrOperationResult<RFC0010::VerifiedHirModule>
```

`ConcurrencySyntaxKind` tags are `Spawn = 0x01` and `Suspend = 0x02`;
`OwnershipSurfaceAdmissionResult` tags are `Verified = 0x01` and
`SourceRejected = 0x02`. Failure fields encode in declaration order. The phase walks the immutable AST
in schema traversal order and publishes one failure for every
`SpawnExpression` or `SuspendStatement`; order is validated primary span,
traversal ordinal, then kind tag. `OwnershipAdmittedBoundModule` has a private
constructor, owns the moved bound-module capability, and exists only when the
failure sequence is empty. Signature checking, body checking, and
`CheckedModuleBuilder` accept only a borrowed
`OwnershipAdmittedBoundModule`; their existing checked source and invariant
result algebras do not change.

After complete checking, a private session operation atomically moves the
admitted bound module and the matching `VerifiedCheckedModule` into
`OwnershipAdmittedCheckedModule`. It first validates semantic context, module,
source digest, AST tree identity, binding revision, checked-facts revision,
dispatch revision, and imported-interface revisions. A mismatch selects the
existing checker invariant branch and publishes no wrapper. The session
retains a successful wrapper until both HIR and the ownership event overlay
have been verified, then destroys it before the module transaction commits.
`SourceRejected` consumes the bound-module capability, emits every failure,
and constructs no signature facts, body facts, checked module, HIR, Built MIR,
event overlay, ownership fact, or successor. No API accepts the unadmitted
bound module for checking, and no API constructs an admitted checked wrapper
from caller-provided receipts or digests.

Both failure kinds map to the fresh `ZOM4095
ConcurrencySemanticsUnavailable`, Error, `Concurrency syntax has no admitted
semantic contract`, arity 0. The implementation transaction deletes the stale
unemitted `ZOM4067 ScopedTaskBorrowEscapes` and `ZOM4068
ScopedTaskReferentHere` registry rows and every associated emitter or
reservation; neither numeric code is reassigned. No ownership source failure,
task fact, task region, or task diagnostic survives past admission. A Built MIR
candidate containing spawn, suspend, resume, task-enter, task-join, task-exit,
cancellation, or coroutine-capture operations is therefore forged and selects
RFC 0010 `InvalidFact` during Built MIR input validation. Admitting any such
operation requires a separate accepted concurrency RFC that replaces this
admission result, restores the applicable RFC 0010 concurrency-to-MIR
contract, and updates Chapter 15 first.

### RFC 0007 Ownership Event Overlay

RFC 0013 MIR revision has no `ownershipEvents` or `unsafeOccurrences`
field. RFC 0007 therefore does not claim that either inventory is encoded in
`zom.mir-revision`. It owns one separate immutable overlay:

```text
OwnershipEventOverlayRevision = SHA256Digest

OwnershipFunctionEventOverlay {
  owner: DefId,
  slots: SortedMap<MirEventKey, MirEventSlot>,
  deferredActivations:
      SortedMap<LoanKey, DeferredActivationFact>,
  unsafeOccurrences:
      SortedMap<UnsafeBoundaryKey, MirUnsafeOccurrence>,
  markerUses:
      SortedMap<OwnershipMarkerUseKey, OwnershipMarkerUse>,
  logicalDropPlans: SortedMap<MirEventKey, LogicalDropPlan>,
  castResourcePlans:
      SortedMap<CastCarrierKey, VerifiedCastResourcePlanFact>,
}

VerifiedOwnershipEventOverlay {
  semanticContext: SemanticContextBrand,
  contextFingerprint: ContextFingerprint,
  module: ModuleId,
  checkedFactsRevision: CheckedFactsRevision,
  builtRevision: MirRevisionId,
  functions: SortedMap<DefId, OwnershipFunctionEventOverlay>,
  revision: OwnershipEventOverlayRevision,
}

OwnershipEventOverlayInput {
  checked: const OwnershipAdmittedCheckedModule,
  hir: const RFC0010::VerifiedHirModule,
  built: const RFC0013::VerifiedBuiltMir,
  body: const RFC0005::BodyCheckingInput,
}

buildOwnershipEventOverlay(
  input: Borrowed<const OwnershipEventOverlayInput>,
) -> RFC0010::IrOperationResult<OwnershipEventOverlayCandidate>

verifyOwnershipEventOverlay(
  candidate: Borrowed<const OwnershipEventOverlayCandidate>,
  input: Borrowed<const OwnershipEventOverlayInput>,
) -> RFC0010::IrOperationResult<VerifiedOwnershipEventOverlay>
```

The checker/session invokes `buildOwnershipEventOverlay` after Built MIR
verification and before ownership analysis, while the admitted checked module
and its exact RFC 0005 `BodyCheckingInput` are still alive. Immediately before
the call, the private checker/session constructs one
`OwnershipEventOverlayInput` from those exact live capabilities. The producer
and verifier each call RFC 0015's sole
`MarkerProofInput::from(const BodyCheckingInput&)` constructor independently.
Each proof input is a non-owning pass-duration borrow and is destroyed before
the admitted checked wrapper or body input may be destroyed. No caller may
reconstruct it from revision digests or individual store, signature, role,
policy, coherence, or interner references.

The producer and verifier create distinct RFC 0015 proof inputs and query
contexts from the same body input. Each starts with an empty private active-key stack and an
empty private optional memo, performs its own complete ordered marker queries,
and destroys both after its pass. The verifier never reads a producer memo,
query result cache, traversal plan, or candidate decision to choose its query
set. The two passes may share only the immutable lineage-bearing input and RFC
0015's linearizable semantic-type interning authority; they compare canonical
query keys and completed results after independent computation. A hidden
singleton, global registry, session lookup, wrapper field, overlay field,
repository entry, serialized capability, or memo that outlives this call is
forbidden.

Before either pass reads a function body or performs a marker query, the
operation validates the pass-local RFC 0015 `MarkerProofInput` constructed from
`input.body`. Its semantic context, context fingerprint, semantic-type store
identity, policy revision, standard-marker authority, local-signature parent,
imported-signature view revision, and frozen-coherence revision must equal
`input.checked` and its checked-facts lineage exactly. An invalid canonical identity
selects RFC 0010 `IdentityInvariantRejected`. A valid identity with a foreign,
missing, stale, swapped, or post-teardown lineage selects
`IrInvariantRejected(InputRevisionMismatch, OwnershipProofValidation)`. An RFC
0015 query-level `InvariantRejected` retains the mapping below: identity
failures select `IdentityInvariantRejected`; checker failures select
`IrInvariantRejected(InvalidFact, OwnershipProofValidation)`. Every rejected
branch publishes no deferred-activation fact, marker use, logical-drop plan,
cast-resource plan, candidate overlay, or verified overlay.

The producer joins each checked `NodeId` occurrence to its verified HIR
operation in schema traversal order and then to exactly one derived
`MirEventKey`; neither `NodeId` nor HIR identity enters the result. Ordinary
slots are reconstructed from the closed Built MIR operation fields. Unsafe
multiplicity and source association come from the one-to-one checked-fact/HIR/
MIR join and are encoded only in this overlay. The same transaction derives
one complete deferred-activation projection for every eligible mutable receiver
borrow, one logical drop plan for every initialization event, and one cast
resource route plan for every `MirCheckedCast` while the frozen
checked module, semantic type store, canonical definition inventory, explicit
RFC 0015 marker-proof authority, and selected impl and witness facts are still
available. The producer and verifier each derive a distinct call-duration
`MarkerProofInput` from `input.body`, validate its lineage independently, use
it only for that pass, and destroy it before the pass returns.
Every RFC 0015 query required by the ownership handoff is recorded in the
same function overlay as one revision-bound `OwnershipMarkerUse`. This is
an RFC 0007-owned checker projection; RFC 0005 does not publish or encode a
logical drop plan.
The overlay verifier independently reconstructs all six inventories and
publishes no value for a missing, additional, reordered, duplicated, gapped,
source-incompatible, or resource-plan-incompatible association.

The deferred-activation map, logical drop plans, and cast resource routes are
the authoritative semantic handoff for receiver activation, logical drop, and
`Linear` component routing. Ownership analysis never reconstructs a
deinitializer, dyn payload, erased payload, union alternative, or marker fact
from a type name and never reopens checker dispatch or receiver-adjustment facts
after `OwnershipAdmittedCheckedModule` is destroyed. The
verified overlay retains only canonical identities, types, closed marker
decisions and their permitted proof records, MIR places, and exact route
evidence; it retains no AST or HIR identity.

The wrapper has a private verifier-owned constructor. Its module, semantic
context, checked-facts revision, and Built revision must equal the admitted
checked module and Built MIR exactly. It neither changes nor extends the MIR
preimage. Moving any overlay field into MIR requires one accepted replacement
of the canonical MIR framing, implementation, verifier, and exact oracles.

The implementation validates both the marker-proof borrow and the borrow-
evidence repository capability for liveness, identity, and lineage before
reading a function body, semantic type, marker proof, dispatch target, or
borrow summary. A foreign, missing, stale, swapped, or post-teardown capability
selects `InputRevisionMismatch` without dereferencing repository storage. No
global registry, brand-only lookup, hidden session singleton, lease-only
resolution, or stored marker-proof input is legal.

### Built MIR Ownership Vocabulary

Ownership analysis uses the one RFC 0010 CFG. Every function has a closed set
of blocks; every block terminates; all edge kinds and cleanup successors are
explicit. The ownership-relevant records are:

```text
MirPoint =
    Entry
  | BeforeStatement { block: MirBlockId, ordinal: uint32 }
  | AfterStatement { block: MirBlockId, ordinal: uint32 }
  | BeforeTerminator { block: MirBlockId }
  | Edge { from: MirBlockId, edgeOrdinal: uint32, to: MirBlockId }
  | Exit { block: MirBlockId, kind: MirExitKind }

MirExitKind =
    Return | ResidualReturn | Break | Continue | Panic | Unwind
  | Cancellation | Unreachable

MirLocation {
  owner: DefId,
  point: MirPoint,
}

MirEventKey {
  location: MirLocation,
  operandOrdinal: uint32,
}

OwnershipPoint =
    Cfg { point: MirPoint }
  | BeforeEvent { event: MirEventKey }
  | AfterEvent { event: MirEventKey }

OwnershipEventStage = Source | Effect | Commit

OwnershipEventRole =
    Operation
  | EntryRoot
  | OperandRead
  | OperandCopy
  | OperandMove
  | ConstantOperand
  | DestinationWrite
  | BorrowIssue
  | BorrowActivation
  | StorageLive
  | StorageDead
  | SetDiscriminant
  | Deinitialize
  | LogicalDrop
  | LinearConsume
  | Capture
  | Escape
  | VariantSwitch
  | PanicPayload
  | UnsafeOperation
  | UnsafeAcknowledgement
  | StaticAddress
  | CheckedCastCheck
  | CheckedCastSuccess
  | CheckedCastFailure
  | CastCarrierInitialize
  | CastCarrierTransfer
  | CastCarrierDrop

MirEventSlot {
  key: MirEventKey,
  stage: OwnershipEventStage,
  roles: SortedNonEmptySequence<OwnershipEventRole>,
}

CastCarrierKey {
  check: MirEventKey,
}

CastCarrierSourceMode = Copy | Move | Constant

CastCarrierFact {
  key: CastCarrierKey,
  carrierPlace: MovePathKey,
  source: MirEventKey,
  sourceMode: CastCarrierSourceMode,
  sourcePlace: Maybe<MovePathKey>,
  carrierType: SemanticTypeId,
  resultType: SemanticTypeId,
  successTransfer: MirEventKey,
  failureDrop: Maybe<MirEventKey>,
  dropObligations: SortedUniqueSequence<DropObligationKey>,
  linearObligations: SortedUniqueSequence<LinearObligationKey>,
}

OwnershipMarkerDecision =
    Positive { proof: RFC0005::MarkerFact }
  | ExplicitNegative { explicitFact: RFC0005::MarkerFact }
  | Unsatisfied

OwnershipMarkerUseKey {
  event: MirEventKey,
  marker: DefId,
  subject: SemanticTypeId,
  markerPolicyRevision: RFC0015::MarkerPolicyRegistryRevision,
  coherenceRevision: RFC0005::CoherenceViewRevision,
}

OwnershipMarkerUse {
  key: OwnershipMarkerUseKey,
  decision: OwnershipMarkerDecision,
}

LogicalDropAction =
    Declared { deinitializer: DefId }
  | Builtin { ownerType: SemanticTypeId }
  | Dynamic { existentialType: SemanticTypeId }

LogicalDropPlanComponent {
  place: MovePathKey,
  valueType: SemanticTypeId,
  dropAction: Maybe<LogicalDropAction>,
  copyDecision: OwnershipMarkerUseKey,
  linearDecision: OwnershipMarkerUseKey,
  declarationOrdinal: uint32,
}

LogicalDropPlan {
  root: MovePathKey,
  components:
      SortedMap<MovePathKey, LogicalDropPlanComponent>,
}

CastResourceRouteProof =
    Identity
  | UnionInject { alternative: SemanticTypeId }
  | DynErase {
      interface: RFC0005::InterfaceInstantiation,
      impl: ImplId,
      witnesses: WitnessArgumentsId,
    }
  | DynUpcast { path: NonEmptySequence<DefId> }
  | CheckedPayload { kind: RFC0005::CastKind }

CastResourceRoute {
  carrier: MovePathKey,
  result: MovePathKey,
  proof: CastResourceRouteProof,
}

VerifiedCastResourcePlanFact {
  key: CastCarrierKey,
  mode: RFC0005::CastMode,
  kind: RFC0005::CastKind,
  carrierType: SemanticTypeId,
  targetType: SemanticTypeId,
  resultType: SemanticTypeId,
  carrierPlan: MirEventKey,
  successPlan: MirEventKey,
  routes: SortedUniqueSequence<CastResourceRoute>,
}

CastCarrierPhase = Absent | Initialized | Transferred | Dropped

MirUnsafeScopeBoundaryKind = Enter | Exit

MirUnsafeScopeBoundary {
  kind: MirUnsafeScopeBoundaryKind,
  scope: MirSourceScopeId,
}

MirStatementKind =
    Assign = 0x01
  | StorageLive = 0x02
  | StorageDead = 0x03
  | BorrowCreation = 0x04
  | SetDiscriminant = 0x05
  | Deinitialize = 0x06
  | UnsafeScopeBoundary = 0x07
```

Point tags are `Entry = 0x01`, `BeforeStatement = 0x02`,
`AfterStatement = 0x03`, `BeforeTerminator = 0x04`, `Edge = 0x05`, and
`Exit = 0x06`. Exit tags are `0x01` through `0x08` in declaration order.
Block and statement identities are the deterministic RFC 0010 MIR identities.
`Cancellation` remains an upstream closed-union tag but is inadmissible under
the pre-checker Chapter 15 gate; no verified RFC 0007 input contains that exit.
An edge ordinal is its index in the terminator's closed successor field order.
`OwnershipPoint` tags are `Cfg = 0x01`, `BeforeEvent = 0x02`, and
`AfterEvent = 0x03`. `OwnershipEventStage` tags are `Source = 0x01`,
`Effect = 0x02`, and `Commit = 0x03`. `OwnershipEventRole` tags are `0x01`
through `0x1c` in declaration order. `CastCarrierSourceMode` tags are `Copy =
0x01`, `Move = 0x02`, and `Constant = 0x03`; `CastCarrierPhase` tags are
`Absent = 0x01`, `Initialized = 0x02`, `Transferred = 0x03`, and `Dropped =
0x04`. `OwnershipMarkerDecision` tags are `Positive = 0x01`,
`ExplicitNegative = 0x02`, and `Unsatisfied = 0x03`.
`LogicalDropAction` tags are `Declared = 0x01`, `Builtin = 0x02`, and
`Dynamic = 0x03`. `CastResourceRouteProof` tags are `Identity = 0x01`,
`UnionInject = 0x02`, `DynErase = 0x03`, `DynUpcast = 0x04`, and
`CheckedPayload = 0x05`. `MirUnsafeScopeBoundaryKind` tags are `Enter = 0x01`
and `Exit = 0x02`. Closed-union and record fields encode in declaration order.
Marker-use keys order by canonical `event`, expanded `marker`, expanded
`subject`, `markerPolicyRevision`, then `coherenceRevision`; the map key must
equal `OwnershipMarkerUse.key`. Carrier keys order by `check`, plan-component
maps order by `place`, routes order by `carrier`, `result`, then complete
route-proof bytes, and every fact/map key must agree.
`MirEventKey` is the
identity of one ownership-relevant
transfer within a location. `operandOrdinal` is the zero-based index in that
location's causally ordered `ownershipEvents` slot sequence. One slot may carry
multiple sorted roles, so a consuming move/capture/escape operand retains one
identity rather than three.

`CastCarrierFact.dropObligations` and `linearObligations` are the complete
obligations pending on the initialized carrier before either branch. On
success, every retained obligation records its carrier-to-result transfer in
the corresponding `DropObligationFact` or `LinearObligationFact`; on failure,
the same complete key sets are discharged or consumed by the carrier drop. A
key missing from either branch relation, an additional result obligation, or a
different obligation subject is `InvalidOwnershipProof`.

`logicalDropPlans` contains exactly one row for every event that initializes a
value generation, including entry roots, ordinary destination commits, cast
carrier initialization, and cast success commits. Its map key is the
initialization event; no read, move, drop, or uninitialized destination has a
row. `LogicalDropPlan.components` is the complete maximal resource-component
inventory for its root. A component references exactly one `Copy` decision and
one `Linear` decision in the same function overlay. Both keys use the plan's
initialization event and the component's exact `valueType`; their marker fields
are the checker-resolved canonical `Copy` and `Linear` definition identities,
and their lineage fields are the exact policy and coherence revisions used for
both queries.
A component is present exactly when its `Copy` decision is not `Positive` or
its `Linear` decision is `Positive`. A `Declared` action names the exact
checked deinitializer definition;
a `Builtin` action names the exact semantic type governed by one compiler-
defined logical drop; and a `Dynamic` action names the exact existential type
whose value carries its verified dynamic drop dispatch. A `Positive` `Copy`,
not-positive `Linear`, action-free representation does not enter the plan and
carries no ownership obligation. Every component place is the root or a typed
descendant of the plan root, and declaration ordinals are unique and
contiguous in logical drop order.

The RFC 0007 checker projection constructs that inventory with one exact two-
phase algorithm. It completes query discovery before emitting any plan
component, then folds the recorded tree in postorder. A `Positive` `Linear`
decision never stops query discovery.

Phase one begins at the plan root and constructs a private, non-serialized
projection tree. At each visited place, in canonical declaration order, it:

1. resolves the exact declaration, canonical `valueType`, and whether the
   place has one admitted `Declared`, compiler-owned `Builtin`, or verified
   `Dynamic` direct action;
2. queries the exact RFC 0015 `Copy` and `Linear` keys under the frozen
   definition inventory, policy registry, coherence view, initialization
   event, and subject, and inserts both completed decisions into `markerUses`;
3. stops descendant discovery only when the current place has a direct action
   or has no stored fields; and
4. otherwise visits every stored field with its exact substituted canonical
   type, including all fields below an action-free `Positive` `Linear` place.

No component is inserted during phase one. An `InvariantRejected` query aborts
the complete transaction under the failure mapping above, and no partial
`markerUses` map survives. An explicit negative or `Unsatisfied` decision is a
completed query, not an early traversal stop. Thus producer and verifier derive
the same query tree without reading a candidate plan, and every queried place
has both marker-use rows even when postorder folding later omits or subsumes
that place.

Phase two folds children before their parent. Child folds use canonical
declaration order for validation and reverse declaration order for logical
cleanup; `declarationOrdinal=0` names the first cleanup component and all
ordinals are contiguous. The fold at each place is exact:

1. A place with a direct action requires a not-positive `Copy` decision. It
   emits the current maximal component with that exact action and no descendant
   component, whether `Linear` is positive or not positive. A positive `Copy`
   decision with any direct action is `InvalidFact`.
2. At an action-free place with `Positive` `Linear` and positive `Copy`, every
   immediate stored field's already recorded `Copy` decision must be
   `Positive`. `ExplicitNegative` and `Unsatisfied` both fail this condition
   without being collapsed. When the condition holds, the fold emits exactly
   the current component with no action and suppresses every descendant
   component. Descendant `Linear` results remain recorded in `markerUses` but
   do not manufacture separate obligations beneath this maximal `Copy +
   Linear` generation.
3. At an action-free place with `Positive` `Linear` and not-positive `Copy`,
   the fold emits exactly the current component. It selects
   `Builtin(ownerType)` when the already folded descendant component sequence
   is non-empty, providing structural reverse-field cleanup; it selects no
   action when that sequence is empty. The descendant components are subsumed
   in both cases.
4. At an action-free place with not-positive `Linear`, a non-empty descendant
   component sequence is retained and the current place is omitted. If that
   sequence is empty, a not-positive `Copy` decision emits one action-free
   current component, while positive `Copy` emits nothing.

The final map is the sorted, non-overlapping maximal component set. Positive
and not-positive descendant decisions affect cleanup selection only through
the completed child folds; no parent may speculate about a child query or stop
before recording it. A duplicate deinitializer, unresolved declaration,
missing or additional descendant marker use, wrong query order, positive
`Copy + Linear` parent with a not-positive immediate field, incorrect
structural-cleanup selection, dynamic value without exact dispatch evidence,
component that is both an ancestor and descendant of another component, or any
different postorder result is `InvalidFact` before ownership analysis.

`VerifiedCastResourcePlanFact` is computed and independently verified while
the admitted checked module is alive, then encoded in the event overlay. Its
key is the cast check event; its types, mode, kind, impl, witnesses, dyn path,
marker decisions, deinitializer identities, and result alternative must be the
one-to-one projection of the frozen checker/HIR/Built association. The carrier
plan key names the cast source event that initializes the temporary. The
success plan key names the success commit and is rooted at the target
destination, or at the initialized target alternative of the canonical
optional result. Both keys must resolve in `logicalDropPlans`; `routes` is a
complete bijection between those two nontrivial component maps. No ownership-
analysis phase may derive or amend either semantic plan after publication.

Route proof is exact. `Identity` requires byte-identical actions and complete
decision alternatives and payloads after excluding their event-bearing use
keys; both component records are still independently verified. `UnionInject`
requires the checked union alternative and preserves the payload action.
`DynErase` requires the exact selected impl and witness proof
that the result's dynamic drop dispatch invokes the source component's action.
`DynUpcast` requires the exact checked inheritance path and preserves the
embedded payload and dynamic drop entry. `CheckedPayload` is legal only for
`AnyDowncastChecked` or `ErrorUnionExtractChecked`; the successful runtime
check transfers the selected payload out of the wrapper and changes only to
the result component's independently checked drop action. The failure branch
retains and executes the carrier plan. Every route preserves the
positive-versus-not-positive result for `Copy` and `Linear`; a non-identity
route may reference only the exact independently checked decision records for
the result component, so its `DropRequirement` and linear-obligation presence
do not change. `ExplicitNegative` and `Unsatisfied` remain distinct even when
both are not positive. A source-only or result-only nontrivial
component, a non-bijective route, a proof incompatible with the cast family,
or a marker/deinitializer mismatch is `InvalidFact` before ownership analysis.

The slot sequence is generated independently from the Built MIR schema. Every
ordinary statement or terminator projects all `Source` slots first in language
evaluation order, exactly one `Effect` slot next, and every `Commit` slot last.
The effect slot carries `Operation` plus every applicable operation-level role;
its ordinal therefore follows the operation's complete source inventory rather
than occupying a fixed ordinal. A destination place has no write effect before
its commit slot. The closed projection is:

| Built MIR operation | `Source` slots | `Effect` slot roles | `Commit` slots |
|---|---|---|---|
| Assignment or overwrite | The `Rvalue::Use` operand | `Operation` | Destination with `DestinationWrite` |
| `StorageLive` | None | `Operation`, `StorageLive`; payload is the affected local | None |
| `StorageDead` | None | `Operation`, `StorageDead`; payload is the affected local | None |
| `MirStatement::BorrowCreation` | Borrowed source place with `OperandRead` | `Operation`, `BorrowIssue` | Reference destination with `DestinationWrite` |
| `SetDiscriminant` | None | `Operation` | Destination with `DestinationWrite`, `SetDiscriminant` |
| Deinitialize | None | `Operation`, `Deinitialize`; payload is the affected place | None |
| Logical drop | Affected place with `OperandRead` and `LinearConsume` when applicable | `Operation`, `LogicalDrop` | None |
| Drop-and-replace | Replacement operand | `Operation`, `LogicalDrop` | Destination with `DestinationWrite` |
| Call | Receiver when present, then arguments by index | `Operation`, plus `BorrowActivation` for the exact deferred receiver loan | Destination with `DestinationWrite` when present |
| Return | Return operand when present | `Operation` | None |
| Logical variant switch | Discriminant operand | `Operation`, `VariantSwitch` | None |
| Logical panic | Borrowed payload operand with `PanicPayload` | `Operation` | None |
| `MirCheckedCast` | The cast input operand, plus `CastCarrierInitialize` | `Operation`, `CheckedCastCheck`, plus `UnsafeOperation` when at least one unsafe occurrence is attached | Guaranteed: result destination with `DestinationWrite`, `CheckedCastSuccess`, `CastCarrierTransfer`; optional or forced: none at the common location |
| Closure construction | Captures in checked-capture order | `Operation` | Closure destination with `DestinationWrite` |
| `UnsafeScopeBoundary(Enter)` | None | `Operation`, `UnsafeAcknowledgement`; payload is the affected source scope | None |
| `UnsafeScopeBoundary(Exit)` | None | `Operation`; payload is the affected source scope | None |
| Goto or unreachable | None | `Operation` plus any applicable unsafe role | None |

`UnsafeScopeBoundary = 0x07` is the one direct-replacement Built MIR statement
variant. There are no separate outer enter and exit variants. Its canonical
statement bytes are exactly outer tag `0x07`, the one-byte
`MirUnsafeScopeBoundaryKind`, then `uint32be(scope.ordinal)`. The scope ordinal
must be nonzero and name a source scope owned by the enclosing function. These
bytes occupy the ordinary statement position in the canonical function record
and therefore participate in the canonical `zom.mir-revision`
`MirRevisionId`; the RFC 0007 event overlay never copies or substitutes them.
The complete MIR statement tag set is `0x01` through `0x07` as declared
above. The RFC 0007 enablement transaction makes that closed set authoritative
before any unsafe-scope statement is admitted to Built MIR. Any statement
variant, field, or tag change directly replaces the canonical MIR framing and
all exact oracles in the same accepted change. No alternate MIR domain or
decoder is permitted.

The two boundary kinds are not unsafe occurrences and carry no unsafe ordinal,
operation kind, requirement, checked `NodeId`, or source association. Every
admitted unsafe source scope has one enter statement that dominates every
operation in that scope and a sorted non-empty set of exit statements. Along
every path from an enclosed operation to a function exit or a point outside
the scope, exactly one matching exit occurs; the exit set collectively
postdominates the operation even when no individual exit does. Boundary events
are properly nested on every CFG path, and an exit closes only the innermost
open scope. The enter effect is the acknowledgement `MirEventKey`; exit effects
have ordinary operation keys. A missing, additional, crossed, foreign-scope,
non-dominating, or path-incomplete boundary is `InvalidControlFlow` before
ownership analysis.

The Built MIR verifier independently decodes the outer tag and payload before
event projection. It rejects an unknown kind, zero or foreign scope, an enter
outside its own source scope, a duplicate enter, an exit with no matching open
scope, a crossed nesting stack, an enter that fails to dominate an enclosed
operation, or an exit set that fails the exact path-cut rule. Re-encoding the
verified statement must reproduce the same six bytes. A candidate that uses
outer tags `0x08` or `0x09` as separate enter/exit forms, or that omits the
inner kind byte, is `CanonicalCodecMismatch`.

The MIR unsafe-scope oracle uses RFC 0013's zero fingerprint, module `a1`,
checked-facts bytes `22`, dispatch bytes `33`, borrow-evidence bytes `44`, and
one complete 113-byte component-test canonical function record. The record uses
expanded owner bytes `b1`, function kind `0x01`, source-definition kind
`Function = 0x02`, result-type bytes `d1`, source-key bytes `c1` with span
`[0, 0)`, one root source scope, no locals, and one block in that scope. The
block contains exactly `UnsafeScopeBoundary(Enter, scope=1)` then
`UnsafeScopeBoundary(Exit, scope=1)`, followed by `Return(None)`. Statements are
elements of the canonical statement sequence and are not individually framed.
The function record is:

```text
0000000000000001b101020000000000000001d1c10000000000000000000000000000000000000000000000010000000100c10000000000000000000000000000000000000000000000000000000000000001000000010000000100000000000000020701000000010702000000010100
```

Its complete 283-byte MIR preimage is:

```text
7a6f6d2e6d69722d7265766973696f6e0000000000000000000000000000000000000000000000000000000000000000000000000000000001a1222222222222222222222222222222222222222222222222222222222222222233333333333333333333333333333333333333333333333333333333333333334444444444444444444444444444444444444444444444444444444444444444000000000000000100000000000000710000000000000001b101020000000000000001d1c10000000000000000000000000000000000000000000000010000000100c10000000000000000000000000000000000000000000000000000000000000001000000010000000100000000000000020701000000010702000000010100
```

Its SHA-256 is
`c49976b9fc841ecf6cd2e2d62af3442d36a22571b52291a0601e60ea92f71aa0`.
The component record uses deliberately minimal expanded identity bytes; a
semantic fixture replaces them with registry-valid expanded keys and proves
dominance, nesting, and exit-cut validation. Independent encoders must
reproduce the complete function record, revision bytes, and revision hash.
Mutations change the outer tag, inner kind, scope ordinal, statement order,
statement count, function frame length, source-scope association, terminator,
and one payload byte independently and require revision inequality or verifier
rejection.

Every operand contributes one source slot, including a constant. A place
operand adds exactly one of `OperandCopy` or `OperandMove` and also
`OperandRead`; a borrow source and a non-consuming logical place use add only
`OperandRead`. A constant adds `ConstantOperand`, never `OperandRead`,
`OperandCopy`, or `OperandMove`, and changes no initialization, loan, linear, or
raw-provenance state. A constant that denotes a verified static reference also
adds `StaticAddress`; its source event is the `ReferenceRoot.introduction`, and
the later destination commit installs the `LocalValue` definition.

A logical-drop source role is a conditional ownership inspection, not an
ordinary value read. `Closed` mode requires `Initialized`; `Open` mode follows
the three-way transfer defined by the initialization lattice and does not read
an uninitialized alternative. All other `OperandRead` events retain the exact
`Initialized` precondition.

Capture, escape, linear-consume, panic-payload, static-address, and unsafe roles
attach to the same source slot when applicable. Destination and discriminant
roles attach only to the commit slot. Repeated fields retain sequence order;
absent optional fields contribute no slot. A place occurrence is one slot and
its projection chain is not recursively assigned independent ordinals. A
generated slot with identical roles and payload still has its own ordinal. No
ownership fact may identify an operation by bare `MirLocation`, by an assumed
operation ordinal, or by a fact-specific ordinal.

Every stage and every role other than the set-valued `UnsafeOperation` role is
a pure projection of Built MIR bytes. `UnsafeOperation` is present exactly
when that event's overlay-owned unsafe-occurrence subsequence is non-empty; the
role and subsequence are committed only to `OwnershipEventOverlayRevision`.
They do not enter or alter `MirRevisionId`.

`MirCheckedCast` branch projection is total. The source event creates one
cast-owned logical carrier before the check. A guaranteed cast commits its
target value at the common location and transfers that carrier. An optional
cast has one success edge whose edge location projects
`Effect(Operation, CheckedCastSuccess)` and then a result commit with
`CastCarrierTransfer`; its failure edge projects `Effect(Operation,
CheckedCastFailure, LogicalDrop, CastCarrierDrop)` plus `LinearConsume` when
applicable, and only then commits the canonical null result. A forced cast has
the same success edge and commit; its failure edge performs the same carrier
drop and then reaches the explicit logical `ForcedCast` panic without a result
commit. Under the abort-only contract below, each carrier-drop component
action returns normally and continues the drop or enters the terminal RFC 0006
abort path. There is no unwind or cleanup successor. Only completion of every
component reaches `Dropped`, the optional null commit or forced-cast panic, and
the carrier `StorageDead`. Edge ordinals follow the closed success-then-failure
field order. A missing, additional, reordered, duplicated, unwind-bearing, or
mode-incompatible edge, drop, or commit is `InvalidControlFlow` before source
analysis.

The source event performs its declared read, copy, move, or constant
materialization exactly once and atomically initializes the carrier. A move
transfers the source value, all drop obligations, every reference definition,
every raw carrier, and all `Linear` obligations into the cast carrier; the
source becomes uninitialized at `AfterEvent(source)`. A copy preserves the
source and creates an independent carrier value under the ordinary copy rules;
a not-positive `Copy` decision is a source failure and publishes no carrier
fact, while `Copy + Linear` introduces a distinct carrier obligation. A
constant carrier has no source place. The operand must be completely
initialized: a partially initialized aggregate cannot initialize a carrier
and is rejected by the ordinary source-read rule. There is therefore no
partially initialized carrier state.

The success resource transformation is fixed by the exact
`VerifiedCastResourcePlanFact` encoded in the verified event overlay. The
checker-side plan derivation consumes `CastKind`, the checked source, target,
and result types, the selected impl and witnesses, the exact active result
alternative, the canonical definition inventory, the immutable semantic type
store, and RFC 0015 marker decisions. It does not infer a type,
marker, deinitializer, or resource route from source spelling. The checker and
overlay verifier must prove this closed predicate before publishing the cast:

1. `carrierPlan` is the complete maximal component plan for `carrierType`; its
   component maps equal the pending drop and `Linear` obligations at the
   carrier immediately before the branch.
2. `successPlan` is independently derived from the exact initialized result
   shape: `target` for guaranteed and forced success, and the initialized
   `target` alternative of canonical `target | null` for optional success.
3. Every carrier component has exactly one result component and every result
   component has exactly one carrier component. The route records the ordinary
   `DropTransfer` and, when applicable, `LinearTransfer` at
   `successTransfer`. It preserves the same `DropResourceSubject`, drop-
   obligation key, and linear-obligation key while its proof authorizes the
   exact optional source-action to optional result-action transition.
4. Any representation byte or projection absent from `carrierPlan` has a
   `Positive` `Copy` decision, a not-positive `Linear` decision, and no logical
   drop. It
   owns no resource subject and disappears with the carrier storage without a
   drop or deinitialization event. A source-only or result-only nontrivial
   component makes the cast invalid.
5. A success transfer neither consumes, duplicates, nor creates a drop or
   `Linear` obligation. A failed optional or forced cast executes the unchanged
   `carrierPlan`; it does not consult `successPlan` or change the carrier's
   drop action before failure cleanup.

The predicate applies by cast family. Integer, float, admitted reference, and
raw-pointer-preserving casts require empty drop and `Linear` plans.
`UnionInject`, `DynErase`,
and `DynUpcast` route only their checked payload component. `AnyDowncastChecked`
and `ErrorUnionExtractChecked` route only the runtime-proven target component;
a non-`Copy` erased or residual wrapper is admissible only when its checked
`CheckedPayload` route transfers every resource component and leaves no
unmatched nontrivial wrapper component. If any family cannot prove the exact
relation, the body checker
rejects the cast with RFC 0005 `ZOM4013 CheckerInvalidCast` and publishes no
`CheckedCastFact`. A forged checked fact or Built cast that violates the
predicate is `InvalidFact` before ownership source analysis.

A cast whose checked source semantic type is a raw pointer and whose target or
initialized result payload is a safe reference is never admitted, including
inside `unsafe`. RFC 0005 rejects it with `ZOM4013 CheckerInvalidCast` and
publishes no `CheckedCastFact`, HIR, Built MIR cast, event-overlay row, region,
or escape proof. A forged checked fact, HIR operation, Built cast, or overlay
route selecting raw-to-reference is `InvalidFact` before ownership analysis.
An unsafe acknowledgement authorizes only the recorded raw operation; it does
not prove pointer validity, alignment, initialization, referent lifetime, or a
reference region and therefore cannot create safe-reference authority. This
fail-closed rule is total until a separate accepted RFC defines that authority
and its escape law.

The carrier is not hidden state. HIR-to-MIR lowering allocates one cast-owned
temporary local of `carrierType` in the ordinary MIR local-declaration
inventory, emits `StorageLive` before the common source event, and emits
`StorageDead` on every branch after transfer or drop. `carrierPlace` is the
complete root move path of that local. The RFC 0007 overlay binds that already
encoded local to `CastCarrierKey`; no MIR field is added. The verifier
requires one such local, exactly one live interval, no user-visible source
name, no independent read or write, and exact dominance/postdominance by the
cast events. A missing, shared, aliased, type-mismatched, or externally used
carrier local is `InvalidOwnershipProof`.

On success, the commit atomically transfers the complete carrier state to the
logical result generation and changes the carrier from `Initialized` to
`Transferred`; it never copies a moved or `Linear` payload. The carrier's
complete drop and `Linear` relations must follow the authoritative overlay
routes and preserve each runtime resource subject. Scalar, dyn, union, and
error-union casts also
map every initialized reference-typed or raw-pointer descendant selected by the
verified result shape. Reference-to-raw creates one raw carrier whose origins
are the complete source-reference origins, and raw-to-raw preserves the complete
reaching carrier set. No admitted transition consumes a raw carrier to create a
safe reference. Acknowledgement never synthesizes an origin, loan, or region. At
`AfterEvent(successTransfer)` the result generation is `Initialized`, the
carrier move path is `Uninitialized`, every paired obligation is pending at
its result projection, and no carrier obligation or reference definition
remains.

On optional or forced failure, the failure effect changes the carrier from
`Initialized` to `Dropped`. It executes the exact initialized-descendant
logical-drop plan in reverse declaration order, discharges the carrier drop
obligations, and consumes every linked `Linear` obligation exactly once. An
uninitialized, transferred, already dropped, missing, or partially initialized
carrier at that effect is `InvalidOwnershipProof`. Guaranteed casts encode
`failureDrop = None`; optional and forced casts encode the exact failure effect
as `Some`. Every cast encodes exactly one success transfer. At every successor
of the cast before its `StorageDead`, the carrier is `Transferred` or
`Dropped`, never `Initialized`; `StorageDead` then accepts its uninitialized
storage under the closed rule below.
The independent ownership verifier recomputes the carrier fact, every phase at
every cutpoint, branch-specific drop/transfer, partial-initialization
precondition, exact plan membership, route bijection, resource-subject
preservation, obligation-key preservation, and one-to-one linear transition.
It compares those transfers with the already verified overlay plan and never
reopens checker signatures or invents a deinitializer after the admitted
checked module has been destroyed.

`Entry` has no effect slot. It projects receiver, parameter, capture, return,
and ordinary-local roots in canonical `MirLocalId` order as `Commit` slots with
`EntryRoot`; initially live receiver, parameter, and capture roots also carry
`DestinationWrite`, while dead return and ordinary-local roots do not. An
ordinary `Edge` or `Exit` control-only location projects one `Effect` slot at
ordinal zero with `Operation` and any applicable exit or unsafe role. A checked-
cast edge additionally projects the exact branch role and branch-specific
commit described above.

`MirStatement::BorrowCreation` is the sole Built MIR borrow-issuance form. The
Built MIR verifier rejects every other attempted issuance shape as
`InvalidFact` before RFC 0007 analysis. No ownership projection, compatibility
path, or duplicate loan identity exists.

`SetDiscriminant` verifies that the selected variant belongs to the
destination's logical type, that the destination is writable, and that no
initialized payload descendant of the previous variant remains. Required
logical drops or deinitializations therefore precede it as explicit MIR
operations. Its commit kills every obsolete `Downcast` descendant, establishes
the selected logical variant, and does not initialize selected payload fields
or synthesize a drop. A logical variant switch reads its discriminant before
the effect and refines only its declared successor edges; it performs no hidden
initialization, move, or tag-layout mutation.

A logical panic's borrowed payload is a non-consuming source use. Any safe loan
that authorizes the payload is issued by a dominating canonical
`MirStatement::BorrowCreation`; the panic terminator does not issue a hidden loan, store the
payload, or extend its region. Its panic and optional unwind successors remain
the explicit RFC 0010 edges inspected by this analysis.

Event-key and slot fields encode in declaration order; keys order by location
then operand ordinal, stages by tag, and roles by tag. The RFC 0007 overlay
verifier regenerates the complete slot sequence, stage, and role set, rejects a
missing, additional, reordered, or kind-incompatible slot, and commits the
derived view to `OwnershipEventOverlayRevision`. Slot identity, stage, and all
roles except `UnsafeOperation` must be a pure projection of fields already
encoded by `zom.mir-revision`; unsafe occurrence multiplicity and the
set-valued role are the separately encoded checked-fact association defined by
this RFC. Neither category changes `MirRevisionId`. If any required ordinary
event, stage, or non-unsafe role cannot be derived from MIR bytes, Built MIR
is incomplete and overlay construction fails. The canonical preimage is never
silently extended.

Every place is typed at each projection:

```text
MirPlace {
  local: MirLocalId,
  rootType: SemanticTypeId,
  projections: Sequence<TypedMirProjection>,
  resultType: SemanticTypeId,
}

TypedMirProjection =
    Field { field: DefId, input: SemanticTypeId, result: SemanticTypeId }
  | Index { index: MirLocalId, input: SemanticTypeId,
            result: SemanticTypeId }
  | Dereference { input: SemanticTypeId, result: SemanticTypeId }
  | Downcast { variant: DefId, input: SemanticTypeId,
               result: SemanticTypeId }
  | Subslice { first: uint32, pastLast: uint32,
               input: SemanticTypeId, result: SemanticTypeId }
```

Projection tags are `0x01` through `0x05`. Every semantic type expands through
the immutable RFC 0005 type store when encoded or compared. A projection chain
must be type-correct and must preserve its exact local and definition
identities. A dynamic index never proves disjointness merely because another
index uses a different local.

The following events must be explicit in Built MIR and participate in
`MirRevisionId`:

- storage live and storage dead;
- copy, move, assignment, overwrite, deinitialization, logical drop, and
  drop-and-replace;
- shared and mutable borrow creation plus deferred activation when applicable;
- calls with exact receiver and argument operand modes and RFC 0013 direct
  borrow summary selection;
- return and every store of a reference-derived value;
- logical variant switches, discriminant updates, and logical panic payload
  uses;
- every `MirCheckedCast` input, check, success/failure edge, result commit,
  checked unsafe requirement, and source span;
- closure construction and its complete checked capture records;
- every `MirUnsafeScopeBoundary` entry and exit, including its source-scope
  payload; and
- every checked unsafe operation's ordinary MIR operation fields.

Unsafe-occurrence ordinal, checked-fact source association, acknowledgement
association, and occurrence source span exist only in the RFC 0007 overlay and
do not participate in `MirRevisionId`. The Built revision commits only the
ordinary operation, cast requirement/span, and unsafe-scope boundary fields
from which the overlay association is verified.

Each event, including a synthetic `Entry` event, has one validated `SourceSpan`
in the MIR source map. A synthetic event uses the declaration span of its root.
The source map is presentation data addressed by `MirEventKey`; a span is never
an ownership identity.

Every operation executes transactionally in five exact phases:

1. **Address.** Resolve and type-check every place and projection in language
   evaluation order. Dynamic index locals and dereference bases must be readable
   in the operation input state. This phase computes addresses only and cannot
   copy, move, issue a loan, activate a loan, drop, deinitialize, or write.
2. **Preflight.** Check the complete source inventory, destination mode,
   initialization/drop obligation, place conflicts, loan permissions, marker
   evidence, and escape requirements against the same input state. A place's
   address/preflight check is anchored to that place's source, effect, or commit
   event key but does not execute that slot's state transfer.
3. **Source.** Execute source slots in ascending ordinal order. Each slot reads
   the current transient state, so a prior source move is visible to every later
   source in the same operation. Copy and read preserve the source; move and
   linear consumption kill it after that source event.
4. **Effect.** Execute the one operation-level effect after every source. A
   `BorrowIssue` creates an immediate active loan or deferred reserved loan only
   here. A `BorrowActivation` requires the named deferred loan to be reserved
   before this event and makes it active only after this event.
5. **Commit.** Execute destination commits in ascending ordinal order. The old
   destination generation remains visible before its commit; overwrite kills it
   and installs the new generation only after the commit. No RHS, borrow source,
   replacement, capture, or checked-cast input may observe a
   destination write from the same operation.

Preflight cannot authorize a source use after an earlier source slot has moved
the same place; the source phase rechecks readability against its transient
input. If address, preflight, source, effect, or commit fails, the operation
delta is not committed: no source move, loan issue, activation, drop, or write is
published. Complete-diagnostic exploration may continue from the operation
input snapshot, but that snapshot cannot produce verified successor facts.

`BeforeEvent(e)` is the state immediately before slot `e`, and
`AfterEvent(e)` is the state immediately after its successful transfer. The
first slot consumes the location input; `AfterEvent` of one slot equals
`BeforeEvent` of the next; and the last slot produces the location output.
Statement locations run from `Cfg(BeforeStatement)` to
`Cfg(AfterStatement)`. A terminator runs from `Cfg(BeforeTerminator)` through
its common slots and then through the selected branch's `Edge` or `Exit` effect
slot to `Cfg(Edge)` or `Cfg(Exit)`. Entry commit slots produce `Cfg(Entry)`;
when no entry slot exists, that point contains the empty-root initial state.
Locations without a state-changing role still publish both event cutpoints.

The producer publishes `OwnershipPointState` at every reachable `Cfg`,
`BeforeEvent`, and `AfterEvent` point, and the independent verifier recomputes
all three. Inclusion and kill boundaries are exact:

- a source value and every authorizing loan or region must be live at
  `BeforeEvent(source)`; a read or copy remains available at
  `AfterEvent(source)`, while a move or linear consumption is absent there;
- a loan is absent at `BeforeEvent(BorrowIssue)` and starts at
  `AfterEvent(BorrowIssue)`; its destination reference definition remains
  absent until `AfterEvent` of the later destination commit;
- a deferred mutable loan is `Reserved` at
  `BeforeEvent(BorrowActivation)` and `Active` at
  `AfterEvent(BorrowActivation)`; an immediate loan is active from
  `AfterEvent(BorrowIssue)`;
- a destination's old initialization and reaching definitions are present at
  `BeforeEvent(commit)`, and the committed generation is present at
  `AfterEvent(commit)`;
- a physical storage generation starts at `AfterEvent(StorageLive)`, contains
  `BeforeEvent(StorageDead)`, and excludes `AfterEvent(StorageDead)`;
- deinitialize and logical drop require the affected generation before their
  effect and kill it after the effect; `SetDiscriminant` kills obsolete variant
  descendants only after its commit; and
- an NLL value region contains `BeforeEvent` of every use. It also contains
  `AfterEvent` of a last non-consuming use, but excludes `AfterEvent` of a move,
  overwrite, deinitialize, drop, or storage-death kill. Static regions contain
  every reachable `OwnershipPoint`.

The equality relations between adjacent cutpoints are semantic transfer rules,
not codec deduplication: every reachable cutpoint remains a distinct structural
key in the proof.

### Place Conflict And Move Paths

A move path is a canonical place prefix:

```text
MovePathKey {
  owner: DefId,
  place: MirPlace,
}

MovePathFact {
  key: MovePathKey,
  parent: Maybe<MovePathKey>,
}

MovePathPair {
  first: MovePathKey,
  second: MovePathKey,
}
```

The path inventory contains every local root and every prefix of a place read,
written, copied, moved, borrowed, dropped, returned, captured, or passed to a
call. It is the sorted unique closure of those prefixes. `parent` removes the
last projection. There is no numeric allocation slot.

Move-path and pair fields encode in declaration order. Keys order by expanded
owner, local, root type, projection sequence, and result type. Pairs order by
`first` then `second`; both keys must share the function owner and `first` must
be canonically lower than `second`.

`Conflicts(a, b)` is symmetric and exact:

1. different local roots do not conflict;
2. identical paths conflict;
3. a path conflicts with every prefix and descendant;
4. two different structure or tuple fields under the same verified aggregate
   prefix do not conflict;
5. two subslices under the same prefix do not conflict only when their closed-
   open constant ranges are proven disjoint;
6. dynamic indices conflict with every index or subslice under the same prefix;
7. different downcasts conflict conservatively;
8. dereference projections conflict unless their complete prefix is identical;
   separate runtime addresses or raw pointers never prove disjointness; and
9. any malformed type/projection relationship is `InvalidPlace`, not a
   conservative source conflict.

`Conflicts(a, a)` is reflexive and implicit; no self-pair encodes.
`MovePathPair` stores only a conflict between two distinct paths, once, with
the lower canonical key first. Consumers answer an identical-key query as
`true` before consulting the encoded distinct unordered-pair set. The
independent verifier recomputes exactly every distinct pair, rejects a self-
pair or reversed pair, and compares the complete set. For `M` move paths the
encoded pair inventory is therefore bounded by `M * (M - 1) / 2`; the `M`
reflexive facts consume no record slots.

Moving a path marks the path and every descendant uninitialized. Every ancestor
is recomputed from its children. Moving a complete root invalidates all
projections. Reinitializing a complete path marks that path and every known
descendant initialized. Reinitializing one projection recomputes its ancestors
without changing proven-disjoint siblings.

### Initialization Lattice And CFG Dataflow

Initialization uses three may-bits for every reachable
`(OwnershipPoint, MovePathKey)` pair:

```text
InitializationState =
    Dead
  | Uninitialized
  | Initialized
  | MaybeInitialized
  | MaybeDeadUninitialized
  | MaybeDeadInitialized
  | MaybeDeadMaybeInitialized

MoveCause {
  event: MirEventKey,
  moved: MovePathKey,
}

DeinitializationKind = Explicit | LogicalDrop

InitializationLossCause =
    NeverInitialized { event: MirEventKey, path: MovePathKey }
  | Moved { cause: MoveCause }
  | Deinitialized {
      event: MirEventKey,
      path: MovePathKey,
      kind: DeinitializationKind,
    }
  | StorageEnded { event: MirEventKey, path: MovePathKey }

InitializationPointFact {
  path: MovePathKey,
  state: InitializationState,
  lossCauses: SortedUniqueSequence<InitializationLossCause>,
}

DropRequirement = Logical | Linear | LinearLogical

DropMode = Closed | Open

DropObligationKey {
  introduction: MirEventKey,
  place: MovePathKey,
}

DropResourceSubject {
  introduction: MirEventKey,
  origin: MovePathKey,
  originType: SemanticTypeId,
}

DropComponent {
  place: MovePathKey,
  subject: DropResourceSubject,
  valueType: SemanticTypeId,
  action: Maybe<LogicalDropAction>,
  declarationOrdinal: uint32,
}

DropTransfer {
  from: MovePathKey,
  to: MovePathKey,
  event: MirEventKey,
}

DropDischargeKind =
    LogicalDrop
  | OverwriteDrop
  | ReturnTransfer
  | ConsumingCallTransfer
  | CastFailureDrop

DropDischarge {
  event: MirEventKey,
  place: MovePathKey,
  kind: DropDischargeKind,
  mode: DropMode,
  components: Sequence<DropComponent>,
}

DropObligationFact {
  key: DropObligationKey,
  subject: DropResourceSubject,
  requirement: DropRequirement,
  components: SortedUniqueSequence<DropComponent>,
  transfers: SortedUniqueSequence<DropTransfer>,
  discharges: SortedUniqueSequence<DropDischarge>,
}

DropObligationState =
    Absent
  | Pending { place: MovePathKey }
  | Discharged { first: DropDischarge }
```

State tags are `0x01` through `0x07` in declaration order. Their exact
`{mayBeDead, mayBeUninitialized, mayBeInitialized}` bit triples are
`{1,0,0}`, `{0,1,0}`, `{0,0,1}`, `{0,1,1}`, `{1,1,0}`, `{1,0,1}`, and
`{1,1,1}` respectively. The all-zero triple does not encode. Joining reachable
predecessors is bitwise union; an unreachable predecessor contributes no
triple. In particular, joining `Dead` with `Initialized` produces
`MaybeDeadInitialized`, never `Initialized`. This preserves storage-lifetime
uncertainty across diamonds and loop backedges.

`DeinitializationKind` tags are `Explicit = 0x01` and `LogicalDrop = 0x02`.
`InitializationLossCause` tags are `NeverInitialized = 0x01`, `Moved = 0x02`,
`Deinitialized = 0x03`, and `StorageEnded = 0x04`. Variant and record fields
encode in declaration order. `DropRequirement`, `DropMode`, and
`DropDischargeKind` tags are `0x01` onward in declaration order. `MoveCause`
orders by canonical `event` then `moved`; loss causes order by complete
canonical bytes. Drop-obligation keys order by introduction then place;
resource subjects encode and order by introduction, origin, then expanded
origin type; components order by place, complete subject bytes, expanded value
type, action bytes, then declaration ordinal;
transfers order by event, from, then to; discharges order by event, place, kind,
mode, then complete component sequence. A discharge's `components` sequence is
execution order and is not sorted.
`DropObligationState` tags are `Absent = 0x01`, `Pending = 0x02`, and
`Discharged = 0x03`. Every drop obligation appears exactly once in the
`dropStates` map of every complete `OwnershipResourceStateAlternative`, so drop, linear,
and cast-carrier state are never independently joined or Cartesian-expanded.

`DropResourceSubject` is the canonical identity of one ownership-resource
generation, including an action-free affine or `Linear` resource. At introduction,
`subject.introduction == key.introduction`,
`subject.origin == key.place`, and `originType` equals the verified plan
component type installed at that place. Copying creates a new subject at the
copy destination. Ordinary moves, cast-carrier initialization, and successful
cast routes preserve the complete subject byte-for-byte even when the current
`DropComponent.valueType` and `action` change under a verified
`CastResourceRoute`. A discharge must use the optional action in the complete
plan for the current place and type. `Some(action)` invokes that exact logical
drop action; `None` invokes no deinitializer and is legal for an affine
resource with a not-positive `Copy` decision, a `Positive` `Linear` resource,
or both. Every
`Positive` `Linear` discharge records the linked `LinearConsume`. No join,
cast, overwrite, or decoder may synthesize or merge subjects.

At function entry, each dead return or ordinary-local path receives one
`NeverInitialized` cause at its synthetic `Entry` event. `StorageLive` replaces
it with that storage-live event. A successful source move replaces the affected
paths' causes with `Moved`; explicit deinitialization and logical drop replace
them with `Deinitialized`; `StorageDead` replaces them with `StorageEnded`.
Transfer through an unaffected operation preserves the complete set; a join
unions it; successful initialization clears it. A cause is retained exactly
when a reachable predecessor contributes a dead or uninitialized bit from that
cause. An `Initialized` fact has an empty cause set. Every other encoded state
has a non-empty set. The verifier recomputes the complete relation; a missing,
additional, unreachable, duplicate, or out-of-order cause is
`InvalidOwnershipProof`.

At function entry, value parameters and initialized capture locals are
`Initialized`; return places and ordinary locals are `Dead` until
`StorageLive`. `StorageLive` requires exactly `Dead` on all reaching paths and
produces `Uninitialized`. An `Initialize` event requires a definitely live
state with no initialized bit. `Overwrite` requires a definitely live state
and the exact drop-and-replace discharge whenever a nontrivial initialized
alternative exists. A storage event that violates these preconditions is
malformed Built MIR and selects `InvalidOwnershipProof`.

Initialization also creates the complete drop-obligation inventory from the
one `logicalDropPlans` row keyed by that initialization event. A missing,
additional, or mismatched row is `InvalidFact` before source analysis. A
representation part absent from the plan has a `Positive` `Copy` decision, a
not-positive `Linear` decision, and no drop action, so it creates no
obligation. At an entry,
constant, fresh construction, or copy initialization, each plan component
creates one `DropResourceSubject` and one obligation. An ordinary move instead
pairs source and destination components by identical relative projection,
marker-decision alternative and payload, value type, and optional action, and
transfers the existing subject and obligation. A checked-cast move uses only
its verified route plan. Neither transfer creates a subject. Not-positive
`Copy` plus not-positive `Linear` is `Logical`; `Positive` `Copy` plus
`Positive` `Linear` is `Linear`; and not-positive `Copy` plus `Positive`
`Linear` is `LinearLogical`. A present drop action additionally requires a
not-positive `Copy` decision. Both `Positive` `Linear` forms link the same
initialization to exactly one `LinearObligationKey`. Every other combination
is `InvalidFact`. Partial
field initialization therefore creates obligations only for those fields;
initializing the remaining fields adds the missing obligations, while moving a
field transfers its obligations with that field and leaves its siblings
unchanged. An ordinary move and a successful checked-cast carrier transfer
record `DropTransfer`; they never discharge ownership. A checked-cast transfer
must satisfy the exact verified route plan and preserve the complete resource
subject; the same obligation key becomes pending at the paired result
projection with the result plan's `valueType` and `action`. No
result obligation is introduced independently. Return and consuming call
operands end the current function's responsibility with the corresponding
discharge kind.

A `Closed` logical drop accepts exactly `Initialized`. It executes every
currently initialized component in reverse declaration order, discharges each
obligation, invokes only each present logical drop action, emits
`LinearConsume` for each linked linear obligation, and
produces `Uninitialized`. An `Open` logical drop accepts `Uninitialized`,
`Initialized`, or `MaybeInitialized`. It is a no-op for an uninitialized
alternative; for every initialized alternative it executes exactly that
alternative's initialized components in reverse declaration order. All output
alternatives are `Uninitialized`. At a joined `MaybeInitialized` summary, the
co-located complete resource alternatives identify the exact pending
nontrivial component set; they are never cross-combined. A closed drop on `Uninitialized` or
`MaybeInitialized`, an open or closed drop on any state containing
`mayBeDead`, a missing component, a duplicated component, declaration-order
execution, or a drop of an obligation already moved or discharged is
`InvalidOwnershipProof`. Drop-and-replace uses `OverwriteDrop`, follows the
same closed/open rules for the previous generation, and only then commits the
replacement generation and its new obligations.

RFC 0007 admits logical-drop actions only under RFC 0006's first-implementation
`panic = "abort"` capability. `DropDischarge.components` is the authoritative
normal execution sequence. For each component in that sequence, the logical
drop performs one indivisible micro-transition immediately before invoking its
optional action: it removes that component's pending drop obligation, performs
the linked `LinearConsume` when present, marks that component generation
uninitialized, and passes the retained runtime payload to the exact action.
`None` continues immediately. `Some(action)` is called through the RFC 0006
abort-on-panic boundary: normal return continues with the next component, while
panic enters the terminal abort path and has no ownership, cleanup, or unwind
successor. Remaining components do not run after abort because the process
terminates. `AfterEvent(drop)` and the encoded `DropDischarge` exist only after
every component returns normally; no partial discharge fact is published for an
aborting execution.

The producer and verifier replay this component micro-sequence independently
from the authoritative overlay plan and complete relational alternative. They
require reverse declaration order, pre-consumption before every action call,
one normal continuation to the next component, and no commit before the whole
sequence completes. A logical drop, overwrite drop, or cast-failure drop with an
unwind successor, a cleanup-resuming panic successor, post-call consumption, a
skipped normal component, or a replacement commit before full normal completion
is `InvalidControlFlow` before source analysis. Enabling RFC 0006
`panic = "unwind"` does not weaken this rule: unwindable logical-drop actions
require a separate accepted RFC and new ownership-event and MIR revisions that
define per-component unwind state and remaining-cleanup continuation. Until
then, a requested unwind strategy remains rejected before lowering under RFC
0006, and every admitted logical-drop panic aborts.

`StorageDead` has a closed transfer. `Uninitialized` becomes `Dead`.
`Initialized` or `MaybeInitialized` may become `Dead` directly only when every
possibly initialized component has no drop requirement, so no drop or linear
obligation exists. A nontrivial initialized alternative must first pass through the exact
closed or open logical drop and reach `Uninitialized`; otherwise
`StorageDead` is `InvalidOwnershipProof`. `Dead` and every state containing
`mayBeDead` are also invalid; storage end is not idempotent. The transfer then
records `StorageEnded` for every descendant. Thus partial initialization,
field mutation, and CFG joins cannot silently discard a non-`Copy` or `Linear`
generation.

The forward solver carries drop, linear, and cast-carrier state in one complete
relational alternative; the initialization lattice remains its exact joined
summary. A drop introduction changes `Absent` or a
previously `Discharged` generation to `Pending`; a move updates its place; a
discharge records the first exact discharge. A second discharge, pending
obligation at a nontrivial `StorageDead`, or cross-product of independently
joined drop and linear alternatives is `InvalidOwnershipProof`.

A read, copy, move, borrow, return, or consuming call operand requires exactly
`Initialized`. Any state containing `mayBeDead` or `mayBeUninitialized` is not
readable. If every complete loss cause is `Moved`, the event produces one
`UseAfterMove` containing every sorted move cause. If any cause is
`NeverInitialized`, `Deinitialized`, or `StorageEnded`, the event produces one
`UninitializedPlaceUse` containing every sorted loss cause, including moves on
other predecessors. An unavailable state with no cause is
`InvalidOwnershipProof`; it is never relabelled as a source failure.

The forward solver processes blocks and edges in canonical point order. It
computes a least fixed point over all three initialization bits, complete loss-
cause sets, loan state, raw provenance, and linear path alternatives. Loop
backedges use the same union as every other edge. No lexical-depth shortcut or
dead-as-bottom collapse may decide a path-sensitive state.

### Loans, Reservation, Activation, And Invalidation

A loan identity is its borrow-creation event:

```text
LoanKey {
  issue: MirEventKey,
}

LoanActivation =
    Immediate
  | Deferred { activation: MirEventKey }

LoanFact {
  key: LoanKey,
  kind: Shared | Mutable,
  source: MovePathKey,
  destination: MovePathKey,
  activation: LoanActivation,
  region: RegionKey,
  sourceOrigins: SortedUniqueSequence<ReferenceOrigin>,
  parents: SortedUniqueSequence<LoanKey>,
}

DeferredActivationFact {
  loan: LoanKey,
  receiverSource: MirEventKey,
  activation: MirEventKey,
  receiverMode: RFC0005::ReceiverMode,
  adjustmentSource: SemanticTypeId,
  adjustmentDestination: SemanticTypeId,
  adjustmentSteps:
      NonEmptySequence<RFC0005::ReceiverAdjustmentStep>,
}

LoanPhase = Reserved | Active | Suspended

LoanPointState {
  loan: LoanKey,
  phases: SortedNonEmptySequence<LoanPhase>,
  suspendingChildren: SortedUniqueSequence<LoanKey>,
}
```

Loan kind tags are `Shared = 0x01` and `Mutable = 0x02`; activation tags are
`Immediate = 0x01` and `Deferred = 0x02`; `LoanPhase` tags are `Reserved =
0x01`, `Active = 0x02`, and `Suspended = 0x03`. All record fields encode in
declaration order. Loan keys order by canonical issue event. Source origins,
parent loans, and child keys order canonically; phase sequences order by tag.
`DeferredActivationFact` fields encode in declaration order. The authoritative
map is `OwnershipFunctionEventOverlay.deferredActivations`, orders by `loan`,
and requires every map key to equal `fact.loan`. No second deferred-activation
map is published in ownership facts.

A root borrow of owned storage has empty `sourceOrigins` and `parents`. A
reborrow has the complete source reference-origin set reaching its issue and
`parents` contains exactly every `LoanKey` named by those origins' `active`
regions. Input and static active regions remain in `sourceOrigins` but add no
parent loan. The loan region is always `RegionKey::Loan(key)`.

`LoanPointState` represents all reachable alternatives at one point. Join
unions phases and suspending-child keys. `Suspended` is present exactly when
`suspendingChildren` is non-empty; every listed child must name a live child
loan whose `parents` contains `loan` and whose source conflicts with the parent
source.
`Reserved` is legal only for a deferred mutable loan before its activation;
`Active` is the only phase of an immediate shared loan; a shared loan is never
suspended. A loan has no point-state row outside its region. The verifier
recomputes every phase and child set from the CFG and region solution.

The `LoanKey.issue` event is exactly the `Effect` slot carrying `BorrowIssue`,
not the source read or destination commit. Every shared loan activates
immediately after that effect. An explicit source `&mut` loan also activates
immediately. A deferred mutable receiver loan is reserved after its issue
effect. Deferred activation is legal only through one complete
`DeferredActivationFact` in the verified event overlay.

During `buildOwnershipEventOverlay`, the producer derives each candidate row
from one RFC 0009 dispatch receiver whose checked `receiverMode` is `Mutable`
and whose exact RFC 0005 `ReceiverAdjustment.steps` contains exactly one
`BorrowMutable` or `ReborrowMutable` step. The independent overlay verifier
starts from the same immutable admitted checked module, HIR, and Built MIR but
does not read the candidate map or any producer association table. It rebuilds
the checked `NodeId`-to-HIR-to-MIR join, eligibility predicate, and complete row
while the checked wrapper and its `BodyCheckingInput` remain live, then compares
canonical rows. The borrow step must be the final reference-producing step; any
following steps may only dereference the same receiver place and may not move,
copy, or create another borrow. An explicit source `&mut` borrow has no inserted
receiver-adjustment borrow step and is therefore immediate.

`receiverSource` is the associated call's receiver `Source` slot;
`activation` is that same call's `Effect` slot carrying `BorrowActivation`;
and `loan.issue` is the unique dominating `MirStatement::BorrowCreation` effect that writes
the temporary read by `receiverSource`. The fact copies `receiverMode`, source
type, destination type, and every adjustment step byte-for-byte from the
verified dispatch fact after the checked `NodeId`-to-HIR association; no AST
identity remains. The issue destination must be the receiver source place, the
issue and call must share one owner, and no intervening event may move,
overwrite, deinitialize, or expose that temporary. The call first executes the
receiver and every argument source slot; activation then changes `Reserved` to
`Active`; any result destination commits afterward. Activation must be
dominated by issue and reachable without leaving the destination reference's
storage.

The overlay map contains exactly one row for every eligible checked receiver
borrow and no row for an immediate loan. Missing, additional, swapped-call,
wrong-step, explicit-borrow, non-mutable-mode, non-final-borrow, or independently
rebuilt field mismatches are `InvalidFact`; a missing dispatch fact is
`MissingRequiredFact`. The producer cannot classify a loan as two-phase from
method spelling, place type, or MIR shape alone.

Ownership analysis consumes only that verified map. Its derived `LoanFact`
inventory must contain exactly one `Deferred` row for every overlay row with the
same issue and activation, and every deferred loan must have exactly one overlay
row; every other loan is immediate. The ownership producer and independent
ownership verifier check this bijection and the issue-to-receiver-to-activation
chain from Built MIR plus the immutable overlay. They never query RFC 0005 or
RFC 0009, inspect a checked wrapper, or reconstruct receiver adjustments after
the overlay transaction ends. A missing, additional, or disagreeing relation in
a candidate ownership proof is `InvalidOwnershipProof`.

A reserved mutable loan behaves as a shared loan for overlap checks: it blocks
writes, moves, drops, and another active or reserved mutable loan, while shared
reads may continue. Activation requires no other reserved or active overlapping
loan. An active mutable loan excludes every other overlapping loan and every
use of the borrowed source except through that loan. An active shared loan
excludes mutation, move, drop, and mutable borrowing of an overlapping path.

Assignment, deinitialization, drop, and move invalidate every overlapping live
loan unless the operation is performed through the unique active mutable loan
that owns the access and the referent is reinitialized before any later use.
Raw-pointer operations do not create safe loans or prove non-overlap.

### Region Origins And Non-Lexical Solution

Region identities cover origin lifetimes, physical storage, reference-value
generations, and closure values:

```text
BorrowInputKey = Receiver | Parameter { index: uint32 }

RegionKey =
    Static { owner: DefId }
  | Input { owner: DefId, input: BorrowInputKey }
  | Loan { loan: LoanKey }
  | Storage {
      live: MirEventKey,
      root: MovePathKey,
    }
  | LocalValue {
      introduction: MirEventKey,
      destination: MovePathKey,
    }
  | ClosureValue {
      construction: MirEventKey,
      closure: MovePathKey,
    }

RegionFact {
  key: RegionKey,
  livePoints: SortedUniqueSequence<OwnershipPoint>,
  outlives: SortedUniqueSequence<RegionKey>,
}

ReferenceRoot {
  region: RegionKey,
  referent: MovePathKey,
  introduction: MirEventKey,
}

ReferenceOrigin {
  root: ReferenceRoot,
  active: RegionKey,
  activation: MirEventKey,
}

ReferenceValueFact {
  value: RegionKey,
  origins: SortedNonEmptySequence<ReferenceOrigin>,
}

ReferencePointFact {
  path: MovePathKey,
  values: SortedNonEmptySequence<ReferenceValueFact>,
}
```

`BorrowInputKey` tags are `Receiver = 0x01` and `Parameter = 0x02`, matching
RFC 0013. Region tags are `0x01` through `0x06` in declaration order. Variant
and record fields encode in declaration order. Region keys, ownership points,
outlives keys, reference origins, and reference values order by complete
canonical bytes. `ReferencePointFact` values with the same `value` key merge by
the sorted union of origins and then encode once. Region membership is a set of
event-granular ownership cutpoints, not a source block interval or only an
enclosing CFG point.

`Storage` names one physical storage generation. For parameters and captures,
`live` is their synthetic `Entry` commit event; for an ordinary local or
temporary it is the exact `StorageLive` effect event. Its points start after
that event and extend through the cutpoint before the matching `StorageDead` on
each reachable path, and `root` must be the projection-free local root named by
that event. `LocalValue` names one reference-value
definition into the exact reference-typed local or projected destination
written by its introduction event. `ClosureValue` names the closure root
written by one construction event. The verifier derives all three inventories
from Built MIR; no producer-selected region key is legal.

`ReferencePointFact` is the complete reaching-definition relation for every
reference-typed move path whose initialization state contains the initialized
bit at a point. Maybe-initialized joins retain the complete values contributed
by initialized predecessors; readability is still rejected by the
initialization lattice. `ReferenceValueFact.value` must be `LocalValue` and
must name the definition that installed the reference in that place.
`ReferenceRoot.region` and `ReferenceOrigin.active` must be `Static`, `Input`,
or `Loan`; storage, value, and closure regions never masquerade as reference
authority. All nested owners must equal the function owner.

A root borrow creates one root and active pair
`(Loan(issue), borrowedPlace, issue)`. An input reference starts with an
`Input(owner, input)` root and active region at its synthetic entry event. A
verified static reference uses `Static(owner)` for both regions and its creation
event. The borrow effect establishes the loan and origins; the later destination
commit establishes the `LocalValue` definition. Every one of these values,
including input and static values, receives a `LocalValue` definition. A
reborrow preserves each complete `ReferenceRoot`,
sets `active = Loan(child)`, sets `activation = child.issue`, and records
constraints to every source active region. Thus direct-input and static
authority survive reborrow while the child loan remains the active liveness
constraint. No reference origin is inferred from a type or place name.

Reference transfer is exact:

1. Copy preserves the complete source-origin set, leaves the source reaching
   definition available, and creates one `LocalValue(copyDestinationEvent,
   destination)` definition.
2. Move preserves the complete source-origin set in one new destination value
   definition and kills the source definition after the move event.
3. Initialize, assignment, and overwrite kill every earlier destination value
   after performing the required read/drop checks, then install exactly the new
   value definition and complete origin set. Deinitialization and
   `StorageDead` install no value.
4. A CFG join unions reaching value definitions. When the same definition
   reaches through multiple predecessors, its origins are the complete sorted
   union; definitions from different introduction events remain distinct.
5. RFC 0013 `DirectRoot` call results perform the same destination-definition
   step with the exact receiver or parameter origins. `None` creates no
   reference origin.
6. Reborrow reads every reaching source definition, creates the child-loan
   constraints described above, preserves each root, replaces each active
   region with the child loan, and installs the complete resulting origins in
   one new destination definition.

An operation on an aggregate place applies these rules componentwise to every
known reference-typed descendant using the exact projection-relative source and
destination paths. A partial overwrite kills only conflicting descendants; a
complete overwrite or move kills all descendants. Dynamic-index conflicts use
the place-conflict relation and cannot select one convenient element.

The forward reference-value equations are solved with initialization and loan
state. The independent verifier recomputes every definition, kill, origin
union, call result, and reborrow relation. Missing, additional, merged-across-
generation, or origin-losing rows are `InvalidOwnershipProof`.

`LocalValue.livePoints` is an NLL value lifetime, not its destination's full
storage lifetime. For each definition, the solver seeds the exact before-event
cutpoint that uses, copies, moves, borrows, returns, stores, captures, or drops
that reaching definition. A non-consuming use also seeds its after-event
cutpoint. It propagates liveness backward only through ownership cutpoints and
CFG edges on which that definition reaches, stopping at the definition commit
and at the after-event cutpoint of every overwrite, move, deinitialization,
logical drop, or `StorageDead` kill. Consequently the region ends after its
last non-consuming use, at a consuming use, or at an earlier kill on each path
and may end before the physical `Storage` region.
`ClosureValue` uses the same reaching-definition liveness for the constructed
closure and its ordinary move-path lineage: a move transfers that lineage, a
copy adds a second reaching place, and overwrite, deinitialization, or storage
death kills only the affected branch. Its live points are the union of actual
uses of every reaching copy, never the physical lifetime of all closure-typed
storage.

The orientation is exact: `a.outlives` contains `b` iff `a` outlives `b`, so
`b.livePoints` is a subset of `a.livePoints`. The solver first derives the
primitive directed relation `a -> b` from the constraints below, where the
arrow has that same outlives orientation. It then computes the complete
reflexive-transitive closure. Every `RegionFact.outlives` contains its own key
and every transitively outlived key, with no other row. Mutually reachable keys
are legal and must have equal solved live-point sets. The verifier independently
reconstructs the primitive edges, closure, strongly connected components, and
least live-point solution; storing only primitive edges or omitting reflexive
rows is `InvalidOwnershipProof`.

The solver constructs primitive outlives constraints from:

- physical storage and NLL reference-value liveness;
- borrow issue, use, copy, move, assignment, and destruction;
- reborrow parent-child relations;
- call arguments and the exact RFC 0013 `BorrowReturnRelation`;
- return and storage destinations;
- closure captures; and
- every CFG edge on which a reference value remains live.

It seeds the directly required points, adds `Storage -> Loan` for a root borrow,
adds every `ReferenceOrigin.active -> LocalValue` or `-> ClosureValue`
constraint required by a transfer or escape, propagates each
`b` point into `a` for every primitive `a -> b`, and computes the least point
sets satisfying the closure. A loan region must remain within the exact source
`Storage` region and, for a reborrow, within every source active region. An
input region is available for the complete call and may escape only through the
exact direct-root relation published for the callable. A static region is
available at every point but still obeys mutable-exclusivity rules.

At a call, the analysis resolves the exact summary through the supplied borrow
evidence lease and repository capability. `None` creates no borrowed result
origin. `DirectRoot` copies the selected receiver or parameter origins to the
new destination value definition. Missing, additional, stale, unauthorized, or
disagreeing summaries are RFC 0010 input invariants and never become source
failures.

### Reborrow Suspension And Restoration

A borrow through a reference creates a child loan and records the complete
reaching `sourceOrigins` and `parents` relations. The child region must be a
subset of every source-origin region. A mutable parent is `Suspended` at every
point where an overlapping child that names it is reserved or active. The
parent cannot be used directly at those points.

Restoration is path-sensitive. On an edge, the parent becomes active exactly
when no child region contains the edge destination and the parent region does.
When different branches end different children, the join computes suspension
from the complete live-child set. A child point outside any source-origin region
selects `BorrowDoesNotLiveLongEnough` with every failing origin. A missing
restoration transition in an otherwise verified candidate is
`InvalidOwnershipProof`.

### Return, Storage, And Closure Escape

Return conformance is exact:

- `BorrowReturnRelation::None` permits only a non-borrowed result or origins
  whose `root.region` is `Static`;
- `DirectRoot(input)` requires every returned origin's `root.region` to equal
  that exact input, including a reborrow whose active region is a child loan;
- an origin whose `root.region` is a local or temporary loan cannot enter a
  return point; and
- nested, parametric, opaque, and unverified extern results are rejected before
  Built MIR by RFC 0013 and are an input invariant if present.

For assignment, every source origin must outlive the new destination
`LocalValue` NLL region. The destination region ends after the last use or an
earlier overwrite, move, deinitialization, or `StorageDead`; it is never
substituted with the complete physical `Storage` interval. Receiver fields,
out-parameters, globals, heap objects, and other call-escaping storage require a
static origin. RFC 0013 publishes no storage-effect relation, so an input borrow
stored into call-escaping storage is rejected.

Closure construction consumes the exact checked capture inventory. `Move` and
`Copy` captures update ordinary ownership and linear state. A shared or mutable
reference capture requires every origin to contain the exact `ClosureValue` NLL
region. RFC 0013 rejects borrow-bearing body-local closure contracts before
checked-module assembly; a record that bypasses that source gate is
`AdditionalFact` at ownership input validation.

Every successful escape operand has one complete proof record:

```text
EscapeFactKey = MirEventKey

EscapeKind =
    Return
  | Store { destination: MovePathKey, destinationRegion: RegionKey }
  | ClosureCapture { closure: MovePathKey, closureRegion: RegionKey }

EscapeOriginRoute =
    Direct
  | RawCarrier { carrier: RawProvenanceCarrierKey }

EscapeOriginCause {
  origin: ReferenceOrigin,
  route: EscapeOriginRoute,
}

EscapeProof =
    Owned
  | Static
  | DirectInput { input: BorrowInputKey }
  | Contained {
      requiredPoints: SortedNonEmptySequence<OwnershipPoint>,
    }
  | AddressOnly

VerifiedEscapeFact {
  key: EscapeFactKey,
  source: MovePathKey,
  kind: EscapeKind,
  origins: SortedUniqueSequence<EscapeOriginCause>,
  rawCarriers: SortedUniqueSequence<RawProvenanceCarrierKey>,
  proof: EscapeProof,
}
```

Escape-kind tags are `0x01` through `0x03`; route tags are `Direct = 0x01` and
`RawCarrier = 0x02`; proof tags are `0x01` through `0x05`, all in declaration
order. Variant and record fields encode in declaration order. Escape keys use
the canonical `MirEventKey` order. Origin causes and carriers order by complete
canonical bytes. Every nested sequence is sorted and unique.

`origins` is the complete multi-origin relation. It contains one `Direct` row
for every direct reference origin and one `RawCarrier` row for every
`Reference` origin in every reaching raw carrier's transitive provenance. A
raw-route carrier must occur in `rawCarriers`; its raw provenance must contain
the exact `ReferenceOrigin`. A loan origin uses the loan issue as its
root introduction and active activation; input roots use their synthetic entry
event and static roots use their creation event. No two routes or carriers collapse merely because
they name the same region. `rawCarriers`
contains every reaching raw carrier, including carriers with only non-reference
origins. The verifier derives and compares both complete sequences.

`Owned` requires both sequences empty. `Static` requires a non-empty origin
sequence, every `origin.root.region` to be `Static`, and every active region to
contain `BeforeEvent(key)`. `DirectInput` is legal only for `Return`,
requires a non-empty origin sequence, requires every origin to name the RFC
0013 receiver or parameter in `origin.root.region`, and requires every active
region to contain `BeforeEvent(key)`. `Contained` requires a non-empty origin
sequence, requires `requiredPoints` to equal the destination or closure point
set derived from Built MIR, and requires every `origin.active` region to contain
that complete set.
`AddressOnly` requires an empty origin sequence and non-empty `rawCarriers`.
It proves only that an escaping value remains an address. It cannot justify a
safe-reference result, and no raw-carrier origin route can enter a reference
definition or `DirectInput`, `Static`, or `Contained` reference proof.

The verifier derives one row for every admissible escape operand and rejects a
missing, additional, wrong-kind, wrong-ordinal, missing-origin-route,
incomplete-point-set, foreign-carrier, or proof-incompatible row as
`InvalidOwnershipProof`.

### Copy And Linear Marker Decisions

Ownership analysis does not query a marker by text and does not rerun RFC
0015's marker proof engine. Instead, each event-overlay pass constructs a fresh
proof input from `OwnershipEventOverlayInput.body`, performs the complete query
set, and publishes or independently verifies the authoritative `markerUses`
map. The query set contains exactly
one canonical key for every `Copy` operand and both the `Copy` and `Linear`
keys for every phase-one resource-projection node. This includes every
descendant below an action-free `Positive` `Linear` aggregate and every
candidate place that postorder folding omits or subsumes. A direct declared,
builtin, or dynamic action is the only aggregate boundary that stops descendant
query discovery. Repeated equal `(event, marker, subject)` queries under the
same policy and coherence revisions share one map row. No other query enters
the map.

The event is the exact source operand event for a `Copy` operand and the exact
initialization event for a resource-plan traversal. The marker is the resolved
RFC 0015 `DefId` selected by the checked input for the semantic role; source
spelling, a prelude name, an interface ordinal, and a local handle are never
marker identity. For the standard `Copy` and `Linear` roles, that identity
must come from the finalized toolchain-core authority and retain its exact
`CoreSemanticContextFingerprint` and verified distribution digest through the
body-derived marker-proof lineage. A user definition with equal spelling, a
foreign core context, or a different distribution digest is not the same
marker. The subject is the exact canonical semantic type queried at
that event. The key carries the exact policy-registry and frozen-coherence
revisions from the same body-derived proof input; both must match the admitted
checked module and its checked-facts lineage. The proof input itself, its
interning capability, active stacks, and memo are not fields of
`OwnershipMarkerUse`, the overlay, ownership facts, or any repository.

The RFC 0015 result maps into the closed persisted algebra without loss:

| RFC 0015 `MarkerProofResult` | RFC 0007 `OwnershipMarkerDecision` |
|---|---|
| `Positive { proof }` | `Positive { proof }` |
| `Negative { explicitFact }` | `ExplicitNegative { explicitFact }` |
| `Unsatisfied` | `Unsatisfied` with no fact payload |
| `InvariantRejected { failures }` | No marker use, plan, or overlay is published |

`InvariantRejected` is not an encodable marker-decision tag. Identity failures
select RFC 0010 `IdentityInvariantRejected`; checker failures select
`IrInvariantRejected` with `InvalidFact` at `OwnershipProofValidation`. The
typed checker failures remain in the compiler bug bundle. Neither branch may
be converted to `Unsatisfied` or persisted in a partial overlay.

The independent overlay verifier reconstructs the complete phase-one query
tree and phase-two postorder fold without reading the candidate's query set or
plans. It constructs its own RFC 0015 proof input from the shared live body
input and reruns each query using a fresh verifier query context, active stack,
and memo that share no state with the producer. For `Positive`,
an explicit proof must be the exact same-key positive fact in the frozen
coherence view; a structural, builtin, or policy-subject proof is reconstructed
independently.

For `ExplicitNegative`, `explicitFact.key` must equal `(key.marker,
key.subject)`, its polarity must be `Negative`, its evidence must be
`Explicit`, and every byte must equal the same-key verified coherence fact.
Structural, builtin, and policy-subject negative facts are impossible. For `Unsatisfied`, the
record contains only tag `0x03`; the verifier repeats RFC 0015's ordered
resolution and proves that the result is exactly `Unsatisfied`. A foreign,
stale, swapped, malformed, missing, additional, or differently ordered use is
`InvalidFact` and publishes no verified overlay.

`Positive` is the only positive decision. `ExplicitNegative` and `Unsatisfied`
are both not positive, but they are neither equal nor interchangeable:
`ExplicitNegative` carries one real explicit RFC 0005 negative `MarkerFact`,
whereas `Unsatisfied` carries no `MarkerFact` at all. Not-positive is a verifier
predicate, not a fourth codec tag and not authority to synthesize a negative
fact. The canonical encoder, equality, dump, producer, and verifier preserve
the two alternatives exactly.

A `copy` operand requires the exact `Positive` `Copy` use for its event and
subject. A consuming operation without that decision is a move. A Built MIR
copy with a missing, `ExplicitNegative`, `Unsatisfied`, foreign, or mismatched
decision is `MissingRequiredFact` or `InvalidFact` before analysis.

### RFC 0025 Acceptance Synchronization

On 2026-07-25, the accepted RFC 0025 proposal at SHA-256
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`
made toolchain-core context and distribution-digest lineage mandatory for
standard `Copy` and `Linear` marker identity. The exact positive,
explicit-negative, and unsatisfied ownership decisions, the fail-closed
`InvariantRejected` path, complete descendant query inventory, and independent
producer/verifier construction remain authoritative.

The ordinary RFC 0015 marker inventories are built for the complete session
after ordinary binding. Every ownership consumer validates its byte-equal
core-role projection against the finalized standard authority before reading a
marker decision. A missing, foreign, stale, swapped, or
distribution-mismatched lineage publishes no marker use, resource plan, event
overlay, or ownership facts. Product implementation remains tracked by RFC
0025's `R25` tasks; this synchronization does not complete an ownership slice.

A `Positive` `Linear` decision creates an obligation at each initialization;
`ExplicitNegative` and `Unsatisfied` create none:

```text
LinearObligationKey {
  introduction: MirEventKey,
  place: MovePathKey,
}

LinearCarrierKey {
  obligation: LinearObligationKey,
  creation: MirEventKey,
  place: MovePathKey,
}

LinearTransfer {
  from: MovePathKey,
  to: MovePathKey,
  event: MirEventKey,
}

LinearConsumption {
  place: MovePathKey,
  event: MirEventKey,
  kind: Return | ConsumingCall | LogicalDrop,
}

LinearObligationFact {
  key: LinearObligationKey,
  subject: SemanticTypeId,
  transfers: SortedUniqueSequence<LinearTransfer>,
  consumptions: SortedUniqueSequence<LinearConsumption>,
}

LinearCarrierTransition {
  predecessor: LinearCarrierKey,
  transfer: LinearTransfer,
}

LinearCarrierFact {
  key: LinearCarrierKey,
  incoming: SortedUniqueSequence<LinearCarrierTransition>,
}

LinearObligationState =
    Absent
  | Pending { carrier: LinearCarrierKey }
  | Consumed {
      carrier: LinearCarrierKey,
      first: LinearConsumption,
    }

OwnershipResourceStateAlternative {
  dropStates: SortedMap<DropObligationKey, DropObligationState>,
  states: SortedMap<LinearObligationKey, LinearObligationState>,
  castStates: SortedMap<CastCarrierKey, CastCarrierPhase>,
}

OwnershipResourcePointState {
  alternatives: SortedNonEmptySequence<OwnershipResourceStateAlternative>,
}
```

Consumption-kind tags are `Return = 0x01`, `ConsumingCall = 0x02`, and
`LogicalDrop = 0x03`. `LinearObligationState` tags are `Absent = 0x01`,
`Pending = 0x02`, and `Consumed = 0x03`. Every record and variant field encodes
in declaration order. Obligation keys order by canonical introduction then
place. Carrier keys order by obligation, creation, then place. Transfers order
by event, from, then to. Consumptions order by event, place, then kind. State
maps order by obligation; each alternative encodes `dropStates`, `states`, then
`castStates`, and alternatives order by their complete canonical bytes.
Carrier transitions order by predecessor then transfer.

Every function drop obligation, linear obligation, and cast carrier appears
exactly once in every `OwnershipResourceStateAlternative` in its corresponding map.
Linear `Absent` means that
the dynamic path has not introduced the current generation. `Pending(carrier)`
names its exact current carrier.
`Consumed(carrier, first)` ties the first consumption to the carrier consumed
on that same path. The complete carrier map is a structural carrier-to-
obligation relation: `carrier.key.obligation` is authoritative, every
incoming predecessor has the same obligation, and no carrier may occur in a
state for a different obligation. A function with no drop or linear
obligations and no cast carriers has exactly one alternative containing three
empty state maps at every reachable point.

A root carrier has `creation == obligation.introduction`,
`place == obligation.place`, and an empty `incoming` sequence. A transferred
carrier has a non-empty `incoming` sequence. For every incoming transition,
`creation == transition.transfer.event`, the predecessor names the same
obligation, `transfer.from == predecessor.place`, and
`transfer.to == carrier.place`. The transfer must occur exactly once in the
enclosing `LinearObligationFact.transfers` sequence. That sequence contains
exactly the static transfers reached by the obligation; its consumptions
sequence likewise contains exactly the consuming events reached by the
obligation. The incoming sequence contains one transition for every reaching
predecessor alternative at that static transfer event. Distinct predecessors
that converge at the same destination event therefore produce one carrier with
multiple incoming transitions; neither predecessor may be selected or
omitted.

The carrier relation may contain strongly connected components when a value is
transferred around a loop. Every carrier must nevertheless be reachable from
the obligation's unique root, and no transition may target the root. For each
obligation, the producer computes the least simultaneous fixed point of point
alternatives and incoming transitions: seed the root at its introduction,
apply each MIR transfer to every reaching complete alternative, union complete
alternatives at CFG joins, add the corresponding predecessor transition, and
repeat until neither relation grows. Carrier keys are finite because a
non-root key is determined by the obligation and one static MIR transfer event
and destination. The verifier independently derives the complete carrier
inventory, incoming relation, root reachability, strongly connected
components, and least point-alternative solution.

`OwnershipResourcePointState` retains a sorted set of complete alternatives, not
independent unions of absence, places, and consumption sites. A CFG join is set
union of complete alternatives. It neither forms a Cartesian product nor
merges a consumption from one alternative with a carrier from another.

The transfer rules are exact:

1. At an introduction, each complete alternative transforms `Absent` or
   `Consumed` for that obligation to `Pending(rootCarrier)` and leaves all
   other obligations unchanged. Its earlier-generation consumption is removed
   only in that transformed alternative. An alternative already containing
   `Pending` proves reintroduction before consumption and emits
   `LinearNotConsumed`; that rejected alternative remains unchanged for
   diagnostic exploration.
2. A move transforms only an alternative whose matching obligation is
   `Pending(sourceCarrier)` at the operand place. It replaces that state with
   `Pending(destinationCarrier)` and adds the destination carrier plus one
   incoming transition from `sourceCarrier`. If other alternatives reach the
   same transfer with different source carriers, all transitions target that
   same destination carrier. No absent, consumed, or unrelated-obligation state
   is synthesized. The relations are finite because they contain static MIR
   events, not dynamic loop iterations.
3. A verified copy of a `Copy + Linear` value leaves every source obligation
   unchanged and introduces a distinct destination obligation keyed by that
   copy initialization event and destination place.
4. A successful checked-cast commit is a transfer, not a consumption. It is
   legal only when the carrier/result resource predicate proves exactly one
   result component with a `Positive` `Linear` decision and the same canonical
   `DropResourceSubject`. It creates the
   destination carrier at `successTransfer`, records the ordinary
   `LinearTransfer`, and preserves the obligation key. An absent, duplicated,
   resource-subject-changing, source-only, or result-only linear route is
   `InvalidOwnershipProof` for a forged candidate and cannot be published by
   the checker.
5. A consuming event transforms each matching `Pending(carrier)` alternative
   to `Consumed(carrier, currentConsumption)`. A matching `Consumed` alternative
   emits `LinearConsumedTwice` with the complete sorted first-consumption causes
   from exactly the alternatives on which the event is a second consumption.
   `Absent` and states whose carrier does not map to the operand are unchanged;
   ordinary initialization checking independently rejects an unavailable
   operand. Successful alternatives and rejected alternatives are never
   cross-combined.
6. A normal obligation-lifetime exit emits `LinearNotConsumed` exactly when at
   least one complete alternative contains `Pending` for that obligation.
   `Absent` and `Consumed` alternatives satisfy it. Normal exits are `Return`,
   `ResidualReturn`, `Break`, and `Continue` exits that leave the obligation's
   storage-live scope, plus `StorageDead`. Panic, unwind, and unreachable exits
   follow RFC 0006 and do not weaken any normal-path
   alternative.

Every CFG edge, including a loop backedge, uses the same complete-alternative
transfer and set-union equations. A `Pending` alternative may cross a backedge
unchanged, or through a finite carrier-transition cycle, when the obligation
was introduced before the loop and remains available for consumption after the
loop. A backedge is never a failure merely because it carries `Pending`.

If a backedge reaches the obligation's own repeated introduction, the normal
introduction rule applies: `Absent` or `Consumed` starts the next dynamic
generation at the root, while `Pending` emits `LinearNotConsumed` because the
previous generation was not consumed. The least fixed point retains every
complete alternative at the loop header and every loop exit. At each normal
exit defined above, every reachable alternative must be `Absent` or `Consumed`;
any `Pending` alternative emits `LinearNotConsumed`. `Consumed` proves exactly
one consumption because a later consumption of that same generation selects
`LinearConsumedTwice`. An obligation on a path with no normal exit is not
rejected solely for remaining pending on that infinite path.

A consume/reinitialize diamond remains valid because each branch's carrier,
obligation state, and first consumption remain in one complete alternative
through the join. Static obligation and carrier keys therefore prove every
iteration without allocating a dynamic generation identifier. The verifier
independently recomputes all alternatives, carrier transitions, loop fixed
points, resets, and per-exit failures.

### Unsafe Boundaries And Raw Provenance

Each RFC 0005 unsafe operation reaches one collision-free unsafe occurrence
within one MIR event with its operation kind, requirement, enclosing
acknowledgement, and source span:

```text
UnsafeBoundaryKey {
  event: MirEventKey,
  unsafeOrdinal: uint32,
}

MirUnsafeOccurrence {
  key: UnsafeBoundaryKey,
  operation: RFC0005::UnsafeOperation,
  requirement: RFC0005::UnsafeRequirement,
  acknowledgement: Maybe<MirEventKey>,
  sourceSpan: SourceSpan,
}

VerifiedUnsafeBoundaryFact {
  key: UnsafeBoundaryKey,
  operation: RFC0005::UnsafeOperation,
  requirement: RFC0005::UnsafeRequirement,
  acknowledgement: MirEventKey,
  rawInputs: SortedUniqueSequence<RawProvenanceCarrierKey>,
  rawOutputs: SortedUniqueSequence<RawProvenanceCarrierKey>,
}

RawProvenanceCarrierKey {
  introduction: MirEventKey,
  destination: MovePathKey,
}

RawProvenanceOrigin =
    Reference { origin: ReferenceOrigin }
  | RawInput { input: BorrowInputKey }
  | StaticAddress { creation: MirEventKey }
  | UnsafeAddress { boundary: UnsafeBoundaryKey }

RawProvenanceFact {
  key: RawProvenanceCarrierKey,
  predecessors: SortedUniqueSequence<RawProvenanceCarrierKey>,
  origins: SortedNonEmptySequence<RawProvenanceOrigin>,
}

RawOriginUniverse = SortedUniqueSequence<RawProvenanceOrigin>
```

`UnsafeBoundaryKey` uses canonical `MirEventKey` encoding and order and must
identify exactly one RFC 0005 fact. Within an event, `unsafeOrdinal` is the
zero-based contiguous index in the RFC 0007 overlay function's encoded
`unsafeOccurrences` subsequence for that event. The subsequence preserves the
verified checked-fact/HIR association order: operand occurrences in language
evaluation order, each place's base followed by its projections from base to
result, and the enclosing operation occurrence last. It is never sorted by
unsafe-operation tag or source span. A place remains one ownership source slot
even when two projections or a projection plus its enclosing checked cast
require unsafe acknowledgement; distinct unsafe ordinals prevent key collision
without inventing another value transfer.

No HIR or Built MIR field named `unsafeOccurrences` is accepted. The builder
walks the admitted checked facts, immutable HIR, and Built MIR and emits one
candidate `MirUnsafeOccurrence` for each RFC 0005 `UnsafeScopeFact`. The
separate verifier walks the same immutable inputs independently, reconstructs
the exact operation kind, requirement, acknowledgement scope, source span, HIR
operation, Built operation, and derived event for every expected occurrence,
and compares the complete candidate inventory without reading a producer
association table. The
overlay codec, not `zom.mir-revision`, fixes and hashes these fields. A
missing, additional, reordered, duplicated, gapped, wrong-span,
wrong-acknowledgement, or source-incompatible occurrence selects RFC 0010
`InvalidFact` at `OwnershipProofValidation` before ownership analysis. An event
slot carries the set-valued `UnsafeOperation` role when its occurrence
subsequence is non-empty; multiplicity exists only in `UnsafeBoundaryKey`,
never by duplicating a role.

RFC 0005
`UnsafeOperation` tags remain `RawDereference = 0x01`, `RawCast = 0x02`,
`ExternCall = 0x03`, `Transmute = 0x04`, and `PackedFieldAccess = 0x05`;
`UnsafeRequirement` tags remain `None = 0x01` and `RawPointerBoundary =
0x02`. A verified boundary fact exists exactly for each RFC 0005
`UnsafeScopeFact`, requires `RawPointerBoundary`, and identifies the dominating
Built MIR `MirStatement::UnsafeScopeBoundary(Enter)` effect for the exact
checked enclosing unsafe scope. The verifier reconstructs the complete matching
`MirStatement::UnsafeScopeBoundary(Exit)` cut
set, requires the enter to dominate the operation, and requires every path from
the operation to a function exit or point outside the scope to cross exactly
one member of that set. `MirUnsafeOccurrence.acknowledgement` is `Some` of that
enter effect only when RFC 0005 records `acknowledged = true`; it is `None`
otherwise. The event owner, source scope, nesting stack, checked enclosing
unsafe node, HIR scope, operation event, and exit cut set must all agree. `None`
remains part of the upstream requirement tag space but is invalid in a verified
boundary fact. An unacknowledged operation produces a source failure and
therefore no candidate boundary fact.

Boundary keys order by event then `unsafeOrdinal`; both key fields and
occurrence fields encode in declaration order. Carrier-key and provenance-fact
fields encode in declaration order. Carrier
keys order by introduction then destination. Provenance-origin tags are
`Reference = 0x01`, `RawInput = 0x02`, `StaticAddress = 0x03`, and
`UnsafeAddress = 0x04`; variants encode fields in declaration order and order
by complete canonical bytes. Predecessors and origins are sorted, unique, and
complete.

#### Ownership Event Overlay Codec

The shared canonical primitives are the ones defined under Ownership Facts
Canonical Codec And Revision. `MirLocation` encodes
`Frame(expanded owner DefId key)` then `MirPoint`; `MirEventKey` appends
`uint32be(operandOrdinal)`. `UnsafeBoundaryKey` appends
`uint32be(unsafeOrdinal)`. `SourceSpan` uses the RFC 0011 expanded
`SourceFileKey`, `uint64be(byteStart)`, and `uint64be(byteEnd)` encoding. Each
function overlay record encodes:

```text
Frame(expanded owner DefId key)
EncodeSortedMap(slots)
EncodeSortedMap(deferredActivations)
EncodeSortedMap(unsafeOccurrences)
EncodeSortedMap(markerUses)
EncodeSortedMap(logicalDropPlans)
EncodeSortedMap(castResourcePlans)
```

The enclosing function-map key must equal `owner`; each slot-map key must equal
`MirEventSlot.key`; each deferred-activation key must equal
`DeferredActivationFact.loan`; and each unsafe-map key must equal
`MirUnsafeOccurrence.key`. Each marker-use map key must equal
`OwnershipMarkerUse.key`; each logical-drop-plan key must equal its
initialization event; and each cast-resource-plan key must equal
`VerifiedCastResourcePlanFact.key`. Every event owner must equal the function
owner. The decoder rejects any disagreement, duplicate, gap, additional row,
missing row, or out-of-order key without normalization.

`OwnershipEventOverlayRevision` is SHA-256 over:

```text
ASCII("zom.ownership-event-overlay")
0x00
ContextFingerprint
Frame(Encode(expanded owning ModuleKey))
CheckedFactsRevision
Encode(MirRevisionId digest)
EncodeFramedSequence(OwnershipFunctionEventOverlay in expanded DefId order)
```

The canonical domain is `zom.ownership-event-overlay`. This RFC is
`IMPLEMENTING`; the complete event-overlay artifact remains gated. The schema
has one domain and decoder. A schema change directly replaces this contract,
its producers, consumers, verifier, and vectors. The empty oracle
uses zero fingerprint bytes, module bytes `a1`, checked-facts bytes `44`, MIR
digest bytes `22`, and no functions. Its 141-byte preimage is:

```text
7a6f6d2e6f776e6572736869702d6576656e742d6f7665726c61790000000000000000000000000000000000000000000000000000000000000000000000000000000001a1444444444444444444444444444444444444444444444444444444444444444422222222222222222222222222222222222222222222222222222222222222220000000000000000
```

Its SHA-256 is
`9e673e954367c3f2783cef1a9ca46e4d7e89040f2d4285ac6e42c2137bbed1d2`.
The empty-function oracle adds owner bytes `b1` and six empty maps. Its
206-byte preimage is:

```text
7a6f6d2e6f776e6572736869702d6576656e742d6f7665726c61790000000000000000000000000000000000000000000000000000000000000000000000000000000001a144444444444444444444444444444444444444444444444444444444444444442222222222222222222222222222222222222222222222222222222222222222000000000000000100000000000000390000000000000001b1000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
```

Its SHA-256 is
`5e36e3dd6068992f4e3b99ea9eb7df4e3836f9f8c40eb9821238a3c6090d724c`.

The unsafe-collision oracle uses owner `b1`, one effect slot at
`BeforeStatement(block=1, ordinal=0)` with roles `Operation` and
`UnsafeOperation`, then `RawDereference` and `RawCast` occurrences at ordinals
zero and one. Both require `RawPointerBoundary` and acknowledgement at the same
event; component-test expanded source-key bytes `c1` carry spans `[0, 1)` and
`[1, 2)`. Its deferred-activation map is empty and its 513-byte preimage is:

```text
7a6f6d2e6f776e6572736869702d6576656e742d6f7665726c61790000000000000000000000000000000000000000000000000000000000000000000000000000000001a1444444444444444444444444444444444444444444444444444444444444444422222222222222222222222222222222222222222222222222222222222222220000000000000001000000000000016c0000000000000001b1000000000000000100000000000000160000000000000001b10200000001000000000000000000000000000000310000000000000001b10200000001000000000000000002000000000000000200000000000000010100000000000000011400000000000000000000000000000002000000000000001a0000000000000001b1020000000100000000000000000000000000000000000000440000000000000001b102000000010000000000000000000000000102010000000000000001b102000000010000000000000000c100000000000000000000000000000001000000000000001a0000000000000001b1020000000100000000000000000000000100000000000000440000000000000001b102000000010000000000000000000000010202010000000000000001b102000000010000000000000000c100000000000000010000000000000002000000000000000000000000000000000000000000000000
```

Its SHA-256 is
`bb0547e574d4e5929b0c23807103b8566895fe9df68de0e9cfff01d3a9679f85`.
The component source key is intentionally minimal; semantic fixtures use a
registry-valid expanded RFC 0011 key. Independent production and test encoders
must reproduce all three byte strings and hashes. Resource-plan component
oracles additionally encode every action, all three marker-decision tags,
positive explicit, structural, builtin, and policy-subject proofs, including
`SharedReference`, `ConstRawPointer`, and `MutableRawPointer`, explicit negative
facts, payload-free unsatisfied decisions, policy and coherence lineage, plan
key, route, key/value disagreement, and all four positive/not-positive
Copy/Linear combinations. Separate fixtures substitute `ExplicitNegative` and
`Unsatisfied` in every not-positive position and require distinct bytes while
preserving the same plan-presence predicate. No fixture manufactures a negative
`MarkerFact` for `Unsatisfied`. Mutation oracles change each
unsafe ordinal, occurrence order, operation, requirement, acknowledgement,
span endpoint, stage, and role independently and require revision inequality
or verifier rejection.

The two body-derived `MarkerProofInput` values, their producer and verifier
query contexts, their active stacks and completed-result memos, and the private
phase-one projection tree are pass-duration authority and are never encoded.
The authoritative deferred-
activation projection adds one event-overlay map and removes its duplicate
ownership-facts map. The vectors below fix the canonical schemas. Non-empty
resource-plan
oracles additionally require both marker-use rows for every phase-one node,
including descendants suppressed by a folded parent. They exercise a direct-
action boundary; an action-free positive `Linear` and positive `Copy` parent
whose immediate fields are all positive `Copy`; both `ExplicitNegative` and
`Unsatisfied` immediate-field failures; an action-free positive `Linear` and
not-positive `Copy` parent with empty and non-empty child folds; and an action-
free not-positive `Linear` parent with empty and non-empty child folds. The
mutation matrix independently removes, adds, swaps, or changes a descendant
query; changes either marker decision; changes canonical field order; applies
preorder instead of postorder; selects the wrong parent action; retains a
subsumed child; or emits a parent omitted by the fold. Each mutation must be
rejected or change the overlay revision and, when embedded, the enclosing
ownership facts revision.

Deferred-activation codec fixtures encode every receiver mode, source and
destination type, adjustment-step tag and payload, issue, receiver-source, and
activation event. Mutations independently remove or add a row, swap its call,
change a field or map key, reorder adjustment steps, classify an explicit
borrow, or substitute an immediate loan. Each mutation is rejected or changes
the overlay revision and therefore changes any enclosing ownership facts revision.

The function's `RawOriginUniverse` is closed and finite. It is exactly the
sorted union of these root classes and no others:

1. one `Reference(origin)` row for each distinct reaching `ReferenceOrigin` at
   a reference-to-raw conversion root;
2. one `RawInput(input)` row for each raw-typed receiver or parameter;
3. one `StaticAddress(creation)` row for each Built MIR static-address root;
   and
4. one `UnsafeAddress(boundary)` row for each acknowledged unsafe operation
   that creates a raw address without a raw or reference predecessor.

Every universe row must occur in at least one root carrier with no predecessor;
every root-carrier origin must occur in the universe. Derived carriers cannot
introduce an origin. A reference-conversion root contains exactly every
reaching `ReferenceOrigin`; each other root contains its one input, static, or
unsafe seed. Let `Uref`, `Uinput`, `Ustatic`, and `Uunsafe` be the four verified
distinct-origin counts. Then `U = Uref + Uinput + Ustatic + Uunsafe`, with
`Uref` bounded by the finite `ReferenceOrigin` inventory, `Uinput` bounded by
the function input count, and both `Ustatic` and `Uunsafe` bounded by the closed
raw-producing MIR event inventory.

A reference-to-raw conversion creates one carrier with the complete non-empty
set of reaching `Reference` origins.
A raw input and a static address create `RawInput` and `StaticAddress` roots.
An acknowledged unsafe operation that produces an address without a raw or
reference predecessor creates an `UnsafeAddress` root tied to that boundary.
Copy, move, mutable-to-const conversion, reinterpretation, and assignment
create one destination carrier whose predecessors are every reaching input
carrier on every reaching path. A CFG join creates no synthetic carrier; the
point state retains every reaching carrier for the destination place.

The carrier graph may contain cycles and self-edges because a static copy or
assignment event can execute repeatedly in a loop. Raw provenance is the least
solution of these equations over the finite carrier graph:

```text
Origins(root) = Seed(root)
Origins(derived) = union(Origins(predecessor)
                         for predecessor in derived.predecessors)
```

`Seed(root)` is the complete reaching reference-origin set for a
reference-conversion root and the singleton event origin for every other root.

Point carrier sets, predecessor sets, and origin sets are solved
simultaneously from the root seeds. Every encoded carrier must be reachable
from at least one root; therefore every encoded `origins` sequence is non-empty.
For an SCC, origins entering through any predecessor propagate to every member
until no membership changes. The verifier independently recomputes the complete
predecessor relation, root reachability, SCC decomposition, point carrier sets,
and least origin closure. It rejects a greatest, manually seeded, truncated, or
otherwise non-least solution even if every local union equation appears closed.
Termination follows because at most `Araw * Araw` predecessor memberships,
`P * M * Araw` point-carrier memberships, and `Araw * U` origin memberships can
be added.

Only `Reference` origins impose a region-containment requirement on return,
store or closure escape. The other origins remain
explicit because an unsafe dereference must not be mistaken for a safe loan or
for proven validity. Raw dereference and unsafe acknowledgement never introduce
a `ReferenceDefinition`, `ReferenceOrigin`, loan, or region; their result remains
owned or raw unless another independently checked safe operation supplies a
reference. A required boundary with no acknowledgement selects
`RawPointerBoundaryRequiresUnsafe`. Acknowledgement grants only that checked
operation; it does not suppress safe move, borrow, reference escape, or linear
checks. Missing, additional, foreign-owner, non-root-reachable,
origin-losing, origin-fabricating, or non-least carrier records are
`InvalidOwnershipProof`.

### Closed Ownership Source Failure Algebra

The source algebra is:

```text
MoveFailureCause {
  move: MoveCause,
  span: SourceSpan,
}

InitializationFailureCause {
  cause: InitializationLossCause,
  span: SourceSpan,
}

LoanFailureCause {
  loan: LoanKey,
  source: MovePathKey,
  span: SourceSpan,
}

LinearConsumptionCause {
  consumption: LinearConsumption,
  span: SourceSpan,
}

EscapeFailureCause {
  cause: EscapeOriginCause,
  span: SourceSpan,
}

OwnershipSourceFailure =
    UseAfterMove {
      owner: DefId, primary: MirEventKey, primarySpan: SourceSpan,
      place: MovePathKey,
      moves: SortedNonEmptySequence<MoveFailureCause>,
      traversalOrdinal: uint32,
    }
  | MutableBorrowConflict {
      owner: DefId, primary: MirEventKey, primarySpan: SourceSpan,
      place: MovePathKey,
      conflictingLoans: SortedNonEmptySequence<LoanFailureCause>,
      traversalOrdinal: uint32,
    }
  | UninitializedPlaceUse {
      owner: DefId, primary: MirEventKey, primarySpan: SourceSpan,
      place: MovePathKey,
      causes: SortedNonEmptySequence<InitializationFailureCause>,
      traversalOrdinal: uint32,
    }
  | SharedBorrowConflict {
      owner: DefId, primary: MirEventKey, primarySpan: SourceSpan,
      place: MovePathKey,
      conflictingLoans: SortedNonEmptySequence<LoanFailureCause>,
      traversalOrdinal: uint32,
    }
  | BorrowDoesNotLiveLongEnough {
      owner: DefId, primary: MirEventKey, primarySpan: SourceSpan,
      place: MovePathKey,
      origins: SortedNonEmptySequence<EscapeFailureCause>,
      traversalOrdinal: uint32,
    }
  | LinearNotConsumed {
      owner: DefId, primary: MirEventKey, primarySpan: SourceSpan,
      obligation: LinearObligationKey,
      initializationSpan: SourceSpan, traversalOrdinal: uint32,
    }
  | LinearConsumedTwice {
      owner: DefId, primary: MirEventKey, primarySpan: SourceSpan,
      obligation: LinearObligationKey,
      firstConsumptions:
          SortedNonEmptySequence<LinearConsumptionCause>,
      traversalOrdinal: uint32,
    }
  | RawPointerBoundaryRequiresUnsafe {
      owner: DefId, primary: MirEventKey, primarySpan: SourceSpan,
      boundary: UnsafeBoundaryKey,
      traversalOrdinal: uint32,
    }
  | MoveOutOfBorrow {
      owner: DefId, primary: MirEventKey, primarySpan: SourceSpan,
      place: MovePathKey,
      conflictingLoans: SortedNonEmptySequence<LoanFailureCause>,
      traversalOrdinal: uint32,
    }
```

Variant tags are `0x01` through `0x09` in declaration order. Every identity,
place, point, and span is validated before construction. Source failures encode
their tag and fields in declaration order using RFC 0011 canonical identity and
span encodings. Cause-record fields encode in declaration order. Move causes
order by move event, moved path, then span; initialization causes by complete
loss-cause bytes then span; loan causes by loan key, source, then span;
consumption causes by consumption then span; and escape causes by complete
origin-cause bytes then span. Cause sequences are sorted, unique, non-empty,
and contain every reaching cause that can make the primary event illegal. Each
move span must equal the source-map span for
`move.event`; each loan source must equal the referenced `LoanFact.source` and
its span must equal the source-map span for `loan.issue`; each consumption span
must equal the source-map span for `consumption.event`; each initialization span
must equal its loss-cause event; and each escape span must equal
`cause.origin.activation`. No free-form text enters a failure.
For `RawPointerBoundaryRequiresUnsafe`, `primary` must equal
`boundary.event`, `primarySpan` must equal the associated
`MirUnsafeOccurrence.sourceSpan`, and the complete boundary key participates in
failure ordering and deduplication. Distinct unacknowledged unsafe occurrences
on one place event therefore produce distinct failures.

The required mapping is exact. The registry transaction adds only
`ZOM4093-ZOM4095` and deletes the stale, unemitted `ZOM4067
ScopedTaskBorrowEscapes` and `ZOM4068 ScopedTaskReferentHere` rows plus every
associated emitter or reservation. Numeric IDs 4067 and 4068 are never
reassigned. `ZOM4095 ConcurrencySemanticsUnavailable` belongs only to the
pre-checker admission result above; no ownership failure below produces it:

| Failure | Primary diagnostic | Secondary diagnostic |
|---|---|---|
| `UseAfterMove` | `ZOM4056 UseAfterMove`, Error, `Use of moved value`, arity 0 | `ZOM4057 ValueMovedHere`, Note, `Value moved here`, arity 0 |
| `MutableBorrowConflict` | `ZOM4058 MutableBorrowConflicts`, Error, `Mutable borrow conflicts with an active borrow`, arity 0 | `ZOM4060 BorrowOriginHere`, Note, `Conflicting borrow starts here`, arity 0 |
| `UninitializedPlaceUse` | `ZOM4093 UninitializedPlaceUse`, Error, `Use of uninitialized or deinitialized place`, arity 0 | `ZOM4094 PlaceBecameUnavailableHere`, Note, `Place became unavailable here`, arity 0 |
| `SharedBorrowConflict` | `ZOM4059 SharedBorrowConflicts`, Error, `Shared borrow conflicts with an active mutable borrow`, arity 0 | `ZOM4060 BorrowOriginHere`, Note, `Conflicting borrow starts here`, arity 0 |
| `BorrowDoesNotLiveLongEnough` | `ZOM4061 BorrowDoesNotLiveLongEnough`, Error, `Borrowed value does not live long enough`, arity 0 | `ZOM4062 BorrowReferentHere`, Note, `Borrowed value is created here`, arity 0 |
| `LinearNotConsumed` | `ZOM4063 LinearNotConsumed`, Error, `Linear value is not consumed on all normal paths`, arity 0 | `ZOM4064 LinearInitializedHere`, Note, `Linear value is initialized here`, arity 0 |
| `LinearConsumedTwice` | `ZOM4065 LinearConsumedTwice`, Error, `Linear value is consumed more than once`, arity 0 | `ZOM4066 LinearFirstConsumedHere`, Note, `Linear value was first consumed here`, arity 0 |
| `RawPointerBoundaryRequiresUnsafe` | `ZOM4069 RawPointerBoundaryRequiresUnsafe`, Error, `Raw pointer safe boundary requires unsafe acknowledgement`, arity 0 | None |
| `MoveOutOfBorrow` | `ZOM4070 MoveOutOfBorrow`, Error, `Cannot move a value while it is borrowed`, arity 0 | `ZOM4060 BorrowOriginHere`, Note, `Conflicting borrow starts here`, arity 0 |

`UseAfterMove` emits one `ZOM4057` note for every retained move cause.
`UninitializedPlaceUse` emits one `ZOM4094` note for every retained loss cause,
including move causes from other predecessors.
Borrow-conflict and move-out failures emit one `ZOM4060` note for every retained
loan cause. `LinearConsumedTwice` emits one `ZOM4066` note for every retained
first-consumption cause. Notes follow their primary in canonical cause order;
no cause is omitted merely because another cause has a lower span or key.

`BorrowDoesNotLiveLongEnough` emits one `ZOM4062` note for every retained
escape origin. Direct and raw-carried routes remain separate notes when both
reach the same region. The primary is emitted once per escape operand; origin
notes follow in canonical `EscapeFailureCause` order.

Reborrow escape, returned-reference escape, stored-reference escape, closure
escape, checked-cast escape, and raw-provenance escape all select
`BorrowDoesNotLiveLongEnough`. A write through an unavailable
exclusive path selects `MutableBorrowConflict`. A logical drop while borrowed
selects `MoveOutOfBorrow`.

### Source Suppression, Precedence, And Ordering

Analysis computes all independent failures. To avoid cascades, event-local
transition rules are exact:

1. an invalid identity or verified input produces an invariant branch and no
   source analysis;
2. a rejected unavailable-place use emits `UseAfterMove` only when every loss
   cause is `Moved`; otherwise it emits `UninitializedPlaceUse` with every loss
   cause and suppresses `UseAfterMove`. Either result performs no derived
   borrow, move, or linear action;
3. for a `Positive` `Linear` obligation, a second consumption emits
   `LinearConsumedTwice` with every sorted reaching first-consumption cause and
   suppresses `UseAfterMove` at that same event;
4. a move or drop blocked by a live loan emits `MoveOutOfBorrow` and does not
   move, drop, or consume the place; the failure retains every overlapping
   reserved or active loan that blocks any reaching alternative;
5. a rejected borrow creation emits exactly one mutable or shared conflict and
   issues no loan; its cause relation contains every overlapping loan that
   conflicts on any reaching alternative;
6. a rejected return, store, closure capture, or checked-cast escape emits one
   region primary with every sorted invalid origin and does not extend
   provenance into the destination;
7. each missing unsafe acknowledgement emits the failure for its complete
   boundary key but does not suppress another unsafe occurrence or independent
   safe ownership failures; and
8. `LinearNotConsumed` is emitted once per obligation and primary normal exit
   or rejected reintroduction after all point transitions reach the fixed
   point.

Cause completeness is evaluated before diagnostic suppression. The optimized
producer and independent verifier must compute byte-identical loss-cause,
conflicting-loan, first-consumption, and escape-origin sets.
Presentation never selects one canonical winner from a multi-cause relation.
Mutating only a secondary cause therefore changes the failure bytes and must be
detected even when the primary diagnostic code and span remain unchanged.

Failures deduplicate by complete canonical bytes. Global order is RFC 0011
package key, crate key, module key, validated primary span, numeric primary
diagnostic ID, MIR schema traversal ordinal, expanded owner key, primary
`MirEventKey`, variant tag, and remaining complete payload. Notes are emitted
immediately after their primary and are not separately sorted. Worker arrival,
hash iteration, or source spelling is never a tie breaker.

### Verified Ownership Facts

Successful analysis publishes:

```text
VerifiedOwnershipFacts {
  semanticContext: SemanticContextBrand,
  contextFingerprint: ContextFingerprint,
  module: ModuleId,
  builtRevision: MirRevisionId,
  eventOverlayRevision: OwnershipEventOverlayRevision,
  borrowEvidenceRevision: BorrowEvidenceRevision,
  functions: SortedMap<DefId, OwnershipFunctionFacts>,
  revision: OwnershipFactsRevision,
}

OwnershipFunctionFacts {
  owner: DefId,
  movePaths: SortedMap<MovePathKey, MovePathFact>,
  conflicts: SortedUniqueSequence<MovePathPair>,
  pointStates: SortedMap<OwnershipPoint, OwnershipPointState>,
  loans: SortedMap<LoanKey, LoanFact>,
  regions: SortedMap<RegionKey, RegionFact>,
  dropObligations:
      SortedMap<DropObligationKey, DropObligationFact>,
  linearObligations:
      SortedMap<LinearObligationKey, LinearObligationFact>,
  linearCarriers:
      SortedMap<LinearCarrierKey, LinearCarrierFact>,
  castCarriers:
      SortedMap<CastCarrierKey, CastCarrierFact>,
  rawOrigins: RawOriginUniverse,
  rawProvenance:
      SortedMap<RawProvenanceCarrierKey, RawProvenanceFact>,
  escapes: SortedMap<EscapeFactKey, VerifiedEscapeFact>,
  unsafeBoundaries:
      SortedMap<UnsafeBoundaryKey, VerifiedUnsafeBoundaryFact>,
}

OwnershipPointState {
  point: OwnershipPoint,
  initialization:
      SortedMap<MovePathKey, InitializationPointFact>,
  references: SortedMap<MovePathKey, ReferencePointFact>,
  loans: SortedMap<LoanKey, LoanPointState>,
  resources: OwnershipResourcePointState,
  rawCarriers:
      SortedMap<MovePathKey,
                SortedNonEmptySequence<RawProvenanceCarrierKey>>,
}
```

`OwnershipPointState` fields encode in declaration order. Its map key must equal
`point`, and the map contains every reachable CFG and event cutpoint defined by
the closed slot projection. `initialization` contains every move path, including dead paths. Its
map key must equal `InitializationPointFact.path`. `references` contains every
reference-typed place whose initialization fact has the initialized bit and at
least one reaching definition, and its map key must equal
`ReferencePointFact.path`; initialized-predecessor definitions remain present
at maybe-initialized joins. Places with no initialized bit and non-reference
places have no row. `loans` contains exactly the loans whose regions contain
the point; each key must equal
`LoanPointState.loan` and its phase sequence records every reachable reserved,
active, or suspended alternative. `resources` contains every drop obligation with
an absent, pending, or discharged state and every linear obligation with an
absent, pending, or consumed state, plus every cast-carrier phase, inside the
same complete relational alternative. `rawCarriers` contains exactly each live raw-
pointer place and every reaching carrier for that place. Empty raw-carrier
values do not encode.

`OwnershipFunctionFacts` deliberately contains no second marker-use or deferred-
activation map. Ownership analysis consumes both authoritative inventories from
its verified event-overlay input, and
`VerifiedOwnershipFacts.eventOverlayRevision` binds the resulting facts to that
exact handoff. The facts producer and verifier must reject a different overlay
revision before reading any marker decision or activation row; they do not copy,
reconstruct, or normalize either inventory into duplicate facts fields.

Point-state maps order by their structural keys. Every nested value sequence
orders by the canonical rules of its element type. Duplicate map keys,
key/value disagreement, a missing complete-inventory row, an additional row,
or an out-of-order record is `InvalidOwnershipProof`; the decoder never
normalizes it.

The same key/value equality is mandatory for every function map:
`movePaths.key`, `loans.key`, `regions.key`,
`dropObligations.key`, `linearObligations.key`, `linearCarriers.key`,
`castCarriers.key`, `rawProvenance.key`, `escapes.key`, and
`unsafeBoundaries.key` must equal their enclosing map key.
`rawOrigins` must equal the complete root-origin universe. `conflicts` contains
the complete distinct lower-key-first pair inventory and no reflexive pair.
Every record owner must equal the function owner through all nested locations,
places, loans, regions, scopes, carriers, and causes. Cross-function or cross-
module references are invalid except for expanded semantic types and
definitions explicitly authorized by the verified input lineage.

Every map key is a MIR function, block, statement, local, place, or event
identity, or a structural key rooted in one of those identities. No fact is
keyed by AST `NodeId`, HIR ID, source spelling, object address, store slot,
worker number, or traversal pointer. Semantic types and definitions occur only
as validated values and encode by their expanded canonical keys.

Facts contain the complete state needed by cleanup elaboration. They do not
contain target layout, ABI, runtime symbol, or LLVM data.

### Ownership Facts Canonical Codec And Revision

All unsigned integers encode big-endian. `bool` and closed-union tags are one
byte. Byte strings use RFC 0011 `Frame(bytes) = uint64be(length) || bytes`.
Records concatenate field encodings in declaration order. A sequence encodes
`uint64be(count)` followed by `Frame(Encode(element))` for each item. A map
encodes `uint64be(count)` followed by
`Frame(Encode(key)) || Frame(Encode(value))` for each entry. Closed unions
encode the declared one-byte tag followed by variant fields in declaration
order. `Maybe<T>` encodes `0x00` or `0x01 || Encode(T)`. Sorted containers
compare complete unframed canonical element or key bytes, require strict byte
increase, and reject duplicates or out-of-order input. Semantic identities and
types expand through the exact live registries and type store; local numeric
handle slots do not encode.

Each `OwnershipFunctionFacts` record encodes these fields in order:

```text
Frame(expanded owner DefId key)
EncodeSortedMap(movePaths)
EncodeSortedSequence(conflicts)
EncodeSortedMap(pointStates)
EncodeSortedMap(loans)
EncodeSortedMap(regions)
EncodeSortedMap(dropObligations)
EncodeSortedMap(linearObligations)
EncodeSortedMap(linearCarriers)
EncodeSortedMap(castCarriers)
EncodeSortedSequence(rawOrigins)
EncodeSortedMap(rawProvenance)
EncodeSortedMap(escapes)
EncodeSortedMap(unsafeBoundaries)
```

`OwnershipFactsRevision` is SHA-256 over:

```text
ASCII("zom.ownership-facts")
0x00
ContextFingerprint
Frame(Encode(expanded owning ModuleKey))
Encode(Built MirRevisionId)
OwnershipEventOverlayRevision
BorrowEvidenceRevision
EncodeFramedSequence(OwnershipFunctionFacts in expanded DefId order)
```

The canonical domain is `zom.ownership-facts`. This RFC is `IMPLEMENTING`; the
complete ownership-facts artifact remains gated. The schema contains no
duplicate activation map. Every type named in the facts schema has
its tags, field order, and sort order fixed in the section that defines it. A
schema change directly replaces this contract and its independent vectors.

The empty-function oracle uses a zero context fingerprint, module bytes `a1`,
32 MIR revision digest bytes `22`, event-overlay bytes `55`,
borrow-evidence bytes `33`, and zero functions. Its complete 165-byte preimage
is shown below. The `55` bytes represent an
`OwnershipEventOverlayRevision` computed under the required
`zom.ownership-event-overlay` domain; the ownership facts field width and bytes are
otherwise unchanged because this codec binds the overlay revision as an opaque
digest rather than duplicating overlay inventories.

```text
7a6f6d2e6f776e6572736869702d66616374730000000000000000000000000000000000000000000000000000000000000000000000000000000001a12222222222222222222222222222222222222222222222222222222222222222555555555555555555555555555555555555555555555555555555555555555533333333333333333333333333333333333333333333333333333333333333330000000000000000
```

Its SHA-256 is
`3fc9636b5e668ec6b10fac4ce2c76b0a20d87f9b25dd51b7729141ed33296b93`.

The function-framing oracle uses the same parents and one 113-byte function
record with owner bytes `b1` and thirteen empty record groups in the exact
order above. Its complete 286-byte preimage is:

```text
7a6f6d2e6f776e6572736869702d66616374730000000000000000000000000000000000000000000000000000000000000000000000000000000001a1222222222222222222222222222222222222222222222222222222222222222255555555555555555555555555555555555555555555555555555555555555553333333333333333333333333333333333333333333333333333333333333333000000000000000100000000000000710000000000000001b10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
```

Its SHA-256 is
`b2d0e68fd7a597ddabb36e3d2c78cf96a596561921fb965e856fdd26363bfba2`.

The exact non-empty point-state oracle uses the same parents and owner. It
encodes one `pointStates` map entry whose key and value point are
`OwnershipPoint::Cfg(MirPoint::Entry)` (`01 01`), empty initialization,
reference, loan, and raw-carrier maps, and one `OwnershipResourceStateAlternative` with
empty drop-obligation, linear-obligation, and cast-carrier state maps. All other function
groups are empty. This is a codec-valid component oracle; the semantic proof
verifier independently requires the complete point inventory for its Built MIR
fixture. Its complete 378-byte preimage is:

```text
7a6f6d2e6f776e6572736869702d66616374730000000000000000000000000000000000000000000000000000000000000000000000000000000001a1222222222222222222222222222222222222222222222222222222222222222255555555555555555555555555555555555555555555555555555555555555553333333333333333333333333333333333333333333333333333333333333333000000000000000100000000000000cd0000000000000001b100000000000000000000000000000000000000000000000100000000000000020101000000000000004a01010000000000000000000000000000000000000000000000000000000000000001000000000000001800000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
```

Its SHA-256 is
`f5acb38c2474aea982f75216e69997d0aa8d2634eebdabc7fc6d59e01a2d6577`.

Additional non-empty semantic fixtures encode every ownership-point tag, event stage and
role, unsafe-scope enter/exit operation and scope payload, causal cutpoint state,
complete loss-cause, conflict, loan, region-closure, reference-definition/origin/NLL,
multi-predecessor linear-carrier transition, raw-origin-universe, raw-carrier
SCC closure, escape-origin, and unsafe record.
A test-owned encoder reproduces all three exact vectors above without calling
the production codec. A lineage composition oracle separately computes the
actual event overlay revision for each marker-decision fixture, places that digest
in `eventOverlayRevision`, and proves that changing a decision tag, proof,
explicit negative fact, unsatisfied state, marker key, or policy/coherence
lineage changes both the overlay revision and the enclosing facts revision.
The fixed `55` vectors isolate ownership facts framing and therefore remain exact.

### Independent Proof Oracle

The producer and verifier are separate implementations:

- the event-overlay producer and verifier receive the same
  `OwnershipEventOverlayInput` while its exact RFC 0005 `BodyCheckingInput` and
  admitted checked module remain live. Each constructs a distinct RFC 0015
  `MarkerProofInput` and query context with an empty private active stack and
  completed-result memo, independently derives the complete phase-one resource
  projection tree, marker-use query inventory, and checked receiver activation
  projection, then independently performs the phase-two postorder fold. Neither
  reads the other's memo, in-flight
  state, traversal, query set, candidate plan, or fold decisions. They may
  share only the immutable authoritative `BodyCheckingInput`, RFC 0015's
  linearizable semantic interner, immutable checked facts, generated algebra
  definitions, canonical primitive encoders, and SHA-256;
- the producer computes candidates using the optimized bitset worklist;
- the verifier reconstructs the closed causal event-slot inventory, every
  source/effect/commit stage and CFG/before-event/after-event cutpoint, move
  paths, conflicts, three-bit initialization, all loss causes, reaching reference
  definitions, multi-root/active origins, NLL kills and last-use regions,
  checked-cast carrier initialization, branch transfer, failure-edge logical
  drop, loan phases, deferred-activation overlay bijection,
  suspending children, region closure, complete linear
  alternatives, multi-predecessor carrier transitions and loop fixed points,
  partial-initialization drop obligations, the complete descendant `Copy` and
  `Linear` query tree, action boundaries, immediate-field `Copy + Linear`
  validity, postorder maximal-component folding, structural-cleanup selection,
  open/closed plan application,
  `DropResourceSubject` preservation, cast-route application, mutation,
  overwrite, and `StorageDead` transitions,
  the raw origin universe and least SCC carrier provenance, complete escape
  origins, collision-free overlay-only unsafe occurrences, Built MIR unsafe-
  scope entry dominance and complete exit cut sets, unsafe boundaries, and
  source-free proof records from Built MIR, the verified ownership event
  overlay, and capability-resolved evidence;
- the ownership producer and verifier consume the same immutable verified
  overlay plan but use separate transfer functions; neither may reopen checker
  signatures or derive a receiver adjustment, deferred-activation row,
  deinitializer, marker, dyn payload route, or cast resource plan;
- the verifier uses its own transfer functions, worklist storage, canonical
  record writer, and revision computation;
- the two implementations may share only immutable input types, RFC 0011
  primitive identity encoding, SHA-256, and generated enum definitions; and
- the verifier compares complete canonical records before constructing the
  private verified wrapper.

For source rejection, the verifier also recomputes every complete multi-cause
failure payload from the verified input. Producer/verifier equality includes
secondary initialization, move, loan, consumption, and escape causes before
suppression and sorting; matching only primary codes or spans is insufficient.

The deterministic test oracle uses exhaustive state exploration for bounded
CFGs and compares its reachable-state union with both implementations. A text
dump or producer-computed hash is never an oracle.

### Ownership Analysis Result And Failure Precedence

The result remains the fixed RFC 0013 algebra:

```text
OwnershipAnalysisResult =
    Verified { facts: VerifiedOwnershipFacts }
  | SourceRejected {
      failures: SortedNonEmptySequence<OwnershipSourceFailure>,
    }
  | IdentityInvariantRejected {
      failures: SortedNonEmptySequence<RFC0011::IdentityInvariant>,
    }
  | IrInvariantRejected {
      failures: SortedNonEmptySequence<RFC0010::IrFailureFact>,
    }
```

Tags are `Verified = 0x01`, `SourceRejected = 0x02`,
`IdentityInvariantRejected = 0x03`, and `IrInvariantRejected = 0x04`.
No branch contains a value owned by another branch. There is no capability,
partial-facts, recovered, or unknown branch.

The validation order is exact:

1. validate semantic identities, source ranges, and context brands;
2. validate repository capability liveness and brand, the canonical Built MIR
   artifact and revision,
   event-overlay domain/revision, complete marker-use inventory and
   Built/checked association,
   evidence lease and revision, wrapper lineage, and required parent revisions;
3. return identity failures before any invalid handle is dereferenced;
4. select RFC 0010 `InputRevisionMismatch`;
5. select `CanonicalCodecMismatch`;
6. select the remaining legal input `MissingRequiredFact`, `AdditionalFact`,
   `InvalidFact`, or `InvalidPlace` failure;
7. run source analysis over completely verified input;
8. if any source failure exists, return `SourceRejected` and construct no
   ownership candidate;
9. otherwise construct the complete candidate and run the independent proof
   verifier; and
10. select `MissingRequiredFact`, `AdditionalFact`, `InvalidFact`,
    `InvalidPlace`, `InvalidOwnershipProof`, or
    `CanonicalCodecMismatch` for candidate failure, or publish exactly one
    verified value.

Every IR failure is legal only at RFC 0010 `OwnershipProofValidation`, has a
`Definition` owner and `None` or `Mir` site, and has `IrFailureDetail::None`.
All non-codec IR failures map to `ZOM9945 OwnershipProofInvariant`.
`CanonicalCodecMismatch` maps to `ZOM9949 IrCanonicalCodecMismatch`. Identity
failures retain RFC 0011 `ZOM9910-ZOM9921` mappings.

### OwnershipCheckedMir Construction

`VerifiedOwnershipFacts` has a private constructor available only to the
independent verifier. `OwnershipCheckedMir` and every successor wrapper also
have private constructors. After the pre-checker admission and event-overlay
operations defined above, the public ownership-analysis and successor surface
is exactly:

```text
analyzeOwnership(
  built: Borrowed<const RFC0013::VerifiedBuiltMir>,
  overlay: Borrowed<const VerifiedOwnershipEventOverlay>,
  evidence: Borrowed<const RFC0013::VerifiedBorrowEvidenceLease>,
  repository: Borrowed<const BorrowEvidenceRepositoryCapability>,
) -> OwnershipAnalysisResult

finalizeOwnership(
  built: Moved<RFC0013::VerifiedBuiltMir>,
  overlay: Moved<VerifiedOwnershipEventOverlay>,
  facts: Moved<VerifiedOwnershipFacts>,
  repository: Borrowed<const BorrowEvidenceRepositoryCapability>,
) -> RFC0010::IrOperationResult<OwnershipCheckedMir>

elaborateDrops(
  input: Moved<OwnershipCheckedMir>,
  repository: Borrowed<const BorrowEvidenceRepositoryCapability>,
) -> RFC0010::IrOperationResult<RFC0013::DropElaboratedMir>

elaborateCoroutines(
  input: Moved<RFC0013::DropElaboratedMir>,
  repository: Borrowed<const BorrowEvidenceRepositoryCapability>,
) -> RFC0010::IrOperationResult<RFC0013::CoroutineElaboratedMir>

verifyExecutableMir(
  input: Moved<RFC0013::CoroutineElaboratedMir>,
  repository: Borrowed<const BorrowEvidenceRepositoryCapability>,
) -> RFC0010::IrOperationResult<RFC0013::VerifiedExecutableMir>
```

`Borrowed<const T>` is a non-owning call-duration reference that cannot be
stored in a result. `Moved<T>` consumes one move-only value. Each
`IrOperationResult<T>` has the exact RFC 0010 `Verified { value: T }`,
`CapabilityRejected`, `IdentityInvariantRejected`, and `IrInvariantRejected`
branches; `CapabilityRejected` is structurally present but illegal for these
pre-target phases and is never selected. No branch returns the consumed
predecessor. `analyzeOwnership` borrows Built MIR, so a
source-rejected module remains available to the driver for diagnostics but
cannot satisfy `finalizeOwnership`. `finalizeOwnership` is callable only with
the private-constructor `VerifiedOwnershipFacts` carried by the `Verified`
branch; it consumes Built MIR, event overlay, and facts and either publishes
one wrapper or publishes no value. There is no generic constructor template, public aggregate
initializer, facts-only successor, or overload omitting the capability.

Construction rechecks, without dereferencing unvalidated handles:

- semantic context brand and fingerprint;
- module identity;
- canonical Built MIR artifact and exact `MirRevisionId`;
- exact `OwnershipEventOverlayRevision`, checked-facts revision, and complete
  overlay revision recomputation;
- wrapper, facts, and resolved lease `BorrowEvidenceRevision` equality;
- facts revision recomputation;
- byte equality between the analyzed lease and
  `VerifiedBuiltMir.borrowEvidenceLease`; and
- successful resolution of that lease through the explicit capability against
  the live session-owned RFC 0013 `BorrowEvidenceRepository`, including
  semantic-context brand, repository `RegistryBrand`, module, revision key,
  session epoch, and pre-teardown lifetime.

The constructor stores the Built MIR, verified event overlay,
`builtRevision`, `eventOverlayRevision`, `borrowEvidenceRevision`, and facts.
This is RFC 0007's explicit extension of the RFC 0013 wrapper field set; it does
not alter MIR. The constructor exposes no
overload accepting a bare module, digest, caller-built fact map, or different
lease. Ownership facts are moved values owned directly by
`OwnershipCheckedMir`; there is no ownership-fact repository or ownership
lease. The constructor does not retain a repository pointer or capability in
the encoded or runtime wrapper. Every successor operation directly receives a
live capability again, resolves the embedded lease before inspecting or moving
the predecessor payload, and then commits the moved successor atomically. A
rejected operation destroys its consumed local input and returns no predecessor
or partial successor; CompilerSession retains its previously published module
transaction until the returned `Verified` value is committed. Thus wrapper
destruction never depends on repository
destruction order and no hidden lookup exists. A foreign, missing, stale,
swapped, or post-teardown lease or capability selects RFC 0010
`InputRevisionMismatch` before candidate construction. The verified result of
`analyzeOwnership`, bound to the exact event-overlay revision, is the only
production token that can satisfy construction.
A rejected branch constructs neither
`OwnershipCheckedMir` nor any drop-, coroutine-, executable-MIR, LIR, or backend
successor.

### Deterministic Parallelism

Module-level validation and evidence resolution run once before workers start.
Functions are then independent units keyed by expanded `DefId`. Each worker
uses function-local move paths, points, worklists, loans, regions, and
obligations. It returns a complete candidate or complete source failures.

The coordinator waits for every function, sorts candidates and failures by
canonical key, and publishes only after the module verifier succeeds. It never
stops at the first worker failure. Worker counts `1`, `2`, `4`, and `8`, reverse
function/block insertion, fixed map-seed permutations, and repeated processes
must produce byte-identical failures, facts, revisions, and dumps.

### Operational Budgets And Termination

Let `X` be reachable ownership event slots, `Pcfg` reachable MIR CFG points,
`P = Pcfg + 2 * X` reachable `OwnershipPoint` cutpoints, `E` CFG edges, `M`
move paths, `D` distinct initialization-loss causes, `L` loans, `R` regions,
`B` drop obligations, `Q` linear obligations, `Ccast` checked-cast carriers,
`Ab` the verified place-carrier count for drop obligation `b`, `Cb` its
discharge count,
`Aq` the verified carrier count derived from one root plus reachable static
transfer keys and `Cq` the consumption count for obligation `q`, `Araw` raw
provenance carriers, `U` raw origins, `V` reference-value definitions, and `O`
reference origins in one function. Define the
finite complete resource-alternative bound
`K = 4^Ccast * product(b in B, 1 + Ab + Ab * Cb) *
product(q in Q, 1 + Aq + Aq * Cq)`. `K` is a mathematical natural number,
represented by an arbitrary-precision integer or the exact symbolic factor
vector; it is never converted to `size_t`, used to reserve a container, or
evaluated with fixed-width multiplication. All operands come from verified
finite inventories. The algorithms are monotone:

- initialization adds at most `3 * P * M` state bits and `P * M * D`
  loss-cause memberships;
- loan propagation adds at most `3 * P * L` phase bits and `P * L * L`
  suspending-child memberships, plus `L * O` source-origin and `L * L` parent
  memberships;
- reference propagation adds at most `P * M * V` reaching-definition
  memberships and `P * M * V * O` origin memberships;
- region solving adds at most `P * R + P * V + R * R` general, explicit
  value-liveness, and outlives-closure memberships;
- joint drop/linear propagation adds at most `P * K` complete-alternative
  memberships, including cast-carrier phases, and drop transitions add at most
  `sum(Ab * Ab)` memberships,
  while the carrier graphs add at most `sum(Aq * Aq)` incoming-transition
  memberships, including loop SCC and self-edge transitions;
- raw provenance adds at most `P * M * Araw` reaching-carrier memberships,
  `Araw * Araw` predecessor memberships, and `Araw * U` origin memberships;
- place-conflict construction examines at most `M * (M - 1) / 2` pairs.

The implementation maintains arbitrary-precision counters or component-wise
inventory counters for these derived bounds and
enqueues a successor only when a state bit, complete alternative, transition,
predecessor, or origin membership changes. A counter exceeding its mathematical
bound is `InvalidOwnershipProof`, because it proves a non-monotone transfer,
duplicate queue admission, or corrupted inventory. Bound arithmetic cannot
overflow. Worklists and fact containers grow only for an actual newly inserted
membership; the solver never allocates the Cartesian bound.

RFC 0013 has no capability-rejection branch for this operation. Therefore an
arbitrary source-size cutoff cannot change ownership semantics. Compilation
cancellation or host resource exhaustion may stop the whole driver transaction
and publishes no result; neither is an ownership-analysis branch and neither
may be relabelled `InvalidOwnershipProof` for a valid input. CI records peak
`X`, `P*M*D`, `P*L`, `L*O`, `L*L`, `P*M*V`, `P*M*V*O`, `P*V`, `P*R`,
`P*K`, `sum(Ab*Ab)`, `sum(Aq*Aq)`,
`P*M*Araw`, `Araw*Araw`, `Araw*U`, worklist pushes, and proof bytes for
regression monitoring.

### Mutation And Negative Proof Matrix

Starting from one complete valid multi-block fixture, tests independently
mutate:

- context brand, fingerprint, module, function owner, Built MIR digest, checked
  revision, dispatch revision, evidence lease, evidence
  revision, repository capability, repository brand, session epoch, and lease
  lifetime;
- every MIR-point and ownership-point tag, `BeforeEvent`/`AfterEvent` key,
  block, statement ordinal, edge ordinal, event location, operand-ordinal gap
  and range, source/effect/commit stage, effect position, operation slot, role
  tag/set/order, closed causal event order, constant role, destination commit,
  `SetDiscriminant`, variant-switch discriminant, panic payload, canonical
  borrow form, every `MirCheckedCast` mode/kind/input/branch/result commit,
  every marker-use event/marker/subject key, policy revision, coherence
  revision, decision tag, positive proof, explicit negative fact, payload-free
  unsatisfied decision, and key/value agreement; every logical-drop-plan
  key/root/component/action/deinitializer/decision reference,
  cast-plan key and carrier/success-plan reference, route proof and bijection,
  source/result drop-plan mismatch, resource-subject introduction/origin/type,
  current optional action/type, subject preservation, unmatched
  nontrivial source or result component, source-only linear component,
  result-only linear component, bare-location collision, same-call operand
  swap, successor, source scope, unsafe-scope outer tag, inner boundary kind,
  scope ordinal, boundary payload framing, event source span, and cross-event
  span swap;
- local root, root type, each projection tag and payload, projection order,
  projection input/result type, move-path parent, distinct conflict pair,
  reflexive self-pair, and reversed pair;
- every initialization tag and may-bit meaning, dead/live join, point-state
  key/value agreement, each loss-cause tag and payload, mixed move/non-move
  cause order, cause completeness, cause/source-map span, and the
  `UseAfterMove` versus `UninitializedPlaceUse` selection;
- loan kind, source, destination, issue-before-source, issue-after-commit,
  deferred activation before the last call source or after the destination
  commit, overlay activation-fact key, missing/additional/swapped independently
  rebuilt activation row, receiver source, receiver mode, adjustment source,
  destination, and steps, ownership-side checker lookup, explicit-borrow
  misclassification, complete source origins, parent set, region, every point phase, phase
  order, suspending child, issue and activation cutpoint inclusion, reservation,
  activation, suspension, and restoration;
- region origin, CFG or event live point, missing before/after cutpoint,
  primitive outlives edge, orientation, reflexive row, transitive row, strongly
  connected component, `Storage`, `LocalValue`, and `ClosureValue` key,
  last-use boundary, overwrite kill,
  wrong value destination, reaching reference definition, generation
  merge/split, multi-origin root/active copy/move/join/reborrow transfer,
  input-reborrow return, call summary, return relation, store, closure capture,
  and closure copy/move/overwrite;
- marker role, subject, decision tag, marker-policy revision, coherence
  revision, positive-proof key/polarity/evidence, explicit-negative-fact
  key/polarity/explicit evidence, forbidden structural, builtin, or
  policy-subject negative,
  forbidden fact payload on `Unsatisfied`, `ExplicitNegative`/`Unsatisfied`
  collapse, omitted or additional canonical query, copy use, linear
  introduction, carrier key, carrier-to-obligation mapping, incoming
  transition, converging predecessor set, carrier SCC/self-edge, root
  reachability, state tag, complete alternative, transfer relation, consumption
  relation, consume/reinitialize diamond, stable pending backedge, loop reset,
  reintroduction, loop exit, and normal exit;
- every logical-drop component order, pre-consumption, normal continuation,
  abort-on-panic boundary, forbidden unwind or cleanup-resuming panic successor,
  skipped remaining component, partial discharge publication, and premature
  replacement or cast-result commit;
- unsafe boundary event, unsafe ordinal, occurrence sequence/order/gap,
  same-place multiple occurrences, operation, requirement, acknowledgement,
  acknowledgement scope, unsafe-scope enter/exit kind and scope payload,
  missing or additional enter, crossed nesting, non-dominating enter,
  incomplete exit cut set, raw input/output, carrier key, predecessor SCC and
  self-edge, root reachability, each raw provenance-origin tag, universe
  membership, missing root, additional derived origin, missing or additional
  least-closure origin, maximal four-class cyclic merge, source mapping, and
  every attempted raw-to-reference checked fact, route, reference definition,
  origin, loan, region, or non-`AddressOnly` escape proof;
- every escape event key, kind tag and payload, proof tag and payload,
  direct origin route, raw-carrier origin route, route multiplicity, raw
  carrier, and required point;
- each retained initialization, move, conflicting-loan, first-consumption, and
  escape-origin cause by deletion, insertion, replacement, permutation,
  duplication, and wrong span, including a mutation that leaves the primary
  diagnostic unchanged;
- borrow-evidence repository brand, lease key, embedded lease equality, lease
  teardown, missing/foreign/stale capability, and any attempted ownership-fact
  repository or ownership lease;
- each arbitrary-precision or symbolic budget factor, product, actual
  membership counter, fixed-width-overflow substitution, and attempted
  Cartesian preallocation;
- every sequence count, frame length, enum tag, field order, sort order,
  duplicate key, missing record, additional record, and revision byte; and
- every result tag with another branch's payload or a successor value attached
  to a rejected branch.

Pairwise precedence fixtures combine identity plus source failure, revision plus
source failure, codec plus source failure, invalid place plus source failure,
source failure plus candidate corruption, and two candidate corruptions. They
must select the first legal stage above and publish no forbidden value.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| Gate routing authority | `.codex/subagents/manifest.yaml` | `task-router` |
| RFC governance and review tracking | `docs/rfc/0007-borrow-lifetime-ownership-checker.md`, `docs/rfc/tracking/0007-review-and-implementation.md`, `docs/rfc/README.md` | `rfc` |
| Checked marker, capture, receiver, unsafe, logical-drop, cast-resource-route, and ownership input facts | `products/zomlang/compiler/checker/**`, `products/zomlang/compiler/type/**` | `binder-checker` |
| Borrow-evidence repository capability, lease resolution, and session transaction wiring | `products/zomlang/compiler/driver/**` | `module-system` |
| Required source registry entries and invariant diagnostics | `products/zomlang/compiler/diagnostics/**` | `error-system` |
| Frontend-only concurrency exclusion and fail-closed handoff | `docs/spec/chapters/15-concurrency.md` | `concurrency` |
| Built MIR, ownership analysis, facts, verifier, typestate, and dump | `products/zomlang/compiler/mir/**`, `products/zomlang/compiler/hir/**` | `ir-backend` |
| Source ownership, references, drop, raw provenance, and runtime safety | `docs/spec/chapters/14-memory-management.md`, `libraries/zc/**`, `products/zomlang/runtime/**` excluding `products/zomlang/runtime/**/task*`, `products/zomlang/runtime/**/async*`, `products/zomlang/runtime/**/actor*`, `products/zomlang/runtime/**/channel*`, and `products/zomlang/runtime/**/scheduler*` | `runtime-memory` |
| Normative type, memory, concurrency, and architecture alignment | `docs/spec/**`, `docs/design/**` | `spec-audit` |
| Unit, lit, conformance, differential, corpus, architecture, codec, and coverage gates | `products/zomlang/tests/**`, `scripts/check-ownership-architecture.py`, `scripts/run-ownership-coverage.py`, `scripts/check-ownership-coverage.py`, `.github/workflows/**` | `verification` |

The three proposed verification-owned script paths are
`scripts/check-ownership-architecture.py`,
`scripts/run-ownership-coverage.py`, and
`scripts/check-ownership-coverage.py`. They do not exist and are not currently
granted to the `verification` owner by `.codex/subagents/manifest.yaml`.
Before any implementation starts, `task-router` must grant those exact paths
to `verification` in the coordinated transition below. This RFC does not
authorize differently named scripts or a broad `scripts/**` grant.

## Security And Safety Impact

This pass is a mandatory memory-safety gate. A false negative can permit a
dangling reference, use after move, overlapping mutable access, double
consumption, missing linear consumption, or reference escape. A stale or forged
proof can let cleanup or backend emission consume facts for a different module
or MIR body.

The design limits these risks with verified Built MIR, branded evidence
leases, immutable facts, independent reconstruction, exact revision lineage,
closed source failures, and successor suppression. Unsafe acknowledgement is
operation-specific. It does not grant unchecked access to safe ownership state.

Fact and failure records contain canonical identities and validated source
spans but no object addresses, raw pointer values, source contents, or hash-map
state. Debug dumps are explicit user-requested artifacts under RFC 0010.

## Drawbacks And Risks

- Complete point-by-path and point-by-loan facts can be memory-intensive.
- Precise non-lexical regions and two-phase activation require two independent
  fixed-point implementations for trustworthy verification.
- Conservative index, dereference, and raw-provenance rules reject some safe
  programs.
- Chapter 15 programs cannot enter HIR, MIR, or ownership analysis until a
  concurrency RFC defines their semantic producer contract.
- The RFC 0013 direct-root summary rejects aggregate and parametric borrow
  relations that require an explicit region language.
- Deterministic complete diagnostics require analyzing all functions after the
  first source failure.

## Alternatives Considered

### Ownership Analysis During Type Checking

Type inference and CFG ownership dataflow have different domains and fixed
points. Combining them would make checker recovery state part of MIR safety and
would obscure the revision boundary. The selected design consumes frozen facts.

### Ownership Analysis On HIR

HIR lacks the complete CFG, temporary lifetimes, exit edges, logical drops, and
operation order required by Chapter 14. Built MIR is the
earliest complete input.

### A Separate Ownership CFG

A second CFG could diverge on evaluation order, early return, panic, or cleanup.
The selected design addresses every fact by an RFC 0010 CFG
point or a derived event cutpoint and uses the Built MIR successor graph
directly.

### Lexical Loan Lifetimes

Ending every loan at block exit is simpler but rejects valid last-use programs
and misses path-sensitive uses through loops and joins. The selected design
computes the least point set satisfying liveness constraints.

### Runtime Borrow Tracking

Runtime tracking adds overhead and detects invalid safe code only when executed.
It also does not prove linear consumption or static region containment. Runtime
checks may aid debugging but are not the safety contract.

### Treat Source Failures As IR Invariants

Invalid ownership source is expected compiler input. RFC 0013 provides the
dedicated source branch so the driver emits registered diagnostics without
classifying the program as a compiler defect.

## Compatibility And Rollout

No implementation step in this RFC is authorized by its eventual acceptance
alone. The named `RFC0007 Ownership Rail Enablement Transaction` is complete
only when all of these governance records exist:

1. RFC 0007 has reached `ACCEPTED`, then its proposal and tracker record
   `ACCEPTED -> IMPLEMENTING` and name one exact implementation series;
2. RFC 0005's proposal and tracker name the participating direct-replacement
   series that deletes both raw-to-reference `CastKind` cases, retags the
   remaining closed union, refreshes its checked-fact codec/revision/oracles,
   and changes the checker matrix to `ZOM4013` with no cast or unsafe fact;
3. RFC 0013's proposal and tracker record `ACCEPTED -> IMPLEMENTING`, replace
   the unset `implementation` field with its tracker link, and name the
   participating direct-replacement series
   for Built MIR revision,
   `VerifiedBorrowEvidenceLease`, the explicit
   `BorrowEvidenceRepositoryCapability`, the fixed ownership result, and
   capability-bearing ownership and successor constructor signatures;
4. RFC 0006's proposal and tracker record `ACCEPTED -> IMPLEMENTING`, replace
   the unset `implementation` field with its tracker link, and name the
   participating direct-replacement series
   for ownership-driven cleanup consumption; and
5. `task-router` updates `.codex/subagents/manifest.yaml` to grant
   `scripts/check-ownership-architecture.py`,
   `scripts/run-ownership-coverage.py`, and
   `scripts/check-ownership-coverage.py` to `verification` before any gate file
   is created or edited.

The RFC 0013 and RFC 0006 records were made on 2026-07-17 and authorize only
their tracker-owned prerequisite slices. Until the RFC 0005 direct-replacement
record, RFC 0007 record, and manifest grant also exist, every RFC 0007 code,
test, script, specification, or design-document slice remains blocked. A gate
file without its manifest grant or an RFC 0007 implementation link without the
upstream records authorizes nothing.

After that transaction, the implementation is one direct ownership rail:

1. complete Built MIR ownership vocabulary and `zom.mir-revision`
   verification;
2. implement source failures, canonical ordering, and the diagnostic adapter;
3. implement move paths, place conflicts, initialization, and move dataflow;
4. implement loans, activation, regions, reborrow restoration, and evidence-
   backed call/return conformance;
5. implement marker, linear, closure, checked-cast, raw-provenance, and unsafe
   rules;
6. implement ownership facts, independent verification, revisions, and
   `OwnershipCheckedMir` construction;
7. wire the session transaction and RFC 0006 cleanup consumer; and
8. enable ownership-gated successor construction only after every required
   conformance and architecture gate passes.

No source syntax migration is required. There is no bypass flag, alternate AST
analysis, partial publication, recovered proof, duplicate fact schema, or
decoder for another ownership-facts domain. Rollback before `LANDED` reverts
the complete ownership slice and leaves successor MIR unavailable.

## Documentation And Teaching Plan

- Align Chapter 14 with event-keyed move-path joins, NLL reference-value
  regions, root/active reborrow origins, checker-time deferred-activation
  projection, direct-root return conformance, abort-only logical-drop actions,
  linear obligations, raw provenance, raw-to-reference rejection, and
  conservative suspension behavior.
- Keep Chapter 15's frontend-only boundary exact and document the fail-closed
  HIR/MIR handoff until a concurrency RFC supplies semantic facts.
- Add `docs/design/ownership-analysis.md` only when live implementation exists;
  keep its schema and verifier map synchronized with code.
- Document the retained `ZOM4056-ZOM4066` and `ZOM4069-ZOM4070` ownership
  relationships, deletion without reassignment of `ZOM4067-ZOM4068`, and
  `ZOM4093-ZOM4095`, including the pre-checker `ZOM4095` rejection, with
  examples.
- Add contributor examples showing Built MIR event slots, points, reference
  definitions, and corresponding verified ownership facts after the dump
  exists.

## Operational Readiness

CI runs the source-failure matrix, proof verifier, codec oracles, architecture
gate, determinism matrix, bounded exhaustive differential oracle, translated
corpus, sanitizer build, and full CTest suite. Release builds retain lineage
and final ownership verification before cleanup or code generation.

Coverage is fail-hard. `scripts/run-ownership-coverage.py` performs one
coverage-instrumented configure, build, and external CTest run, then emits raw
profiles plus `llvm-cov export` JSON. `scripts/check-ownership-coverage.py`
derives the source census from every non-test `.cc` below
`products/zomlang/compiler/mir/ownership/**` plus every changed non-test `.cc`
under `products/zomlang/compiler/{checker,type,hir,mir,driver,diagnostics}/**`
between the implementation series merge base and head. Each inventoried file
must have at least 70 percent line coverage, and aggregate inventoried line
coverage may not decrease from the merge-base run. A file can be exempt only
when a checked-in `products/zomlang/tests/coverage/ownership-exemptions.json`
row names the exact path, uncovered line ranges, technical reason, approving
`verification` owner, and expiry commit; expired, broad-glob, missing, or
unapproved rows fail. Generated files and third-party files are outside the
census rather than exempted.

The checker emits deterministic JSON and Markdown evidence containing the
toolchain identity, merge-base and head commits, complete census, per-file
covered and total lines, percentage, baseline delta, exemptions, and final
verdict. Missing profiles, a changed-source census mismatch, unparsable LLVM
output, an empty census, any below-threshold file, or baseline regression is a
failure. `scripts/check-ownership-coverage.py --self-test` must exercise one
passing fixture and mutations for empty census, omitted changed file, 69.99
percent coverage, baseline regression, malformed tool output, and invalid or
expired exemption. CI retains the JSON, Markdown, profiles, and exported
coverage as one artifact.

Tracing records function identity, point/path/loan/region/obligation counts,
worklist changes, proof bytes, and phase duration. It never records source
contents by default. A production compiler bug bundle retains complete typed
RFC 0010 or RFC 0011 invariant facts.

## Acceptance Criteria

1. RFC 0007 is indexed, structurally valid, and approved at one exact proposal
   hash by every required owner; no implementation begins until the complete
   `RFC0007 Ownership Rail Enablement Transaction` is recorded.
2. `requires` includes RFCs 0005, 0006, 0010, 0011, 0013, and 0015; the binding
   RFC 0011, RFC 0013, and RFC 0015 hashes are recorded in the tracker. The
   enablement transaction directly replaces RFC 0005 by deleting both raw-to-
   reference `CastKind` cases, retagging the remaining closed union, refreshing
   its checked-fact codec/revision/oracles, and requiring `ZOM4013` with no cast
   or unsafe fact.
3. `analyzeOwnership` accepts only matching Built MIR revision,
   `VerifiedOwnershipEventOverlay`, `VerifiedBorrowEvidenceLease`, and live
   `BorrowEvidenceRepositoryCapability` borrowed references and implements
   exactly the four RFC 0013 result branches without a global or lease-only
   lookup.
4. Built MIR contains complete typed places and CFG; the RFC 0007 event overlay
   contains causally staged unified `MirEventKey`
   identities, one authoritative checker-time deferred-activation map,
   collision-free unsafe occurrences, one complete revision-bound marker-use
   inventory, one authoritative logical drop plan per
   initialization, and one verified cast-resource plan per cast.
   `buildOwnershipEventOverlay` and `verifyOwnershipEventOverlay` receive the
   same exact `OwnershipEventOverlayInput` while its RFC 0005
   `BodyCheckingInput` is live; each pass constructs its own RFC 0015 proof
   input, query context, active stack, and memo, none of which is stored in the
   overlay, facts, wrapper, repository, singleton, or global lookup.
   Together they cover
   constants, canonical `MirStatement::BorrowCreation`, discriminant update and
   switch, borrowed panic payload,
   every `MirCheckedCast` source/check/carrier/branch/drop/commit, exits, borrow
   activation, captures, marker uses, complete Built MIR unsafe-scope boundary
   events with outer statement tag `0x07` and exact six-byte payload framing,
   overlay-only unsafe occurrence/source association, event source mapping,
   and evidence lineage before ownership runs. Chapter 15 syntax fails
   at pre-checker bound-module admission with fresh `ZOM4095`; forged
   concurrency MIR fails Built input validation and never projects ownership
   facts.
5. Move paths cover every local and projection prefix; reflexive conflict is
   implicit, and encoded pairs contain exactly the distinct unordered
   conflicts within the `M * (M - 1) / 2` bound.
6. Initialization and move dataflow executes address/preflight, all source
   reads and moves, operation effects, and destination commits in the exact
   transactional causal order; preserves the explicit dead bit across branches,
   loops, joins, and storage boundaries; retains every sorted loss cause; and
   distinguishes move-only use from never-initialized,
   deinitialized, and storage-ended use through partial moves,
   reinitialization, overwrite, drop-and-replace, and all exit kinds.
7. Loans cover shared, mutable, effect-stage issue, deferred effect-stage
   activation, exact independent checker-time `DeferredActivationFact`
   projection from RFC 0005 and RFC 0009 checked receiver facts into event overlay,
   ownership-side overlay-to-loan bijection with no checker re-resolution,
   before/after cutpoint transitions, invalidation, complete
   multi-origin and multi-parent reborrow relations, parent suspension,
   reserved/active/suspended point alternatives, all conflicting-loan causes,
   and path-sensitive restoration.
8. Event-granular region solving fixes `a outlives b` as `b.livePoints` subset
   `a.livePoints`, publishes the complete reflexive-transitive closure, and
   covers `Storage`, NLL `LocalValue`, and `ClosureValue` identities,
   temporaries, inputs, calls, direct-root returns, stores, checked casts, and
   raw provenance. Reference
   point facts preserve complete reaching definitions and multi-origin
   copy/move/overwrite/join/reborrow transfer, and local value regions end at
   their last use or earlier kill rather than `StorageDead` by default.
9. Copy and Linear decisions preserve RFC 0015 `Positive`, explicit `Negative`,
   and `Unsatisfied` outcomes as `Positive`, `ExplicitNegative`, and
   payload-free `Unsatisfied` under the exact policy and coherence lineage, use
   unified event identities to distinguish operands at one location, never
   infer a marker from spelling, never encode `InvariantRejected`, and never
   manufacture a negative `MarkerFact` for an unsatisfied query. Resource-plan
   projection first queries and records both decisions for every required
   descendant, then folds the private tree in postorder. Only a direct action
   stops discovery; `Copy + Linear` requires every immediate stored field to
   be positive `Copy`; and structural cleanup is selected exactly for an
   action-free positive `Linear`, not-positive `Copy` parent whose child fold
   is non-empty.
10. Linear obligations transfer and consume exactly once on every normal path
    using complete relational alternatives and a complete multi-predecessor
    carrier-transition relation, including branch convergence, carrier SCCs,
    stable pending backedges, consume/reinitialize diamonds, loop
    reintroduction, per-exit validation, `Copy + Linear`, returns, calls,
    logical drops, and every first-consumption cause.
    Consuming checked casts consume the verified overlay carrier and result
    plans, preserve every `DropResourceSubject` through complete
    carrier-to-result routes, discard only plan-absent trivial representation
    state, and preserve a one-to-one `Linear` obligation relation; an unmatched
    nontrivial or linear component is rejected before Built MIR. Drop
    obligations independently cover closed and open logical drop,
    initialized, uninitialized, maybe-initialized, partial aggregate mutation,
    overwrite, consuming checked-cast failure, and fail-closed `StorageDead`.
    Every logical-drop component is pre-consumed before its optional action,
    normal return continues to the next component, action panic enters the
    terminal RFC 0006 abort path, and no unwind, cleanup-resuming panic,
    remaining-cleanup, or partial-discharge successor is admitted.
11. Unsafe acknowledgement is operation-specific; safe ownership checks remain
    active inside `unsafe`; multiple unsafe operations attached to one place
    occurrence have contiguous collision-free `UnsafeBoundaryKey` ordinals;
    Built MIR encodes enter and exit with the sole outer
    `UnsafeScopeBoundary = 0x07` statement tag, one inner kind byte, and one
    big-endian scope ordinal, with no compatibility tags;
    the finite four-class raw-origin universe is
    complete, reference-to-raw conversion cannot extend origin lifetime, and
    raw-to-reference is rejected even inside `unsafe` without publishing a
    checked fact, reference definition, loan, region, or reference escape proof;
    cyclic carrier propagation computes the least SCC provenance closure and
    terminates within the `Araw * Araw` edge and `Araw * U` origin bounds.
12. Every `OwnershipSourceFailure` variant maps to its exact primary and note
    diagnostic, including proposed `ZOM4093-ZOM4094`, while pre-checker
    admission maps only to fresh `ZOM4095` and deletes `ZOM4067-ZOM4068`
    without reassignment. Ownership failures retain every
    initialization, move, conflicting-loan, first-consumption, and escape-
    origin cause, and has deterministic suppression, source anchors, and order.
13. `VerifiedOwnershipFacts` is immutable, keyed only by structural MIR
    identities, contains the exact CFG/event-cutpoint, reference-value,
    `DropResourceSubject`, drop-obligation, linear-carrier,
    cast-carrier, raw-origin,
    raw-carrier, escape-origin, and unsafe-boundary schemas, records exact
    Built/event-overlay/evidence revisions, and contains no AST,
    binder, HIR, target, object-address, local-store-slot identity, or duplicate
    deferred-activation or marker-use inventory.
14. The `zom.ownership-event-overlay` codec reproduces the exact 141-byte,
    206-byte, and unsafe-collision 513-byte oracles. The `zom.ownership-facts`
    production codec and independent test encoder reproduce the exact 165-byte
    empty, 286-byte function-framing, and 378-byte non-empty point-state
    oracles plus every non-empty component vector. The unchanged
    `zom.mir-revision` codec reproduces the exact 283-byte unsafe-scope
    framing oracle with revision
    `c49976b9fc841ecf6cd2e2d62af3442d36a22571b52291a0601e60ea92f71aa0`;
    only the canonical domain and unsafe-scope outer tag are accepted.
15. The independent overlay verifier recomputes the causal event projection,
    every deferred-activation row, authoritative marker-use query, logical-drop
    plan, and cast-resource plan through its own proof input constructed from
    the shared `OwnershipEventOverlayInput.body` while the admitted checked
    module and exact `BodyCheckingInput` are live. Producer and verifier use
    distinct proof inputs, empty active stacks, and memos; the verifier derives
    its own complete phase-one query tree and phase-two postorder fold without
    reading producer query selection, traversal, candidate decisions, transfer
    functions, worklists, caches, or canonical record writer. It also
    recomputes every before/after state and every semantic fact and record.
16. The complete single- and multi-mutation matrix selects the required
    identity, input-revision, codec, source, or proof branch and publishes no
    forbidden value.
17. Only matching verified Built MIR, RFC 0007 event overlay, ownership facts,
    embedded RFC 0013 lease, and explicit live repository capability satisfy the exact
    `finalizeOwnership`, `elaborateDrops`, `elaborateCoroutines`, and
    `verifyExecutableMir` moved-input APIs; facts are moved values with no
    ownership repository, every operation resolves through the explicit
    borrowed capability, and every rejected branch returns no predecessor or
    cleanup, coroutine, executable MIR, LIR, or backend successor.
18. Worker counts `1`, `2`, `4`, and `8`, input permutations, and repeated
    processes produce byte-identical failures, facts, revisions, and dumps.
19. The optimized solver agrees with bounded exhaustive path exploration for
    generated CFGs, NLL multi-origin reference values, maximal cyclic raw-origin
    merges, exact linear loop alternatives, and the translated ownership
    corpus. All exponential bounds use arbitrary-precision or symbolic
    arithmetic and valid large input is never an ownership invariant failure.
20. The task-router manifest grants exactly
    `scripts/check-ownership-architecture.py`,
    `scripts/run-ownership-coverage.py`, and
    `scripts/check-ownership-coverage.py` to `verification`; the architecture gate
    rejects `NodeId`, AST, binder metadata, `TypeEnv`, name lookup, foreign body
    reads, a second CFG, bare-location operation identity, fact-specific
    ordinals, lease-only repository lookup, fixed-width Cartesian budgets, raw
    failure strings, and caller-constructed verified wrappers in the ownership
    implementation.
21. Chapters 14 and 15, design docs, diagnostics documentation, MIR dumps, and
    conformance expectations match the landed implementation, including the
    Chapter 15 fail-closed boundary.
22. Sanitizer configure/build, default CTest, lit, conformance, RFC, format,
    CJK, spec-alignment, architecture, coverage checker self-test, per-file
    70-percent ownership coverage, aggregate baseline non-regression, and diff-
    hygiene gates pass before `LANDED`.

## Implementation Plan

1. Finalize exact-hash owner review, move RFC 0007 through `REVIEW` and
   `ACCEPTED`, then execute the complete `RFC0007 Ownership Rail Enablement
   Transaction` across RFC 0005, RFC 0007, RFC 0013, RFC 0006, and the task-
   router manifest. No following step may begin before it completes.
2. Complete the RFC 0013 direct replacement with explicit repository
   capability plumbing and the RFC 0007-owned event overlay and
   cutpoint view, authoritative revision-bound marker-use inventory, canonical
   `MirStatement::BorrowCreation`, total cast-carrier
   resource projection, authoritative checker-time deferred-activation map,
   logical-drop and cast-resource plans,
   collision-free overlay-only unsafe occurrences, complete Built MIR unsafe-
   scope boundary projection with the sole outer `0x07` tag, pre-checker
   Chapter 15 rejection, source map, verifier, dump, and malformed-input tests
   without changing the MIR domain, framing, or lineage fields.
3. Add the closed source-failure types and exhaustive diagnostic adapter for
   `ZOM4056-ZOM4066`, `ZOM4069-ZOM4070`, and `ZOM4093-ZOM4094`; add fresh
   pre-checker `ZOM4095`, and delete `ZOM4067-ZOM4068` plus every scoped-task
   producer or reservation without reassigning either numeric code.
4. Implement structural move paths, place conflicts, initialization, move and
   drop obligations, canonical `DropResourceSubject` generations, closed/open
   logical drop, component pre-consumption and abort-on-panic enforcement,
   fail-closed `StorageDead`, and the independent bounded oracle.
5. Implement loans, verified-overlay-bound deferred activation with no ownership-
   side checker lookup, storage and NLL value regions,
   complete reference definitions/origins, reborrows, and direct-root borrow-
   evidence consumption.
6. Implement return, storage, closure, checked-cast, raw-provenance, strict raw-
   to-reference rejection, and unsafe-boundary checks.
7. Thread the complete `OwnershipEventOverlayInput` through the checker-time
   handoff. Build the authoritative Copy and Linear inventory by completing the
   full descendant query tree before a postorder logical-drop-plan fold; make
   producer and verifier construct separate body-derived proof inputs, query
   contexts, active stacks, and memos; and implement linear obligations without
   storing marker authority in HIR, Built MIR, overlays, wrappers, or
   repositories.
8. Add the complete immutable ownership-facts schema, canonical codec,
   revision, producer, independent verifier, resource-plan component vectors,
   and mutation injection.
9. Add capability-bearing private `OwnershipCheckedMir` and successor
   construction plus atomic CompilerSession publication; connect RFC 0006
   cleanup only to that wrapper.
10. Add unit, lit, conformance, differential, translated-corpus, determinism,
    performance-counter, architecture, coverage-runner, coverage-checker, and
    coverage-checker self-test gates at the exact verification-owned paths.
11. Align Chapters 14 and 15 and live architecture documentation, then run the
    complete repository verification matrix before `LANDED`.

## Test Plan

- Build: `cmake --preset sanitizer` and
  `cmake --build --preset sanitizer -j 8`.
- Unit tests: every source/effect/commit event slot, role, ordinal, constant,
  destination commit, `BorrowIssue`/`BorrowActivation` cutpoint, canonical borrow
  rejection, every `MirCheckedCast` mode/kind/branch/result, discriminant
  update/switch, panic payload, place/projection
  relation, move-path parent, conflict pair,
  implicit self-conflict, lattice transfer, loss cause, edge join, loan state,
  activation, complete checker-time overlay `DeferredActivationFact`,
  ownership-side bijection without checker lookup, root/active reborrow origin,
  reference definition and NLL kill,
  outlives orientation and closure, marker use, linear carrier and complete
  alternative, authoritative logical-drop plan, cast-resource route,
  `DropResourceSubject`, unsafe occurrence ordinal, multiple unsafe operations
  on one place occurrence, unsafe-scope outer tag, inner kind, six-byte payload,
  enter/exit dominance and complete exit cut set, raw-origin universe, escape
  origin, source failure, result branch, codec field, repository capability,
  and wrapper constructor.
- CFG tests: straight-line, diamond, nested branch, loop, irreducible loop,
  early return, residual return, logical variant switch, borrowed-payload panic,
  unwind, unreachable block, partial initialization, every checked-cast success
  and failure shape with cast carrier transfer/drop, `SetDiscriminant`, and
  drop-and-replace. Same-location fixtures move a source
  twice and alias a source with its later destination to prove transactional
  causal transfer and rollback.
- Storage-lattice fixtures: `Dead + Initialized` and `Dead + Uninitialized`
  diamonds, conditional `StorageLive`, loop backedges that mix dead and live
  states, rejected direct `StorageDead` after nontrivial partial initialization,
  accepted open drop followed by `StorageDead`, and reads from every
  may-dead state. Each fixture asserts the exact three-bit join and complete
  loss-cause set, including mixed move/never-initialized/deinitialized causes
  and `ZOM4093-ZOM4094`, against the exhaustive oracle.
- Borrow tests: shared/shared, shared/mutable, mutable/shared,
  mutable/mutable, disjoint fields, dynamic indexes, subslices, dereferences,
  immediate/deferred activation, independent overlay reconstruction, missing,
  additional, swapped, and field-mutated activation rows, forbidden ownership-
  side checker resolution, nested multi-origin reborrows, input/static
  root preservation, sibling reborrows, and restoration at joins.
- Reference-value tests: input/static/root-loan definitions, copy, move,
  initialize, overwrite, deinitialize, same-generation origin union,
  different-generation separation, maybe-initialized joins, DirectRoot call
  results, last-use before/after cutpoints, last use before `StorageDead`, and
  kills on every CFG path.
- Escape tests: local and temporary return, RFC 0013 receiver/parameter return,
  valid input-root reborrow return,
  local and escaping stores, closure capture, checked-cast result, direct plus
  multiple raw-carrier origins, duplicate regions through distinct routes, and
  one-note-per-origin ordering. Raw-to-reference fixtures reject the cast inside
  and outside `unsafe` before checked-fact publication and reject forged routes,
  reference definitions, loans, regions, and non-`AddressOnly` escape proofs.
  Pre-checker admission tests prove every `spawn`
  and `suspend` emits fresh `ZOM4095`, constructs no signature facts, checked
  module, or HIR, and emits neither `ZOM4067` nor `ZOM4068`; forged concurrency
  MIR selects `InvalidFact` and publishes no ownership value.
- Marker tests: positive explicit, structural, builtin, and policy-subject
  evidence, including `SharedReference`, `ConstRawPointer`, and
  `MutableRawPointer`, explicit negative, unsatisfied, invariant-rejected,
  missing/foreign/mismatched Copy and Linear decision, policy and coherence
  revision mismatch, not-positive predicate, forbidden negative fact
  synthesis, and body-derived `MarkerProofInput` identity, lifetime, and
  teardown. Fixtures reject a
  missing, foreign, stale, swapped, reconstructed, stored, post-teardown, or
  hidden-singleton input and prove the exact identity-versus-IR failure mapping.
  A poisoned producer memo, producer in-flight sentinel, or deliberately
  different producer query order cannot affect the verifier's fresh active
  stack, memo, complete query set, or result. Resource-projection fixtures query
  both `Copy` and `Linear` for the root and every action-free descendant,
  including descendants below a positive `Linear` parent and components later
  suppressed by folding. They cover a direct-action stop; all four parent
  Copy/Linear combinations; positive `Copy + Linear` with all-positive-Copy
  immediate fields; distinct `ExplicitNegative` and `Unsatisfied` field
  failures; positive `Linear` plus not-positive `Copy` with empty and non-empty
  descendant folds; reverse-field `Builtin(ownerType)` cleanup; action-free
  parent omission; and maximal child retention. Mutations stop at positive
  `Linear`, omit or add a descendant query, change either descendant decision,
  swap canonical field order, fold preorder, retain a subsumed child, select
  the wrong cleanup action, or emit the wrong parent. Linear-flow fixtures then
  cover `Copy + Linear`, move transfer, duplicate consumption, branch-specific
  first consumptions, exact consume/reinitialize diamonds, multiple
  predecessor carriers converging at one destination site, carrier SCCs and
  self-edges, carrier swaps between obligations, a
  loop-preexisting pending value consumed after the loop, valid consumed loop
  reintroduction, invalid pending reintroduction at the obligation's own site,
  complete alternative joins, each normal loop exit, and unconsumed normal
  exits.
- Drop tests: closed and open drop over `Uninitialized`, `Initialized`, and
  `MaybeInitialized`; rejection of every may-dead state; direct
  `StorageDead` for only trivial values; rejection of initialized non-`Copy`
  and `Linear` values without a proven logical drop; partial aggregate
  initialization, field move, field reinitialization, overwrite, action-free
  affine and `Linear` resources, the three `DropRequirement` forms, and reverse-declaration-order
  component mutation. Checked-cast fixtures cover move/copy carrier
  initialization; checker-time source, target, and optional-result plan
  projection; exact resource-subject pairing; plan-absent trivial
  representation discard; rejection of unmatched nontrivial source or result
  components; one-to-one `Linear` transfer; success transfer;
  optional/forced failure drop; linked linear consumption; and every phase
  mutation. Ordinary logical-drop action fixtures prove
  per-component pre-consumption, normal continuation, terminal abort on panic,
  no remaining cleanup after abort, no partial discharge fact, and rejection of
  unwind or cleanup-resuming panic successors and premature commits.
- Diagnostic tests: exact primary/note code, severity, arity, source anchor,
  retained typed payload, all reaching initialization and branch-move causes,
  all conflicting branch loans, all reaching first consumptions, all direct and
  raw-carried escape origins, secondary-only cause mutations, deduplication,
  suppression, and ordering for every source variant.
- Raw provenance tests: each of the four root classes, every pairwise merge,
  one maximal diamond and loop SCC containing all origin classes and maximum
  fixture carriers, self-copy and cyclic assignment, copy/move/reinterpret
  propagation, universe/root equality, exact least-closure mutations, and
  `Araw * Araw` plus `Araw * U` termination counters.
- Codec tests: exact `zom.ownership-event-overlay` 141-byte empty, 206-byte
  empty-function, and 513-byte unsafe-collision vectors; exact
  `zom.ownership-facts` 165-byte empty, 286-byte function-framing, and
  378-byte non-empty point-state vectors; non-empty component vectors including
  `OwnershipPoint`, deferred activation, drop obligations, cast carriers,
  cast result resource plans, `DropResourceSubject`, all marker-decision tags,
  positive evidence forms, explicit negative fact, payload-free unsatisfied
  decision, all optional drop-action and route-proof forms, unsafe ordinal,
  event stage, and all role tags; exact
  `zom.mir-revision` 283-byte unsafe-scope vector plus the `0x07` outer tag,
  inner kind, scope payload, sequence count, and frame-length mutations; every
  tag/count/length/order/duplicate mutation; independently
  recomputed digest, and no production codec call from the test oracle. The
  pass-local marker proof inputs remain call-duration only and must not add a
  query context, query tree, memo, tag, domain, or decoder. The
  deferred-activation authority must exist only in the overlay map, never as a
  duplicate facts field;
  non-empty plan oracles include descendant marker-use rows before one folded
  parent component, and every descendant-query, decision, order, fold, and
  cleanup-selection mutation is rejected or changes both the overlay revision
  and its enclosing facts revision.
- Proof-lineage tests: stale, foreign, swapped, wrong-module,
  wrong-evidence, missing/foreign/stale marker-proof input, missing/foreign/
  stale repository capability, post-teardown marker and repository capability,
  malformed, missing, additional, and source-
  plus-invariant precedence fixtures; reject an ownership-fact repository,
  ownership lease, lease-only lookup, stored marker-proof authority, hidden
  global or singleton lookup, or successor construction without an explicit
  live capability. Assert that producer and verifier each construct a distinct
  marker input only from the exact live RFC 0005 `BodyCheckingInput`, validate
  its lineage independently, borrow it only for that pass, destroy it when that
  pass returns, and never store it in any output. Assert distinct
  producer/verifier query contexts and memo state, the exact borrowed analysis
  inputs, moved finalize/successor inputs, result branches, consumed-input
  destruction, and no-predecessor-on-rejection ownership contract.
- Differential tests: generate bounded typed MIR CFGs, enumerate reachable
  reference, linear, raw, and ownership states independently, and compare with
  producer and verifier. Separately exercise symbolic `K` values above every
  fixed-width integer without Cartesian allocation or invariant rejection.
- Translated corpus: maintain an attribution and semantic-mapping manifest for
  applicable Rust MIR borrow-check and Move reference/resource cases; every
  translation records whether ZOM should accept or emit one registered code.
- Determinism: workers `1`, `2`, `4`, and `8`; reverse function/block/record
  input; fixed map-seed permutations; repeated clean processes; compare source
  failures, canonical facts, revisions, dumps, and successor availability.
- Lit and conformance: assert registered diagnostic codes and MIR/ownership
  dumps for every source-reachable family; no test proves semantics from an AST
  dump alone.
- Architecture: run `python3 scripts/check-ownership-architecture.py`; forbid
  AST, `NodeId`, binder metadata, `TypeEnv`, source-name marker inference,
  admitted Chapter 15 MIR operations, foreign body reads, duplicate
  CFGs, bare-location ownership facts,
  fact-specific ordinals, lease-only repository resolution, fixed-width
  Cartesian budgets, public verified constructors, raw failure strings, and
  target facts.
- Coverage: run `python3 scripts/check-ownership-coverage.py --self-test`, then
  `python3 scripts/run-ownership-coverage.py` and
  `python3 scripts/check-ownership-coverage.py`; require the exact changed-
  source census, at least 70 percent line coverage per file, no aggregate
  baseline regression, and a retained machine-readable evidence artifact.
- Spec alignment: Chapters 3, 4, 5, 6, 14, and 15 plus checker, HIR, MIR,
  diagnostics, and conformance artifacts.
- Repository gates: `python3 scripts/check-rfc.py`,
  `python3 scripts/check-format.py`, changed-file CJK scan,
  `git diff --check`, and `ctest --preset default --output-on-failure`.

## Open Questions

None

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-07-08 | DRAFT | Created the ownership-analysis proposal. |
| 2026-07-10 | REVIEW | Entered technical review. |
| 2026-07-10 | RETURNED | Review required an accepted Built MIR input, a legal source-rejection seam, exact proof lineage, and complete verification contracts. |
| 2026-07-11 | RETURNED | Added RFC 0013 as a required dependency while its ownership integration boundary awaited acceptance. |
| 2026-07-17 | DRAFT | Re-entered drafting after RFC 0013 and RFC 0015 acceptance with a complete Built MIR ownership model, source algebra, canonical facts, independent verifier, and evidence-lineage gates. |
| 2026-07-17 | DRAFT | Required-owner matrix review returned the checked-cast, unsafe-occurrence identity, deferred-activation evidence, wrapper API ownership, concurrency boundary, runtime exclusion, codec-oracle, and coverage contracts for repair. |
| 2026-07-17 | DRAFT | Closed the owner-matrix blockers with total checked-cast projection, collision-free unsafe ordinals, checked-fact-derived activation, exact moved successor APIs, Chapter 15 fail-closed input, manifest-aligned runtime exclusions, non-empty codec evidence, and fail-hard 70-percent per-file coverage. |
| 2026-07-17 | DRAFT | Exact-owner review returned consuming checked-cast ownership, MIR unsafe-occurrence encoding, pre-HIR concurrency admission, stale task diagnostics, and incomplete drop/StorageDead transfer for repair. |
| 2026-07-17 | DRAFT | Closed the exact-owner blockers with a cast-owned carrier, canonical unsafe identity, pre-HIR ZOM4067 admission, ZOM4068 deletion, joint drop/linear/cast alternatives, ownership-facts oracles, and fail-closed StorageDead. |
| 2026-07-17 | DRAFT | Exact-owner re-review returned type-changing cast resource lineage, unsafe boundary projection, pre-checker admission reachability, diagnostic-code reassignment, and stale borrow-form baseline text for repair. |
| 2026-07-17 | DRAFT | Closed the re-review blockers with exact cast resource-plan preservation, overlay-only unsafe occurrences plus Built boundary events, bound-module admission, fresh ZOM4095 allocation with ZOM4067-ZOM4068 deletion, and canonical MirStatement::BorrowCreation terminology. |
| 2026-07-17 | DRAFT | Independent review returned missing post-checker resource authority and ambiguous unsafe-scope statement framing. |
| 2026-07-17 | DRAFT | Closed the independent-review blockers with authoritative logical-drop and cast-resource overlay facts, canonical DropResourceSubject generations, ownership codecs, one outer unsafe-scope tag, and an exact MIR framing oracle. |
| 2026-07-17 | DRAFT | Independent re-review returned the unclosed marker-result algebra and missing revision-bound marker-use handoff inventory. |
| 2026-07-17 | DRAFT | Closed the marker blockers with exact Positive, ExplicitNegative, and Unsatisfied decisions, invariant fail-closed handling, authoritative event overlay marker uses, decision-referenced resource plans, and overlay/facts lineage oracles. |
| 2026-07-18 | DRAFT | Exact re-review returned the missing explicit RFC 0015 marker-query capability flow and a contradictory positive-Linear resource traversal rule in proposal `f1e19ad2c85c0d6c4f114f4ac1a5af7b343b72e22aa77ebadf284057bf3a90e4`. |
| 2026-07-18 | DRAFT | Closed both blockers with a call-duration `MarkerProofInput`, independent producer/verifier query contexts, exact lineage failure mapping, and one query-first then postorder resource-plan algorithm. |
| 2026-07-18 | REVIEW | Independent exact-hash review accepted proposal `c7fb9d9ef4665371c07ddc79d3ad4c3bcad2e7061565ea70801e65081964f405` with no technical blockers; entered unanimous required-owner review. |
| 2026-07-18 | REVIEW | Closed formal re-review blockers by moving deferred activation into the authoritative checker-time overlay, rejecting every raw-to-reference path, defining abort-only per-component logical-drop execution, refreshing codecs and oracles, and correcting current REVIEW state text; fresh exact-hash owner review remains required. |
| 2026-07-18 | ACCEPTED | All ten required owners approved the same exact REVIEW proposal and tracker snapshots with no objections; implementation remains blocked until an explicit `ACCEPTED -> IMPLEMENTING` transition and the coordinated enablement transaction. |
| 2026-07-24 | IMPLEMENTING | Enablement transaction recorded: RFC 0005 deleted `RawConstToReferenceChecked`/`RawMutableToReferenceChecked` and retagged `CastKind` 0x01-0x0d; task-router granted verification ownership of the RFC 0007 architecture and coverage paths; implementation tracker now authorizes the ordered slices. |
| 2026-07-25 | IMPLEMENTING | Synchronized RFC 0024's complete body-owned event-overlay input and independent producer and verifier proof-input construction. |
| 2026-07-25 | IMPLEMENTING | Synchronized the accepted RFC 0025 toolchain-core context and distribution-digest lineage for standard Copy and Linear identity while retaining fail-closed ownership decisions at proposal SHA-256 `4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`. |
