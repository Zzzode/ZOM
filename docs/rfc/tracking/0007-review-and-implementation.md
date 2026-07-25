# RFC 0007 Review And Implementation Tracker

This document is the local discussion, exact-hash review, and implementation
record for RFC 0007. It does not approve the proposal. Proposal frontmatter and
the Decision Record remain authoritative.

## Discussion Record

### 2026-07-10 Review Return

Review returned the proposal because ownership analysis did not yet have an
accepted Built MIR input, a legal source-rejection branch, revision-bound
cross-module borrow evidence, a complete proof schema, or an independent
verification strategy.

### 2026-07-11 Integration Dependency

RFC 0013 was added as the required integration boundary. It defines the fixed
`OwnershipAnalysisResult`, `VerifiedBorrowEvidenceLease`, MIR revision, and
successor evidence lineage. RFC 0007 remained `RETURNED` until that dependency
was accepted and a complete proposal could be submitted.

### 2026-07-17 Built MIR Ownership Redesign

The first Built MIR redesign was submitted at proposal SHA-256
`588bf8f347b40a3e4bda02b15b93a84bdf8693ffaa67d2237b996cc8577ceeb6`
and tracker SHA-256
`e9ffda2ab6b9ab7ed7b87009c3cc8fe12286992672ebc71310dd2f52369407d7`.
It received no approval.

### 2026-07-17 Exact-Hash Proof Review Return

RFC governance returned that draft because it did not define
`MirTaskScopeId`, complete point/linear/escape/unsafe/raw-provenance proof
records, exact record tags and mutations, a sound dead/live join, finite
multi-path linear consumption, or complete multi-cause diagnostic provenance.
It also omitted the direct RFC 0011 dependency, did not hard-gate the RFC 0013
and RFC 0006 implementation transitions, used a broad script impact, and
retained stale RFC-index verification claims.

### 2026-07-17 Proof Schema And Enablement Repair

RFC 0011 is landed at file SHA-256
`383dc8905ae389949008f47f3b501d812a26d91769460d7e41731283b2f8cc03`.
RFC 0013 is accepted at file SHA-256
`e3909d5caad48a1c0255ee57d2a8fcc327e046945f20a586e0c6bf0115a237c3`.
RFC 0006's accepted contract SHA-256 is
`0b8915df3a7d5a49a52b3980bd8063edff7b24c4d0bc08a18697048e567d9ebc`.
Both RFC 0013 and RFC 0006 recorded `ACCEPTED -> IMPLEMENTING` on 2026-07-17
for their direct-replacement prerequisite slices; neither transition approves
or implements RFC 0007.
RFC 0015 is accepted at file SHA-256
`9704d5651606e8a74034c8af4be5172b4007a6c9f0ee8ea2f5ee183223401c01`
and has precedence for the marker-proof lineage it names.

RFC 0007 now directly requires RFCs 0005, 0006, 0010, 0011, 0013, and 0015.
The repair defines the complete structural proof schema, explicit dead-state
bit, finite linear path relation, all-cause failure provenance, canonical codec, and
the named `RFC0007 Ownership Rail Enablement Transaction`. It proposes exactly
`scripts/check-ownership-architecture.py` and requires task-router manifest
authority before that gate or any implementation begins.

That repaired DRAFT proposal SHA-256 was:

`47681f8f662c78f174748ededc9f74fc7113709c8bc9a51797c41426b3145044`

It received no approval.

### 2026-07-17 Relational Proof Review Return

Exact-hash review returned proposal
`47681f8f662c78f174748ededc9f74fc7113709c8bc9a51797c41426b3145044`
and tracker
`d58f8aa2e2717cf79ebcf92cc56e154511e6b4e9f058000f5e374b3f0cad9262`.
The review found eight blocking contracts:

1. unavailable reads did not distinguish never-initialized, explicitly
   deinitialized, storage-ended, and moved origins with complete source
   provenance;
2. linear point state used independent may-fields that could fabricate
   impossible carrier, consumption, and absence combinations at joins;
3. escape and task failures retained only one origin instead of every direct
   and raw-carried cause;
4. `RegionFact.outlives` did not fix an orientation or require an exact
   reflexive-transitive closure;
5. the conflict inventory mixed an implicit reflexive rule with an encoded
   pair contract that did not exclude self-pairs or state the exact bound;
6. `OwnershipCheckedMir` depended on an undefined ownership repository
   lifetime instead of the accepted RFC 0013 borrow-evidence lease;
7. raw provenance did not define a closed finite origin universe or a complete
   termination bound; and
8. tracker hashes, review gates, and status evidence were consequently stale.

No owner approved either returned hash.

### 2026-07-17 Relational Provenance Repair

The new draft replaces those contracts directly:

- initialization facts retain the complete sorted `InitializationLossCause`
  relation and select proposed `ZOM4093-ZOM4094` for unavailable reads that
  are not move-only;
- each linear point contains complete relational alternatives, and every
  pending or consumed state names an obligation-bound carrier in an acyclic
  carrier graph;
- escape facts and source failures retain every direct and raw-carried origin,
  with exact route, ordering, note emission, and mutation requirements;
- `a.outlives` now means that `a` outlives `b`, stores the complete reflexive-
  transitive closure, and requires `b.livePoints` to be a subset of
  `a.livePoints`;
- reflexive place conflicts are implicit while encoded records contain only
  distinct lower-key-first unordered pairs within `M * (M - 1) / 2`;
- `OwnershipCheckedMir` resolves only the exact RFC 0013 embedded
  `VerifiedBorrowEvidenceLease`; ownership facts are moved values and there is
  no ownership repository or ownership lease;
- the four-class `RawOriginUniverse` is derived from a closed Built MIR root
  inventory and provenance is bounded by `Araw * U`; and
- the ownership codec is now `zom.ownership-facts`, with independent
  137-byte and 250-byte framing vectors.

The repaired DRAFT proposal SHA-256 is:

`d76c0db25d0fdccb01f2b9e40b17c6c5df4b5dc71e8b775a28f5de94dd6ec428`

This hash has no owner approval. Any proposal-byte change requires a new hash
and a fresh review record.

### 2026-07-17 Loop And Convergence Review Return

Exact-hash review returned proposal
`d76c0db25d0fdccb01f2b9e40b17c6c5df4b5dc71e8b775a28f5de94dd6ec428`
and tracker
`5ee12b4be900258b09b5bd2718a3cc3959b0ee23cfbb25cfc57153515c3f9efa`.
One review found that a single-predecessor linear carrier could not encode
branch convergence, that a pending obligation introduced before a loop must be
allowed to cross a stable backedge and be consumed after the loop, and that raw
copy/assignment loops require carrier SCCs and self-edges.

A concurrent review of the same returned proposal also found that ordinary
storage, NLL local values, closure values, and scoped children lacked complete
region identities; reference values had no reaching-definition or multi-origin
transfer relation; and local destinations incorrectly used the full physical
storage interval. It also found fact-specific operation ordinals, a lease with
no repository-resolution authority, and a fixed-width-overflow hole in the
exponential linear bound. Concurrent proposal edits invalidated that review for
approval, but not its technical findings. No owner approved the returned
proposal or tracker hash.

### 2026-07-17 Event, NLL, Loop, And Authority Repair

The repaired draft directly closes all findings:

- a canonical `MirEventKey` and closed slot/role projection distinguish every
  operand and operation while remaining derived from MIR revision bytes;
- `Storage`, `LocalValue`, `ClosureValue`, and `ScopedChild` regions plus
  `ReferencePointFact` encode exact reaching definitions, root/active origins,
  copy/move/overwrite/join/call/reborrow transfer, and last-use NLL;
- linear carriers publish complete multi-predecessor transition relations,
  allow carrier SCCs and stable pending backedges, and validate every normal
  loop exit exactly once;
- raw carriers allow SCCs and self-edges and publish the least root-seeded
  provenance closure on the finite carrier-origin lattice;
- `analyzeOwnership`, `OwnershipCheckedMir`, and every successor constructor
  require an explicit live `BorrowEvidenceRepositoryCapability`, with no
  lease-only or global lookup;
- all exponential budgets use arbitrary-precision or exact symbolic products,
  never Cartesian preallocation or fixed-width semantic rejection; and
- the changed fact schema uses `zom.ownership-facts`, with new independent
  137-byte and 250-byte framing vectors.

The repaired DRAFT proposal SHA-256 is:

`05e016a815d7ccef13507b8f74630561b598fc8fba9a84ed73a195a3fa5ebfa9`

This hash has no owner approval. Any proposal-byte change requires a new hash
and a fresh review record.

### 2026-07-17 Causal Projection And Cutpoint Review Return

Exact-hash review returned proposal
`05e016a815d7ccef13507b8f74630561b598fc8fba9a84ed73a195a3fa5ebfa9`
and tracker
`57a31a69d24144501a20a438e263dfa06992da9bf0208ea75ab4bef94c5eca07`.
It received no approval. The review found three remaining blockers:

1. the event-slot table wrote destinations before reading or moving RHS values,
   borrow sources, replacements, captures, spawn arguments, and suspend
   payloads, and fixed operation effects at ordinal zero, so loan issue and
   activation had no causal transfer boundary;
2. the closed projection omitted constant operand behavior,
   `SetDiscriminant`, logical variant switches, and borrowed panic payloads, and
   did not select `MirStatement::BorrowCreation` as the single canonical
   issuance identity; and
3. region liveness and published point state remained `MirPoint`-granular while
   move, issue, activation, kill, and write transfer occurred between slots, so
   the proof could not express the state immediately before and after an event.

### 2026-07-17 Causal Projection And Total-MIR Repair

The repaired draft closes those blockers directly:

- every ordinary operation projects all source slots, one operation effect,
  and destination commits in causal order; address and preflight are
  non-mutating phases, source moves are sequential, and a failed operation
  publishes no partial delta;
- `BorrowIssue` is the borrow effect after the source read and before the
  reference destination commit; `BorrowActivation` is the call effect after all
  receiver and argument sources and before any result commit;
- constants, `SetDiscriminant`, logical variant switches, and borrowed panic
  payloads have exact roles and transfer; `MirStatement::BorrowCreation` is the
  sole issuance form and Built MIR rejects every other issuance shape before
  ownership;
- `OwnershipPoint` adds structural `Cfg`, `BeforeEvent`, and `AfterEvent`
  cutpoints, and initialization, references, loans, regions, linear state, raw
  carriers, escape containment, operational bounds, mutations, and tests all
  use those exact boundaries; and
- the unapproved `zom.ownership-facts` domain remains the draft codec. Its
  empty and function-framing oracle preimages contain no non-empty point, stage,
  or role record, so their exact lengths and SHA-256 values remain unchanged;
  new non-empty vectors cover the added tags and causal states.

The repaired DRAFT proposal SHA-256 is:

`1a91ad79e385dedaaa8d969f1fd12080be6c89e2427c40ea9b4787d30a9195ea`

This hash has no owner approval. Any proposal-byte change requires a new hash
and a fresh review record.

### 2026-07-17 Exact Technical Re-review Acceptance

An independent exact-hash technical review accepted proposal
`1a91ad79e385dedaaa8d969f1fd12080be6c89e2427c40ea9b4787d30a9195ea`.
The review confirmed the causal `Source -> Effect -> Commit` projection, total
current and accepted MIR event coverage, canonical
`MirStatement::BorrowCreation`, and
event-granular liveness cutpoints. This technical result resolves the three
previously returned blockers but does not substitute for the required-owner
approval matrix or authorize a status transition.

### 2026-07-17 Required-Owner Matrix Review Return

The required-owner matrix returned proposal
`1a91ad79e385dedaaa8d969f1fd12080be6c89e2427c40ea9b4787d30a9195ea`
and tracker
`a521b0dc3ef2377b64a2de700aa84b1a70b344b999cd82bdad2186eb3a73ed9b`.
It received no owner approval. The review found seven blockers:

1. `MirCheckedCast` had no total source/check/branch/result event projection;
2. multiple RFC 0005 unsafe operations attached to one place occurrence could
   collide on a bare `MirEventKey`;
3. deferred activation had no explicit independently verifiable fact derived
   from the exact RFC 0005 and RFC 0009 receiver records;
4. `OwnershipCheckedMir` and successor construction had no exact public API,
   borrow/move ownership notation, result algebra, or rejection ownership;
5. speculative task-scope production contradicted Chapter 15's frontend-only
   semantic boundary;
6. the Repository Impact runtime-memory row failed to exclude the concurrency-
   owned runtime globs; and
7. the codec lacked an exact non-empty oracle and verification lacked a fail-
   hard per-file coverage threshold.

### 2026-07-17 Required-Owner Matrix Repair

The repaired draft closes those blockers directly:

- all checked-cast modes project one input source and check, exact success and
  failure edge effects, and mode-specific result commits;
- `UnsafeBoundaryKey { event, unsafeOrdinal }` and an encoded contiguous
  `MirUnsafeOccurrence` sequence distinguish every unsafe operation without
  duplicating a place transfer;
- `DeferredActivationFact` is a complete one-to-one relation reconstructed from
  the mutable checked receiver mode, exact adjustment steps, borrow issue,
  receiver source, and call effect;
- analysis borrows its inputs, finalization and successors consume move-only
  inputs through exact RFC 0010 result types, and rejection returns no
  predecessor or partial successor;
- Chapter 15 operations fail before ownership analysis and the proposal defines
  no task identity, task region, task escape, or task diagnostic;
- Repository Impact mirrors the manifest's five concurrency-runtime exclusions;
  and
- `zom.ownership-facts` has exact 137-byte, 250-byte, and non-empty 326-byte
  oracles, while verification gains an exact changed-source census, at least
  70-percent line coverage per inventoried file, aggregate baseline non-
  regression, checked exemptions, evidence artifacts, and negative self-tests.

The proposal remains `DRAFT` with `approvers: []`. Its repaired proposal
SHA-256 is
`efc47eb758f01b74386f83e6bd1abfeee61cf0265c0fcd13dd86c3f4d746db90`.
It requires a fresh exact-hash review from every required owner.

### 2026-07-17 Exact-Owner Contract Review Return

Exact-owner review returned proposal
`efc47eb758f01b74386f83e6bd1abfeee61cf0265c0fcd13dd86c3f4d746db90`
and tracker
`9950ef76ebf53ba647869ac0348530891227c07ff9fba934cf45ce7646f4f810`.
It received no approval. The review found five remaining blockers:

1. a consuming `MirCheckedCast` moved its input before the check but had no
   cast-owned carrier, failure-edge logical drop, partial-initialization rule,
   or linked linear transfer;
2. `unsafeOccurrences` was simultaneously claimed to be absent from and
   encoded in MIR revision, leaving no accepted field, revision, verifier,
   or codec oracle for `UnsafeBoundaryKey`;
3. Chapter 15 rejection incorrectly reused RFC 0010
   `FeatureBoundaryVerification` after executable MIR instead of defining a
   legal pre-HIR checker/session result and successor suppression;
4. live `ZOM4067-ZOM4068` scoped-task rows had no legal producer under that
   frontend boundary and conflicted with the proposed failure algebra; and
5. `StorageDead` could erase initialized non-`Copy` or `Linear` values without
   a proven logical drop, and open drop, obligation discharge, partial
   initialization, and mutation transfers were incomplete.

### 2026-07-17 Exact-Owner Re-review Return

Exact-owner re-review returned proposal
`cf1fe8c08b49706aaeb554f2ae195bce9186f3b8a2a91a8cc73176cf524a41ea`
and tracker
`e99deb76f37076d9e1ade09ae95df1a747e20dced3bfd58b6b864e999992e296`.
It received no approval. The review found five blockers:

1. a type-changing consuming cast transferred carrier obligations without an
   exact result-type drop plan, resource-subject relation, remaining-source
   rule, or complete `Linear` transition;
2. unsafe occurrences were described as both overlay-only and part of
   `MirRevisionId`, while unsafe-scope entry and exit had no complete event
   projection or key validation;
3. Chapter 15 admission accepted `VerifiedCheckedModule` even though the
   checker cannot construct that complete value for untyped `spawn`, and the
   proposal did not explicitly replace RFC 0010's concurrency-to-MIR clause;
4. the proposed pre-checker failure reassigned `ZOM4067`, contrary to the
   diagnostic owner's no-reassignment rule; and
5. the proposal and tracker claimed that the live Built MIR model still had a
   borrow rvalue even though `MirStatement::BorrowCreation` is already the only
   production issuance form.

### 2026-07-17 Cross-Owner Contract Closure Repair

The repaired draft closes those blockers directly:

- checked-cast success independently derives the exact result drop plan,
  requires every nontrivial carrier component to pair with one result component
  of the same semantic resource subject and logical drop plan, permits only
  source-only `Copy`, non-`Linear`, obligation-free representation state,
  forbids result fabrication, and preserves every `Linear` obligation through
  a one-to-one transfer;
- unsafe occurrence ordinals, source association, acknowledgement association,
  and occurrence spans live only in `zom.ownership-event-overlay`;
  `MirUnsafeScopeBoundary` enter and exit statements remain ordinary MIR
  operations with exact event projection, dominance, nesting, and complete
  exit-cut validation;
- ownership-surface admission now consumes `VerifiedBoundModuleInput` before
  signature or body checking requires complete expression types, successful
  checking is sealed into `OwnershipAdmittedCheckedModule`, and RFC 0007
  explicitly replaces RFC 0010's concurrency-to-MIR clause for the currently
  frontend-only `spawn` and `suspend` nodes;
- the enablement transaction deletes `ZOM4067-ZOM4068` without reassignment and
  allocates fresh `ZOM4095 ConcurrencySemanticsUnavailable`; and
- every live-contract reference names canonical
  `MirStatement::BorrowCreation`, matching the current Built MIR rail.

The six existing oracle preimages and hashes remain byte-identical because the
repair changes validation and source contracts without changing their encoded
component records. The proposal remains `DRAFT` with `approvers: []`,
`decision: TBD`, and no status transition. Its repaired proposal SHA-256 is
`93f042edee388609082f629bc11a956ee651aaf468617faffcbfe66f7f99bed6`.
It requires a fresh exact-hash owner review.

### 2026-07-17 Marker Decision And Handoff Re-review Return

Independent re-review returned proposal
`13a3289001a24ad7c525ea8caafcd022f7d5a5ac7f7540b0802595415f705ce8`
and tracker
`5b7111e90d8e4336fef6d91f4525d0f435abec093c341b1258dd4947249a4951`.
It received no approval. The review found two remaining blockers:

1. `LogicalDropPlanComponent` required positive or negative RFC 0005
   `MarkerFact` payloads even though RFC 0015 `Unsatisfied` carries no fact.
   This left no closed distinction between explicit negative evidence and an
   unsatisfied proof query and could authorize a forged negative fact; and
2. `OwnershipMarkerUse` was a loose prose-only union. No authoritative
   revision-bound checker-time inventory keyed the event, marker, subject,
   policy revision, coherence revision, and decision into the overlay handoff.
   Claims that HIR, MIR, or ownership facts already transported that inventory
   were therefore false.

### 2026-07-17 Marker Decision And Handoff Repair

The repaired draft closes both blockers directly:

- `OwnershipMarkerDecision` is the exact closed persisted projection of RFC
  0015: `Positive`, `ExplicitNegative`, and payload-free `Unsatisfied` use tags
  `0x01`, `0x02`, and `0x03`. `ExplicitNegative` contains only the exact
  verified explicit negative coherence fact. `Unsatisfied` contains no
  `MarkerFact`, and not-positive is a verifier predicate rather than an
  encodable tag or authority to synthesize evidence;
- RFC 0015 `InvariantRejected` is not persisted. Identity failures select RFC
  0010 `IdentityInvariantRejected`, checker failures select
  `IrInvariantRejected(InvalidFact, OwnershipProofValidation)`, and neither
  branch publishes a partial marker use, resource plan, or event overlay;
- each `OwnershipFunctionEventOverlay` now carries the complete sorted
  `markerUses` map keyed by event, expanded marker, expanded subject, exact
  marker-policy revision, and exact coherence revision. The
  checker-time producer and independent verifier reconstruct the same closed
  query inventory with separate RFC 0015 memo state while the admitted checked
  module remains live;
- logical-drop components reference Copy and Linear marker-use keys instead of
  embedding assumed positive or negative facts. Resource traversal records
  decisions for every visited place, including omitted trivial components, and
  cast routing preserves the positive-versus-not-positive result without
  collapsing `ExplicitNegative` into `Unsatisfied`;
- `zom.ownership-event-overlay` contains the marker-use map with no parallel
  decoder. Its canonical vectors are recorded under Verification Evidence; and
- ownership facts bind the opaque `OwnershipEventOverlayRevision`. A
  lineage-composition oracle proves that
  every marker-use mutation changes both the event overlay revision and the
  enclosing ownership facts revision.

The proposal remains `DRAFT` with `approvers: []`, `decision: TBD`, and no
status transition. Its repaired proposal SHA-256 is
`f1e19ad2c85c0d6c4f114f4ac1a5af7b343b72e22aa77ebadf284057bf3a90e4`.
It requires a fresh exact-hash owner review.

### 2026-07-18 Marker Capability And Resource Projection Re-review Return

Exact re-review returned proposal
`f1e19ad2c85c0d6c4f114f4ac1a5af7b343b72e22aa77ebadf284057bf3a90e4`
and tracker
`17b48fc615b2ebe9d84c7aae8903fe46e16facbd08ae9d91622aa52a90357ecd`.
Neither hash received approval. The review found two blocking contracts:

1. `buildOwnershipEventOverlay` did not explicitly receive RFC 0015's
   authoritative `MarkerProofInput`. The draft therefore did not close its
   construction from the exact live RFC 0005 `BodyCheckingInput`, call-duration
   lifetime, producer/verifier independent query contexts and memos,
   non-storage rule, or exact lineage and failure mapping; and
2. the resource-plan prose both treated positive `Linear` as a completed
   aggregate and required descendant marker evidence. It did not define one
   implementable order that records every required descendant `Copy` and
   `Linear` decision before selecting maximal components and cleanup actions.

### 2026-07-18 Marker Capability And Resource Projection Repair

The repaired draft closes both blockers directly:

- `buildOwnershipEventOverlay` receives one explicit borrowed RFC 0015
  `MarkerProofInput`. The private checker/session creates it only from the exact
  live RFC 0005 `BodyCheckingInput` and policy registry; it remains live through
  candidate construction and independent verification and is destroyed before
  either owning input. No digest reconstruction, stored wrapper or overlay
  field, repository entry, hidden singleton, global, or session lookup is
  permitted;
- producer and verifier construct distinct RFC 0015 query contexts with empty
  private active stacks and completed-result memos. They derive the complete
  query tree and results independently. Neither may inspect the other's memo,
  in-flight state, query selection, traversal, candidate plan, or decisions;
  RFC 0015's linearizable semantic interner is the only shared mutable
  authority;
- context, fingerprint, semantic-store identity, policy revision, local-
  signature parent, imported-signature view, and coherence revision must match
  the admitted module. Invalid canonical identity selects
  `IdentityInvariantRejected`; foreign, missing, stale, swapped, or post-
  teardown lineage selects `InputRevisionMismatch`; query-level checker
  rejection selects `InvalidFact` at `OwnershipProofValidation`. Every branch
  publishes no partial marker use, plan, candidate, or verified overlay;
- resource projection is one exact two-phase algorithm. Phase one visits the
  root and every action-free stored-field descendant in canonical declaration
  order and records both `Copy` and `Linear`; only a direct action or leaf stops
  discovery. Phase two folds children before parents, validates immediate-field
  `Copy` for `Copy + Linear`, selects structural reverse-field cleanup only for
  a positive-`Linear`, not-positive-`Copy` parent with a non-empty child fold,
  and emits the unique maximal non-overlapping component set; and
- the call-duration capability, query contexts, active stacks, memos, and
  private query tree are not encoded. The event overlay and ownership facts schemas, domains,
  tags, and the seven exact byte vectors remain unchanged. Non-empty fixture
  oracles now contain all descendant marker-use rows, and mutations cover query
  omission/addition/order, distinct negative outcomes, preorder folding,
  immediate-field invalidity, child subsumption, and cleanup selection.

The proposal remained `DRAFT` with `approvers: []` and `decision: TBD` at
SHA-256
`c7fb9d9ef4665371c07ddc79d3ad4c3bcad2e7061565ea70801e65081964f405`.

### 2026-07-18 Exact Technical Acceptance And Review Entry

Independent exact-hash re-review accepted proposal
`c7fb9d9ef4665371c07ddc79d3ad4c3bcad2e7061565ea70801e65081964f405`
and tracker
`f1b05e0b69a7cebeec908ff8e8a2cd83527c1bb92f016ef6ab6d9f04522707d4`
with no technical blockers. The review independently verified the explicit
call-duration RFC 0015 marker capability, separate producer and verifier query
contexts, complete two-phase marker traversal and postorder resource-plan
fold, closed marker decisions and lineage, cast and unsafe boundaries,
diagnostic and rejection precedence, enablement transaction, and all eight
normative byte vectors.

RFC 0007 now enters `REVIEW` with `approvers: []` and `decision: TBD`. No
technical acceptance carries formal owner approval forward. Every owner in
`required-owners` must approve the same exact `REVIEW` proposal and tracker
snapshot before the acceptance transition.

### 2026-07-18 Formal Required-Owner Re-review Return

Formal re-review returned the current `REVIEW` proposal because four contracts
remained blocking:

1. one proposal sentence and three tracker statements described the current
   proposal or RFC index as `DRAFT` instead of `REVIEW`;
2. `DeferredActivationFact` was derived while checker authority was live but
   stored only in `OwnershipFunctionFacts`, forcing the later independent
   ownership verifier to reopen RFC 0005 and RFC 0009 checker facts after the
   checked wrapper had been destroyed;
3. raw-to-reference success preserved raw provenance without defining the
   pointer-validity, alignment, initialization, lifetime, region, and escape
   authority required to construct a safe reference; and
4. ordinary and checked-cast logical drop allowed action panic/unwind without
   defining per-component pre-consumption, normal and unwind continuation, or
   remaining cleanup.

The returned proposal SHA-256 was
`5a436548fcd3029037ec36b9c93c6c96d74cfc60ba9808d1fe15fbfd3c2d6992`;
the returned tracker SHA-256 was
`00cf36d245ff6faeffd86b99b6da835d0732c49f9ce19d4a2aca569d57fd0eb3`.
No owner approval was recorded for either hash.

### 2026-07-18 Checker Handoff And Fail-Closed Safety Repair

The repaired `REVIEW` proposal closes all four blockers directly:

- current-state text now consistently names `REVIEW` while historical status
  records remain unchanged;
- `OwnershipFunctionEventOverlay.deferredActivations` is the sole authoritative
  activation map. Producer and independent overlay verifier separately rebuild
  every RFC 0005/RFC 0009 receiver association while the checked wrapper and
  `BodyCheckingInput` are live. Ownership facts contain no duplicate map, and
  ownership producer/verifier consume only the verified overlay-to-loan
  bijection without checker lookup;
- raw-to-reference is fail-closed. The enablement transaction directly deletes
  RFC 0005's `RawConstToReferenceChecked` and
  `RawMutableToReferenceChecked`, retags the remaining closed `CastKind` union,
  refreshes its checked-fact codec/revision/oracles, and requires `ZOM4013` with
  no checked cast or unsafe fact. Unsafe acknowledgement cannot create a safe
  reference, loan, region, or reference escape proof;
- logical-drop components pre-consume their drop and linked linear obligations
  immediately before each optional action. Normal return continues to the next
  component; action panic enters RFC 0006's terminal abort path. Unwind,
  cleanup-resuming panic, remaining-cleanup, partial-discharge, and premature-
  commit successors are rejected; and
- the event-overlay non-empty oracles are 206 and 513 bytes, while
  ownership-facts non-empty oracles are 286 and 378 bytes.

The repaired proposal remains `REVIEW` with `approvers: []`, `decision: TBD`,
and `implementation: TBD`. Its exact SHA-256 is
`cb7ced8b17c6f8b6bd551a9d60f3aef5f1dd3deca56ede0b6606d72a019b9851`.
Every required owner must review this new exact hash; the repair author cannot
serve as its independent approval.

### 2026-07-18 Formal Required-Owner Approval

All ten required owners approved proposal
`cb7ced8b17c6f8b6bd551a9d60f3aef5f1dd3deca56ede0b6606d72a019b9851`
and tracker
`902002929daa6e4924f4ea4fbfe98468a7488f97db7a44b1e362f2716ded9a9b`
with no blocking or non-blocking objection.

- `task-router` and `rfc` approved the owner routing, dependency graph,
  coordinated enablement transaction, exact REVIEW state, and transition
  ordering.
- `binder-checker`, `module-system`, `concurrency`, and `runtime-memory`
  approved the authoritative deferred-activation overlay, checker-time
  producer/verifier isolation, raw-to-reference rejection, repository
  capability, pre-checker concurrency admission, and abort-only component
  drop.
- `error-system`, `ir-backend`, `spec-audit`, and `verification` approved the
  closed failure algebra, MIR/overlay/facts lineage, direct-replacement
  boundaries, spec transaction, exact byte oracles, coverage contract, and
  implementation gates.

The review repair author did not serve as an approver. Every approval applies
only to the exact REVIEW proposal and tracker hashes above and does not imply
implementation evidence or authorize production ownership publication.

## Current Repository Baseline

The current compiler has no `BorrowCheckerPhase` or AST ownership analysis.
`products/zomlang/compiler/mir/built-mir.h` and `.cc` contain the first Built
MIR value vocabulary for locals, projections, places, operands,
`MirStatement::BorrowCreation`, blocks, and return/unreachable terminators.
`MirRvalueKind` contains only `Use`; no borrow rvalue exists. The repository
does not yet contain the complete RFC 0007 ownership input,
`OwnershipSourceFailure`, `VerifiedOwnershipFacts`, its independent verifier,
or `OwnershipCheckedMir` construction.

Existing `ZOM4056-ZOM4070` rows in
`products/zomlang/compiler/diagnostics/diagnostics-checker.def` are registry
capacity, not implementation evidence. In particular, `ZOM4067
ScopedTaskBorrowEscapes` and `ZOM4068 ScopedTaskReferentHere` are stale rows
with no legal producer under the proposed Chapter 15 boundary. The enablement
transaction must delete both rows and every associated emitter or reservation
without reassigning either numeric code, then add the currently absent
`ZOM4093-ZOM4095`. Existing RFC 0013 borrow-interface and checker facts are
upstream inputs, not ownership proof publication.

RFC 0005 currently specifies `RawConstToReferenceChecked` and
`RawMutableToReferenceChecked` as successful checked-cast facts. RFC 0007 has no
pointer-validity or safe-reference authority that can soundly consume them. The
enablement transaction must therefore delete both cases, retag and refresh the
RFC 0005 checked-fact codec/revision/oracles, and update the checker to emit only
`ZOM4013` for those shapes before any RFC 0007 overlay or ownership input is
constructed.

## Owner Review Checklist

| Owner | Review State | Required Review Surface |
|---|---|---|
| `task-router` | Approved | Exact architecture and coverage script path grants plus RFC 0005/0006/0007/0013 coordinated enablement transaction |
| `rfc` | Approved | Dependency graph, current REVIEW state, exact-hash process, section completeness, and acceptance ordering |
| `binder-checker` | Approved | One-to-one checked-cast, strict RFC 0005 raw-to-reference deletion, explicit call-duration RFC 0015 marker-proof input, independent query contexts, complete two-phase Copy/Linear resource projection, capture, receiver, authoritative checker-time deferred-activation overlay, unsafe-occurrence, logical-drop-plan, and cast-resource-route facts projected while checker authority is live with no later semantic re-resolution or negative-fact synthesis |
| `module-system` | Approved | Explicit RFC 0013 repository capability, exact lease resolution, teardown rejection, capability-bearing successors, and atomic publication with no ownership repository |
| `error-system` | Approved | Closed source algebra, retained ownership codes, deletion without reassignment of `ZOM4067-ZOM4068`, fresh `ZOM4093-ZOM4095`, all-cause notes, suppression, precedence, and invariant routing |
| `concurrency` | Approved | Chapter 15 pre-checker bound-module admission, exact fresh `ZOM4095` producer and successor suppression, no task/suspension ownership producer, and no concurrency runtime implementation impact |
| `ir-backend` | Approved | RFC 0007 event overlay activation/resource codec and independent checker-time verifier, complete descendant marker-use query set, postorder maximal-component fold and cleanup selection, resource plans, cast carriers, one outer `UnsafeScopeBoundary = 0x07` MIR statement codec and oracle, canonical `MirStatement::BorrowCreation`, one-CFG rule, event cutpoints, facts without duplicate activation authority, typestate, and cleanup handoff |
| `runtime-memory` | Approved | Affine move/drop, canonical `DropResourceSubject`, all three drop-requirement forms, open/closed abort-only component drop, fail-closed `StorageDead`, type-changing cast verified-plan and subject preservation, NLL references, reborrow, cyclic raw provenance, raw-to-reference rejection, relational Linear state, panic, Chapter 14 safety, and exact runtime exclusions |
| `spec-audit` | Approved | Chapters 3, 4, 5, 6, 14, and 15 alignment with the proposed compiler contract |
| `verification` | Approved | Independent MIR unsafe-scope, event overlay activation/resource, and ownership facts exact oracles, marker-capability lineage/lifetime and producer-verifier isolation, raw-to-reference and abort-only component-drop negatives, descendant-query/postorder/resource-plan/cast/drop/event/cutpoint/NLL/loop mutation matrix, symbolic-budget differential solver, translated corpus, determinism, architecture, and fail-hard 70-percent per-file coverage gates |

Approval is recorded only in RFC 0007 frontmatter after every required owner
approves the same exact `REVIEW` proposal hash. This table is a routing aid and
does not grant approval.

## Review Entry Checklist

- [x] RFC 0011 is landed and directly included in `requires`.
- [x] RFC 0013 is accepted and included in `requires`.
- [x] RFC 0015 is accepted, included in `requires`, and its marker-lineage
      precedence is recorded.
- [x] Borrowed analysis, moved finalization/successor operations, RFC 0013
      analysis branches, RFC 0010 successor branches, and rejected-input
      ownership are exact.
- [x] Built MIR source/effect/commit event slots, CFG/before/after cutpoints,
      places, moves, cast-owned carriers, reference definitions, loans,
      derived activation state, regions, drop/linear alternatives, escape, and
      unsafe state are closed in RFC 0007 facts; authoritative deferred-
      activation projections and revision-bound marker uses exist only in the
      overlay inventory.
- [x] The event overlay contains the sole authoritative checker-time deferred-
      activation map, one complete revision-bound marker-use inventory, one
      logical-drop plan per initialization, and one verified cast-resource plan
      per cast; canonical
      resource subjects and complete route proofs prevent ownership analysis
      from reconstructing semantics after checked-module destruction.
- [x] Overlay construction receives one explicit call-duration RFC 0015
      `MarkerProofInput` constructed from the exact live RFC 0005
      `BodyCheckingInput`; producer and verifier own distinct empty active
      stacks and memos, no authority or query state is stored, and all lineage
      and teardown failures map exactly and publish nothing.
- [x] The same live overlay transaction independently reconstructs every
      deferred receiver association from checked facts, HIR, and Built MIR;
      ownership consumes only the verified map, proves its loan bijection, and
      never reopens checker dispatch or receiver-adjustment facts.
- [x] Marker decisions preserve positive proof, explicit negative fact, and
      payload-free unsatisfied outcomes under exact policy/coherence lineage;
      invariant rejection publishes nothing, and no unsatisfied query can
      manufacture a negative `MarkerFact`.
- [x] Logical-drop projection records `Copy` and `Linear` for the complete
      action-free descendant tree before postorder folding; only direct actions
      stop discovery, immediate-field `Copy + Linear` validity is exact, and
      structural cleanup and maximal-component selection are deterministic.
- [x] Constants, canonical `MirStatement::BorrowCreation`, `SetDiscriminant`, logical variant
      switch, borrowed panic payload, checked casts, calls, drops, and closures
      have a total causal projection over admissible Built MIR; Chapter 15
      operations fail closed before projection.
- [x] Raw-to-reference is fail-closed through direct deletion of both RFC 0005
      cast kinds, refreshed upstream checked-fact codec/revision/oracles, and
      `ZOM4013` with no cast or unsafe fact; unsafe acknowledgement cannot create
      reference authority, a loan, a region, or a reference escape proof.
- [x] Three-bit initialization, complete loss provenance,
      full reference/point state, drop/linear carrier alternatives, cast
      carriers, escape facts, overlay-owned deferred-activation facts, unsafe-
      occurrence facts, and raw carrier
      provenance have exact identities, tags, fields, ordering, validation, and
      mutations.
- [x] Linear consumption is a finite multi-predecessor carrier-to-obligation
      relation whose complete alternatives preserve branch convergence, stable
      pending backedges, loop SCCs, reintroduction, transfer, exit, and first-
      consumption correlations.
- [x] Drop obligations share those complete alternatives; open/closed drop,
      partial initialization, mutation, overwrite, checked-cast failure,
      all three drop-requirement forms, checked-cast verified-plan and canonical
      resource-subject preservation, and
      `StorageDead` have complete creation, transfer, discharge, and rejection
      rules.
- [x] Every logical-drop component is pre-consumed immediately before its
      optional action; normal return continues the sequence, panic terminates
      through RFC 0006 abort, and unwind, cleanup-resuming panic, partial-
      discharge, remaining-cleanup, and premature-commit successors are
      rejected.
- [x] Initialization, move, conflicting-loan, first-consumption, and direct or
      raw-carried escape causes retain every canonical row through producer,
      verifier, failure payload, and diagnostic-note parity.
- [x] Source failures map exhaustively to live diagnostics and proposed
      `ZOM4093-ZOM4094`; pre-checker rejection uses fresh `ZOM4095`, and the
      transaction deletes `ZOM4067-ZOM4068` without reassignment, with exact
      registration gating.
- [x] Region outlives orientation, `Storage`, event-granular NLL `LocalValue`,
      `ClosureValue`, root/active origin transfer, exact
      issue/activation/kill cutpoints, reflexive-transitive closure, strongly
      connected components, and least live-point propagation are exact.
- [x] Reflexive conflict is implicit; encoded conflict records are distinct,
      unordered, lower-key-first, and bounded by `M * (M - 1) / 2`.
- [x] Raw origins form an exact four-class root universe, cyclic derived
      carriers add no origin, and least SCC closure has finite `Araw * Araw`
      and `Araw * U` bounds.
- [x] Input, source, candidate, codec, and identity precedence is explicit.
- [x] Ownership facts are keyed by structural MIR identities and have an exact
      revision codec bound to the separate event-overlay revision.
- [x] Independent MIR 283-byte unsafe-scope, event overlay 141/206/513-byte,
      and ownership facts 165/286/378-byte exact oracles are specified.
- [x] Producer and verifier independence is normative for both marker queries
      and ownership facts; neither verifier reads producer memo, traversal,
      query selection, fold decision, transfer function, cache, or record
      writer.
- [x] `finalizeOwnership` consumes exact Built MIR, event overlay, and facts;
      every successor uses moved inputs, the exact embedded RFC 0013 lease, and
      an explicit live borrowed repository capability, with no ownership
      repository, lease, predecessor, or successor on rejection.
- [x] Deterministic parallelism, finite monotone work bounds, and arbitrary-
      precision or symbolic exponential budgets are specified.
- [x] Repository impact covers every required owner and mirrors the manifest's
      concurrency/runtime-memory exclusions.
- [x] RFC 0005, RFC 0013, RFC 0006, RFC 0007, and task-router authority are hard-
      gated by the named coordinated enablement transaction.
- [x] Acceptance criteria and test plan include unit, lit, conformance,
      differential, translated-corpus, proof-lineage, determinism, sanitizer,
      spec, format, CJK, architecture, non-empty codec, coverage self-test,
      per-file 70-percent coverage, and baseline non-regression gates.
- [x] `docs/rfc/README.md` currently indexes RFC 0007 as `ACCEPTED`, matching
      proposal frontmatter.
- [x] Fresh owner review approved one exact `REVIEW` proposal and tracker
      snapshot with unanimous required-owner approval.

## Decision Record

Decision: ACCEPTED on 2026-07-18.

Final accepted proposal SHA-256:
`2766b4ce7ddbb0cc08ea550d0c618228daf7a91e8b951aef03d4c9f1aced6dbb`.

All ten required owners approved the exact REVIEW proposal
`cb7ced8b17c6f8b6bd551a9d60f3aef5f1dd3deca56ede0b6606d72a019b9851`
and tracker
`902002929daa6e4924f4ea4fbfe98468a7488f97db7a44b1e362f2716ded9a9b`
with no objections. Governance authorized the mechanical transition from the
approval-recorded proposal
`5fcc8f5f0e13795ea3d458aa2cf5d32a0eb934ebfb34b13eac87b62c76283140`
and tracker
`2d8e77873a4a6eb0e697024d8cbf60bb666145f04fc93860f07af6c77f5a449b`.

RFC 0007 is accepted as a design only. `implementation: TBD` remains
authoritative, production ownership publication remains forbidden, and no
implementation slice may start before an explicit `ACCEPTED -> IMPLEMENTING`
transition plus the coordinated enablement transaction below.

## Implementation Tracker

The `RFC0007 Ownership Rail Enablement Transaction` completed on 2026-07-24:

- RFC 0005 deleted `RawConstToReferenceChecked` and `RawMutableToReferenceChecked`
  and retagged the remaining `CastKind` cases contiguously from `0x01` through
  `0x0d` (commit `c89b1f2f`).
- task-router granted `verification` ownership of the RFC 0007 architecture and
  coverage paths in `.agents/subagents/manifest.yaml`.
- RFC 0007 frontmatter transitioned `ACCEPTED -> IMPLEMENTING` with an
  implementation pointer and a matching Status History row.

The ordered implementation series is now authorized:

| Slice | State | Required Evidence |
|---|---|---|
| Built MIR and ownership event overlay | In Progress (event identity and slot codec only) | Executable evidence covers owner-bound `MirLocation`, all six `MirPoint` branches, causal operand ordinals, `Move` as `OperandRead` plus `OperandMove`, separate producer and verifier implementations, framed sorted-map slots with non-empty role sequences, independently encoded published revisions, and foreign owner/point/ordinal/role mutations. The complete six-inventory overlay contract is not implemented. |
| Closed ownership diagnostics | Authorized | Ownership source variants, deletion without reassignment of `ZOM4067-ZOM4068`, fresh pre-checker `ZOM4095`, proposed `ZOM4093-ZOM4094`, exact primary/all-cause note mapping, suppression, ordering, and retained payloads |
| Move paths, initialization, and drop | Authorized | Implicit reflexive conflict, exact distinct-pair inventory, three-bit lattice, complete loss causes, canonical `DropResourceSubject`, all three drop requirements, open/closed component drop with pre-consumption and abort-only action panic, partial initialization/mutation, checked-cast verified-plan and subject preservation, fail-closed `StorageDead`, overwrite, and differential oracle |
| Loans, references, and regions | Authorized | Complete before/after point phases, issue/activation/commit timing, exact checker-time overlay `DeferredActivationFact`, independent reconstruction while checker authority lives, ownership-side overlay bijection with no checker lookup, `Storage` and event-granular NLL value regions, reaching reference definitions, root/active multi-origin transfer, reborrow restoration, exact outlives closure, call evidence, escape, and differential oracle |
| Marker, linear, unsafe, and capture boundaries | Authorized | RFC 0015 marker-input construction, lifetime, lineage validation, producer/verifier isolation, full descendant Copy/Linear query tree, postorder component fold, cast-carrier drop/linear transfer, multi-predecessor linear SCCs, stable pending backedges, collision-free unsafe ordinals, four-class raw-origin universe, strict raw-to-reference rejection, least raw SCC closure, and complete admissible escape records |
| Verified ownership facts and typestate | Authorized | Independent event overlay and ownership facts codecs, exact 141/206/513 and 165/286/378-byte oracles plus the MIR 283-byte unsafe-scope oracle, no duplicate activation or marker inventory in facts, complete marker-capability/descendant-query/postorder/resource-plan/cast/drop/event/cutpoint/NLL/loop mutations, symbolic budget checks, and private constructors |
| Session and cleanup integration | Authorized (RFC 0006 prerequisite in progress) | Atomic publication, exact borrowed Built/overlay/evidence analysis inputs, moved Built/overlay/facts finalization and successor inputs, embedded lease plus explicit live capability resolution, value-owned facts without an ownership repository, cleanup consumption, and no predecessor or successor on rejection |
| Repository completion gates | Authorized | Exact architecture and coverage scripts, coverage checker self-test, per-file 70-percent line floor, aggregate baseline non-regression, sanitizer, default CTest, lit, conformance, corpus, determinism, spec, format, CJK, and diff hygiene |

### Event Identity And Slot Codec Executable Evidence

The current implementation slice constructs and independently verifies only
the event identity and slot codec:

- `MirLocation` binds every event point to its function owner, and `MirPoint`
  covers `Entry`, `BeforeStatement`, `AfterStatement`, `BeforeTerminator`,
  `Edge`, and `Exit`;
- a causal operand ordinal distinguishes source, effect, and commit events at
  the same MIR location;
- production `Move` operands publish `OperandRead` and `OperandMove`; `Copy`
  is covered only by the canonical codec oracle because production Built MIR
  has no `Copy` lowering caller;
- marker-use and logical-drop publication is blocked on RFC 0024 because the
  checked input has no verified semantic-role authority for canonical `Copy`
  and `Linear` definitions and the production graph supplies no configured
  prelude;
- the producer and verifier independently derive the slot sequence from Built
  MIR, while the codec frames the sorted event map and every non-empty role
  sequence;
- the published overlay revision is checked against a structurally independent
  oracle; and
- negative tests mutate a foreign owner, point branch, causal ordinal, and
  role, and require verification failure.

This evidence does not complete the six-inventory
`OwnershipFunctionEventOverlay` contract: `slots` has executable identity and
codec coverage, while `deferredActivations`, `unsafeOccurrences`, `markerUses`,
`logicalDropPlans`, and `castResourcePlans` remain unimplemented. In
particular, no executable producer or verifier yet constructs
`logicalDropPlans`. RFC 0024 review and acceptance is a prerequisite for those
two inventories. The Built MIR and ownership event overlay row therefore
remains `In Progress` and must not be reported as `Implemented`.

The implementation tracker must be updated with executable evidence only after
all five coordinated governance records exist. A transition or implementation link in
only one RFC does not unblock any slice.

## Verification Evidence

- `zom.mir-revision` unsafe-scope framing was independently recomputed: the
  283-byte preimage has SHA-256
  `c49976b9fc841ecf6cd2e2d62af3442d36a22571b52291a0601e60ea92f71aa0`.
- `zom.ownership-event-overlay` oracle preimages were independently
  recomputed: 141-byte empty SHA-256
  `9e673e954367c3f2783cef1a9ca46e4d7e89040f2d4285ac6e42c2137bbed1d2`,
  206-byte empty-function SHA-256
  `5e36e3dd6068992f4e3b99ea9eb7df4e3836f9f8c40eb9821238a3c6090d724c`,
  and 513-byte two-occurrence collision SHA-256
  `bb0547e574d4e5929b0c23807103b8566895fe9df68de0e9cfff01d3a9679f85`.
- `zom.ownership-facts` oracle preimages were independently recomputed:
  165-byte empty-function SHA-256
  `3fc9636b5e668ec6b10fac4ce2c76b0a20d87f9b25dd51b7729141ed33296b93`,
  286-byte function-framing SHA-256
  `b2d0e68fd7a597ddabb36e3d2c78cf96a596561921fb965e856fdd26363bfba2`,
  and 378-byte non-empty point-state SHA-256
  `f5acb38c2474aea982f75216e69997d0aa8d2634eebdabc7fc6d59e01a2d6577`.
- Ownership facts remains an opaque overlay-revision consumer; the normative
  lineage-composition oracle now requires every event overlay activation, marker-
  decision, or lineage mutation to change both revisions without adding a
  duplicate activation or marker inventory to `OwnershipFunctionFacts`.
- The marker capability and two-phase resource-projection authority remains
  call-duration only. The deferred-activation repair directly replaces the
  event overlay and ownership facts non-empty framing. Non-empty fixture oracles require
  complete
  descendant marker-use rows before postorder folding and cover missing,
  additional, reordered, and mutated queries; distinct explicit-negative and
  unsatisfied field outcomes; invalid immediate-field `Copy + Linear`;
  preorder folding; wrong child subsumption; and wrong cleanup selection.
- Proof-lineage fixtures now require construction from the exact live RFC 0005
  `BodyCheckingInput`, reject storage and hidden lookup, cover post-teardown and
  every lineage mismatch, and prove producer/verifier stack and memo isolation.
- The exact repaired `REVIEW` proposal hash
  `cb7ced8b17c6f8b6bd551a9d60f3aef5f1dd3deca56ede0b6606d72a019b9851`
  is embedded in the discussion and Decision Record before fresh owner review.
- Changed RFC and tracker files contain no CJK text.
- Placeholder scan permits only frontmatter `implementation: TBD`; the
  Decision Record is `ACCEPTED`.
- `python3 scripts/check-rfc.py` passes with the current `ACCEPTED` index row.
- Final exact-hash, CJK, placeholder, link, and diff-hygiene evidence is
  refreshed after this tracker update.
