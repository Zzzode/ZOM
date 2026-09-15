---
rfc: 52
title: RFC Granularity And Internal-Change Threshold
type: process
status: REVIEW
author: ZOM Compiler Team
review-manager: rfc
required-owners: [rfc, task-router, verification]
approvers: []
created: 2026-09-14
updated: 2026-09-15
area: process
requires: []
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0052-rfc-granularity-and-internal-change-threshold-review.md#discussion-record
decision: TBD
implementation: TBD
tracking-issue: docs/rfc/tracking/0052-rfc-granularity-and-internal-change-threshold-review.md#decision-record
---

# RFC 0052: RFC Granularity And Internal-Change Threshold

## Summary

The repository currently carries 48 RFCs, including a long "closure" series
(RFCs 0018, 0019, 0027 through 0042) in which narrow internal refactors,
partition reviews, codec closures, and admission cutovers each received a full
RFC, review rounds, a tracking document, and owner sign-off. At the same time,
`docs/rfc/README.md` says narrow refactors and drift repair do not require an
RFC, and the project is effectively a single-maintainer pre-1.0 compiler with
23 RFCs permanently in IMPLEMENTING. This RFC fixes a durable, reviewable
threshold for when an internal change requires an RFC, defines a lighter
tracked-change path for changes that need review but not a full proposal, and
decides the one-time housekeeping disposition of the closure series.

## Motivation

The RFC process exists to make hard-to-reverse, cross-owner, or user-visible
decisions reviewable before implementation. Applied uniformly to every
internal cutover, it has produced measurable costs and benefits that need an
explicit balance:

Benefits observed in the closure series:

- generated-schema and descriptor-inventory admissions gained explicit
  producer/consumer/verifier review and self-test gates;
- stable identity materialization and diagnostic fact cutovers landed without
  dual code paths, matching the radical-refactoring rule;
- tracking documents record why each partition exists.

Costs observed:

- sixteen thousand lines of tracking documents surround a compiler with no
  shipped 1.0; many closure RFCs remain IMPLEMENTING or ACCEPTED indefinitely;
- reviewers repeatedly re-approve internal partitions invisible to users and
  to the language;
- the practiced threshold is undocumented, so authors cannot predict whether a
  refactor needs a proposal, and at least one design note drift repair
  (reconciling stale `docs/design/` notes) clearly needs no RFC while a
  comparable documentation/verifier change historically received one.

A written threshold protects both properties: consequential decisions stay
reviewable, and mechanical work stops paying full-proposal cost.

## Goals

- One decision matrix that maps a change's properties to one of three paths:
  full RFC, lightweight tracked change, or ordinary commit.
- Explicit criteria covering user-visible contracts, irreversibility,
  cross-owner coordination, new verification gates, identity/codec/schema
  changes, and pure drift repair.
- A disposition rule for the existing closure series that does not rewrite
  history and does not fabricate statuses.
- Alignment of `docs/rfc/README.md` and the rfc skill so the threshold is
  stated once and enforced by `check-rfc.py` where mechanically checkable.

## Non-Goals

- Closing, superseding, or rewriting any existing RFC retroactively.
- Changing owner authority, the review-round model, or the status state
  machine.
- Regulating language design RFCs, which always require the full path.
- Introducing pull-request review rules; this is about proposal weight, not
  code review.

## Prior Art

- **Rust two-tier model.** Language/library changes use RFCs; compiler
  internals use Major Compiler Proposals (MCPs), a short document with a
  short discussion period. The split maps consequentiality, not prestige, and
  is the closest model to what ZOM needs.
- **Swift Evolution.** Pitch then proposal for language and standard library
  changes, while compiler internals and SourceKit work largely use issue and
  implementation review without evolution proposals.
- **Go.** A deliberately minimal proposal process aimed at decisions; design
  docs and normal changes do not use it.
- **LLVM.** RFCs are reserved for large architectural or policy changes;
  pass-level and common-subsystem work uses Discourse/RFC-for-large-changes
  judgement rather than a mandatory document per refactor.
- **ZOM's own rules.** `.codex/rules/design-principles.md` already directs
  radical refactoring with no compatibility shims and immediate deletion, and
  `docs/rfc/README.md` exempts narrow refactors; this RFC makes the boundary
  operational instead of relying on taste.

## Guide-Level Explanation

An author answers a short list of questions before writing anything:

1. Does this change language syntax or semantics, a public CLI/file format,
   or a diagnostic code's meaning? Full RFC.
2. Does it change an identity domain, a canonical codec, a schema that crosses
   a verified boundary, or introduce/remove a verification gate? Full RFC,
   because it is hard to reverse after artifacts and tests depend on it.
3. Does it coordinate work across two or more owner surfaces with design
   choices another owner must accept? Full RFC.
4. Is it an internal refactor that changes structure or file decomposition
   within one owner's surface, keeps every contract and byte output identical,
   and is gated by existing tests? Ordinary commit with a clear message; no
   RFC, no tracking document.
5. Is it between those - an internal behavior change, a new test-only seam, or
   a gate relabeling that another owner should see but no external or identity
   contract changes? Lightweight tracked change: a short section appended to
   the owning RFC's tracker, or an MCP-style one-page proposal under
   `docs/rfc/tracking/` linked from the index, with a short fixed review
   window rather than multi-round owner approval.

Drift repair - making documents, specs, and tests agree with an already
accepted contract - is always an ordinary commit, never an RFC.

## Reference-Level Design

### Decision matrix

| Property of the change | Full RFC | Lightweight tracked change | Ordinary commit |
|---|---|---|---|
| Language syntax/semantics or spec chapter norm | Yes | No | No |
| User-visible CLI, output, file/lock format, diagnostics meaning | Yes | No | No |
| Identity domain, canonical codec, verified-boundary schema | Yes | No | No |
| New normative verification gate or gate semantics | Yes | No | No |
| Design decision across two or more owners | Yes | No | No |
| Internal per-family IR/builder/verifier refactor, byte-identical | No | No | Yes |
| File/module decomposition within an owner surface | No | No | Yes |
| Test-only seam, fixture, regression aid | No | Yes if cross-owner | Yes otherwise |
| Drift repair to an existing accepted contract | No | No | Yes |
| Process/agent/skill wording that changes authority | Yes | No | No |
| Process/agent/skill clarification that preserves authority | No | Yes | Yes if wording-only |

A "yes" in any full-RFC row requires the full path regardless of the other
rows; when a cell is disputed the decision defaults up one tier (ordinary
commit to tracked change, tracked change to full RFC). The `rfc` review
manager adjudicates tier placement; `task-router` adjudicates when the dispute
is about owner routing or authority wording. The review manager may upgrade a
lightweight item to a full RFC when review reveals a contract change. A
proposal that turns out to be drift repair is not "downgraded" through a new
state edge: the proposal goes `REVIEW -> WITHDRAWN` (the only legal terminal
edge), the proposal file is retained with a status-history row pointing at the
replacement, and the work continues as an ordinary commit or a tracked change
under the rules below. The proposal number is never reused.

### Lightweight tracked change format

- no frontmatter proposal file and no status state machine;
- a dated entry in an existing RFC tracker when one exists (an RFC "owns" a
  change when the change modifies the same contract area), otherwise a short
  file under `docs/rfc/tracking/changes/` with: problem, affected surfaces,
  contract-preservation statement, and verification commands;
- the single lightweight reviewer is the manifest owner of the changed paths;
  `rfc` is only the process host and does not run multi-round review;
- task-router routes a tracked change to that path owner exactly like an
  ordinary change; no new trigger keyword is needed because routing is by
  affected paths, not topic;
- a review window of owner discretion (the review manager may set a short
  fixed window per item); multi-round owner review is not used;
- discoverability is a separate "Tracked Changes" subsection of the RFC
  index. Tracked changes cannot occupy the main proposal table because
  `check-rfc.py` requires every main-table row to correspond to a
  frontmatter-bearing proposal; files under `docs/rfc/tracking/` are not
  globbed as proposals.

### Disposition of the closure series

History is preserved; nothing is renumbered or deleted. Going forward:

- closure RFCs that have fully landed keep their current LANDED status and are
  simply not used as precedent for new proposals;
- ACCEPTED or IMPLEMENTING closure RFCs are reviewed at their next status
  change (which legally requires the transition allowed by the current state
  machine, including setting `implementation` for IMPLEMENTING) and either
  completed against remaining tracker rows or, if their remaining work is
  mechanical, finished as ordinary commits in the subsystem with a closing
  note recorded by `rfc` in the tracker;
- the index gains a short note that the closure series reflects the earlier
  threshold and the matrix in this RFC governs new work;
- no new "partition closure" or "cutover" RFC is opened for work whose
  contract already exists; the tracked-change path carries it.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| RFC process text, index, and authoring skill | `docs/rfc/README.md`, `docs/rfc/0000-template.md`, `.codex/skills/rfc/SKILL.md` | rfc |
| Subagent routing and manifests | `.codex/subagents/task-router.md`, `.codex/subagents/manifest.yaml` | task-router |
| Structural gate changes, if the optional hint is accepted | `scripts/check-rfc.py` | verification |
| Existing tracker location for lightweight entries | `docs/rfc/tracking/**` | rfc |

## Security And Safety Impact

None. The matrix keeps every identity, codec, gate, and safety-relevant
contract on the full-RFC path; the lightweight path cannot approve a change to
those rows.

## Drawbacks And Risks

- A threshold can be gamed by splitting a consequential change into
  "mechanical" commits. Mitigation: the review manager can upgrade any change,
  and the full-RFC triggers are properties of the merged effect, not the
  commit description.
- Lighter process could reduce the recorded rationale for internal decisions.
  Mitigation: lightweight entries still record problem, contract preservation,
  and verification; byte-parity and architecture gates retain evidence.
- Reviewers lose a forced checkpoint for large refactors. Mitigation:
  cross-owner and codec/schema rows still force an RFC, and within-owner
  refactors remain subject to normal review and gates.

## Alternatives Considered

- **Keep full RFCs for everything.** Rejected: the closure series demonstrates
  diminishing review value and indefinite IMPLEMENTING statuses for invisible
  internal partitions.
- **Abolish internal RFCs entirely.** Rejected: identity/codec and cross-stage
  contract changes have genuinely needed multi-owner review (RFCs 0010, 0015,
  0047, 0048); removing the gate loses real catches.
- **Adopt MCP naming wholesale.** Considered; a ZOM-specific matrix is clearer
  given the verified-capability and gate vocabulary, and the lightweight path
  mirrors MCP's function without importing another project's mechanics.

## Compatibility And Rollout

Process-only change. On acceptance: update `docs/rfc/README.md` "When To Write
An RFC" to embed the matrix and tracked-change path; align the rfc skill and
task-router text; optionally add a `check-rfc.py` warning when a new RFC title
matches closure-cutover language for work with no contract delta (mechanical
check only; the review manager decision remains authoritative). Existing RFCs
are untouched.

## Documentation And Teaching Plan

The matrix itself is the teaching material; add one worked example showing a
byte-identical IR builder migration (ordinary commit, as in RFC 0048 family
lands) versus an IR contract change (full RFC), and one drift-repair example
from the 2026-09-14 design-note resync.

## Operational Readiness

None. No CI resource change; `check-rfc.py` changes, if added, must keep
current RFCs valid.

## Acceptance Criteria

- `docs/rfc/README.md` contains the decision matrix and the lightweight
  tracked-change definition, with the rfc skill and task-router aligned.
- The closure-series disposition note appears in the index without changing
  any existing RFC status.
- At least one real upcoming change is demonstrably routed by the matrix (the
  RFC 0048 per-family lands remain ordinary commits; a hypothetical identity
  change still routes to a full RFC).
- `scripts/check-rfc.py` passes for all existing RFCs after the text changes.

## Implementation Plan

1. Review and adjust the matrix with owners.
2. Update README, skill, and task-router in one documentation change after
   acceptance.
3. Add the tracked-changes index subsection and decide on the optional
   mechanical hint in `check-rfc.py`.
4. Apply the disposition rule lazily as closure RFCs next move status.

## Test Plan

- Build: none.
- Unit tests: none.
- Lit tests: none.
- Conformance: `python3 scripts/check-rfc.py` (the script takes no arguments
  and has no self-test today) and `python3 scripts/check-english-only.py`
  (Markdown is in scope for the English-only gate even though clang-format is
  not). If the optional mechanical title hint is accepted, that change adds a
  real `--self-test` to `scripts/check-rfc.py` under the verification owner in
  the same change.
- Generated files: none.
- Format: `python3 scripts/check-format.py` for any non-markdown change;
  markdown-only changes are clang-format neutral.

## Open Questions

- Is an owner-discretion review window sufficient, or should a fixed default
  number of working days be stated for lightweight tracked changes?

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-09-14 | DRAFT | Initial draft from the 2026-09-14 process audit. |
| 2026-09-14 | REVIEW | Frozen for owner review. |
| 2026-09-15 | RETURNED | Round 1: downgrade had no legal state edge; tracked-change owner/trigger and index placement were unresolved; gate ownership and the test command were wrong. |
| 2026-09-15 | DRAFT | Revised: WITHDRAWN-plus-pointer disposition, path-owner routing and adjudication, separate tracked-changes subsection, verification owner for the optional gate change, correct commands. |
| 2026-09-15 | REVIEW | Round 2 frozen after Round 1 revision; tracker and new SHA-256 snapshot bound |
