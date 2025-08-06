# Declarations

Declarations introduce new named entities into a program's namespace. They define variables, functions, types, classes, and other program constructs.

## Declaration Categories

1. **Variable Declarations**: `let`, `const`, `var`
2. **Function Declarations**: `fun`
3. **Type Declarations**: `alias`, `interface`, `struct`, `enum`, `error`
4. **Class Declarations**: `class`
5. **Module Declarations**: `module`, `namespace`

## Variable Declarations

### `let` Declarations

Declare mutable variables:

```zom
// Basic let declaration
let count = 0;
let name = "Alice";

// With explicit type annotation
let age: i32 = 25;
let height: f64 = 5.8;

// Multiple declarations
let x = 10, y = 20, z = 30;

// Uninitialized declaration (requires type annotation)
let result: str;
if (condition) {
    result = "success";
} else {
    result = "failure";
}

// Destructuring declaration
let (first, second) = getTuple();
let { name, age } = getPerson();
let [head, ...tail] = getArray();
```

### `const` Declarations

Declare immutable constants:

```zom
// Basic const declaration
const PI = 3.14159;
const MAX_SIZE = 1000;

// With explicit type
const GREETING: str = "Hello, World!";

// Complex constants
const CONFIG = {
    host: "localhost",
    port: 8080,
    ssl: false
};

// Computed constants
const AREA = PI * RADIUS * RADIUS;

// Destructuring const
const { width, height } = getDimensions();
const [r, g, b] = getColor();
```

### `var` Declarations

Declare variables with function scope (legacy, prefer `let`):

```zom
// Basic var declaration
var oldStyle = "legacy";

// Function-scoped (not block-scoped)
if (true) {
    var functionScoped = "visible outside block";
}
print(functionScoped); // This works (unlike let)
```

## Function Declarations

### Basic Function Declaration

```zom
// Simple function
fun greet(name: str) -> str {
    return "Hello, " + name + "!";
}

// Function with multiple parameters
fun add(a: i32, b: i32) -> i32 {
    return a + b;
}

// Function with no return value (unit type)
fun printMessage(message: str) {
    print(message);
}

// Function with no parameters
fun getCurrentTime() -> str {
    return Date.now().toString();
}
```

### Function Parameters

```zom
// Default parameter values
fun greet(name: str, greeting: str = "Hello") -> str {
    return greeting + ", " + name + "!";
}

// Optional parameters
fun createUser(name: str, email: str, age?: i32) {
    // age is of type i32?
    if (age != nil) {
        print("Age: " + age.toString());
    }
}

// Rest parameters
fun sum(...numbers: i32[]) -> i32 {
    let total = 0;
    for (let num in numbers) {
        total += num;
    }
    return total;
}

// Named parameters
fun createPoint(x: f64, y: f64, z: f64 = 0.0) -> Point {
    return Point(x, y, z);
}

// Call with named parameters
let point = createPoint(x: 10.0, y: 20.0);
let point3D = createPoint(x: 1.0, y: 2.0, z: 3.0);
```

### Function Overloading

```zom
// Overload by parameter count
fun format(value: i32) -> str {
    return value.toString();
}

fun format(value: f64, precision: i32) -> str {
    return value.toFixed(precision);
}

fun format(value: str, maxLength: i32) -> str {
    return value.length > maxLength ? value.substring(0, maxLength) + "..." : value;
}
```

### Generic Functions

```zom
// Generic function with type parameter
fun identity<T>(value: T) -> T {
    return value;
}

// Generic function with constraints
fun compare<T: Comparable>(a: T, b: T) -> i32 {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

// Multiple type parameters
fun pair<T, U>(first: T, second: U) -> (T, U) {
    return (first, second);
}

// Generic function with default type
fun parseOrDefault<T = str>(input: str, defaultValue: T) -> T {
    // Implementation
}
```

### Async Functions

```zom
// Async function declaration
async fun fetchData(url: str) -> str {
    let response = await httpClient.get(url);
    return await response.text();
}

// Async function with error handling
async fun safelyFetchData(url: str) -> str? {
    try {
        let response = await httpClient.get(url);
        return await response.text();
    } catch (error) {
        print("Failed to fetch data: " + error.message);
        return nil;
    }
}
```

### Function with Error Handling

```zom
// Function that can throw errors
fun divide(a: f64, b: f64) -> Result<f64, DivisionByZeroError> {
    if (b == 0.0) {
        return Failure(DivisionByZeroError("Cannot divide by zero"));
    }
    return Success(a / b);
}

// Function with multiple error types
fun parseAndValidate(input: str) -> Result<i32, ParseError | ValidationError> {
    let parsed = parseInt(input);
    match (parsed) {
        when Failure(e) => return Failure(e)
        when Success(v) => if (v < 0) return Failure(ValidationError("Value must be non-negative")) else return Success(v)
    }
}
```

## Type Declarations

### Type Aliases

```zom
// Simple type alias
alias UserID = i64;
alias EmailAddress = str;

// Generic type alias
alias Result<T, E> = T | E;
alias Optional<T> = T | nil;

// Complex type alias
alias EventHandler<T> = (T) -> unit;
alias AsyncOperation<T> = () -> Promise<T>;

// Function type alias
alias BinaryOperator<T> = (T, T) -> T;
alias Predicate<T> = (T) -> bool;

// Object type alias
alias Point2D = { x: f64, y: f64 };
alias Person = {
    name: str,
    age: i32,
    email?: str
};
```

### Interface Declarations

```zom
// Basic interface
interface Drawable {
    fun draw();
}

// Interface with properties
interface Named {
    name: str;
    readonly id: i64;
}

// Interface with methods and properties
interface Shape {
    readonly area: f64;
    readonly perimeter: f64;

    fun scale(factor: f64);
    fun contains(point: Point) -> bool;
}

// Generic interface
interface Container<T> {
    fun add(item: T);
    fun remove(item: T) -> bool;
    fun contains(item: T) -> bool;
    fun size() -> i32;
}

// Interface inheritance
interface ColoredShape extends Shape {
    color: Color;
    fun changeColor(newColor: Color);
}

// Multiple interface inheritance
interface NamedShape extends Named, Shape {
    fun getDisplayName() -> str;
}

// Interface with optional methods
interface Configurable {
    fun configure(options: ConfigOptions);
    fun reset?(); // Optional method
}
```

### Struct Declarations

```zom
// Basic struct
struct Point {
    x: f64,
    y: f64
}

// Struct with default values
struct Color {
    r: u8 = 0,
    g: u8 = 0,
    b: u8 = 0,
    a: u8 = 255
}

// Generic struct
struct Pair<T, U> {
    first: T,
    second: U
}

// Struct with computed properties
struct Rectangle {
    width: f64,
    height: f64,

    // Computed property
    get area() -> f64 {
        return this.width * this.height;
    }
}

// Struct with methods
struct Vector2D {
    x: f64,
    y: f64,

    fun length() -> f64 {
        return sqrt(this.x * this.x + this.y * this.y);
    }

    fun normalize() -> Vector2D {
        let len = this.length();
        return Vector2D(this.x / len, this.y / len);
    }
}
```

### Enum Declarations

```zom
// Simple enum
enum Direction {
    North,
    South,
    East,
    West
}

// Enum with explicit values
enum StatusCode {
    OK = 200,
    NotFound = 404,
    InternalServerError = 500
}

// Enum with associated values
enum Result<T, E> {
    Success(T),
    Failure(E)
}

// Complex enum with multiple associated values
enum WebEvent {
    PageLoad,
    KeyPress(char),
    Click { x: i32, y: i32 },
    Scroll { deltaX: f64, deltaY: f64 }
}

// Enum with methods
enum Planet {
    Mercury = 0.330,
    Venus = 4.87,
    Earth = 5.97,
    Mars = 0.642,

    fun surfaceGravity() -> f64 {
        const G = 6.67300E-11;
        const RADIUS = 6.37814E6;
        return G * this.mass / (RADIUS * RADIUS);
    }
}
```

### Error Declarations

Error types provide structured error handling with custom error types that can carry additional context information.

```zom
// Simple error type
error NetworkError {
    message: str
}

// Error with multiple fields
error ValidationError {
    field: str,
    message: str,
    code: i32
}

// Generic error type
error ParseError<T> {
    input: str,
    expectedType: Type<T>,
    position: i32
}

// Error hierarchy
error DatabaseError {
    message: str,
    code: i32
}

error ConnectionError extends DatabaseError {
    host: str,
    port: i32
}

error QueryError extends DatabaseError {
    query: str,
    parameters: any[]
}
```

## Class Declarations

### Basic Class Declaration

```zom
// Simple class
class Person {
    let name: str;
    let age: i32;

    init(name: str, age: i32) {
        this.name = name;
        this.age = age;
    }

    fun greet() -> str {
        return "Hello, I'm " + this.name;
    }
}
```

### Class with Access Modifiers

```zom
class BankAccount {
    public let accountNumber: str;
    private let balance: f64;
    protected let owner: str;

    public init(accountNumber: str, owner: str, initialBalance: f64 = 0.0) {
        this.accountNumber = accountNumber;
        this.owner = owner;
        this.balance = initialBalance;
    }

    public fun getBalance() -> f64 {
        return this.balance;
    }

    public fun deposit(amount: f64) {
        if (amount > 0) {
            this.balance += amount;
        }
    }

    private fun validateTransaction(amount: f64) -> bool {
        return amount > 0 && amount <= this.balance;
    }
}
```

### Class Inheritance

```zom
// Base class
class Animal {
    protected let name: str;
    protected let species: str;

    init(name: str, species: str) {
        this.name = name;
        this.species = species;
    }

    fun makeSound() -> str {
        return "Some generic animal sound";
    }

    fun getInfo() -> str {
        return this.name + " is a " + this.species;
    }
}

// Derived class
class Dog extends Animal {
    private let breed: str;

    init(name: str, breed: str) {
        super(name, "Dog");
        this.breed = breed;
    }

    override fun makeSound() -> str {
        return "Woof!";
    }

    fun getBreed() -> str {
        return this.breed;
    }
}
```

### Generic Classes

```zom
// Generic class
class Stack<T> {
    private let items: T[] = [];

    fun push(item: T) {
        this.items.push(item);
    }

    fun pop() -> T? {
        return this.items.pop();
    }

    fun peek() -> T? {
        return this.items.length > 0 ? this.items[this.items.length - 1] : nil;
    }

    fun isEmpty() -> bool {
        return this.items.length == 0;
    }
}

// Generic class with constraints
class SortedList<T: Comparable> {
    private let items: T[] = [];

    fun add(item: T) {
        let index = this.findInsertionPoint(item);
        this.items.insert(index, item);
    }

    private fun findInsertionPoint(item: T) -> i32 {
        // Binary search implementation
        let left = 0;
        let right = this.items.length;

        while (left < right) {
             let mid = (left + right) / 2;
             if (this.items[mid] < item) {
                 left = mid + 1;
             } else {
                 right = mid;
             }
         }
         return left;
     }
 }
 ```