---
paths:
  - "products/zomlang/compiler/**"
---

# Compiler Pipeline Rules

> Applies to `lexer/`, `parser/`, `ast/`, `binder/`, `checker/`, `symbol/`,
> `diagnostic/`, `driver/`.  See `.agents/skills/spec-alignment/SKILL.md` for
> the five-way consistency check between spec and implementation.

---

## Pipeline Contract

```
 lexer  ──► parser  ──► binder  ──► checker  ──► … downstream
  ▲          ▲           ▲           ▲
  │          │           │           │
 ZomLexer.g4 grammar-  scope docs  type-system
 chapter 02 reference   chapters    chapters
 (spec)    (spec)      (spec)      (spec)
```

**Each phase's output is the single source of truth for the next phase.**
Phase N must not reach back into phase N-1's state after transition. Cross-phase
coordination happens via well-defined types.

---

## Lexer (`lexer/`)

### Rules

1. **Canonical lexer spec lives in `docs/spec/ZomLexer.g4`** — the C++ lexer in
   `lexer.cc` is a hand-rolled implementation of *that* grammar, not the other
   way around. Any token added to `lexer.cc` must first be added to `ZomLexer.g4`
   and the lexical-structure chapter.
2. Multi-character operators are dispatched from the first character's branch.
   Symmetric rule: for each two-character operator, the handler lives inside the
   `case <first-char>` branch and consumes **exactly** the right number of bytes.
   - Example: `!!` lives in `case '!'` and advances 2;
     `?!` must live in `case '?'` and advance 2 (currently missing as of 2026-06-24
     per audit finding ERR-001).
3. All keywords live in `ast/kinds.h` between `FirstKeyword` and `LastKeyword`.
   Never hard-code string comparisons in `lexer.cc` — use the centralized lookup.
4. Lexer never produces a partial token or falls back to `Identifier` for a
   malformed but recognizable operator. Emit a `ZOMxxxx` diagnostic instead.

### Audit Checklist (Spec ↔ Lexer)

- [ ] Every `ERROR_*` / `KEYWORD_*` / `OP_*` in `ZomLexer.g4` has a matching
      case in `lexer.cc` and a matching entry in `ast/kinds.h` and `token.cc`.
- [ ] Every reserved keyword listed in `02-lexical-structure.md` § Keywords is
      either in the grammar switch or explicitly marked "reserved for v2" there.
      A keyword with no grammar rule and no v2 marker must be **deleted** from
      the reservation list per Rule #4 of design principles.

---

## Parser (`parser/`)

### Rules

1. **Canonical grammar lives in `docs/spec/chapters/17-grammar-reference.md`**.
   The parser in `parser.cc` is a hand-written recursive descent implementation
   of *that* EBNF. Parser changes require spec changes in the same commit.
2. Operator precedence table lives in `docs/spec/chapters/04-expressions.md`.
   The `getBinaryOperatorPrecedence()` switch in `parser.cc` must match it row by
   row. Any discrepancy is a P0 bug.
3. Postfix suffixes (EBNF `PostfixSuffix ::= '?!' | '!!' | '++' | '--'`) are
   handled in `parseUpdateExpression`'s postfix loop. Every new postfix operator
   is added there, not sprinkled into outer levels.
4. The `isModifier()` switch + `Modifier ::=` EBNF production +
   `02-lexical-structure.md § Modifier Keywords` table are a **three-way set**.
   If they differ, fix whichever is wrong in the same commit.
5. Parser errors emit a `ZOMxxxx` diagnostic code, never a generic string.
   Diagnostic codes live in a centralized list; never invent ad-hoc string
   messages.

### Audit Checklist (Spec ↔ Parser)

- [ ] Every `Xxx ::=` EBNF production in chapter 17 has a matching
      `parseXxx(...)` entry point in the parser whose structure matches it.
- [ ] The precedence array in `parser.cc` and the precedence table in
      chapter 04 produce identical orderings for every operator.
- [ ] Postfix operators defined in `PostfixSuffix` are all consumed in the
      `parseUpdateExpression` loop. No postfix operator is handled at a
      different (higher/lower) precedence level.
- [ ] Every `XFAIL` test under `tests/conformance/expectations/ast/**` has a ticket or tracking
      note. An `XFAIL` test whose root cause has been fixed must be upgraded
      (expectation corrected, `XFAIL` marker removed) immediately.

---

## AST (`ast/`)

### Rules

1. All syntax kinds live in `kinds.h` between `FirstToken` / `LastToken` and
   `FirstNode` / `LastNode` contiguous ranges. New kinds are inserted at the
   range boundary to keep the enum dense.
2. `ast-nodes.def` / `ast-builders.h` / visitor interfaces are generated or
   maintained 1:1 with `kinds.h`. Adding a kind to `kinds.h` requires updating
   every table that maps `SyntaxKind` to a handler.
3. AST nodes that have no parser entry point (dead nodes reserved for "future
   codegen") — either wire the parser path to construct them in this release,
   or **delete the node** per design principle #4. Dead AST kinds mislead
   auditors and spec writers.
4. AST dump / pretty-print utilities must round-trip cleanly for every construct.
   If a lit test FileCheck uses `CHECK-DAG` only because the order is
   non-deterministic, fix the order before merging.

---

## Binder (`binder/`) + Symbol (`symbol/`)

### Rules

1. **Scope tree is a pure tree, never a DAG with cross edges.** No direct access
   to another `SymbolTable`'s scope. Cross-module identity flows through a
   `CompilerSession` / whole-program registry **only**.
2. `SymbolFlags` defined in `symbol-flags.h` must actually be written by someone.
   Any flag whose setter (`addFlag`, `setFlag`) is never invoked across the
   compiler → **delete the flag** per Rule #4.
3. Name lookup order is documented in the modules chapter. Any deviation between
   the documented order and the implementation is a P0 bug.
4. Export / `pub(...)` visibility flags must match the spec chapters exactly.
   Today (2026-06-24): the `Export` flag is defined but never set (audit finding
   MOD-007) — this must be fixed before the module system ships.

---

## Type Checker (`checker/`)

> **CURRENTLY EMPTY / STUB as of 2026-06-24.** When implementing, read this list
> first and build the architecture around it, because every wrong choice here
> causes years of cascading breakage (see Rust/Swift/Kotlin/Go type-system retrofits).

### Non-Negotiable Architectural Constraints

1. **`CompilerSession`-scoped, never global.** No `static TypeStore` anywhere.
2. **Traits / interfaces are a core type-system primitive on day one.** Do not
   retrofit them after struct-only typing. `Send` / `Sync` / `Sendable` /
   `Drop` / `Clone` / `Eq` / `Hash` are the bedrock of memory safety, concurrency
   safety, error handling, and the standard library — they cannot be late additions.
3. **Raises clauses flow through checker, not parser.** The parser accepts the
   annotation; the checker validates the actual error union, widening, and
   compatibility with `?` / `?!` propagation.
4. **Generics are a core feature, not a post-1.0 nice-to-have.** Without generics
   there is no `Result<T, E>`, no `Vector<T>`, no `Future<T>` — the language is
   dead on arrival. Implement parametric polymorphism before specializing on it.
5. **Type errors use diagnostic codes.** A single "type mismatch" message is not
   enough. Each mismatch kind gets its own ZOM code + an auto-suggestion when
   feasible.

---

## Driver (`driver/`)

### Rules

1. Phase order in the driver matches the pipeline contract exactly. No back-edges
   (`parse` → `bind` → `parse again`), no skipping phases based on heuristics
   like "the source file has no generic so we can skip checker".
2. `CompilerSession` is the single owner of `SourceManager`, `DiagnosticEngine`,
   the cross-module symbol registry, and the list of `TranslationUnit` handles.
   No phase constructs these on its own.
3. Every phase is independently testable with a ztest:
   `ParserTest`, `BinderTest`, `TypeCheckerTest` all run against a minimal
   `CompilerSession` fixture without invoking the driver binary.
4. The driver must report how many `ZOMxxxx` errors were emitted for each
   category (lex / parse / bind / type) and exit with a distinct non-zero code
   per phase failure, so that lit / ztest can distinguish "parse error" from
   "semantic error" without grepping human-readable output.
