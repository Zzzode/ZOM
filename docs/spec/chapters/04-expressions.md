# Expressions

Expressions are constructs that evaluate to values. Zom provides a rich set of expression types for various programming needs.

## Expression Categories

1. **Primary Expressions**: Basic building blocks
2. **Postfix Expressions**: Member access, function calls, subscripting
3. **Prefix Expressions**: Unary operators
4. **Binary Expressions**: Arithmetic, logical, comparison operators
5. **Conditional Expressions**: Ternary operator
6. **Assignment Expressions**: Value assignment
7. **Function Expressions**: Anonymous functions

## Primary Expressions

### Literal Expressions

```zom
42              // Integer literal
3.14            // Floating-point literal
"hello"         // String literal
true            // Boolean literal
null            // Null literal
```

### Identifier Expressions

```zom
myVariable      // Variable reference
MyClass         // Type reference
SOME_CONSTANT   // Constant reference
```

### `this` Expression

`this` resolves only inside a callable that declares an explicit `this`
receiver parameter. It names that receiver. A nested closure may use the
receiver only through the capture rules in this chapter; a named function
without its own receiver does not inherit one.

```zom
class Point {
    let x: f64;
    let y: f64;

    fun distanceFromOrigin(this) -> f64 {
        return sqrt(this.x * this.x + this.y * this.y);
    }
}
```

### Parenthesized Expressions

```zom
let result = (a + b) * c;
let complex = ((x * y) + z) / (a - b);
```

### Array Literals

```zom
// Empty array
let empty: i32[] = [];

// Array with elements
let numbers = [1, 2, 3, 4, 5];
let mixed = ["hello", "world", "!"];

// Nested arrays
let matrix = [
    [1, 2, 3],
    [4, 5, 6],
    [7, 8, 9]
];

// Array with spread elements
let base = [1, 2, 3];
let extended = [0, ...base, 4, 5]; // [0, 1, 2, 3, 4, 5]
```

### Object Literals

Object literals create anonymous record values. Property names must be identifiers; computed keys and method shorthand syntax are not valid.

```zom
// Empty object
let empty = {};

// Object with properties
let person = {
    name: "Alice",
    age: 30,
    isActive: true
};

// Property shorthand
let name = "Bob";
let age = 25;
let shorthand = { name, age }; // Same as { name: name, age: age }

// Object with function-valued properties (not method syntax)
let calculator = {
    value: 0,
    add: fun(current: i32, x: i32) -> i32 { return current + x; },
    result: fun(current: i32) -> i32 { return current; }
};

// Spread properties
let base = { a: 1, b: 2 };
let extended = { ...base, c: 3 }; // { a: 1, b: 2, c: 3 }
```

> **Note:** Object literals in ZOM are pure records. Method shorthand (`{ m() {} }`) and computed keys (`{ [expr]: v }`) are not valid syntax. Function-valued properties do not synthesize a receiver. To attach receiver-based behavior, define a callable with an explicit `this` receiver in a class body or `impl` block.

### Struct Literals

Nominal struct literals use a qualified type path followed by a field list.
Field entries use either `name: expression` or shorthand `name`.

```zom
struct Point { x: i32, y: i32 }

let x = 1;
let p = Point { x, y: 2 };
let origin = geometry.Point { x: 0, y: 0 };
```

## Postfix Expressions

### Member Access

A member-bearing `.`, `?.`, or `::` suffix is followed by a
`DeclaredDefinitionName`. This name domain contains identifiers and the five
declaration names `init`, `deinit`, `get`, `set`, and `this`.

```zom
// Dot notation
let length = myString.length;
let method = myObject.doSomething();
super.init();

// Bracket notation
let element = myArray[0];
let property = myObject["propertyName"];
let computed = myObject[computedKey];
```

### Optional Chaining

Safely access nested properties that might be null:

```zom
let user: User? = getUser();
let street = user?.address?.street;
let upperName = user?.name?.toUpperCase();

// Method calls with optional chaining
let result = user?.calculateSomething?.(param1, param2);

// Array access with optional chaining
let firstItem = user?.items?.[0];
```

### Function Calls

```zom
// Basic function call
let result = add(5, 3);

// Method call
let length = myString.length();

// Function call with named arguments
let point = createPoint(x: 10, y: 20);

// Function call with spread arguments
let numbers = [1, 2, 3];
let sum = add(...numbers);

// Generic function call
let parsed = parse<i32>("42");
```

### Subscript Expressions

```zom
// Array subscripting
let first = array[0];
let last = array[array.length - 1];

// Dictionary subscripting
let value = dictionary["key"];

// Multi-dimensional subscripting
let element = matrix[row][column];
```

## Prefix Expressions

### Unary Arithmetic Operators

```zom
let positive = +42;     // Unary plus
let negative = -42;     // Unary minus
let incremented = ++x;  // Pre-increment
let decremented = --y;  // Pre-decrement
```

### Logical NOT Operator

```zom
let isNotValid = !isValid;
let isEmpty = !array.length;
```

### Bitwise NOT Operator

```zom
let inverted = ~0b1010; // Results in ...11110101
```

### Reference and Dereference Operators

The `&` operator creates a reference to a value, and the `*` operator dereferences a pointer or reference.

```zom
let value = 42;
let ref: &i32 = &value;      // Create immutable reference
let deref: i32 = *ref;       // Dereference reference (always safe)

mut mvalue = 100;
let mref: &mut i32 = &mut mvalue;  // Create mutable reference
*mref = 200;                       // Write through mutable reference (always safe)

// Raw pointers require unsafe to dereference
let ptr: *const i32 = &value;      // Create raw pointer (safe)
// let val = *ptr;                   // ❌ ZOM0901 RawPointerDerefOutsideUnsafe
let val = unsafe { *ptr };          // Dereference raw pointer (requires unsafe)
```

| Operation | Syntax | Requires `unsafe`? |
|-----------|--------|-------------------|
| Create reference | `&value` / `&mut value` | ❌ No |
| Create raw pointer | `&value as *const T` | ❌ No |
| Dereference `&T` / `&mut T` | `*ref` | ❌ No |
| Dereference `*const T` / `*mut T` | `*raw_ptr` | ✅ Yes |

### Type Operators

```zom
// typeof operator
let typeString = typeof myVariable;

// Type casting with as operators
let intVal: i32 = 42;
let wide: i64 = intVal as i64;        // Guaranteed widening cast
let narrow: i8? = intVal as? i8;      // Optional narrowing cast
let forced: i8 = intVal as! i8;       // Forced narrowing cast; panics on failure
```

See Ch.03 §Type Casting and Conversion for the complete semantics of `as`,
`as?`, and `as!`.

## Binary Expressions

### Arithmetic Operators

```zom
let sum = a + b;           // Addition
let difference = a - b;    // Subtraction
let product = a * b;       // Multiplication
let quotient = a / b;      // Division
let remainder = a % b;     // Modulo
let power = a ** b;        // Exponentiation
```

### Comparison Operators

```zom
let equal = a == b;        // Equality
let notEqual = a != b;     // Inequality
let sameReference = a === b; // Reference equality
let differentReference = a !== b; // Reference inequality
let less = a < b;          // Less than
let greater = a > b;       // Greater than
let lessEqual = a <= b;    // Less than or equal
let greaterEqual = a >= b; // Greater than or equal
```

### Logical Operators

```zom
let and = a && b;          // Logical AND (short-circuit)
let or = a || b;           // Logical OR (short-circuit)
let nullCoalesce = a ?? b; // Null coalescing
```

### Error Handling Operators

Zom's error handling uses explicit control flow (no `try/catch`). Use these operators or pattern matching:

```zom
let result = riskyOperation()?!;  // Propagate error
let value = errorUnion!!;         // Select the success alternative
let fallback = riskyOperation()?: defaultValue;  // Use default on error
match (riskyOperation()) {
    when Ok(v) => { handleSuccess(v); }
    when Err(e) => { handleError(e); }
}
```

### Bitwise Operators

```zom
let bitwiseAnd = a & b;    // Bitwise AND
let bitwiseOr = a | b;     // Bitwise OR
let bitwiseXor = a ^ b;    // Bitwise XOR
let leftShift = a << b;    // Left shift
let rightShift = a >> b;   // Right shift (sign-extending)
let unsignedRightShift = a >>> b; // Unsigned right shift
```

### Type Check Operators

```zom
let isString = value is str;           // Type check
let hasProperty = "length" in object;  // Property existence check
let isInstance = obj instanceof MyClass; // Instance check
```

### Range Syntax Is Not Part Of V1

ZOM v1 does not define range-expression syntax. Sequence ranges are library
values constructed through ordinary functions and methods. The parser rejects
`a .. b`, `a ..< b`, and `a ... b` in expression position; the `...` token is
reserved for spread and rest syntax.

## Conditional Expressions

The ternary conditional operator provides a concise way to choose between two values:

```zom
let result = condition ? valueIfTrue : valueIfFalse;
let max = a > b ? a : b;
let status = isLoggedIn ? "Welcome" : "Please log in";

// Nested conditionals
let grade = score >= 90 ? "A" : score >= 80 ? "B" : score >= 70 ? "C" : "F";
```

## Assignment Expressions

### Simple Assignment

```zom
mut x = 42;
x = x + 10;
```

### Compound Assignment

```zom
mut x = 1;
mut y = 10;
mut z = 4;
mut w = 16;
mut a = 5;
mut b = 2;
mut flags = 0b1010;
mut value = 1;
mut result = true;

x += 5;    // Equivalent to: x = x + 5
y -= 3;    // Equivalent to: y = y - 3
z *= 2;    // Equivalent to: z = z * 2
w /= 4;    // Equivalent to: w = w / 4
a %= 3;    // Equivalent to: a = a % 3
b **= 2;   // Equivalent to: b = b ** 2

// Bitwise compound assignment
flags |= newFlag;   // Set flag
flags &= ~oldFlag;  // Clear flag
value <<= 1;        // Left shift
value >>= 1;        // Right shift

// Logical compound assignment
result &&= condition;  // Logical AND assignment
result ||= defaultValue; // Logical OR assignment
result ??= fallbackValue; // Null coalescing assignment
```

## Function Expressions

Function expressions create anonymous functions. A function expression has the
same parameter, generic parameter, return-type, and `raises` syntax as a
function declaration, but it has no binding identifier.

```text
FunctionExpression ::= 'fun' TypeParameters? OrdinaryParameterClause CaptureClause?
                       ReturnType? BlockStatement
CaptureClause ::= 'use' '[' CaptureList? ']'
CaptureList ::= CaptureElement (',' CaptureElement)* ','?
CaptureElement ::= Identifier | '&' Identifier | 'this'
```

The `use` token is contextual. It starts a capture clause only in the position
immediately after the parameter clause and before the optional return type. In
all other expression positions, `use` remains an ordinary identifier.
Function expressions and lambdas cannot declare a receiver parameter.

The capture clause is an explicit capture set:

- `name` captures the enclosing lexical binding by value.
- `&name` captures the enclosing lexical binding by reference.
- `this` captures the nearest enclosing explicitly declared receiver.

The `this` form is valid only when a receiver is in scope. `&this` is not valid;
receiver capture is always written as `this`. Capture entries name bindings in
an enclosing lexical scope. Parameters and declarations inside the function
expression body are not captures. Module-scope declarations and imported names
do not need capture entries.

When a capture clause is present, it is exhaustive. The function expression body
may refer to its parameters, declarations inside its body, module-scope names,
imports, and the listed captures. `use []` is therefore an explicit no-capture
function expression. When the capture clause is omitted, the semantic analyzer
infers the capture set from references to enclosing lexical bindings.

Capture lists preserve source order. A trailing comma is permitted. Empty
elements are not permitted.

Capture legality is checked after parsing. By-reference captures must not
outlive the referenced storage. Function expressions used as `spawn` bodies are
also checked by the spawn-boundary rules in Chapter 15; by-value captures must
satisfy `Sendable`, and by-reference captures require `Shared` plus a valid
lifetime.

```zom
// Basic function expression
let add = fun (a: i32, b: i32) -> i32 { return a + b; };

// Function expression with block body
let complexOperation = fun (x: i32) -> i32 {
    let doubled = x * 2;
    let squared = doubled * doubled;
    return squared;
};

// Function expression as an argument
let numbers = [1, 2, 3, 4, 5];
let doubled = numbers.map(fun (x: i32) -> i32 { return x * 2; });
let filtered = numbers.filter(fun (x: i32) -> bool { return x > 2; });

// Function expression capturing variables
let multiplier = 3;
let multiply = fun (x: i32) use [multiplier] -> i32 {
    return x * multiplier;
};

// Explicitly no captures
let identity = fun (x: i32) use [] -> i32 { return x; };
```

## Operator Precedence

Operators are evaluated in the following order (highest to lowest precedence):

1. **Primary**: `()`, `[]`, `.`, `?.`
2. **Postfix**: `++`, `--` (postfix), `?!` (try/propagate), `!!` (unwrap/panic)
3. **Prefix**: `+`, `-`, `!`, `~`, `*`, `&`, `++`, `--` (prefix), `typeof`
4. **Exponentiation**: `**`
5. **Multiplicative**: `*`, `/`, `%`
6. **Additive**: `+`, `-`
7. **Shift**: `<<`, `>>`, `>>>`
8. **Relational and cast**: `<`, `>`, `<=`, `>=`, `is`, `in`, `instanceof`,
   `as`, `as?`, `as!`
9. **Equality**: `==`, `!=`, `===`, `!==`
10. **Bitwise AND**: `&`
11. **Bitwise XOR**: `^`
12. **Bitwise OR**: `|`
13. **Logical AND**: `&&`
14. **Logical OR**: `||`
15. **Null Coalescing**: `??`
16. **Error Elvis**: `?:`
17. **Conditional**: `? :`
18. **Assignment**: `=`, `+=`, `-=`, etc.

## Postfix Error-Handling Operators: `?!` and `!!`

ZOM provides two built-in postfix operators for operands with a verified error-
union role fact. Both belong to the postfix-expression tier, associate from
left to right, and share the same precedence rank as postfix `++` and `--`. The
call/member/index chain is formed before these suffixes. A following cast or
infix operator therefore receives the result of the postfix error operator.

Canonical union alternative order never determines success or residual roles.
An ordinary union, a nominal `Result`, or any operand without a verified error-
union role fact is rejected by the operator's registered non-error-union
diagnostic. A malformed role fact is an internal semantic invariant rather than
a source diagnostic.

### `expr?!` - Propagate

- The operand must have a verified error-union role fact. An operand without one
  emits `ZOM4032 ErrorPropagateNonUnion`.
- The expression type is `shape.successType`.
- Every alternative in `shape.residualType` must be accepted by the containing function's
  explicit raises type.
- A missing or incompatible raises effect emits
  `ZOM4025 ErrorPropagateOutsideRaises`.

### `expr!!` - Forced Unwrap

- The operand must have a verified error-union role fact. An operand without one
  emits `ZOM4026 ErrorUnwrapNonUnion`.
- The expression type is `shape.successType`.
- The residual path is a panic edge. Chapter 11 defines the checked source
  contract and deliberately does not specify panic formatting, backtraces,
  unwind behavior, or target ABI.
