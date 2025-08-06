# Generics

Generics enable writing flexible, reusable code while maintaining type safety.

### Generic Functions

```zom
// Basic generic function
fun identity<T>(value: T) -> T {
    return value;
}

// Generic function with multiple type parameters
fun pair<T, U>(first: T, second: U) -> (T, U) {
    return (first, second);
}

// Generic function with constraints
fun max<T: Comparable>(a: T, b: T) -> T {
    return a > b ? a : b;
}

// Generic function with default type parameter
fun parseValue<T = str>(input: str) -> T? {
    // Implementation depends on T
    return nil; // Placeholder
}
```

### Generic Classes

```zom
class Box<T> {
    private let value: T;

    public init(value: T) {
        this.value = value;
    }

    public fun getValue() -> T {
        return this.value;
    }

    public fun map<U>(transform: (T) -> U) -> Box<U> {
        return Box(transform(this.value));
    }
}

// Usage
let intBox = Box(42);
let stringBox = intBox.map(x => x.toString());
```

### Generic Interfaces

```zom
interface Comparable<T> {
    fun compareTo(other: T) -> i32;
}

interface Functor<T> {
    fun map<U>(transform: (T) -> U) -> Functor<U>;
}

interface Monad<T> extends Functor<T> {
    fun flatMap<U>(transform: (T) -> Monad<U>) -> Monad<U>;
}
```

### Type Constraints

```zom
// Single constraint
fun sort<T: Comparable<T>>(array: T[]) -> T[] {
    // Sorting implementation
    return array;
}

// Multiple constraints
fun processData<T: Serializable & Comparable<T>>(data: T[]) -> str {
    let sorted = sort(data);
    return serialize(sorted);
}

// Where clause for complex constraints
fun complexOperation<T, U>(input: T) -> U
    where T: Convertible<U>,
          U: Serializable {
    let converted = input.convert();
    return converted;
}
```

### Associated Types

```zom
interface Iterator<T> {
    type Item = T;

    fun next() -> Item?;
    fun hasNext() -> bool;
}

interface Collection<T> {
    type Iterator: Iterator<T>;
    type Item = T;

    fun iterator() -> Iterator;
    fun size() -> i32;
}
```

### Generic Enums

```zom
enum Option<T> {
    Some(T),
    None,

    fun map<U>(transform: (T) -> U) -> Option<U> {
        match (this) {
            when Some(value) => Some(transform(value))
            when None => None
        }
    }

    fun flatMap<U>(transform: (T) -> Option<U>) -> Option<U> {
        match (this) {
            when Some(value) => transform(value)
            when None => None
        }
    }
}
```
