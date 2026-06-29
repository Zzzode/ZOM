# ZomLang Compiler Test Suite

This directory contains the compiler test suite. Tests are grouped by the
contract they verify, not by the implementation detail that happens to run them.

## Test Families

| Path | Contract | Runner |
|---|---|---|
| `unittests/` | Component behavior for compiler libraries | ztest through CTest |
| `conformance/` | Language source corpus consumed by runner/oracle layers | CTest, lit, ANTLR |
| `language/` | AST/FileCheck conformance fixtures currently registered as lit tests | lit through CTest |
| `regression/` | Reproducers for fixed bugs | CTest |
| `tools/` | Test support tools such as FileCheck and lit regeneration | Python |

The long-term direction is for `conformance/` to be the source corpus root.
Runner layers such as grammar acceptance, AST snapshots, diagnostics, and e2e
execution should consume that corpus rather than owning separate source trees.

## Conformance Model

Conformance has two parts:

1. Source fixtures: `.zom` files that describe a language construct.
2. Oracles: runner-specific expectations for a compiler surface.

Current oracles:

| Target | CTest label | Meaning |
|---|---|---|
| `check-conformance-grammar` | `conformance-grammar` | ANTLR `ZomLexer.g4` / `ZomParser.g4` ACCEPT/REJECT verdicts |
| `check-conformance-ast` | `conformance-ast` | Real `zomc compile --dump-ast` output checked with lit/FileCheck |
| `check-conformance` | `conformance` | Every registered conformance oracle |

`language/` remains the physical home of the current AST/FileCheck fixtures
until the source corpus and runner expectations are split. Its CTest tests are
already labelled as `conformance-ast`.

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

Add a `.zom` fixture under the deepest applicable `language/` directory for
now. Include a `RUN:` line and stable `CHECK:` assertions:

```zom
// RUN: %zomc compile --dump-ast %s | %FileCheck %s

fun greet(name: str) -> str {
    return name;
}

// CHECK: "node": "SourceFile"
// CHECK: "node": "FunctionDecl"
// CHECK: "name": "greet"
```

For expected failures, assert non-zero exit and prefer diagnostic codes over
English wording:

```zom
// RUN: ! %zomc compile --dump-ast %s 2>&1 | %FileCheck %s

let = ;

// CHECK: ZOM
```

After parser, binder, AST dump, or diagnostic changes, regenerate only the
affected lit fixture unless the AST format changed globally:

```bash
python3 products/zomlang/tests/tools/regen-lit.py products/zomlang/tests/language/path/test.zom
```

Read the regenerated diff before accepting it.

## Adding Grammar Conformance

Add grammar verdict fixtures under `conformance/grammar/<chapter>/`. Each file
must start with the four-line header consumed by `run_tests.sh`:

```zom
// Section and short description
// Covers rule: ruleName
// Expected: ACCEPT
// ExpectedDiagnostic: none
```

Run grammar conformance directly:

```bash
bash products/zomlang/tests/conformance/grammar/run_tests.sh
bash products/zomlang/tests/conformance/grammar/run_tests.sh 04-expressions
```

ANTLR build output is written to the build tree by CTest or to
`build/conformance-grammar` when run directly.

## Support Tools

| Tool | Purpose |
|---|---|
| `tools/filecheck.py` | FileCheck-compatible AST/diagnostic pattern matching |
| `tools/regen-lit.py` | Regenerates CHECK blocks from current `zomc --dump-ast` output |
| `lit.cfg.py` | lit suite configuration for compiler conformance tests |
