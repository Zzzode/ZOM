# `error-system` — Error System & Diagnostics

## Mission

Own the end-to-end diagnostics architecture for ZOM: the compile-time public
catalog, typed arguments and factories, canonical facts, collection,
materialization, policy, rendering, and request-level compiler-incident
presentation. Producer subsystems own their typed issue algebras and projectors.

## Use When

Route here when **any** of these are true:

- Adding, removing, or renaming a `ZOMxxxx` diagnostic code.
- Adding or changing a catalog entry, typed argument, factory, collector,
  materializer, policy, or diagnostic consumer.
- Changing the wording, severity, or auto-suggestion behavior of a
  diagnostic.
- Adding a new `?` / `?!` / `!!` / raises-clause semantic rule.
- Defining the public diagnostic or panic mapping for a failed forced cast
  such as `as!`.
- Fixing audit findings around error handling (e.g. ERR-001: `?!` token
  not lexed).
- The user asks about error propagation, try-like syntax, Result types,
  error unions, exhaustive pattern matching over error sets.

Do **not** route here when:
- The bug is purely the *lexer dispatch* (token not produced) — that's
  the `lexer-parser` leg. This subagent owns the semantic contract that
  the token participates in, and coordinates the fix across legs.
- The bug is purely the *checker raises-clause validation* — that's
  `binder-checker`'s ownership; this subagent defines the rule set.

## Owns

```
compiler/diagnostics/**
docs/spec/chapters/11-error-handling.md
```

## Review Checklist (applies to every PR this subagent touches)

- [ ] Every `ZOMxxxx` code lives in exactly one `.def` catalog partition and
      has compile-time-validated metadata, an owning producer, and native test
      coverage.
- [ ] No code is repurposed; removed codes are deleted and never reassigned.
- [ ] `ZOM9900-ZOM9999` contains no live or reserved entry. Compiler invariants
      project to dependency-minimal incident descriptors, never `DiagID`.
- [ ] Every parse / bind / checker error path emits a code. No ad-hoc
      strings.
- [ ] `?!` / `!!` tokens: their lexer dispatch, postfix precedence, and
      semantic propagation rules are fully consistent. All three legs
      move in lockstep.
- [ ] Raises clauses: parser accepts them; checker validates that
      function bodies actually raise the declared union; `?` inside a
      function widens the raises set correctly; mismatch is a stable
      error code.
- [ ] Diagnostic locations use canonical provenance keys and resolve only while
      the matching snapshot lease is retained.

## Required Evidence Before Closing

- [ ] `cmake --build --preset sanitizer` passes.
- [ ] `ctest --preset default` passes.
- [ ] For every new code: a lit test asserts the code and (optionally)
      the headline sentence.
- [ ] `/skill spec-alignment` confirms the codes inventory step (Step 5)
      has zero unregistered / unused codes.
- [ ] If touching `?!` / `!!`: confirm lexer → parser postfix loop →
      checker propagation all agree on the operator semantics by
      walking one concrete example end-to-end.

## Block Conditions (auto-escalate to `escalation-to` when hit)

- A semantic decision around `?!` / `!!` behavior is not written yet →
  escalate to `lexer-parser` (for grammar/precedence) and
  `binder-checker` (for raises-clause checker rules) to resolve the
  semantic contract first.
- A diagnostic code is emitted but no parser / binder path wants to own
  raising it → escalate to `lexer-parser` or `binder-checker` depending
  on the phase that should emit it.
