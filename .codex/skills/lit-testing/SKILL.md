---
name: lit-testing
description: Author, regenerate, and debug ZOM's LLVM lit + FileCheck AST/integration tests. Use whenever parser, binder, diagnostics, or spec chapters change.
---

# lit Testing Skill

Lit tests are the **contract** between spec and implementation.
When in doubt: add a lit test. Regenerate before merge. Read the regenerated
diff carefully — mechanical regeneration is not automatic approval.

---

## Anatomy of a Lit Test

Every AST conformance test has two files:

- Source: `products/zomlang/tests/conformance/corpus/<chapter>/<path>.zom`
- Expectation: `products/zomlang/tests/conformance/expectations/ast/<chapter>/<path>.check`

```text
// REQUIRES: default
// RUN: %zomc compile --dump-ast %corpus/11-error/error-propagate.zom 2>&1 | %FileCheck %s
// XFAIL: * — ERR-001: `?!` token not lexed yet. Remove when fixed.
// CHECK:      TranslationUnit
// CHECK-NEXT:   FunctionDecl name="main" visibility="Pub"
// CHECK-NEXT:     Block
// CHECK:          VarDecl name="x" initializer=IntegerLiteral(42)
// CHECK:          ErrorDefaultOperator
```

```zom
pub fn main() {
    let x = 42;
    x?!;
}
```

### Directive Cheat Sheet

| Directive | Purpose |
|---|---|
| `// RUN: cmd` | Shell command to run. Use `! cmd` to assert non-zero exit. |
| `// REQUIRES: feature` | Skip unless the config exports this feature. |
| `// UNSUPPORTED: platform` | Skip on this platform (e.g. `darwin`). |
| `// XFAIL: *` | Expect failure. **Mandatory:** append a finding/ticket link. |
| `// CHECK:` | Line appears, anywhere after the previous match. |
| `// CHECK-NEXT:` | Must match the very next line. |
| `// CHECK-SAME:` | Same line as previous CHECK, optionally with gaps. |
| `// CHECK-DAG:` | Line appears anywhere between the surrounding markers (order-agnostic — use sparingly). |
| `// CHECK-NOT:` | Line must **not** appear in the excluded range. |
| `// CHECK: ZOM1234` | Match a diagnostic by code. Prefer this over matching English wording. |

---

## Where to Put New Tests

```
products/zomlang/tests/conformance/
├── corpus/<chapter>/                 // pure ZOM source
└── expectations/ast/<chapter>/        // lit RUN + FileCheck oracle
```

Pick the deepest relevant directory. One feature → one test file is fine;
do not create one mega-file that tests 12 unrelated things. Source files stay
free of RUN/CHECK directives.

---

## Regeneration Workflow

**After any change to parser, binder, AST dump, or diagnostics:**

```bash
python3 products/zomlang/tests/tools/regen-lit.py \
  products/zomlang/tests/conformance/corpus/<chapter>/<path>/<test>.zom
```

**Then — this is the human step:**

1. Run `git diff <test>.zom` and read *every changed CHECK line.*
2. If the diff matches the intended semantic change → approve.
3. If the diff shows unintended AST shape or an extra error diagnostic → fix
   the code change first, regenerate again.
4. If a test now passes that was `XFAIL` → remove the `XFAIL` marker in the
   same commit (see `.agents/rules/testing.md` § XFAIL Hygiene).

To regenerate the whole suite (rare — only after a broad refactor like
`SyntaxKind` renumbering):

```bash
python3 products/zomlang/tests/tools/regen-lit.py \
  products/zomlang/tests/conformance/corpus/
```

Confirm the diff is exactly what you expected before committing.
Do not hide a 400-file regeneration inside a larger commit.

---

## Negative / Error Tests

For expected errors, assert **non-zero exit** and the exact diagnostic code:

```zom
// RUN: ! zomlangc --dump-ast %s 2>&1 | FileCheck %s

fn broken() { let x = ; }

// CHECK: :[[@LINE-1]]:{{[0-9]+}}: error: ZOM2001
// CHECK-SAME: expected expression
```

Key rules:

- Prefix the run line with `! ` so lit knows the command *must* fail.
- Match the code first: `ZOM2001` is stable across wording changes.
- Use `[[@LINE-N]]` so relocating the source line does not break the test.
- Never assert on error text that is likely to change (typo fixes, wording
  improvements). Codes are contracts; text is not.

---

## Debugging a Failing lit Test

```bash
# Run just this test, verbose:
ctest --preset default -R lit-<chapter>-<path>-<test> --verbose

# Or directly:
zomc compile --dump-ast products/zomlang/tests/conformance/corpus/<chapter>/<path>/xxx.zom
```

Most lit failures fall into one of these buckets — diagnose in order:

1. **FileCheck mismatch:** the AST shape changed. If intended → regenerate.
   If unintended → find the parser/binder commit that caused it.
2. **Unexpected pass of XFAIL test:** root cause got fixed accidentally.
   Remove the XFAIL marker.
3. **Sanitizer crash inside the compiler:** the test is a reducer for a real
   bug. Minimize it further (remove lines until it still crashes), add it
   to the regression suite with a stable check, and fix the compiler.
4. **Flaky / timing-dependent:** lit tests are AST-based; timing is never a
   factor. If a test flips, something is non-deterministic in AST dumping
   or symbol ordering — *fix the ordering, don't add CHECK-DAG.*

---

## Exit Criteria

- Every meaningful parser/binder/diagnostic change ships with at least one
  new lit test or modifies an existing one.
- `ctest --preset default -L lit` passes with zero failures.
- No `XFAIL` marker exists without an accompanying finding / ticket link.
- No `CHECK-DAG` was added to mask a non-determinism bug.
