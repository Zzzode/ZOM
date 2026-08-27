# ZOM Grammar Test Suite

## Overview

The ZOM Grammar Test Suite validates the ZomLexer.g4 / ZomParser.g4 ANTLR4 grammar against the ZOM language specification. Every fixture cross-references the exact spec section it exercises so that regressions in the parser can be traced back to the normative prose, and gaps in the spec can be detected from missing coverage.

## Purpose

1. **Validate the g4 grammar** -- executable ACCEPT and REJECT expectations cover language forms, lexer modes, parser rules, and semantic predicates.
2. **Cross-reference the spec** -- every grammar expectation records the spec chapter/section, the grammar rule under test, and the expected verdict.
3. **Serve as a regression harness** -- any change to the grammar that breaks an ACCEPT case or silently accepts a REJECT case fails the suite.

## File Format

Source fixtures are pure `.zom` files under `conformance/corpus/<chapter>/`.
Grammar expectations live under `conformance/expectations/grammar/<chapter>/`
with the same relative path and a `.yml` suffix:

```yaml
section: "Section and short description"
covers_rule: "ruleName"
expected: "ACCEPT"
notes: []
```

- **ACCEPT** -- parser must exit with no syntax errors and no semantic-predicate failures.
- **REJECT** -- parser must report at least one error.
- Files whose name contains `_pos_` are always ACCEPT; `_neg_` are always REJECT; `_edge_` are usually ACCEPT (boundary-valid constructs) but may be REJECT when named accordingly. The expectation file is authoritative; the filename is a mnemonic aid.

The grammar runner is an ANTLR grammar oracle, not the compiler diagnostic
contract. Do not assert public `ZOMxxxx` diagnostics in grammar expectations;
those belong in compiler AST/FileCheck expectations under
`conformance/expectations/ast/`.

## Directory Inventory

| # | Directory | Purpose | Total |
|---|-----------|---------|------:|
| 1 | `02-lexical` | Lexer tokens, literals, identifiers, escapes, whitespace, and comments | 48 |
| 2 | `03-types` | Type syntax, projections, dynamic types, references, and aliases | 114 |
| 3 | `04-expressions` | Operators, calls, casts, literals, lambdas, and spawn expressions | 184 |
| 4 | `05-statements` | Bindings, control flow, borrow syntax, and returns | 116 |
| 5 | `06-declarations` | Functions, named types, aliases, modifiers, and declarations | 43 |
| 6 | `07-patterns` | Binding, literal, structural, alternative, and guarded patterns | 31 |
| 7 | `08-adt` | Structs, classes, enums, variants, constructors, and fields | 46 |
| 8 | `09-interfaces` | Interfaces, impls, method signatures, and trait bounds | 47 |
| 9 | `11-error` | Raises clauses and reserved error-handling syntax | 36 |
| 10 | `12-generics` | Generic parameters, constraints, and instantiation | 36 |
| 11 | `13-modules` | Module declarations, imports, exports, and paths | 54 |
| 12 | `15-concurrency` | Spawn modifiers and suspend forms | 22 |
| 13 | `16-attributes` | Outer attributes, inputs, paths, attachment targets, and unavailable paths | 34 |
| 14 | `20-ffi` | Extern declarations, ABI strings, and unsafe blocks | 16 |
|   | **Total** | | **827** |

## Usage

Run the test harness from this directory or from the repository root. The
runner resolves the corpus and expectations from the repository root.

Prerequisites:

- Bash 4 or newer.
- A working JRE and JDK (`java` and `javac`).
- ANTLR 4 complete jar. The runner first uses `ANTLRJAR` when set, then probes
  common Homebrew install paths and `$HOME/.cache/antlr`.

```bash
# Run every grammar expectation in the suite
bash tests/conformance/runners/grammar/run_tests.sh

# Run only directories whose name matches a substring
bash tests/conformance/runners/grammar/run_tests.sh 02-lexical
bash tests/conformance/runners/grammar/run_tests.sh expressions
bash tests/conformance/runners/grammar/run_tests.sh concurrent

# Run every REJECT test anywhere in the tree
# (file path or content contains "neg" or the canonical "_neg_" segment)
bash tests/conformance/runners/grammar/run_tests.sh negative
```

CTest registration:

```bash
ctest --preset default -L conformance-grammar --output-on-failure
cmake --build --preset sanitizer --target check-conformance-grammar
```

Generated ANTLR output is written outside the source corpus. CTest sets
`ZOM_CONFORMANCE_BUILD_DIR` to the build tree. Direct shell runs default to
`build/conformance-grammar`.

Exit codes: `0` = all tests passed, `1` = one or more verdicts mismatched, `2` = a fixture is missing its expectation metadata.

The runner reports how many ANTLR parser-rule names appear in `covers_rule`
metadata. This is an inventory aid, not the normative compiler-parser coverage
gate. Run `python3 scripts/check-parser-coverage.py` for the authoritative
specification-to-compiler coverage check.

## Spec Chapter Matrix

| Directory | Spec Chapter(s) and Section(s) |
|-----------|--------------------------------|
| `02-lexical` | §3 Lexical Structure (§3.1 tokens, §3.2 keywords, §3.3 identifiers, §3.4 comments, §3.5 numeric literals, §3.6 string/char literals, §3.7 operators) |
| `03-types` | §4 Types (§4.1 type expressions, §4.2 function types, §4.3 tuple/struct types, §4.4 union types, §4.5 reference/optional types, §4.6 generic types) |
| `04-expressions` | §5 Expressions (§5.1 primary expressions, §5.2 precedence table, §5.3 unary/binary operators, §5.4 calls/indexing, §5.5 casts, §5.6 lambdas, §5.7 spawn expressions) |
| `05-statements` | §6 Statements (§6.1 let, §6.2 assignment, §6.3 if/else, §6.4 switch/match, §6.5 loops, §6.6 labeled statements, §6.7 suspend/until, §6.8 return/assert) |
| `06-declarations` | §7 Declarations (§7.1 modules, §7.2 functions, §7.3 classes, §7.4 enums, §7.5 type aliases, §7.6 modifiers) |
| `07-patterns` | §8 Patterns (§8.1 wildcard, §8.2 binding, §8.3 literal, §8.4 struct/tuple, §8.5 or-patterns, §8.6 pattern guards, §8.7 binding modes) |
| `08-adt` | §9 Algebraic Data Types (§9.1 struct declarations, §9.2 enum declarations, §9.3 variant constructors, §9.4 deinit/init, §9.5 class inheritance) |
| `09-interfaces` | §10 Interfaces (§10.1 interface declarations, §10.2 method contracts, §10.3 `implements` clauses, §10.4 default implementations, §10.5 interface inheritance) |
| `11-error` | §11 Error Handling (§11.1 `raises` signature, §11.2 try/catch, §11.3 throw, §11.4 async/await integration, §11.5 yield / generators) |
| `12-generics` | §12 Generics (§12.1 generic parameters, §12.2 where clauses, §12.3 generic instantiation, §12.4 associated types, §12.5 variance placeholders) |
| `13-modules` | §13 Modules and Packages (§13.1 file = module, §13.2 import/export, §13.3 paths, §13.4 package layout, §13.5 visibility) |
| `16-attributes` | §16 Attributes (outer syntax, paths, inputs, attachment targets, and marker uses) |
| `15-concurrency` | §15 Concurrency (spawn modifiers, §15.3 priority/blocking/detached forms, §15.4 `suspend`/`until`, fiber lifecycle) |

## Semantic Predicate Matrix

The ZOM grammar relies on a carefully chosen mix of semantic predicates and
parser actions. Not every form is safe to combine with `throw
ParseCancellationException`; the ALL(*) simulator can silently poison a
DFA state and turn a valid source into a spurious `NoViableAltException`.
The table below summarises the canonical forms used across `ZomParser.g4`
and is the authoritative reference for writing new predicates.

See also: `AGENTS.md § ANTLR 4 .g4 Authoring Rules (ZOM-G4-PATTERN-001 ~ 003)`.

| Kind | Trigger syntax | Simulator visible? | Can throw PCE? | Typical use |
|---|---|---|---|---|
| **Tail Parser Action** (`ZOM-G4-PATTERN-001`) | `{code}` placed **after the last terminal** of an alt | **NO** (benign epsilon edge) | YES, rc=2 with exact position | **All REJECT-level diagnostics (ZOMxxxx) that need rc=2 abort-on-error semantics.** This is the recommended form for new REJECT diagnostics. |
| Gated Predicate | `{p}?` as alt **prefix** | YES | NO, swallowed as `predicate=false` | Disambiguation between structurally overlapping alternatives. Never throw from inside the body. |
| Free Predicate | `{p}?` placed in the **middle** of an alt | YES | NO, swallowed as `predicate=false` | Structural validation with soft failure. The grammar falls back to other alternatives or reports a generic syntax error. |
| Parser Action (mid-alt) | `{code}` placed between terminals | YES | NO, poisons the DFA alt | Prohibited by ZOM-G4-PATTERN-001. |

## Semantic Predicate Trigger Matrix

Each semantic predicate in the grammar has explicit trigger coverage in the suite. The table below maps predicate name to the filename globs (and directories) that exercise it.

| Predicate | Trigger Test Files (file glob / path) |
|-----------|----------------------------------------|
| `checkModifierList` (duplicate / mutually exclusive modifiers) | `06-declarations/modifier_list_edge_*.zom`, `06-declarations/abstract_static_reject_neg_01.zom`, `06-declarations/static_mutating_reject_neg_01.zom`, `08-adt/struct_full_neg_09.zom`, `09-interfaces/iface_mod_dup_reject_neg_09.zom`, `09-interfaces/iface_mod_abstract_static_neg_07.zom`, `09-interfaces/iface_mod_static_mutating_neg_08.zom`, `04-expressions/modifier_duplicate_neg_01.zom`, `11-error/abstract_static_reject_neg_10.zom` |
| `checkSpawnModifierName` (validates spawn modifier keywords) | `04-expressions/spawn_expr_edge_01.zom`, `15-concurrency/spawn_priority_nocall_reject_neg_01.zom`, `15-concurrency/spawn_priority_wrong_arg_neg_02.zom`, `15-concurrency/spawn_unknown_mod_reject_neg_03.zom` |
| `checkSpawnModifierCall` (spawn modifier requires a call expression, not a statement) | `04-expressions/spawn_expr_edge_01.zom`, `15-concurrency/spawn_priority_call_pos_04.zom`, `15-concurrency/spawn_priority_nocall_reject_neg_01.zom`, `15-concurrency/spawn_multi_mods_edge_01.zom` |
| `checkSuspendUntil` (`suspend` must be followed by `until` + expr, or a standalone expression) | `05-statements/suspend_forms_pos_01.zom`, `05-statements/suspend_forms_pos_02.zom`, `05-statements/suspend_not_until_reject_neg_01.zom`, `15-concurrency/suspend_until_pos_07.zom`, `15-concurrency/suspend_until_complex_edge_03.zom`, `15-concurrency/suspend_foo_reject_neg_04.zom`, `15-concurrency/suspend_ill_formed_neg_05.zom`, `15-concurrency/suspend_plain_pos_06.zom`, `15-concurrency/multi_suspend_edge_02.zom` |
| `checkBoolLiteral` (true/false literals inside pattern context) | `07-patterns/literal_bool_predicate_trigger_edge_01.zom` |
| `checkBindPat` (binding-pattern disambiguation; `_` by itself is wildcard, not a bind) | `07-patterns/underscore_bind_reject_neg_01.zom` |
| `reserved` (rejects reserved keywords used as identifiers and reserved feature syntax) | `02-lexical/kw_vs_ident_neg_01.zom`, `02-lexical/reserved_kw_reject_neg_*.zom`, `04-expressions/reserved_*_neg_01.zom` (`var`, `namespace`, `yield`, `delete`, `await`, `instanceof`, `async`, `throw`), `09-interfaces/iface_impl_keyword_neg_10.zom`, `11-error/try_catch_reject_neg_03.zom`, `11-error/throw_reject_neg_04.zom`, `11-error/async_await_reject_neg_06.zom`, `11-error/var_reject_neg_08.zom`, `11-error/yield_reject_neg_09.zom` |
| `char literal single-scalar` (char literal holds exactly one Unicode scalar value) | `02-lexical/char_single_scalar_pos_01.zom`, `02-lexical/char_single_scalar_pos_02.zom`, `02-lexical/char_single_scalar_edge_01.zom`, `02-lexical/char_single_scalar_neg_01.zom`, `04-expressions/char_literal_neg_01.zom` |
| `decimal leading-sep` (DECIMAL_LITERAL reject `_` as leading separator) | `02-lexical/decimal_leading_sep_reject_neg_01.zom`, `02-lexical/decimal_forms_pos_03.zom`, `02-lexical/radix_literals_pos_01.zom`, `04-expressions/decimal_leading_sep_neg_01.zom` |
| `unicode escape range` (validates `\u{...}` escapes fit Unicode scalar range and use hex digits) | `02-lexical/string_escapes_pos_01.zom`, `02-lexical/string_escapes_neg_01.zom`, `02-lexical/string_escapes_neg_02.zom`, `02-lexical/ident_unicode_escapes_pos_01.zom`, `02-lexical/ident_zwnj_zwj_edge_01.zom` |
| `rejectUnavailableConditionalAttribute` (rejects the exact unavailable `zom::cfg` path at a tail action) | `16-attributes/attr_zom_cfg_reject_neg_20.zom` |

## Total

Recursive count of grammar expectation files: **827**
