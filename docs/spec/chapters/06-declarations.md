# Declarations

Declarations introduce new named entities into a program's namespace. They define variables, functions, types, classes, and other program constructs. Every declaration may be preceded by an outer attribute list `#[...]` and/or a modifier list.

## Declaration Categories

```ebnf
Declaration ::= VariableStatement
              | ConstDeclaration
              | FunctionDecl
              | ClassDecl
              | StructDecl
              | InterfaceDecl
              | EnumDecl
              | ErrorDecl
              | AliasDecl
              | ImplDecl
              | ExternDecl
              | MacroRulesDecl
```

| Category | Keywords | Description |
|---|---|---|
| **Value Declarations** | `mut`, `let`, `const` | Runtime bindings and compile-time constants |
| **Function Declarations** | `fun` | Named function definitions |
| **Class Declarations** | `class` | Reference types with inheritance |
| **Struct Declarations** | `struct` | Value types with named fields |
| **Interface Declarations** | `interface` | Contracts for types to implement |
| **Enum Declarations** | `enum` | Tagged union / algebraic data types |
| **Error Declarations** | `error` | Structured error value types |
| **Type Aliases** | `alias` | Named synonyms for type expressions |
| **Impl Declarations** | `impl` | Interface implementations and marker evidence |
| **Extern Declarations** | `extern` | Foreign function interface bindings |
| **Macro Rules Declarations** | `macro` | Declarative macro 2.0 definitions |

---

## Modifiers and Attributes

All declarations (except value declarations at statement position) accept an optional `ModifierList` prefix and/or outer attributes `#[...]`.

```ebnf
Modifier     ::= 'public' | 'private' | 'protected'
               | 'static' | 'readonly' | 'mutating' | 'override'
               | 'abstract' | 'export'

ModifierList ::= Modifier*
                 (* semantic predicate enforces valid combinations:
                    e.g. 'static mutating' rejected, 'abstract static' accepted *)
```

| Modifier | Applies To | Semantics |
|---|---|---|
| `public` | Class/struct/interface members, functions | Visible to all modules |
| `private` | Class/struct/interface members, functions | Visible only within enclosing module |
| `protected` | Class members | Visible within module and subclasses |
| `static` | Class/struct/interface members | Belongs to type, not instances |
| `readonly` | Struct fields, interface properties | Cannot be mutated after initialization |
| `mutating` | Methods | May mutate `self` / `this` |
| `override` | Class methods | Overrides a superclass method |
| `abstract` | Classes, methods | No implementation; must be overridden |
| `export` | Top-level declarations | Promotes symbol across crate boundaries |

Value declarations (`mut`, `let`, `const`) do **not** accept a `ModifierList` prefix. Visibility and attributes flow via the enclosing `moduleItem` / `StatementListItem` production.

---

## Value Declarations

ZOM separates runtime binding mutability from compile-time constants:

- `mut` declares a mutable runtime binding.
- `let` declares an immutable runtime binding.
- `const` declares a compile-time constant.

```ebnf
VariableStatement  ::= ( 'mut' | 'let' ) VariableDeclList ';'

VariableDeclList   ::= VariableDecl (',' VariableDecl)*
VariableDecl       ::= ( BindingIdent | BindingPattern ) TypeAnnotation? Initializer?
                       (* mut without Initializer requires TypeAnnotation.
                          let without Initializer is accepted only where
                          definite assignment can prove exactly one write
                          before first read. *)
Initializer        ::= '=' AssignmentExpression

ConstDeclaration   ::= 'const' ConstDeclList ';'
ConstDeclList      ::= ConstDecl (',' ConstDecl)*
ConstDecl          ::= BindingIdent TypeAnnotation? '=' ConstExpression
ConstExpression    ::= AssignmentExpression
                       (* semantically restricted to expressions accepted
                          by the constant evaluator *)
```

Runtime bindings are block-scoped. `const` declarations are available wherever declarations are accepted, but their initializer must be evaluable by the constant evaluator.

Only `mut` bindings may be reassigned or used as mutable places, including calls that require a mutable receiver or mutable borrow. `let` bindings may be read, moved, or immutably borrowed, but may not be reassigned or mutably borrowed.

For fields, `let` denotes immutable storage after object initialization. A `let` field may be definitely assigned by the owning `init` path before `this` escapes; after initialization it follows the same immutable-place rule as a local `let`.

### `mut` Declarations

Declare mutable runtime bindings:

```zom
// Basic mutable declaration
mut count = 0;
mut name = "Alice";

// With explicit type annotation
mut age: i32 = 25;
mut height: f64 = 5.8;

// Multiple declarations
mut x = 10, y = 20, z = 30;

// Uninitialized declaration (requires type annotation)
mut result: str;
if (condition) {
    result = "success";
} else {
    result = "failure";
}
```

### `let` Declarations

Declare immutable runtime bindings:

```zom
// Basic immutable declaration
let count = 0;
let name = "Alice";

// With explicit type annotation
let age: i32 = 25;
let height: f64 = 5.8;

// Destructuring declaration
let (first, second) = getTuple();
let { name, age } = getPerson();
let [head, ...tail] = getArray();
```

### `const` Declarations

Declare compile-time constants:

```zom
// Basic compile-time constants
const PI = 3.14159;
const MAX_SIZE = 1000;

// With explicit type
const GREETING: str = "Hello, World!";

// Compile-time aggregate constants
const CONFIG = {
    host: "localhost",
    port: 8080,
    ssl: false
};

// Computed constants
const AREA = PI * RADIUS * RADIUS;
```

`const` declarations require an initializer and bind identifiers only. Destructuring `const` declarations are not part of v1. Runtime calls, allocation, I/O, non-deterministic operations, and ordinary function calls are rejected unless the callee is explicitly admitted to const evaluation by a future const-function design. A `const` has no stable storage address; it is a named compile-time value that may be substituted at use sites. Storage-backed global objects are intentionally separate from `const`.

### Rejected `var`

`var` is reserved only to produce a targeted diagnostic (`ZOM5003`). It is not a declaration form in ZOM. Use `mut` for mutable block-scoped runtime bindings.

---

## Function Declarations

```ebnf
FunctionDecl   ::= ModifierList UnsafePrefix? 'fun' BindingIdent TypeParameters?
                   ParameterClause FunctionSignature? FunctionBody?

UnsafePrefix   ::= 'unsafe'   (* soft keyword; semantic predicate enforces position *)

FunctionSignature ::= '->' TypeExpr RaisesClause?   (* return type with optional raises *)
                    | RaisesClause                   (* raises without return type *)

FunctionBody   ::= BlockStatement | ';'

RaisesClause   ::= 'raises' TypeExpr
                   (* single type expression; union types expressed via
                      TypeExpr itself: e.g. `raises ParseError | IoError` *)

ParameterClause ::= '(' ParameterList? ')'
ParameterList   ::= Parameter (',' Parameter)* ','?
Parameter       ::= OuterAttributeList? (Identifier ':')? TypeExpr Initializer?
                  | OuterAttributeList? 'this'
                    (* unnamed positional params allowed: `fun f(i32, str) -> i32`;
                       `this` is the explicit receiver parameter and defaults to Self. *)
```

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
    if (age != null) {
        print("Age: " + age.toString());
    }
}

// Rest parameters
fun sum(...numbers: i32[]) -> i32 {
    mut total = 0;
    for (let num in numbers) {
        total += num;
    }
    return total;
}

// Named parameters at call site
fun createPoint(x: f64, y: f64, z: f64 = 0.0) -> Point {
    return Point(x, y, z);
}

// Call with named parameters
let point = createPoint(x: 10.0, y: 20.0);
let point3D = createPoint(x: 1.0, y: 2.0, z: 3.0);
```

### Function with Error Handling (`raises`)

The `raises` clause explicitly declares which error types a function may produce:

```zom
// Function that raises a single error type
fun divide(a: f64, b: f64) -> f64 raises DivisionByZeroError {
    if (b == 0.0) {
        return DivisionByZeroError("Cannot divide by zero");
    }
    return a / b;
}

// Function with multiple error types (union in raises)
fun parseAndValidate(input: str) -> i32 raises ParseError | ValidationError {
    let parsed = parseInt(input);
    match (parsed) {
        when Failure(e) => { return e; }
        when Success(v) => {
            if (v < 0) {
                return ValidationError("Value must be non-negative");
            }
            return v;
        }
    }
}

// Function with raises only (no return type = unit)
fun connect(host: str) raises ConnectionError | TimeoutError {
    // implementation
}
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

### Unsafe Functions

An `unsafe fun` declares a function whose correct calling depends on preconditions that the compiler cannot statically verify. Calling an `unsafe fun` requires the caller to wrap the call in an `unsafe { }` block, attesting that the preconditions are satisfied.

```zom
/// # Safety
/// Caller must ensure:
/// - `ptr` is non-null and properly aligned
/// - `ptr` points to a valid, initialized `T`
/// - No concurrent mutable access to `*ptr` during this call
unsafe fun read_unchecked<T>(ptr: *const T) -> T {
    *ptr  // OK: function body is implicitly unsafe context
}

// Calling requires unsafe { } at call site
let value = unsafe { read_unchecked(my_ptr) };
```

Rules:

1. **Body is implicitly unsafe.** The body of an `unsafe fun` is treated as an `unsafe` context.
2. **Caller attestation.** Every call to an `unsafe fun` must appear within an `unsafe { }` block, or the compiler emits `ZOM0904 UnsafeCallOutsideUnsafe`.
3. **Documentation required.** Every `unsafe fun` SHOULD include a `# Safety` section in its doc comment. Lint `ZOM0920 MissingSafetyDoc` warns when an `unsafe fun` lacks a `# Safety` doc section.
4. **Transitivity.** An `unsafe fun` that calls another `unsafe fun` does NOT need a nested `unsafe { }` block.

### Reserved Function Forms

`async` and `await` are reserved words, but asynchronous function syntax is not part of the current parser grammar. ZOM uses the zero-color `suspend`/`spawn` model instead (see [Ch.15 Concurrency](15-concurrency.md)).

---

## Class Declarations

Classes are reference types that support single inheritance, polymorphism, and encapsulation.

```ebnf
ClassDecl      ::= ModifierList 'class' BindingIdent TypeParameters?
                   ClassHeritage?
                   '{' ClassElement* '}'

ClassHeritage  ::= ':' TypeExpr        (* single superclass; written with colon,
                                           NOT 'extends' keyword *)

ClassElement   ::= ';'
                 | OuterAttributeList ModifierList InitDecl
                 | OuterAttributeList ModifierList DeinitDecl
                 | OuterAttributeList ModifierList PropertyDecl
                 | OuterAttributeList ModifierList ClassConstDecl
                 | OuterAttributeList ModifierList MethodDecl
                 | OuterAttributeList ModifierList ClassFieldDecl
                 | OuterAttributeList ModifierList ComputedPropertyDecl

PropertyStorage ::= 'mut' | 'let'
PropertyDecl    ::= PropertyStorage PropertyName
                     '?'? TypeAnnotation? Initializer? ';'
ClassConstDecl  ::= 'const' BindingIdent TypeAnnotation? '=' ConstExpression ';'
ClassFieldDecl  ::= PropertyName ':' TypeExpr ('=' Expression)?
                     (';' | ',' | (* implicit separator before next keyword-starting member *))
MethodDecl      ::= 'fun' PropertyName TypeParameters?
                     ParameterClause FunctionSignature?
                     ( BlockStatement | ';' )
InitDecl        ::= 'init' TypeParameters? ParameterClause
                     RaisesClause? BlockStatement
DeinitDecl      ::= 'deinit' ParameterClause RaisesClause? BlockStatement

ComputedPropertyDecl ::= 'get' PropertyName ParameterClause
                          FunctionSignature? BlockStatement
                          SetAccessorDecl?
SetAccessorDecl  ::= OuterAttributeList ModifierList 'set' PropertyName
                      ParameterClause FunctionSignature? BlockStatement
                   | 'set' ParameterClause FunctionSignature? BlockStatement
```

### Basic Class Declaration

```zom
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
    private mut balance: f64;
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

Class inheritance uses the colon (`:`) syntax, **not** the `extends` keyword:

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

// Derived class (colon syntax, NOT 'extends')
class Dog: Animal {
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

### Abstract Classes and Methods

```zom
abstract class Shape {
    protected let color: str;

    public init(color: str) {
        this.color = color;
    }

    // Abstract method — must be implemented by subclasses
    abstract public fun area() -> f64;
    abstract public fun perimeter() -> f64;

    // Concrete method
    public fun getColor() -> str {
        return this.color;
    }
}

class Circle: Shape {
    private let radius: f64;

    public init(color: str, radius: f64) {
        super(color);
        this.radius = radius;
    }

    override public fun area() -> f64 {
        return 3.14159 * this.radius * this.radius;
    }

    override public fun perimeter() -> f64 {
        return 2.0 * 3.14159 * this.radius;
    }
}
```

### Computed Properties (get/set)

```zom
class Temperature {
    private mut celsius: f64;

    init(celsius: f64) {
        this.celsius = celsius;
    }

    // Read-only computed property
    get fahrenheit() -> f64 {
        return this.celsius * 9.0 / 5.0 + 32.0;
    }

    // Computed property with getter and setter
    get kelvin() -> f64 {
        return this.celsius + 273.15;
    }
    set kelvin(value: f64) {
        this.celsius = value - 273.15;
    }
}
```

### Generic Classes

```zom
class Stack<T> {
    private let items: T[] = [];

    fun push(item: T) {
        this.items.push(item);
    }

    fun pop() -> T? {
        return this.items.pop();
    }

    fun peek() -> T? {
        return this.items.length > 0 ? this.items[this.items.length - 1] : null;
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
        mut left = 0;
        mut right = this.items.length;

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

### Deinitializer

```zom
class Resource {
    private let handle: *mut u8;

    init() {
        this.handle = unsafe { allocate_memory(1024) };
    }

    deinit {
        unsafe { free_memory(this.handle) };
    }
}
```

---

## Struct Declarations

Structs are value types with named fields. They do not support inheritance but can implement interfaces via standalone `impl` blocks.

```ebnf
StructDecl     ::= ModifierList 'struct' BindingIdent TypeParameters?
                   '{' StructElement* '}'

StructElement  ::= OuterAttributeList ModifierList StructFieldDecl
                 | OuterAttributeList ModifierList MethodDecl
                 | OuterAttributeList ModifierList StructCtorDecl

StructFieldDecl ::= ('mut' | 'readonly')? PropertyName
                    ':' TypeExpr
                    ( '=' Expression )?   (* default value *)
                    ( ';' | ',' | (* implicit separator OK before next keyword member *) )?

StructCtorDecl  ::= ('init' | 'deinit') ParameterClause RaisesClause? BlockStatement
```

### Basic Struct

```zom
struct Point {
    x: f64,
    y: f64
}
```

### Struct with Default Values

```zom
struct Color {
    r: u8 = 0,
    g: u8 = 0,
    b: u8 = 0,
    a: u8 = 255
}
```

### Struct with Field Mutability

```zom
struct Buffer {
    readonly capacity: i32,
    mut length: i32,
    mut data: u8[]
}
```

### Generic Struct

```zom
struct Pair<T, U> {
    first: T,
    second: U
}
```

### Struct with Methods

```zom
struct Vector2D {
    x: f64,
    y: f64;

    fun length() -> f64 {
        return sqrt(this.x * this.x + this.y * this.y);
    }

    fun normalize() -> Vector2D {
        let len = this.length();
        return Vector2D(x: this.x / len, y: this.y / len);
    }
}
```

### Struct with Constructor

```zom
struct Person {
    let name: str;
    let age: i32;
    mut email: str?;

    init(name: str, age: i32) {
        this.name = name;
        this.age = age;
        this.email = null;
    }

    init(name: str, age: i32, email: str) {
        this.name = name;
        this.age = age;
        this.email = email;
    }
}
```

---

## Interface Declarations

Interfaces define contracts that types can implement. They support single-inheritance via the colon (`:`) syntax and may contain associated type declarations.

```ebnf
InterfaceDecl  ::= ModifierList 'interface' BindingIdent TypeParameters?
                   InterfaceHeritage?
                   '{' InterfaceBody '}'

InterfaceHeritage ::= ':' InterfaceBoundList   (* super-interfaces; colon-separated,
                                                   NOT 'extends' *)
InterfaceBoundList ::= InterfaceBound ( '+' InterfaceBound )*
                       (* '+' = conjunction (AND); '|' is ONLY for UnionType *)
InterfaceBound     ::= QualifiedPathOrIdent ( '<' TypeArgumentList '>' )?
QualifiedPathOrIdent ::= PathSegment ( '::' PathSegment )*

InterfaceBody   ::= InterfaceElement*
InterfaceElement ::= ';'
                  | OuterAttributeList ModifierList 'fun' MethodSignature ';'?
                  | OuterAttributeList ModifierList ('get' | 'set') PropertySignature ';'?
                  | OuterAttributeList ModifierList 'type' Identifier TypeParameters?
                    ( ':' InterfaceBoundList )? ( '=' TypeExpr )? ';'

PropertySignature ::= PropertyName '?'? TypeAnnotation
MethodSignature   ::= PropertyName '?'? CallSignature
CallSignature     ::= TypeParameters? ParameterClause FunctionSignature?
```

Interfaces may be marked `unsafe` to indicate that implementing them carries a semantic contract the compiler cannot enforce. See [Ch.09 Interfaces](09-interfaces.md) for full semantics.

### Basic Interface

```zom
interface Drawable {
    fun draw();
    fun getBounds() -> Rectangle;
}
```

### Interface with Properties

```zom
interface Named {
    name: str;
    readonly id: i64;
}
```

### Interface with Methods and Properties

```zom
interface Shape {
    readonly area: f64;
    readonly perimeter: f64;

    fun scale(factor: f64);
    fun contains(point: Point) -> bool;
}
```

### Generic Interface

```zom
interface Container<T> {
    fun add(item: T);
    fun remove(item: T) -> bool;
    fun contains(item: T) -> bool;
    fun size() -> i32;
}
```

### Interface Inheritance

Interface inheritance uses the colon (`:`) syntax with `+` for multiple super-interfaces:

```zom
// Single super-interface
interface ColoredShape: Shape {
    color: Color;
    fun changeColor(newColor: Color);
}

// Multiple super-interfaces (conjunction via '+')
interface NamedShape: Named + Shape {
    fun getDisplayName() -> str;
}
```

### Interface with Optional Methods

```zom
interface Configurable {
    fun configure(options: ConfigOptions);
    fun reset?(); // Optional method
}
```

### Interface with Associated Types

```zom
interface Iterator {
    type Item;   // Associated type

    fun next() -> this.Item?;
    fun hasNext() -> bool;
}

interface Collection {
    type Element;
    type Iter: Iterator<Item = this.Element>;

    fun iter() -> this.Iter;
    fun count() -> i32;
}
```

### Unsafe Interfaces

Interfaces with unverifiable semantic invariants may be documented as unsafe via the `unsafe` prefix in their `impl` declaration:

```zom
interface Sendable {
    // Marker interface — see Ch.16 §16.9 for standard markers
}

// Implementing an unsafe interface requires 'unsafe impl'
unsafe impl Sendable for MyType {
    // ...
}
```

---

## Enum Declarations

Enums define tagged union (algebraic data) types. Each variant may carry associated tuple data or an explicit discriminant value.

```ebnf
EnumDecl       ::= ModifierList 'enum' BindingIdent TypeParameters?
                   '{' EnumBody? '}'
EnumBody       ::= EnumVariant ( ',' EnumVariant )* ','?
EnumVariant    ::= OuterAttributeList Identifier
                   ( '(' VariantTypeList ')' )?   (* tuple associated value *)
                   ( '=' Expression )?            (* explicit discriminant / raw value *)
VariantTypeList ::= TypeExpr ( ',' TypeExpr )* ','?
```

### Simple Enum

```zom
enum Direction {
    North,
    South,
    East,
    West
}
```

### Enum with Explicit Values

```zom
enum StatusCode {
    OK = 200,
    NotFound = 404,
    InternalServerError = 500
}
```

### Enum with Associated Values

```zom
enum Result<T, E> {
    Success(T),
    Failure(E)
}
```

### Complex Enum with Multiple Associated Values

```zom
enum WebEvent {
    PageLoad,
    KeyPress(char),
    Click(i32, i32),
    Scroll { deltaX: f64, deltaY: f64 }
}
```

### Enum with Methods

```zom
enum Planet {
    Mercury = 0.330,
    Venus = 4.87,
    Earth = 5.97,
    Mars = 0.642;

    fun surfaceGravity() -> f64 {
        const G = 6.67300E-11;
        const RADIUS = 6.37814E6;
        return G * this.mass / (RADIUS * RADIUS);
    }
}
```

---

## Error Declarations

Error types provide structured error handling with custom error types that can carry additional context information. They work with the `raises` clause and `match`/`is` patterns.

```ebnf
ErrorDecl      ::= ModifierList 'error' BindingIdent TypeParameters?
                   ErrorHeritage?
                   '{' ErrorBody? '}'
ErrorHeritage  ::= ':' TypeExpr             (* error inheritance chain;
                                                   colon-separated, NOT 'extends' *)
ErrorBody      ::= ErrorField ( (',' | ';') ErrorField )* (',' | ';')?
ErrorField     ::= PropertyName ':' TypeExpr
                   ( '=' Expression )?
```

There is no `throw` keyword (Principle P3: Explicit Error Flow). Errors are returned as values and handled via pattern matching.

### Simple Error Type

```zom
error NetworkError {
    message: str
}
```

### Error with Multiple Fields

```zom
error ValidationError {
    field: str,
    message: str,
    code: i32
}
```

### Generic Error Type

```zom
error ParseError<T> {
    input: str,
    expectedType: Type<T>,
    position: i32
}
```

### Error Hierarchy

Error inheritance uses the colon (`:`) syntax:

```zom
error DatabaseError {
    message: str,
    code: i32
}

error ConnectionError: DatabaseError {
    host: str,
    port: i32
}

error QueryError: DatabaseError {
    query: str,
    parameters: any[]
}
```

---

## Type Aliases

Type aliases introduce named synonyms for type expressions. They do not create new types; the alias is interchangeable with the underlying type at all positions.

```ebnf
AliasDecl      ::= ModifierList 'alias' BindingIdent TypeParameters?
                   '=' TypeExpr ';'
```

### Simple Type Alias

```zom
alias UserID = i64;
alias EmailAddress = str;
```

### Generic Type Alias

```zom
alias Result<T, E> = T | E;
alias Optional<T> = T | null;
```

### Complex Type Alias

```zom
alias EventHandler<T> = (T) -> unit;
alias AsyncOperation<T> = () -> Promise<T>;
```

### Function Type Alias

```zom
alias BinaryOperator<T> = (T, T) -> T;
alias Predicate<T> = (T) -> bool;
```

### Object Type Alias

```zom
alias Point2D = { x: f64, y: f64 };
alias Person = {
    name: str,
    age: i32,
    email?: str
};
```

---

## Impl Declarations

Impl blocks attach interface implementations to types. There are two forms: **standalone impl** (for ordinary interface implementations) and **marker impl** (for marker trait evidence).

```ebnf
ImplDecl       ::= StandaloneImplDecl
                 | MarkerImplDecl

StandaloneImplDecl ::= 'impl' TypeParameters? InterfaceBoundList 'for' TypeExpr
                       '{' ImplMember* '}'
    (* 'impl' is a SOFT keyword — recognized only at impl-head position *)

ImplMember     ::= ModifierList 'fun' BindingIdent TypeParameters? ParameterClause
                    FunctionSignature? ( ';' | BlockStatement )
                 | 'type' Identifier TypeParameters? '=' TypeExpr ';'
                 | 'mut' VariableDeclList ';'
                 | 'let' VariableDeclList ';'
                 | 'const' ConstDeclList ';'
                 | ModifierList 'alias' BindingIdent TypeParameters? '=' TypeExpr ';'

MarkerImplDecl ::= UnsafePrefix? 'impl' '!'? AttrPath TypeParameters?
                   'for' TypeExpr ( ';' | '{' StructElement* '}' )
    (* Marker impl: provides explicit marker trait evidence.
       '!' negates the impl (negative impl).
       'unsafe' prefix marks a marker impl with caller-proven invariants. *)
```

### Standalone Interface Impl

```zom
interface Drawable {
    fun draw();
    fun getBounds() -> Rectangle;
}

class Button {
    let position: Point;
    let size: Size;
    let text: str;

    init(position: Point, size: Size, text: str) {
        this.position = position;
        this.size = size;
        this.text = text;
    }
}

impl Drawable for Button {
    public fun draw() {
        print("Drawing button: " + this.text);
    }

    public fun getBounds() -> Rectangle {
        return Rectangle(this.position, this.size);
    }
}
```

### Impl with Associated Types

```zom
interface Iterator {
    type Item;
    fun next() -> this.Item?;
    fun hasNext() -> bool;
}

struct VecIter<T> {
    let data: T[];
    mut index: i32;

    init(data: T[]) {
        this.data = data;
        this.index = 0;
    }
}

impl Iterator for VecIter<T> {
    type Item = T;

    fun next() -> T? {
        if (this.index >= this.data.length) return null;
        let value = this.data[this.index];
        this.index += 1;
        return value;
    }

    fun hasNext() -> bool {
        return this.index < this.data.length;
    }
}
```

### Marker Impl

```zom
// Positive marker impl
impl std::marker::Sendable for MyType;

// Negative marker impl
impl !std::marker::Shared for MyType;

// Unsafe marker impl
unsafe impl std::marker::Sendable for RawPointerWrapper;

// Marker impl with body (for marker impls that need to prove invariants)
impl std::marker::Linear for FileHandle {
    // FileHandle must be consumed or explicitly closed
}
```

---

## Extern Declarations

Foreign function interface (FFI) declarations provide bindings to code written in other languages.

```ebnf
ExternDecl     ::= UnsafePrefix? 'extern' AbiLiteral? ( ExternBlock | FunctionDecl )
    (* 'extern' is a SOFT keyword *)

AbiLiteral     ::= '"' ('C' | 'Cdecl' | 'system' | 'zom-cdecl') '"'

ExternBlock    ::= '{' ExternItem* '}'
ExternItem     ::= FunctionDecl                            (* external function *)
                 | 'variable' Identifier ':' TypeExpr ';'  (* external variable *)
                 | 'type' Identifier '=' 'opaque'? Identifier TypeExpr? ';'
                   (* opaque type alias for FFI forward declarations *)
```

### External Function Binding

```zom
extern "C" {
    fun printf(format: str, ...) -> i32;
    fun malloc(size: i64) -> *mut u8;
    fun free(ptr: *mut u8);
}
```

### External Variable

```zom
extern "C" {
    variable errno: i32;
}
```

### Opaque Type Forward Declaration

```zom
extern "C" {
    type FILE = opaque;
    fun fopen(filename: str, mode: str) -> *FILE;
    fun fclose(stream: *FILE) -> i32;
}
```

### Unsafe Extern Block

```zom
unsafe extern "C" {
    fun memcpy(dest: *mut u8, src: *const u8, n: i64) -> *mut u8;
}
```

---

## Macro Rules Declarations

Declarative macro 2.0 definitions. The `macro` keyword is a soft keyword recognized only at macro-head position.

```ebnf
MacroRulesDecl ::= 'macro' Identifier '!' '{' MacroRule* '}'

MacroRule      ::= '(' MacroPattern ')' '=>' '{' MacroTokenTree '}' ';'?
                 | '[' MacroPattern ']' '=>' '{' MacroTokenTree '}' ';'?

MacroPattern   ::= MacroPatToken ( ',' MacroPatToken )* ( ',' '...' )?
MacroPatToken  ::= '$' Identifier ':' MacroFragSpec
                 | Identifier | Literal
                 | Punctuator

MacroFragSpec  ::= Identifier   (* e.g. expr, stmt, item, pat, ty, ident, path, tt *)
```

### Basic Macro

```zom
macro println! {
    ($msg:expr) => {
        print($msg + "\n");
    };
    ($fmt:expr, $($arg:expr),*) => {
        print(format!($fmt, $($arg),*) + "\n");
    };
}
```

---

## Module Declarations

A `module` clause at the head of a source file declares the dotted symbol path of that file. It is optional for crate-root files, whose implicit module name is the crate name from the manifest. When present, it must be the first non-comment, non-shebang item in the file.

```ebnf
ModuleDecl     ::= 'module' ModuleName ';'
                 | 'module' ModuleName '{' ModuleItem* '}'
                 | 'export'? 'module' ModuleName '=' AttributePath ';'

ModuleName     ::= Identifier ('.' Identifier)*
```

```zom
// Simple module declaration
module myapp.services.auth;

// Module alias
module utils = myapp::utilities::common;

// Exported module alias
export module math = myapp::math::core;
```

See [Ch.13 Modules and Imports](13-modules-and-imports.md) for the full module system specification.

---

## Declaration Order and Scoping

- Declarations at module scope are hoisted within that module; the order of top-level declarations does not affect name resolution.
- Local value declarations (`mut`, `let`, `const`) inside block bodies follow lexical scoping and are not hoisted.
- Forward references to functions, types, and classes at module scope are permitted.
- Forward references to local `let`/`mut` bindings are not permitted; definite-assignment rules apply.

## Conditional Compilation on Declarations

Declarations may be gated by `#[zom::cfg(...)]` attributes at module scope. Unlike statements, declarations (including `const`, `fun`, `class`, `struct`, `interface`, `enum`, `error`, `alias`, `import`, `export`, `module`, `impl`, `extern`, `macro`) unconditionally accept all outer attributes.

```zom
#[zom::cfg(feature = "logging")]
fun logMessage(msg: str) {
    print("[LOG] " + msg);
}

#[zom::cfg(target_os = "linux")]
extern "C" {
    fun epoll_create1(flags: i32) -> i32;
}
```

Value declarations (`let`, `mut`) at module scope may NOT be individually cfg-gated; they must be wrapped in a standalone block:

```zom
// Error ZOM1901: cannot gate a single let/mut declaration
// #[zom::cfg(feature = "debug")]
// let debug_level = 3;

// OK: wrap in standalone block
#[zom::cfg(feature = "debug")] {
    let debug_level = 3;
}
```

See [Ch.19 Conditional Compilation](19-conditional-compilation.md) for full `#[zom::cfg(...)]` syntax and semantics.
