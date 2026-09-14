# RFC 0052 Review And Implementation Tracker

## Discussion Record

### 2026-09-14 DRAFT Authored And DRAFT -> REVIEW

RFC 0052 (RFC Granularity And Internal-Change Threshold) moves to `REVIEW`.
It is a process RFC responding to the 2026-09-14 audit finding that the
repository carries a long closure-RFC series for narrow internal refactors
while `docs/rfc/README.md` says narrow refactors and drift repair require no
RFC.

The proposal defines a decision matrix (full RFC, lightweight tracked change,
ordinary commit) keyed to user-visible contracts, identity/codec/schema
changes, new normative gates, cross-owner decisions, byte-identical internal
refactors, and drift repair, and defines a lazy disposition of the existing
closure series without rewriting history. Prior art includes Rust RFCs vs
MCPs, Swift Evolution, Go proposals, and LLVM's large-change RFC convention.

Frontmatter is `status: REVIEW`, `updated: 2026-09-14`, with `discussion` and
`tracking-issue` bound here. `approvers` is empty; no approval or status
transition is recorded.

Frozen proposal snapshot (SHA-256 of the RFC document at REVIEW entry):

| Proposal SHA-256 | `aaf8faf591d7aafaa38e78f49ca244b0e7ee729f2960f24855aaa82d5a1bf6e5` |
|---|---|

## Owner Review Matrix

| Owner | Surface | Round 1 |
|---|---|---|
| `rfc` | RFC process text, index, tracked-change format, `check-rfc.py` implications | Pending |
| `task-router` | Skill and subagent routing alignment, internal-change thresholds | Pending |

## Decision Record

TBD.

## Implementation Tracker

Not started; the RFC is not ACCEPTED.
