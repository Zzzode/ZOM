# RFC 0022 Review And Implementation Tracker

## Discussion Record

### 2026-07-24 Initial Proposal

RFC 0022 was created after comparing ZOM's live type, statement, pattern,
checker-fact, HIR, and MIR boundaries with Scala 3 explicit nulls, Kotlin smart
casts, TypeScript control-flow analysis, and Dart sound null safety.

The proposal selects:

- `T?` as exactly `T | null`;
- declared types that never change and per-use effective types;
- a checker-local control-flow graph with deterministic fixed-point analysis;
- primitive null comparison, `is`, pattern, assignment, short-circuit, and
  reachability transfer;
- binding-only subjects with conservative mutable-binding stability;
- no property, getter, index, dereference, alias, or user-defined-guard
  refinement;
- direct removal of `PatternRefinementFact`;
- one canonical per-use `CheckedFlowRefinementFact`;
- independent graph, transfer, fixed-point, and fact verification;
- `ZOM4096-ZOM4098` for nullable uses and actionable refinement notes; and
- verified HIR and MIR consumption without source-level re-analysis.

The proposal is ready for focused owner review. It does not authorize
implementation until the RFC is accepted.

Candidate RFC SHA-256:
`dca55c848ca03c6cb0b27e7bf606cb95804f053726d66bdc4f04e929157b0fb7`.

### 2026-07-24 Review Findings

The first review returned the proposal because six contracts were not closed:

- checker flow stability consumed ownership evidence published after MIR,
  creating a phase cycle;
- assignment transfer and graph termination depended on post-flow expression
  checking;
- the type domain did not define exact behavior for `any`, partial overlap, or
  finite convergence;
- match guards omitted the join of pattern failure and guard false;
- HIR and MIR referred to an operation and dominance proof that RFC 0010 did
  not define; and
- the checked-facts codec, diagnostics, and query owner surfaces were
  incomplete.

These findings moved the RFC through `REVIEW -> RETURNED -> DRAFT`.

### 2026-07-24 Closure Pass

The revised proposal re-enters `REVIEW` with:

- a strict pre-flow, flow-solver, post-flow-checking, and independent-verifier
  phase order;
- checker-owned `PreFlowCaptureInventory`, `BodyShapeFacts`,
  `FlowStabilityInventory`, and a finite `BodyFlowTypeBasis` that consume no
  ownership or executable-IR evidence;
- exact `narrow`, `exclude`, `join`, assignment, and match-guard rules,
  including conservative `any` behavior;
- a direct canonical checked-facts replacement with a framing oracle;
- complete `ZOM4096-ZOM4098` producer, recovery, ordinal, note-selection, and
  association rules;
- exact one-to-one `HirRefinementUse` and `MirRefinementView` source lineage;
  and
- explicit module-system ownership for stable body queries and checked-facts
  repository integration.

### 2026-07-24 Tooling Review And Closure

Tooling review returned the proposal because checked flow facts were addressable
only through compiler-local nodes and handles. A language server could not bind
hover, completion, or diagnostics to one source revision without reading
checker internals, and a failed body check exposed no defined degraded path.

The repaired proposal:

- adds a revision-local `VerifiedFlowToolingProjection` keyed by
  `StableBodyOwnerKey`;
- maps every reachable resolved binding use bijectively to
  `LocalSyntaxPath`;
- expands types to stable `SemanticTypeKey` values;
- binds the value to exact database, checked-facts, and provenance revisions;
- forbids the projection from authorizing compiler or IR work; and
- assigns recovered/incomplete editor analysis and LSP transport to RFC 0023.

These findings moved the RFC through a second
`REVIEW -> RETURNED -> DRAFT -> REVIEW` cycle.

### 2026-08-27 Required-Owner Review Of Current Snapshot (held in REVIEW)

A required-owner review was performed against the current frozen REVIEW
snapshot, not the prose alone. Two snapshot facts first had to be reconciled:

- The earlier audit's recorded review hash
  `dca55c848ca03c6cb0b27e7bf606cb95804f053726d66bdc4f04e929157b0fb7` is stale.
  The internal-contract-versioning removal (`0c01c39f`) later edited the
  checked-facts codec preimage in this RFC (dropped the `.v4` revision suffix
  and the `v3` decoder wording, recomputed the framing-oracle preimage), so the
  current file SHA-256 is
  `1cbe543a09878e69517406c431449cf220cce464489e3da43006dadfb8fba949`. That
  current hash is the frozen snapshot under review here; all earlier approvals
  are invalidated by the normative edit.
- The revised 660-byte framing-oracle preimage was independently recomputed:
  its byte length is 660, its ASCII prefix is `zom.checked-facts-revision`, it
  contains the 25 group records `b0`..`c8`, and its SHA-256 reproduces the
  documented `d47c54ce5572667a36d8267ac9ad72a07e8b0ac482e8626c142b297607411930`
  exactly. The codec oracle is self-consistent.

One blocking design defect was found and it is not mechanical drift, so the RFC
is held in `REVIEW` and no `REVIEW -> ACCEPTED` transition is performed:

**Blocking (error-system): the proposed `ZOM4096-ZOM4098` diagnostic codes are
already allocated in production for a different diagnostic family.** The RFC
Source Diagnostics section allocates `ZOM4096 NullableValueRequiresNonNullProof`,
`ZOM4097 FlowRefinementUnavailableHere`, and `ZOM4098 FlowRefinementInvalidatedHere`,
and the 2026-07-24 Technical Closure Audit below asserts "`ZOM4096-ZOM4098` are
free". That is no longer true. The live checker diagnostic registry
`products/zomlang/compiler/diagnostics/defs/diagnostics-checker.def` now defines:

- `DIAG(4096, ControlFlowSemanticsUnavailable, kError, "Control-flow syntax has no admitted semantic contract", 0)`
- `DIAG(4097, VoidReturnSemanticsUnavailable, kError, "Void return syntax has no admitted semantic contract", 0)`
- `DIAG(4098, ExpressionStatementSemanticsUnavailable, kError, "Expression statement syntax has no admitted semantic contract", 0)`
- `DIAG(4099, FunctionBodySemanticsUnavailable, kError, "Function body syntax has no admitted semantic contract", 0)`

and live conformance expectations assert them, e.g.
`products/zomlang/tests/conformance/expectations/diagnostics/05-statements/control_flow_semantics_unavailable_neg_01.check`
(`ZOM4096`), `.../return_pos_01.check` (`ZOM4097`),
`.../use_after_move_neg_11.check` (`ZOM4098`), and
`.../function_body_semantics_unavailable_neg_01.check` (`ZOM4099`). These body-
shape/semantics-unavailable codes were added after the 2026-07-24 audit as part
of the body-admission work, consuming `ZOM4096-ZOM4099`. The highest previously
free checker `ZOM40xx` code assumed by the audit no longer holds. Accepting
RFC 0022 as written would either double-allocate three diagnostic ids or
silently repurpose live, test-asserted codes. Resolving this requires a
substantive normative edit (choose a currently free checker code range, update
Source Diagnostics, the `CheckerErrorId`/`CheckerNoteId` additions, precedence,
and every affected snapshot), which itself invalidates this snapshot and
requires fresh review. It is therefore a genuine design gap, not a stale pin or
truncated hash to fix in-pass.

**Secondary (error-system, non-decisive but recorded): the producer-tag
allocation does not match the live enum.** The RFC states "`FlowRefinement` has
tag `0x16`, immediately after RFC 0015 `SignatureClassification = 0x15`". The
live `CheckerDiagnosticProducer` enum in
`products/zomlang/compiler/checker/inference/checked-facts.h` ends at
`Constant = 0x13` and contains no `SignatureClassification` member; the
`0x15`/`0x16` tags occupied in that header belong to the separate
`CheckedFactGroup` enum (`ErrorUnionShape = 0x15`, `ErrorOperator = 0x16`). The
checked-facts group tag the RFC adds (`FlowRefinement = 0x17`) is genuinely
free (the group enum currently ends at `ErrorOperator = 0x16`), but the
diagnostic-producer tag statement must be corrected against the real enum in
the same revision that fixes the code collision.

Surfaces that were reviewed and found sound on the current snapshot (recorded
for completeness; they do not lift the block):

- `binder-checker`: `PatternRefinementFact` and `CheckedPatternFact.refinements`
  exist in `products/zomlang/compiler/checker/inference/checked-facts.h` and are
  referenced from `body-checker.cc`, so the direct-removal target is real; the
  new checked-facts group tag `FlowRefinement = 0x17` is free; and the strict
  pre-flow / flow-solver / post-flow / independent-verifier phase order consumes
  no ownership or executable-IR evidence, so it introduces no phase cycle.
- `verification`: the framing-oracle codec preimage is self-consistent
  (660 bytes, SHA-256 as documented).
- `rfc`: all 19 required sections are present in order, Open Questions is
  `None`, prior art cites four mature designs (Scala 3, Kotlin, TypeScript,
  Dart), and `check-rfc.py` passes; the only governance blocker is the stale
  recorded snapshot noted above, which this entry supersedes with the current
  hash.

The remaining owner surfaces (`lexer-parser`, `module-system`, `ir-backend`,
`tooling-lsp`, `spec-audit`) were not carried to a verdict because the
error-system blocker already prevents acceptance of this snapshot; recording
approvals for them would not correspond to a completed acceptance review of a
defect-free snapshot.

## Owner Review Matrix

| Owner | State | Review Surface |
|---|---|---|
| `rfc` | Reviewed 2026-08-27 (no blocking defect on this surface; snapshot rehashed to `1cbe543a...`) | Governance completeness, prior art, scope, status, and tracking |
| `lexer-parser` | Pending | Existing expression syntax, evaluation order, primitive null comparisons, `is`, and short-circuit semantics |
| `binder-checker` | Reviewed 2026-08-27 (no blocking defect on this surface) | Declared/effective types, stability, CFG analysis, facts, codec, and independent verifier |
| `module-system` | Pending | Stable body query keys, checked-facts repository publication, and exact invalidation |
| `error-system` | Object 2026-08-27: `ZOM4096-ZOM4098` collide with live production `ControlFlow/VoidReturn/ExpressionStatement SemanticsUnavailable` codes asserted by conformance; producer-tag `SignatureClassification = 0x15` reference does not match the live `CheckerDiagnosticProducer` enum. | `ZOM4096-ZOM4098`, precedence, anchors, notes, suppression, and rendering |
| `ir-backend` | Pending | Exact HIR/MIR source-use lineage and semantic view verification |
| `tooling-lsp` | Pending | Verified flow-type projection, editor source mapping, revision binding, and separation from recovered IDE facts |
| `spec-audit` | Pending | Chapter 03/04/05/07 consistency and implementation-claim boundary |
| `verification` | Reviewed 2026-08-27 (codec oracle self-consistent) | Native unit, lit, conformance, mutation, incremental, sanitizer, and architecture gates |

Each approval must identify the exact RFC SHA-256. Normative edits invalidate
earlier approvals.

## Decision Record

Decision: Held in REVIEW as of 2026-08-27. A required-owner review of the frozen
snapshot `1cbe543a09878e69517406c431449cf220cce464489e3da43006dadfb8fba949`
found one blocking design defect: the proposed `ZOM4096-ZOM4098` diagnostic
codes are already allocated in the production checker diagnostic registry and
asserted by live conformance expectations, so the RFC's diagnostic allocation
must be revised (a substantive normative edit) before acceptance. `error-system`
objects; the RFC stays in `REVIEW`. No approval is fabricated, no
`REVIEW -> ACCEPTED` transition is taken, and no implementation is authorized by
this tracker.

## Implementation Tracker

| Slice | State | Required Evidence |
|---|---|---|
| Normative spec alignment | Pending acceptance | Chapters 03, 04, 05, and 07 plus spec-alignment review |
| Checked-facts direct replacement | Pending acceptance | Deleted pattern refinement field, canonical flow group, codec oracle, cache invalidation |
| Subject inventory and stability | Pending acceptance | Local, parameter, pre-flow capture, mutable-binding, direct-use, borrow-syntax, and address-escape matrix |
| Checker-local flow graph | Pending acceptance | Complete supported syntax, deterministic edges, labels, exits, and callable isolation |
| Type-domain and fixed-point solver | Pending acceptance | Finite basis, `any` and partial-overlap rules, null/type/pattern transfers, joins, assignments, loops, and convergence tests |
| Source diagnostics | Pending acceptance | `ZOM4096-ZOM4098` registry, precedence, notes, renderer, and lit snapshots |
| Independent verifier | Pending acceptance | Separate construction and mutation coverage for every invariant |
| Incremental query integration | Pending acceptance | Stable body key, exact invalidation, revision determinism, parallel scheduling |
| Verified tooling projection | Pending accepted checker facts | Stable owner/path mapping, semantic type keys, exact revision fields, stale/cross-owner negatives |
| HIR and MIR handoff | Pending accepted checker facts | Exact source-use views, missing/additional/duplicated/hoisted evidence negatives |
| Production and documentation cutover | Pending all prior slices | Sanitizer build, default CTest, format, architecture gates, design notes, release notes |

## Verification Evidence

- Revised candidate hash
  `dca55c848ca03c6cb0b27e7bf606cb95804f053726d66bdc4f04e929157b0fb7`
  was recomputed and matched this tracker on 2026-07-24.
- `python3 scripts/check-rfc.py`: passed for 23 proposal RFCs on 2026-07-24.
- `git diff --check`: passed on 2026-07-24.
- `python3 scripts/check-format.py`: passed on 2026-07-24 with the Xcode
  toolchain `clang-format`; the executable was not available on the default
  `PATH`.
- Scala 3, Kotlin, TypeScript, and Dart primary language documentation was
  reviewed on 2026-07-24.
- Live repository inspection confirmed that nullable unions and relevant syntax
  exist, while production CFG refinement, stable-subject analysis, and
  independently verified per-use flow facts do not.

## Technical Closure Audit (2026-07-24)

Independent repository inspection confirmed the proposal's live dependencies
and non-conflicts:

- `PatternRefinementFact` exists in
  `products/zomlang/compiler/checker/inference/checked-facts.h` and `.cc`, and is
  referenced from `body-checker.cc`. The RFC's direct removal target is
  present and addressable.
- The proposed `ZOM4096-ZOM4098` range does not collide with existing
  diagnostic codes. The highest existing checker code in the `ZOM40xx`
  family is `ZOM4095` (RFC 0007); `ZOM4091`/`ZOM4092` are used by RFC 0018
  and the marker-impl conformance suite. `ZOM4096-ZOM4098` are free.
- `PreFlowCaptureInventory`, `BodyShapeFacts`, `FlowStabilityInventory`,
  `BodyFlowTypeBasis`, `CheckedFlowRefinementFact`, and the canonical codec
  oracle are new checker-local constructs defined by this RFC; they do not
  require pre-existing production code.
- The phase order (pre-flow -> flow-solver -> post-flow-checking ->
  independent verifier) consumes no ownership or executable-IR evidence,
  so it does not create a phase cycle with RFC 0013's MIR publication.
- `python3 scripts/check-rfc.py` passed for all 23 proposal RFCs.

No blocking technical gaps found. Remaining work is required-owner approval
of exact REVIEW SHA-256
`dca55c848ca03c6cb0b27e7bf606cb95804f053726d66bdc4f04e929157b0fb7`.
