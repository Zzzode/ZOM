# `module-system` — Module System & Visibility

## Mission

Own the module graph, import/export semantics, visibility (`pub` /
`pub(crate)` / `pub(path, …)`), package layout, cross-module
`CompilerSession` coordination, and the `Export` flag write path.

## Use When

Route here when **any** of these are true:

- Import, export, or package-path resolution is wrong.
- `pub`, `pub(crate)`, visibility modifiers, or re-exports change.
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
docs/spec/chapters/12-modules.md
docs/spec/chapters/13-visibility.md
docs/spec/chapters/14-package.md
```

## Review Checklist (applies to every PR this subagent touches)

- [ ] `CompilerSession` remains the single owner of `SourceManager`,
      `DiagnosticEngine`, cross-module symbol registry, and the list of
      `TranslationUnit` handles. No phase constructs these privately.
- [ ] Driver phase order matches pipeline contract: parse → bind → check
      → … No heuristics that skip a phase per-file.
- [ ] Cross-module identity flows through `CompilerSession` APIs. No
      direct pointer walks from one `SymbolTable` into another.
- [ ] `Export` flag has at least one write site (per `addFlag`/`setFlag`
      grep). If it still does not after the PR → blocker.
- [ ] Visibility modifiers `pub(...)` map 1:1 onto the spec chapter 13
      enumeration.
- [ ] Circular import error detection produces a deterministic `ZOMxxxx`
      diagnostic, not a stack overflow or panic.

## Required Evidence Before Closing

- [ ] `cmake --build --preset sanitizer` passes.
- [ ] `ctest --preset default` passes.
- [ ] A new `examples/` or `tests/conformance/` multi-file test exercises
      cross-TU import / export if any path in that area changed.
- [ ] `/skill spec-alignment` confirms chapters 12/13/14 have no drift vs
      the implementation.

## Block Conditions (auto-escalate to `escalation-to` when hit)

- A module-graph decision contradicts the existing spec and requires
  rewriting chapters 12/13/14 → escalate to `spec-audit` to spec the
  change first, implement second.
- Driver phase reordering is requested but would change the diagnostic
  contract (e.g. "run type-checker inside parse") → escalate to
  `spec-audit` for a drift review before touching code.
- Parallel per-TU work appears but ZOM's async / task runtime has no
  defined semantics for spawning → escalate to `concurrency` to define
  cancellation / error propagation first.
