# ZomLang Conformance Tests

`conformance/` is the language conformance source and runner area. It has one
source corpus and separate runner-specific expectations.

## Layout

```text
conformance/
├── corpus/            # Pure .zom source fixtures, grouped by spec chapter
├── expectations/      # Runner-specific oracle files
│   ├── ast/
│   ├── binder/
│   ├── diagnostics/
│   ├── e2e/
│   └── grammar/
└── runners/           # Executable runner glue registered with CTest
    ├── ast/
    ├── binder/
    ├── diagnostics/
    ├── e2e/
    └── grammar/
```

Source files must not contain runner-specific directives such as `RUN`,
`CHECK`, `Expected`, or `ExpectedDiagnostic`. Those belong in the matching
expectation file.

## Runner Layers

| Layer | CTest label | Source | Oracle |
|---|---|---|---|
| Grammar | `conformance-grammar` | `corpus/**/*.zom` | `expectations/grammar/**/*.yml` |
| AST | `conformance-ast` | `corpus/**/*.zom` | `expectations/ast/**/*.check` |
| Diagnostics | `conformance-diagnostics` | `corpus/**/*.zom` | `expectations/diagnostics/**/*.check` |
| Parser coverage | `parser;coverage;specification` | `docs/spec/chapters/17-grammar-reference.md` | `zomlang/compiler/parser/parser-coverage.yml` |

Every additional layer must reuse `corpus/` and add only its own expectation
schema and runner.

The parser coverage guard maps every grammar production in
`17-grammar-reference.md` to the parser function that owns it. The guard checks
that no grammar production is unmapped, no stale production remains in
`parser-coverage.yml`, mapped parser functions exist, lexical productions are
marked lexical, and optional AST/test references point to real repository
artifacts.

The AST runner registers a coverage guard that requires every
`corpus/**/*.zom` source to have a same-relative-path
`expectations/ast/**/*.check` oracle. For sources that also have a grammar
oracle, the same guard enforces the grammar verdict: `expected: ACCEPT` must
map to a normal AST `RUN:` line, and `expected: REJECT` must map to `RUN: !`.
AST-only legacy checks and format-contract checks that intentionally do not
have a same-relative-path grammar oracle must be allowlisted in
`tools/check-ast-coverage.py`.

## Chapter Directories

The corpus is grouped by spec chapter:

```text
02-lexical/
03-types/
04-expressions/
05-statements/
06-declarations/
07-patterns/
08-adt/
09-interfaces/
11-error/
12-generics/
13-modules/
15-concurrency/
16-attributes/
20-ffi/
```

Use nested construct directories only when they clarify the source fixture. Do
not create runner-owned source trees.
