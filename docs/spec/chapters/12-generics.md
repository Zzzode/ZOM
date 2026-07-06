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

Type constraints restrict which concrete types may be substituted for a generic parameter. ZOM uses a **single unified bound list syntax**: both interface bounds and marker bounds appear in `:`-separated bound lists, joined by `+`. Interface bounds always accept the positive form; marker bounds additionally accept a `!` (negative) prefix.

#### Bound List Grammar

```ebnf
BoundList ::= BoundItem ( '+' BoundItem )*
BoundItem ::= '!' MarkerPath
            | InterfaceName ( '<' GenericArgs '>' )?
```

- **Positive interface bounds only.** Writing `fun f<T: !Drawable>(x: T)` is a hard error. Rationale: interfaces describe *behavioral obligations* — "does NOT implement Drawable" is not a useful static contract; refactoring into smaller, finer-grained interfaces achieves the same goal without requiring negative reasoning. The diagnostic is ZOM0422 `NegativeInterfaceBoundNotAllowed`.
- **Marker bounds allow negation.** The form `!Shared` means "definitely does NOT impl the Shared marker". Marker negation is sound because markers are structural Boolean properties of types, closed under negative coherence (Ch.22 §22.3).
- **Order-independence.** The bound set `{Drawable, Sendable, Shared}` describes exactly the same predicate as `{Sendable, Drawable, Shared}`. The tooling canonicalization convention is: interface bounds first, then marker bounds, each subgroup sorted alphabetically.
- **Duplicate detection.** Duplicate bounds within the same list produce warning W1204 `DuplicateBound`, suppressed by default.

#### Short Form vs. Where Clause

ZOM offers two equivalent syntactic surfaces. The inline short form is preferred for simple cases; the `where` clause is preferred when type parameters each carry different, lengthy bound sets, or when bounds reference associated types.

```zom
// Short form (single type param, two bounds)
fun draw<T: Drawable + Sendable>(x: T);

// Equivalent long form — where clause
fun draw<T>(x: T)
where
    T: Drawable,
    T: Sendable,
{
    ...
}
```

Where clause preferred when:
1. Multiple type parameters, each with different bound sets.
2. Bounds exceed the 80-column line-width rule in short form.
3. Bounds reference associated types, e.g. `T::Item: Cloneable`.
4. Bound conjunction involves more than three items (readability threshold).

Four illustrative examples:

```zom
// 1. Single interface bound
fun sort<T: Comparable<T>>(array: T[]) -> T[] {
    return array;
}

// 2. Interface + 2 markers. Ordering does not affect semantics.
fun render_all<T: Drawable + Sendable + Shared>(surfaces: T[]) -> Canvas {
    return Canvas();
}

// 3. Interface + negated marker — !Sendable is legitimate
fun spawn_local<T: Runnable + !Sendable>(task: T) -> LocalJoinHandle<T> {
    return LocalJoinHandle(task);
}

// 4. Where clause for a complex multi-parameter signature
fun complex_render<T, U>(surfaces: T[], transforms: U[]) -> Canvas
where
    T: Drawable + Sendable + Shared,
    U: Transform + Linear,
    T::Item: Cloneable,
{
    return Canvas();
}
```

Two anti-examples:

```zom
// ERROR — interface negation is not a meaningful static contract.
// fun f<T: !Drawable>(x: T) -> unit;
// → ZOM0422 NegativeInterfaceBoundNotAllowed

// ERROR — two interface negations; rewrite using structural marker bounds
// fun g<T: !JsonSerializable + !BinarySerializable>(x: T) -> unit;
// → ZOM0422 × 2
```

#### Semantic Rules for Bound Lists

1. **Conjunction vs. type-intersection distinction.** The bound-list `+` operator (conjunction) is a *predicate on a single concrete type*. It must be distinguished from the type-level intersection operator `&` (Ch.03):

   | Construct            | Syntax                     | Level          | Meaning                                                  |
   |----------------------|----------------------------|----------------|----------------------------------------------------------|
   | Bound conjunction    | `T: Drawable + Sendable`   | Generic head   | "For the single concrete T, prove BOTH properties"       |
   | Type intersection    | `Drawable & Movable`       | Type position  | "One value that simultaneously IS both types"            |

   Type intersections `A & B` at type position are independent from the bound conjunction above. They are enforced structurally by the type checker as true sub-typing relationships, not as proof obligations on generic parameters. To name an intersection as an existential, write `dyn (Drawable & Movable)` (requires object-safe interfaces).

2. **Marker negation `!` is legal only on marker bounds.** Applied to an interface name it raises ZOM0422.

3. **Marker-only privileges in where clauses.** Markers are Boolean predicates in a proper lattice, which grants them three syntactic privileges that interface bounds do not possess:
   - **Negation.** As above.
   - **Optional relaxation with `?`.** On auto-derived markers the syntax `?M` means "T does NOT implicitly carry M as a precondition", relaxing the default upper bound. Meaningful only for prelude-closed markers (Sendable, Shared, Sized) and user-defined auto-markers.
   - **Commutative / associative closure.** The expression `M1 + M2 + M3` forms a proper Boolean conjunction. Interface bounds share the alphabetical-order rule but do NOT form a closed lattice — there is no automatic way to combine `Drawable + Hashable` into a named third interface.

   ```zom
   fn spawn_single_writer<F, T>(f: F) -> JoinHandle<T>
       where
           F: FnOnce() -> T,
           F: Sendable + !Shared,        // !Shared: requires no shared-read aliasing
           T: Sendable + Linear,         // Linear: must be consumed exactly once
   { ... }

   // Relaxation for a raw allocator — do NOT require Sendable or Sized on T
   fn raw_alloc<T: ?Sized + ?Sendable>(size_bytes: usize) -> *mut T { ... }
   ```

#### Bound Satisfaction at Call Site

For each call to a generic function `f<T_real>()`, the compiler performs, for every bound declared on each type parameter:

- **Interface bound** `T: I<...>` → a valid impl block must exist declaring `impl I<...> for T_real`. Failure raises a member of the ZOM04xx series (`ZOM0410 TraitBoundUnsatisfied` and friends; see chapter-level diagnostic table).
- **Positive marker bound** `T: M` → the marker bitmap for `T_real` must have bit `M` set. Otherwise `ZOM0430 MarkerBoundMissing`.
- **Negative marker bound** `T: !M` → the marker bitmap for `T_real` must have bit `M` explicitly clear (either by negative impl or by the negative-closure lattice rejecting derivation). Otherwise `ZOM0431 NegativeMarkerBoundViolated`.

Bound satisfaction is also checked *inside* the generic body (prior to monomorphisation) against the declared bounds alone. The body may not assume any property of `T` that is not listed in its bound set; violations are diagnosed at body-check time via the same ZOM04xx diagnostic codes.

### Variance of Generic Parameters

User-defined generic named types are invariant in all type parameters in v1.
This conservative rule prevents mutable containers from accidentally lifting a
reference coercion through the container boundary.

```zom
let xs: Vec<&mut i32> = make_mut_refs();
// INVALID: Vec<T> is invariant in T.
// let ys: Vec<&i32> = xs;
```

Function types still use the standard variance rule: parameter types are
contravariant, and return and raises members are covariant. See
[Ch.03 Variance](03-types.md#variance).

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
            when Some(value) => { return Some(transform(value)); }
            when None => { return None; }
        }
    }

    fun flatMap<U>(transform: (T) -> Option<U>) -> Option<U> {
        match (this) {
            when Some(value) => { return transform(value); }
            when None => { return None; }
        }
    }
}
```
