# ZomLang Compiler Test Suite

This directory contains the compiler test suite. Tests are grouped by the
contract they verify, not by the implementation detail that happens to run
them.

## Test Families

| Path | Contract | Runner |
|---|---|---|
| `unittests/` | Component behavior for compiler libraries | ztest through CTest |
| `conformance/` | Shared language source corpus plus runner expectations | CTest, lit, ANTLR |
| `regression/` | Reproducers for fixed bugs | CTest |
| `tools/` | Test support tools such as FileCheck and lit regeneration | Python |

`conformance/corpus` is the only source-file entry point for language
conformance. Runner-specific oracle data lives under
`conformance/expectations`, and executable runner glue lives under
`conformance/runners`.

## Conformance Model

Conformance has three parts:

1. Source fixtures: pure `.zom` files under `conformance/corpus/<chapter>/`.
2. Expectations: runner-specific oracle files under `conformance/expectations/`.
3. Runners: executable CTest/lit/ANTLR integration under `conformance/runners/`.

Current oracles:

| Target | CTest label | Meaning |
|---|---|---|
| `check-conformance-grammar` | `conformance-grammar` | ANTLR `ZomLexer.g4` / `ZomParser.g4` ACCEPT/REJECT verdicts |
| `check-conformance-ast` | `conformance-ast` | Real `zomc compile --dump-ast` output checked with lit/FileCheck |
| `check-conformance-diagnostics` | `conformance-diagnostics` | Real `zomc compile --check` diagnostics checked with lit/FileCheck |
| `check-conformance` | `conformance` | Every registered conformance oracle |

Future binder and e2e layers must consume the same corpus and add only their own
expectation files plus runner implementation.

## Running Tests

```bash
cmake --preset sanitizer
cmake --build --preset sanitizer -j
ctest --preset default --output-on-failure
```

Focused runs:

```bash
ctest --preset default -L unittest --output-on-failure
ctest --preset default -L conformance --output-on-failure
ctest --preset default -L conformance-ast --output-on-failure
ctest --preset default -L conformance-grammar --output-on-failure
ctest --preset default -L lit --output-on-failure
```

CMake convenience targets:

```bash
cmake --build --preset sanitizer --target check-unit
cmake --build --preset sanitizer --target check-conformance
cmake --build --preset sanitizer --target check-conformance-ast
cmake --build --preset sanitizer --target check-conformance-grammar
cmake --build --preset sanitizer --target check-regression
```

## Adding Unit Tests

Add focused ztest files under the owning component directory:

```text
products/zomlang/tests/unittests/compiler/<component>/<name>-test.cc
```

Use `ZC_TEST`, `ZC_EXPECT`, and `ZC_ASSERT`. Keep unit tests close to the
library boundary they exercise.

## Adding AST/FileCheck Conformance

Add the source fixture under the deepest applicable spec chapter directory:

```text
products/zomlang/tests/conformance/corpus/<chapter>/<feature>.zom
```

Add the matching expectation file at the same relative path:

```text
products/zomlang/tests/conformance/expectations/ast/<chapter>/<feature>.check
```

The `.check` file owns lit directives and FileCheck patterns:

```text
// RUN: %zomc compile --dump-ast %corpus/05-statements/example.zom | %FileCheck %s
// CHECK: "node": "SourceFile"
```

For expected failures, assert non-zero exit and prefer diagnostic codes over
English wording:

```text
// RUN: ! %zomc compile --dump-ast %corpus/05-statements/broken.zom 2>&1 | %FileCheck %s
// CHECK: ZOM
```

After parser, binder, AST dump, or diagnostic changes, regenerate only the
affected expectation unless the AST format changed globally:

```bash
python3 products/zomlang/tests/tools/regen-lit.py \
  products/zomlang/tests/conformance/corpus/<chapter>/<feature>.zom
```

Read the regenerated diff before accepting it.

## Adding Grammar Conformance

Add the source fixture under `conformance/corpus/<chapter>/`. Add the matching
grammar expectation under `conformance/expectations/grammar/<chapter>/` with the
same relative path and a `.yml` suffix:

```yaml
section: "Section and short description"
covers_rule: "ruleName"
expected: "ACCEPT"
notes: []
```

Grammar expectations only assert the ANTLR oracle's ACCEPT/REJECT verdict.
Public `ZOMxxxx` diagnostics are compiler behavior and belong in AST/FileCheck
expectations.

Run grammar conformance directly:

```bash
bash products/zomlang/tests/conformance/runners/grammar/run_tests.sh
bash products/zomlang/tests/conformance/runners/grammar/run_tests.sh 04-expressions
```

ANTLR build output is written to the build tree by CTest or to
`build/conformance-grammar` when run directly.

## Support Tools

| Tool | Purpose |
|---|---|
| `tools/filecheck.py` | FileCheck-compatible AST/diagnostic pattern matching |
| `tools/regen-lit.py` | Regenerates AST `.check` expectations from current `zomc --dump-ast` output |
