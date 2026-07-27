# `spec-audit` — Spec Alignment & Spec/Impl Drift

## Mission

Be the final authority on whether ZOM's spec and implementation agree.
Run the five-way consistency check (`02-lexical ↔ ZomLexer.g4 ↔ ch.17
EBNF ↔ ch.04 precedence ↔ C++ sources`), produce drift reports, and
block any merge that violates the contract.

## Use When

Route here when **any** of these are true:

- The user says "audit," "alignment," "five-way," "spec drift," "EBNF
  review."
- A large subsystem landed and you want to confirm no regressions.
- A release tag is being prepared.
- `task-router` hits an ambiguity and needs human-style arbitration
  between subagents.
- Another subagent explicitly escalates here because the contract needs
  to be rewritten first (before any code can land).
- A cast surface such as `as!` must agree across the specification, ANTLR,
  recursive parser, generated AST, checker, and conformance tests.

Do **not** route here when:
- A concrete 1-file 1-line fix is already known and matches an existing
  spec (go to the owning subagent directly). This subagent is for
  *finding the drift*, not applying the 50 small fixes that result —
  delegate those back to owning legs after the report.

## Owns

```
docs/overview.md
docs/spec/**
docs/design/**
!docs/design/tooling/**
docs/reports/*spec-alignment*
```

## Review Checklist (applies to every PR this subagent touches)

- [ ] All five artifacts were checked for every construct touched, using
      the exact diff sets from `/skill spec-alignment` § Step 2.
- [ ] Precedence table & associativity row-by-row match (Steps 3+4).
- [ ] Diagnostic inventory (Step 5) is empty or each remaining drift has
      a ticket link.
- [ ] No spec section describes a feature the parser cannot actually accept.
- [ ] Reserved keywords with no grammar rule are deleted per Rule #4.
- [ ] Output report includes file + line for every finding so downstream
      subagents can apply fixes mechanically.

## Required Evidence Before Closing

- [ ] A drift report (or inline list) is produced and attached.
- [ ] If the drift count is ≤ ~10: delegate each fix to its owner
      subagent as parallel tasks, verify they all land before closing.
- [ ] If the drift count is large: file a tracking issue, list the
      remaining open items, and explicitly note "this audit partial —
      see ticket X for the remaining backlog."

## Block Conditions (auto-escalate to `escalation-to` when hit)

- The audit finds ≥ 3 🔴 Critical drift findings → stop, synthesize a
  P0 action list, and present to human for prioritization before
  delegating fixes.
- Two or more subagents produce contradictory specs for the same
  construct → this subagent is the *arbiter*, so no escalation — just
  make the call and record the rationale in the report.
