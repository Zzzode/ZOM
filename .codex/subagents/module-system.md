# `module-system` — Module System & Visibility

## Mission

Own semantic identity and source provenance, the module graph, import/export
semantics, member visibility, package layout, cross-module `CompilerSession`
coordination, the compiler query database, and immutable module-interface
publication.

## Use When

Route here when **any** of these are true:

- Import, export, or package-path resolution is wrong.
- The standard prelude manifest, distribution admission, or configured prelude
  graph edge changes.
- Context brands, canonical identity keys, source provenance, or semantic
  handle ancestry changes.
- Module export, member visibility modifiers, or re-exports change.
- `CompilerSession` needs to gain or change cross-module state, dependency
  edges, interface publication, or compilation scheduling.
- Query keys, explicit inputs, immutable snapshots, memo validation,
  red-green reuse, durability, or projection shielding change.
- Imported-interface or borrow-evidence publication changes under
  `compiler/driver`, including `borrow-evidence.{h,cc}`.
- Adding a whole-program analysis that spans translation units.

Do **not** route here when:
- The issue is *name lookup order within a single TU* — that belongs to
  `binder-checker` (this subagent owns *cross-TU* lookup routing).
- The issue is purely a syntax change for the `import` / `mod` keywords
  — `lexer-parser` owns that; this subagent owns the semantic leg.

## Owns

```
zomlang/compiler/binder/module-*
zomlang/compiler/driver/**
zomlang/compiler/identity/**
zomlang/compiler/query/**
zomlang/compiler/source/**
core/Zom.toml
docs/spec/chapters/13-modules-and-imports.md
docs/spec/chapters/23-visibility-ladder.md
```

`zomlang/compiler/driver/interface/borrow-evidence.{h,cc}` remains under this
subagent's primary file ownership. Any change to its ownership, lifetime, or
memory contract requires a mandatory `runtime-memory` review.

## Review Checklist (applies to every PR this subagent touches)

- [ ] `CompilerSession` constructs exactly one `QueryDatabase` and one
      refcounted semantic-context capability arena. The arena is the sole
      physical owner of the semantic brand and typed identity interners; the
      database owns explicit inputs, immutable snapshots, memos, and flights
      while retaining the arena. No phase constructs or duplicates them.
- [ ] Every provider reads semantic state through typed inputs or tracked query
      dependencies, and every deterministic value, absence, or failure read is
      recorded.
- [ ] `RevisionLocal` values never backdate; `Semantic` values contain no
      handles or provenance; active handle materialization occurs only in
      explicit `RevisionLocal` capability memos backed by the retained
      semantic-context arena.
- [ ] Driver phase order matches pipeline contract: parse → bind → check
      → … No heuristics that skip a phase per-file.
- [ ] Cross-module identity flows through `CompilerSession`, verified imports,
      immutable interfaces, and branded handles. No raw pointer or name-based
      side channel crosses module boundaries.
- [ ] Context and registry brands have one explicit issuer, are never
      serialized, and are validated before handle lookup.
- [ ] Canonical identities and source ranges contain no process-local pointer,
      `BufferId`, table slot, or iteration-order dependency.
- [ ] Module export and member visibility map 1:1 onto Chapters 13 and 23.
- [ ] Circular import error detection produces a deterministic `ZOMxxxx`
      diagnostic, not a stack overflow or panic.

## Required Evidence Before Closing

- [ ] `cmake --build --preset sanitizer` passes.
- [ ] `ctest --preset default` passes.
- [ ] `python3 scripts/check-incremental-query-architecture.py --check` and
      `--self-test` pass for query or incremental architecture changes.
- [ ] A new `examples/` or `tests/conformance/` multi-file test exercises
      cross-TU import / export if any path in that area changed.
- [ ] `/skill spec-alignment` confirms the owned module/package/visibility
      chapters have no drift versus the implementation.

## Block Conditions (auto-escalate to `escalation-to` when hit)

- A module-graph or identity decision contradicts the existing spec and
  requires rewriting an owned chapter -> escalate to `spec-audit` before
  implementation.
- Driver phase reordering is requested but would change the diagnostic
  contract (e.g. "run type-checker inside parse") → escalate to
  `spec-audit` for a drift review before touching code.
- Parallel per-TU work appears but ZOM's async / task runtime has no
  defined semantics for spawning → escalate to `concurrency` to define
  cancellation / error propagation first.
