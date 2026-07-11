# `module-system` — Module System & Visibility

## Mission

Own semantic identity and source provenance, the module graph, import/export
semantics, member visibility, package layout, cross-module `CompilerSession`
coordination, and the `Export` flag write path.

## Use When

Route here when **any** of these are true:

- Import, export, or package-path resolution is wrong.
- Context brands, canonical identity keys, source provenance, or semantic
  handle ancestry changes.
- Module export, member visibility modifiers, or re-exports change.
- `CompilerSession` needs to gain / change cross-module state (symbol
  registries, dependency edges, parallel compilation scheduling).
- The `Export` `SymbolFlag` finally gets written (audit finding MOD-007).
- Adding a whole-program analysis that spans translation units.

Do **not** route here when:
- The issue is *name lookup order within a single TU* — that belongs to
  `binder-checker` (this subagent owns *cross-TU* lookup routing).
- The issue is purely a syntax change for the `import` / `mod` keywords
  — `lexer-parser` owns that; this subagent owns the semantic leg.

## Owns

```
products/zomlang/compiler/symbol/**
products/zomlang/compiler/driver/**
products/zomlang/compiler/identity/**
products/zomlang/compiler/source/**
docs/spec/chapters/13-modules-and-imports.md
docs/spec/chapters/23-visibility-ladder.md
```

## Review Checklist (applies to every PR this subagent touches)

- [ ] `CompilerSession` remains the single owner of `SourceManager`,
      `DiagnosticEngine`, cross-module symbol registry, and the list of
      `TranslationUnit` handles. No phase constructs these privately.
- [ ] Driver phase order matches pipeline contract: parse → bind → check
      → … No heuristics that skip a phase per-file.
- [ ] Cross-module identity flows through `CompilerSession` APIs. No
      direct pointer walks from one `SymbolTable` into another.
- [ ] Context and registry brands have one explicit issuer, are never
      serialized, and are validated before handle lookup.
- [ ] Canonical identities and source ranges contain no process-local pointer,
      `BufferId`, table slot, or iteration-order dependency.
- [ ] `Export` flag has at least one write site (per `addFlag`/`setFlag`
      grep). If it still does not after the PR → blocker.
- [ ] Module export and member visibility map 1:1 onto Chapters 13 and 23.
- [ ] Circular import error detection produces a deterministic `ZOMxxxx`
      diagnostic, not a stack overflow or panic.

## Required Evidence Before Closing

- [ ] `cmake --build --preset sanitizer` passes.
- [ ] `ctest --preset default` passes.
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
