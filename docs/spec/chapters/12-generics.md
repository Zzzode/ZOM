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

#### Marker Privileges in Where Clauses

Marker bounds are Boolean predicates in a proper lattice, which grants them three
syntactic privileges that interface bounds do not possess.

1. **Negation.** A marker bound accepts a `!` prefix meaning "the target type
   explicitly does NOT possess marker M". Interface bounds reject the `!`
   prefix with ZOM0448 since behavioral negation lacks a proof procedure in
   general.
2. **Optional relaxation with `?`**. On auto-derived markers the syntax `?M`
   means "T does NOT implicitly carry M as a precondition", relaxing the
   default upper bound that the compiler assumes for parameters. Only meaningful
   for markers that are auto-closed over primitive types by the language
   prelude (Sendable, Shared, Sized). User-defined auto-markers inherit this
   privilege in symmetric fashion.
3. **Commutative / associative closure.** The expression `M1 + M2 + M3` forms
   a proper Boolean conjunction: order does not matter (commutative) and
   regrouping does not matter (associative). `zom fmt` canonicalises to
   "interface bounds first, then markers, each subgroup alphabetically".
   Interface bounds share the alphabetical-order rule but do NOT form a
   closed lattice — there is no automatic way to combine `Drawable + Hashable`
   into a named third interface.

```zom
// Negation + conjunction (correct)
fn spawn_single_writer<F, T>(f: F) -> JoinHandle<T>
    where
        F: FnOnce() -> T,
        F: Sendable + !Shared + 'static,   // !Shared: requires NO shared-read aliasing
        T: Sendable + Linear,              // Linear: must be consumed exactly-once
    { ... }

// Relaxation for raw allocator (we do NOT require Send on the target type
// because this function will not move T across threads — it only returns a
// pointer that the caller later attaches semantics to).
fn raw_alloc<T: ?Sized + ?Sendable>(size_bytes: usize) -> *mut T { ... }
```

Anti-example for interface negation (produces ZOM0448):
```zom
// ❌ ZOM0448 — interface negation is not permitted
fn draw_except_shape<T>(x: T) where T: !Drawable;
```

#### Conjunction vs Intersection

The bound-list `+` operator (conjunction) describes a *predicate* on a single
concrete type. It MUST be distinguished from the type-level intersection
operator `&` (Ch.03):

| Construct            | Syntax                 | Level          | Meaning                                           |
|----------------------|------------------------|----------------|---------------------------------------------------|
| Bound conjunction    | `T: Drawable + Sendable` | Generic head   | "For the single concrete T, prove BOTH properties" |
| Type intersection    | `Drawable & Movable`   | Type position  | "One value that simultaneously IS both types"     |

Type intersections `A & B` at type position are independent from the bound
conjunction above. They are enforced structurally by the type checker as
true sub-typing relationships, not as proof obligations on generic parameters.

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
