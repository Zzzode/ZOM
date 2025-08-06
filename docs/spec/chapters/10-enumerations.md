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
    Image { url: str, width: i32, height: i32 },
    Video { url: str, duration: f64 },
    Audio { url: str, duration: f64 }
}
```

### Enum Methods

```zom
enum Color {
    Red = 0xFF0000,
    Green = 0x00FF00,
    Blue = 0x0000FF,

    fun toHex() -> str {
        return "#" + this.value.toString(16).padStart(6, '0');
    }

    fun toRgb() -> (u8, u8, u8) {
        let value = this.value;
        let r = (value >> 16) & 0xFF;
        let g = (value >> 8) & 0xFF;
        let b = value & 0xFF;
        return (r as u8, g as u8, b as u8);
    }
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
        when Image { url, width, height } => {
            print("Image: " + url + " (" + width + "x" + height + ")");
        }
        when Video { url, duration } => {
            print("Video: " + url + " (" + duration + "s)");
        }
        when Audio { url, duration } => {
            print("Audio: " + url + " (" + duration + "s)");
        }
    }
}
```
