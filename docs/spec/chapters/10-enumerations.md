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

### Pattern Matching with Enums

```zom
fun processResult<T, E>(result: Result<T, E>) {
    match (result) {
        when Success(value) => {
            print("Operation succeeded with value: " + value.toString());
        }
        when Failure(error) => {
            print("Operation failed with error: " + error.toString());
        }
    }
}

fun handleMessage(message: Message) {
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
