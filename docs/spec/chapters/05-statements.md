# Statements

Statements are the building blocks of program execution. They perform actions but do not produce values (unlike expressions). Every statement in ZOM terminates with a semicolon or is a block-delimited construct.

## Statement Categories

1. **Simple Statements**: Expression statements, empty statements
2. **Declaration Statements**: `let`, `mut`, `const` bindings in statement position
3. **Block Statements**: Grouped statements with lexical scope
4. **Control Flow Statements**: `if`, `match`, `when`, loops
5. **Jump Statements**: `break`, `continue`, `return`
6. **Concurrency Statements**: `spawn`, `suspend`
7. **Unsafe Block**: `unsafe { }` granting unsafe operation capability (expression form)
8. **Debugger Statement**: `debugger;`

```ebnf
Statement ::= BlockStatement
            | EmptyStatement
            | VariableStatement
            | Declaration
            | ExpressionStatement
            | IfStatement
            | MatchStatement
            | WhenStatement
            | WhileStatement
            | DoWhileStatement
            | ForStatement
            | ForInStatement
            | ContinueStatement
            | BreakStatement
            | ReturnStatement
            | SpawnStatement
            | SuspendStatement
            | DebuggerStatement
            | LabeledStatement
```

## Simple Statements

### Expression Statements

Any expression can be used as a statement by appending a semicolon. The expression's value is discarded.

```ebnf
ExpressionStatement ::= Expression ';'
```

The first token of the expression MUST NOT be `{`, `class`, `struct`, `enum`, `mut`, `let`, `const`, `fun`, `interface`, `error`, `alias`, or `module` to avoid ambiguity with declarations and block statements.

```zom
print("Hello, World!");     // Function call statement
x = y + z;                  // Assignment statement
array.push(newElement);     // Method call statement
++counter;                  // Increment statement
```

### Empty Statement

An empty statement consists of a lone semicolon. It performs no action.

```ebnf
EmptyStatement ::= ';'
```

```zom
;  // Empty statement

// Sometimes useful in loops
for (mut i = 0; i < 10; ++i) ;
```

## Declaration Statements

Value declarations (`let`, `mut`, `const`) may appear in statement position inside block bodies. They introduce new bindings into the enclosing scope.

```ebnf
VariableStatement ::= 'mut' VariableDeclarationList ';'
                    | 'let' VariableDeclarationList ';'
                    | 'const' ConstDeclarationList ';'

VariableDeclarationList ::= VariableDecl ( ',' VariableDecl )* ','?
VariableDecl            ::= Pattern ( ':' TypeExpr )? ( '=' Expression )?

ConstDeclarationList    ::= ConstItem ( ',' ConstItem )* ','?
ConstItem               ::= Identifier ( ':' TypeExpr )? '=' Expression
```

See [Ch.06 Declarations](06-declarations.md) for full semantics of `let`, `mut`, and `const`.

```zom
{
    let x = 10;           // Immutable binding
    mut y = 20;           // Mutable binding
    const PI = 3.14159;   // Compile-time constant

    y = x + 30;           // OK: reassign mutable binding
    // x = 5;              // Error: cannot reassign let binding
}
```

## Block Statements

Block statements group zero or more statements together and create a new lexical scope.

```ebnf
BlockStatement ::= '{' StatementList? '}'
StatementList  ::= Statement+
```

```zom
{
    let x = 10;
    let y = 20;
    print(x + y);
}

// Blocks create new scope
{
    let localVar = "I'm local";
    print(localVar);
}
// localVar is not accessible here
```

Blocks may appear as statements anywhere a statement is expected. They are also used as the body of functions, `if` branches, loop bodies, etc.

## Control Flow Statements

### `if` Statements

Conditional execution based on a boolean expression.

```ebnf
IfStatement ::= 'if' '(' Expression ')' Statement ( 'else' Statement )?
```

```zom
// Basic if statement
if (condition) {
    doSomething();
}

// if-else statement
if (score >= 90) {
    grade = "A";
} else {
    grade = "B";
}

// if-else if-else chain
if (temperature > 30) {
    print("Hot");
} else if (temperature > 20) {
    print("Warm");
} else if (temperature > 10) {
    print("Cool");
} else {
    print("Cold");
}

// Single-statement body (no braces needed for single statements)
if (debug) print("Debug mode enabled");
```

The condition expression must have type `bool`. No implicit conversion from numeric or pointer types is performed.

### `match` Statements

Pattern matching for complex conditional logic. Each clause uses `=>` (rocket) to separate the pattern from the body.

```ebnf
MatchStatement ::= 'match' '(' Expression ')' '{' MatchClause* DefaultClause? '}'
MatchClause    ::= 'when' Pattern GuardClause? '=>' Statement
DefaultClause  ::= 'default' '=>' StatementList
GuardClause    ::= 'if' Expression
```

```zom
// Basic match statement
match (value) {
    when 1 => print("One");
    when 2 => print("Two");
    when 3 => print("Three");
    default => print("Other");
}

// Match with block body
match (operation) {
    when "add" => {
        let result = a + b;
        print(result);
    }
    when "subtract" => {
        let result = a - b;
        print(result);
    }
    default => print("Unknown operation");
}

// Match with guards
match (number) {
    when x if x > 0 => print("Positive");
    when x if x < 0 => print("Negative");
    when 0 => print("Zero");
}

// Match with type patterns
match (value) {
    when str => print("String: " + value);
    when i32 => print("Integer: " + value.toString());
    when bool => print("Boolean: " + value.toString());
    default => print("Unknown type");
}

// Match with destructuring
match (point) {
    when (0, 0) => print("Origin");
    when (x, 0) => print("On X-axis at " + x);
    when (0, y) => print("On Y-axis at " + y);
    when (x, y) => print("Point at (" + x + ", " + y + ")");
}
```

The scrutinee expression (in parentheses after `match`) is evaluated once, then matched against each `when` clause in order. The first clause whose pattern matches (and whose guard evaluates to `true`, if present) is executed. If no clause matches, the `default` clause runs; if there is no `default`, a compile-time exhaustiveness error is reported.

See [Ch.07 Patterns](07-patterns.md) for the full pattern syntax.

### `when` Statements

The `when` statement provides Kotlin-style branching where each clause matches an *expression value* (not a pattern) against the scrutinee using `==`. It is distinct from `match`: `when` uses `:` separators and expression-based clauses, while `match` uses `=>` and pattern-based clauses.

```ebnf
WhenStatement ::= 'when' '(' Expression ')' '{' WhenClause* ('default' ':' StatementList)? '}'
WhenClause    ::= Expression ':' StatementList
```

```zom
// Basic when statement
when (day) {
    1: print("Monday");
    2: print("Tuesday");
    3: print("Wednesday");
    4: print("Thursday");
    5: print("Friday");
    default: print("Weekend");
}

// When with expression ranges (via function calls)
when (score) {
    inRange(90, 100): print("A");
    inRange(80, 89):  print("B");
    inRange(70, 79):  print("C");
    inRange(60, 69):  print("D");
    default:          print("F");
}
```

Each `when` clause expression is evaluated and compared to the scrutinee using `==`. The first matching clause's statement list is executed. If none match, `default` runs; without `default`, a compile-time exhaustiveness check applies.

### `while` Loops

```ebnf
WhileStatement ::= 'while' '(' Expression ')' Statement
```

```zom
// Basic while loop
mut i = 0;
while (i < 10) {
    print(i);
    ++i;
}

// While loop with complex condition
while (hasMoreData() && !shouldStop) {
    processNextItem();
}

// Infinite loop (use with break)
while (true) {
    let input = readInput();
    if (input == "quit") break;
    processInput(input);
}
```

The condition is evaluated before each iteration. If it is `false`, the loop terminates.

### `do-while` Loops

```ebnf
DoWhileStatement ::= 'do' Statement 'while' '(' Expression ')' ';'
```

```zom
// Execute at least once
mut input: str;
do {
    input = readInput();
    processInput(input);
} while (input != "quit");
```

The body is executed once before the condition is evaluated. The condition is then checked after each iteration; if `false`, the loop terminates.

### `for` Loops (C-style)

```ebnf
ForStatement ::= 'for' '(' ForInit? ';' Expression? ';' ForUpdate? ')' Statement
ForInit      ::= 'mut' VariableDeclarationList
               | 'let' VariableDeclarationList
               | ExpressionList
ForUpdate    ::= ExpressionList
```

```zom
// C-style for loop
for (mut i = 0; i < 10; ++i) {
    print(i);
}

// For loop with multiple variables
for (mut i = 0, j = 10; i < j; ++i, --j) {
    print("i: " + i + ", j: " + j);
}

// For loop with complex initialization and update
for (mut node = head; node != null; node = node.next) {
    processNode(node);
}

// Empty for loop components (infinite loop)
for (;;) {
    if (shouldBreak()) break;
    doWork();
}
```

The `init` part is executed once before the loop begins. The `condition` is evaluated before each iteration; if `false`, the loop terminates. The `update` part is evaluated after each iteration.

### `for-in` Loops

`for-in` iterates over values produced by an iterable or iterator expression. It does not enumerate object property names.

```ebnf
ForInStatement ::= 'for' '(' ('mut' | 'let')? Pattern 'in' Expression ')' Statement
```

```zom
// Iterate over array values
let numbers = [1, 2, 3, 4, 5];
for (let number in numbers) {
    print(number);
}

// Iterate over string characters
for (let char in "hello") {
    print(char);
}

// Iterate over map entries
let person = { name: "Alice", age: 30 };
for (let entry in person.entries()) {
    print(entry.key + ": " + entry.value);
}

// Iterate with index
for (let entry in numbers.enumerate()) {
    print("Index " + entry.index + ": " + entry.value);
}

// Mutable pattern binding
for (mut item in collection) {
    item.modify();
}
```

The expression after `in` must implement the iterator protocol. Each iteration, the next value is bound to the pattern.

## Jump Statements

### `break` Statement

Exits the nearest enclosing loop or `match` statement.

```ebnf
BreakStatement ::= 'break' Identifier? ';'
```

```zom
// Break from loop
for (mut i = 0; i < 100; ++i) {
    if (i == 50) break;
    print(i);
}

// Labeled break (break from nested loops)
outer: for (mut i = 0; i < 10; ++i) {
    for (mut j = 0; j < 10; ++j) {
        if (i * j > 20) break outer;
        print("(" + i + ", " + j + ")");
    }
}
```

Without a label, `break` exits the innermost enclosing `while`, `do-while`, `for`, `for-in`, or `match`. With a label, it exits the labeled statement.

### `continue` Statement

Skips the rest of the current loop iteration and proceeds to the next iteration check.

```ebnf
ContinueStatement ::= 'continue' Identifier? ';'
```

```zom
// Skip even numbers
for (mut i = 0; i < 10; ++i) {
    if (i % 2 == 0) continue;
    print(i); // Only prints odd numbers
}

// Labeled continue
outer: for (mut i = 0; i < 5; ++i) {
    for (mut j = 0; j < 5; ++j) {
        if (j == 2) continue outer;
        print("(" + i + ", " + j + ")");
    }
}
```

Without a label, `continue` applies to the innermost enclosing loop. With a label, it applies to the labeled loop.

### `return` Statement

Exits a function and optionally returns a value.

```ebnf
ReturnStatement ::= 'return' Expression? ';'
```

```zom
// Return with value
fun add(a: i32, b: i32) -> i32 {
    return a + b;
}

// Return without value (unit type)
fun printMessage(msg: str) {
    print(msg);
    return; // Optional for unit-returning functions
}

// Early return
fun divide(a: f64, b: f64) -> f64? {
    if (b == 0.0) return null;
    return a / b;
}
```

If the function has a declared return type, the expression (if present) must be assignable to that type. A bare `return` (no expression) is valid only in functions returning `()`.

## Labeled Statements

Statements can be labeled for use with `break` and `continue`.

```ebnf
LabeledStatement ::= Identifier ':' Statement
```

Labels may only prefix control-flow or block statements. Declarations (`let`, `mut`, `const`, `fun`, `class`, `struct`, `interface`, `enum`, `error`, `alias`, `import`, `export`, `module`, `type`) must NOT be labeled (enforced by constraint C25#2).

```zom
// Label a loop
mainLoop: while (true) {
    let input = readInput();

    innerLoop: for (mut i = 0; i < input.length; ++i) {
        if (input[i] == 'q') break mainLoop;
        if (input[i] == 's') continue mainLoop;
        processCharacter(input[i]);
    }
}

// Label a block
validation: {
    if (!isValidEmail(email)) break validation;
    if (!isValidPassword(password)) break validation;

    // Validation passed
    createAccount(email, password);
}
```

Outer attributes (`#[...]`) are not allowed immediately after a label.

## Concurrency Statements

ZOM uses a **zero-color** concurrency model. There is no `async`/`await`. Instead, two keywords manage task suspension and spawning: `suspend` and `spawn`.

### `spawn` Statement

`spawn` creates a new asynchronous task. In statement form, the `TaskHandle<T>` result is discarded.

```ebnf
SpawnStatement     ::= 'spawn' SpawnModifierList? ( SpawnBlockBody | Expression ) ';'?
SpawnModifierList  ::= SpawnModifier ( ','? SpawnModifier )*
SpawnModifier      ::= 'detached'
                     | 'blocking'
                     | 'priority' '(' ( 'high' | 'low' ) ')'
SpawnBlockBody     ::= '{' StatementList Expression? '}'
```

```zom
// Spawn a block body
spawn {
    let result = heavy_computation();
    report_result(result);
}

// Spawn with modifiers
spawn detached, priority(high) {
    background_task();
}

// Spawn a blocking task (runs on the blocking thread pool)
spawn blocking {
    let data = read_file_sync(path);
    process(data);
}

// Spawn a function call
spawn compute_value();

// Spawn a closure
spawn fun() -> i32 use [x, y] {
    return x + y;
};
```

Semantics:
- **Default `spawn`**: The body's immediate enclosing scope must be inside an active `Scope` (provided by `spawn_scope` or the runtime root scope). The body is enqueued before `spawn` returns (eager task principle).
- **`spawn detached`**: All captures must be `'static`; not bound to any scope; exiting without join triggers lint ZOM8008.
- **`spawn blocking`**: The body is dispatched to the blocking thread pool and does not consume an M:N worker.
- **Capture checking**: Ownership transfers across a spawn-closure boundary must satisfy `Sendable`; shared references must satisfy `Shared`; violation produces compile error ZOM8001.

### `suspend` Statement

`suspend` removes the current task from the run queue and waits for a suspend event to become ready before resuming.

```ebnf
SuspendStatement ::= 'suspend' ( ';'
                               | 'until' Expression ';' )
```

```zom
// Yield the current task (suspend without a specific event)
suspend;

// Suspend until a specific event is ready
suspend until timer.duration(1000);

// Suspend until an I/O event
suspend until socket.readable();
```

Semantics:
1. Evaluate the `until` expression to yield an event object `ev` (must be a `SuspendEvent<T>` or implement `SuspendEventContract<T>`).
2. Inject the current task's waker into `ev.waker`.
3. Register `ev` with the appropriate reactor.
4. The current task yields the worker.
5. When the event becomes ready or is canceled, the reactor invokes the waker and the task re-enters the runnable queue.

The statement form `suspend until expr;` discards the event result. To obtain the result, use the expression form `let result = suspend until expr;` (expression form is a spec requirement; parser support is tracked separately).

## Unsafe Block Statement

An `unsafe` block grants the capability to perform operations that the compiler cannot prove safe. It does not disable type checking, borrow checking, or any other safe-language analysis -- it only enables the specific unsafe operations listed in [Ch.03 §Unsafe Safety Model](03-types.md).

```ebnf
UnsafeBlockStatement ::= 'unsafe' BlockStatement
```

> **Note**: In the current implementation, `unsafe { }` is parsed as `UnsafeBlockExpr` (an expression form). It appears as a statement when followed by `;` as an expression statement: `unsafe { raw_op(); };`. The bare `unsafe { stmt* }` without semicolon is not yet recognized as a standalone statement form.

```zom
// Basic unsafe block
unsafe {
    let value = *raw_pointer;     // OK: raw pointer dereference
    extern_c_function();          // OK: calling extern "C" function
    unsafe_helper();              // OK: calling unsafe fun
};

// Outside unsafe block, these operations are errors
// let value = *raw_pointer;     // Error ZOM0901: RawPointerDerefOutsideUnsafe
// extern_c_function();         // Error ZOM0902: ExternCallOutsideUnsafe
```

An `unsafe` block:

1. Creates a new lexical scope (identical to a regular block statement).
2. Grants the capability to perform unsafe operations within its body.
3. Does NOT suppress diagnostics for safe operations -- type errors, borrow errors, etc. are still reported normally.
4. Should be as small as possible, wrapping only the specific unsafe operation.

### Nesting

Unsafe blocks may be nested. The inner block does not revoke any capabilities granted by the outer block:

```zom
unsafe {
    // Outer unsafe context
    let a = *ptr_a;  // OK

    unsafe {
        // Inner unsafe context (redundant but valid)
        let b = *ptr_b;  // Still OK
    }
};
```

### Best Practices

- **Minimize scope.** Place the `unsafe` block around only the operation that requires it, not the entire function.
- **Document the reason.** Add a comment explaining why the `unsafe` is sound:
  ```zom
  // SAFETY: ptr is guaranteed non-null by the caller contract
  let value = unsafe { *ptr };
  ```
- **Prefer safe wrappers.** Encapsulate unsafe operations in a function that provides a safe interface:
  ```zom
  fun safe_read(ptr: *const i32) -> i32? {
      if (ptr == null) return null;
      // SAFETY: we just checked for null
      return unsafe { *ptr };
  }
  ```

## Debugger Statement

Triggers a debugger breakpoint at the current execution point.

```ebnf
DebuggerStatement ::= 'debugger' ';'
```

```zom
fun complexCalculation(x: f64) -> f64 {
    let intermediate = x * 2;
    debugger; // Breakpoint here
    return intermediate + 10;
}
```

In non-debug builds, `debugger;` is a no-op.

## Reserved Syntax

The following keywords are reserved and produce specific diagnostic codes when used in statement position. They are not part of the current statement grammar.

| Keyword(s) | Diagnostic | Description |
|---|---|---|
| `throw`, `try`, `catch`, `finally` | ZOM5001 | Exception syntax not implemented. Use explicit error values, `raises` annotations, and pattern matching instead. |
| `async`, `await` | ZOM5002 | Async/await not implemented. ZOM uses the zero-color `suspend`/`spawn` model (see [Concurrency Statements](#concurrency-statements)). |
| `var` | ZOM5003 | `var` not implemented. Use `let`, `mut`, or `const` instead. |
| `actor`, `channel` | ZOM5004 | Actor/channel are library types, not language keywords. |
| `yield`, `generator` | ZOM5005 | Generator syntax not implemented. |
| `namespace`, `package` | ZOM5006 | Use module dotted paths instead. |
| `type` | ZOM5007 | Top-level `type` declaration not implemented. Use `alias` for type aliases. |
| `delete`, `instanceof`, `of`, `with` | ZOM5008 | JavaScript legacy syntax, not part of ZOM v1. Use `is` for type testing. |

```ebnf
ReservedStatement ::= 'throw' Expression? ';'?
                    | 'try' BlockStatement
                      ( 'catch' '(' Identifier ':'? TypeExpr ')' BlockStatement )*
                      ( 'finally' BlockStatement )?
                    | 'async' Statement
                    | 'await' Expression
                    | 'var' VariableDeclList ';'
                    | 'actor' Identifier TypeParameters? '{' ... '}'
                    | 'channel' TypeExpr
                    | 'yield' Expression?
                    | 'generator' Identifier '{' ... '}'
                    | 'namespace' Identifier '{' ... '}'
                    | 'package' Identifier '{' ... '}'
                    | 'type' Identifier TypeParameters? '=' TypeExpr ';'
                    | 'delete' Expression
                    | Expression 'instanceof' TypeExpr
                    | 'of' Expression
                    | 'with' '(' Expression ')' Statement
```

## Conditional Compilation Gates

Statements at module or block scope may be gated by `#[zom::cfg(...)]` attributes. However, the attribute can only gate a **standalone block** of the form `#[zom::cfg(...)] { stmt* }`. It cannot gate individual expressions, control-flow statements, or declaration-statement forms like `let`, `mut`, `for`, `if`, or `return`.

```zom
// OK: cfg gates a standalone block
#[zom::cfg(feature = "logging")] {
    log("debug info");
}

// Error ZOM1901: cannot gate a single let statement
// #[zom::cfg(feature = "logging")]
// let log_level = 3;

// Error ZOM1601: non-cfg attributes not allowed on statements
// #[deprecated]
// print("hello");
```

See [Ch.19 Conditional Compilation](19-conditional-compilation.md) for full `#[zom::cfg(...)]` syntax and semantics.
