# Zom V1 Modules Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Bring the compiler AST, parser, and module tests into alignment with the Zom v1 static module syntax.

**Architecture:** Replace the old string-based import model with AST nodes that represent dotted module paths and named import/export specifiers. Update the hand-written parser to parse the new top-level module grammar and emit the revised AST, then refresh unit and lit tests around the new syntax and AST dump shape.

**Tech Stack:** C++, zc core containers, hand-written parser, ztest unit tests, lit/FileCheck integration tests

---

### Task 1: Define the module AST surface

**Files:**
- Modify: `products/zomlang/compiler/ast/module.h`
- Modify: `products/zomlang/compiler/ast/module.cc`
- Modify: `products/zomlang/compiler/ast/factory.h`
- Modify: `products/zomlang/compiler/ast/factory.cc`
- Modify: `products/zomlang/compiler/ast/dumper.h`
- Modify: `products/zomlang/compiler/ast/dumper.cc`

**Step 1: Write the failing test**

Add parser and AST expectations for:

```cpp
module math.geometry;
import math.geometry as geo;
import math.geometry.{Point as GeoPoint, distance};
export {GeoPoint};
```

The test should assert that the AST preserves:
- module name segments
- module import alias
- named import specifiers and aliases
- export-list specifiers

**Step 2: Run test to verify it fails**

Run:

```bash
cmake --preset debug
cmake --build --preset debug --target parser-test
ctest --test-dir build/debug --output-on-failure -R parser-test
```

Expected: parser tests fail because the old AST still models string-based imports and expression-based exports.

**Step 3: Write minimal implementation**

Implement:
- `ModulePath` as dotted identifier segments
- `ImportSpecifier`
- `ExportSpecifier`
- `ImportDeclaration` with module-import vs named-import shape
- `ExportDeclaration` with export-list vs re-export shape

**Step 4: Run test to verify it passes**

Run:

```bash
cmake --build --preset debug --target parser-test
ctest --test-dir build/debug --output-on-failure -R parser-test
```

Expected: parser tests covering AST structure pass.

### Task 2: Parse the v1 top-level module grammar

**Files:**
- Modify: `products/zomlang/compiler/parser/parser.h`
- Modify: `products/zomlang/compiler/parser/parser.cc`

**Step 1: Write the failing test**

Add parser coverage for:

```zom
module graphics;
import math.geometry;
import math.geometry.{Point, distance as calcDistance};
export fun length() -> i32 { return 0; }
export {Point};
export math.geometry.{distance as distance2};
```

**Step 2: Run test to verify it fails**

Run:

```bash
cmake --build --preset debug --target parser-test
ctest --test-dir build/debug --output-on-failure -R parser-test
```

Expected: parse failures or incorrect AST shape because the parser still expects `import name = "path"` and `export expr`.

**Step 3: Write minimal implementation**

Implement parser support for:
- optional leading `module` declaration
- module import clauses
- grouped named imports
- declaration-site export
- local export lists
- explicit re-export clauses

**Step 4: Run test to verify it passes**

Run:

```bash
cmake --build --preset debug --target parser-test
ctest --test-dir build/debug --output-on-failure -R parser-test
```

Expected: parser tests pass with the new syntax.

### Task 3: Refresh integration coverage for AST dumping

**Files:**
- Modify: `products/zomlang/tests/language/modules/import-export.zom`

**Step 1: Write the failing test**

Replace the old negative test with a positive AST dump snapshot using the new v1 syntax.

**Step 2: Run test to verify it fails**

Run:

```bash
lit -v products/zomlang/tests/language/modules/import-export.zom
```

Expected: FileCheck mismatch because the parser and AST dumper still emit the old shape.

**Step 3: Write minimal implementation**

Update the AST dumper output for the revised module nodes and specifier lists.

**Step 4: Run test to verify it passes**

Run:

```bash
lit -v products/zomlang/tests/language/modules/import-export.zom
```

Expected: AST dump matches the new snapshot.

### Task 4: Full verification

**Files:**
- Modify: `products/zomlang/tests/unittests/compiler/parser/parser-test.cc`
- Modify: `products/zomlang/tests/unittests/compiler/ast/factory-test.cc`
- Modify: `products/zomlang/tests/language/modules/import-export.zom`

**Step 1: Run focused verification**

Run:

```bash
ctest --test-dir build/debug --output-on-failure -R parser-test
lit -v products/zomlang/tests/language/modules/import-export.zom
```

Expected: focused module coverage passes.

**Step 2: Run broader required verification**

Run:

```bash
cmake --build --preset debug
ctest --preset default -R parser-test
```

Expected: updated parser target builds cleanly and parser unit suite passes.

**Step 3: Run formatting sanity check**

Run:

```bash
git diff --check -- products/zomlang/compiler/ast/module.h products/zomlang/compiler/ast/module.cc products/zomlang/compiler/ast/factory.h products/zomlang/compiler/ast/factory.cc products/zomlang/compiler/ast/dumper.h products/zomlang/compiler/ast/dumper.cc products/zomlang/compiler/parser/parser.h products/zomlang/compiler/parser/parser.cc products/zomlang/tests/unittests/compiler/parser/parser-test.cc products/zomlang/tests/unittests/compiler/ast/factory-test.cc products/zomlang/tests/language/modules/import-export.zom
```

Expected: no whitespace or patch formatting issues.
