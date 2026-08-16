---
rfc: 33
title: Stable Header Review Partition Closure
type: testing
status: SUPERSEDED
author: ZOM Compiler Team
review-manager: rfc
required-owners: [rfc, binder-checker, verification]
approvers: [rfc, binder-checker, verification]
created: 2026-07-28
updated: 2026-07-28
area: testing
requires: [27, 30, 31]
supersedes: []
superseded-by: [34]
discussion: docs/rfc/tracking/0033-review-and-implementation.md#discussion-record
decision: docs/rfc/tracking/0033-review-and-implementation.md#decision-record
implementation: docs/rfc/tracking/0033-review-and-implementation.md#implementation-tracker
tracking-issue: docs/rfc/tracking/0033-review-and-implementation.md#implementation-tracker
---

# RFC 0033: Stable Header Review Partition Closure

## Summary

This RFC replaces RFC 0030 task `R30-12H` with three dependency-ordered review
tasks. The first task owns stable header enums, the header-site sum, and both
parameter records. The second owns `StableDefinitionHeader`. The third owns
`StableImplementationOccurrenceHeader`. Each task includes its native tests
and remains capped at 400 changed source lines.

The three tasks still accumulate into the same uncommitted `R29-12AB`
transaction. This RFC changes no stable type, field, tag, canonical domain,
codec, build target, landing file, or final atomicity requirement.

## Motivation

The first `R30-12H` implementation candidate added 601 net lines across the
three files named by the accepted task. Independent Binder and verification
reviews correctly rejected it because RFC 0030 caps every S2 and S3 review
patch at 400 changed source lines.

Compressing the implementation would remove required admission and mutation
tests. Excluding the public header or native test from the count would
contradict the accepted exact-file task. The stable header model already has
three natural dependency boundaries:

- shared closed enums, header sites, and parameter records;
- the definition aggregate; and
- the implementation-occurrence aggregate.

The tracker must expose those boundaries before source review resumes.

## Goals

- Preserve the 400-line cap for every exact-file review task.
- Keep public declarations, implementation, and native tests in each count.
- Give definition and implementation aggregate invariants independent reviews.
- Require populated parameter sequences and cross-field mutation tests.
- Preserve the exact `R29-12AB` landing set and one atomic commit.
- Replace the rejected task directly without aliases or duplicate status.

## Non-Goals

- This RFC does not change RFC 0027 stable header semantics.
- This RFC does not change the stable-binding schema.
- This RFC does not change canonical encodings or wire tags.
- This RFC does not move implementation into additional files.
- This RFC does not relax the 400-line cap.
- This RFC does not authorize an intermediate commit or push.
- This RFC does not change the immutable implementation-series base.

## Prior Art

LLVM review guidance recommends splitting a large change into smaller patches
that build on one another and permits reviewers to request independently
reviewable patches:
<https://llvm.org/docs/CodeReview.html>.

LLVM contribution guidance requires an isolated change with a small unit test
and directs independent changes into separate patches:
<https://llvm.org/docs/Contributing.html>.

Git patch series record a base commit and prerequisite patches in topological
order so dependent review units retain an explicit application order:
<https://git-scm.com/docs/git-format-patch#_base_tree_information>.

This RFC applies the same pattern to review tasks while preserving one final
atomic landing transaction.

## Guide-Level Explanation

Contributors review stable headers in three steps:

```mermaid
flowchart LR
    A["Header primitives and parameter records"] --> B["Definition header aggregate"]
    B --> C["Implementation occurrence header aggregate"]
    C --> D["Header codecs and wire oracles"]
    D --> L["Atomic R29-12AB landing"]
```

Every step changes only the existing facts header, facts source, and fact test.
The next step starts from the exact approved hash of the preceding step. No
step is committed or pushed independently.

## Reference-Level Design

### Replacement Tasks

RFC 0030 `R30-12H` is replaced by these tasks:

| Task | Depends On | Entities | Required Native Evidence |
|---|---|---|---|
| `R30-12H-A` | `R30-12G`; RFC 0033 `R33-07` | `DefinitionBodyDisposition`, `ImplementationSourceForm`, `ScopeRole`, `StableHeaderSite`, `StableHeaderGenericParameter`, `StableHeaderCallableParameter` | Closed enum, site variant, key-record, owner-site, ordinal, position-name, clone, and inequality tests |
| `R30-12H-B` | `R30-12H-A` | `StableDefinitionHeader` | Populated generic and callable sequences; module, digest, authority-site, kind, namespace, name, activation, visibility, body-disposition, scope-role, owner, site, clone, and inequality tests |
| `R30-12H-C` | `R30-12H-B` | `StableImplementationOccurrenceHeader` | Populated generic sequence; module, authority, occurrence, record, owner, site, source-form, scope-role, clone, and inequality tests |

Each task edits exactly:

```text
products/zomlang/compiler/binder/stable/stable-binding-facts.h
products/zomlang/compiler/binder/stable/stable-binding-facts.cc
products/zomlang/tests/unittests/compiler/binder/stable-binding-facts-test.cc
```

For each task, changed source lines are additions plus deletions across all
three files relative to the exact approved predecessor hash. The total must
not exceed 400.

### Callable Parameter Invariant

`StableHeaderCallableParameter` admits exactly:

- `Receiver` with no declared name; or
- `Ordinary(ordinal)` with one declared name.

Its retained position must equal the position in
`CallableParameterIdentityRecord`, and its stable key must equal the digest of
that complete record. A callable parameter site is always a definition
authority site.

### Aggregate Sequence Evidence

Aggregate tests must use non-empty canonical sequences. Empty-sequence tests
remain useful but cannot satisfy aggregate owner, site, ordinal, position, or
clone evidence.

The definition aggregate independently verifies that every parameter belongs
to its definition key and exact authority site. The implementation aggregate
independently verifies that every generic parameter belongs to its
implementation key and exact occurrence site.

### Downstream Dependency

RFC 0030 `R30-12I` depends on `R30-12H-C`. All tasks after `R30-12I` retain
their current identifiers and dependencies. RFC 0030 `R30-13`, `R30-14`, and
`R30-15` remain unchanged.

### Landing And Status

The three review tasks do not create commits, branches, pushes, compatibility
surfaces, or partial landing status. RFC 0030 `R30-15` remains the only
authorized `R29-12AB` commit and push.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| RFC authority | `docs/rfc/0030-stable-binding-foundation-verification.md`, `docs/rfc/0033-stable-header-review-partition-closure.md`, `docs/rfc/tracking/0030-review-and-implementation.md`, `docs/rfc/tracking/0033-review-and-implementation.md`, `docs/rfc/README.md` | `rfc` |
| Stable header facts | `products/zomlang/compiler/binder/stable-binding-facts.{h,cc}` | `binder-checker` |
| Native stable header tests | `products/zomlang/tests/unittests/compiler/binder/stable-binding-facts-test.cc` | `verification` |

## Security And Safety Impact

This RFC adds no external input, runtime capability, or memory authority. The
split increases review coverage of move-only Pimpl ownership, closed sums, and
cross-field admission. It preserves the same final code and atomic landing.

## Drawbacks And Risks

- Three reviews require more exact-hash bookkeeping than one review.
- Later tasks cannot begin until all three predecessor hashes are approved.
- The shared test file accumulates across tasks, so every review must compare
  against the exact preceding hash rather than the immutable series base.
- A source candidate prepared before acceptance must be reshaped to the new
  first task; no approval from the rejected candidate carries forward.

## Alternatives Considered

### Exclude Tests Or Headers From The Limit

The accepted task names both files, and both carry review risk. Exclusion
would weaken the limit rather than make the patch smaller.

### Compress Tests And Admission Logic

The rejected candidate already lacked populated-sequence and cross-field
mutations. Further compression would reduce evidence below the stable header
contract.

### Increase The Limit

The three semantic boundaries are independently reviewable. Increasing the
limit is unnecessary and would remove the control that found this defect.

### Commit Each Review Task

RFC 0030 requires one atomic `R29-12AB` landing. Intermediate commits or pushes
would violate that transaction.

## Compatibility And Rollout

There is no compatibility surface. The rejected cumulative candidate is
reshaped into `R30-12H-A`, then extended by `R30-12H-B` and
`R30-12H-C`. Every later source task continues from the approved cumulative
tree. The final allowlist and commit remain unchanged.

Rollback before `R30-15` discards the uncommitted candidate. Rollback after
`R30-15` reverts the one atomic landing commit.

## Documentation And Teaching Plan

RFC 0030 and its tracker are synchronized to the three replacement tasks. No
language specification or user documentation changes.

## Operational Readiness

Review bookkeeping records exact predecessor and candidate hashes for all
three tasks. The existing schema, formatting, English-only, internal
versioning, architecture, build, and native-test gates remain required.

## Acceptance Criteria

- RFC 0030 and its tracker name the three replacement tasks and exact
  dependencies.
- Each replacement task counts all three named files and retains the 400-line
  limit.
- The callable position-name invariant is explicit.
- Aggregate evidence requires populated canonical parameter sequences.
- `R30-12I` depends on `R30-12H-C`.
- The exact landing set, immutable base, and atomic commit remain unchanged.
- `rfc`, `binder-checker`, and `verification` approve one unchanged proposal
  hash and tracker hash.
- `python3 scripts/check-rfc.py` passes.

## Implementation Plan

1. Accept this design-only synchronization transaction.
2. Reshape the rejected candidate into `R30-12H-A` and obtain exact-hash
   Binder and verification approval.
3. Add and approve `R30-12H-B`.
4. Add and approve `R30-12H-C`.
5. Resume RFC 0030 `R30-12I`.
6. Land all accumulated source only through RFC 0030 `R30-15`.

## Test Plan

- RFC structure: `python3 scripts/check-rfc.py`.
- Schema: `python3 scripts/check-stable-binding-schema.py --check`;
  `python3 scripts/check-stable-binding-schema.py --self-test`.
- Format: `python3 scripts/check-format.py`.
- Repository language: `python3 scripts/check-english-only.py --check
  --base-file products/zomlang/tests/coverage/implementation-series-base.txt`.
- Internal naming: `python3 scripts/check-no-internal-versioning.py --check`.
- Diff hygiene: `git diff --check`.
- Native evidence after source wiring: RFC 0030 Test Plan.

## Open Questions

None

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-07-28 | DRAFT | Initial proposal. |
| 2026-07-28 | REVIEW | Ready for exact-hash owner review. |
| 2026-07-28 | ACCEPTED | All required owners approved proposal SHA-256 `3fc78517c36a5794e01bcaca2dcca8d2a616a04b8737f2e2225282a47eea0422`. Acceptance transaction `rfc0033-accept-20260728-3fc78517` synchronizes RFC 0030, its tracker, RFC 0033, its tracker, and the RFC index without changing source, the immutable base, or the atomic landing boundary. |
| 2026-07-28 | SUPERSEDED | RFC 0034 transaction `rfc0034-accept-20260728-09802348` replaces the unworkable review graph with independently bounded fact reviews and codec-before-aggregate dependencies. No RFC 0033 source task landed. |
