# `rfc` - RFC Governance

## Mission

Own the ZOM RFC process, RFC templates, proposal structure, status transitions,
and cross-subagent design review routing.

## Use When

Route here when any of these are true:

- The user asks for an RFC, proposal, design intake process, architecture
  decision record, status transition, or RFC template.
- A change belongs under `docs/rfc/**`.
- A request changes repository-wide process, agent, skill, or governance
  behavior.
- A proposal needs prior-art enforcement before implementation.
- A proposal touches multiple subagents and needs review routing.

Do not route here when:

- A local bug fix already has a written contract and only needs an owning
  implementation subagent.
- A spec chapter must be audited for parser drift; route that to `spec-audit`.
- A test expectation must be regenerated; route that to `verification`.

## Owns

```text
docs/rfc/**
.agents/skills/rfc/**
```

## Review Checklist (applies to every PR this subagent touches)

- [ ] RFC frontmatter matches `docs/rfc/README.md`.
- [ ] RFC `type`, `review-manager`, `discussion`, `decision`,
      `implementation`, and `tracking-issue` fields are present.
- [ ] RFC `required-owners` exactly matches the `Repository Impact` owners.
- [ ] RFC Index row matches the proposal frontmatter.
- [ ] RFC status transition is allowed and recorded in `Status History`.
- [ ] Required template sections are present and ordered.
- [ ] Prior art is specific, mature, and relevant to the decision.
- [ ] Goals and non-goals control scope.
- [ ] Reference-level design gives implementers enough detail.
- [ ] Repository impact maps paths to owning subagents.
- [ ] Technical review is routed to every affected owner.
- [ ] Owner approvals are recorded before `ACCEPTED`.
- [ ] Blocking open questions are resolved before `ACCEPTED`.
- [ ] Security and safety impact is covered or explicitly marked `None`.
- [ ] Drawbacks, risks, compatibility, rollout, and rollback cost are covered.
- [ ] Documentation, teaching, and operational readiness impacts are covered or
      explicitly marked `None`.
- [ ] Acceptance criteria list concrete evidence required to call the RFC
      complete.
- [ ] Alternatives are neutral and do not keep unused API or syntax forms.
- [ ] Implementation and test plans are concrete enough to execute.
- [ ] All repository artifacts are written in English.

## Required Evidence Before Closing

- [ ] RFC file path and status are reported.
- [ ] Review manager, approvers, and tracking links are reported.
- [ ] Affected subagents are listed.
- [ ] Required follow-up reviews are identified.
- [ ] Markdown links and Mermaid diagrams are syntactically plausible.
- [ ] `python3 scripts/check-rfc.py` passes.
- [ ] No repository file contains new non-English content.

## Block Conditions (auto-escalate to `escalation-to` when hit)

- The RFC changes language semantics but does not route to `spec-audit`.
- The RFC changes AST, lexer, parser, or syntax tests but does not route to
  `lexer-parser` and `verification`.
- The RFC changes runtime or memory behavior but does not route to
  `runtime-memory`.
- The proposal lacks prior art for a mature design space.
- The proposal leaves core semantics undefined.
- The proposal is moving to `ACCEPTED` with missing owner signoff, unresolved
  blocking open questions, or `TBD` decision tracking.
