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

Future layers must reuse `corpus/` and add only their own expectation schema and
runner.

The AST runner registers a coverage guard that requires every
`corpus/**/*.zom` source to have a same-relative-path
`expectations/ast/**/*.check` oracle. Format-contract checks that intentionally
reuse an existing corpus source must be allowlisted in
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
19-conditional/
20-ffi/
21-macros/
```

Use nested construct directories only when they clarify the source fixture. Do
not create runner-owned source trees.
