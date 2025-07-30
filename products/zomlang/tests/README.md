# ZomLang Compiler Test Suite

This directory contains the comprehensive test suite for the ZomLang compiler. The tests are organized into several categories to ensure thorough coverage of all compiler components and language features.

## Test Structure

### 1. Unit Tests (`unittests/`)

Low-level component tests that verify individual compiler modules work correctly in isolation.

- **`unittests/compiler/`** - Tests for compiler components
  - `basic/` - Basic compiler infrastructure tests
  - `source/` - Source manager and location tests
  - `lexer/` - Lexical analysis tests
  - `parser/` - Syntax analysis tests
  - `ast/` - AST node and manipulation tests
  - `diagnostics/` - Error reporting tests
  - `checker/` - Semantic analysis tests

### 2. Language Tests (`language/`)

Tests that verify the compiler correctly handles ZomLang language constructs according to the language specification.

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

```bash
ctest -R "unit-basic-thread-pool-test"  # Run specific unit test
ctest -R "language-"                     # Run all language tests
ctest -R "regression-issue-001"          # Run specific regression test
```

## Adding New Tests

### Unit Tests

1. Create a new `.cc` file in the appropriate `unittests/` subdirectory
2. Follow the naming convention: `*-test.cc`
3. Use the `ZC_TEST` macro for test cases
4. The test will be automatically discovered and added

### Language Tests

1. Create a `.zom` file in the appropriate `language/` subdirectory
2. Optionally create a `.expected` file with expected output
3. The test will be automatically discovered and added



### Regression Tests

1. Create a directory named `issue-XXX/` in `regression/`
2. Add `.zom` test files to the directory
3. Optionally add `.expected` files for output comparison
4. Tests will be automatically discovered

## Test Utilities

The test suite provides several utility functions in `test-utils.cmake`:

- `add_language_test(name source_file)` - Add a language specification test
- `add_language_test_with_output(name source_file expected_file)` - Add test with output comparison
- `add_regression_test(name source_file issue_number)` - Add a regression test
- `add_performance_test(name executable)` - Add a performance test
- `add_language_tests_from_directory(directory)` - Auto-discover language tests

## Test Dependencies

Tests depend on:

- **zomc** - The ZomLang compiler executable
- **ztest** - The testing framework library
- **frontend** - Compiler frontend library
- Various compiler component libraries

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
