# RFC 0048 Review And Implementation Tracker

## Discussion Record

### 2026-09-10 DRAFT Authored And DRAFT -> REVIEW

RFC 0048 (Recursive IR Construction And Structural Verification) was moved from
`DRAFT` to `REVIEW` under the RFC process gates in `docs/rfc/README.md` and the
review-readiness bar in `.codex/skills/rfc/SKILL.md`.

Motivation that requires a design RFC rather than an incremental fix:

- Adding a single binary-initializer local (`let x: bool = a == b; return x;`)
  required editing three independent whole-function shape classifiers (surface
  admission, HIR `functionReturnShape`, Built MIR shape gates), re-balancing
  global count equations, and routing between overlapping rails. The IR types
  already supported the form; the construction/verification algorithm did not.
- RFC 0010 already specifies the target artifacts: a source-shaped expression
  HIR and a general place-based CFG MIR with per-layer verifiers. RFC 0048
  changes only how those artifacts are constructed and verified, leaving the
  RFC 0010 contracts, codecs, and revision identities unchanged.

The proposal resolves the design from mature prior art and folds the decisions
into the Reference-Level Design; Open Questions is `None`:

- Recursive destination-driven construction with a per-function context and
  append-only block cursor (rustc THIR-to-MIR builder, Cranelift
  `FunctionBuilder`, Swift SILGen).
- Structural per-node verification over the finished graph in place of global
  count equations and per-shape validators (Cranelift `Verifier`, MLIR per-op
  `verify()`).
- A lowering-legality inventory that keeps fail-closed capability behavior
  node-local and preserves the exact accept/reject and diagnostic partition over
  the conformance corpus (MLIR conversion legality).

Frontmatter is `status: REVIEW`, `updated: 2026-09-10`, with `discussion` and
`tracking-issue` bound to this tracker. `approvers` stays empty, `decision` and
`implementation` stay `TBD`; no `REVIEW -> ACCEPTED` transition is performed and
no owner approval is recorded here. Implementation does not begin until every
required owner approves the frozen snapshot.

Frozen proposal snapshot (SHA-256 of the RFC document at REVIEW entry):

| Proposal SHA-256 | `ff5d5a2d922f22f1b77686daafab42f6f2729ee52858da24cddf0fbfae97422e` |
|---|---|

## Owner Review Matrix

| Owner | Surface | Round 1 | Round 2 | Round 3 |
|---|---|---|---|---|
| `ir-backend` | HIR/Built MIR recursive construction, structural verifiers, canonical byte allocation, LIR structural admission | REQUEST-CHANGES | APPROVED | APPROVED |
| `binder-checker` | Checker body fact-production gates, fact resolver/node-key contracts, copy/move evidence | REQUEST-CHANGES | APPROVED (editorial) | APPROVED |
| `error-system` | IR failure matrix source-construct capability row, ZOM4095-4103 projection, residual function-anchor | REQUEST-CHANGES | REQUEST-CHANGES (one taxonomy gap) | APPROVED |
| `verification` | Corpus parity tool, architecture-gate markers, in-memory mutation tests, oracle regeneration | REQUEST-CHANGES | REQUEST-CHANGES (IR-byte channel) | APPROVED (one wording fix applied) |
| `rfc` | Process, template conformance, required-owner completeness | Pending | Pending | APPROVED |

### 2026-09-10 Four-Owner Review Round 1 - All REQUEST-CHANGES, Revised

All four technical owners reviewed the frozen snapshot against live code. The
direction (recursive destination-driven construction plus structural
verification) was confirmed sound, but the original draft under-scoped the work.
Agreed blocking findings, all addressed in the revised REVIEW snapshot:

- No legal `(CapabilityRejected, Hir/MirConstruction, *)` row exists; IR
  capability maps to ZOM6009 and ZOM4095-4103 is produced only by ownership
  surface/checker source rails. Addressed by a Phase 0 RFC 0010 failure-algebra
  extension plus capability projector arms to the existing ZOM codes.
- The checker body rail refuses to publish facts for well-typed unlowered
  shapes (58 `rejectInvariant` sites), so they never reach a MIR legality walk.
  Addressed by moving lowering-shape/staging gates out of checker fact
  production (Phase 1); `compiler/checker/body/**` is now in scope.
- `compiler/lir/mir-to-lir.cc` and the ownership overlay read locals/blocks
  positionally and cross-check parameter ordinals against borrow evidence; five
  current rails use distinct local conventions. Addressed by adding `compiler/lir/**`
  and `compiler/ownership/**` to Repository Impact, a deterministic ordering
  contract (dense contiguous ordinals, parameters first, per-construct local-kind
  placement, one StorageLive), generalized structural LIR admission, and byte
  parity as an explicit replication requirement.
- The draft factually said "one block per function" and treated calls as rvalues;
  admitted slice already emits two- and four-block CFGs with call terminators,
  continuation blocks, receiver borrow/effect, unsafe boundaries, and
  StorageDead. Addressed by specifying terminator-driven call lowering and the
  full emitted statement vocabulary.
- Verification gaps: no corpus parity tool exists; architecture gates pin the
  deleted markers; "mutation" tests hash bytes rather than run the verifier; HIR
  has no candidate mutation seam; byte oracles live in the two ztests, not
  `tests/coverage/**`. Addressed by adding the parity tool, gate-marker
  migration in scope, in-memory candidate/CFG mutation tests with a new HIR seam,
  corrected paths, and a post-parity pilot node.

The proposal is updated and re-frozen (snapshot hash updated above). It returns
to REVIEW for a second round; no owner approval is recorded and the RFC does not
advance to ACCEPTED.

### 2026-09-10 Four-Owner Review Round 2 - Two APPROVED, Two REQUEST-CHANGES, Revised

The four owners re-reviewed the revised snapshot against live code.

- `ir-backend`: APPROVED. All nine round-1 findings resolved at the design level;
  every load-bearing claim checked against code. Three non-blocking notes: the
  structural MIR verifier should not assert exact per-block statement counts
  (reworded to ordering/termination invariants), per-construct local placement
  remains in the "general" builder (a deliberate, documented scoping choice), and
  the capability-tag choice is deferred to Phase 0.
- `binder-checker`: APPROVED. B1/B2 and M1/M2 plus minors resolved. Editorial
  notes folded in: name the error-postfix pre-HIR refusal, mark the capture arm a
  future slice, and keep the MIR body-input/marker-engine carve-out prominent.
- `verification`: REQUEST-CHANGES. F1-F4/F6/F7 resolved; F5 remained blocking -
  `compile --check` stops after checking and there is no HIR/MIR dump mode, so an
  exit/diagnostic parity tool cannot see IR byte drift for accepted programs.
  Resolved in this revision by adding a deterministic debug HIR/MIR canonical dump
  surface (`utils/zomc/**` added to impact) and a two-channel parity tool
  (process channel + IR channel), plus per-file invocation/include-exclude rules
  and codec-framing oracle guidance.
- `error-system`: REQUEST-CHANGES. Findings 1/3/4 resolved; Finding 2's mechanism
  is correct but the residual taxonomy omitted a live third function-anchored
  ZOM4099 case: `class_literal_body_shape_neg_03` (a class aggregate) is rejected
  today only by the surface gate's struct-only scan while checker/HIR/MIR/LIR are
  class-agnostic and the struct twin links. Resolved in this revision by keying
  legality on node kind plus resolved definition kind (class versus struct),
  allowing construct-bearing bodies without a dedicated code to map to ZOM4099 at
  the function-declaration anchor, and pinning the projector discriminator and
  per-code producer split (no-HIR-record codes stay surface-produced; only ZOM4103
  and ZOM4099 reach the construction rail).

The proposal is re-frozen with these changes (snapshot hash updated) and returns
to REVIEW for a third round on the two revised surfaces. No approval is recorded.

### 2026-09-10 Four-Owner Review Round 3 - All Technical Owners APPROVED

The two remaining owners re-reviewed the re-frozen snapshot against live code.

- `error-system`: APPROVE. New Issue 1 resolved - legality keyed on node kind plus
  resolved definition kind (`DefinitionKind::Class` vs `Struct`, available on the
  HIR aggregate DefId), construct-bearing bodies without a dedicated code (the
  class aggregate) map to ZOM4099 at the function-declaration anchor, verified
  byte-identical against `class_literal_body_shape_neg_03.check` (4099 at 2:1)
  by a live run. New Issue 2 resolved - a construct discriminator feeds total
  `capabilityDiagnosticId`/`sameCapabilityRoot`/`irOperationalFailureDisplay`
  arms; no-HIR-record codes (4095/4096/4097/4098) stay OwnershipSurface and only
  ZOM4103/ZOM4099 gain the construction-rail arm; no dual production. A nit to
  list ZOM4097 in the surface-side parenthetical was applied.
- `verification`: APPROVE after one wording fix. F5 resolved - a deterministic
  debug `--dump-hir`/`--dump-mir` surface (buildable from `VerifiedHirModule::dump()`
  and the `MirRevisionCodec` framed bytes) plus a two-channel parity tool
  (process + IR) makes accepted-program IR byte drift observable; per-file
  invocation and include/exclude classes pinned. Minors 2/4 resolved. Minor 3
  flagged a self-contradiction: the Compatibility section said both ztest files'
  hex digests are regenerated while the Test Plan (correctly) keeps the
  hand-assembled codec-framing oracles in `built-mir-test.cc` byte-identical; the
  contradiction was fixed so only production-builder witnesses in
  `hir-module-test.cc` are regenerable under the audited exception list.

All four technical required owners (`ir-backend`, `binder-checker`,
`error-system`, `verification`) now APPROVE the frozen snapshot. The `rfc` process
review remains open; the RFC stays `REVIEW` and is not advanced to ACCEPTED until
the process owner signs off.

## Decision Record

Decision: Accepted 2026-09-10. All five required owners - `rfc`, `ir-backend`,
`binder-checker`, `error-system`, and `verification` - approved the frozen
proposal snapshot `ff5d5a2d922f22f1b77686daafab42f6f2729ee52858da24cddf0fbfae97422e`
after three code-verified review rounds. The process review confirmed frontmatter,
template, required-owner completeness, snapshot integrity, prior-art depth, and
`scripts/check-rfc.py` all pass. `decision` is set; `implementation` stays TBD and
no `ACCEPTED -> IMPLEMENTING` transition is set in this change.

## Implementation Tracker

No implementation authorized. RFC 0048 is not `ACCEPTED` and not `IMPLEMENTING`;
the implementation pointer stays TBD. On acceptance the ordered work is
Phase 0 (failure algebra + capability seam + parity tool), Phase 1 (checker
fact-production move), Phases 2-4 (recursive HIR, recursive MIR, structural
verifiers + generalized LIR), Phase 5 (remove surface body-shape classifiers and
update architecture gates), each gated on corpus byte parity.

### 2026-09-11 Phase 0 - Complete (5 commits)

Commits `f5c45192..6128f750` on develop, each behavior-preserving:

- `scripts/check-ir-parity.py` with record/check/self-test and two channels:
  process (per-file exit code + normalized diagnostics over all 923 corpus
  sources) and, under `--ir`, deterministic `--dump-hir`/`--dump-mir` hashes for
  the clean-compiling sources.
- Baselines `tests/coverage/corpus-process-parity.json` (923 sources;
  859 reject / 64 clean) and `tests/coverage/corpus-ir-parity.json` (HIR + MIR
  hashes for the 64 clean sources). Both verified byte-parallel after the matrix
  change.
- `zomc --dump-hir`/`--dump-mir` (`--emit=hir|mir`): HIR via the canonical
  textual dump; MIR renders each `VerifiedBuiltMir::canonicalFunctionRecords()`
  framed record as hex plus the revision digest. Deterministic and build-path
  independent.
- `IrFailureKind::UnsupportedSourceConstruct` (0x14) is a capability kind legal
  at `HirConstruction` (Module or Definition owner) and `MirConstruction`
  (Definition owner only), ranged through `isLegalIrFailureShape`, and mapped by
  the capability projector to the existing ZOM4099 with a dedicated operational
  display. The descriptor's independent `sourceSpan` is the construct anchor. No
  producer yet, so behavior is unchanged. The exhaustive matrix test spans kind
  0x14 and a focused legality test was added.

### 2026-09-11 Phase 1 - Precondition audit findings (implementation not started)

A line-by-line audit of the 58 `rejectInvariant` call sites in
`compiler/checker/body/body-checker.cc` established that the shape gates
conflate two concerns and must be split rather than moved wholesale:

- WIRING (stay invariant, internal transport/structural): the
  `InferenceLifecycle` (1815/1831/1864), `InputReceiptMismatch`
  (2193/2259/2530), `CanonicalCodecMismatch` (3295/3456) sites; Unknown-node
  receipt (2249); operator-catalog mismatch (2309); redundant post-shape arity
  checks (2696/2768); literal-fact plumbing (2804/3196); missing post-loop place
  (3212); definition/pattern binding inventory plumbing (3235/3242/3252); and
  the completeness count equation (3356).
- TYPE (genuine user correctness errors currently routed as invariants; must
  become source diagnostics, stay in the checker): argument/return type
  disagreement (2716/2797/3165), assignment target/value type mismatch
  (3203/3278), immutable field/place writes (3002/3218), dereference of a
  non-reference (3073), struct-literal field type mismatch (3101 line 712), and
  the arity/method-not-found sub-reasons currently sharing the call shape
  `none` exits (2686/2758).
- SHAPE (type-valid but the lowering slice cannot emit; Phase 1 moves these to
  the capability inventory): the five-stage production schedule around line
  2555 and `primitiveBinaryOperationShape` condition/operand placement
  (2602/2644, line 1137 arithmetic-in-condition), `unsafe` block tail shape
  (2612/2620/2634/2644), direct/concrete call argument placement - literals/
  parameters only (2708/2791), receiver placement and generic/raises/abi call
  restrictions (2686/2758), `readIndexShape` placement (2835/2842), borrow and
  reborrow operand form (3045/3063/3085/3093), and the catch-all for empty
  fact families (3328: casts, compound assignment, captures, exhaustiveness,
  projections, error-union shapes). Closure captures (2539) and destructuring
  patterns (3242/3252) are kept out by retained surface/pre-HIR checks and stay
  fail-closed invariants rather than moving.

Important coupling: Phase 1 cannot land before the recursive HIR builder can
consume the newly published facts (Phase 2); publishing facts for a shape the
current shape-HIR builder rejects would create intermediate ICEs. Phases 1 and 2
should therefore be implemented and parity-gated as one vertical slice per
construct family. The next concrete step is to attach a source diagnostic to one
TYPE split (assignment/argument type mismatch) and route one SHAPE split through
`UnsupportedSourceConstruct`, both behind the parity gate.

## Verification Evidence

- `python3 scripts/check-rfc.py` passes for the revised REVIEW snapshot.
