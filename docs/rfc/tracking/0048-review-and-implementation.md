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

| Proposal SHA-256 | `02ae255ca00333327a1fe97a6eb14fe642581fcc601443e6ee57d91340567c1f` |
|---|---|

## Owner Review Matrix

| Owner | Surface | Status |
|---|---|---|
| `ir-backend` | HIR and Built MIR recursive construction, structural verifiers, canonical codec/byte oracles | Pending |
| `binder-checker` | Checked-fact consumption contracts read by the recursive lowerers | Pending |
| `error-system` | Capability-failure projection and the ZOM4099-family fail-closed diagnostic seam | Pending |
| `verification` | Corpus accept/reject and diagnostic byte-parity gates, sanitizer and mutation coverage | Pending |

## Decision Record

No decision yet. RFC 0048 is `REVIEW`, not `ACCEPTED`; `decision` stays TBD
until every required owner approves the frozen snapshot.

## Implementation Tracker

No implementation authorized. RFC 0048 is not `ACCEPTED` and not `IMPLEMENTING`;
the implementation pointer stays TBD. On acceptance the ordered work is the five
phases in the RFC (recursive HIR construction, structural HIR verifier, recursive
MIR construction plus the legality inventory, structural MIR verifier, then
capability-gate convergence), each gated on corpus parity.

## Verification Evidence

- `python3 scripts/check-rfc.py` passes for this transition.
