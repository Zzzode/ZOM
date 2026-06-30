---
rfc: 0
title: RFC Template
type: process
status: TEMPLATE
author: ZOM Compiler Team
review-manager: ZOM Compiler Team
required-owners: []
approvers: []
created: 2026-06-30
updated: 2026-06-30
area: process
requires: []
supersedes: []
superseded-by: []
discussion: TBD
decision: TBD
implementation: TBD
tracking-issue: TBD
---

# RFC NNNN: Title

## Summary

One short paragraph describing the proposed change and the decision this RFC
asks reviewers to accept.

## Motivation

Describe the problem, the current constraints, and why the issue must be solved
now. Be specific about the user, implementer, or test workflow that is blocked.

## Goals

- Goal 1.
- Goal 2.
- Goal 3.

## Non-Goals

- Non-goal 1.
- Non-goal 2.
- Non-goal 3.

## Prior Art

List mature designs that solve the same class of problem. Prefer compiler,
language, or tooling sources with production usage.

For each item, explain what ZOM should copy, what ZOM should avoid, and why the
comparison is relevant.

## Guide-Level Explanation

Explain the proposal as a user or contributor would experience it after it
exists. Use examples. Avoid implementation internals unless users need them.

## Reference-Level Design

Define the exact contract an implementer must follow. Include syntax,
semantics, data structures, command-line behavior, error behavior, generated
artifacts, and deterministic ordering rules as applicable.

Use Mermaid diagrams for architecture or state transitions.

## Repository Impact

List every affected path family and owning subagent.

| Area | Paths | Owner |
|---|---|---|
| Example | `path/**` | `subagent-id` |

## Security And Safety Impact

Describe memory safety, concurrency safety, capability boundaries, sandboxing,
unsafe behavior, diagnostics that prevent misuse, and data exposure risks.

If there is no security or safety impact, write `None` and explain why the
proposal cannot affect those boundaries.

## Drawbacks And Risks

List the strongest reasons not to accept this RFC, the main implementation
risks, and how the design limits the blast radius.

## Alternatives Considered

List serious alternatives and explain why this RFC does not choose them.

Keep the comparison technical and neutral. Do not preserve superseded syntax,
superseded APIs, or counter-examples as normative content.

## Compatibility And Rollout

Describe how the repository moves from the current state to the proposed state.
This is not a forward-compatibility promise; it is the implementation and test
rollout plan for the current repository.

Include affected generated files, conformance snapshots, user-visible commands,
and rollback cost.

## Documentation And Teaching Plan

List the spec chapters, design docs, examples, diagnostics, release notes, and
developer-facing docs that must change. Explain how contributors or users should
learn the new behavior.

If no documentation or teaching work is needed, write `None`.

## Operational Readiness

List any runtime, CLI, CI, release, observability, performance, or maintenance
concerns that must be ready before landing. Include ownership for recurring
operational work.

If the proposal has no operational surface, write `None`.

## Acceptance Criteria

List the concrete evidence required to call this RFC complete. Criteria should
be observable through files, tests, generated artifacts, command output, review
approval, or status links.

## Implementation Plan

1. Step one.
2. Step two.
3. Step three.

## Test Plan

List the exact verification required before this RFC can move to `LANDED`.

- Build:
- Unit tests:
- Lit tests:
- Conformance:
- Generated files:
- Format:

## Open Questions

- Question 1.
- Question 2.

If there are no open questions, write `None`. Accepted RFCs must have no
blocking open questions.

## Status History

| Date | Status | Notes |
|---|---|---|
| YYYY-MM-DD | DRAFT | Initial draft. |
