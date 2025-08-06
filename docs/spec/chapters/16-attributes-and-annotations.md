# Attributes and Annotations

Attributes provide metadata and modify the behavior of declarations.

### Built-in Attributes

```zom
// Deprecation
@deprecated("Use newFunction() instead")
fun oldFunction() {
    // Implementation
}

// Inline functions
@inline
fun fastMath(x: f64) -> f64 {
    return x * x + 2.0 * x + 1.0;
}

// No inline
@noinline
fun debugFunction() {
    // This function will never be inlined
}

// Conditional compilation
@if(DEBUG)
fun debugPrint(message: str) {
    print("DEBUG: " + message);
}

@if(!DEBUG)
fun debugPrint(message: str) {
    // Empty in release builds
}
```

### Serialization Attributes

```zom
@serializable
struct User {
    @serialize(name: "user_id")
    id: i64,

    @serialize
    name: str,

    @serialize(optional: true)
    email: str?,

    @ignore
    password: str // Not serialized
}
```

### Validation Attributes

```zom
struct CreateUserRequest {
    @validate(minLength: 3, maxLength: 50)
    name: str,

    @validate(email: true)
    email: str,

    @validate(minValue: 18, maxValue: 120)
    age: i32,

    @validate(pattern: "^[a-zA-Z0-9_]+$")
    username: str
}
```

### Custom Attributes

```zom
// Define custom attribute
@attribute
struct ApiEndpoint {
    method: str,
    path: str,
    auth: bool = false
}

// Use custom attribute
class UserController {
    @ApiEndpoint(method: "GET", path: "/users", auth: true)
    fun getUsers() -> User[] {
        // Implementation
    }

    @ApiEndpoint(method: "POST", path: "/users")
    fun createUser(request: CreateUserRequest) -> User {
        // Implementation
    }
}
```

### Reflection and Attributes

```zom
fun processController(controller: any) {
    let type = typeof(controller);

    for (let method in type.getMethods()) {
        if (let endpoint = method.getAttribute<ApiEndpoint>()) {
            registerRoute(endpoint.method, endpoint.path, method, endpoint.auth);
        }
    }
}
```
