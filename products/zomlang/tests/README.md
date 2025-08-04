# ZomLang Compiler Test Suite

This directory contains the comprehensive test suite for the ZomLang compiler using a dual-framework approach: **ztest** for unit tests and **lit** for AST integration tests. The tests are organized into several categories to ensure thorough coverage of all compiler components and language features.

## Testing Framework Overview

The ZomLang test suite uses two specialized testing frameworks:

1. **ztest Unit Tests**: For testing individual compiler components in isolation
2. **lit AST Tests**: For testing end-to-end AST generation and validation with FileCheck-style assertions

## Test Structure

### 1. Unit Tests (`unittests/`) - ztest Framework

Low-level component tests that verify individual compiler modules work correctly in isolation using the ztest framework (a customized Google Test).

- **`unittests/compiler/`** - Tests for compiler components
  - `basic/` - Basic compiler infrastructure tests
  - `source/` - Source manager and location tests
  - `lexer/` - Lexical analysis tests
  - `parser/` - Syntax analysis tests
  - `ast/` - AST node and manipulation tests
  - `diagnostics/` - Error reporting tests
  - `checker/` - Semantic analysis tests

**Example ztest Unit Test**:

```cpp
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/factory.h"

ZC_TEST("ASTFactory: Basic Node Creation") {
  ASTFactory factory;
  auto node = factory.createSourceFile("test.zom");
  ZC_EXPECT(node != nullptr);
  ZC_EXPECT(node->getType() == NodeType::SourceFile);
}
```

### 2. Language Tests (`language/`) - lit Framework

End-to-end tests that verify the compiler correctly handles ZomLang language constructs and generates proper AST output using the lit testing framework with FileCheck-style validation.

- **`language/lexical/`** - Lexical analysis (identifiers, keywords, literals, operators, comments)
- **`language/expressions/`** - Expression tests (primary, binary, unary, assignment, call, member)
- **`language/statements/`** - Statement tests (blocks, control-flow, loops, jumps, try-catch)
- **`language/declarations/`** - Declaration tests (variables, functions, classes, interfaces, types)
- **`language/types/`** - Type system tests (basic types, generics, type checking)
- **`language/functions/`** - Function-specific tests (parameters, return types, closures)
- **`language/classes/`** - Class tests (inheritance, access modifiers, constructors)
- **`language/interfaces/`** - Interface tests (declarations, implementations, inheritance)
- **`language/modules/`** - Module system tests (imports, exports, resolution)
- **`language/errors/`** - Error handling and recovery tests
- **`language/semantic/`** - Semantic analysis (type checking, scoping, validation)

**Example lit AST Test**:

```zom
// RUN: %zomc compile --dump-ast %s | %FileCheck %s

fun foo(n: i32, s: str) -> str {}


// CHECK: {
// CHECK-NEXT:   "node": "SourceFile",
// CHECK-NEXT:   "fileName": "{{.*function.zom}}",
// CHECK-NEXT:   "children": [
// CHECK-NEXT:     {
// CHECK-NEXT:       "node": "FunctionDeclaration",
// ......
```

### 3. Performance Tests (`performance/`)

Benchmarks and performance regression tests.

- **Lexer benchmarks** - Tokenization performance
- **Parser benchmarks** - Parsing performance
- **Memory usage tests** - Memory consumption analysis
- **Compilation speed tests** - Overall compilation performance
- **Large file handling** - Scalability tests

### 4. Regression Tests (`regression/`)

Tests that ensure previously fixed bugs do not reappear.

- **Issue-based organization** - Each directory named `issue-XXX/` contains tests for a specific bug
- **Automatic discovery** - Tests are automatically discovered and added
- **Custom regression tests** - Complex scenarios that require custom test logic

## Running Tests

### All Tests

```bash
# Configure and build with sanitizer preset (recommended for testing)
cmake --preset sanitizer
cmake --build --preset sanitizer

# Run all tests using the default test preset
ctest --preset default
# or run all tests with the allTests preset
ctest --preset allTests
```

### Coverage Tests

```bash
# Configure and build with coverage preset
cmake --preset coverage
cmake --build --preset coverage

# Run tests with coverage collection
ctest --preset coverageTests
```

### Manual Test Execution

```bash
# Run tests manually with output on failure
ctest --output-on-failure

# Run tests with specific configuration
ctest --preset default --output-on-failure
```

### Individual Tests

#### ztest Unit Tests

```bash
ctest -R "unit-basic-thread-pool-test"  # Run specific unit test
ctest -R "ast-factory-test"             # Run AST factory tests
ctest -L "unittest"                     # Run all unit tests
```

#### lit AST Tests

```bash
# Run a specific lit test
export PATH="$PATH:/Users/$(whoami)/Library/Python/3.9/bin"
lit products/zomlang/tests/language/declarations/functions/function-definitions/function-lit.zom -v

# Run all lit tests in a directory
lit products/zomlang/tests/language/ -v

# Run lit test with FileCheck manually
./build-sanitizer/products/zomlang/utils/zomc/zomc compile --dump-ast my-test.zom | \
  python3 products/zomlang/tests/tools/filecheck.py my-test.zom
```

#### Test Categories

```bash
ctest -R "language-"                     # Run all language tests
ctest -R "regression-issue-001"          # Run specific regression test
ctest -L "functions"                     # Function-related tests
ctest -L "ztest"                        # All ztest unit tests
ctest -L "lit"                          # All lit integration tests
```

## Adding New Tests

### ztest Unit Tests

1. Create a new test file in the appropriate `unittests/` subdirectory
2. Follow the naming convention: `<component>-test.cc`
3. Include the necessary headers and use the ztest framework
4. Add the test to the CMakeLists.txt in the same directory

**Example**:

```cpp
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/factory.h"

ZC_TEST("ASTFactory: Basic Node Creation") {
  ASTFactory factory;
  auto node = factory.createSourceFile("test.zom");
  ZC_EXPECT(node != nullptr);
  ZC_EXPECT(node->getType() == NodeType::SourceFile);
}

ZC_TEST("ASTFactory: Function Declaration") {
  ASTFactory factory;
  auto func = factory.createFunctionDeclaration("testFunc");
  ZC_EXPECT(func != nullptr);
  ZC_EXPECT(func->getName() == "testFunc");
}
```

### lit AST Tests

1. Create a new `.zom` file in the appropriate `language/` subdirectory
2. Add the `// RUN:` directive to specify how to run the test
3. Include `// CHECK:` directives to validate AST output
4. Use descriptive comments to explain the test purpose

**Example**:

```zom
// RUN: %zomc compile --dump-ast %s | %FileCheck %s

// Test: Basic function declaration with parameters
// This test verifies that function declarations with typed parameters
// generate the correct AST structure

fun greet(name: str, age: i32) -> str {
    return "Hello, " + name + "!";
}

// CHECK: "node": "SourceFile"
// CHECK: "node": "FunctionDeclaration"
// CHECK: "name": "greet"
// CHECK: "parameters"
// CHECK: "name": "name"
// CHECK: "type": "str"
// CHECK: "name": "age"
// CHECK: "type": "i32"
// CHECK: "returnType": "str"
```

**FileCheck Patterns**:

- `// CHECK:` - Must appear in order
- `// CHECK-NEXT:` - Must appear on the very next line
- `// CHECK-NOT:` - Must not appear between here and next CHECK
- `// CHECK-SAME:` - Must appear on the same line as previous CHECK

### Regression Tests

1. Create a directory named `issue-XXX/` in `regression/`
2. Add `.zom` test files to the directory
3. Optionally add `.expected` files for output comparison
4. Tests will be automatically discovered
5. Use appropriate framework (ztest for unit-level, lit for AST-level)

**Example ztest Regression Test**:

```cpp
// Regression test for issue #123 - Parser crash on nested generics
#include "zc/ztest/test.h"
#include "zomlang/compiler/parser/parser.h"

ZC_TEST("Regression Issue 123: Nested Generic Types") {
  Parser parser;
  auto result = parser.parseType("Container<Array<str>>");
  ZC_EXPECT(result.isSuccess());
  ZC_EXPECT(result.getType()->isGeneric());
}
```

**Example lit Regression Test**:

```zom
// RUN: %zomc compile --dump-ast %s | %FileCheck %s

// Regression test for issue #123
// Bug: Parser incorrectly handles nested generic types
// Fixed: Proper precedence handling in type parser

type Container<T> = {
    items: Array<T>
};

type NestedContainer = Container<Array<str>>;

// CHECK: "node": "TypeDeclaration"
// CHECK: "name": "Container"
// CHECK: "node": "TypeDeclaration"
// CHECK: "name": "NestedContainer"
// CHECK: "genericArgs"
```

## Test Utilities

The test suite includes several utility tools to aid in test development and debugging:

### Test Frameworks

- **ztest**: Customized Google Test framework for unit testing
  - Located in `libraries/zc/ztest/`
  - Provides `ZC_TEST`, `ZC_EXPECT`, `ZC_ASSERT` macros
  - Automatic test discovery and registration

- **lit**: LLVM Integrated Tester for AST validation
  - Python-based test runner with FileCheck integration
  - Supports `// RUN:` directives for test execution
  - Pattern matching with `// CHECK:` directives

### Test Tools

- **FileCheck** (`tools/filecheck.py`): Pattern matching tool for AST validation
  - Supports `CHECK`, `CHECK-NEXT`, `CHECK-NOT`, `CHECK-SAME` patterns
  - Regex and literal string matching
  - Whitespace-aware matching options

- **zomc**: ZomLang compiler with testing support
  - `--dump-ast`: Generate JSON AST output for validation
  - `--dump-tokens`: Generate token stream for lexer testing
  - `--check-syntax`: Syntax-only compilation for parser testing

### CMake Utilities

The test suite provides several utility functions in `test-utils.cmake`:

- `add_language_test(name source_file)` - Add a language specification test
- `add_language_test_with_output(name source_file expected_file)` - Add test with output comparison
- `add_regression_test(name source_file issue_number)` - Add a regression test
- `add_performance_test(name executable)` - Add a performance test
- `add_language_tests_from_directory(directory)` - Auto-discover language tests

### Test Runners

- **CTest Integration**: All tests are integrated with CMake's CTest for unified execution
- **lit Runner**: Direct execution of lit tests with pattern matching
- **Custom Test Discovery**: Automatic discovery of test files based on naming conventions
- **Parallel Execution**: Tests can be run in parallel for faster execution

### Debugging Tools

- **Verbose Output**: Use `-v` flag with CTest or lit for detailed test output
- **Test Filtering**: Run specific tests or test categories using regex patterns
- **Failure Analysis**: Detailed failure reports with context and expected vs actual results
- **Manual FileCheck**: Run FileCheck manually for debugging pattern matching

## Test Dependencies

The test suite requires the following dependencies:

### Build Dependencies

- **CMake 3.20+**: Build system and test runner
- **Clang 15+**: C++ compiler with C++20 support
- **Ninja**: Build system backend (recommended)

### Runtime Dependencies

- **zc Library**: Core utilities and ztest framework
- **ZomLang Compiler (zomc)**: The compiler being tested
- **Python 3.9+**: Required for lit test runner and FileCheck

### Python Dependencies

```bash
# Install lit test runner
pip3 install lit

# Verify installation
lit --version
```

### Test Framework Dependencies

- **ztest**: Built-in unit testing framework (part of zc library)
- **lit**: LLVM Integrated Tester for AST validation
- **FileCheck**: Custom Python implementation for pattern matching

### Optional Dependencies

- **LLVM FileCheck**: Alternative pattern matching tool (if available)
- **Valgrind**: Memory error detection (Linux only)
- **AddressSanitizer**: Memory error detection (built-in)
- **Coverage Tools**: For code coverage analysis

### Environment Setup

```bash
# Add lit to PATH (if installed with --user)
export PATH="$PATH:/Users/$(whoami)/Library/Python/3.9/bin"

# Verify all tools are available
which lit
which python3
./build-sanitizer/products/zomlang/utils/zomc/zomc --version
```

## Coverage

When using the coverage preset, test coverage information is automatically collected for all tests. To generate coverage reports:

```bash
# Configure and build with coverage preset
cmake --preset coverage
cmake --build --preset coverage

# Run tests with coverage collection
ctest --preset coverageTests

# Generate coverage reports using CMake targets
cmake --build --preset coverage --target coverage  # Generate full coverage report (HTML + text)
# Coverage reports will be generated in build-coverage/coverage/ directory
# - HTML report: build-coverage/coverage/html/
# - Text report: build-coverage/coverage/coverage.txt
```

## Continuous Integration

All tests are automatically run in CI/CD pipelines. The test suite is designed to:

- Run quickly for fast feedback
- Provide clear error messages
- Be deterministic and reliable
- Scale with the project size

## Best Practices

1. **Test Naming** - Use descriptive names that explain what is being tested
2. **Test Organization** - Group related tests in appropriate directories
3. **Test Independence** - Each test should be independent and not rely on others
4. **Error Messages** - Provide clear, actionable error messages
5. **Performance** - Keep tests fast; use performance tests for benchmarks
6. **Documentation** - Document complex test scenarios and edge cases
7. **Maintenance** - Regularly review and update tests as the compiler evolves
