---
paths:
  - "tests/**"
  - "examples/**"
  - "**/*test*.cc"
  - "**/*test*.zom"
---

# Testing Strategy

ZOM uses two mandatory test layers and one optional (but strongly recommended)
fuzz layer. Every non-documentation change must add coverage or explain why the
existing coverage is already sufficient.

| Layer | Tool | Purpose | Runs With |
|---|---|---|---|
| **Unit tests** | `ztest` (harness in-tree) | Low-level behavior of lexer, parser visitors, binder, zc types, utilities | `ctest --preset default -L unittest` |
| **AST / integration tests** | **LLVM lit + FileCheck** (+ regen helper) | End-to-end frontend: compile a `.zom` source and assert on AST dump / diagnostics / bindings | `ctest --preset default -L lit` |
| **Fuzz tests** (future) | `libFuzzer` + custom mutators | Lexer, parser robustness against adversarial input | Manual (`fuzz/` harness) |

---

## Unit Tests (ztest)

### Naming

```
// ✓ Good — descriptive, says what behavior is being tested
ParserTest.ParseErrorDefaultExpressionOperator
LexerTest.ErrorPropagateTokenMatchesEbnf
BindingVerifierTest.ForeignModulePrivateDefinitionIsRejected

// ❌ Bad — generic or terse
TestParser.Test1
CheckCase
runLexer
```

### Coverage Contract

For any non-trivial function `Xxx` in the compiler:

- At least one test for the happy path.
- At least one test for each error / early-return branch (use `ZOMxxxx` code or
  `DiagnosticKind` enum in the assertion — never string-match English output).
- If the function branches on a flag / enum variant / kind — assert every
  combination or document in a comment why the uncovered combinations are
  unreachable (and add `ZC_UNREACHABLE()`).

---

## AST / Frontend Tests (LLVM lit + FileCheck)

Lit tests are the **source of truth** for spec ↔ implementation alignment.
Each AST conformance case has a pure source under
`tests/conformance/corpus/` and a matching lit/FileCheck
expectation under `tests/conformance/expectations/ast/`.

### Required Preamble

Every AST expectation file starts with at least:

```text
// RUN: %zomc compile --dump-ast %corpus/05-statements/example.zom 2>&1 | %FileCheck %s
```

Optional modifiers:

| Modifier | Use when |
|---|---|
| `// RUN: ! %zomc compile --dump-ast %corpus/... 2>&1 ...` | The source is *expected* to exit non-zero (parse error, type error). |
| `// XFAIL: *` | The test currently fails and the root cause is tracked in a finding / issue. `XFAIL` without a linked bug / finding number is rejected. |
| `// REQUIRES: linux` / `// UNSUPPORTED: darwin` | Test targets platform-specific behavior. |

### FileCheck Style

| Construct | Prefer over | Why |
|---|---|---|
| `CHECK-NEXT:` | `CHECK:` on consecutive lines | Catches accidental intermediate nodes. |
| `CHECK-SAME:` | Repeating the same `CHECK:` line with `.`* | Prevents false positives on re-ordered fields. |
| `CHECK-DAG:` only when order is truly arbitrary | Use `CHECK-NOT:` + `CHECK-NEXT:` otherwise | Avoid masking node order bugs. |
| `CHECK: ZOM2011` | `CHECK: {{.*}} error` | Stable across wording changes. |

### After a Parser or Spec Change

Run:

```bash
python3 tests/tools/regen-lit.py \
  tests/conformance/corpus/path/to/test.zom
```

This rewrites FileCheck lines to match the *current* actual output. After
regeneration you **must** read the diff — regeneration is mechanical; approving
the regenerated checks is a human review step. Never land a regenerated lit test
without eyeballing the changed assertions.

### `XFAIL` Hygiene

- An `XFAIL` test must include the finding ID or issue link in the comment:
  `// XFAIL: * — covered by finding ERR-001 (?! token not lexed yet)`.
- Once the underlying bug is fixed, the `XFAIL` marker must be removed in the
  same commit. Do not accumulate permanently-xfailing tests.
- If a `XFAIL` test starts **passing** (i.e. you fixed it accidentally), that's
  still a failure — remove the `XFAIL` marker immediately.

---

## End-to-End Sanitizer Runs

- **All tests run under the `sanitizer` preset by default for CI and local dev.**
- If a sanitizer fires:
  1. Fix the root cause. Do **not** mark the test as `UNSUPPORTED: asan`.
  2. Add a regression test that reproduces the exact crash / UB.
  3. Verify that the regression test fails on `HEAD^` and passes on HEAD.

---

## Coverage

```bash
# Enable coverage at configure time
cmake --preset sanitizer -DZOM_ENABLE_COVERAGE=ON
cmake --build --preset sanitizer
ctest --preset default

# Report
make coverage
```

### Policy

- Coverage for `lexer/`, `parser/`, `ast/`, `binder/`, `checker/`, `type/`,
  `identity/`, `hir/`, `mir/`, and `ir/` must never regress below the baseline
  recorded in the most recent coverage report.
- New compiler source files without >70% line coverage are rejected on review
  unless the code is a pure FFI boundary (tested elsewhere) or infrastructure
  glue whose failure modes are inherently unreachable without mocking.

---

## Test Anti-Patterns (Block on Review)

1. ❌ Tests that assert only on human-readable diagnostic text. Assert on the
   `ZOMxxxx` diagnostic code / enum variant first; text is optional.
2. ❌ A test whose only assertion is `EXPECT_TRUE(true);` to make coverage green.
3. ❌ `Thread.sleep(1000)` or wall-clock waits to "let things happen." Use proper
   synchronization primitives or explicit poll loops with bounded timeout.
4. ❌ Flaky tests that are not triaged within 48 hours. If a test is flaky and
   we cannot immediately fix it, **delete the test** per Principle 4 rather
   than let it silently train the team to ignore failures.
5. ❌ Commenting out a failing assertion "to make CI green." Either fix the
   assertion or delete the whole test. No half-alive tests.
