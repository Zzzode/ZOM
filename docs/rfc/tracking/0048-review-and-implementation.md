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

| Proposal SHA-256 | `2b54f6c323180f9618d184a2378829b8d46ad0d16da46a6b9e53ae08d7456192` |
|---|---|

## Owner Review Matrix

| Owner | Surface | Round 1 |
|---|---|---|
| `ir-backend` | HIR/Built MIR recursive construction, structural verifiers, canonical byte allocation, LIR structural admission | REQUEST-CHANGES |
| `binder-checker` | Checker body fact-production gates, fact resolver/node-key contracts, copy/move evidence | REQUEST-CHANGES |
| `error-system` | IR failure matrix source-construct capability row, ZOM4095-4103 projection, residual function-anchor | REQUEST-CHANGES |
| `verification` | Corpus parity tool, architecture-gate markers, in-memory mutation tests, oracle regeneration | REQUEST-CHANGES |
| `rfc` | Process, template conformance, required-owner completeness | Pending |

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

## Decision Record

No decision yet. RFC 0048 is `REVIEW`, not `ACCEPTED`; `decision` stays TBD
until every required owner approves the frozen snapshot.

## Implementation Tracker

No implementation authorized. RFC 0048 is not `ACCEPTED` and not `IMPLEMENTING`;
the implementation pointer stays TBD. On acceptance the ordered work is
Phase 0 (failure algebra + capability seam + parity tool), Phase 1 (checker
fact-production move), Phases 2-4 (recursive HIR, recursive MIR, structural
verifiers + generalized LIR), Phase 5 (remove surface body-shape classifiers and
update architecture gates), each gated on corpus byte parity.

## Verification Evidence

- `python3 scripts/check-rfc.py` passes for the revised REVIEW snapshot.
