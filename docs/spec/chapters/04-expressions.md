# Expressions

Expressions are constructs that evaluate to values. Zom provides a rich set of expression types for various programming needs.

## Expression Categories

1. **Primary Expressions**: Basic building blocks
2. **Postfix Expressions**: Member access, function calls, subscripting
3. **Prefix Expressions**: Unary operators
4. **Binary Expressions**: Arithmetic, logical, comparison operators
5. **Conditional Expressions**: Ternary operator
6. **Assignment Expressions**: Value assignment
7. **Closure Expressions**: Anonymous functions

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

Refers to the current instance in class methods:

```zom
class Point {
    let x: f64;
    let y: f64;

    fun distanceFromOrigin() -> f64 {
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

```zom
// Empty object
let empty = {};

// Object with properties
let person = {
    name: "Alice",
    age: 30,
    isActive: true
};

// Computed property names
let key = "dynamicKey";
let obj = {
    [key]: "value",
    ["computed" + "Key"]: 42
};

// Property shorthand
let name = "Bob";
let age = 25;
let shorthand = { name, age }; // Same as { name: name, age: age }

// Object with methods
let calculator = {
    value: 0,
    add(x: i32) {
        this.value += x;
    },
    get result() {
        return this.value;
    }
};

// Spread properties
let base = { a: 1, b: 2 };
let extended = { ...base, c: 3 }; // { a: 1, b: 2, c: 3 }
```

## Postfix Expressions

### Member Access

```zom
// Dot notation
let length = myString.length;
let method = myObject.doSomething();

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

### Type Operators

```zom
// typeof operator
let typeString = typeof myVariable;

// Type casting
let casted = <i32>someValue;
let safeCast = someValue as i32;
let optionalCast = someValue as? i32; // Returns null if cast fails
```

### Await Expression

Used in async functions to wait for asynchronous operations:

```zom
async fun fetchData() -> str {
    let response = await httpClient.get("https://api.example.com/data");
    let data = await response.json();
    return data.message;
}
```

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
let strictEqual = a === b; // Strict equality (type and value)
let strictNotEqual = a !== b; // Strict inequality
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
let value = optionalValue!!;      // Force unwrap (panics if null)
let fallback = riskyOperation()?: defaultValue;  // Use default on error
let handled = match (riskyOperation()) {
    when Ok(v) => v,
    when Err(e) => handleError(e)
};
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

### Range Operators

```zom
let closedRange = 1...10;    // Closed range [1, 10]
let halfOpenRange = 1..<10;  // Half-open range [1, 10)
```

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
let x = 42;
y = x + 10;
```

### Compound Assignment

```zom
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

## Closure Expressions

Closure expressions create anonymous functions:

```zom
// Basic closure
let add = (a: i32, b: i32) => a + b;

// Closure with block body
let complexOperation = (x: i32) => {
    let doubled = x * 2;
    let squared = doubled * doubled;
    return squared;
};

// Closure with inferred types
let numbers = [1, 2, 3, 4, 5];
let doubled = numbers.map(x => x * 2);
let filtered = numbers.filter(x => x > 2);

// Closure capturing variables
let multiplier = 3;
let multiply = (x: i32) => x * multiplier;

// Async closure
let asyncOperation = async (url: str) => {
    let response = await fetch(url);
    return await response.text();
};
```

## Operator Precedence

Operators are evaluated in the following order (highest to lowest precedence):

1. **Primary**: `()`, `[]`, `.`, `?.`
2. **Postfix**: `++`, `--` (postfix)
3. **Prefix**: `+`, `-`, `!`, `~`, `++`, `--` (prefix), `typeof`, `await`
4. **Cast**: `as`, `as?`, `<Type>`
5. **Exponentiation**: `**`
6. **Multiplicative**: `*`, `/`, `%`
7. **Additive**: `+`, `-`
8. **Shift**: `<<`, `>>`, `>>>`
9. **Relational**: `<`, `>`, `<=`, `>=`, `is`, `in`, `instanceof`
10. **Equality**: `==`, `!=`, `===`, `!==`
11. **Bitwise AND**: `&`
12. **Bitwise XOR**: `^`
13. **Bitwise OR**: `|`
14. **Logical AND**: `&&`
15. **Logical OR**: `||`
16. **Null Coalescing**: `??`
17. **Error Handling**: `?!`, `!!`, `?:`
18. **Conditional**: `? :`
19. **Assignment**: `=`, `+=`, `-=`, etc.
