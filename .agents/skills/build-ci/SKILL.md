---
name: build-ci
description: Build, configure, and verify ZOM with CMake presets, sanitizers, and coverage. Use whenever a change touches any source file that could affect compilation.
---

# Build & CI Skill

Invokes automatically for any task that modifies `.h`, `.cc`, `.cmake`,
`CMakeLists.txt`, or any test file. Also invoked manually before any commit
that is expected to be build-clean.

---

## Step 1 — Configure

```bash
cmake --preset sanitizer
```

If `ZOM_ENABLE_COVERAGE=ON` is required (coverage runs, spec-alignment
verification with coverage gate):

```bash
cmake --preset sanitizer -DZOM_ENABLE_COVERAGE=ON
```

**Fail conditions that must be reported, not silently worked around:**

- Preset not found → confirm the working directory is the repo root.
- Compiler not found → check `CC`/`CXX`; ZOM requires GCC 12+, Clang 15+,
  or Xcode 15+.
- Missing Python, psutil (lit dependency) → `pip install psutil`.

---

## Step 2 — Build

```bash
cmake --build --preset sanitizer -j
```

Build errors → surface them verbatim. If the error is in a header owned by
zc or the compiler, identify the owning subagent (see `.agents/subagents/`)
and delegate a fix rather than patching blind.

**Never** disable a warning or suppress a sanitizer to "make it build."

---

## Step 3 — Tests

Full run:

```bash
ctest --preset default --output-on-failure
```

Subsets:

| Goal | Command |
|---|---|
| Unit tests only | `ctest --preset default -L unittest` |
| Lit tests only | `ctest --preset default -L lit` |
| Lit tests matching a substring | `ctest --preset default -L lit -R "parser\|concurrency"` |
| Re-run only previously failed tests | `ctest --preset default --rerun-failed` |
| With verbose per-test output | append `--verbose` |

When a test fails:

1. Attach the full output (`--output-on-failure` is mandatory).
2. Classify the failure: assertion, sanitizer, timeout, missing diagnostic,
   FileCheck mismatch, XFAIL start passing, etc.
3. If it is a FileCheck mismatch and the new output is semantically correct
   per the latest spec, run the `regen-lit.py` helper for that test
   (see `.agents/rules/testing.md` § After a Parser or Spec Change), read
   the regenerated diff, and land it.

---

## Step 4 — Format Check

```bash
python scripts/check-format.py
```

Format issues → apply `.clang-format` directly:

```bash
find products/zomlang libraries/zc -name "*.cc" -o -name "*.h" \
  | xargs clang-format -style=file -i
```

Do not edit `check-format.py` to exclude files. Either format them or add
an explicit exclusion to `.clang-format-ignore` with a comment.

---

## Step 5 — Coverage (Optional)

```bash
cmake --preset sanitizer -DZOM_ENABLE_COVERAGE=ON
cmake --build --preset sanitizer -j
ctest --preset default
make coverage
```

Report the per-module coverage percentages. If `lexer/`, `parser/`, `ast/`,
`binder/`, or `symbol/` dropped below the baseline recorded in the last
coverage summary, **block the commit** until the regression is explained or
tests are added.

---

## Exit Criteria

Build + ctest pass with zero failures, sanitizer clean, format clean.
Coverage (when requested) does not regress below baseline.
