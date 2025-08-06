# Patterns

Patterns are used in match expressions, variable declarations, and function parameters to destructure and match values.

## Pattern Types

### Literal Patterns

Match exact values:

```zom
match (value) {
    when 42 => print("The answer")
    when "hello" => print("Greeting")
    when true => print("Affirmative")
    when nil => print("Nothing")
}
```

### Identifier Patterns

Bind values to variables:

```zom
match (someValue) {
    when x => print("Value is: " + x.toString())
}

// In variable declarations
let x = 42; // x is an identifier pattern
```

### Wildcard Pattern

Ignore values:

```zom
match (tuple) {
    when (_, y) => print("Second element: " + y)
    when (x, _) => print("First element: " + x)
}
```

### Tuple Patterns

Destructure tuples:

```zom
let point = (3.0, 4.0);
let (x, y) = point; // Destructuring assignment

match (point) {
    when (0.0, 0.0) => print("Origin")
    when (x, 0.0) => print("On X-axis")
    when (0.0, y) => print("On Y-axis")
    when (x, y) => print("Point at (" + x + ", " + y + ")")
}
```

### Array Patterns

Destructure arrays:

```zom
let numbers = [1, 2, 3, 4, 5];
let [first, second, ...rest] = numbers;

match (numbers) {
    when [] => print("Empty array")
    when [x] => print("Single element: " + x)
    when [x, y] => print("Two elements: " + x + ", " + y)
    when [first, ...rest] => print("First: " + first + ", rest: " + rest.length)
}
```

### Object Patterns

Destructure objects:

```zom
let person = { name: "Alice", age: 30, city: "New York" };
let { name, age } = person;
let { name: personName, age: personAge } = person; // Renaming

match (person) {
    when { name: "Alice" } => print("Hello Alice!")
    when { age: x } if x >= 18 => print("Adult")
    when { name, age } => print(name + " is " + age + " years old")
}
```

### Type Patterns

Match by type:

```zom
match (value) {
    when str => print("String: " + value)
    when i32 => print("Integer: " + value.toString())
    when bool => print("Boolean: " + value.toString())
    when Point => print("Point at (" + value.x + ", " + value.y + ")")
}
```

### Guard Patterns

Add conditions to patterns:

```zom
match (number) {
    when x if x > 0 => print("Positive")
    when x if x < 0 => print("Negative")
    when 0 => print("Zero")
}

match (person) {
    when { age: x } if x >= 65 => print("Senior")
    when { age: x } if x >= 18 => print("Adult")
    when { age: x } => print("Minor")
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
    when Success(value) => print("Success: " + value)
    when Failure(error) => print("Error: " + error)
}

enum WebEvent {
    Click { x: i32, y: i32 },
    KeyPress(char),
    PageLoad
}

match (event) {
    when Click { x, y } => print("Clicked at (" + x + ", " + y + ")")
    when KeyPress(key) => print("Key pressed: " + key)
    when PageLoad => print("Page loaded")
}
```