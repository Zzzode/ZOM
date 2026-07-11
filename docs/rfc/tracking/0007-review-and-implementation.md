# RFC 0007 Review And Implementation Tracker

This document is the local discussion and tracking record for RFC 0007. It does
not approve the proposal. RFC status, approvers, and the recorded decision
remain authoritative in the proposal frontmatter.

## Discussion Record

### 2026-07-10 Governance And Ownership-IR Review

The proposal was returned for these blocking issues:

- discussion and tracking fields pointed back to proposal sections instead of
  a review artifact;
- substantial typed-AST borrow-checker work preceded an acceptance decision;
- the normative `Typed AST -> CFG` contract conflicts with RFC 0010, which
  requires ownership analysis to consume revision-safe Built MIR containing
  complete places and semantic exits;
- `ir-backend` and `products/zomlang/compiler/mir/**` were absent from the
  owner and repository-impact contracts;
- implementation-readiness text no longer matched the live checker pipeline;
- field-sensitive moves, trusted scoped-task roots, reborrow restoration,
  closure escape rules, marker facts, and translated-corpus gates remain open.

## Owner Review Checklist

| Owner | Review State | Blocking Surface |
|---|---|---|
| `rfc` | Returned | Legal dependency, discussion, decision, and status ordering |
| `binder-checker` | Returned | Replace AST-shape ownership reconstruction with checked semantic facts |
| `concurrency` | Pending | Scoped task roots, capture regions, and suspension safety |
| `ir-backend` | Returned | Built MIR places, exits, revisions, and ownership-fact handoff |
| `runtime-memory` | Pending | Drop, linear, panic, and unsafe-boundary contracts |
| `spec-audit` | Pending | Reference, ownership, and concurrency spec alignment |
| `verification` | Pending | Diagnostic-family matrix and translated ownership corpus |

No owner approval is recorded by this table. Approval is recorded only in RFC
0007 frontmatter after every blocking review item is resolved.

## Decision Record

Decision: TBD.

RFC 0007 is `RETURNED`. The acceptance dependency order is RFC 0011, RFC 0004,
RFC 0005, RFC 0008, RFC 0010, RFC 0006, RFC 0013, then RFC 0007. RFC 0013 must
be accepted before this proposal returns to DRAFT, and the redesigned RFC 0007
must receive a fresh exact-hash review.

## Implementation Tracker

The current `BorrowCheckerPhase`, AST place collection, dataflow, diagnostics,
and conformance work is a disposable pre-acceptance experiment. Accepted
implementation work must not begin until a recorded `ACCEPTED -> IMPLEMENTING`
transition. A revised RFC must first define the Built MIR ownership input and
proof output expected by RFC 0010.

## Verification Evidence

- Existing focused checker and diagnostics tests are experiment evidence only.
- `python3 scripts/check-rfc.py` validates document structure but does not
  constitute owner approval.
