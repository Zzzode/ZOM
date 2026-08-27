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
- the event-overlay non-empty oracles are 206 and 512 bytes, while
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
`compiler/mir/built-mir.h` and `.cc` contain the first Built
MIR value vocabulary for locals, projections, places, operands,
`MirStatement::BorrowCreation`, blocks, and return, unreachable, and call terminators.
The current direct-call lowering admits only same-module identifier callees with
no type arguments, raises clause, ABI override, or unwind edge. Each argument
must be a checker-verified scalar literal whose semantic type exactly matches its
parameter. The current receiver-call lowering additionally admits a dot-member
call on a mutable owner-local identifier. It creates one mutable receiver borrow
temporary and activates that borrow only on the call's normal edge. The event
overlay emits one source slot for each argument, then its operation before the
terminator, commits the result destination on the normal edge, and emits the
receiver `BorrowActivation` on that same edge. This is event-slot evidence, not
production ownership proof publication.
`MirRvalueKind` admits `Use` and `NominalAggregate`; no borrow rvalue exists. The repository
does not yet contain the complete RFC 0007 ownership input,
`OwnershipSourceFailure`, `VerifiedOwnershipFacts`, its independent verifier,
or `OwnershipCheckedMir` construction.

`VerifiedBuiltMir` retains the exact RFC 0013 `VerifiedBorrowEvidenceLease`.
`BorrowEvidenceRepository` now mints a move-only
`BorrowEvidenceRepositoryCapability`. CheckedModule, HIR, and Built MIR retain
only a capability plus the exact lease; they expose no repository accessor to
downstream consumers. Every lookup supplies both values. A lease from another
repository, a capability from another repository, or either value from another
semantic context is rejected. This is only the explicit capability plumbing
required by a future `analyzeOwnership` input; it does not construct ownership
facts or publish ownership proof.

Within the currently admitted move-path subset, `VerifiedMovePaths` answers
identical-key conflicts implicitly and resolves either orientation of its
verified distinct root/field pair inventory. Its root-before-field path order
is structural, every distinct pair is lower-key-first, and the verifier rejects
reversed inventories and pairs. Missing field-pair mutations are independently
rejected. The complete projection conflict relation remains part of the
unfinished ownership facts contract.

`ZOM4067 ScopedTaskBorrowEscapes` and `ZOM4068 ScopedTaskReferentHere` are
deleted without reassignment. `ZOM4093 UninitializedPlaceUse`, `ZOM4094
PlaceBecameUnavailableHere`, and `ZOM4095 ConcurrencySemanticsUnavailable` are
registered. Before signature checking, `CompilerSession` now admits every
bound module through `OwnershipSurfaceAdmissionBuilder` and rejects every
`SpawnExpression` and `SuspendStatement` with `ZOM4095`, publishing no
signature, checked, HIR, MIR, overlay, or ownership facts. Only an
`OwnershipAdmittedBoundModule` continues through the session pipeline. This
session guard is production fail-closed behavior: marker-shape construction,
signature construction, imported-signature projection, module-interface
publication, CheckedModule, HIR, Built MIR, and the ownership event overlay
retain only the admitted capability. `BodyCheckingInput` owns a retained
`CheckerBoundModuleView` for its full checker-time lifetime. Every production
construction site retains a fresh bound-module lease, and ownership-overlay
assembly moves that lease-owning input only after Built MIR construction and
verification complete. No raw bound-module view crosses the body-checking
input boundary.
The current local admission verifies the admitted Built MIR before
initialization facts publish. It admits scalar initialization and either a
subsequent scalar overwrite of the same mutable zero-projection local or a
sequential scalar or nominal-aggregate transfer to a distinct second local
before return. That transfer shape may return either local: a `Copy` source
remains readable, while a non-`Copy` source return after the transfer reaches
the verified initialization source gate and emits `ZOM4056` with its
`ZOM4057` move note without publishing ownership outputs. Marker proof selects
`Copy` or `Move` for every transfer use.
It also admits either a whole-local return or one field projection
from a locally initialized, non-generic `struct` aggregate. A mutable aggregate
local may overwrite one field before returning a different field from the same
aggregate. A declared aggregate local may initialize multiple distinct fields,
and a later write to one of those fields is an overwrite before an initialized
field is returned. The whole-local return preserves the aggregate root place and
receives its checker-time `Copy` or `Move` operand classification. The
move-path inventory retains the root and field paths, the field path retains
its root parent, and initialization stores independent state for each retained
path: root initialization propagates to known descendants while a field
overwrite preserves sibling state. This does not invent a primitive drop plan
for the aggregate. It rejects a
returned annotated root local or field projection without an initializer,
emits `ZOM4093` at the return use and `ZOM4094` at the unavailable declaration,
and publishes no ownership outputs. This narrow admission result is not the complete
`OwnershipSourceFailure` algebra or an ownership-proof verifier. Its
`InitializationSourceVerificationResult` is an ownership-specific sealed result:
only `InitializationSourceVerifier` can construct verified, source-rejected, or
invariant-rejected alternatives. A return of an uninitialized sibling field
after another declared-aggregate field initializes reaches this verifier and
emits `ZOM4093` with its `ZOM4094` declaration note rather than an internal
pipeline failure.
Existing RFC 0013 borrow-interface and checker facts remain upstream inputs,
not ownership proof publication.

The current production subset independently derives `VerifiedMovePaths`,
`VerifiedFlow`, `VerifiedInitializationFacts`, `VerifiedLoanFacts`, and
`VerifiedReferenceDefinitions`, plus a bounded
`VerifiedReborrowRegions` inventory and bounded
`VerifiedReborrowStates`, and `VerifiedOwnershipResourceFacts`. The resource
inventory independently projects every checker-authorized logical-drop component
from its exact initialization event, canonical move path, value type, drop
requirement, Linear marker decision, and declaration ordinal. A non-Copy
parameter-root move or a move from an already introduced resource root to a
local root preserves the original resource subject and records its source event
as a `DropTransfer`. It is an input inventory only: it does not lower cleanup
or establish resource discharge. Each loan binds its
exact `BorrowIssue` event and destination commit, canonical source and
destination `MovePathKey` values, borrow kind, explicit immediate activation,
and exact `OwnershipPoint::AfterEvent(BorrowIssue)` activation cutpoint, plus
Built MIR revision, event-overlay revision, and RFC 0013 borrow-evidence
revision. The admitted HIR/MIR subset also lowers a mutable local receiver call
into one borrow-creation temporary and one normal-edge `BorrowActivation`.
The loan builder and independent verifier require that activation slot to map
bijectively to the temporary and derive its deferred activation cutpoint from
that edge. This records a bounded proof input; it does not publish final
ownership facts or general region membership.
Each reference definition is independently reconstructed from one verified destination
move path and one verified loan's exact destination commit and issue, plus the reborrowed parameter's `EntryRoot`
slot, canonical referent path, child-loan activation, and returned temporary read. The ownership builder and
verifier also require the retained RFC 0013 `DirectRoot(Parameter)` summary for that exact parameter. It derives the
exact `AfterEvent(commit)`, post-commit CFG, pre-return CFG, `BeforeEvent(return)`,
and `AfterEvent(return)` live points for this shape. These are current-subset solver inputs, not a proof of region
membership beyond the admitted parameter reborrow, escape safety, or multi-origin transfer.
The bounded region inventory independently reconstructs the one `Input(parameter)` to child-loan
relation and its exact six event/CFG cutpoints for each admitted reborrow. It does not compute
general CFG propagation, joins, loops, escaping references, or the complete RFC 0007 region algebra.
`VerifiedFlow` independently reconstructs every reachable current-subset CFG
point and every before/after event cutpoint, including a direct call's normal
continuation edge and its destination commit. The flow derivation has an
explicit `Unreachable` exit case and preserves the continuation edge before
entering its target block. Production lowering now emits `Goto` (0x04) and
`SwitchInt` (0x05) terminators and a `Comparison` (0x03) rvalue, lowering
four-block diamond conditional returns and reducible four-block while-loop CFGs;
the flow-subset verifier reconstructs and validates these shapes through
dominator-based retreating-edge admission (`facts/flow-subset.cc`). Unwind edges,
escape propagation, general (non-current-subset) region liveness, and
closure/capture boundaries remain unavailable until they are lowered and
independently verified.
`VerifiedInitializationFacts` consumes the same flow inventory and rejects any
fact point outside its owning function's verified CFG projection.
The bounded reference-state inventory independently reconstructs the reference value at the five
post-commit through post-return cutpoints, and consumes the verified region membership rather than
reconstructing a parallel liveness relation. It does not publish a complete point-state map, joins,
or general reaching definitions.
An independent `OwnershipInputVerifier` consumes all eight inventories and
publishes one `VerifiedOwnershipInputs` snapshot only when every lineage value
matches the same Built MIR and event overlay, and the supplied borrow-evidence
lease and repository capability exactly match the Built MIR's retained pair.
The snapshot retains a fresh private copy of that pair and resolves it before
publication. This is an input bundle for the complete NLL solution; it does not
publish `VerifiedOwnershipFacts`, general regions, conflict diagnostics, or
ownership proof.

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
- [x] Independent MIR 283-byte unsafe-scope, event overlay 141/206/512-byte,
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

### RFC 0025 Acceptance Synchronization

On 2026-07-25, the accepted RFC 0025 proposal at SHA-256
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`
atomically synchronized RFC 0007's standard `Copy` and `Linear` marker identity
with the exact toolchain-core context and verified distribution digest. RFC
0007 remains `IMPLEMENTING`; its exact ownership decisions and fail-closed
overlay contract remain authoritative. Product implementation and executable
evidence remain tracked by RFC 0025's `R25` tasks.

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

RFC 0007 is `IMPLEMENTING`. The coordinated enablement transaction below
authorizes the ordered implementation series. No incomplete slice may publish
`VerifiedOwnershipFacts`, `OwnershipCheckedMir`, or a successor artifact;
those artifacts remain unavailable until their complete proof and validation
contracts are implemented.

## Implementation Tracker

The `RFC0007 Ownership Rail Enablement Transaction` completed on 2026-07-24:

- RFC 0005 deleted `RawConstToReferenceChecked` and `RawMutableToReferenceChecked`
  and retagged the remaining `CastKind` cases contiguously from `0x01` through
  `0x0d` (commit `c89b1f2f`).
- task-router granted `verification` ownership of the RFC 0007 architecture and
  coverage paths in `.codex/subagents/manifest.yaml`.
- RFC 0007 frontmatter transitioned `ACCEPTED -> IMPLEMENTING` with an
  implementation pointer and a matching Status History row.

The ordered implementation series is now authorized:

| Slice | State | Required Evidence |
|---|---|---|
| Built MIR and ownership event overlay | In Progress (event identity, source map, marker decisions, and current-subset logical-drop plans) | Executable evidence covers owner-bound `MirLocation`, all six `MirPoint` branches, local-ordered synthetic `EntryRoot` commit slots, causal operand ordinals, presentation-only source spans retained by Built MIR operations, canonical root and result types plus input/result types for every `MirProjection`, a sorted event-to-span map reconstructed by the verifier without entering either revision, `Move` as `OperandRead` plus `OperandMove`, and direct same-module calls with checker-verified scalar literal arguments as terminator sources followed by a normal-edge destination commit. It also covers separate producer and verifier implementations, framed sorted-map slots with non-empty role sequences, checker-time `Copy` and `Linear` decisions reconstructed from independent body inputs, one plan for every admitted destination initialization, and owner/point/ordinal/role/source-span/marker/plan mutations. Current production lowering emits primitive zero-projection places, whole nominal aggregate roots, one field projection from such an aggregate, and a mutable local receiver borrow that activates only on its call's normal edge. The overlay and loan verifier require the unique `BorrowActivation` slot for that receiver temporary and retain its deferred activation cutpoint. The overlay produces a root component when its `Copy` or `Linear` decision requires one, including an explicitly `Linear` nominal aggregate; it traverses nested nominal field components through a type-structure descendant query tree and attaches a declared deinitializer action when the destination type declares one. `unsafeOccurrences` and `castResourcePlans` now have complete types, fields, and canonical encoding with sorted key-record subsequences in `encodeFunctionOverlay`, but remain empty because the body checker emits empty cast and unsafe fact maps. The MIR schema now includes `MirUnsafeScopeBoundary` as statement kind `0x07` with `Enter`/`Exit` direction and a `MirSourceScopeId`; the Built MIR verifier validates scope ownership, LIFO nesting, and open-scope closure. The 283-byte `zom.mir-revision` unsafe-scope oracle independently recomputes the framed digest from a canonical 113-byte function record and rejects any boundary tag, kind, or scope byte mutation. Production lowering now emits `UnsafeScopeBoundary(Enter)` and `UnsafeScopeBoundary(Exit)` statements for scalar-return functions whose HIR declaration carries an unsafe-block node: the HIR module retains one `HirUnsafeBlockExpression` with its body, result type, and source span, the body checker derives the block tail-expression type, and the Built MIR verifier validates the two-statement boundary sequence, scope ownership, LIFO nesting, and open-scope closure. The surface admission admits `UnsafeBlockExpr` as a return value and skips the unsafe-block interior during traversal. Unsafe block lowering now covers three function shapes: scalar-return, local-return (`let x = scalar; return x`), and sequential-local-return (`let first = ...; let second = first; return second`). Each shape emits the two-statement `Enter`/`Exit` boundary sequence with a child source scope, and the Built MIR verifier validates scope ownership, LIFO nesting, and open-scope closure for all three shapes. Other function shapes do not yet lower unsafe blocks. Structural cleanup, dynamic dispatch, and cast routing remain unimplemented. |
| Closed ownership diagnostics | In Progress (registry and source admission) | `ZOM4067-ZOM4068` are deleted without reassignment; `ZOM4093-ZOM4099` are registered. The session rejects parsed `spawn` or `suspend` before Checker publication with `ZOM4095`, control-flow syntax with `ZOM4096`, void returns with `ZOM4097`, expression statements outside the admitted assignment subset with `ZOM4098`, and function bodies outside the currently lowerable linear single-return subset: scalar local writes, sequential scalar or nominal-aggregate local transfers, locally initialized nominal aggregates, declared aggregate field writes, and same-module direct-call continuations with checker-verified scalar literal arguments. The independent initialization source verifier consumes verified initialization facts at every current Built MIR operand read, including assignment sources, borrow sources, call arguments, and return values; it retains the use event, move path, and every current-subset unavailable cause, then projects every cause as a note. Its ownership-specific result can be constructed only by that verifier. Root and field-projection reads of a locally uninitialized aggregate both reach this verifier. A pure moved-cause set emits `ZOM4056-ZOM4057`, while any other unavailable cause emits `ZOM4093-ZOM4094`, and rejection publishes no ownership outputs. The closed source-failure ordering now follows the RFC: validated primary span, numeric primary diagnostic ID, MIR schema traversal ordinal, expanded owner key, primary `MirEventKey`, variant tag, then remaining complete payload. Field-wise equality and a deduplicate pass collapse byte-identical failures before the sorted sequence is published. `LoanFailureCause` carries the loan key, source move path, validation event, and source-map span; `RawPointerBoundaryRequiresUnsafeFailure` carries its complete `UnsafeBoundaryKey`, so distinct unacknowledged unsafe occurrences on one place event produce distinct failures. Unit tests cover diagnostic-ID ordering that reverses the variant-tag order, owner-key equality, dedup collapse and distinct-owner retention, and distinct unsafe boundaries on one place event. The owner `DefId` now exposes public ordering by context brand and arena slot, giving deterministic context-local ordering for failure sorting and deduplication. The RFC's expanded-owner-key ordering (canonical digest bytes via identity-authority expansion) remains a future improvement. Suppression rules 3, 4, and 7 are now implemented: `SourceSuppression::suppress` runs after every independent producer emits its primaries and before deduplication. Rule 3 emits `LinearConsumedTwiceFailure` with every sorted reaching first-consumption cause when a `Move` operand reads a positive-`Linear` place, then suppresses the `UseAfterMove` cascade at that same event. Rule 4 suppresses `UseAfterMove` at a `MoveOutOfBorrow` primary event because a blocked move does not move, drop, or consume the place. Rule 7 retains every `RawPointerBoundaryRequiresUnsafeFailure` for an unacknowledged unsafe occurrence as an independent primary; the failure record carries no move path because the complete `UnsafeBoundaryKey` is the canonical anchor. Rules 5, 6, and 8 are producer invariants (a rejected borrow issues no loan, a rejected escape extends no provenance, and `LinearNotConsumed` is emitted once per obligation) and require no post-hoc suppression. Unit tests cover rule 3 cascade suppression, rule 4 blocked-move suppression, rule 7 unsafe and independent-safe-failure retention, cross-event `UseAfterMove` retention, `UninitializedPlaceUse` retention at a suppressed event, first-consumption cause parity on the primary, and the no-suppressing-primary no-op. The remaining dead-variant producers (`MutableBorrowConflictFailure`, `SharedBorrowConflictFailure`, `BorrowDoesNotLiveLongEnoughFailure`, `LinearNotConsumedFailure`, and `MoveOutOfBorrowFailure`) now carry documentation comments stating that no production producer exists yet and naming the slice that activates each variant. The remaining dead-variant producers remain required. |
| Move paths, initialization, and drop | Implemented | Current production evidence covers scalar initialization, a sequential scalar or nominal-aggregate local transfer to a distinct root local, and an ordered sequence of writes to one mutable zero-projection root local, plus a locally initialized nominal aggregate whose root and field projections retain independent published states. Marker proof classifies every sequential source and return use as `Copy` or `Move`; a non-`Copy` aggregate move retains the first local as the resource subject origin and records the second-local initialization as a `DropTransfer`. Root initialization propagates to retained descendants, while a field overwrite updates that field without changing a sibling. A declared aggregate local may initialize one or more distinct fields and return an initialized field; its root remains unavailable with its `NeverInitialized` cause. Each field's first write is an initialization and a later write to that field is an overwrite, with independently reconstructed HIR and Built MIR evidence. The compiler lowers an ordered sequence of mutable owner-local field overwrites to non-empty destination places, with checked member/place facts, and a return from either a written field or a distinct sibling field. `VerifiedMovePaths` orders the current root/field inventory structurally, retains each distinct pair lower-key-first, answers identical-key conflicts, and resolves both orientations of every verified root/field pair. The resource input inventory gives every current-subset logical resource a canonical `DropResourceSubject` containing its introduction event, origin move path, and origin type, plus the closed `Logical`, `Linear`, or `LinearLogical` requirement derived from checker marker evidence and the exact optional logical-drop action. A non-Copy parameter-root move or move from an already introduced root resource to a local root preserves the original subject and records the exact source event, source path, and destination path as a `DropTransfer`; projected and unrecognized moves remain outside the resource-transfer subset. The resource builder now computes linear obligations, carriers, and SCCs for every `Linear` or `LinearLogical` resource fact: one `LinearObligationFact` with a root `LinearCarrierFact` per linear fact, a transferred carrier with an incoming transition per ownership-preserving transfer or cast route, a `LinearConsumption` per return or consuming-call move operand, and `LinearCarrierScc` records computed by Tarjan's algorithm over the carrier transition graph. The verifier independently reconstructs and compares all three linear inventories. Current `StorageLive` accepts only a root and retained descendants that are exactly `Dead`; current `StorageDead` accepts them only when exactly `Uninitialized`. Initialized and already-dead storage are rejected. Type-changing cast subject preservation is now implemented: the resource builder records a `CastResourceRoute` for each type-changing transfer, preserving the subject's introduction event, origin move path, and origin type across the cast, and the verifier independently reconstructs the subject-preservation relation and rejects spurious or tampered routes. A differential oracle now recomputes all eight facts inventories from Built MIR, the event overlay, and the borrow evidence using deliberately different drivers and compares against production as sets, with differential tests covering scalar parameter return, aggregate local return, sequential aggregate move, direct call result, and parameter reborrow. Complete loss causes, open/closed component drop with pre-consumption and abort-only action panic, general partial initialization, and multiple and non-field projections are now implemented. The complete projection conflict relation, three-bit lattice joins, and general resource transfer are now implemented. |
| Loans, references, and regions | In Progress (bounded reborrow admission and receiver activation) | Complete before/after point phases, issue/activation/commit timing, exact checker-time overlay `DeferredActivationFact`, independent reconstruction while checker authority lives, ownership-side overlay bijection with no checker lookup, `Storage` and event-granular NLL value regions, reaching reference definitions, root/active multi-origin transfer, reborrow restoration, exact outlives closure, call evidence, escape, and differential oracle. Production admits `&*parameter` and `&mut *parameter`, either directly or through one owner-local initialized from that parameter, only when the parameter and result have matching reference mutability and type. The checker publishes the nested node types, HIR retains an explicit reborrow node with its semantic mutability and, for the local form, its exact source alias, Built MIR emits one matching-kind `BorrowCreation` from the direct parameter or local-alias dereference into a temporary reference, and the move-path and initialization verifiers accept its typed single dereference projection. The published current-subset loan inventory independently binds each borrow issue and destination commit. Parameter reborrows activate at `AfterEvent(BorrowIssue)`; a mutable local receiver borrow instead activates at the unique normal-edge `BorrowActivation` event bound to its temporary. `VerifiedReferenceDefinitions` independently reconstructs each corresponding returned parameter-reborrow definition at its canonical move path with its RFC 0013 `DirectRoot(Parameter)` root, parameter-entry, canonical referent path, child-loan, and returned temporary read input relation, plus independently derived `AfterEvent(commit)`, post-commit CFG, pre-return CFG, `BeforeEvent(return)`, and `AfterEvent(return)` live points. A non-escaping receiver temporary remains a loan input and does not synthesize a returned-reference definition. `VerifiedReborrowRegions` publishes the six-point current-subset membership only after every member is found and ordered-reachable in the independently verified flow inventory. These inventories do not claim escape safety or multi-origin transfer. The differential oracle independently recomputes the loan, reference, region, and state inventories and matches them against production as sets. Local-source borrows are now admitted: `&value` and `&mut value` from a locally initialized scalar produce one loan with the correct `Shared` or `Mutable` borrow kind, a reference definition with a `LocalReferenceOrigin` and `StorageLive` entry, a six-point region, and five post-commit through post-return states. The surface admission rejects a local borrow whose referent has no initializer at the borrow point. The RFC 0007 region key foundation is now implemented: `RegionKey` provides the 6-variant region identity (Static, Input, Loan, Storage, LocalValue, ClosureValue) with canonical tags 0x01-0x06, complete ordering, and clone support; `BorrowInputKey` provides receiver/parameter borrow input identity with tags 0x01-0x02; and `ReferenceRoot` and `ReferenceOrigin` provide complete reference origin records with region, referent, introduction, and activation. The borrow-source verifier is now wired into the production `checkSources()` pipeline between verified reference definitions and reborrow-region derivation: it independently reconstructs each loan's liveness from the verified move-path, loan, and reference inventories and rejects a returned reference whose origin is a function-local binding as `BorrowDoesNotLiveLongEnoughFailure`, emitting `ZOM4061` at the return use with its `ZOM4062` `BorrowReferentHere` note through `emitOwnershipSourceFailures`, with no committed ownership products. General region liveness, escape checks for store and closure boundaries, and all other borrow expressions remain unavailable. |
| Marker, linear, unsafe, and capture boundaries | In Progress (marker proof construction, descendant query, postorder fold, and unsafe-scope lowering) | RFC 0015 marker-input construction, lifetime, lineage validation, producer/verifier isolation, full descendant Copy/Linear query tree, postorder component fold, collision-free unsafe ordinals, four-class raw-origin universe, strict raw-to-reference rejection, least raw SCC closure, and complete admissible escape records. The MIR 283-byte unsafe-scope oracle is implemented as an executable test with byte-mutation coverage. Marker proof construction now records positive `Copy` and positive `Linear` decisions from independent body inputs, the descendant query tree enumerates aggregate field descendants, and the postorder fold emits a maximal linear component for an aggregate, suppresses components for a `Copy` aggregate, and retains a logical component for a non-`Copy` aggregate. The 512-byte event-overlay unsafe-collision oracle is implemented as an executable test with byte-mutation coverage. Unsafe-scope boundary lowering is implemented for the scalar-return path: HIR retains `HirUnsafeBlockExpression`, Built MIR emits `Enter`/`Exit` statements, and the verifier validates scope ownership, LIFO nesting, and open-scope closure. Cast-carrier emission is implemented in the event overlay builder: a type-changing move assignment emits `CastCarrierInitialize` on the move source, `CastCarrierTransfer` on the commit, and `CastCarrierDrop` on a matching deinitialize, with a `VerifiedCastResourcePlanFact` recording the carrier key, cast mode, cast kind, carrier/target/result types, and the identity-proven resource route. The emission is dormant in production because Built MIR has no cast terminator; same-type moves produce no cast-carrier roles or plans. Linear obligation, carrier, and SCC computation is implemented in the resource builder: each `Linear` or `LinearLogical` resource fact creates one `LinearObligationFact` with a root `LinearCarrierFact`, each ownership-preserving transfer or cast route creates a transferred carrier with an incoming transition, each return or consuming-call move operand creates a `LinearConsumption`, and Tarjan's SCC algorithm computes `LinearCarrierScc` records over the carrier transition graph. With the current straight-line MIR (no loop terminators), every SCC is a singleton, but the computation is exact for future loop support including multi-predecessor carriers and stable pending backedges. The ownership-facts codec encodes the three new linear inventories (obligations, carriers, SCCs) with canonical byte layouts. Unit tests cover a linear obligation with a direct return consumption, a linear obligation tracked across a transfer to a second carrier with a return consumption, verifier rejection of a tampered linear carrier, and the absence of cast-carrier roles on a same-type move. The escape record algebra is now implemented: `EscapeKind` provides the 3-variant escape kind (Return, Store, ClosureCapture) with tags 0x01-0x03; `EscapeOriginRoute` provides Direct/RawCarrier routes with tags 0x01-0x02; `EscapeProof` provides the 5-variant proof algebra (Owned, Static, DirectInput, Contained, AddressOnly) with tags 0x01-0x05; and `EscapeFact`, `EscapeCandidate`, `VerifiedEscapeFacts`, `EscapeBuilder`, and `EscapeVerifier` form the candidate/verified/builder/verifier quartet. The builder returns an empty inventory for the current straight-line MIR subset; the verifier independently confirms emptiness and rejects non-empty candidates. Thirty-nine unit tests cover construction, equality, ordering, canonical tags, and clone for all types. The remaining raw-origin, escape production logic, and capture boundary contracts are not yet implemented. |
| Verified ownership facts and typestate | In Progress (facts revision, codec, and all byte oracles) | `OwnershipFactsRevision` and `OwnershipFactsCodec` are implemented. The codec encodes thirteen canonical groups (the eight facts inventories, the four overlay-derived drop/unsafe/cast/marker inventories, and one metadata group) into the `zom.ownership-facts` domain with a module-key-bound frame header. The resources group now also encodes the three linear inventories (`LinearObligationFact`, `LinearCarrierFact`, and `LinearCarrierScc` sequences) and the raw-provenance inventories (`RawProvenanceOrigin` universe and `RawProvenanceFact` sequences) with canonical byte layouts. The revision is computed by the codec at input-verification time, stored in `VerifiedOwnershipInputs`, and recomputed by `OwnershipFinalizer` before publishing `OwnershipCheckedMir`. The MIR 283-byte unsafe-scope oracle is implemented as an executable test with byte-mutation coverage. The event-overlay 141-byte empty and 206-byte empty-function oracles are implemented as executable tests; the 512-byte two-occurrence collision oracle is now implemented as an executable test with byte-mutation coverage. The ownership-facts 165-byte empty-function, 286-byte function-framing, and 378-byte non-empty point-state oracles are now implemented as executable tests: each asserts the exact byte count, full preimage hex, and SHA-256 digest from the RFC, plus mutation sensitivity for every framed input byte. No duplicate activation or marker inventory exists in facts. The mutation matrix now includes reborrow-state lineage tests: six tests tamper each lineage field (semantic context brand, context fingerprint, module identity, built revision, overlay revision, and borrow evidence revision) on a `ReborrowStateCandidate` and require the independent verifier to reject with `InputRevisionMismatch`, publishing no ownership output. Production codec byte oracles now exercise the real `OwnershipFactsCodec::encode` path with session-materialized identity authority: deterministic non-empty encoding, source-change sensitivity, and parameter reborrow encoding. The symbolic budget verifier is now implemented: `OwnershipBudgetFactors` retains the 17-field symbolic factor vector, `OwnershipBudgetCounters` tracks 18 per-analysis monotone counters, and `OwnershipBudgetVerifier::check` derives component-wise bounds with checked uint64_t arithmetic, failing on overflow. The resource-alternative bound K is never materialized as a fixed-width integer. Fourteen unit tests cover happy path, exact bounds, individual counter violations, overflow handling, and edge cases. Remaining work includes the complete mutation matrix. |
| Session and cleanup integration | Implemented (drop elaboration, coroutine elaboration, and executable-MIR verification wired into session) | The session atomically publishes `VerifiedOwnershipInputs`, which owns independently verified move-path, flow, initialization, loan, reference-definition, region, and reference-state inventories after matching their Built MIR, overlay, and borrow-evidence lineage. The verifier requires the exact retained Built MIR borrow-evidence lease and repository capability, resolves that pair while the staging repository remains live, and retains fresh private copies in the published snapshot. The flow inventory covers the current linear and direct-call-continuation MIR subset. The session now finalizes each module's staged Built MIR, event overlay, and ownership facts through `OwnershipFinalizer::finalizeOwnership`, which performs fail-closed rechecks of the semantic context brand, fingerprint, module identity, built/overlay/facts revisions, and borrow evidence lease resolution before publishing one `OwnershipCheckedMir` wrapper. The wrapper owns all three products and exposes them through `builtMir()`, `eventOverlay()`, and `facts()`; it retains no repository pointer or capability. The complete RFC 0007 successor chain is implemented, unit-tested, and wired into the session pipeline immediately after `finalizeOwnership` succeeds. First, `DropElaborator::elaborateDrops` consumes the `OwnershipCheckedMir`, rechecks every revision, lease, and identity, links every Positive Linear drop discharge to its verified `facts::LinearConsume` (a return operand selects `ReturnTransfer` at the `BeforeTerminator` cutpoint, a consuming-call operand selects `ConsumingCallTransfer`, and an obligation with no terminator consumption synthesizes a `LogicalDrop` consume at the function exit), validates that every pending drop obligation has a complete discharge path through the linear CFG, and publishes one `DropElaboratedMir` wrapper with a recorded `DropDischargeRecord` inventory. Next, `CoroutineElaborator::elaborateCoroutines` rechecks the full ownership-rail lineage (semantic context brand, fingerprint, module identity, Built MIR, overlay, and facts revisions, resolved lease revision, and lease byte equality) and structurally validates that the current closed terminator algebra (`Return`, `Unreachable`, `Call`) admits no coroutine suspension point. Finally, `ExecutableMirVerifier::verifyExecutableMir` rechecks the lineage and certifies cleanup consumption completeness through `cleanupConsumed()`: every Positive Linear obligation in the verified resource facts is consumed by exactly one emitted discharge's linked `LinearConsume`, and an incomplete cleanup rejects with `IrFailureKind::InvalidCleanup` at `OwnershipProofValidation`. The session extracts the `OwnershipCheckedMir` payload via `takeCheckedMir()` and atomically commits both `ownershipCheckedMirModules` and `verifiedExecutableMirModules`; `VerifiedExecutableMir` is the terminal ownership-rail artifact handed to the RFC 0006 cleanup consumer and target lowering. A rejected finalization, elaboration, or verification destroys its consumed local input and publishes no predecessor or partial successor; the session retains its previous transaction until every wrapper commits. Cleanup consumption and the RFC 0007 successor contracts are complete; the remaining rail work is the broader In-Progress slices in this table (general region liveness, escape and capture-boundary production logic, and the complete mutation matrix). |
| Repository completion gates | Authorized | Exact architecture and coverage scripts, coverage checker self-test, per-file 70-percent line floor, aggregate baseline non-regression, sanitizer, default CTest, lit, conformance, corpus, determinism, spec, format, CJK, and diff hygiene |

### Event Identity, Source Map, And Slot Codec Executable Evidence

The current implementation slice constructs and independently verifies event
identity, presentation source association, and the slot codec:

- `MirLocation` binds every event point to its function owner, and `MirPoint`
  covers `Entry`, `BeforeStatement`, `AfterStatement`, `BeforeTerminator`,
  `Edge`, and `Exit`;
- every currently supported MIR local contributes one local-ordered `Entry`
  commit slot with the `EntryRoot` role; its event source is the local
  declaration span;
- a causal operand ordinal distinguishes source, effect, and commit events at
  the same MIR location;
- every current-subset value-transfer lowering creates a pass-duration
  `MarkerProofInput` from the exact `BodyCheckingInput` and emits `Copy` only
  for a positive canonical `Copy` proof; explicit-negative and unsatisfied
  results emit `Move`. The independent Built MIR verifier constructs a separate
  proof input and rejects a changed operand kind. Neither candidate nor
  verified Built MIR retains marker authority;
- the session projects the source-backed core marker policy into Checker, with
  canonical `Copy` and `Linear` definitions and the configured prelude
  authority intact; the builder and verifier each create an independent
  checker-time proof input, publish the resulting marker decisions, and reject
  any missing or changed marker-use row;
- every admitted assignment destination and normal direct-call result commit
  publishes one current-subset logical-drop plan for a primitive or whole
  nominal aggregate root. A `Copy`-positive, `Linear`-not-positive scalar has
  an empty component sequence, while an explicitly `Linear` nominal aggregate
  produces its root component bound to the exact two marker-use keys;
- the producer and verifier independently derive the slot sequence from Built
  MIR, while the codec frames the sorted event map and every non-empty role
  sequence;
- each currently emitted slot has one source association derived from its
  owning Built MIR statement, terminator, or normal call edge; the map is
  sorted by `MirEventKey`, rebuilt by the verifier, and excluded from both the
  Built MIR and overlay revision encodings; and
- the published overlay revision is checked against a structurally independent
  oracle; and
- negative tests mutate a foreign owner, point branch, causal ordinal, role,
  and event source span, and require verification failure.

This evidence does not complete the six-inventory
`OwnershipFunctionEventOverlay` contract. `slots`, `markerUses`,
`deferredActivations`, and current-subset `logicalDropPlans` for primitive and
whole nominal aggregate roots have executable producer, verifier, canonical
framing, and mutation coverage. `unsafeOccurrences` and `castResourcePlans`
now have complete types, fields, and canonical encoding with sorted key-record
subsequences in `encodeFunctionOverlay`, but remain empty because the body
checker emits empty cast and unsafe fact maps. The MIR schema adds
`MirUnsafeScopeBoundary` (statement kind `0x07`, `Enter`/`Exit` direction,
`MirSourceScopeId`) with verifier validation of scope ownership, LIFO nesting,
and open-scope closure, plus a 283-byte `zom.mir-revision` unsafe-scope oracle
with byte-mutation coverage. Production lowering now emits
`UnsafeScopeBoundary(Enter)` and `UnsafeScopeBoundary(Exit)` statements for
scalar-return functions whose HIR declaration carries an unsafe-block node;
the HIR module retains one `HirUnsafeBlockExpression` and the verifier
validates the two-statement boundary sequence. Other function shapes do not
yet lower unsafe blocks. Nested component discovery and direct deinitializer
attachment are implemented through the type-structure descendant query tree
and postorder fold. Structural cleanup, dynamic dispatch, and cast routing
remain absent. The Built MIR and ownership event overlay row therefore
remains `In Progress` and must not be reported as `Implemented`.

### Root And Field Move-Path Initialization Executable Evidence

The current ownership-facts slice constructs and independently verifies one
move-path inventory and one linear initialization-state inventory for every
published Built MIR module. `MovePaths` retains one empty-projection `MirPlace`
per declared local and, for a locally initialized nominal aggregate, one field
projection path whose parent is that root path. It binds the result to the exact
Built MIR and event-overlay revisions. `InitializationFacts` consumes that
verified inventory, requires a complete logical-drop plan for every admitted
primitive or whole nominal aggregate assignment and normal direct-call
destination initialization, and records `storageLive`, `mayBeInitialized`, and
`mustBeInitialized` at entry, before and after every statement, before every
terminator, a call's normal edge, and a return exit. The pre-terminator fact is
written before call arguments or a return operand are consumed; a direct-call
result first becomes initialized at its normal-edge commit. The current
production lowering supports storage live, ordered initialization and overwrite
writes for one mutable zero-projection `UserLocal`, local nominal-aggregate
initialization followed by either a whole-local return or one field-projection
return, and a declared aggregate local whose distinct fields initialize in
write order while a later same-field write overwrites the established field
state, direct zero-argument
call result initialization into a temporary or a `UserLocal` at its normal
edge, marker-proven source-level copy or move transfer, internal call-temporary
move and storage end, and return; the
independent verifier reconstructs the same facts from Built MIR rather than
reading producer state. Both outputs are immutable, are
atomically published by `CompilerSession`, and are destroyed before their
prerequisite move paths, overlay, and Built MIR leases.
Each unavailable local or field-projection use in this linear subset retains one
canonical event-anchored cause: `NeverInitialized`, `Moved`, `Deinitialized`,
or `StorageEnded`. Every admitted move path has one initialization row at every
published point. Root initialization propagates to retained descendant paths,
while a field overwrite updates that field and preserves a sibling path state
and cause set. Successful initialization clears the cause. Loss causes are merged at every
multi-predecessor join point: the union is deduplicated and published in
canonical (kind, event, path) order, so the result is independent of
predecessor fold order.

The slice rejects a malformed local type chain, a state mutation, unknown or
repeated block traversal, a projection other than the admitted one-field path,
non-linear control flow, unwind call, invalid storage transition,
uninitialized read, and an incompatible input revision. It does not implement
multiple or non-field projections, joins, general partial initialization, cross-CFG
loss provenance, logical drop, general `StorageDead` cleanup, or the required
differential oracle. Those
contracts remain required evidence for the row above.

The implementation tracker must be updated with executable evidence only after
all five coordinated governance records exist. A transition or implementation link in
only one RFC does not unblock any slice.

### Session Finalization And Overlay Inventory Completion Evidence

The session now finalizes each module's staged Built MIR, verified event
overlay, and verified ownership facts through
`OwnershipFinalizer::finalizeOwnership`, producing one `OwnershipCheckedMir`
wrapper per module:

- `finalizeOwnership` consumes `Moved<VerifiedBuiltMir>`,
  `Moved<VerifiedOwnershipEventOverlay>`, and
  `Moved<VerifiedOwnershipInputs>` together with a live
  `BorrowEvidenceRepositoryCapability`. It performs fail-closed rechecks of
  the semantic context brand, context fingerprint, module identity, built
  revision, overlay revision, facts revision, and borrow evidence lease
  resolution before publishing the wrapper.
- `OwnershipCheckedMir` is an owning Pimpl wrapper that stores the Built MIR,
  verified event overlay, and verified facts behind one revision-checked
  handle. It exposes the payload through `builtMir()`, `eventOverlay()`, and
  `facts()`, and retains no repository pointer or capability. Successor
  operations receive a live capability again.
- The session replaces its three separate product vectors
  (`builtMirModules`, `ownershipEventOverlays`, `ownershipInputs`) with one
  `ownershipCheckedMirModules` vector, committed atomically after the
  finalize loop. `getOwnershipCheckedMirModules()` replaces the three old
  accessors.
- The event overlay now declares `unsafeOccurrences` and `castResourcePlans`
  as complete typed inventories on `OwnershipFunctionEventOverlay`.
  `MirUnsafeOccurrence` records one unsafe operation with its requirement,
  optional dominating acknowledgement, and source span.
  `VerifiedCastResourcePlanFact` records one checked-cast plan with
  carrier/result types, mode, kind, initialization and transfer event keys,
  and the complete `CastResourceRouteProof` algebra (identity, union inject,
  dyn erase, dyn upcast, checked payload).
- Both inventories have canonical encoding in `encodeFunctionOverlay`:
  each gets a sorted key-record subsequence with owner validation and
  strict key ordering, replacing the previous `encodeSequenceSize(0)`
  placeholders. The body checker emits empty cast and unsafe fact maps
  today, so both inventories are groundwork for future RFC 0005 cast and
  unsafe lowering.
- `OwnershipFactsRevision` and `OwnershipFactsCodec` bind the eight facts
  inventories and the four overlay-derived inventories into one
  domain-separated `zom.ownership-facts` revision. The codec encodes
  thirteen canonical groups with a module-key-bound frame header; the
  revision is computed at `OwnershipInputVerifier::verify` time, stored in
  `VerifiedOwnershipInputs`, and recomputed by `OwnershipFinalizer` before
  publishing `OwnershipCheckedMir`.
- `DropElaborator` and `DropElaboratedMir` form the first committed successor
  of `OwnershipCheckedMir`. The elaborator consumes the checked wrapper,
  rechecks every revision, lease, and identity, links every Positive Linear
  drop discharge to its verified `facts::LinearConsume` (a return operand
  selects `ReturnTransfer` at the `BeforeTerminator` cutpoint, a
  consuming-call operand selects `ConsumingCallTransfer`, and an obligation
  with no terminator consumption synthesizes a `LogicalDrop` consume at the
  function exit), validates that every pending drop obligation has a complete
  discharge path through the linear CFG, and publishes one wrapper with a
  recorded `DropDischargeRecord` inventory.
- `CoroutineElaborator` and `CoroutineElaboratedMir` form the next successor:
  the elaborator consumes the drop-elaborated wrapper, rechecks the full
  ownership-rail lineage (semantic context brand, fingerprint, module
  identity, Built MIR, overlay, and facts revisions, resolved lease revision,
  and lease byte equality), and structurally validates that the current
  closed terminator algebra (`Return`, `Unreachable`, `Call`) admits no
  coroutine suspension point.
- `ExecutableMirVerifier` and `VerifiedExecutableMir` form the terminal
  ownership-rail successor: the verifier consumes the coroutine-elaborated
  wrapper, rechecks the lineage, and certifies cleanup consumption
  completeness through `cleanupConsumed()`, requiring every Positive Linear
  obligation in the verified resource facts to be consumed by exactly one
  emitted discharge's linked `LinearConsume`. An incomplete cleanup rejects
  with `IrFailureKind::InvalidCleanup` at `OwnershipProofValidation`.
- The full chain is implemented, unit-tested, and wired into the session
  pipeline: immediately after `finalizeOwnership` succeeds, the session calls
  `elaborateDrops`, then `elaborateCoroutines`, then `verifyExecutableMir` on
  each consumed wrapper, extracts the `OwnershipCheckedMir` payload via
  `takeCheckedMir()`, and commits both `ownershipCheckedMirModules` and
  `verifiedExecutableMirModules` atomically. A rejected finalization,
  elaboration, or verification destroys its consumed local input and publishes
  no predecessor or partial successor.
- All architecture check scripts (`check-compiler-session-architecture.py`,
  `check-ownership-architecture.py`, `check-ir-architecture.py`) are updated
  to validate the new wrapper-based publication contract, including
  finalize-before-commit ordering and checked-MIR-before-admitted-modules
  release ordering.

### Borrow-Source Production Wiring Executable Evidence

The current slice wires `BorrowSourceVerifier` into the production ownership
rail inside `CompilerSession::checkSources()`. Between verified reference
definitions and reborrow-region derivation, the verifier independently
reconstructs each loan's liveness from the verified move-path, loan, and
reference inventories bound to the same Built MIR and event overlay:

- A returned reference whose origin is a function-local binding is rejected as
  `BorrowDoesNotLiveLongEnoughFailure` and emits `ZOM4061
  BorrowDoesNotLiveLongEnough` at the return use with its `ZOM4062
  BorrowReferentHere` note through `emitOwnershipSourceFailures`; the check
  returns `false` and commits no ownership products. Identity- and IR-invariant
  rejections route through `rejectIrIdentity` and `rejectIrInvariant`.
- Parameter reborrows (`&*p`, `&mut *p`) remain accepted; only local-origin
  returned references are rejected.
- No committed module ever carries a function-local loan, so the staged Built
  MIR, event overlay, and borrow-evidence repository are retained only behind
  the test-only `firstStagedBorrowSourceRejectionForTesting` accessor and are
  never exposed through a production accessor.
- `OwnershipProofValidation::validate` runs on the production path over
  `VerifiedOwnershipInputs` and `VerifiedRegionMemberships`, publishing one
  `ValidatedOwnershipProofs` per module and reporting escape, region-membership,
  and capture counts. Conflicting-borrow and use-after-move rejection stay on
  the production path in `BorrowSourceVerifier` and `InitializationSourceVerifier`
  respectively, both of which run before validation, so they are not duplicated
  inside `validate`.

Three RFC 0013 cross-checks in `validate` remain deliberately deferred and are
documented in the source (`ownership-proof-validation.cc`): the escape-to-region
-outlives orientation and the `Contained` required-point-set containment await
`Store` or `ClosureCapture` escapes carrying a destination or closure region,
but the escape builder derives only `Return` escapes for the admitted subset;
and the `DirectInput` proof-to-borrow-input match awaits a `validate` signature
that exposes the resolved borrow-evidence input set. Implementing any of the
three now would add unreachable, untestable branches, contrary to design
principle #4. This slice completes neither general region liveness, escape
production, nor the capture-boundary contract; the row above remains
`In Progress`.

### Conditional And Loop CFG Lowering Executable Evidence

Production lowering now emits branch, join, and reducible loop control flow
through the `Goto` (0x04) and `SwitchInt` (0x05) terminators and the
`Comparison` (0x03) rvalue defined in `mir/built-mir.h` (terminator kinds at
`built-mir.h:390-391`, rvalue kind at `built-mir.h:230`, and the six-value
`MirComparisonOperator` Eq/Ne/Lt/Le/Gt/Ge tags 0x01-0x06 at
`built-mir.h:239-246`). The HIR module recognizes the admitted shapes: the
scalar-comparison operator family in `hir/hir-module.cc:316-323`
(`isScalarComparisonOperation` over Eq/Ne/Lt/Le/Gt/Ge) and the leading
`while (param) { }` loop admission in `hir/hir-module.cc:664-673`. The following
end-to-end pipeline tests in `tests/unittests/compiler/hir/hir-module-test.cc`
compile source through the session and assert the lowered Built MIR CFG:

- The minimal reducible while loop `fun spin(cond: bool) -> i32 { while (cond)
  { } return 0; }` lowers to a four-block CFG (entry `Goto`, `SwitchInt` header,
  reducible back-edge `Goto` body, `Return` exit) at
  `hir-module-test.cc:736-802`.
- A two-arm scalar-literal conditional lowers to a four-block diamond
  (`SwitchInt` head, per-arm `Goto`, join `Return`) at
  `hir-module-test.cc:804-853`.
- A two-arm parameter conditional lowers each arm to a parameter-return
  assignment plus `Goto` at `hir-module-test.cc:854-903`.
- An equality-comparison conditional condition materializes one `Comparison`
  rvalue with operator `Eq` (0x01) at `hir-module-test.cc:905-965`, and a
  less-than relational condition emits operator `Lt` (0x03) at
  `hir-module-test.cc:967-993`.

These shapes are flow-verified: the dominator-based flow-subset verifier in
`ownership/facts/flow-subset.cc:34` (`isAdmittedFlowSubset`) reconstructs and
admits the reducible loop and diamond CFGs, and rejects irreducible or dangling
control flow (gate tests in this file). They do NOT yet establish general
region-membership or escape ownership proof over the new CFG shapes: the escape
builder derives only `Return` escapes for the admitted subset, general region
liveness over branch/loop points is not produced, and the three deferred
`OwnershipProofValidation` cross-checks remain unimplemented. The
`Loans, references, and regions`, `Marker, linear, unsafe, and capture
boundaries`, and `Verified ownership facts and typestate` rows therefore remain
`In Progress`; fail-closed admission stays the enforcement boundary for every
shape outside the flow subset.

## Verification Evidence

- RFC 0025 acceptance synchronization is bound to proposal SHA-256
  `4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`.
  `python3 scripts/check-rfc.py` and scoped `git diff --check` passed for this
  documentation transaction; toolchain-core marker-lineage and mutation evidence
  remains assigned to RFC 0025's `R25` tasks and completes no ownership slice.

- `zom.mir-revision` unsafe-scope framing was independently recomputed: the
  283-byte preimage has SHA-256
  `c49976b9fc841ecf6cd2e2d62af3442d36a22571b52291a0601e60ea92f71aa0`.
- `zom.ownership-event-overlay` oracle preimages were independently
  recomputed: 141-byte empty SHA-256
  `9e673e954367c3f2783cef1a9ca46e4d7e89040f2d4285ac6e42c2137bbed1d2`,
  206-byte empty-function SHA-256
  `5e36e3dd6068992f4e3b99ea9eb7df4e3836f9f8c40eb9821238a3c6090d724c`,
  and 512-byte two-occurrence collision SHA-256
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
- Borrow-source production wiring is exercised by the unit tests `Check pipeline
  rejects a returned local borrow` and `Check pipeline rejects a returned mutable
  local borrow` (`ownership-borrow-source-test.cc`), which assert
  `!checkSources()` and `getDiagnosticEngine().hasErrors()`; the accept path is
  held by `Borrow source verifier accepts a parameter reborrow` and its mutable
  variant. Conformance case
  `05-statements/return_initialized_local_reference_escape_neg_52` (corpus plus
  ast, diagnostics, and grammar expectations) checks `Error [ZOM4061]: Borrowed
  value does not live long enough` with the `Note [ZOM4062]: Borrowed value is
  created here`. The eight ownership fact-derivation tests that used the
  now-rejected returned-local-borrow source were re-sourced from the test-only
  staged-rejection accessor and the full ownership unit suite passes (16/16).
- The exact repaired `REVIEW` proposal hash
  `cb7ced8b17c6f8b6bd551a9d60f3aef5f1dd3deca56ede0b6606d72a019b9851`
  is embedded in the discussion and Decision Record before fresh owner review.
- Changed RFC and tracker files contain no CJK text.
- Placeholder scan permits only frontmatter `implementation: TBD`; the
  Decision Record is `ACCEPTED`.
- `python3 scripts/check-rfc.py` passes with the current `ACCEPTED` index row.
- Final exact-hash, CJK, placeholder, link, and diff-hygiene evidence is
  refreshed after this tracker update.
