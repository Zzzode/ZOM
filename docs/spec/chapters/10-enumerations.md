# Enumerations

Enumerations define types with a fixed set of named values, optionally with associated data.

### Simple Enums

```zom
enum Direction {
    North,
    South,
    East,
    West
}

enum Status {
    Pending = 0,
    InProgress = 1,
    Completed = 2,
    Failed = 3
}
```

### Discriminants

Every enum variant has an integer discriminant. A variant without an explicit
discriminant takes its zero-based source index within the enum (`North` is `0`,
`South` is `1`, and so on). An explicit discriminant is written
`Variant = <integer-literal>` and overrides the source index:

```zom
enum Color { Red = 10, Green = 20, Blue = 30 }
```

Only integer-literal discriminants are admitted. A general const-expression
discriminant (`Red = 1 + 9`) parses but has no admitted semantic contract yet
and is rejected with `ZOM4128` (explicit enum discriminants are not supported
yet).

### Enums with Associated Values

```zom
enum Result<T, E> {
    Success(T),
    Failure(E)
}

enum Option<T> {
    Some(T),
    None
}

enum Message {
    Text(str),
    Image(str, i32, i32),
    Video(str, f64),
    Audio(str, f64)
}
```

### Variant Value Expressions

A qualified unit-variant reference `EnumName::VariantName` is an expression
that yields the variant's discriminant as an integer constant:

```zom
enum Color { Red, Green, Blue }

fn entry() -> i32 {
    let c: Color = Color::Red;   // c holds the discriminant 0
    let r: i32 = 41;
    return r;
}
```

The base must be a bare identifier naming the enum type; the checker resolves
the binding and the variant discriminant (explicit when declared, otherwise the
zero-based source index). A qualified reference to a tuple variant
(`Result::Ok`) is a constructor path, not a value — see the next section.

### Tuple Variant Construction

A tuple variant is constructed by calling its qualified path with the variant
fields as arguments:

```zom
enum Result { Ok(i32), Err(i32) }

fn entry() -> i32 {
    let r: Result = Result::Ok(41);
    let a: i32 = 40;
    let answer: i32 = a + 1;
    return answer;
}
```

The construction is admitted today as a dead-erased binding: the constructed
value is stored into a local of the enum type but is never read, so the
erased local and its construction are skipped during lowering and only scalar
locals reach MIR/LIR. Reading a tuple-variant value back out (destructuring in
a `match` arm, field projection) has no admitted semantic contract yet.

### Pattern Matching with Enums

```zom
fn processResult<T, E>(result: Result<T, E>) {
    match (result) {
        when Success(value) => {
            print("Operation succeeded with value: " + value.toString());
        }
        when Failure(error) => {
            print("Operation failed with error: " + error.toString());
        }
    }
}

fn handleMessage(message: Message) {
    match (message) {
        when Text(content) => {
            print("Text message: " + content);
        }
        when Image(url, width, height) => {
            print("Image: " + url + " (" + width + "x" + height + ")");
        }
        when Video(url, duration) => {
            print("Video: " + url + " (" + duration + "s)");
        }
        when Audio(url, duration) => {
            print("Audio: " + url + " (" + duration + "s)");
        }
    }
}
```

The destructuring forms above (`when Text(content) =>`, `when Click { x, y } =>`)
parse but have no admitted semantic contract through the ownership surface yet.
The only admitted enum `match` shape today is two qualified unit-variant
pattern arms (`when Color.Red =>`, `when Color.Green =>`) on the same enum
type, as documented in [Ch.05 §Admitted match shapes](05-statements.md#admitted-match-shapes).
Tuple-variant and struct-variant destructuring are rejected with `ZOM4096`
(control-flow syntax has no admitted semantic contract) until the
aggregate-value infrastructure lands.
