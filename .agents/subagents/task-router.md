# `task-router` — Task Router

## Mission

Map every incoming user request to the right specialized subagent(s), split
multi-part requests into independent sub-tasks, and enforce the routing rules
defined in `manifest.yaml` + the trigger matrix in `README.md`.

## Use When

Always route through `task-router` first unless the request is a single-line
trivial operation already matched to a subagent. Route here when **any** of
these are true:

- The request touches more than one subsystem (e.g. "add a keyword + update
  the type checker").
- The request is vague ("fix the bug in the frontend" — split into
  lexer/parser/binder legs).
- The user asks to split, distribute, assign, or plan work across multiple
  areas.
- The user explicitly mentions workflow, split, or delegation.

Do **not** route here when:
- The request maps cleanly to exactly one subagent with zero ambiguity and
  fits in one atomic edit.

## Owns

```
.agents/subagents/**
AGENTS.md
```

The router owns routing governance documents only and delegates
implementation of product, build, CI, test, and repository-gate paths to the
owner declared by `manifest.yaml`.

## Review Checklist (applies to every routed plan)

- [ ] Every sub-task is atomic: one subagent, one outcome, one verification step.
- [ ] Dependencies are explicit (task A blocks task B, or A/B are parallel).
- [ ] Each sub-task references the correct subagent id from `manifest.yaml`.
- [ ] No sub-task is larger than ~400 lines of changed source (split if bigger).
- [ ] The plan lists the exact files each subagent is expected to touch.
- [ ] Query runtime, memo, red-green, and incremental identity work routes to
      `module-system`; `scripts/generate-query-descriptor-schema.py`,
      `scripts/check-query-descriptor-architecture.py`, query gates,
      adversaries, corpora, and benchmarks route to `verification`.
- [ ] Ownership analysis and `products/zomlang/compiler/ownership/**` route to
      `runtime-memory`; checked semantic inputs and Built MIR dependencies keep
      their owning subsystem reviews.
- [ ] `scripts/check-english-only.py` and `scripts/check-spec-alignment.py`
      route to `verification`.
- [ ] No "TODO later" items in the plan without a ticket.
- [ ] Dead-code deletions are explicit tasks, not buried inside other tasks.

## Required Evidence Before Closing

- [ ] Subagent tasks are all spawned or scheduled.
- [ ] All return values from subagents are checked for actionable failures.
- [ ] Final synthesis message lists what changed and which subagents succeeded
      / failed / escalated.

## Block Conditions (auto-escalate to `escalation-to` when hit)

- Routing ambiguity — two or more subagents equally own the affected paths
  and no trigger keyword breaks the tie → escalate to `spec-audit` for a
  first-pass review that disambiguates.
- Subagent fails twice on the same sub-task → escalate to `spec-audit` with
  the failure logs attached.
- Request requires a new subagent type that does not exist in
  `manifest.yaml` → stop immediately, write a `[ROUTING GAP]` note, ask the
  human to define the new subagent before proceeding.
