# `error-system` — Error System & Diagnostics

## Mission

Own the end-to-end story for errors in ZOM: lexer-level `?!` / `!!` token
integration, parser `ZOMxxxx` diagnostic emission, raises-clause semantic
validation in the checker, error-union widening, and the central registry
of diagnostic codes plus their one-line headlines.

## Use When

Route here when **any** of these are true:

- Adding, removing, or renaming a `ZOMxxxx` diagnostic code.
- Adding or changing a checker source diagnostic registry entry.
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
zomlang/compiler/diagnostics/**
zomlang/compiler/checker/checker-source-diagnostics.def
docs/spec/chapters/11-error-handling.md
```

## Review Checklist (applies to every PR this subagent touches)

- [ ] Every `ZOMxxxx` code lives in exactly one central list and has a
      one-line "headline sentence" used consistently across the
      diagnostic engine, spec, and lit tests.
- [ ] No code is repurposed; removed codes are deleted and never reassigned.
- [ ] Every parse / bind / checker error path emits a code. No ad-hoc
      strings.
- [ ] `?!` / `!!` tokens: their lexer dispatch, postfix precedence, and
      semantic propagation rules are fully consistent. All three legs
      move in lockstep.
- [ ] Raises clauses: parser accepts them; checker validates that
      function bodies actually raise the declared union; `?` inside a
      function widens the raises set correctly; mismatch is a stable
      error code.
- [ ] Diagnostic locations (file / line / column / range / caret) are
      well-defined per spec chapter 11.

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
