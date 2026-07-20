# Declarations

Declarations introduce named entities into a program's namespace. Module items
and non-expression block items may carry an outer attribute list. Function
parameters have their own attribute slot. Type members and enum variants do not
accept outer attributes.

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

---

## Modifiers and Attributes

Named function, type, and alias declarations have a `ModifierList` syntax slot.
Module-level visibility modifiers are rejected with `ZOM2088`; `export` creates
the module export surface. Member modifiers apply only inside a type body.

```ebnf
Modifier     ::= 'public' | 'private' | 'protected'
               | 'static' | 'readonly' | 'mutating' | 'override'
               | 'abstract'

ModifierList ::= Modifier*
                 (* semantic predicate enforces valid combinations:
                    e.g. 'static mutating' and 'abstract static' are rejected *)
```

The visibility spellings are exactly `public`, `private`, and `protected`.
Identifier aliases such as `pub`, `priv`, and `internal` are not modifiers.
`mut` is a mutable-binding or mutable-field declaration head; it is never a
member of `ModifierList`.

| Modifier | Applies To | Semantics |
|---|---|---|
| `public` | Class/struct/interface members | Public member visibility fact |
| `private` | Class/struct/interface members | Private member visibility fact |
| `protected` | Class members | Protected member visibility fact |
| `static` | Class/struct/interface members | Belongs to type, not instances |
| `readonly` | Struct fields, interface properties | Cannot be mutated after initialization |
| `mutating` | Methods | May mutate storage through an explicit `this` receiver |
| `override` | Class methods | Overrides a superclass method |
| `abstract` | Classes, methods | No implementation; must be overridden |
| `export` | Module declarations | Adds a symbol to the module export surface |

`public`, `private`, and `protected` are retained member metadata. The current
language does not reject member lookup from those facts and does not compute a
subclass access context. Chapter 23 defines this boundary. Module visibility is
controlled only by explicit `export` surfaces.

Module-scope value declarations (`mut`, `let`, `const`) do not accept a
`ModifierList` prefix. A type-body field declaration has its own member
modifier slot before the `mut`, `let`, or `const` declaration head. Module
visibility is expressed only by `export`.

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

For fields, `let` denotes immutable storage after object initialization. A `let`
field may be definitely assigned by an `init` callable that explicitly declares
`this`, before that receiver escapes. After initialization it follows the same
immutable-place rule as a local `let`.

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

`var` is a reserved lexer keyword and is not a declaration form. Rejected uses
emit registered parser diagnostics. Use `mut` for mutable block-scoped runtime
bindings.

---

## Function Declarations

```ebnf
FunctionDecl   ::= ModifierList 'fun' BindingIdent TypeParameters?
                   FunctionSignature WhereClause? FunctionBody

FunctionSignature ::= OrdinaryParameterClause
                      ( '->' TypeExpr RaisesClause? | RaisesClause )?
MemberFunctionSignature ::= ParameterClause
                            ( '->' TypeExpr RaisesClause? | RaisesClause )?

FunctionBody   ::= BlockStatement | ';'

RaisesClause   ::= 'raises' TypeExpr
                   (* single type expression; union types expressed via
                      TypeExpr itself: e.g. `raises ParseError | IoError` *)

ParameterClause ::= '(' ParameterList? ')'
OrdinaryParameterClause ::= '(' OrdinaryParameterList? ')'
ParameterList   ::= Parameter (',' OrdinaryParameter)* ','?
OrdinaryParameterList ::= OrdinaryParameter (',' OrdinaryParameter)* ','?
Parameter       ::= ReceiverParameter | OrdinaryParameter
ReceiverParameter ::= OuterAttributeList? 'this' (':' TypeExpr)?
                      (* `this` is the explicit receiver parameter and defaults to Self. *)
OrdinaryParameter ::= OuterAttributeList? Identifier ':' TypeExpr Initializer?
```

A callable has a receiver only when its parameter clause explicitly declares
`this`. Placement in a class, struct, error, interface, or `impl` body does not
synthesize a receiver. A `this` expression in the callable body resolves to
that declared parameter. Only a direct method, accessor, initializer, or
deinitializer member may declare a receiver. Module functions, block
functions, extern functions, function expressions, and lambdas use
`OrdinaryParameterClause`. `ZOM2095 ReceiverNotAllowedHere` rejects `this` in
ordinary callable contexts.

The receiver is unique and must be the first parameter. `ZOM2093
ReceiverMustBeFirstParameter` rejects a receiver in any later position. A
receiver never has a default value;
`ZOM2094 ReceiverDefaultNotAllowed` rejects an initializer on `this`.

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

// Nullable parameter with a default value
fun createUser(name: str, email: str, age: i32? = null) {
    if (age != null) {
        print("Age: " + age.toString());
    }
}

// Default parameter used by positional calls
fun createPoint(x: f64, y: f64, z: f64 = 0.0) -> Point {
    return Point(x, y, z);
}

let point = createPoint(10.0, 20.0);
let point3D = createPoint(1.0, 2.0, 3.0);
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

### Reserved Function Forms

`async` and `await` are reserved words, but asynchronous function syntax is not part of the current parser grammar. ZOM uses the zero-color `suspend`/`spawn` model instead (see [Ch.15 Concurrency](15-concurrency.md)).

---

## Class Declarations

Classes are reference types that support single inheritance, polymorphism, and encapsulation.

```ebnf
ClassDecl      ::= ModifierList 'class' BindingIdent TypeParameters?
                   ClassHeritage? WhereClause?
                   '{' ClassElement* '}'

ClassHeritage  ::= ':' TypeExpr        (* single superclass *)

ClassElement   ::= ModifierList InitDecl
                 | ModifierList DeinitDecl
                 | ModifierList PropertyDecl
                 | ModifierList ClassConstDecl
                 | ModifierList MethodDecl
                 | ModifierList ClassFieldDecl
                 | ModifierList ComputedPropertyDecl

PropertyStorage ::= 'mut' | 'let'
PropertyDecl    ::= PropertyStorage PropertyName
                     '?'? TypeAnnotation? Initializer? ';'
ClassConstDecl  ::= 'const' BindingIdent TypeAnnotation? '=' ConstExpression ';'
ClassFieldDecl  ::= PropertyName ':' TypeExpr ('=' Expression)?
                     (';' | ',' | (* implicit separator before next keyword-starting member *))
MethodDecl      ::= 'fun' PropertyName TypeParameters? MemberFunctionSignature
                     ( BlockStatement | ';' )
InitDecl        ::= 'init' ParameterClause RaisesClause? BlockStatement
DeinitDecl      ::= 'deinit' ParameterClause RaisesClause? BlockStatement

ComputedPropertyDecl ::= 'get' PropertyName MemberFunctionSignature BlockStatement
                          SetAccessorDecl?
SetAccessorDecl  ::= ModifierList 'set' PropertyName
                      MemberFunctionSignature BlockStatement
                   | 'set' PropertyName MemberFunctionSignature BlockStatement
```

### Basic Class Declaration

```zom
class Person {
    let name: str;
    let age: i32;

    init(this, name: str, age: i32) {
        this.name = name;
        this.age = age;
    }

    fun greet(this) -> str {
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

    public init(this, accountNumber: str, owner: str, initialBalance: f64 = 0.0) {
        this.accountNumber = accountNumber;
        this.owner = owner;
        this.balance = initialBalance;
    }

    public fun getBalance(this) -> f64 {
        return this.balance;
    }

    public fun deposit(this, amount: f64) {
        if (amount > 0) {
            this.balance += amount;
        }
    }

    private fun validateTransaction(this, amount: f64) -> bool {
        return amount > 0 && amount <= this.balance;
    }
}
```

### Class Inheritance

Class inheritance uses a colon (`:`):

```zom
// Base class
class Animal {
    protected let name: str;
    protected let species: str;

    init(this, name: str, species: str) {
        this.name = name;
        this.species = species;
    }

    fun makeSound(this) -> str {
        return "Some generic animal sound";
    }

    fun getInfo(this) -> str {
        return this.name + " is a " + this.species;
    }
}

// Derived class
class Dog: Animal {
    private let breed: str;

    init(this, name: str, breed: str) {
        super(name, "Dog");
        this.breed = breed;
    }

    override fun makeSound(this) -> str {
        return "Woof!";
    }

    fun getBreed(this) -> str {
        return this.breed;
    }
}
```

### Abstract Classes and Methods

```zom
abstract class Shape {
    protected let color: str;

    public init(this, color: str) {
        this.color = color;
    }

    // Abstract method — must be implemented by subclasses
    abstract public fun area(this) -> f64;
    abstract public fun perimeter(this) -> f64;

    // Concrete method
    public fun getColor(this) -> str {
        return this.color;
    }
}

class Circle: Shape {
    private let radius: f64;

    public init(this, color: str, radius: f64) {
        super(color);
        this.radius = radius;
    }

    override public fun area(this) -> f64 {
        return 3.14159 * this.radius * this.radius;
    }

    override public fun perimeter(this) -> f64 {
        return 2.0 * 3.14159 * this.radius;
    }
}
```

### Computed Properties (get/set)

```zom
class Temperature {
    private mut celsius: f64;

    init(this, celsius: f64) {
        this.celsius = celsius;
    }

    // Read-only computed property
    get fahrenheit(this) -> f64 {
        return this.celsius * 9.0 / 5.0 + 32.0;
    }

    // Computed property with getter and setter
    get kelvin(this) -> f64 {
        return this.celsius + 273.15;
    }
    set kelvin(this, value: f64) {
        this.celsius = value - 273.15;
    }
}
```

### Generic Classes

```zom
class Stack<T> {
    private let items: T[] = [];

    fun push(this, item: T) {
        this.items.push(item);
    }

    fun pop(this) -> T? {
        return this.items.pop();
    }

    fun peek(this) -> T? {
        return this.items.length > 0 ? this.items[this.items.length - 1] : null;
    }

    fun isEmpty(this) -> bool {
        return this.items.length == 0;
    }
}

// Generic class with constraints
class SortedList<T: Comparable> {
    private let items: T[] = [];

    fun add(this, item: T) {
        let index = this.findInsertionPoint(item);
        this.items.insert(index, item);
    }

    private fun findInsertionPoint(this, item: T) -> i32 {
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

    init(this) {
        this.handle = unsafe { allocate_memory(1024) };
    }

    deinit(this) {
        unsafe { free_memory(this.handle) };
    }
}
```

---

## Struct Declarations

Structs are value types with named fields. They do not support inheritance but can implement interfaces via standalone `impl` blocks.

```ebnf
StructDecl     ::= ModifierList 'struct' BindingIdent TypeParameters?
                   WhereClause?
                   '{' StructElement* '}'

StructElement  ::= ModifierList StructFieldDecl
                 | ModifierList MethodDecl
                 | ModifierList StructCtorDecl

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

    fun length(this) -> f64 {
        return sqrt(this.x * this.x + this.y * this.y);
    }

    fun normalize(this) -> Vector2D {
        let len = this.length();
        return Vector2D { x: this.x / len, y: this.y / len };
    }
}
```

### Struct with Constructor

```zom
struct Person {
    readonly name: str;
    readonly age: i32;
    mut email: str?;

    init(this, name: str, age: i32) {
        this.name = name;
        this.age = age;
        this.email = null;
    }

    init(this, name: str, age: i32, email: str) {
        this.name = name;
        this.age = age;
        this.email = email;
    }
}
```

---

## Interface Declarations

Interfaces define contracts that types can implement. They support inheritance
through the colon (`:`) syntax and may contain methods, accessors, and associated
type declarations.

```ebnf
InterfaceDecl  ::= ModifierList 'interface' BindingIdent TypeParameters?
                   InterfaceHeritage?
                   '{' InterfaceBody '}'

InterfaceHeritage ::= ':' InterfaceBoundList   (* super-interfaces *)
InterfaceBoundList ::= InterfaceBound ( '+' InterfaceBound )*
                       (* '+' = conjunction (AND); '|' is ONLY for UnionType *)
InterfaceBound     ::= QualifiedPathOrIdent ( '<' TypeArgumentList '>' )?
QualifiedPathOrIdent ::= PathSegment ( '::' PathSegment )*

InterfaceBody   ::= InterfaceElement*
InterfaceElement ::= ModifierList 'fun' MethodSignature ';'
                  | ModifierList ('get' | 'set') PropertySignature ';'
                  | ModifierList 'type' Identifier TypeParameters?
                    ( ':' InterfaceBoundList )? ( '=' TypeExpr )? ';'

PropertySignature ::= PropertyName MemberFunctionSignature
MethodSignature   ::= PropertyName CallSignature
CallSignature     ::= TypeParameters? MemberFunctionSignature
```

### Basic Interface

```zom
interface Drawable {
    fun draw(this);
    fun getBounds(this) -> Rectangle;
}
```

### Interface with Properties

```zom
interface Named {
    get name(this) -> str;
    readonly get id(this) -> i64;
}
```

### Interface with Methods and Properties

```zom
interface Shape {
    readonly get area(this) -> f64;
    readonly get perimeter(this) -> f64;

    fun scale(this, factor: f64);
    fun contains(this, point: Point) -> bool;
}
```

### Generic Interface

```zom
interface Container<T> {
    fun add(this, item: T);
    fun remove(this, item: T) -> bool;
    fun contains(this, item: T) -> bool;
    fun size(this) -> i32;
}
```

### Interface Inheritance

Interface inheritance uses the colon (`:`) syntax with `+` for multiple super-interfaces:

```zom
// Single super-interface
interface ColoredShape: Shape {
    get color(this) -> Color;
    fun changeColor(this, newColor: Color);
}

// Multiple super-interfaces (conjunction via '+')
interface NamedShape: Named + Shape {
    fun getDisplayName(this) -> str;
}
```

### Interface Methods

```zom
interface Configurable {
    fun configure(this, options: ConfigOptions);
    fun reset(this);
}
```

### Interface with Associated Types

```zom
interface Iterator {
    type Item;   // Associated type

    fun next(this) -> Self::Item?;
    fun hasNext(this) -> bool;
}

interface Collection {
    type Element;
    type Iter: Iterator;

    fun iter(this) -> Self::Iter;
    fun count(this) -> i32;
}
```

## Enum Declarations

Enums define tagged union types. Each variant is either a unit variant or a
tuple variant and may carry an explicit constant discriminant expression.

```ebnf
EnumDecl       ::= ModifierList 'enum' BindingIdent TypeParameters?
                   '{' EnumBody? '}'
EnumBody       ::= EnumVariant ( ',' EnumVariant )* ','?
EnumVariant    ::= Identifier
                   ( '(' VariantTypeList ')' )?   (* tuple associated value *)
                   ( '=' ConstExpression )?       (* explicit discriminant *)
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

### Enum with Multiple Associated Values

```zom
enum WebEvent {
    PageLoad,
    KeyPress(char),
    Click(i32, i32),
    Scroll(f64, f64),
}
```

---

## Error Declarations

Error types provide structured error handling with custom error types that can carry additional context information. They work with the `raises` clause and `match`/`is` patterns.

```ebnf
ErrorDecl      ::= ModifierList 'error' BindingIdent '{' ErrorBody? '}'
ErrorBody      ::= StructElement*
```

There is no `throw` keyword (Principle P3: Explicit Error Flow). Errors are returned as values and handled via pattern matching.

### Simple Error Type

```zom
error NetworkError {
    message: str;
}
```

### Error with Multiple Fields

```zom
error ValidationError {
    field: str;
    message: str;
    code: i32;
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
alias Optional<T> = T | null;
```

The `Result<T, E>` example in this chapter is the nominal enum declared in the
enum section. It is not a type alias and does not acquire the checked
error-union role used by raising calls. Code handles that declaration through
its `Success` and `Failure` variants. The identifier `Result` is not reserved;
other declarations with that name follow ordinary scope and module rules.

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

An ordinary impl attaches one behavior interface implementation to one target
type. A marker impl publishes one concrete marker fact for one closed target
type.

```ebnf
ImplDecl       ::= StandaloneImplDecl
                 | MarkerImplDecl

StandaloneImplDecl ::= UnsafePrefix? 'impl' TypeParameters? InterfaceBound 'for' TypeExpr
                       WhereClause? '{' ImplMember* '}'
    (* 'impl' is a SOFT keyword — recognized only at impl-head position *)

ImplMember     ::= ModifierList 'fun' BindingIdent TypeParameters?
                    MemberFunctionSignature ( ';' | BlockStatement )
                 | 'type' Identifier TypeParameters? '=' TypeExpr ';'
                 | 'mut' VariableDeclList ';'
                 | 'let' VariableDeclList ';'
                 | 'const' ConstDeclList ';'

MarkerImplDecl ::= PositiveMarkerImplDecl | NegativeMarkerImplDecl
PositiveMarkerImplDecl ::= UnsafePrefix? 'impl' MarkerImplPath 'for' ClosedTypeExpr ';'
NegativeMarkerImplDecl ::= 'impl' '!' MarkerImplPath 'for' ClosedTypeExpr ';'
MarkerImplPath ::= AttrPath | BindingIdent
ClosedTypeExpr ::= TypeExpr
    (* ClosedTypeExpr contains no impl-owned or unresolved type parameter. *)
```

Each ordinary declaration names exactly one interface and always has an impl
body. Type parameters and a `where` clause belong only to ordinary impls.
Marker declarations have no type parameters, `where` clause, associated
bindings, members, or body.

### Standalone Interface Impl

```zom
interface Drawable {
    fun draw(this);
    fun getBounds(this) -> Rectangle;
}

class Button {
    let position: Point;
    let size: Size;
    let text: str;

    init(this, position: Point, size: Size, text: str) {
        this.position = position;
        this.size = size;
        this.text = text;
    }
}

impl Drawable for Button {
    public fun draw(this) {
        print("Drawing button: " + this.text);
    }

    public fun getBounds(this) -> Rectangle {
        return Rectangle(this.position, this.size);
    }
}
```

### Impl with Associated Types

```zom
interface Iterator {
    type Item;
    fun next(this) -> Self::Item?;
    fun hasNext(this) -> bool;
}

struct VecIter<T> {
    readonly data: T[];
    mut index: i32;

    init(this, data: T[]) {
        this.data = data;
        this.index = 0;
    }
}

impl<T> Iterator for VecIter<T> {
    type Item = T;

    fun next(this) -> T? {
        if (this.index >= this.data.length) return null;
        let value = this.data[this.index];
        this.index += 1;
        return value;
    }

    fun hasNext(this) -> bool {
        return this.index < this.data.length;
    }
}
```

### Marker Impl

```zom
// Positive marker assertion
unsafe impl std::marker::Sendable for MyType;

// Negative marker fact
impl !std::marker::Shared for MyType;

// Short marker paths use the same bodyless form
unsafe impl Linear for FileHandle;
```

A positive marker assertion requires `unsafe`, but the parser retains a
positive candidate without that prefix so signature checking can classify the
resolved interface before enforcing safety. A negative marker fact cannot use `unsafe`. For every
short or qualified positive path, the final semicolon identifies a marker
candidate and an opening implementation body identifies an ordinary impl. The
checker then validates that the resolved interface has marker shape.

The parser rejects source-shape violations with these diagnostics:

| Source failure | Diagnostic |
|---|---|
| `+` after an ordinary impl interface | `ZOM2100 ImplRequiresSingleInterface` |
| type parameters on a marker impl | `ZOM2101 MarkerImplCannotBeGeneric` |
| a `where` clause on a marker impl | `ZOM2102 MarkerImplCannotHaveWhereClause` |
| `unsafe` on a negative marker impl | `ZOM2103 NegativeMarkerImplCannotBeUnsafe` |
| a body on a marker impl | `ZOM2104 MarkerImplCannotHaveBody` |

A bodyless positive candidate targeting a behavior interface emits
`ZOM4089 BehaviorInterfaceRequiresImplBody`. Only a candidate that resolves to
a marker-only interface and lacks `unsafe` emits
`ZOM4091 PositiveMarkerImplRequiresUnsafe`. A marker declaration whose target
contains an impl-owned or unresolved type parameter publishes no marker fact.

---

## Extern Declarations

Foreign function interface (FFI) declarations provide bindings to code written in other languages.

```ebnf
ExternDecl     ::= 'extern' AbiLiteral? ExternBlock
    (* 'extern' is a SOFT keyword *)

AbiLiteral     ::= '"' ('C' | 'Cdecl' | 'system' | 'zom-cdecl') '"'

ExternBlock    ::= '{' ExternItem* '}'
ExternItem     ::= 'fun' Identifier FunctionSignature ';'  (* external function *)
                 | 'variable' Identifier ':' TypeExpr ';'  (* external variable *)
```

### External Function Binding

```zom
extern "C" {
    fun puts(message: str) -> i32;
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

## Module Declarations

A source file may begin with one `module` declaration. When present, it must be
the first source item after the shebang and outer attributes. The declared name
is one identifier; module paths use `::` only in the alias target.

```ebnf
ModuleDeclaration ::=
    'module' Identifier ';'
  | 'module' Identifier '{' ModuleItem* '}'
  | 'export'? 'module' Identifier '=' ModuleAliasPath ';'

ModuleAliasPath ::= Identifier ('::' Identifier)+
```

```zom
module auth;

module auth {
    export fun validate() -> bool { true }
}

module utils = myapp::utilities::common;

export module math = myapp::math::core;
```

See [Ch.13 Modules and Imports](13-modules-and-imports.md) for the full module system specification.

---

## Declaration Order and Scoping

- Declarations at module scope are hoisted within that module; the order of top-level declarations does not affect name resolution.
- A named function declared directly in a block is hoisted within that immediate
  block. Its name is visible from the start of the block through the end of the
  block, including before the declaration. It is neither a module member nor an
  export-surface entry.
- A block-scoped named function is a callable boundary. Its body cannot capture
  parameters, local values, or pattern bindings from an enclosing callable or
  block; those values must be passed explicitly. Closure expressions provide
  lexical capture.
- Local value declarations (`mut`, `let`, `const`) inside block bodies follow lexical scoping and are not hoisted.
- Forward references to functions, types, and classes at module scope are permitted.
- Forward references to local `let`/`mut` bindings are not permitted; definite-assignment rules apply.

## Attributes on Declarations

An outer attribute list may prefix a module-item declaration, including an
import, export, value declaration, function, named type, alias, standalone impl,
or extern block. The parser retains the list on the
containing `StatementListItem`. The module declaration itself cannot carry an
attribute because it occupies the source-file header rather than the module-item
list.

Attributes are not accepted on type members or enum variants because those AST
nodes have no attribute storage. See Chapter 16 for the complete placement
matrix.

The exact path `zom::cfg` is rejected because the compiler has no conditional
selection phase. Generic qualified attribute acceptance does not imply code
generation hooks or source removal.
