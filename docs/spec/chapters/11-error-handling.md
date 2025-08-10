# Error Handling

Zom provides robust error handling mechanisms through exceptions, Result types, and optional values.

### Result Types

Result types explicitly handle errors by returning either a success value or an error.

```zom
enum Result<T, E> {
    Success(T),
    Failure(E)
}

fun safeDivide(a: f64, b: f64) -> Result<f64, str> {
    if (b == 0.0) {
        return Failure("Division by zero");
    }
    return Success(a / b);
}

fun processResult() {
    let result = safeDivide(10.0, 2.0);
    match (result) {
        when Success(value) => print("Result: " + value.toString())
        when Failure(error) => print("Error: " + error)
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

Use `?!` to propagate errors or `!!` to force unwrap (panics on failure).

```zom
fun readConfigFile() -> Result<Config, FileError | ParseError> {
    let content = readFile("config.json")?!;
    let config = parseJson(content)?!;
    return Success(validateConfig(config));
}

fun initializeApp() {
    let config = readConfigFile()!!;
    startApp(config);
}
```

### Result Types

```zom
enum Result<T, E> {
    Success(T),
    Failure(E)
}

fun safeDivide(a: f64, b: f64) -> Result<f64, str> {
    if (b == 0.0) {
        return Failure("Division by zero");
    }
    return Success(a / b);
}

fun processResult() {
    let result = safeDivide(10.0, 2.0);
    match (result) {
        when Success(value) => print("Result: " + value.toString())
        when Failure(error) => print("Error: " + error)
    }
}
```

### Optional Values

```zom
fun findUser(id: i64) -> User? {
    // Search for user
    if (userExists(id)) {
        return getUser(id);
    }
    return null;
}

fun processUser(userId: i64) {
    let user = findUser(userId);
    if (user != null) {
        print("Found user: " + user.name);
    } else {
        print("User not found");
    }

    // Using optional chaining
    let email = user?.email?.toLowerCase();

    // Using null coalescing
    let displayName = user?.name ?? "Anonymous";
}
```

### Error Propagation

```zom
fun readConfigFile() -> Config raises FileError, ParseError {
    let content = readFile("config.json"); // Can throw FileError
    let config = parseJson(content); // Can throw ParseError
    return validateConfig(config); // Can throw ValidationError
}

fun initializeApp() {
    try {
        let config = readConfigFile();
        startApp(config);
    } catch (FileError error) {
        print("Configuration file error: " + error.message);
        useDefaultConfig();
    } catch (ParseError error) {
        print("Configuration parse error: " + error.message);
        useDefaultConfig();
    }
}
```
