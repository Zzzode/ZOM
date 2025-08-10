# Types

Zom features a rich, static type system that provides safety guarantees while maintaining expressiveness and performance.

## Type System Overview

The Zom type system is:

- **Static**: All types are known at compile time
- **Strong**: No implicit conversions between incompatible types
- **Inferred**: Types can often be inferred from context
- **Nominal**: Types are distinguished by name, not just structure
- **Generic**: Supports parametric polymorphism

## Predefined Types

### Integer Types

| Type | Size | Range | Description |
|------|------|-------|-------------|
| `i8` | 8 bits | -128 to 127 | Signed 8-bit integer |
| `i32` | 32 bits | -2³¹ to 2³¹-1 | Signed 32-bit integer |
| `i64` | 64 bits | -2⁶³ to 2⁶³-1 | Signed 64-bit integer |
| `u8` | 8 bits | 0 to 255 | Unsigned 8-bit integer |
| `u16` | 16 bits | 0 to 65,535 | Unsigned 16-bit integer |
| `u32` | 32 bits | 0 to 2³²-1 | Unsigned 32-bit integer |
| `u64` | 64 bits | 0 to 2⁶⁴-1 | Unsigned 64-bit integer |

```zom
let byte: u8 = 255;
let count: i32 = -42;
let bigNumber: u64 = 18_446_744_073_709_551_615;
```

### Floating-Point Types

| Type | Size | Precision | Description |
|------|------|-----------|-------------|
| `f32` | 32 bits | ~7 decimal digits | Single-precision float |
| `f64` | 64 bits | ~15 decimal digits | Double-precision float |

```zom
let pi: f32 = 3.14159;
let precise: f64 = 3.141592653589793;
let scientific: f64 = 6.022e23;
```

### Boolean Type

```zom
let isValid: bool = true;
let isComplete: bool = false;
```

### String Type

```zom
let message: str = "Hello, Zom!";
let empty: str = "";
let multiline: str = "Line 1\nLine 2";
```

### Special Types

- **`null`**: The type of the `null` value, representing absence
- **`unit`**: The type `()`, used for functions that don't return a value
- **`never`**: The bottom type, for functions that never return
- **`any`**: The top type, can hold any value (use sparingly)

```zom
let nothing: null = null;
let empty: unit = ();
fun crash() -> never {
    throw "This function never returns";
}
```

## Type Expressions

### Parenthesized Types

Types can be parenthesized for clarity:

```zom
let x: (i32) = 42;
let complex: ((i32, str) -> bool) = someFunction;
```

### Union Types

Union types represent values that can be one of several types:

```zom
type StringOrNumber = str | i32;
type Result = Success | Error;

let value: StringOrNumber = "hello";
value = 42; // Also valid

fun process(input: str | i32 | bool) {
    match (input) {
        when str { print("String: " + input); }
        when i32 { print("Number: " + input.toString()); }
        when bool { print("Boolean: " + input.toString()); }
    }
}
```

### Intersection Types

Intersection types represent values that satisfy multiple type constraints:

```zom
interface Named {
    name: str;
}

interface Aged {
    age: i32;
}

type Person = Named & Aged;

let person: Person = {
    name: "Alice",
    age: 30
};
```

### Optional Types

Optional types represent values that may or may not exist:

```zom
let maybeNumber: i32? = 42;
let nothing: str? = null;

// Optional chaining
let length = maybeString?.length;

// Null coalescing
let defaultValue = maybeNumber ?? 0;
```

### Array Types

Array types represent ordered collections of elements:

```zom
let numbers: i32[] = [1, 2, 3, 4, 5];
let strings: str[] = ["hello", "world"];
let matrix: i32[][] = [[1, 2], [3, 4]];

// Array operations
let first = numbers[0];
let length = numbers.length;
numbers.push(6);
```

### Tuple Types

Tuple types represent fixed-size, ordered collections with potentially different element types:

```zom
// Anonymous tuple
let point: (f64, f64) = (3.0, 4.0);
let person: (str, i32, bool) = ("Alice", 30, true);

// Named tuple elements
let namedPoint: (x: f64, y: f64) = (x: 3.0, y: 4.0);
let coordinate = namedPoint.x; // Access by name

// Destructuring
let (name, age, isActive) = person;
let (x, y) = point;
```

### Function Types

Function types describe the signature of functions:

```zom
// Basic function type
type BinaryOp = (i32, i32) -> i32;

// Function with no parameters
type Supplier<T> = () -> T;

// Function with no return value
type Consumer<T> = (T) -> unit;

// Higher-order function
type Mapper<T, U> = (T -> U, T[]) -> U[];

// Function with error handling
type SafeParser = (str) -> i32 raises ParseError;

// Examples
let add: BinaryOp = (a, b) => a + b;
let getString: Supplier<str> = () => "hello";
let print: Consumer<str> = (s) => console.log(s);
```

### Object Types

Object types define the structure of objects:

```zom
// Anonymous object type
let point: { x: f64, y: f64 } = { x: 3.0, y: 4.0 };

// Object type with methods
type Calculator = {
    value: f64,
    add: (f64) -> unit,
    multiply: (f64) -> unit,
    result: () -> f64
};

// Optional properties
type Config = {
    host: str,
    port: i32,
    ssl?: bool,  // Optional property
    timeout?: i32
};
```

## Type Queries

Type queries extract type information from values:

```zom
let value = 42;
type ValueType = typeof value; // i32

let obj = { name: "Alice", age: 30 };
type ObjectType = typeof obj; // { name: str, age: i32 }

// Keyof operator
type PersonKeys = keyof { name: str, age: i32 }; // "name" | "age"
```

## Type Annotations

Type annotations explicitly specify types:

```zom
// Variable annotations
let count: i32 = 0;
let name: str = "Alice";

// Function parameter and return type annotations
fun greet(name: str): str {
    return "Hello, " + name;
}

// Complex type annotations
let callback: (str) -> bool = (s) => s.length > 0;
let data: { id: i32, values: f64[] } = {
    id: 1,
    values: [1.0, 2.0, 3.0]
};
```
