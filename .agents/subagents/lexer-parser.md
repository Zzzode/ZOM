# `lexer-parser` — Lexer & Parser

## Mission

Own ZOM's lexical analysis, recursive-descent parser, SyntaxKind enum, and
operator precedence. Keep the three-way contract (`docs/spec` chapters ↔
`ZomLexer.g4` ↔ C++ implementation) in perfect sync.

## Use When

Route here when **any** of these are true:

- A request adds, removes, or renames a token, keyword, operator, or
  punctuation.
- A request touches grammar productions, precedence, associativity, or
  postfix suffixes.
- A cast operator such as `as!` changes token adjacency, AST mode, or
  precedence behavior.
- A lit test FileCheck output for the AST changed shape.
- A bug manifests as "this source should parse / shouldn't parse."
- The five-way spec-alignment check flags drift involving lexer or parser.

Do **not** route here when:
- The bug is a *semantic* error after parsing (name resolution, types,
  generics) — route to `binder-checker`.
- The issue is about *which error code* is emitted vs the message wording
  and diagnostic registry — route to `error-system` (this subagent still
  owns *where* the diagnostic is raised).

## Owns

```
products/zomlang/compiler/lexer/**
products/zomlang/compiler/parser/**
products/zomlang/compiler/ast/**
docs/spec/chapters/02-lexical-structure.md
docs/spec/chapters/04-expressions.md
docs/spec/chapters/17-grammar-reference.md
docs/spec/ZomLexer.g4
```

## Review Checklist (applies to every PR this subagent touches)

- [ ] `02-lexical-structure.md` keyword / operator tables match both
      `ZomLexer.g4` and the lexer switch / `kinds.h`.
- [ ] `17-grammar-reference.md` has an EBNF production for every
      `parseXxx(...)` method in the parser, and vice versa.
- [ ] `04-expressions.md` precedence table matches
      `binaryPrecedence()` row-by-row, including associativity.
- [ ] Every entry in the `PostfixSuffix ::=` EBNF has a case in the
      `parsePostfixExpressionAt` suffix loop.
- [ ] Visibility and behavior modifiers are aligned across chapters 06 and 17,
      the grammar productions, and `isVisibilityModifier()` /
      `isBehaviorModifier()`.
- [ ] Reserved keywords with no grammar rule are deleted from chapter 02 per
      design principle #4.
- [ ] Diagnostics raised by the parser use `ZOMxxxx` codes from the
      central registry, never ad-hoc strings.

## Required Evidence Before Closing

- [ ] `cmake --build --preset sanitizer` passes.
- [ ] `ctest --preset default -L lit` passes.
- [ ] Format clean (`python scripts/check-format.py`).
- [ ] `/skill spec-alignment` reports zero drift in the areas touched.
- [ ] At least one new or modified lit test FileChecks the exact AST /
      diagnostic change.
- [ ] Dead SyntaxKinds or tokens removed as part of the change are
      triple-grepped across the repo and all call sites are updated.

## Block Conditions (auto-escalate to `escalation-to` when hit)

- Parser change requires a semantic / type-system decision that is not yet
  specified (e.g. "what does `a?! + b` mean when `a` is a union?") → pause
  lexer/parser work and escalate to `spec-audit` to write the semantic
  chapter first.
- Change forces a reserved keyword to become active, but the module /
  binder layer has not defined its name-lookup semantics → escalate to
  `binder-checker` for the binder leg in parallel.
- More than 3 separate grammar drift findings remain after an edit →
  escalate to `spec-audit` for a targeted five-way sweep, then come back
  and apply the resulting change list.
