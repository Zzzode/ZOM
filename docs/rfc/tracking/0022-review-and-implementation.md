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
- a direct checked-facts `v4` replacement with a 663-byte framing oracle;
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

## Owner Review Matrix

| Owner | State | Review Surface |
|---|---|---|
| `rfc` | Pending | Governance completeness, prior art, scope, status, and tracking |
| `lexer-parser` | Pending | Existing expression syntax, evaluation order, primitive null comparisons, `is`, and short-circuit semantics |
| `binder-checker` | Pending | Declared/effective types, stability, CFG analysis, facts, codec, and independent verifier |
| `module-system` | Pending | Stable body query keys, checked-facts repository publication, and exact invalidation |
| `error-system` | Pending | `ZOM4096-ZOM4098`, precedence, anchors, notes, suppression, and rendering |
| `ir-backend` | Pending | Exact HIR/MIR source-use lineage and semantic view verification |
| `tooling-lsp` | Pending | Verified flow-type projection, editor source mapping, revision binding, and separation from recovered IDE facts |
| `spec-audit` | Pending | Chapter 03/04/05/07 consistency and implementation-claim boundary |
| `verification` | Pending | Native unit, lit, conformance, mutation, incremental, sanitizer, and architecture gates |

Each approval must identify the exact RFC SHA-256. Normative edits invalidate
earlier approvals.

## Decision Record

Decision: Pending.

RFC 0022 is in `REVIEW`. No implementation is authorized by this tracker.

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
  `products/zomlang/compiler/checker/checked-facts.h` and `.cc`, and is
  referenced from `body-checker.cc`. The RFC's direct removal target is
  present and addressable.
- The proposed `ZOM4096-ZOM4098` range does not collide with existing
  diagnostic codes. The highest existing checker code in the `ZOM40xx`
  family is `ZOM4095` (RFC 0007); `ZOM4091`/`ZOM4092` are used by RFC 0018
  and the marker-impl conformance suite. `ZOM4096-ZOM4098` are free.
- `PreFlowCaptureInventory`, `BodyShapeFacts`, `FlowStabilityInventory`,
  `BodyFlowTypeBasis`, `CheckedFlowRefinementFact`, and the `v4` codec
  oracle are new checker-local constructs defined by this RFC; they do not
  require pre-existing production code.
- The phase order (pre-flow -> flow-solver -> post-flow-checking ->
  independent verifier) consumes no ownership or executable-IR evidence,
  so it does not create a phase cycle with RFC 0013's MIR publication.
- `python3 scripts/check-rfc.py` passed for all 23 proposal RFCs.

No blocking technical gaps found. Remaining work is required-owner approval
of exact REVIEW SHA-256
`dca55c848ca03c6cb0b27e7bf606cb95804f053726d66bdc4f04e929157b0fb7`.
