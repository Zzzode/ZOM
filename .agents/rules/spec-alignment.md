---
paths:
  - "docs/spec/**"
  - "products/zomlang/compiler/**"
---

# Spec ↔ Implementation Alignment Rules

> The number one source of P0 bugs in ZOM today is *spec drift*: the spec says X,
> the parser accepts Y, the binder produces Z, and the diagnostic engine prints
> W.  Nothing kills an early language faster than an implementation that does
> not match its own spec.  Use `/skill spec-alignment` for the automated check.

---

## The Five-Way Consistency Rule

Every syntactic or semantic construct must match in all five places.
Missing any one of them is a bug, even if the other four agree.

| # | Artifact | Owner Path | Canonical Format |
|---|---|---|---|
| 1 | **Lexical chapter** | `docs/spec/chapters/02-lexical-structure.md` | Keyword tables, reserved words, regex-like lexical rules |
| 2 | **Lexer grammar** | `docs/spec/ZomLexer.g4` | ANTLR grammar tokens |
| 3 | **Grammar reference** | `docs/spec/chapters/17-grammar-reference.md` | EBNF productions |
| 4 | **Expression semantics chapter** | `docs/spec/chapters/04-expressions.md` | Precedence + associativity tables |
| 5 | **Implementation** | `compiler/lexer/*.cc`, `compiler/parser/*.cc`, `compiler/ast/kinds.h` | C++ source |

### Workflow for Any Construct Change

1. Update the lexical chapter (1) and `ZomLexer.g4` (2).
2. Update the EBNF in chapter 17 (3).
3. Update the precedence / semantics in chapter 04 (4) *if* it involves an operator.
4. Update `ast/kinds.h` + lexer + parser (5) to match (1-4).
5. Add or regenerate a lit test under `tests/language/` that FileChecks the
   resulting AST / diagnostic.
6. Run `/skill spec-alignment` to confirm no regressions in the other 40 constructs.

**Do not do (5) without (1-4).** Do not do (1-4) without (5).
All six steps, one commit.

---

## Spec Must Reflect Reality, Not Wishful Thinking

The spec is **not a design wishlist or a product roadmap.**

| ❌ Bad | ✅ Good |
|---|---|
| Chapter describes an `async fn` feature whose grammar produces a parse error. | Chapter says "Concurrency syntax is reserved for a future version; today the parser rejects `async`." Explicit, honest, no ambiguity. |
| Precedence table says `?!` is priority 17 while EBNF says postfix. | Fix whichever is wrong; no drift. |
| Modifier Keywords table includes 15 words but EBNF `Modifier` has 7. | Either shrink the table or expand the grammar; pick one and justify. |
| A reserved keyword has no grammar rule and no "reserved for v2" note. | Delete the keyword from the reservation list per Rule #4. |

### Hard Rule for Wishful Spec

> If a subsection of the spec has no corresponding parser acceptance test under
> `tests/language/` and no parser path that produces it, the subsection must be
> re-written to honestly describe what the parser *does* accept today, OR the
> construct must be fully implemented in the same PR.

No half-done chapters. No "TODO: finish this" sections.
**The spec is a contract. If we have not yet signed it, do not publish it.**

---

## Diagnostic Codes (`ZOMxxxx`)

- Codes live in one authoritative place. Add new codes only by extending that list.
- Every parser / binder / checker error path uses a code.
- A code must map to exactly one "headline sentence" used consistently across
  `diagnostic/*`, lit tests, and any user-facing documentation.
- **Do not re-use codes for different errors.** A deprecated code can be removed
  per Rule #3 (no forward compat) but never repurposed.

---

## Audit Reports as Drift Detection

The files under `docs/reports/zom-*-audit-*.md` are generated outputs that
enumerate **spec/impl discrepancies**. Each finding lists concrete file paths
and line numbers.

When implementing a fix for a finding:

1. Fix the construct in the five artifacts above.
2. Re-run the audit skill to confirm the finding is no longer reproducible.
3. Move the finding from "Open" to "Fixed" in the next version of the report, or
   (better) delete the finding entirely if the report is regenerated.
4. Leave the original report file intact and append a small section
   "Post-publication updates" linking to the fix commit.
