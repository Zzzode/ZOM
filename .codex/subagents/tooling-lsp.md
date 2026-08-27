# `tooling-lsp` - IDE Semantics And Language Server

## Mission

Own the editor-facing semantic facade, Language Server Protocol adapter,
document-version discipline, request cancellation, and resilient IDE behavior
over incomplete ZOM source without weakening compiler verification.

## Use When

Route here when **any** of these are true:

- Adding or changing hover, completion, signature help, definition,
  references, rename, semantic tokens, inlay hints, code actions, or IDE
  diagnostics.
- Defining editor document versions, request-scoped analysis leases,
  cancellation, stale-response suppression, or diagnostic refresh behavior.
- Mapping source positions to recoverable syntax, binding, type, or flow facts.
- Adding the IDE semantic facade or the JSON-RPC/LSP transport adapter.
- Integrating an editor extension with the ZOM language server.

Do **not** route here when:

- The language type rule itself changes - `binder-checker` owns that rule.
- Query snapshots, memo validation, or red-green reuse change -
  `module-system` owns the query-runtime leg.
- Parser recovery node shape changes - `lexer-parser` owns that syntax leg.
- Diagnostic IDs, severity, or source messages change - `error-system` owns
  the diagnostic registry.
- HIR, MIR, LIR, or native emission changes - `ir-backend` owns compiler IR.

## Owns

```text
tools/ide/**
tools/lsp/**
editors/**
docs/design/tooling/**
```

## Review Checklist (applies to every PR this subagent touches)

- [ ] IDE analysis and compiler verification remain separate authority rails.
      No recovered or partial IDE fact can enter `VerifiedCheckedFacts`, HIR,
      MIR, LIR, or code generation.
- [ ] Every request is bound to one immutable query snapshot and exact stamps
      for the complete transitive query-input frontier it reads.
- [ ] A stale request publishes no semantic payload and receives exactly one
      terminal protocol error. A stale server-originated notification is
      suppressed.
- [ ] Cancellation publishes no semantic memo, diagnostic result, or partial
      response payload, and every cancelled request receives its terminal
      cancellation error.
- [ ] Incomplete syntax uses closed recovery states. It never fabricates a
      `DefId`, `SemanticTypeId`, callable selection, or non-null proof.
- [ ] Recovered bindings are explicitly distinguished from verified bindings.
      Only verified bindings may authorize rename or another destructive edit.
- [ ] Recovery-local identities remain revision-local, never enter semantic
      query values, and cannot escape their analysis lease.
- [ ] Hover and completion consume editor-facing value objects. Compiler
      handles, AST nodes, query internals, and semantic types are not serialized
      directly into LSP payloads.
- [ ] Valid-source IDE flow types equal the independently verified compiler
      tooling projection for the same body and revision.
- [ ] Feature degradation is local: one missing expression or unresolved name
      does not erase unrelated syntax, binding, or type results.
- [ ] Stable-body semantic projections backdate on exact equality so an edit in
      one body does not execute binding or analysis for unrelated owners.
- [ ] Protocol ordering, sorting, ranges, edits, and diagnostics are
      deterministic.

## Required Evidence Before Closing

- [ ] `cmake --build --preset sanitizer` passes.
- [ ] `ctest --preset default` passes.
- [ ] IDE fixture tests cover incomplete syntax, partial binding, flow-sensitive
      hover and completion, verified/recovered binding authority,
      recovery-local identities, cancellation, and stale-response suppression.
- [ ] LSP integration tests exercise initialize, open/change/close, hover,
      completion, verified rename, push diagnostics, terminal stale and
      cancellation errors, workspace admission, and shutdown over framed
      JSON-RPC.
- [ ] Differential tests prove valid-source IDE facts equal compiler tooling
      projections.
- [ ] `python3 scripts/check-format.py` and
      `python3 scripts/check-incremental-query-architecture.py --check` pass.

## Block Conditions (auto-escalate to `escalation-to` when hit)

- An IDE feature needs a new language rule or changes an effective type ->
  escalate to `binder-checker`.
- A request cannot obtain one revision-consistent source and semantic snapshot
  -> escalate to `module-system`.
- Incomplete syntax requires a new recovery-node contract -> escalate to
  `lexer-parser`.
- A diagnostic needs a new code, precedence, severity, or message -> escalate
  to `error-system`.
- A recovered fact would need to authorize executable lowering -> block the
  design and escalate to `verification`.
