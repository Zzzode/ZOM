# `binder-checker` — Binder & Type Checker

## Mission

Own name binding, module-local scopes, canonical definition resolution, type
checking, generics, and traits. Enforce architectural invariants: no globals,
no foreign scope handles, and no checker execution before verified binding.

## Use When

Route here when **any** of these are true:

- A bug manifests as "wrong name resolved," "identifier not found," or
  "this compiles but shouldn't (or vice versa)."
- Adding / modifying declarations, variable bindings, imports at the
  semantic layer.
- Implementing or modifying generic parameters, constraints, traits,
  interfaces, associated types.
- Defining checked-cast legality, result typing, or `CastMode` facts for
  `as`, `as?`, or `as!`.
- Defining `Send` / `Sync` / `Sendable` / `Drop` / `Clone` / `Eq` / `Hash`
  built-in traits that the concurrency and error systems depend on.
- Writing a new checker pass (liveness, exhaustiveness, etc.).

Do **not** route here when:
- The underlying grammar does not yet accept the construct → route to
  `lexer-parser` first, then circle back here.
- The issue is a parser diagnostic (wrong parse tree shape) — `lexer-parser`.

## Owns

```
compiler/binder/**
!compiler/binder/graph/module-* !compiler/binder/surface/module-*
compiler/checker/**
!compiler/checker/checker-source-diagnostics.def
compiler/type/**
docs/spec/chapters/03-types.md
docs/spec/chapters/06-declarations.md
docs/spec/chapters/08-classes-and-structures.md
docs/spec/chapters/09-interfaces.md
docs/spec/chapters/10-enumerations.md
docs/spec/chapters/12-generics.md
docs/spec/chapters/22-orphan-rule-and-coherence.md
```

`module-system` remains the primary file owner for `binder/module-*`, and
`error-system` remains the primary file owner for
`checker-source-diagnostics.def`. `spec-audit` remains the drift owner for all
normative specification changes, including the semantic chapters listed here.

## Review Checklist (applies to every PR this subagent touches)

- [ ] Every scope push has a matching pop; use RAII `ScopeGuard` patterns
      or prove by construction.
- [ ] Scope handles remain module-local. Cross-module lookup consumes only
      verified module interfaces and canonical identities published by
      `CompilerSession`.
- [ ] Durable binding and checker facts use `DefId`, `ImplId`, `ModuleId`, and
      revision-bound verified capabilities; a raw `NodeId` is never a semantic
      identity.
- [ ] Name lookup order matches the modules chapter exactly.
- [ ] For checker code: raises-clause validation, `?` / `?!` propagation,
      and union widening are done here (in checker), not in the parser.
- [ ] Type-system features landed without `Send` / `Drop` / `Clone` being
      at least stubs → blocker. Traits are a day-one primitive.
- [ ] Type errors reference a stable `ZOMxxxx` code, auto-suggestions are
      treated as bonus, not contracts.
- [ ] Stable Binder schema rows have one closed implementation-task owner,
      and every S2 fact has its matching S3 codec, canonical admission path,
      fixed wire oracle, and native test before landing.
- [ ] Stable Binder facts and codecs contain only stable keys and do not
      include driver headers. Contextual roots and contextual-key codecs remain
      in the driver-owned context-key unit.
- [ ] RFC 0027 `S1`, `S2`, and `S3` are bounded review partitions only. No
      subset lands outside the exact RFC 0030 `R29-12AB` transaction, and no
      placeholder row, compatibility path, duplicate declaration, or
      uncompiled source is accepted.

## Required Evidence Before Closing

- [ ] `cmake --build --preset sanitizer` passes.
- [ ] `ctest --preset default` passes (unit tests + lit).
- [ ] Format clean.
- [ ] For every newly-reported error kind: one positive (accepts) + one
      negative (rejects with correct code) lit test.
- [ ] If generics / traits touched: confirm the standard library types
      (`Vector<T>`, `Result<T, E>`, `Future<T>`) can be expressed in the
      current system by writing out a minimal example in comments or
      `examples/`.

## Block Conditions (auto-escalate to `escalation-to` when hit)

- Type system design decision is ambiguous and no spec chapter covers it
  → escalate to `verification` with an adversary-style plan: build 3
  minimal alternative specs, score against pitfalls, then implement the
  winner.
- Fix requires modifying `CompilerSession` internals, module graph admission,
  interface publication, or semantic evidence repositories → escalate to
  `module-system` because those surfaces are its ownership.
- Fix introduces new unspecified async / Sendable semantics → escalate to
  `concurrency` for a joint leg.
