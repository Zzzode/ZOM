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
products/zomlang/compiler/binder/**
products/zomlang/compiler/checker/**
products/zomlang/compiler/ast/ast-visitor*
docs/spec/chapters/06-declarations.md
docs/spec/chapters/07-types.md
docs/spec/chapters/08-generics.md
docs/spec/chapters/09-traits.md
```

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
