# RFC 0049 Review And Implementation Tracker

## Discussion Record

### 2026-09-14 DRAFT Authored And DRAFT -> REVIEW

RFC 0049 (Algebraic Data Type Design) consolidates the 2026-06
`docs/design/algebraic-data-types.md` note into a reviewable language RFC and
moves to `REVIEW` under the gates in `docs/rfc/README.md` and
`.codex/skills/rfc/SKILL.md`.

Why an RFC is required: the proposal changes language syntax and semantics
(variant forms, newtype, matching, variance attributes, derives, repr
controls), checker and borrow semantics, diagnostic codes, and LIR layout
contracts across many owner surfaces. The prior note presented these
decisions as a "current canonical design" with nine unregistered diagnostic
codes and no accepting RFC; this RFC converts it into a proposal, marks every
code PROPOSED (the `ZOM3010-3014` placeholders collide with the registered
binder range and must be re-picked), and removes the non-production C++
skeletons.

Frontmatter is `status: REVIEW`, `updated: 2026-09-14`, with `discussion` and
`tracking-issue` bound to this tracker. `approvers` stays empty and `decision`
and `implementation` stay `TBD`; no `REVIEW -> ACCEPTED` transition is
performed and no owner approval is recorded in this entry. Implementation
does not begin until every required owner approves the frozen snapshot.

Frozen proposal snapshot (SHA-256 of the RFC document at REVIEW entry):

| Proposal SHA-256 | `bae1d2c060767d248c3560453f98a7b9b7397d64b6c2ff208e3e4bee22166ed1` |
|---|---|

## Owner Review Matrix

| Owner | Surface | Round 1 |
|---|---|---|
| `lexer-parser` | Proposed grammar deltas (record variants, newtype, positional products), recovery, ANTLR oracle, AST schema additions | Pending |
| `binder-checker` | Variant resolution, generic variance, exhaustiveness coverage, coherence/orphan interaction, move/Linear checks | Pending |
| `error-system` | Proposed diagnostics allocation from sparse ranges, reservation ledger, producer-stage assignment | Pending |
| `ir-backend` | Match and aggregate HIR/MIR lowering, drop interaction, LIR layout, niches, repr(C) | Pending |
| `runtime-memory` | Recursive boxing via owned allocation, drop glue, zc primitives | Pending |
| `module-system` | ADT export surface, cross-module variant/derive visibility | Pending |
| `spec-audit` | Spec chapters 07/08/10/12/14/16/18 and grammar-reference alignment | Pending |
| `verification` | Lit/ztest coverage, conformance parity, layout oracles, gates | Pending |
| `rfc` | Process, template conformance, required-owner completeness | Pending |

## Decision Record

TBD. No decision is recorded until all required owners approve.

## Implementation Tracker

Not started; the RFC is not ACCEPTED.
