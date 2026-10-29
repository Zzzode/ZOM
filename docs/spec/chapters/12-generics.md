# Generics

Generics enable writing flexible, reusable code while maintaining type safety.

### Generic Functions

```zom
// Basic generic function
fn identity<T>(value: T) -> T {
    return value;
}

// Generic function with multiple type parameters
fn pair<T, U>(first: T, second: U) -> (T, U) {
    return (first, second);
}

// Generic function with constraints
fn max<T: Comparable>(a: T, b: T) -> T {
    return a > b ? a : b;
}

// Generic function with default type parameter
fn parseValue<T = str>(input: str) -> T? {
    // Implementation depends on T
    return null; // Placeholder
}
```

#### Call-Site Type Arguments

Call-site type arguments are solved in this revision only for free, unbounded
generic functions whose value parameters are bare type parameters declared by
the callee. This does not cover bounded functions, functions with default
type parameters, generic classes and enums, generic methods,
receiver-bearing functions, or tuple, aggregate, or other structural
type-argument positions; the examples elsewhere in this chapter that use those
forms are reserved, not accepted.

**Resolution.** A call binds equality constraints between the type of each
value argument and the bare type-parameter type of the corresponding value
parameter. Solving is a whole-variable-to-ground binding: no expected type is
propagated into a generic position and no structural position is unified. A
parameter observed at several value arguments must bind to equal ground
types. When type arguments are omitted they are inferred from the value
arguments; when they are written, each argument name resolves as a
predefined scalar type name. See
[Ch.03 Type Inference](03-types.md#type-inference) for the
equality-constraint vocabulary.

Resolution failures are reported at the call expression:

| Condition | Diagnostic |
|---|---|
| Value arguments bind one parameter to different ground types | `ZOM4010` |
| A parameter receives neither a binding nor an explicit argument | `ZOM4014` |
| The explicit type-argument count differs from the generic parameter count | `ZOM4016` |
| Explicit type arguments are written on a non-generic callee | `ZOM4035` |

The value-argument count is checked by the ordinary call rule, so a mismatch
is `ZOM4036` exactly as for a non-generic callee.

**Acceptance boundary.** In this revision every resolved generic call is
rejected before instantiation. A call in a value position is rejected with
`ZOM4099`; a call used as a bare expression statement is rejected with
`ZOM4098`; a call in a module initializer is rejected with `ZOM4104`.
Instantiation of free, unbounded functions over admitted scalar carriers is
reserved; no generic call is accepted end-to-end in this revision.

### Generic Classes

Generic class and interface instance members explicitly declare `this` in
their parameter clause.

```zom
class Box<T> {
    private let value: T;

    public init(this, value: T) {
        this.value = value;
    }

    public fn getValue(this) -> T {
        return this.value;
    }

    public fn map<U>(this, transform: (T) -> U) -> Box<U> {
        return Box(transform(this.value));
    }
}

// Usage
let intBox = Box(42);
let stringBox = intBox.map(fn (x: i32) -> str { return x.toString(); });
```

### Generic Interfaces

```zom
interface Comparable<T> {
    fn compareTo(this, other: T) -> i32;
}

interface Functor<T> {
    fn map<U>(this, transform: (T) -> U) -> Functor<U>;
}

interface Monad<T> : Functor<T> {
    fn flatMap<U>(this, transform: (T) -> Monad<U>) -> Monad<U>;
}
```

### Type Constraints

Type constraints restrict which concrete types may be substituted for a generic parameter. ZOM uses a single positive bound-list syntax: interface and marker names appear after `:`, joined by `+`.

#### Bound List Grammar

```ebnf
BoundList ::= TypeExpression ( '+' TypeExpression )*
```

- **Order-independence.** The bound set `{Drawable, Sendable, Shared}` describes exactly the same predicate as `{Sendable, Drawable, Shared}`. The tooling canonicalization convention is: interface bounds first, then marker bounds, each subgroup sorted alphabetically.
- **Duplicate detection.** Duplicate bounds within the same list produce warning W1204 `DuplicateBound`, suppressed by default.

#### Short Form vs. Where Clause

ZOM offers two equivalent syntactic surfaces on declarations that accept a
`where` clause: functions, classes, structs, and standalone interface `impl`
blocks. The inline short form is preferred for
simple cases; the `where` clause is preferred when type parameters each carry
different, lengthy bound sets, or when bounds reference associated types.
Interface declarations do not accept a `where` clause at the parser level;
constraints on interface type parameters are expressed directly in the type
parameter list. A source form such as `interface I<T> where T: Eq { ... }` is a
syntax error, not a delayed type-checker rule.
Marker implementations have neither type parameters nor a `where` clause and
therefore have no generic constraint surface.

```zom
// Short form (single type param, two bounds)
fn draw<T: Drawable + Sendable>(x: T);

// Equivalent long form — where clause
fn draw<T>(x: T)
where
    T: Drawable,
    T: Sendable;
```

Where clause preferred when:
1. Multiple type parameters, each with different bound sets.
2. Bounds exceed the 80-column line-width rule in short form.
3. Bounds reference associated types, e.g. `T::Item: Cloneable`.
4. Bound conjunction involves more than three items (readability threshold).

Four illustrative examples:

```zom
// 1. Single interface bound
fn sort<T: Comparable<T>>(array: T[]) -> T[] {
    return array;
}

// 2. Interface + 2 markers. Ordering does not affect semantics.
fn render_all<T: Drawable + Sendable + Shared>(surfaces: T[]) -> Canvas {
    return Canvas();
}

// 3. Interface + marker
fn spawn_local<T: Runnable + TaskBound>(task: T) -> LocalJoinHandle<T> {
    return LocalJoinHandle(task);
}

// 4. Where clause for a complex multi-parameter signature
fn complex_render<T, U>(surfaces: T[], transforms: U[]) -> Canvas
where
    T: Drawable,
    T: Sendable,
    T: Shared,
    U: Transform,
    U: Linear,
    T::Item: Cloneable,
{
    return Canvas();
}
```

#### Semantic Rules for Bound Lists

1. **Conjunction vs. type-intersection distinction.** The bound-list `+` operator (conjunction) is a *predicate on a single concrete type*. It must be distinguished from the type-level intersection operator `&` (Ch.03):

   | Construct            | Syntax                     | Level          | Meaning                                                  |
   |----------------------|----------------------------|----------------|----------------------------------------------------------|
   | Bound conjunction    | `T: Drawable + Sendable`   | Generic head   | "For the single concrete T, prove BOTH properties"       |
   | Type intersection    | `Drawable & Movable`       | Type position  | "One value that simultaneously IS both types"            |

   Type intersections `A & B` at type position are independent from the bound conjunction above. They are enforced structurally by the type checker as true sub-typing relationships, not as proof obligations on generic parameters. They are not dynamic-interface principals; a `dyn` type names one interface principal and may add marker suffixes.

2. **Conjunction.** The expression `M1 + M2 + M3` in a type-parameter bound list requires all three bounds. A `where` predicate carries one right-hand type, so multiple obligations for the same subject are written as repeated predicates. Interface and marker bounds share the same source syntax and canonical ordering.

   ```zom
   fn spawn_single_writer<F, T>(f: F) -> JoinHandle<T>
   where
       F: Sendable,
       F: Linear,
       T: Sendable,
       T: Linear;
   ```

#### Bound Satisfaction at Call Site

Bounds are parsed, recorded, and validated at the declaration site: a bound
that does not name an interface is rejected with `ZOM4129 GenericBoundNotInterface`,
and a primitive type used as an interface bound is rejected with
`ZOM4130 PrimitiveTypeBoundUnsupported`. Generic function bodies are checked
parametrically against the abstract parameter and its declared bounds; the
body may not assume any property of `T` that is not listed in its bound set.

Call-site satisfaction is not implemented in this revision. Impl lookup
(`ZOM4018 CheckerTraitNotImplemented`), marker bitmap checks, and witness
production at the call are reserved and land with witnesses, as does positive
resolution of members made available by a bound.

### Variance of Generic Parameters

User-defined generic named types are invariant in all type parameters.
This conservative rule prevents mutable containers from accidentally lifting a
reference coercion through the container boundary.

Function types still use the standard variance rule: parameter types are
contravariant, and return and raises members are covariant. See
[Ch.03 Variance](03-types.md#variance).

### Associated Types

```zom
interface Iterator<T> {
    type Item = T;

    fn next(this) -> Item?;
    fn hasNext(this) -> bool;
}

interface Collection<T> {
    type Iterator: Iterator<T>;
    type Item = T;

    fn iterator(this) -> Iterator;
    fn size(this) -> i32;
}
```

### Generic Enums

```zom
enum Option<T> {
    Some(T),
    None
}

enum Result<T, E> {
    Success(T),
    Failure(E)
}
```
