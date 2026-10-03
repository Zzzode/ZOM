# Patterns

Patterns are used in match statements, variable declarations, and function
parameters to destructure and match values.

## Pattern Types

### Literal Patterns

Match exact values:

```zom
match (value) {
    when 42 => { print("The answer"); }
    when "hello" => { print("Greeting"); }
    when true => { print("Affirmative"); }
    when null => { print("Nothing"); }
}
```

### Identifier Patterns

Bind values to variables:

```zom
match (someValue) {
    when x => { print("Value is: " + x.toString()); }
}

// In variable declarations
let x = 42; // x is an identifier pattern
```

### Wildcard Pattern

Ignore values:

```zom
match (tuple) {
    when (_, y) => { print("Second element: " + y); }
    when (x, _) => { print("First element: " + x); }
}
```

### Tuple Patterns

Destructure tuples:

```zom
let point = (3.0, 4.0);
let (x, y) = point; // Destructuring assignment

match (point) {
    when (0.0, 0.0) => { print("Origin"); }
    when (x, 0.0) => { print("On X-axis"); }
    when (0.0, y) => { print("On Y-axis"); }
    when (x, y) => { print("Point at (" + x + ", " + y + ")"); }
}
```

### Array Patterns

Destructure arrays:

```zom
let numbers = [1, 2, 3, 4, 5];
let [first, second, ...rest] = numbers;

match (numbers) {
    when [] => { print("Empty array"); }
    when [x] => { print("Single element: " + x); }
    when [x, y] => { print("Two elements: " + x + ", " + y); }
    when [first, ...rest] => { print("First: " + first + ", rest: " + rest.length); }
}
```

### Object Patterns

Destructure objects:

```zom
let person = { name: "Alice", age: 30, city: "New York" };
let { name, age } = person;
let { name: personName, age: personAge } = person; // Renaming

match (person) {
    when { name: "Alice" } => { print("Hello Alice!"); }
    when { age: x } if x >= 18 => { print("Adult"); }
    when { name, age } => { print(name + " is " + age + " years old"); }
}
```

### Type Patterns

Match by type:

```zom
match (value) {
    when str => { print("String: " + value); }
    when i32 => { print("Integer: " + value.toString()); }
    when bool => { print("Boolean: " + value.toString()); }
    when Point => { print("Point at (" + value.x + ", " + value.y + ")"); }
}
```

### Guard Patterns

Add conditions to patterns:

```zom
match (number) {
    when x if x > 0 => { print("Positive"); }
    when x if x < 0 => { print("Negative"); }
    when 0 => { print("Zero"); }
}

match (person) {
    when { age: x } if x >= 65 => { print("Senior"); }
    when { age: x } if x >= 18 => { print("Adult"); }
    when { age: x } => { print("Minor"); }
}
```

### Enum Patterns

Match enum variants:

```zom
enum Result<T, E> {
    Success(T),
    Failure(E)
}

match (result) {
    when Success(value) => { print("Success: " + value); }
    when Failure(error) => { print("Error: " + error); }
}

enum WebEvent {
    Click { x: i32, y: i32 },
    KeyPress(char),
    PageLoad
}

match (event) {
    when Click { x, y } => { print("Clicked at (" + x + ", " + y + ")"); }
    when KeyPress(key) => { print("Key pressed: " + key); }
    when PageLoad => { print("Page loaded"); }
}
```

## Admitted Pattern Surface

The parser accepts the full pattern grammar above. The ownership surface
admits only the following bounded pattern forms through the semantic pipeline
today:

- **Bool and integer literal patterns** (`when true =>`, `when 42 =>`) in a
  `match` statement, subject to the admitted match shapes in
  [Ch.05 §Admitted match shapes](05-statements.md#admitted-match-shapes).
- **The wildcard pattern** as the `default =>` arm of an admitted `match`
  statement.
- **Qualified unit enum patterns** (`when Color.Red =>`) as the two arms of an
  admitted enum `match` statement.
- **The bounded match guard** — a single binary expression whose one operand is
  a bare identifier and whose other operand is a scalar literal — on the
  `when true` arm of a bool-with-default `match` statement.

Every other pattern form — identifier patterns in `match` position, tuple,
array, object, and type patterns, string-literal patterns, and tuple-variant
destructuring (`when Success(value) =>`) — parses but has no admitted semantic
contract yet and is rejected with `ZOM4096` (control-flow syntax has no
admitted semantic contract). Tuple-variant destructuring in particular requires
aggregate-value infrastructure the pipeline does not have yet; the
`Result::Ok(41)` construction form is admitted as a dead-erased binding (see
[Ch.10 §Tuple Variant Construction](10-enumerations.md#tuple-variant-construction)),
but destructuring a tuple variant in a `match` arm is not.
