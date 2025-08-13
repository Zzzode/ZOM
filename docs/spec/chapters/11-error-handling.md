# Error Handling

Zom provides robust error handling mechanisms through explicit error types with `raises`, optional values, and user-defined Result types. ZOM has no implicit error control flow - all errors are handled through explicit pattern matching.

### Native Error Types with `raises`

Zom's compiler natively supports error handling through the `raises` mechanism. Functions that can fail declare their error types using `raises`, and errors are handled through explicit pattern matching.

```zom
// Define error types
error DivisionByZeroError {
    message: str,
}

error FileNotFoundError {
    path: str,
}

// Function that can return errors
fun safeDivide(a: f64, b: f64) -> f64 raises DivisionByZeroError {
    if (b == 0.0) {
        return DivisionByZeroError("Cannot divide by zero");
    }
    return a / b;
}

// Handling errors with pattern matching
fun processCalculation() {
    let result = safeDivide(10.0, 2.0);
    match (result) {
        when DivisionByZeroError(error) => {
            print("Error: " + error.message);
            return 0.0;
        }
        when value: f64 => {
            print("Result: " + value.toString());
            return value;
        }
    }
}
```

### Optional Values

Optional values (`T?`) represent nullable data. Use chaining (`?.`) and coalescing (`??`) to handle them.

```zom
fun findUser(id: i64) -> User? {
    if (userExists(id)) return getUser(id);
    return null;
}

fun processUser(userId: i64) {
    let user = findUser(userId);
    let displayName = user?.name ?? "Anonymous";
    print("User: " + displayName);
}
```

### Error Propagation

Errors must be explicitly handled and propagated through pattern matching. Functions declare their error types using `raises`, and errors are propagated using `return`. There is no implicit error propagation - all error handling is explicit.

```zom
fun readConfigFile() -> Config raises FileNotFoundError | ParseError {
    let contentResult = readFile("config.json");
    match (contentResult) {
        when FileNotFoundError(error) => return error;
        when content: str => {
            let configResult = parseJson(content);
            match (configResult) {
                when ParseError(error) => return error;
                when config: Config => return validateConfig(config);
            }
        }
    }
}

fun initializeApp() {
    let configResult = readConfigFile();
    match (configResult) {
        when FileNotFoundError(error) => {
            print("Config file not found: " + error.path);
            startApp(getDefaultConfig());
        }
        when ParseError(error) => {
            print("Config parse error: " + error.message);
            startApp(getDefaultConfig());
        }
        when config: Config => {
            startApp(config);
        }
    }
}
```

### User-Defined Result Types

While ZOM has native error handling, you can also define your own Result-like enums for cases where you want to handle success/failure as regular data rather than exceptions. Note that these are just regular enums and are not treated as error types by the compiler.

```zom
enum Result<T, E> {
    Success(T),
    Failure(E),
}

// This function returns a Result enum, not a native error
fun safeDivide(a: f64, b: f64) -> Result<f64, str> {
    if (b == 0.0) {
        return Result.Failure("Division by zero");
    }
    return Result.Success(a / b);
}

fun processResult() {
    let result = safeDivide(10.0, 2.0);
    match (result) {
        when Result.Success(value) => print("Result: " + value.toString())
        when Result.Failure(error) => print("Error: " + error)
    }
}
```

### Multiple Error Types

Functions can return multiple error types by declaring them with `raises`. All possible error types must be declared using `raises` and handled explicitly by the caller through pattern matching.

```zom
error ParseError {
    message: str,
    line: i32,
}

fun readConfigFile() -> Config raises FileNotFoundError | ParseError {
    let contentResult = readFile("config.json");
    match (contentResult) {
        when FileNotFoundError(error) => return error;
        when content: str => {
            let configResult = parseJson(content);
            match (configResult) {
                when ParseError(error) => return error;
                when config: Config => return validateConfig(config);
            }
        }
    }
}

fun initializeApp() {
    let configResult = readConfigFile();
    match (configResult) {
        when FileNotFoundError(error) => {
            print("Configuration file error: " + error.path);
            startApp(getDefaultConfig());
        }
        when ParseError(error) => {
            print("Configuration parse error at line " + error.line.toString() + ": " + error.message);
            startApp(getDefaultConfig());
        }
        when config: Config => {
            startApp(config);
        }
    }
}
```
