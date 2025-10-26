# AST Cast System Usage Guide

## Overview

The AST cast system provides type-safe casting utilities for AST nodes, similar to LLVM's cast system. It uses modern C++20 concepts to distinguish between concrete nodes (with specific `SyntaxKind` values) and interface nodes (abstract base classes with `classof` methods).

### Key Features

- **Type Safety**: All casts are checked at runtime using `SyntaxKind` or `classof` methods
- **C++20 Concepts**: Uses `ConcreteASTNode` and `InterfaceASTNode` concepts for compile-time type checking
- **Unified API**: Single set of functions (`isa`, `cast`, `dyn_cast`) works with both concrete and interface types
- **X-Macro Integration**: Automatically generates type traits for all AST nodes defined in `ast-nodes.def`
- **LLVM-Style**: Familiar API for developers coming from LLVM codebase

## Key Functions

### `isa<T>(node)`

Check if a node is of a specific type without performing the cast.

**When to use:**

- Before performing operations that require a specific node type
- In conditional logic to branch based on node type
- When you only need to know the type, not access the casted object

**Examples:**

```cpp
// Check concrete node type
if (isa<FunctionDeclaration>(node)) {
    // node is specifically a FunctionDeclaration
}

// Check interface node type
if (isa<Expression>(node)) {
    // node is any kind of Expression (CallExpression, ArrayLiteralExpression, etc.)
}

// Check multiple types
if (isa<VariableDeclaration>(node) || isa<FunctionDeclaration>(node)) {
    // Handle both variable and function declarations
}

// Check for literal types
if (isa<StringLiteral>(node) || isa<IntegerLiteral>(node)) {
    // Handle literal expressions
}
```

### `cast<T>(node)`

Perform a checked cast when you're certain it will succeed.

**When to use:**

- After an `isa<T>()` check that returned true
- When you know the node type from context (e.g., visitor pattern)
- In performance-critical code where failure is not expected

**Behavior:**

- Throws `zc::Exception` if the cast fails
- Returns a reference to the casted type

**Examples:**

```cpp
// After isa check
if (isa<FunctionDeclaration>(node)) {
    auto& funcDecl = cast<FunctionDeclaration>(node);
    // Use funcDecl safely - access function-specific methods
    auto& name = funcDecl.getName();
}

// In visitor methods (type is guaranteed)
void visit(const Node& node) {
    if (node.getKind() == SyntaxKind::CallExpression) {
        auto& callExpr = cast<CallExpression>(node);
        // Process call expression
        auto& callee = callExpr.getCallee();
    }
}

// Cast to interface type
auto& expr = cast<Expression>(node);  // Works if node is any Expression
auto& stmt = cast<Statement>(node);   // Works if node is any Statement

// Cast literal types
if (isa<StringLiteral>(node)) {
    auto& strLit = cast<StringLiteral>(node);
    auto value = strLit.getValue();
}
```

### `dyn_cast<T>(node)`

Safely attempt a cast that might fail.

**When to use:**

- When the cast might fail and you want to handle it gracefully
- In generic code that processes multiple node types
- When you prefer explicit error handling over exceptions

**Behavior:**

- Returns `zc::Maybe<T&>` - contains the casted reference if successful, `zc::none` if failed
- Never throws exceptions

**Examples:**

```cpp
// Safe casting with explicit checking
if (auto funcDecl = dyn_cast<FunctionDeclaration>(node)) {
    // Use funcDecl.value() - cast succeeded
    processFunction(funcDecl.value());
} else {
    // Cast failed, handle appropriately
    handleNonFunction(node);
}

// Chain multiple attempts
if (auto varDecl = dyn_cast<VariableDeclaration>(node)) {
    processVariable(varDecl.value());
} else if (auto funcDecl = dyn_cast<FunctionDeclaration>(node)) {
    processFunction(funcDecl.value());
} else if (auto classDecl = dyn_cast<ClassDeclaration>(node)) {
    processClass(classDecl.value());
} else {
    processOtherDeclaration(node);
}

// Use with Maybe operations
auto maybeExpr = dyn_cast<Expression>(node);
maybeExpr.map([](const Expression& expr) {
    // Process expression if cast succeeded
    return expr.getType();
});

// Handle different literal types
if (auto strLit = dyn_cast<StringLiteral>(node)) {
    processString(strLit.value().getValue());
} else if (auto intLit = dyn_cast<IntegerLiteral>(node)) {
    processInteger(intLit.value().getValue());
} else if (auto boolLit = dyn_cast<BooleanLiteral>(node)) {
    processBoolean(boolLit.value().getValue());
}
```

## Node Type Categories

### Concrete Nodes (Element Nodes)

These have specific `SyntaxKind` values and represent actual AST node implementations. They are defined in `ast-nodes.def` using the `AST_ELEMENT_NODE` macro:

#### Module Nodes

- `SourceFile` - Root of the AST tree
- `Module` - Module definition
- `ImportDeclaration` - Import statements
- `ExportDeclaration` - Export statements

#### Declaration Nodes

- `FunctionDeclaration` - Function definitions
- `VariableDeclaration` - Variable declarations
- `ClassDeclaration` - Class definitions
- `InterfaceDeclaration` - Interface definitions
- `StructDeclaration` - Struct definitions
- `EnumDeclaration` - Enum definitions
- `ParameterDeclaration` - Function parameters
- `PropertyDeclaration` - Class/struct properties

#### Statement Nodes

- `BlockStatement` - Block of statements
- `IfStatement` - Conditional statements
- `WhileStatement` - While loops
- `ForStatement` - For loops
- `ReturnStatement` - Return statements
- `ExpressionStatement` - Expression as statement
- `MatchStatement` - Pattern matching

#### Expression Nodes

- `CallExpression` - Function calls
- `ArrayLiteralExpression` - Array literals
- `ObjectLiteralExpression` - Object literals
- `PropertyAccessExpression` - Member access (obj.prop)
- `ElementAccessExpression` - Index access (obj[index])
- `ConditionalExpression` - Ternary operator
- `ParenthesizedExpression` - Parenthesized expressions

#### Literal Nodes

- `StringLiteral` - String constants
- `IntegerLiteral` - Integer constants
- `FloatLiteral` - Floating-point constants
- `BooleanLiteral` - Boolean constants
- `NullLiteral` - Null values

#### Type Nodes

- `TypeReferenceNode` - Type references
- `ArrayTypeNode` - Array types
- `UnionTypeNode` - Union types
- `FunctionTypeNode` - Function types
- `PredefinedTypeNode` - Built-in types
- Primitive type nodes: `BoolTypeNode`, `I32TypeNode`, `F64TypeNode`, `StrTypeNode`, etc.

#### Pattern Nodes

- `IdentifierPattern` - Variable binding patterns
- `TuplePattern` - Tuple destructuring
- `ArrayPattern` - Array destructuring
- `WildcardPattern` - Wildcard patterns

### Interface Nodes (Abstract Base Classes)

These are abstract base classes that group related concrete nodes. They use `classof` methods for type checking and are defined using the `AST_INTERFACE_NODE` macro:

- `Node` - Base class for all AST nodes
- `Statement` - Base for all statement types
- `Expression` - Base for all expression types
- `Declaration` - Base for all declaration types
- `NamedDeclaration` - Base for declarations with names
- `TypeNode` - Base for all type nodes
- `UnaryExpression` - Base for unary expressions
- `LiteralExpression` - Base for literal expressions
- `Pattern` - Base for pattern matching constructs

## Best Practices

### 1. Prefer `isa` + `cast` for Performance-Critical Code

```cpp
// Good: Single type check, guaranteed cast
if (isa<CallExpression>(node)) {
    auto& callExpr = cast<CallExpression>(node);
    // Process call expression
    processCall(callExpr);
}
```

### 2. Use `dyn_cast` for Uncertain Casts

```cpp
// Good: Handle potential failure gracefully
if (auto expr = dyn_cast<Expression>(node)) {
    processExpression(expr.value());
}
```

### 3. Cast to Most Specific Type When Possible

```cpp
// Better: Cast to specific type
if (auto funcDecl = dyn_cast<FunctionDeclaration>(node)) {
    // Access function-specific methods
}

// Less optimal: Cast to general interface
if (auto decl = dyn_cast<Declaration>(node)) {
    // Limited to general declaration methods
}
```

### 4. Avoid Redundant Checks

```cpp
// Bad: Redundant isa check
if (isa<Expression>(node)) {
    if (auto expr = dyn_cast<Expression>(node)) {
        // The dyn_cast is guaranteed to succeed
    }
}

// Good: Just use dyn_cast
if (auto expr = dyn_cast<Expression>(node)) {
    // Single check and cast
}
```

### 5. Handle Interface Hierarchies Correctly

```cpp
// All expressions are also nodes, so this works:
if (auto expr = dyn_cast<Expression>(node)) {
    // expr is both an Expression and a Node
    auto& asNode = cast<Node>(expr.value());  // Always succeeds
}
```

## C++20 Concepts Integration

The cast system leverages C++20 concepts to provide compile-time type safety and better error messages:

### Core Concepts

```cpp
// Concept for concrete AST nodes (have SyntaxKind)
template <typename T>
concept ConcreteASTNode = requires {
  typename SyntaxKindTrait<std::remove_reference_t<T>>;
  SyntaxKindTrait<std::remove_reference_t<T>>::value;
};

// Concept for interface AST nodes (have classof method)
template <typename T>
concept InterfaceASTNode = requires(const Node& n) {
  T::classof(n);
} && !ConcreteASTNode<T>;

// Unified concept for all valid AST node types
template <typename T>
concept ASTNode = ConcreteASTNode<T> || InterfaceASTNode<T>;
```

### Benefits

1. **Compile-time Validation**: Invalid types are caught at compile time
2. **Better Error Messages**: Clear error messages when using wrong types
3. **Template Constraints**: Functions only accept valid AST node types
4. **Automatic Dispatch**: Different implementations for concrete vs interface nodes

### Usage with Concepts

```cpp
// This will compile - FunctionDeclaration is a concrete node
if (isa<FunctionDeclaration>(node)) { /* ... */ }

// This will compile - Expression is an interface node
if (isa<Expression>(node)) { /* ... */ }

// This will NOT compile - int is not an AST node type
// if (isa<int>(node)) { /* Compile error! */ }
```

## Advanced Usage and Implementation Details

### SyntaxKindTrait System

The cast system uses `SyntaxKindTrait` to map concrete node types to their corresponding `SyntaxKind` values:

```cpp
// Automatically generated for each concrete node type
template <>
struct SyntaxKindTrait<FunctionDeclaration> {
  static constexpr SyntaxKind value = SyntaxKind::FunctionDeclaration;
};

// Also works with references and const references
template <>
struct SyntaxKindTrait<const FunctionDeclaration&> {
  static constexpr SyntaxKind value = SyntaxKind::FunctionDeclaration;
};
```

### X-Macro Integration

The system uses X-macros to automatically generate code for all node types defined in `ast-nodes.def`:

```cpp
// Forward declarations
#define AST_ELEMENT_NODE(ClassName) class ClassName;
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_ELEMENT_NODE

// SyntaxKindTrait specializations
#define AST_ELEMENT_NODE(ClassName) \
  template <> struct SyntaxKindTrait<ClassName> { \
    static constexpr SyntaxKind value = SyntaxKind::ClassName; \
  };
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_ELEMENT_NODE
```

### Performance Considerations

1. **Runtime Checks**: All casts perform runtime type checking
2. **Branch Prediction**: Use `isa` + `cast` pattern for better branch prediction
3. **Exception Overhead**: `cast` throws on failure, `dyn_cast` returns `Maybe`
4. **Template Instantiation**: Concepts reduce template instantiation errors

### Error Handling

```cpp
// cast() throws zc::Exception on failure
try {
    auto& funcDecl = cast<FunctionDeclaration>(node);
    // Use funcDecl
} catch (const zc::Exception& e) {
    // Handle cast failure
}

// dyn_cast() returns Maybe for graceful error handling
if (auto funcDecl = dyn_cast<FunctionDeclaration>(node)) {
    // Success path
} else {
    // Failure path - no exception thrown
}
```

### Working with NodeList

The cast system works seamlessly with `NodeList` containers:

```cpp
void processStatements(const NodeList<Statement>& statements) {
    for (const auto& stmt : statements) {
        if (auto ifStmt = dyn_cast<IfStatement>(stmt)) {
            processIfStatement(ifStmt.value());
        } else if (auto blockStmt = dyn_cast<BlockStatement>(stmt)) {
            processBlockStatement(blockStmt.value());
        } else if (auto exprStmt = dyn_cast<ExpressionStatement>(stmt)) {
            processExpressionStatement(exprStmt.value());
        }
    }
}
```

### Common Patterns

#### Visitor Pattern Integration

```cpp
class MyVisitor : public Visitor {
public:
    void visit(const Node& node) override {
        // Use cast system to dispatch to specific handlers
        if (auto funcDecl = dyn_cast<FunctionDeclaration>(node)) {
            handleFunction(funcDecl.value());
        } else if (auto classDecl = dyn_cast<ClassDeclaration>(node)) {
            handleClass(classDecl.value());
        }
        // ... handle other node types
    }
};
```

#### Type-Safe Downcasting

```cpp
// Safe downcast from interface to concrete type
void processExpression(const Expression& expr) {
    if (auto callExpr = dyn_cast<CallExpression>(expr)) {
        // Handle function calls
        auto& callee = callExpr.value().getCallee();
    } else if (auto arrayLit = dyn_cast<ArrayLiteralExpression>(expr)) {
        // Handle array literals
        auto& elements = arrayLit.value().getElements();
    }
}
```

## Migration from Old System

If you're migrating from an older cast system, here are the key changes:

### Simplified API

**Old System (hypothetical):**

```cpp
// Multiple confusing functions with different behaviors
auto result = dyn_cast<Type>(node);         // Returns pointer
```

**New System:**

```cpp
// Single, consistent API
auto result = dyn_cast<Type>(node);         // Returns Maybe<T&>
auto& ref = cast<Type>(node);               // Returns T&, throws on failure
bool isType = isa<Type>(node);              // Returns bool
```

### Automatic Type Detection

The new system automatically detects whether a type is concrete or interface:

```cpp
// Both work with the same functions - no need to know the difference
isa<FunctionDeclaration>(node);  // Concrete node - uses SyntaxKind
isa<Expression>(node);           // Interface node - uses classof
```

### Better Error Messages

C++20 concepts provide clearer error messages:

```cpp
// Old: Cryptic template error messages
// New: Clear concept-based errors
template<typename T>
void processNode(const Node& node) requires ASTNode<T> {
    if (auto casted = dyn_cast<T>(node)) {
        // Process casted node
    }
}
```

## Troubleshooting

### Common Issues

1. **Compile Error: Type doesn't satisfy ASTNode concept**
   - Ensure the type is defined in `ast-nodes.def`
   - Check that interface nodes have `classof` methods

2. **Runtime Cast Failure**
   - Use `isa` to check before `cast`
   - Use `dyn_cast` for uncertain casts

3. **Missing SyntaxKind**
   - Ensure concrete nodes are listed in `AST_ELEMENT_NODE` in `ast-nodes.def`
   - Rebuild to regenerate `SyntaxKindTrait` specializations

### Debugging Tips

```cpp
// Debug node types
void debugNode(const Node& node) {
    std::cout << "Node kind: " << static_cast<int>(node.getKind()) << std::endl;

    // Check specific types
    if (isa<Expression>(node)) {
        std::cout << "Is Expression" << std::endl;
    }
    if (isa<Statement>(node)) {
        std::cout << "Is Statement" << std::endl;
    }
}
```

The new cast system provides a more robust, type-safe, and maintainable approach to AST node casting while maintaining familiar LLVM-style semantics.
