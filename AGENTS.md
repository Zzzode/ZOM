# AGENTS.md

Project-wide guidance for AI coding agents working on the ZOM language repository.

> **Canonical design principles live in `.agents/rules/design-principles.md`**.
> Everything below is either a reference or points into `.agents/` for detail.

---

## Attention (Read This First)

1. **No forward compatibility** — no deprecated markers, no dual code paths, no
   `#ifdef ZOM_V1_COMPAT`, no shims. Delete the old API and fix every caller.
2. **Radical refactoring over incremental patching** — one clean rewrite beats
   five compatibility layers.
3. **Best practices first** — study Rust/Zig/Swift 6/Go/C++23 before inventing
   anything. Originality is not a virtue here.
4. **Remove useless things immediately** — unused flag? Reserved keyword with no
   grammar? Spec chapter describing something the parser rejects? Placeholder AST
   node with zero callers? **Delete it.** Revert the deletion later if we need it.
5. **English throughout all artifacts** — code, comments, identifiers, spec
   chapters, diagnostic messages, audit reports, design documents, commit
   messages.
   - **No Chinese anywhere in the repository.** If you encounter Chinese in a
     file, rewrite it to English in the same change — do **not** leave a Chinese
     section as-is and append an English translation. Delete the Chinese.
   - This single paragraph is the **only** tolerated Chinese text in the entire
     repo, and it is tolerated exclusively because it *states* the rule.
   - You MUST produce all code, docs, commit messages in English. Translate your
     reasoning to Chinese only when speaking to the user in the chat reply.
6. **NO "legacy / deprecated / discarded / Rust-style / bad-old-way" comparative
   prose in design docs.** Do NOT keep tables that contrast "what we used to
   write in Rust" with "what ZOM writes now", do NOT leave headings or bullets
   that say "(淘汰/废弃/残留/反面例子/已淘汰/deprecated form)", do NOT preserve
   counter-examples "for reference".
   - If the spec says X is the syntax, write X. Delete every trace of the old
     syntax. A reader should not be able to infer *any* prior design existed
     from reading the current document.
   - Migration / changelog / rationale content, if genuinely useful, lives
     **only** in a per-release CHANGELOG.md outside the normative spec.
     Migration notes inside a spec chapter or canonical design doc are banned.
7. **Commit messages are ASCII-English only.** No CJK characters, no
   non-ASCII punctuation in the subject or body. `git log --oneline` must
   be cleanly grepable with an ASCII regex. See also `§ Git Commits`
   below and `.agents/rules/design-principles.md` § Radical Refactoring.
8. If a tool call fails with a syntax error, read the tool definition and retry
   with the correct shape. Do not silently skip the step.

Additional path-scoped rules live in `.agents/rules/*.md`. Skills loadable via
`/skill` live in `.agents/skills/*/SKILL.md`. Subagent gate specs live in
`.agents/subagents/` (manifest at `.agents/subagents/manifest.yaml`).

---

## Project Overview

ZOM is a modern systems programming language.

```
ZOM/
├── libraries/zc/               # Core library (Own<T>, Vector<T>, String,
│                               #   Maybe<T>, OneOf<T…>, exceptions, macros)
├── products/zomlang/
│   ├── compiler/               # Frontend pipeline
│   │   ├── lexer/              #   Tokenizer
│   │   ├── parser/             #   Recursive descent parser
│   │   ├── ast/                #   AST kinds, nodes, builders, defs
│   │   ├── binder/             #   Scope / name binding / symbol resolution
│   │   ├── checker/            #   Type checker + semantic analysis
│   │   ├── symbol/             #   Symbol tables, flags, scopes
│   │   ├── diagnostic/         #   Diagnostic messages, codes (ZOMxxxx)
│   │   └── driver/             #   Build driver, CompilerSession, CLI
│   ├── runtime/                # Language runtime, allocation, concurrency primitives
│   └── tests/                  # ztest unit tests + LLVM lit AST tests
├── docs/
│   ├── spec/chapters/          # Spec: lexical / expressions / types /
│   │                           #   declarations / modules / errors / concurrency …
│   ├── spec/ZomLexer.g4        # Canonical lexer definition
│   └── reports/                # Multi-expert audit reports (234 findings)
├── examples/                   # Small ZOM source samples
└── scripts/                    # check-format.py, regen-lit.py, etc.
```

### Technology Stack

| Layer | Choice |
|---|---|
| Language | C++20 (constexpr, concepts, Pimpl everywhere) |
| Build | CMake + `CMakePresets.json` — **always use presets, never bare `cmake`** |
| Core library | `zc` — no `std::` unless `zc` has no substitute |
| Unit tests | `ztest` (harness in-tree) — run with `ctest -R unittest` |
| AST tests | LLVM `lit` + FileCheck — run with `ctest -R lit` |
| Sanitizers | `sanitizer` preset — enabled by default for all development builds |
| Test presets | `default` preset = lit + unittest combined |

### Common Commands

#### Configure + Build

```bash
# Default dev build with sanitizers (ALWAYS use this before submitting changes)
cmake --preset sanitizer
cmake --build --preset sanitizer

# If you need debug without sanitizers (rare)
cmake --preset debug
cmake --build --preset debug
```

#### Run Tests

```bash
# All tests (lit + unit)
ctest --preset default

# Only lit tests (AST / parser / binder / semantic)
ctest --preset default -R lit

# Only unit tests (ztest)
ctest --preset default -R unittest

# Verbose failing output
ctest --preset default --output-on-failure

# Regenerate FileCheck expectations for a lit test (after parser/spec change)
python3 products/zomlang/tests/tools/regen-lit.py products/zomlang/tests/language/path/to/test.zom
```

#### Quality Gates

```bash
# C++ code formatting check (mandatory)
python3 scripts/check-format.py

# Code coverage (requires ZOM_ENABLE_COVERAGE=ON configure)
make coverage

# Debug the compiler executable with LLDB (macOS)
lldb ./products/zomlang/compiler/zomlangc -- path/to/source.zom
```

#### Preset Matrix

| Preset | Purpose | Sanitizers | Optimizations |
|---|---|---|---|
| `sanitizer` | **Default development** | ASan + UBSan + LeakSan enabled | -O0 / -O1 |
| `debug` | Symbol-heavy stepping | Off | -O0 |
| `release` | Benchmark / ship | Off | -O2 / LTO |

---

## Architecture: Frontend Pipeline

```
Source (.zom)
  │
  ▼
Lexer         ───► Token stream    (lexer/*.cc, docs/spec/ZomLexer.g4)
  │
  ▼
Parser        ───► AST tree        (parser/*.cc, ast/nodes.cc)
  │                              kinds in ast/kinds.h
  ▼
Binder        ───► Scopes + symbols resolved  (binder/*.cc)
  │                              symbol tables in symbol/*
  ▼
Type Checker  ───► Type + semantic analysis (checker/*.cc — CURRENTLY EMPTY)
  │
  ▼
Diagnostics   ───► ZOMxxxx codes, pretty-printing  (diagnostic/*.cc)
  │
  ▼
… IR / Codegen (future)
```

**CRITICAL KNOWN GAPS (as of 2026-06-24)** that are tracked by audit findings
and must be handled with principle #4 (delete or implement, no drift):

1. **TypeChecker is a stub.** `checker/checker.h` is entirely commented out;
   `checker/checker.cc` is an empty namespace. No type / trait / raises /
   concurrency-safety checking exists yet.
2. **Driver has no `checkSources()` phase.** The driver only wires parse + bind.
3. **ErrorPropagate `?!` lexer branch is missing.** lexer `case '?'` handles
   `?.`, `??`, `??=` but not `charAt(1)=='!'` — token defined in `ast/kinds.h`
   and `ZomLexer.g4:189` but never emitted.
4. **Parser does not consume ErrorPropagate/ErrorUnwrap as postfix.**
   `parseUpdateExpression` only handles `++/--`. Grammar reference:308 lists
   all four as `PostfixSuffix`.
5. **No cross-module `CompilerSession`.** Each `SymbolTable` owns an isolated
   `ScopeManager`; package/import/export semantics are single-file only.
6. **Concurrency chapter is 11 lines.** `docs/spec/chapters/15-concurrency.md`
   explicitly states no grammar is defined. Reserved keywords `async/await`
   exist in the lexical table only.
7. **`Export` symbol flag never written.** Defined in `symbol-flags.h:148`;
   grep `addFlag.*Export` across compiler = zero hits.

---

## Available Skills

Invoke with `/skill <name>`.

| Name | File | Purpose |
|---|---|---|
| `build-ci` | `.agents/skills/build-ci/SKILL.md` | Configure, build, test, coverage, format, sanitizer triage |
| `zc-library` | `.agents/skills/zc-library/SKILL.md` | Own / Vector / String / Maybe / OneOf — type choices and gotchas |
| `lit-testing` | `.agents/skills/lit-testing/SKILL.md` | LLVM lit FileCheck tests, `regen-lit.py`, RUN/XFAIL patterns |
| `ultracode-audit` | `.agents/skills/ultracode-audit/SKILL.md` | Multi-expert adversarial audit (scout → 6-dim audit → confirm/refute → report) |
| `spec-alignment` | `.agents/skills/spec-alignment/SKILL.md` | `spec ↔ lexer ↔ parser ↔ AST ↔ binder` 5-way consistency check |

---

## Available Subagents

Codex/Claude-code custom agent entrypoints (if configured) are routed by
`task-router-agent.md`. The minimum safe gate set for a change is selected from
the trigger matrix in `.agents/subagents/README.md`.

| ID | Owns | Triggered when |
|---|---|---|
| `task-router-agent` | Gate selection + escalation | Default entry for all non-trivial changes |
| `lexer-parser-agent` | Tokenization, grammar, AST, operator precedence | lexer/*.cc, parser/*.cc, ast/kinds.h, spec grammar |
| `binder-checker-agent` | Scopes, symbols, traits, generics, type rules | binder/**, checker/**, symbol/**, traits, ADT |
| `module-system-agent` | Import/export, packages, visibility, dependency topology | modules, `docs/spec/chapters/13-*`, symbol export flags |
| `error-system-agent` | Result/Option, ?! / !! / ?: , raises clauses, panic boundaries | Diagnostic codes, error chapters, error operators in parser/lexer |
| `concurrency-agent` | async/await, Future, nursery, cancel, Sendable, memory model, primitives | runtime concurrency, spec 15-concurrency, channel/mutex, `Send/Sync`/`Sendable` |
| `spec-audit-agent` | Spec ↔ implementation 1:1 alignment | Any change to docs/spec/** or any compiler frontend file |
| `runtime-memory-agent` | Ownership, zc types, RAII, memory model, unsafe boundaries | libraries/zc/**, runtime/**, FFI |
| `verification-agent` | Build + sanitizer + tests + format, evidence-gating | Runs last; required for all merge-ready changes |

---

## Library Preferences

- `zc::Own<T>` over raw pointers — **no raw pointers anywhere except FFI**.
- `zc::Vector<T>` stores `T` directly, **never `zc::Own<T>`** as element.
- `zc::String` is move-only; **only `zc::mv` moves strings**; copying is a compile error.
- `zc::Maybe<T>` + `ZC_IF_SOME` macro replaces null pointer + `orDefault()` pitfalls.
- `std::` is banned by default. Use only if `zc` genuinely lacks the construct.
- All classes use Pimpl: `struct Impl; zc::Own<Impl> impl;` in header, implementation in `.cc`.

## Git Commits

- Keep commit scope honest: one logical change per commit.
- Commit titles MUST follow Conventional Commits:
  `type(scope): imperative summary`.
- `type` MUST be one of `feat`, `fix`, `docs`, `style`, `refactor`, `perf`,
  `test`, or `chore`. Use `!` before `:` for breaking changes, e.g.
  `feat(parser)!: reject legacy operator syntax`.
- `scope` is required and should name the affected product or subsystem, e.g.
  `parser`, `lexer`, `binder`, `spec`, `agents`, `repo`, or `zc`.
- The subject MUST be an imperative ASCII-English phrase with no trailing
  period. Do not use bare sentence titles such as `Implement parser support`
  or `Align spec with grammar`; amend them before push.
- **Subject and body are ASCII-English only.** No CJK characters, no
  non-ASCII quotation marks or dashes. `git log --oneline` must match
  `^[0-9a-f]{7} [ -~]+$` (printable ASCII). Any commit containing
  non-ASCII in the message must be amended **before push**.
- Aggressive amending is preferred over stacking "fix review" micro-commits **before push**.
- After push, amend only if you re-push `--force-with-lease` and coordinate with team.

---

## Documentation Conventions

- **English only.** Doxygen `/// \brief …` on public interfaces; `//` for implementation notes.
- No Chinese anywhere in docs. The only tolerated Chinese text in the entire
  repository is the rule above (Attention §5) — and that one sentence is
  explicitly exempt because it *states* the English-only rule.
- Spec chapters (`docs/spec/chapters/*.md`) **must match the parser and lexer exactly.**
  If the spec says X and parser accepts Y, either fix the spec or fix the parser — no drift.
  Use the `spec-alignment` skill before landing any spec or parser change.
- Audit reports (`docs/reports/zom-*-audit-*.md`) are generated outputs from the
  `ultracode-audit` skill — keep frontmatter machine-parseable.

---

## ANTLR 4 .g4 Authoring Rules (ZOM-G4-PATTERN-001 ~ 003)

> These rules apply to every edit of `docs/spec/ZomLexer.g4` and
> `docs/spec/ZomParser.g4`, and to any generated-parser wrapper in
> `products/zomlang/compiler/parser/` that injects ANTLR actions.
> Cross-reference: `docs/spec/chapters/19-conditional-compilation.md §19.13`
> (worked example) and `products/zomlang/tests/conformance/grammar/README.md
> § Semantic Predicate Matrix` (suite-wide matrix).

### ZOM-G4-PATTERN-001: Tail-Parser-Action Safety Pattern

> Purpose: avoid ALL(\*) Simulator Poisoning (spurious `NoViableAltException`
> on semantically valid input).

Every parser action that needs to `throw ParseCancellationException` to
produce an rc=2 diagnostic **must** follow these placement rules:

- ✅ **MUST** be placed **after the very last terminal token** of its
  alternative (i.e., execute only after the alt has consumed every
  non-ε token it intends to match).
- ❌ **MUST NOT** be placed before a gated semantic predicate
  (`{p}?`) or anywhere else on a prediction-reachable ATN path.
- ❌ **MUST NOT** be placed between terminals in the middle of an
  alternative.
- ❌ **MUST NOT** be embedded inside a `{...}?` semantic predicate
  body — ANTLR catches any `RuntimeException` raised there and
  silently coerces the predicate to `false`, so the diagnostic is
  lost and the grammar appears to accept illegal input.

Failure to honour this rule was the direct cause of the full V1–V4
regression cycle for `attrItem : attrZomCfg`: `{ throw PCE }` lived on a
prediction-reachable path → simulator poisoned the entire alt → gated
predicate `{peekIsZomCfgParen}?` could never rescue it at runtime → NVA
on every `#[zom::cfg(...)]` attribute.

### ZOM-G4-PATTERN-002: Gated Predicate Complementary Partitioning

When a rule has N top-level alternatives that share a non-empty common
prefix, **gate every alt** with a member of a disjoint, exhaustive
predicate family.

Concretely: if `alt1` is guarded by `{p}?`, then `alt2` must be guarded
by `{!p && q}?`, `alt3` by `{!p && !q && r}?`, and so on, so that the
disjunction of all guards is syntactically `true` (P ∧ ¬P coverage).

Benefits:

- Guarantees the DFA is conflict-free. SLL (strong LL(1)) is sufficient
  to take the decision; the simulator never needs to fall back to the
  full ALL(\*) closure.
- Eliminates `antlr4 -Werror` diagnostics of the form
  *"non-LL(1) decision: more than one alternative matches input X"*.
- Makes the intent of each alternative locally obvious to reviewers.

### ZOM-G4-PATTERN-003: Subrule Delegation for Nested Labels

ANTLR 4 reports `error(50): label assigned to ... which is inside a
rewrite/replacement predicate block` (or the equivalent
`label in nested group not supported` diagnostic) when a labelled
alternative appears inside a grouped sub-rule, e.g.

```
// ILLEGAL (error 50):
cfgAtom
    : IDENTIFIER ( v=valuedRhs | b=bareRhs | bad=badRhs )
    ;
```

The correct rewrite preserves the common prefix and moves each labelled
form into its own single-alt subrule, where labelling is legal:

```
// LEGAL (PATTERN-003):
cfgAtom
    : IDENTIFIER ( valuedCfgAtomRhs | bareCfgAtomRhs | badRhsCfgAtomRhs )
    ;

valuedCfgAtomRhs
    : op=cfgOp value=CFG_VALUE   // single alt → labels OK
    ;
```

An additional benefit: at the entry of each subrule, `LA(1)` (the
one-token lookahead) points *after* the shared `IDENTIFIER` prefix,
which fixes a subtle offset bug that otherwise forces predicates to
use `LA(2)` / `LA(3)` manually and drift from the natural grammar
shape.

---

*This file is the top-level entry point. Detailed rules, skills, and subagent
specs live under `.agents/`. Read `design-principles.md` before any design decision;
read `task-router-agent.md` before any non-trivial implementation.*
