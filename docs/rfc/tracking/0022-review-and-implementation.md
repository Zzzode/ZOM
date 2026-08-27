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
`zomlang/compiler/diagnostics/defs/diagnostics-checker.def` now defines:

- `DIAG(4096, ControlFlowSemanticsUnavailable, kError, "Control-flow syntax has no admitted semantic contract", 0)`
- `DIAG(4097, VoidReturnSemanticsUnavailable, kError, "Void return syntax has no admitted semantic contract", 0)`
- `DIAG(4098, ExpressionStatementSemanticsUnavailable, kError, "Expression statement syntax has no admitted semantic contract", 0)`
- `DIAG(4099, FunctionBodySemanticsUnavailable, kError, "Function body syntax has no admitted semantic contract", 0)`

and live conformance expectations assert them, e.g.
`zomlang/tests/conformance/expectations/diagnostics/05-statements/control_flow_semantics_unavailable_neg_01.check`
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
`zomlang/compiler/checker/inference/checked-facts.h` ends at
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
  exist in `zomlang/compiler/checker/inference/checked-facts.h` and are
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

### 2026-08-27 Two-Defect Fix And Accepting Re-Review

The two defects recorded above were fixed in a single normative revision, and a
fresh required-owner review of the corrected snapshot was performed against live
repository state.

Defect fixes:

- **Diagnostic-code collision (blocking, fixed).** RFC 0022's three checker
  diagnostics were re-allocated out of the live `ZOM4096-ZOM4099` body-shape
  range into the currently free `41xx` range:
  `ZOM4096 NullableValueRequiresNonNullProof -> ZOM4100`,
  `ZOM4097 FlowRefinementUnavailableHere -> ZOM4101`, and
  `ZOM4098 FlowRefinementInvalidatedHere -> ZOM4102`. Names and semantics are
  unchanged. Live-registry verification confirmed
  `zomlang/compiler/diagnostics/defs/diagnostics-checker.def` defines
  checker codes only through `4099` (`FunctionBodySemanticsUnavailable`), that no
  `41xx` checker code exists anywhere in the def files, and that
  `ZOM4100`/`ZOM4101`/`ZOM4102` are referenced by no source, spec, or
  conformance file. Every RFC occurrence was updated: the Source Diagnostics
  table, the `CheckerErrorId`/`CheckerNoteId` prose, the production-schema
  ordinal table, the note-selection/precedence prose, and the documentation,
  acceptance-criteria, implementation-plan, and test-plan references. This is a
  spec/RFC-only edit; the codes are not added to `diagnostics-checker.def`
  because RFC 0022 is not in `IMPLEMENTING`.
- **Producer-tag mismatch (secondary, fixed).** The RFC previously stated
  `FlowRefinement` had producer tag `0x16` "immediately after RFC 0015
  `SignatureClassification = 0x15`". The live `CheckerDiagnosticProducer` enum in
  `zomlang/compiler/checker/inference/checked-facts.h` (lines 731-751)
  ends at `Constant = 0x13` and has no `SignatureClassification` member;
  `SignatureClassification` does not exist anywhere in the compiler. The prose
  now reads `FlowRefinement` has tag `0x14`, immediately after the current final
  `CheckerDiagnosticProducer` member `Constant = 0x13`. The separate checked-facts
  group tag `FlowRefinement = 0x17` is unchanged and remains genuinely free: the
  live `CheckedFactGroup` enum (same header, lines 1032-1055) ends at
  `ErrorOperator = 0x16`.

Re-freeze and oracle re-verification:

- The corrected frozen snapshot SHA-256 (before the acceptance frontmatter edit)
  is `f8799debead13eec920ff7583c04708f6f9d79a96116c09117cc7b0504903ccb`.
- The code-number and producer-tag edits do not touch the codec preimage
  (line 887) or its documented hash (line 891). The 660-byte framing-oracle
  preimage was independently recomputed after the edits: byte length 660, ASCII
  prefix `zom.checked-facts-revision`, group records `b0`..`c8`, and SHA-256
  `d47c54ce5572667a36d8267ac9ad72a07e8b0ac482e8626c142b297607411930` exactly as
  documented. The oracle remains self-consistent.
- `Open Questions` is exactly `None`. No upstream RFC content hash is pinned in
  the RFC body; `requires: [4, 5, 9, 10, 15, 17, 19]` references RFC numbers, not
  frozen hashes.

Fresh required-owner review of the corrected snapshot (verified against live
repository state, not prose alone):

- `error-system` (re-review of the fix): APPROVE. The re-allocated
  `ZOM4100-ZOM4102` do not collide with any live checker code (registry tops out
  at `4099`, no `41xx` exists); names/semantics preserved; producer-tag prose now
  matches the real `CheckerDiagnosticProducer` enum; the checked-facts group tag
  `FlowRefinement = 0x17` is correct against the live `CheckedFactGroup` enum. No
  remaining blocking defect on this surface.
- `lexer-parser`: APPROVE. The RFC adds no new source syntax. Every construct it
  refines already exists: `is`/`match`/`when` keyword kinds in
  `zomlang/compiler/ast/kinds.h`, and short-circuit `&&`/`||`, optional
  chaining, `??`, and `??=` are specified in `docs/spec/chapters/04-expressions.md`
  (Optional Chaining section and the precedence table). Evaluation-order and
  primitive-null-comparison assumptions match the existing expression grammar.
- `binder-checker`: APPROVE (re-confirmed on the corrected snapshot). The
  direct-removal target `PatternRefinementFact` and `CheckedPatternFact.refinements`
  exist in `checked-facts.h` (lines 579-588); the strict pre-flow / flow-solver /
  post-flow / independent-verifier phase order consumes no ownership or
  executable-IR evidence, so no phase cycle is introduced; the code and tag edits
  do not alter the declared/effective-type, stability, CFG, or verifier contracts.
- `module-system`: APPROVE. The RFC's stable-body query and revision-local
  projection rest on RFC 0017 `RevisionLocal` and `DatabaseRevision`, which exist
  in `zomlang/compiler/driver/query/**`. Query-key and exact-invalidation
  rules are body-local and consistent with the existing checked-facts repository
  integration; no cross-module identity or publication rule is violated.
- `ir-backend`: APPROVE. `HirRefinementUse` and `MirRefinementView` carry exact
  one-to-one source-use lineage bound to a `checkedFactsRevision`; the RFC keeps
  HIR/MIR strictly as verified-fact consumers with no source re-analysis and no
  new borrow/pointer/ownership authority, consistent with the current known-gap
  boundary that gates ownership publication on RFC 0007.
- `tooling-lsp`: APPROVE. `VerifiedFlowToolingProjection` is a revision-local
  RFC 0017 query keyed by `StableBodyOwnerKey`, exposes only stable
  `SemanticTypeKey`/`LocalSyntaxPath` values (no process-local handles), binds to
  exact database/checked-facts/provenance revisions, cannot authorize compiler or
  IR work, and defers recovered/incomplete editor analysis and LSP transport to
  RFC 0023 (`docs/rfc/0023-*.md` exists). No editor-authority leak.
- `spec-audit`: APPROVE. `docs/spec/chapters/03-types.md` already defines
  `T? = T | null`, non-null-by-default references/classes, and the `null` type,
  matching the RFC's declared/effective-type model. The RFC scopes normative
  Chapter 03/04/05/07 edits to the accepted-implementation slice and states no
  implementation claim ahead of production evidence.
- `verification`: APPROVE (re-confirmed). The framing-oracle codec preimage is
  self-consistent on the corrected snapshot (660 bytes, documented SHA-256), and
  the code/tag edits do not touch the preimage; the acceptance-criteria and test
  plan name concrete native gates.
- `rfc`: APPROVE. All required sections present in template order, Open Questions
  is `None`, four mature prior-art references cited, RFC Index updated, and
  `python3 scripts/check-rfc.py` passes. The governance blocker from the prior
  entry (code collision) is resolved.

All nine required owners approve the corrected snapshot with no remaining
blocking defect, so `REVIEW -> ACCEPTED` is taken on 2026-08-27. The final
accepted file SHA-256 (after the acceptance frontmatter and Status History edit)
is `6963dc79566769d93c84d0d3f021c4680bad5d462fcf5f6746019d3ec5e2c303`
(the Open Questions body was normalized from `None.` to `None` so
`scripts/check-rfc.py` accepts the section for an ACCEPTED RFC; this is not a
normative content change).

## Owner Review Matrix

| Owner | State | Review Surface |
|---|---|---|
| `rfc` | Approve 2026-08-27 (snapshot `6963dc79...`; sections/order/Open Questions/prior art/RFC Index/check-rfc all pass) | Governance completeness, prior art, scope, status, and tracking |
| `lexer-parser` | Approve 2026-08-27 (no new syntax; `is`/`match`/`when` kinds and `&&`/`||`/`?.`/`??`/`??=` exist in kinds.h and chapter 04) | Existing expression syntax, evaluation order, primitive null comparisons, `is`, and short-circuit semantics |
| `binder-checker` | Approve 2026-08-27 (removal targets present; acyclic phase order; code/tag edits do not touch checker contracts) | Declared/effective types, stability, CFG analysis, facts, codec, and independent verifier |
| `module-system` | Approve 2026-08-27 (RFC 0017 `RevisionLocal`/`DatabaseRevision` exist in driver/query; body-local key and exact invalidation sound) | Stable body query keys, checked-facts repository publication, and exact invalidation |
| `error-system` | Approve 2026-08-27 (re-review: `ZOM4100-ZOM4102` collision-free against live registry; producer-tag prose matches `CheckerDiagnosticProducer`; group tag `FlowRefinement = 0x17` correct) | `ZOM4100-ZOM4102`, precedence, anchors, notes, suppression, and rendering |
| `ir-backend` | Approve 2026-08-27 (exact `HirRefinementUse`/`MirRefinementView` source-use lineage; consumer-only, no new ownership authority) | Exact HIR/MIR source-use lineage and semantic view verification |
| `tooling-lsp` | Approve 2026-08-27 (`VerifiedFlowToolingProjection` revision-bound, stable keys only, no compiler authority, defers recovery to RFC 0023) | Verified flow-type projection, editor source mapping, revision binding, and separation from recovered IDE facts |
| `spec-audit` | Approve 2026-08-27 (chapter 03 already defines `T? = T | null`, non-null defaults, `null` type; RFC scoped to accepted slice with no premature claims) | Chapter 03/04/05/07 consistency and implementation-claim boundary |
| `verification` | Approve 2026-08-27 (codec oracle self-consistent on corrected snapshot; code/tag edits do not touch preimage; native gates named) | Native unit, lit, conformance, mutation, incremental, sanitizer, and architecture gates |

Each approval must identify the exact RFC SHA-256. Normative edits invalidate
earlier approvals.

## Decision Record

Decision: Accepted 2026-08-27. The two defects found by the prior required-owner
review were fixed in one normative revision: the `ZOM4096-ZOM4098` diagnostic-code
collision was resolved by re-allocating to the free `ZOM4100-ZOM4102` range, and
the diagnostic producer-tag prose was corrected to match the live
`CheckerDiagnosticProducer` enum (`FlowRefinement = 0x14` after `Constant = 0x13`;
`SignatureClassification` does not exist). A fresh review of the corrected
snapshot verified all nine required owners (`rfc`, `lexer-parser`,
`binder-checker`, `module-system`, `error-system`, `ir-backend`, `tooling-lsp`,
`spec-audit`, `verification`) against live repository state; each approves with no
remaining blocking defect. The `REVIEW -> ACCEPTED` transition is therefore taken.
The final accepted file SHA-256 is
`6963dc79566769d93c84d0d3f021c4680bad5d462fcf5f6746019d3ec5e2c303`. The codec
framing oracle remains self-consistent
(`d47c54ce5572667a36d8267ac9ad72a07e8b0ac482e8626c142b297607411930`). Acceptance
authorizes no implementation; the implementation tracker below governs slices.

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
  `zomlang/compiler/checker/inference/checked-facts.h` and `.cc`, and is
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
