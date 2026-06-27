# ZOM Grammar Test Suite

## Overview

The ZOM Grammar Test Suite validates the ZomLexer.g4 / ZomParser.g4 ANTLR4 grammar against the ZOM language specification. Every fixture cross-references the exact spec section it exercises so that regressions in the parser can be traced back to the normative prose, and gaps in the spec can be detected from missing coverage.

## Purpose

1. **Validate the g4 grammar** -- every parser rule, lexer mode, and semantic predicate has positive, edge, and negative coverage.
2. **Cross-reference the spec** -- every test file opens with a 4-line header naming the spec chapter/section, the grammar rule under test, the expected verdict, and the expected diagnostic (for REJECT cases).
3. **Serve as a regression harness** -- any change to the grammar that breaks an ACCEPT case or silently accepts a REJECT case fails the suite.

## File Format

Every `.zom` fixture uses a mandatory 4-line header convention. The runner script parses these lines verbatim:

```
// §<chapter>[.<section>[.<subsection>]] <human-readable description>
// Covers rule: <ruleName>[ (semanticPredicateName) ]
// Expected: ACCEPT | REJECT
// ExpectedDiagnostic: <diagnostic text, rule name, or "none">
```

Lines after the header are the actual source code fed to the parser. Blank lines and comments inside the body are allowed.

- **ACCEPT** -- parser must exit with no syntax errors and no semantic-predicate failures.
- **REJECT** -- parser must report at least one error matching the `ExpectedDiagnostic` substring.
- Files whose name contains `_pos_` are always ACCEPT; `_neg_` are always REJECT; `_edge_` are usually ACCEPT (boundary-valid constructs) but may be REJECT when named accordingly. The header is authoritative; the filename is a mnemonic aid.

## Directory Inventory

| # | Directory | Purpose | Pos | Edge | Neg | Total |
|---|-----------|---------|-----|------|-----|-------|
| 1 | `02-lexical` | Lexer tokens: literals, keywords, identifiers, escapes, whitespace, comments | 15 | 10 | 15 | 40 |
| 2 | `03-types` | Type syntax: primitives, function types, tuples, unions, references, type aliases | 22 | 15 | 18 | 55 |
| 3 | `04-expressions` | Operators, precedence, calls, casts, lambdas, spawn, control-flow expressions | 35 | 25 | 30 | 90 |
| 4 | `05-statements` | Let, if/else, match, loops, labels, suspend, return, assert | 20 | 12 | 20 | 52 |
| 5 | `06-declarations` | Functions, classes, enums, type aliases, modifiers, declarations at module scope | 12 | 8 | 10 | 30 |
| 6 | `07-patterns` | Match patterns: literal, binding, struct, tuple, or-pattern, wildcard, guard | 12 | 8 | 10 | 30 |
| 7 | `08-adt` | Algebraic data types: structs, enums, variants, constructors, inheritance, fields | 16 | 10 | 14 | 40 |
| 8 | `09-interfaces` | Interface declarations, method signatures, implements clauses, trait bounds | 12 | 8 | 10 | 30 |
| 9 | `11-error` | Error handling: try/catch, throw, raises signatures, async/await, yield | 10 | 6 | 10 | 26 |
| 10 | `12-generics` | Generic parameters, where clauses, generic instantiation, variance placeholders | 10 | 6 | 8 | 24 |
| 11 | `13-modules` | Module files, imports/exports, path resolution, package-level declarations | 18 | 12 | 18 | 48 |
| 12 | `16-attributes` | `@attribute` syntax, built-in attributes, attribute arguments, targets | 8 | 6 | 8 | 22 |
| 13 | `xx-concurrent` | Concurrency primitives: spawn modifiers, priority/blocking/detached, suspend/until | 8 | 6 | 8 | 22 |
|   | **Total** | | **208** | **132** | **169** | **509** |

## Usage

Run the test harness from the `tests/grammar/` directory (or the repo root -- the runner resolves paths relative to its own location).

```bash
# Run every .zom fixture in the suite
bash run_tests.sh

# Run only directories whose name matches a substring
bash run_tests.sh 02-lexical      # just lexical
bash run_tests.sh expressions     # 04-expressions (substring match)
bash run_tests.sh concurrent      # xx-concurrent

# Run every REJECT test anywhere in the tree
# (file path or content contains "neg" or the canonical "_neg_" segment)
bash run_tests.sh negative
```

Exit codes: `0` = all tests passed, `1` = one or more verdicts mismatched, `2` = a fixture was missing its 4-line header.

## Spec Chapter Matrix

| Directory | Spec Chapter(s) and Section(s) |
|-----------|--------------------------------|
| `02-lexical` | §3 Lexical Structure (§3.1 tokens, §3.2 keywords, §3.3 identifiers, §3.4 comments, §3.5 numeric literals, §3.6 string/char literals, §3.7 operators) |
| `03-types` | §4 Types (§4.1 type expressions, §4.2 function types, §4.3 tuple/struct types, §4.4 union types, §4.5 reference/optional types, §4.6 generic types) |
| `04-expressions` | §5 Expressions (§5.1 primary expressions, §5.2 precedence table, §5.3 unary/binary operators, §5.4 calls/indexing, §5.5 casts, §5.6 lambdas, §5.7 spawn expressions) |
| `05-statements` | §6 Statements (§6.1 let, §6.2 assignment, §6.3 if/else, §6.4 switch/match, §6.5 loops, §6.6 labeled statements, §6.7 suspend/until, §6.8 return/assert) |
| `06-declarations` | §7 Declarations (§7.1 modules, §7.2 functions, §7.3 classes, §7.4 enums, §7.5 type aliases, §7.6 modifiers) |
| `07-patterns` | §8 Patterns (§8.1 wildcard, §8.2 binding, §8.3 literal, §8.4 struct/tuple, §8.5 or-patterns, §8.6 pattern guards, §8.7 binding modes) |
| `08-adt` | §9 Algebraic Data Types (§9.1 struct declarations, §9.2 enum declarations, §9.3 variant constructors, §9.4 deinit/init, §9.5 extends / inheritance) |
| `09-interfaces` | §10 Interfaces (§10.1 interface declarations, §10.2 method contracts, §10.3 `implements` clauses, §10.4 default implementations, §10.5 interface inheritance) |
| `11-error` | §11 Error Handling (§11.1 `raises` signature, §11.2 try/catch, §11.3 throw, §11.4 async/await integration, §11.5 yield / generators) |
| `12-generics` | §12 Generics (§12.1 generic parameters, §12.2 where clauses, §12.3 generic instantiation, §12.4 associated types, §12.5 variance placeholders) |
| `13-modules` | §13 Modules and Packages (§13.1 file = module, §13.2 import/export, §13.3 paths, §13.4 package layout, §13.5 visibility) |
| `16-attributes` | §16 Attributes (§16.1 `@` syntax, §16.2 attribute targets, §16.3 built-in attributes, §16.4 user-defined attribute declarations) |
| `xx-concurrent` | §7.3 ConcurrentFeatures + §14 Concurrency (spawn modifiers, priority/blocking/detached forms, `suspend`/`until`, fiber lifecycle) |

## Semantic Predicate Trigger Matrix

Each semantic predicate in the grammar has explicit trigger coverage in the suite. The table below maps predicate name to the filename globs (and directories) that exercise it.

| Predicate | Trigger Test Files (file glob / path) |
|-----------|----------------------------------------|
| `checkModifierList` (duplicate / mutually exclusive modifiers) | `06-declarations/modifier_list_edge_*.zom`, `06-declarations/abstract_static_reject_neg_01.zom`, `06-declarations/static_mutating_reject_neg_01.zom`, `08-adt/struct_full_neg_09.zom`, `09-interfaces/iface_mod_dup_reject_neg_09.zom`, `09-interfaces/iface_mod_abstract_static_neg_07.zom`, `09-interfaces/iface_mod_static_mutating_neg_08.zom`, `04-expressions/modifier_duplicate_neg_01.zom`, `11-error/abstract_static_reject_neg_10.zom` |
| `checkSpawnModifierName` (validates spawn modifier keywords) | `04-expressions/spawn_expr_edge_01.zom`, `xx-concurrent/spawn_priority_nocall_reject_neg_01.zom`, `xx-concurrent/spawn_priority_wrong_arg_neg_02.zom`, `xx-concurrent/spawn_unknown_mod_reject_neg_03.zom` |
| `checkSpawnModifierCall` (spawn modifier requires a call expression, not a statement) | `04-expressions/spawn_expr_edge_01.zom`, `xx-concurrent/spawn_priority_call_pos_04.zom`, `xx-concurrent/spawn_priority_nocall_reject_neg_01.zom`, `xx-concurrent/spawn_multi_mods_edge_01.zom` |
| `checkLabelNoAttrAfterLabel` (attributes on labeled statements are illegal) | `05-statements/label_attr_reject_neg_01.zom` |
| `checkSuspendUntil` (`suspend` must be followed by `until` + expr, or a standalone expression) | `05-statements/suspend_forms_pos_01.zom`, `05-statements/suspend_forms_pos_02.zom`, `05-statements/suspend_not_until_reject_neg_01.zom`, `xx-concurrent/suspend_until_pos_07.zom`, `xx-concurrent/suspend_until_complex_edge_03.zom`, `xx-concurrent/suspend_foo_reject_neg_04.zom`, `xx-concurrent/suspend_ill_formed_neg_05.zom`, `xx-concurrent/suspend_plain_pos_06.zom`, `xx-concurrent/multi_suspend_edge_02.zom` |
| `checkImplementsKeyword` (enforces `implements` token presence in class declaration and rejects `impl` reserved syntax) | `08-adt/class_inheritance_pos_02.zom`, `08-adt/multi_implements_pos_13.zom`, `08-adt/extends_plus_implements_pos_14.zom`, `06-declarations/class_decl_edge_01.zom`, `09-interfaces/class_implements_pos_08.zom`, `09-interfaces/class_implements_edge_07.zom`, `09-interfaces/complex_impl_pos_09.zom`, `09-interfaces/implements_comma_reject_neg_06.zom`, `09-interfaces/iface_impl_keyword_neg_10.zom` |
| `checkBoolLiteral` (true/false literals inside pattern context) | `07-patterns/literal_bool_predicate_trigger_edge_01.zom` |
| `checkBindPat` (binding-pattern disambiguation; `_` by itself is wildcard, not a bind) | `07-patterns/underscore_bind_reject_neg_01.zom` |
| `reserved` (rejects reserved keywords used as identifiers, or reserved syntax like `impl X for Y`) | `02-lexical/kw_vs_ident_neg_01.zom`, `02-lexical/reserved_kw_reject_neg_*.zom`, `04-expressions/reserved_*_neg_01.zom` (`var`, `namespace`, `yield`, `delete`, `await`, `instanceof`, `async`, `throw`), `09-interfaces/iface_impl_keyword_neg_10.zom`, `11-error/try_catch_reject_neg_03.zom`, `11-error/throw_reject_neg_04.zom`, `11-error/async_await_reject_neg_06.zom`, `11-error/var_reject_neg_08.zom`, `11-error/yield_reject_neg_09.zom` |
| `char literal single-scalar` (char literal holds exactly one Unicode scalar value) | `02-lexical/char_single_scalar_pos_01.zom`, `02-lexical/char_single_scalar_pos_02.zom`, `02-lexical/char_single_scalar_edge_01.zom`, `02-lexical/char_single_scalar_neg_01.zom`, `04-expressions/char_literal_neg_01.zom` |
| `decimal leading-sep` (DECIMAL_LITERAL reject `_` as leading separator) | `02-lexical/decimal_leading_sep_reject_neg_01.zom`, `02-lexical/decimal_forms_pos_03.zom`, `02-lexical/radix_literals_pos_01.zom`, `04-expressions/decimal_leading_sep_neg_01.zom` |
| `unicode escape range` (validates `\u{...}` escapes fit Unicode scalar range and use hex digits) | `02-lexical/string_escapes_pos_01.zom`, `02-lexical/string_escapes_neg_01.zom`, `02-lexical/string_escapes_neg_02.zom`, `02-lexical/ident_unicode_escapes_pos_01.zom`, `02-lexical/ident_zwnj_zwj_edge_01.zom` |

## Known Limitations

These parser edge cases are **not** enforced by the grammar today and are captured as skipped / TODO fixtures so they are not silently regressed. Any future grammar update fixing them must also flip the corresponding fixture's verdict.

1. **`>>` as two closing angle brackets.** The lexer currently tokenizes `>>` as a single `RSHIFT` token. Generic instantiations like `Map<String, List<Int>>` require the parser to split this into two `>` on demand (C++-style angle-bracket splitting). Fixtures under `12-generics/` with `double_close_bracket_*` document this; their verdict is overridden to ACCEPT even though the grammar accepts them only because of the single-token `>>` rule.
2. **Left-arrow `<---` token disambiguation.** In certain expression contexts, `<---` can be lexed as either `LT MINUS MINUS MINUS` or a single token. The grammar prefers the multi-token split; no semantic predicate enforces the boundary.
3. **Unicode bidirectional override characters in string literals.** Characters U+202A-U+202E inside string literals are accepted without warning; a lint-stage check is planned instead.
4. **`spawn` without modifier call inside block bodies.** `spawn` followed by a brace block (`spawn { ... }`) is currently accepted as a spawn expression with no modifiers; the spec requires at least one modifier when using the block form. Enforced by a semantic-predicate TODO in `04-expressions/spawn_expr_edge_01.zom`.
5. **Leading zero in decimal literals (octal placeholder).** `0123` is currently lexed as a DECIMAL_LITERAL with value 123; some languages treat this as an octal syntax error. The spec explicitly defers this to a lint, so the grammar accepts it. Fixtures in `02-lexical/decimal_forms_pos_03.zom` assert acceptance.
6. **`match` arm braces in single-expression position.** `match x { true => { 1 } }` -- the braces inside a match arm body are parsed as a block rather than a struct literal. A semantic predicate should disambiguate based on expected type; this is deferred to type-checking.
7. **Raw string literal (r"...") unicode escape processing.** Raw strings currently still process a subset of escapes. Documented in `02-lexical/string_escapes_neg_01.zom` via the override list in the runner.

## Total

Recursive count of `.zom` fixtures in `tests/grammar/`: **509**
