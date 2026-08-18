---
name: rfc
description: Author, review, and maintain ZOM RFCs under docs/rfc. Use this whenever the user asks for an RFC, proposal, design intake, governance change, architecture decision, accepted design, status transition, or process template, even if they do not explicitly mention the rfc skill.
---

# RFC Authoring And Review Skill

Use this skill for any durable design proposal that belongs under `docs/rfc/`.
The goal is to make ZOM design decisions reviewable before implementation,
grounded in mature prior art, and connected to the owning subagents.

## Core References

Read these files before drafting or reviewing an RFC:

- `docs/rfc/README.md`
- `docs/rfc/0000-template.md`
- `.agents/rules/design-principles.md`
- `.agents/subagents/manifest.yaml`

For technical RFCs, also read the affected spec, design, compiler, runtime, or
test files before writing recommendations.

## When An RFC Is Required

Write or request an RFC when a change:

- Changes language syntax, semantics, type rules, diagnostics, modules,
  concurrency, memory behavior, or standard prelude behavior.
- Changes compiler architecture, AST contracts, binder/checker contracts, IR
  contracts, runtime contracts, or test/conformance architecture.
- Adds or changes a repository-wide process, agent, skill, or verification
  gate.
- Introduces a user-visible command, output format, file format, or tool
  contract.
- Requires coordination across multiple subagents.

Do not require an RFC for a local bug fix, formatting cleanup, narrow refactor,
or drift repair when the intended contract is already written.

## Authoring Workflow

1. Inspect the current repository state and read the relevant source docs.
2. Survey prior art before inventing a design.
3. Copy `docs/rfc/0000-template.md` to the next unused RFC number and slug.
4. Fill the YAML frontmatter.
5. Write every required section from the template.
6. Keep the proposal in English and use Mermaid for architecture or state
   diagrams.
7. Assign `type`, `review-manager`, and the affected owner list before review.
8. List affected paths and owning subagents in `Repository Impact`.
9. Include drawbacks, rollout impact, an ordered implementation plan, and a
   concrete test plan.
10. Add or update the `docs/rfc/README.md` RFC Index row.
11. Run `python3 scripts/check-rfc.py` and fix any structural failures.
12. Set `status: REVIEW` only when the document is ready for review and has
    discussion plus tracking links.
13. Move to `ACCEPTED` only after structural and technical blockers are
    resolved.

## Review Checklist

- [ ] Frontmatter matches `docs/rfc/README.md`.
- [ ] `type`, `review-manager`, `discussion`, `decision`, `implementation`,
      and `tracking-issue` fields are present.
- [ ] `required-owners` exactly matches owners listed in `Repository Impact`.
- [ ] Status is one of the approved RFC status values.
- [ ] The RFC has all required sections in the template order.
- [ ] The RFC Index row matches the proposal frontmatter.
- [ ] Goals and non-goals are concrete enough to control scope.
- [ ] Prior art includes mature references or explains why fewer apply.
- [ ] The reference-level design is implementable without major invention.
- [ ] Repository impact lists all affected path families and owners.
- [ ] Security and safety impact is covered or explicitly marked `None`.
- [ ] Drawbacks, risks, compatibility, rollout, and rollback cost are covered.
- [ ] Alternatives are technical and neutral.
- [ ] Documentation, teaching, and operational readiness impacts are covered or
      explicitly marked `None`.
- [ ] Acceptance criteria list concrete evidence required to call the RFC
      complete.
- [ ] The implementation plan is ordered and testable.
- [ ] The test plan names exact build, lit, unit, conformance, generated-file,
      or format checks as applicable.
- [ ] Affected owners have signed off before the RFC moves to `ACCEPTED`, or
      any remaining objections are recorded as non-blocking.
- [ ] `Open Questions` is `None` before acceptance, unless each item is marked
      non-blocking and assigned to follow-up tracking.
- [ ] The RFC does not add compatibility shims or unused placeholders.
- [ ] Internal types, canonical domains, schemas, fixtures, and generated
      artifacts use unversioned names and define only the current contract.

## Status Changes

Update the RFC's `updated` field and `Status History` table whenever changing
status.

Allowed transitions are defined in `docs/rfc/README.md`. Do not skip from
`DRAFT` to `ACCEPTED`; move through `REVIEW`.

Before moving to `ACCEPTED`, replace `TBD` values for `discussion`, `decision`,
and `tracking-issue`, and add approvers for every affected owner.

Run `python3 scripts/check-rfc.py` after every RFC edit and before reporting the
work complete.

## Output Expectations

When creating or editing an RFC, report:

- RFC path and status.
- Important design choices.
- Any unresolved open questions.
- Which subagents should review the technical surface.
- Verification performed, including `python3 scripts/check-rfc.py`.
