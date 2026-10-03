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
            | LabeledStatement
```

## Simple Statements

### Expression Statements

```ebnf
ExpressionStatement ::= Expression ';'
```

The first token of the expression MUST NOT be `{`, `class`, `struct`, `enum`, `mut`, `let`, `const`, `fun`, `interface`, `error`, `alias`, or `module` to avoid ambiguity with declarations and block statements.

#### Admitted expression-statement shapes

The parser accepts the full `ExpressionStatement` production above. Semantic
lowering currently admits only the following expression-statement forms:

- A plain assignment (`=`) to a local binding, or to a field reached through a
  local binding or the method receiver (`this.value = x;`). The write value is a
  scalar literal, an identifier reference, or — for a local target only — an
  admitted primitive binary expression. A field write through a shared
  (non-`mutating`) receiver is rejected with `ZOM4126`.
- A method call on an identifier receiver with scalar literal arguments whose
  result is discarded (`cell.set(42);`), used for its effect.

Every other expression statement — a bare literal or identifier, a prefix or
postfix unary operation, a compound assignment, and a direct free-function call
(`g();`) — has no admitted semantic contract yet and is rejected with `ZOM4098`
(this expression statement is not supported yet).

```zom
x = y + z;                  // Assignment to a local
this.value = x;             // Field write through a mutating receiver
array.push(newElement);     // Discarded method call
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

A `match` can also appear in expression position, where each arm body is an expression and the match yields the selected arm's value. See [Ch.04 Match Expressions](04-expressions.md#match-expressions).

See [Ch.07 Patterns](07-patterns.md) for the full pattern syntax.

#### Admitted match shapes

The parser accepts the full `MatchStatement` grammar above. The ownership
surface admits only the following bounded match shapes for semantic lowering
today. The scrutinee must be a bare identifier (a parameter or local resolved
downstream), and every arm body must tail a scalar-literal `return` — either
`return <literal>;` directly or a block holding exactly that statement:

- **Bool scrutinee.** Two literal-pattern arms, one `when true =>` and one
  `when false =>`; or one literal-pattern arm plus one `default =>` arm.
- **Integer scrutinee.** One or more integer literal-pattern arms plus exactly
  one `default =>` arm covering the open integer domain.
- **Enum scrutinee.** Two qualified unit-variant pattern arms
  (`when Color.Red =>`, `when Color.Green =>`) on the same enum type.
- **Guard.** A guard is admitted only on the `when true` arm of a
  bool-with-default match, and must be a single binary expression whose one
  operand is a bare identifier and whose other operand is a scalar literal
  (`when true if (x > 0) =>`). The guard conjuncts the scrutinee condition,
  keeping the four-block diamond CFG.

Every other pattern form — identifier patterns, tuple, array, object, and type
patterns, string-literal patterns, and tuple-variant destructuring
(`when Success(value) =>`) — parses but has no admitted semantic contract yet
and is rejected with `ZOM4096` (control-flow syntax has no admitted semantic
contract). The bool shape lowers to the same conditional path as a bare-
parameter `if`, the integer shape to the equality conditional path, and the
enum shape to the equality conditional path with the variant discriminant as
the comparison literal.

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

#### Admitted ownership-surface while-loop shape

The parser accepts the full `while` grammar above. The ownership surface admits
only one narrow `while` shape for semantic lowering today: a `while` whose
condition is a bare identifier (a `bool` parameter or local resolved
downstream) and whose body is a block of one or more admitted loop-body writes,
optionally followed by one trailing unlabeled `break;` or `continue;`. A
loop-body write is an assignment `<ident> = <scalar-literal | identifier |
admitted primitive binary>;` targeting a local binding. For example:

```zom
fun spin(cond: bool) -> i32 {
    mut x = 0;
    while (cond) {
        x = x + 1;
    }
    return x;
}
```

This shape lowers end-to-end through semantic HIR into a reducible four-block
Built MIR loop: a header block whose `SwitchInt` branches into the loop body on
a true condition and to the exit otherwise, a body block that carries the write
assignments before the back-edge Goto, and a trailing `break;` exits to the
loop exit instead of taking the back-edge. Every other `while` form — a
non-identifier condition, an empty body, a body containing anything other than
writes plus an optional trailing unlabeled `break;`/`continue;`, or a labeled
`break`/`continue` — has no admitted semantic contract yet and is rejected with
`ZOM4096` (control-flow syntax has no admitted semantic contract).

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

#### Admitted ownership-surface for-loop shape

The parser accepts the full `for` grammar above. The ownership surface admits
only one bounded C-style `for` shape for semantic lowering today:

```zom
for (let id = <scalar-literal>; <ident> <cmp> <ident|literal>; <ident> = <primitive-binary>) { ... }
```

- The `init` is a `let` declaration with exactly one declarator whose pattern
  is a bare identifier and whose initializer is a scalar literal.
- The `cond` is a binary comparison whose operands are each an identifier or a
  scalar literal, with at least one identifier operand.
- The `update` is an assignment (`=`) whose target is a bare identifier and
  whose value is an admitted primitive binary expression.
- The `body` is one of: an empty block; zero or more admitted loop-body writes
  (`<ident> = <scalar-literal | identifier | admitted primitive binary>;`)
  optionally followed by one trailing unlabeled `break;` or `continue;`; one
  leading if-guarded break `if (<ident|lit> <cmp> <ident|lit>) { break; }`
  followed by zero or more admitted loop-body writes; or a sole nested admitted
  `for` loop.

For example:

```zom
mut sum = 0;
for (let i = 0; i < 10; i = i + 1) {
    sum = sum + i;
}
```

The loop desugars downstream to `let id = <literal>; while (<ident> <cmp>
<literal>) { <body> <ident> = <binary>; }` and lowers to a reducible four-block
Built MIR CFG — a five-block CFG when the body leads with an if-guarded break,
and a seven-block CFG for the nested-loop shape. Every other `for` form — a
`mut` or expression initializer, a missing or non-comparison condition, a
non-assignment update, or a body statement outside the list above — has no
admitted semantic contract yet and is rejected with `ZOM4096`. `do-while` and
`for-in` loops likewise remain unadmitted and are rejected with `ZOM4096`.

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

Without a label, `break` exits the innermost enclosing `while`, `do-while`,
`for`, `for-in`, or `match` in the current callable. With a label, it exits the
active labeled statement selected by canonical identifier equality. Explicit
lookup considers only labels on the current AST ancestor chain, searches from
innermost to outermost, and stops at a function or closure boundary. It does not
consider forward labels, siblings, completed labels, or labels outside the
current labeled statement subtree. A failed explicit lookup does not fall back
to an unlabeled loop or `match` target.

#### Admitted break shape

The admitted slice is an unlabeled `break;` appearing as the trailing statement
of an admitted `while` or `for` loop body, or as the sole statement of the
`if` guard in an admitted if-guarded break. A `break;` outside an admitted loop
body, and every labeled `break <ident>;`, parses but has no admitted semantic
contract through the ownership surface yet and is rejected with `ZOM4096`
(control-flow syntax has no admitted semantic contract). The labeled-lookup
semantics above describes the intended contract, not the currently admitted
slice.

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

Without a label, `continue` applies to the innermost enclosing loop in the
current callable and does not target `match`. With a label, it applies only when
the selected active label ultimately prefixes a `while`, `do-while`, `for`, or
`for-in` statement. A label that ultimately prefixes a block is not a valid
`continue` target. Explicit lookup uses the same active-ancestor rules and
callable boundaries as `break` and never falls back to an unlabeled loop.

#### Admitted continue shape

The admitted slice is an unlabeled `continue;` appearing as the trailing
statement of an admitted `while` or `for` loop body. A `continue;` outside an
admitted loop body, and every labeled `continue <ident>;`, parses but has no
admitted semantic contract through the ownership surface yet and is rejected
with `ZOM4096` (control-flow syntax has no admitted semantic contract). The
labeled-lookup semantics above describes the intended contract, not the
currently admitted slice.

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
LabeledStatement ::= Identifier ':' LabelTarget
LabelTarget      ::= BlockStatement
                   | WhileStatement
                   | DoWhileStatement
                   | ForStatement
                   | ForInStatement
                   | LabeledStatement
```

Labels prefix `while`, `do-while`, classic `for`, iterator `for`, blocks, or
another label that ultimately prefixes one of those targets.

A label is active only while evaluating or executing its immediate target
statement. Nested labels therefore make each enclosing label active throughout
the final block or loop subtree, while a label ceases to be active as soon as
its statement completes. Function and closure bodies start independent label
lookup regions; a module-owned label is never visible inside a nested callable.

Each source module and each callable owns one flat label namespace. Label names
are compared after canonical identifier normalization. Two declarations with
the same canonical name in one owner are invalid even when they occur in
disjoint blocks or as nested labels. Labels do not shadow other labels in the
same owner. Label lookup never consults value, type, module, or attribute
namespaces.

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

The current frontend parses `spawn` expressions and `suspend` statements. This
section defines syntax and AST retention only. Chapter 15 defines the same
surface in detail.

### `spawn` Expression

```ebnf
SpawnExpression    ::= 'spawn' SpawnModifier* (SpawnBlockBody | AssignmentExpression)
SpawnModifier      ::= 'detached'
                     | 'blocking'
                     | 'priority' '(' ( 'high' | 'low' ) ')'
SpawnBlockBody     ::= BlockStatement
```

```zom
let background = spawn detached priority(low) { work(); };
let blocking = spawn blocking read_sync(path);
```

The AST stores modifier bits, priority, and a statement body. An expression
body is wrapped in an `ExpressionStatement`. The checker and runtime do not yet
assign task, capture, scheduling, or result semantics to this node.

### `suspend` Statement

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
suspend until ready;
```

The AST stores `Bare` or `Until` mode and the optional condition. `suspend` is
not an expression. No wake, cancellation, scheduler, or event-type semantics
are implemented.

## Unsafe Block Expression Statement

An `unsafe` block grants the capability to perform operations that the compiler cannot prove safe. It does not disable type checking, borrow checking, or any other safe-language analysis -- it only enables the specific unsafe operations listed in [Ch.03 §Unsafe Safety Model](03-types.md).

```ebnf
UnsafeBlockExpr ::= 'unsafe' BlockStatement
```

An unsafe block is an expression. It appears in statement position through an
expression statement.

```zom
// Basic unsafe block
unsafe {
    let value = *raw_pointer;
};

// Outside unsafe block, these operations are errors
// let value = *raw_pointer; // ZOM4069 RawPointerBoundaryRequiresUnsafe
```

An `unsafe` block:

1. Creates a block-expression lexical scope.
2. Grants the capability required by raw-pointer operations within its body.
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

## Reserved Syntax

The lexer reserves the following spellings, but the parser does not accept
them as statement or expression forms:

- `throw`, `try`, `catch`, and `finally`;
- `async` and `await`;
- `var`;
- `actor` and `channel`;
- `yield` and `generator`;
- `namespace` and `package`;
- `type` as a top-level declaration keyword; and
- `delete`, `instanceof`, `of`, and `with`.

Rejected uses emit registered parser diagnostics. Diagnostic identifiers are
owned by `diagnostics-parse.def`; this chapter does not allocate a separate
reserved-syntax diagnostic range.

## Statement Attributes

An outer attribute list may prefix any non-expression statement stored as a
`StatementListItem`. Qualified attributes are retained as syntax metadata; a
semantic effect exists only when a later phase explicitly recognizes the
attribute path. Bare expression statements cannot carry attributes.

```zom
// Accepted qualified metadata on a control-flow statement.
#[trace::branch]
if ready {
    log("ready");
}
```

The exact path `zom::cfg` is rejected because the compiler has no conditional
selection phase. See [Ch.16 Attributes](16-attributes-and-annotations.md) for
the complete placement and AST-retention contract.
