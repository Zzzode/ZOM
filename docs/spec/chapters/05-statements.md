# Statements

Statements are the building blocks of program execution. They perform actions but do not produce values (unlike expressions).

## Statement Categories

1. **Simple Statements**: Expression statements, empty statements
2. **Declaration Statements**: Variable, function, class declarations
3. **Control Flow Statements**: Conditional, loop, jump statements
4. **Compound Statements**: Block statements

## Simple Statements

### Expression Statements

Any expression can be used as a statement by adding a semicolon:

```zom
print("Hello, World!");     // Function call statement
x = y + z;                  // Assignment statement
array.push(newElement);     // Method call statement
++counter;                  // Increment statement
```

### Empty Statement

An empty statement consists of just a semicolon:

```zom
;  // Empty statement

// Sometimes useful in loops
for (let i = 0; i < 10; ++i) ;
```

## Block Statements

Block statements group multiple statements together:

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

## Control Flow Statements

### `if` Statements

Conditional execution based on boolean expressions:

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

// Single-line if (no braces needed for single statements)
if (debug) print("Debug mode enabled");
```

### `match` Statements

Powerful pattern matching for complex conditional logic:

```zom
// Basic match statement
match (value) {
    when 1 { print("One"); }
    when 2 { print("Two"); }
    when 3 { print("Three"); }
    default { print("Other"); }
}

// Match with expressions
let result = match (operation) {
    when "add" => a + b
    when "subtract" => a - b
    when "multiply" => a * b
    when "divide" => a / b
    default => 0
};

// Match with guards
match (number) {
    when x if x > 0 => print("Positive")
    when x if x < 0 => print("Negative")
    when 0 => print("Zero")
}

// Match with type patterns
match (value) {
    when str => print("String: " + value)
    when i32 => print("Integer: " + value.toString())
    when bool => print("Boolean: " + value.toString())
    default => print("Unknown type")
}

// Match with destructuring
match (point) {
    when (0, 0) => print("Origin")
    when (x, 0) => print("On X-axis at " + x)
    when (0, y) => print("On Y-axis at " + y)
    when (x, y) => print("Point at (" + x + ", " + y + ")")
}
```

### Loop Statements

#### `while` Loops

```zom
// Basic while loop
let i = 0;
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

#### `for` Loops

```zom
// C-style for loop
for (let i = 0; i < 10; ++i) {
    print(i);
}

// For loop with multiple variables
for (let i = 0, j = 10; i < j; ++i, --j) {
    print("i: " + i + ", j: " + j);
}

// For loop with complex initialization and update
for (let node = head; node != nil; node = node.next) {
    processNode(node);
}

// Empty for loop components
for (;;) {  // Infinite loop
    if (shouldBreak()) break;
    doWork();
}
```

#### `for-in` Loops (Enhanced for loops)

```zom
// Iterate over array elements
let numbers = [1, 2, 3, 4, 5];
for (let number in numbers) {
    print(number);
}

// Iterate over string characters
for (let char in "hello") {
    print(char);
}

// Iterate over object properties
let person = { name: "Alice", age: 30 };
for (let key in person) {
    print(key + ": " + person[key]);
}

// Iterate with index
for (let (index, value) in numbers.enumerate()) {
    print("Index " + index + ": " + value);
}
```

#### `do-while` Loops

```zom
// Execute at least once
do {
    let input = readInput();
    processInput(input);
} while (input != "quit");
```

### Jump Statements

#### `break` Statement

Exits the nearest enclosing loop or match statement:

```zom
// Break from loop
for (let i = 0; i < 100; ++i) {
    if (i == 50) break;
    print(i);
}

// Labeled break (break from nested loops)
outer: for (let i = 0; i < 10; ++i) {
    for (let j = 0; j < 10; ++j) {
        if (i * j > 20) break outer;
        print("(" + i + ", " + j + ")");
    }
}
```

#### `continue` Statement

Skips the rest of the current loop iteration:

```zom
// Skip even numbers
for (let i = 0; i < 10; ++i) {
    if (i % 2 == 0) continue;
    print(i); // Only prints odd numbers
}

// Labeled continue
outer: for (let i = 0; i < 5; ++i) {
    for (let j = 0; j < 5; ++j) {
        if (j == 2) continue outer;
        print("(" + i + ", " + j + ")");
    }
}
```

#### `return` Statement

Exits a function and optionally returns a value:

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
    if (b == 0.0) return nil;
    return a / b;
}
```

#### `throw` Statement

Throws an exception:

```zom
// Throw with error object
fun validateAge(age: i32) {
    if (age < 0) {
        throw ValidationError("Age cannot be negative");
    }
    if (age > 150) {
        throw ValidationError("Age seems unrealistic");
    }
}

// Throw with string
fun notImplemented() {
    throw "This feature is not yet implemented";
}
```


## Labeled Statements

Statements can be labeled for use with break and continue:

```zom
// Label a loop
mainLoop: while (true) {
    let input = readInput();

    innerLoop: for (let i = 0; i < input.length; ++i) {
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

## Debugger Statement

Triggers debugger breakpoint:

```zom
fun complexCalculation(x: f64) -> f64 {
    let intermediate = x * 2;
    debugger; // Breakpoint here
    return intermediate + 10;
}
```