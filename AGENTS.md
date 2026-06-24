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
   chapters, diagnostic messages, audit reports, design documents, and
   **especially every git commit message and subject line**.
   **No Chinese anywhere in the repository except this single sentence.**
   You must think in English, produce all code and docs in English, and
   only translate the final reply to Chinese when addressing the user.
6. **Commit messages are ASCII-English only.** No CJK characters, no
   non-ASCII punctuation in the subject or body. `git log --oneline` must
   be cleanly grepable with an ASCII regex. See also `§ Git Commits`
   below and `.agents/rules/design-principles.md` § Radical Refactoring.
7. If a tool call fails with a syntax error, read the tool definition and retry
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
- Title format: `area(scope): imperative summary`, e.g.
  `docs(reports): add concurrency audit (44 findings)`.
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

*This file is the top-level entry point. Detailed rules, skills, and subagent
specs live under `.agents/`. Read `design-principles.md` before any design decision;
read `task-router-agent.md` before any non-trivial implementation.*
