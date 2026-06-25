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
    return null; // Placeholder
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
let stringBox = intBox.map(fun (x: i32) -> str { return x.toString(); });
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

Type constraints restrict which concrete types may be substituted for a generic parameter. A single constraint item is either an **interface** (behavioral contract) or a **marker** (structural predicate). Constraints are joined with `+` in a bound list.

Constraint grammar:
```ebnf
BoundList      ::= BoundItem ( '+' BoundItem )*
BoundItem      ::= InterfaceType
                 | MarkerBound
MarkerBound    ::= '!'? ( Identifier | AttributeQualifiedPath )
```

Four concrete examples and two anti-examples:

```zom
// Single interface bound
fun sort<T: Comparable<T>>(array: T[]) -> T[] {
    // Implementation — T promises a total order via Comparable<T>::compareTo
    return array;
}

// Interface + 2 markers (interface first; ordering does not affect semantics)
fun render_all<T: Drawable + Sendable + Shared>(surfaces: T[]) -> Canvas {
    // T promises to render and be shareable across threads
    return Canvas()
}

// Interface + negated marker: !Sendable is a legitimate marker bound
fun spawn_local<T: Runnable + !Sendable>(task: T) -> LocalJoinHandle<T> {
    // T promises to run but only on the current executor thread
    return LocalJoinHandle(task)
}

// Where-clause form for complex multi-parameter cases
fun complex_render<T, U>(surfaces: T[], transforms: U[]) -> Canvas
    where T: Drawable + Sendable + Shared,
          U: Transform + Linear {
    return Canvas()
}

// ============================================================
// Anti-examples: compile-time errors
// ============================================================

// ERROR: Interfaces are behavioral contracts; negation is meaningless.
//   fun f<T: !Drawable>(x: T) -> unit
//   → ZOM0448 NegativeInterfaceBoundNotAllowed
//
// ERROR: Two different interface-satisfaction negations — use a more
//   specific structural marker-bound instead if this intent is needed.
//   fun g<T: !JsonSerializable + !BinarySerializable>(x: T) -> unit
//   → ZOM0448 × 2
```

Semantic rules for bound lists:

1. **Order-independence within a bound list.** The set `{Drawable, Sendable, Shared}` describes exactly the same predicate as `{Sendable, Drawable, Shared}`. Tooling (linters, `zom fmt`) canonicalises to: interface bounds first, then marker bounds, each subgroup sorted alphabetically.
2. **Duplicates produce W1204 DuplicateBound**, suppressed by default.
3. **Conjunction with `&` (type-level intersection) is a TYPE EXPRESSION, not a bound.** `T: I1 + I2` = "T satisfies both I1 and I2". `I1 & I2` as a type = "the anonymous intersection type whose values satisfy both I1 and I2 simultaneously"; use `dyn (I1 & I2)` to name it as an existential.
4. **Marker negation (`!`)** is legal only on marker bounds. On an interface name it emits ZOM0448.

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
