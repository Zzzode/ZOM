---
rfc: 54
title: Core Library API Specification
type: language
status: DRAFT
author: ZOM Compiler Team
review-manager: ZOM Compiler Team
required-owners: []
approvers: []
created: 2026-10-01
updated: 2026-10-01
area: language
requires: [25, 24]
supersedes: []
superseded-by: []
discussion: TBD
decision: TBD
implementation: TBD
tracking-issue: TBD
---

# RFC 0054: Core Library API Specification

## Summary

This RFC specifies the API surface of the ZOM core library: the module
structure, the implicitly-imported prelude, the `Maybe<T>` sum type, the
error-union combinators and `FromError` conversion trait, marker interfaces,
operator interfaces, iterator interfaces, conversion interfaces, formatting
interfaces, and inherent methods on primitive types. It also resolves
existing spec drift where normative chapters reference `std::marker::Sendable`,
`std::collections::HashMap`, `Atomic<T>`, and `::core::Value` without
definitions.

The key design decision: ZOM's language-level `T | E` error unions (with
`raises` clauses and `?!`/`!!`/`?:` operators) and `T?` null-unions already
occupy the slots that Rust fills with `Result<T, E>` and `Option<T>`. The
corelib does not introduce separate `Result` or `Option` types. Instead, it
provides `Maybe<T>` (the library type behind `T?` sugar), `FromError<E>`
(the conversion trait that powers `?!`), the `Error` interface, and
combinators on both `Maybe<T>` and `T | E`.

## Motivation

The ZOM specification chapters reference standard-library types in examples
and semantic rules without a normative API catalog:

- `Option<T>` and `Result<T, E>` appear as enum examples (Ch.10) and in
  interface signatures (Ch.03 `Iterator::next`), but their method surface is
  never specified.
- `std::marker::Sendable` and `std::marker::Shared` are referenced (Ch.16)
  without a defining module.
- `std::collections::HashMap` is referenced (Ch.03) without a defining module.
- `Atomic<T>` is referenced (Ch.03) as "see Ch.15", but Ch.15 does not mention
  atomics.
- `::core::Value` is referenced (Ch.03) without a definition.
- `Vec<T>` and `Map<K, V>` are used in type examples (Ch.03) without a
  defining module.
- `print()` and `toString()` are used throughout the spec without a defining
  module.
- The `Debug` interface is used in impl examples (Ch.09) without a definition.
- The `Show` interface is used as a bound (Ch.09) without a definition.

RFC 0025 defines the source-backed core library *architecture* but explicitly
lists "Defining a complete standard-library API catalog" as a Non-Goal. This
RFC fills that gap.

## Goals

- Define the `core` / `std` module boundary.
- Define the prelude: the set of names implicitly available in every module.
- Define the `Maybe<T>` API surface (the library type behind `T?` sugar).
- Define the error-union support: `FromError<E>`, `Error`, and combinators
  on `T | E`.
- Define the marker interfaces: `Copy`, `Linear`, `Drop`, `Sendable`,
  `Shared`, `Sized`, `Phantom<T>`.
- Define the operator interfaces: arithmetic, bitwise, comparison, indexing.
- Define the iterator interfaces: `Iterator`, `IntoIterator`,
  `FromIterator`.
- Define the conversion interfaces: `From`, `Into`, `TryFrom`, `TryInto`,
  `AsRef`, `AsMut`, `Borrow`, `BorrowMut`, `FromStr`.
- Define the formatting interfaces: `Display`, `Debug`.
- Define inherent methods on primitive types (integers, floats, `bool`,
  `char`, `str`).
- Resolve spec drift: every `std::` and `core::` reference in normative
  chapters must point to a definition in this RFC or its successor.

## Non-Goals

- Implementing the core library. This RFC specifies the API; implementation
  is tracked separately.
- Defining the `std` collections API (`Vec`, `HashMap`, `HashSet`, owned
  `String`). Those belong to a follow-up RFC once the core layer is frozen.
- Defining the I/O, filesystem, threading, or network APIs. Those belong to
  `std` and require separate RFCs.
- Defining the runtime ABI, panic strategy, or error lowering. Those are
  tracked by RFC 0006.
- Specifying the exact memory layout of `Maybe<T>`. Layout is an
  implementation detail governed by the ABI RFC, though the niche
  optimization contract is specified here.

## Prior Art

### Rust `core` / `alloc` / `std`

Rust splits its standard library along two axes: heap allocation and OS
integration. `core` is heap-free and OS-free; `alloc` adds a heap allocator;
`std` adds OS integration. The boundary is drawn at *capabilities*, not
utility.

**ZOM copies:** the capability-based core/std boundary; the minimal governed
prelude; `Maybe` (née `Option`) as a plain enum with combinator methods; the
`Iterator` trait with one required method and everything else provided; the
`From`/`Into` blanket pair; the `Display`/`Debug` split; the four arithmetic
flavors on primitives; the `FromResidual`-style error conversion trait
(`FromError` in ZOM); auto traits for concurrency safety (`Sendable`/`Shared`
in ZOM); the `Borrow`/`BorrowMut` distinction for hash/EQ-consistent views.

**ZOM avoids:** Rust's `Result<T, E>` enum (ZOM has `T | E` error unions);
Rust's `Deref` trait (ZOM has explicit references); Rust's `Drop` trait
(ZOM has `deinit` methods, though a `Drop` interface is provided for generic
code); Rust's `Fn`/`FnMut`/`FnOnce` hierarchy (ZOM function types encode
capture discipline directly).

### Swift Standard Library

Swift ships one implicitly-imported module. `Optional` and `Result` are
plain enums with compiler sugar. Protocols use minimal requirements with
default implementations.

**ZOM copies:** the minimal-requirement pattern for operator interfaces
(`Eq` requires only `eq`; `Ord` requires only `cmp`); `Optional` as a plain
enum with compiler sugar (ZOM: `Maybe` with `T?` sugar); the `Error` marker
protocol pattern (ZOM: `Error` interface with one method).

**ZOM avoids:** Swift's single-module approach (ZOM separates heap-free
`core` from heap-dependent `std`); Swift's `@autoclosure`; Swift's `@frozen`
ABI annotations.

### Go Universe Block

Go's prelude is ~45 predeclared identifiers. Everything else requires an
import. `error` is a one-method interface.

**ZOM copies:** the fiercely-defended prelude boundary; the principle that
only compiler-magic operations are builtin; `error` as a simple interface
rather than a complex type hierarchy.

**ZOM avoids:** Go's `nil` zero value (ZOM uses `Maybe`/`T?` sum types);
Go's `panic`/`recover` (ZOM uses `raises` clauses and error unions); Go's
non-generic legacy containers.

### Zig Standard Library

Zig's `std` is explicitly imported. Allocation is explicit via
`std.mem.Allocator`. No operator overloading. Comptime-based generics.

**ZOM copies:** the explicit-allocation principle (ZOM's ownership model
makes allocation sites explicit through `Own<T>`); the no-implicit-prelude
for collections.

**ZOM avoids:** Zig's no-prelude approach for core types; Zig's comptime
generics (ZOM uses interface-based generics); Zig's `[]const u8` strings
(ZOM has `str` as a proper UTF-8 view).

## Guide-Level Explanation

### Module Structure

Every ZOM program implicitly imports the `core` module. The `std` module
requires an explicit import:

```zom
// core is implicitly available — no import needed
let x: Maybe<i32> = Some(42);
let y: i32 = x.unwrap();

// std requires explicit import
import std::collections::Vec;
let mut v: Vec<i32> = Vec::new();
```

The boundary is drawn at capabilities:

| Layer | Needs heap? | Needs OS? | Contents |
|---|---|---|---|
| `core` | no | no | `Maybe`, error-union support, markers, operators, iterators, conversions, formatting, primitives, atomics, `Duration`, `Cell` |
| `std` | yes | yes | `Vec`, `HashMap`, `HashSet`, owned `String`, I/O, files, threads, channels, networking, paths, processes |

### The Prelude

The prelude is the set of names implicitly available in every module without
import. It is deliberately small. Additions are governance-level decisions
(RFC required).

**Types and constructors:**

| Name | Kind | Source |
|---|---|---|
| `Maybe` | enum | `core::maybe` |
| `Some` | enum constructor | `core::maybe` |
| `None` | enum constructor | `core::maybe` |
| `Ordering` | enum | `core::cmp` |
| `bool`, `char`, `str` | primitive | language |
| `i8`..`i64`, `u8`..`u64` | primitive | language |
| `isize`, `usize` | primitive | language |
| `f32`, `f64` | primitive | language |
| `unit`, `never` | primitive | language |

**Interfaces:**

| Name | Source |
|---|---|
| `Copy` | `core::marker` |
| `Linear` | `core::marker` |
| `Drop` | `core::marker` |
| `Sendable` | `core::marker` |
| `Shared` | `core::marker` |
| `Sized` | `core::marker` |
| `Error` | `core::result` |
| `FromError` | `core::result` |
| `Iterator` | `core::iter` |
| `IntoIterator` | `core::iter` |
| `From` | `core::convert` |
| `Into` | `core::convert` |
| `Eq` | `core::cmp` |
| `Ord` | `core::cmp` |

**Functions:**

| Name | Source |
|---|---|
| `drop` | `core::mem` |
| `panic` | `core::panic` |

Notably absent (require explicit import): `Debug`, `Display`, `Hash`,
`Default`, `Write`, `AsRef`/`AsMut`, `Borrow`/`BorrowMut`, `FromStr`,
`TryFrom`/`TryInto`, all iterator adapters, `Phantom`, `Duration`, atomics,
`NonZero*`, `mem::*`.

### Maybe<T>

`Maybe<T>` is a plain enum — the library type behind the `T?` null-union
sugar:

```zom
enum Maybe<T> {
    Some(T),
    None
}
```

The `T?` form is sugar for `T | null`. For value-heavy APIs that need
explicit variant names or payload-rich absence states, use `Maybe<T>`
directly.

**Layout contract:** `Maybe<Own<T>>`, `Maybe<&T>`, `Maybe<&mut T>`, and
`Maybe<NonZero*>` are pointer-sized / integer-sized with zero tag overhead
(niche optimization). This makes `Maybe` free at FFI and collection
boundaries.

```zom
let x: Maybe<i32> = Some(42);
let y: Maybe<i32> = None;

x.isSome();        // true
y.isNone();        // true
x.unwrap();        // 42 (traps on None)
y.unwrapOr(0);     // 0
x.map(|v| v + 1);  // Some(43)
y.map(|v| v + 1);  // None
x.andThen(|v| Some(v * 2));  // Some(84)
x.filter(|v| v > 100);      // None
x.okOr(MyError::NotFound);  // 42 | MyError
```

**Deliberately not implemented: `Deref`.** There is no silent null-deref;
both variants must be handled.

### Error Unions and FromError

ZOM has no `Result` type — `T | E` is the result. The `raises` clause on a
function declares its error type. The `?!` operator propagates errors. The
`!!` operator traps on error. The `?:` operator provides a default.

What the core library provides:

**`FromError<E>` — the trait that makes `?!` ergonomic:**

```zom
interface FromError<E> {
    fun fromError(err: E) -> Self;
}

// Blanket identity: a function raising E can ?! a call raising E.
impl<E> FromError<E> for E {
    fun fromError(err: E) -> E { err }
}
```

Inside a function with `raises R`, `expr?!` where `expr: T | E` is
admissible iff `R: FromError<E>`. This means a caller's error domain must
explicitly accept each callee's error type via an impl — checked at the
signature level, zero-cost at runtime.

**`Error` interface — one required method:**

```zom
interface Error {
    fun message(this) -> str;
}
```

One required method (not an empty marker) so errors are always printable,
which the diagnostic renderer and `zomc run` exit reporting need.
Convention: error types are enums, one variant per failure mode.

**Error-union combinators** (provided as methods on the `T | E` shape):

```zom
fun isOk(this) -> bool
fun isErr(this) -> bool
fun map<U>(this, f: (T) -> U) -> (U | E)
fun mapErr<F>(this, f: (E) -> F) -> (T | F)
fun andThen<U>(this, f: (T) -> (U | E)) -> (U | E)
fun orElse<F>(this, f: (E) -> (T | F)) -> (T | F)
fun unwrapOr(this, default: T) -> T    // == expr?: default
fun ok(this) -> Maybe<T>
fun err(this) -> Maybe<E>
fun inspect(this, f: (T) -> ()) -> (T | E)
fun inspectErr(this, f: (E) -> ()) -> (T | E)
```

### Marker Interfaces

```zom
// core::marker

interface Copy {}       // bitwise duplication is valid
interface Linear {}     // must be explicitly consumed; no copy, no drop
interface Drop {        // compiler-called at scope exit
    fun drop(this: &mut Self);
}
interface Sendable {}   // ownership may cross thread boundaries (auto-derived)
interface Shared {}     // shared references are thread-safe (auto-derived)
interface Sized {}      // type has statically known size (implicit bound)

struct Phantom<T> {}    // zero-sized; tells compiler about ownership/variance
```

`Copy` and `Linear` are already defined in `core/src/core/marker.zom`.
`Sendable` and `Shared` are auto-derived structurally: a user-defined type
auto-implements them when all its fields satisfy them. Opt out with a
negative impl (`impl !Sendable for MyType;`).

### Operator Interfaces

Operators are interfaces, not compiler-rewritten method names, so user types
work in generic numeric code. Every operator interface has an associated
`Output` and a `Rhs` type parameter defaulting to `Self`.

```zom
interface Add<Rhs = Self> {
    type Output;
    fun add(this, rhs: Rhs) -> Output;
}
// Sub, Mul, Div, Rem — same shape.
// AddAssign, SubAssign, MulAssign, DivAssign, RemAssign — separate traits.
// Neg, Not — unary.
// BitAnd, BitOr, BitXor, Shl, Shr + assign variants.
// Index<Idx>, IndexMut<Idx>.
```

**Comparison (two-tier, justified by floats):**

```zom
enum Ordering { Less, Equal, Greater }

interface Eq {
    fun eq(this, other: &Self) -> bool;
    // Provided: fun ne(this, other: &Self) -> bool { !this.eq(other) }
}

interface Ord : Eq {
    fun cmp(this, other: &Self) -> Ordering;
    // Provided: lt, le, gt, ge, min, max, clamp
}
```

`f32`/`f64` implement neither `Eq` nor `Ord` (NaN breaks reflexivity/totality).
They get inherent `totalCmp` for sorting.

### Iterator Interfaces

```zom
interface Iterator {
    type Item;
    fun next(this: &mut Self) -> Maybe<Self::Item>;
    // All other methods are provided: map, filter, fold, forEach, take, skip,
    // chain, zip, enumerate, collect, count, last, nth, any, all, sum,
    // product, min, max, reduce, partition, unzip, position, ...
}

interface IntoIterator {
    type Item;
    type IntoIter: Iterator<Item = Item>;
    fun intoIter(this) -> IntoIter;
}

interface FromIterator {
    type Item;
    fun fromIter<I: IntoIterator<Item = Item>>(iter: I) -> Self;
}
```

`for` loop desugaring:

```zom
for (item in iter) { body }
// desugars to:
let mut it = iter.intoIter();
while (let Some(item) = it.next()) { body }
```

### Conversion Interfaces

Three axes:

```zom
// Axis 1: infallible, consuming. Implement From; Into comes free.
interface From<T> {
    fun from(value: T) -> Self;
}
interface Into<T> {
    fun into(this) -> T;
}
// Blanket: impl<T, U: From<T>> Into<U> for T

// Axis 2: fallible, consuming — returns the error union.
interface TryFrom<T> {
    type Error;
    fun tryFrom(value: T) -> (Self | Self.Error);
}
interface TryInto<T> {
    type Error;
    fun tryInto(this) -> (T | Self.Error);
}

// Axis 3: borrowed views.
interface AsRef<T> {
    fun asRef(this) -> &T;
}
interface AsMut<T> {
    fun asMut(this) -> &mut T;
}
// Borrow: CONTRACT — eq/ord/hash identical to owner.
// This is what makes HashMap<String, _> lookups with &str sound.
interface Borrow<T> {
    fun borrow(this) -> &T;
}
interface BorrowMut<T> : Borrow<T> {
    fun borrowMut(this) -> &mut T;
}

// Parsing belongs to the type, not the string.
interface FromStr {
    type Error;
    fun fromStr(s: str) -> (Self | Self.Error);
}
```

### Formatting Interfaces

```zom
interface Display {
    fun format(this, f: &mut Formatter);
}

interface Debug {
    fun format(this, f: &mut Formatter);
}
```

**The load-bearing split: user-facing formatting is manual (`Display`),
developer-facing formatting is derived (`Debug`).** This prevents
`toString()` from leaking debug syntax into user output.

`Debug` is compiler-derivable for aggregates whose fields are all `Debug`.
`Display` has no derive — implementing it forces a decision about the
human-facing representation.

`toString()` is a blanket method on `Display`:

```zom
// Blanket: impl<T: Display> ToString for T {
//     fun toString(this) -> String { ... }
// }
```

`print()` is a `std` function (requires `std::io`), not a `core` function.

### Primitive Inherent Methods

Policy-flavored operations are inherent methods (not interface impls) so
there is no import tax and no ambiguity.

**Integers — four arithmetic flavors:**

| Flavor | Example | Behavior |
|---|---|---|
| Panicking | `a + b` | Traps on overflow |
| Checked | `a.checkedAdd(b)` | Returns `Maybe<Self>`, `None` on overflow |
| Wrapping | `a.wrappingAdd(b)` | Two's complement wrap |
| Saturating | `a.saturatingAdd(b)` | Clamp at MIN/MAX |

Also: bit manipulation (`countOnes`, `leadingZeros`, `swapBytes`,
`rotateLeft`), byte order (`toBe`, `fromBe`, `toBeBytes`), math (`abs`,
`clamp`, `pow`, `isPowerOfTwo`, `nextPowerOfTwo`, `ilog2`), and `NonZero*`
supporting types for niche optimization.

**Floats:** constants (`PI`, `E`, `INFINITY`, `NAN`), classification
(`isNaN`, `isFinite`, `isNormal`), math (`sqrt`, `sin`, `cos`, `floor`,
`ceil`, `round`, `abs`, `clamp`), `totalCmp` for sorting (since `Ord` is
absent).

**`bool`:** `then`, `thenSome` (bridge into `Maybe` rail).

**`char`:** classification (`isAlphabetic`, `isNumeric`, `isWhitespace`),
conversion (`toLowercase`, `toUppercase`, `toDigit`, `fromDigit`), encoding
(`lenUtf8`, `encodeUtf8`).

**`str`:** length/emptiness, predicates (`startsWith`, `endsWith`,
`contains`), search (`find`, `rfind`), split (`split`, `lines`), trim
(`trim`, `trimStart`, `trimEnd`), transform (`toLowercase`, `toUppercase`),
parse (`parse<T: FromStr>()`), conversion (`asBytes`, `bytes`, `chars`).

**General rule for slices/strings:** lookup-and-fail operations return
`Maybe`, never trap. The trap forms (`[]`, `!!`, `unwrap`, `expect`) exist
only as explicit escapes.

## Reference-Level Design

### Module Paths

```
core/src/core/
  prelude.zom     — curated re-exports; the only implicitly-imported module
  marker.zom      — Copy, Linear, Drop, Sendable, Shared, Sized, Phantom<T>
  maybe.zom       — Maybe<T> and its full combinator surface
  result.zom      — FromError<E>, Error, error-union combinators
  cmp.zom         — Eq, Ord, Ordering, min/max/clamp free functions
  ops.zom         — arithmetic/bitwise/index operator interfaces
  iter.zom        — Iterator, IntoIterator, FromIterator, adapters, ranges
  convert.zom     — From, Into, TryFrom, TryInto, AsRef, AsMut, Borrow, BorrowMut, FromStr
  fmt.zom         — Display, Debug, Formatter, Write
  default.zom     — Default
  hash.zom        — Hash, Hasher
  mem.zom         — sizeOf, alignOf, swap, replace, take, forget
  cell.zom        — Cell, RefCell
  atomic.zom      — AtomicBool, AtomicI32, AtomicUsize, MemoryOrder
  duration.zom    — Duration (heap-free)
  panic.zom       — panic, unreachable (all trap/abort, never unwind)
  nonzero.zom     — NonZeroI8..NonZeroU64, NonZeroUsize
  char.zom        — char methods
  str.zom         — str methods
  num.zom         — integer and float methods
```

### Maybe<T> API (Normative)

```zom
enum Maybe<T> {
    Some(T),
    None
}
```

**Query:**

| Method | Signature | Description |
|---|---|---|
| `isSome` | `fun isSome(this) -> bool` | Returns `true` if `Some`. |
| `isSomeAnd` | `fun isSomeAnd(this, f: (T) -> bool) -> bool` | Returns `true` if `Some` and predicate holds. |
| `isNone` | `fun isNone(this) -> bool` | Returns `true` if `None`. |

**Unwrap:**

| Method | Signature | Description |
|---|---|---|
| `unwrap` | `fun unwrap(this) -> T` | Returns the payload or traps. |
| `expect` | `fun expect(this, msg: str) -> T` | Returns the payload or traps with `msg`. |
| `unwrapOr` | `fun unwrapOr(this, default: T) -> T` | Returns the payload or `default`. |
| `unwrapOrElse` | `fun unwrapOrElse(this, f: () -> T) -> T` | Returns the payload or computes from `f`. |
| `unwrapOrDefault` | `fun unwrapOrDefault(this) -> T where T: Default` | Returns the payload or `T::default()`. |

**Transform:**

| Method | Signature | Description |
|---|---|---|
| `map` | `fun map<U>(this, f: (T) -> U) -> Maybe<U>` | Transforms the payload. |
| `mapOr` | `fun mapOr<U>(this, default: U, f: (T) -> U) -> U` | Transforms or returns `default`. |
| `mapOrElse` | `fun mapOrElse<U>(this, default: () -> U, f: (T) -> U) -> U` | Transforms or computes. |
| `andThen` | `fun andThen<U>(this, f: (T) -> Maybe<U>) -> Maybe<U>` | Monadic bind. |
| `and` | `fun and<U>(this, other: Maybe<U>) -> Maybe<U>` | Returns `None` if `None`, else `other`. |
| `or` | `fun or(this, other: Maybe<T>) -> Maybe<T>` | Returns `this` if `Some`, else `other`. |
| `orElse` | `fun orElse(this, f: () -> Maybe<T>) -> Maybe<T>` | Returns `this` if `Some`, else computes. |
| `filter` | `fun filter(this, f: (T) -> bool) -> Maybe<T>` | Returns `Some` if predicate holds. |
| `flatten` | `fun flatten(this: Maybe<Maybe<T>>) -> Maybe<T>` | Flattens one level. |
| `zip` | `fun zip<U>(this, other: Maybe<U>) -> Maybe<(T, U)>` | Combines two maybes. |
| `inspect` | `fun inspect(this, f: (T) -> ()) -> Maybe<T>` | Calls `f` on payload, returns `this`. |

**Convert:**

| Method | Signature | Description |
|---|---|---|
| `okOr` | `fun okOr<E>(this, err: E) -> (T | E)` | Converts to error union. |
| `okOrElse` | `fun okOrElse<E>(this, f: () -> E) -> (T | E)` | Converts with lazy error. |

**Entry-style mutation:**

| Method | Signature | Description |
|---|---|---|
| `take` | `fun take(this: &mut Self) -> Maybe<T>` | Takes the payload, leaving `None`. |
| `replace` | `fun replace(this: &mut Self, value: T) -> Maybe<T>` | Replaces, returns old. |
| `getOrInsert` | `fun getOrInsert(this: &mut Self, value: T) -> &mut T` | Inserts if `None`, returns ref. |
| `getOrInsertWith` | `fun getOrInsertWith(this: &mut Self, f: () -> T) -> &mut T` | Lazy insert. |

**Iterate:**

| Method | Signature | Description |
|---|---|---|
| `iter` | `fun iter(this) -> MaybeIter<T>` | Zero-or-one iterator. |

**Interface implementations:**

- `Copy` when `T: Copy`
- `Eq` when `T: Eq`
- `Ord` when `T: Ord` (with `None < Some(x)`)
- `Hash` when `T: Hash`
- `Debug`
- `Display` when `T: Display`
- `From<T>` (gives `Some`)
- `IntoIterator` for `Maybe<T>`, `&Maybe<T>`, `&mut Maybe<T>`
- `FromIterator` (collecting an iterator of `Maybe` short-circuits to `None`)

### Error-Union API (Normative)

**`FromError<E>`:**

```zom
interface FromError<E> {
    fun fromError(err: E) -> Self;
}

impl<E> FromError<E> for E {
    fun fromError(err: E) -> E { err }
}
```

**`Error`:**

```zom
interface Error {
    fun message(this) -> str;
}
```

**Combinators on `T | E`:**

| Method | Signature | Description |
|---|---|---|
| `isOk` | `fun isOk(this) -> bool` | Returns `true` if success. |
| `isErr` | `fun isErr(this) -> bool` | Returns `true` if error. |
| `map` | `fun map<U>(this, f: (T) -> U) -> (U | E)` | Transforms success. |
| `mapErr` | `fun mapErr<F>(this, f: (E) -> F) -> (T | F)` | Transforms error. |
| `andThen` | `fun andThen<U>(this, f: (T) -> (U | E)) -> (U | E)` | Monadic bind. |
| `orElse` | `fun orElse<F>(this, f: (E) -> (T | F)) -> (T | F)` | Error recovery. |
| `unwrapOr` | `fun unwrapOr(this, default: T) -> T` | Returns success or default. |
| `ok` | `fun ok(this) -> Maybe<T>` | Converts to `Maybe`, discarding error. |
| `err` | `fun err(this) -> Maybe<E>` | Converts to `Maybe`, discarding success. |
| `inspect` | `fun inspect(this, f: (T) -> ()) -> (T | E)` | Calls `f` on success. |
| `inspectErr` | `fun inspectErr(this, f: (E) -> ()) -> (T | E)` | Calls `f` on error. |

### Marker Interfaces (Normative)

```zom
// core::marker

interface Copy {}
interface Linear {}
interface Drop {
    fun drop(this: &mut Self);
}
interface Sendable {}
interface Shared {}
interface Sized {}
struct Phantom<T> {}
```

**Auto-derivation rules:**

| Marker | Auto-impl condition | Opt-out |
|---|---|---|
| `Copy` | All fields are `Copy`; type has no `deinit` | `impl !Copy for T;` |
| `Sendable` | All fields are `Sendable` | `impl !Sendable for T;` |
| `Shared` | All fields are `Shared` | `impl !Shared for T;` |

`Linear` is never auto-derived. It must be explicitly implemented.

`Sized` is an implicit bound on all generic type parameters.

**Primitive marker table:**

| Type | Copy | Sendable | Shared |
|---|---|---|---|
| `i8`..`i64`, `u8`..`u64`, `isize`, `usize` | yes | yes | yes |
| `f32`, `f64` | yes | yes | yes |
| `bool` | yes | yes | yes |
| `char` | yes | yes | yes |
| `str` | yes (as `&str`) | yes | yes |
| `&T` | yes | yes when `T: Shared` | yes when `T: Shared` |
| `&mut T` | no | yes when `T: Sendable` | no |
| `Own<T>` | no | yes when `T: Sendable` | no |
| `[T; N]` | yes when `T: Copy` | yes when `T: Sendable` | yes when `T: Shared` |
| `T[]` (slice) | yes (as `&[T]`) | yes when `T: Shared` | yes when `T: Shared` |
| Function types | yes | yes | no |

### Operator Interfaces (Normative)

**Arithmetic:**

```zom
interface Add<Rhs = Self> {
    type Output;
    fun add(this, rhs: Rhs) -> Output;
}
interface Sub<Rhs = Self> {
    type Output;
    fun sub(this, rhs: Rhs) -> Output;
}
interface Mul<Rhs = Self> {
    type Output;
    fun mul(this, rhs: Rhs) -> Output;
}
interface Div<Rhs = Self> {
    type Output;
    fun div(this, rhs: Rhs) -> Output;
}
interface Rem<Rhs = Self> {
    type Output;
    fun rem(this, rhs: Rhs) -> Output;
}
```

**Unary:**

```zom
interface Neg {
    type Output;
    fun neg(this) -> Output;
}
interface Not {
    type Output;
    fun not(this) -> Output;
}
```

**Bitwise:**

```zom
interface BitAnd<Rhs = Self> {
    type Output;
    fun bitAnd(this, rhs: Rhs) -> Output;
}
interface BitOr<Rhs = Self> {
    type Output;
    fun bitOr(this, rhs: Rhs) -> Output;
}
interface BitXor<Rhs = Self> {
    type Output;
    fun bitXor(this, rhs: Rhs) -> Output;
}
interface Shl<Rhs = Self> {
    type Output;
    fun shl(this, rhs: Rhs) -> Output;
}
interface Shr<Rhs = Self> {
    type Output;
    fun shr(this, rhs: Rhs) -> Output;
}
```

**Assign variants** (for `+=`, `-=`, etc.):

```zom
interface AddAssign<Rhs = Self> {
    fun addAssign(this: &mut Self, rhs: Rhs);
}
// SubAssign, MulAssign, DivAssign, RemAssign,
// BitAndAssign, BitOrAssign, BitXorAssign, ShlAssign, ShrAssign
```

**Comparison:**

```zom
enum Ordering {
    Less,
    Equal,
    Greater
}

interface Eq {
    fun eq(this, other: &Self) -> bool;
    // Provided: fun ne(this, other: &Self) -> bool { !this.eq(other) }
}

interface Ord : Eq {
    fun cmp(this, other: &Self) -> Ordering;
    // Provided: lt, le, gt, ge, min, max, clamp
}
```

**Indexing:**

```zom
interface Index<Idx> {
    type Output;
    fun index(this, idx: Idx) -> &Output;
}
interface IndexMut<Idx> : Index<Idx> {
    fun indexMut(this: &mut Self, idx: Idx) -> &mut Output;
}
```

### Iterator Interfaces (Normative)

```zom
// core::iter

interface Iterator {
    type Item;
    fun next(this: &mut Self) -> Maybe<Self::Item>;

    // Provided methods (all have default implementations):
    // Adapters (lazy): map, filter, filterMap, flatMap, flatten, take, skip,
    //   takeWhile, skipWhile, chain, zip, enumerate, peekable, fuse, stepBy,
    //   scan, inspect, cloned, copied
    // Consumers: collect, fold, forEach, find, findMap, position, count,
    //   last, nth, any, all, sum, product, max, min, maxByKey, minByKey,
    //   reduce, partition, unzip
}

interface IntoIterator {
    type Item;
    type IntoIter: Iterator<Item = Item>;
    fun intoIter(this) -> IntoIter;
}

interface FromIterator {
    type Item;
    fun fromIter<I: IntoIterator<Item = Item>>(iter: I) -> Self;
}

interface DoubleEndedIterator : Iterator {
    fun nextBack(this: &mut Self) -> Maybe<Item>;
}

interface ExactSizeIterator : Iterator {
    fun len(this) -> usize;
    fun isEmpty(this) -> bool;
}

interface Extend {
    type Item;
    fun extend<I: IntoIterator<Item = Item>>(this: &mut Self, iter: I);
}
```

### Conversion Interfaces (Normative)

```zom
// core::convert

interface From<T> {
    fun from(value: T) -> Self;
}

interface Into<T> {
    fun into(this) -> T;
}
// Blanket: impl<T, U: From<T>> Into<U> for T

interface TryFrom<T> {
    type Error;
    fun tryFrom(value: T) -> (Self | Self.Error);
}

interface TryInto<T> {
    type Error;
    fun tryInto(this) -> (T | Self.Error);
}
// Blanket: impl<T, U: TryFrom<T>> TryInto<U> for T

interface AsRef<T> {
    fun asRef(this) -> &T;
}

interface AsMut<T> {
    fun asMut(this) -> &mut T;
}

interface Borrow<T> {
    fun borrow(this) -> &T;
}

interface BorrowMut<T> : Borrow<T> {
    fun borrowMut(this) -> &mut T;
}

interface FromStr {
    type Error;
    fun fromStr(s: str) -> (Self | Self.Error);
}
```

### Formatting Interfaces (Normative)

```zom
// core::fmt

interface Display {
    fun format(this, f: &mut Formatter);
}

interface Debug {
    fun format(this, f: &mut Formatter);
}

interface Write {
    fun writeStr(this: &mut Self, s: str);
    fun writeChar(this: &mut Self, c: char);
}
```

`Formatter` is a core type that provides incremental write methods and
format-spec parsing (fill, align, width, precision, sign, alternate).

**`Debug` derive:** The compiler derives `Debug` for aggregates whose fields
are all `Debug`.

**`toString()`:** Blanket method on `Display`:

```zom
// Blanket: impl<T: Display> ToString for T {
//     fun toString(this) -> String { ... }
// }
```

### Default Interface (Normative)

```zom
// core::default

interface Default {
    fun default() -> Self;
}
```

Implementations: primitives default to zero/false; `Maybe<T>` defaults to
`None`; aggregates are derived when all fields are `Default`.

### Hash Interface (Normative)

```zom
// core::hash

interface Hash {
    fun hash(this, hasher: &mut Hasher);
}

interface Hasher {
    fun writeBytes(this: &mut Self, bytes: u8[]);
    fun writeU8(this: &mut Self, v: u8);
    fun writeU16(this: &mut Self, v: u16);
    fun writeU32(this: &mut Self, v: u32);
    fun writeU64(this: &mut Self, v: u64);
    fun writeI8(this: &mut Self, v: i8);
    fun writeI16(this: &mut Self, v: i16);
    fun writeI32(this: &mut Self, v: i32);
    fun writeI64(this: &mut Self, v: i64);
    fun writeUsize(this: &mut Self, v: usize);
    fun writeIsize(this: &mut Self, v: isize);
    fun finish(this) -> u64;
}
```

### Primitive Inherent Methods (Normative)

**Integers (`i8`..`i64`, `u8`..`u64`, `isize`, `usize`):**

Constants: `MIN`, `MAX`, `BITS`.

Arithmetic (four flavors):

| Flavor | Methods |
|---|---|
| Panicking | `a + b` (operator), `a - b`, `a * b`, `a / b`, `a % b` |
| Checked | `checkedAdd`, `checkedSub`, `checkedMul`, `checkedDiv`, `checkedRem`, `checkedNeg`, `checkedShl`, `checkedShr` |
| Wrapping | `wrappingAdd`, `wrappingSub`, `wrappingMul`, `wrappingDiv`, `wrappingRem`, `wrappingNeg`, `wrappingShl`, `wrappingShr` |
| Saturating | `saturatingAdd`, `saturatingSub`, `saturatingMul` |

Bit manipulation: `countOnes`, `countZeros`, `leadingZeros`, `trailingZeros`,
`reverseBits`, `swapBytes`, `rotateLeft`, `rotateRight`.

Byte order: `toBe`, `fromBe`, `toLe`, `fromLe`, `toBeBytes`, `fromBeBytes`,
`toLeBytes`, `fromLeBytes`, `toNeBytes`, `fromNeBytes`.

Math: `abs`, `unsignedAbs`, `absDiff`, `signum`, `isPositive`, `isNegative`,
`clamp`, `midpoint`, `pow`, `divEuclid`, `remEuclid`, `isPowerOfTwo`,
`nextPowerOfTwo`, `checkedNextPowerOfTwo`, `nextMultipleOf`,
`checkedNextMultipleOf`, `ilog2`, `ilog10`, `ilog`, `checkedIlog2`,
`checkedIlog10`, `checkedIlog`.

**Floats (`f32`, `f64`):**

Constants: `INFINITY`, `NEG_INFINITY`, `NAN`, `MIN`, `MAX`, `MIN_POSITIVE`,
`EPSILON`, `MANTISSA_DIGITS`, `RADIX`, `PI`, `E`.

Math: `abs`, `sqrt`, `cbrt`, `powi`, `powf`, `exp`, `exp2`, `ln`, `log2`,
`log10`, `hypot`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`,
`sinh`, `cosh`, `tanh`, `floor`, `ceil`, `round`, `trunc`, `fract`,
`copysign`, `signum`, `mulAdd`, `recip`, `toDegrees`, `toRadians`,
`toBits`, `fromBits`, `toBeBytes`, `fromBeBytes`, `toLeBytes`,
`fromLeBytes`, `totalCmp`, `divEuclid`, `remEuclid`.

Classification: `isNaN`, `isInfinite`, `isFinite`, `isNormal`, `isSubnormal`,
`isZero`, `isSignPositive`, `isSignNegative`, `classify`.

**`bool`:**

Methods: `then`, `thenSome`.

**`char`:**

Methods: `isAlphabetic`, `isNumeric`, `isAlphanumeric`, `isWhitespace`,
`isControl`, `isDigit`, `isLowercase`, `isUppercase`, `isAscii`,
`toLowercase`, `toUppercase`, `toDigit`, `fromDigit`, `lenUtf8`,
`lenUtf16`, `encodeUtf8`, `encodeUtf16`, `isAsciiDigit`, `isAsciiAlphabetic`,
`isAsciiAlphanumeric`, `isAsciiHexDigit`, `toAsciiLowercase`,
`toAsciiUppercase`, `eqIgnoreAsciiCase`.

**`str`:**

Length and emptiness: `length` (property), `isEmpty`.

Predicates: `startsWith`, `endsWith`, `contains`, `eqIgnoreAsciiCase`.

Search: `find`, `rfind`, `matches`, `matchIndices`.

Split: `split`, `splitWhitespace`, `splitAsciiWhitespace`, `splitTerminator`,
`lines`, `splitOnce`, `rsplitOnce`.

Trim: `trim`, `trimStart`, `trimEnd`, `trimMatches`, `trimStartMatches`,
`trimEndMatches`.

Transform: `toLowercase`, `toUppercase`, `repeat`, `replace`, `replacen`.

Parse: `parse<T: FromStr>() -> (T | T.Error)`.

Conversion: `asBytes`, `bytes`, `chars`, `charIndices`.

### Spec Drift Resolution

This RFC resolves the following spec references:

| Spec reference | Resolution |
|---|---|
| `Option<T>` (Ch.03, Ch.10) | Replaced by `Maybe<T>` in this RFC, `core::maybe` |
| `Result<T, E>` (Ch.10) | Removed; `T \| E` error unions are the result type |
| `std::marker::Sendable` (Ch.06, Ch.16) | Defined in this RFC, `core::marker` (not `std::marker`) |
| `std::marker::Shared` (Ch.06, Ch.16) | Defined in this RFC, `core::marker` (not `std::marker`) |
| `std::collections::HashMap` (Ch.03) | Deferred to `std` collections RFC |
| `Atomic<T>` (Ch.03, Ch.15) | Defined in this RFC, `core::atomic` |
| `::core::Value` (Ch.03) | Removed; no such type. The example will use a concrete type. |
| `Vec<T>` (Ch.03) | Deferred to `std` collections RFC |
| `Map<K, V>` (Ch.03) | Deferred to `std` collections RFC |
| `print()` (throughout) | `std::io::print`; spec examples will show import |
| `toString()` (throughout) | Blanket impl on `Display`, `core::fmt` |
| `Debug` (Ch.09) | Defined in this RFC, `core::fmt` |
| `Show` (Ch.09) | Replaced by `Display` in this RFC; spec example will be updated |
| `Display` (Ch.06, Ch.09) | Defined in this RFC, `core::fmt`; signature updated to `format(this, f: &mut Formatter)` |
| `Iterator` (Ch.03) | Defined in this RFC, `core::iter`; `next()` returns `Maybe<Item>` |
| `Numeric<T>` (Ch.09) | Deferred; operator interfaces in this RFC replace it |
| `Comparable<T>` (Ch.09) | Replaced by `Eq` + `Ord` in this RFC |
| `Hash<T>` (Ch.09) | Defined in this RFC, `core::hash` |

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| Core library source | `core/src/core/**` | `runtime-memory` |
| Spec chapters | `docs/spec/chapters/03-types.md`, `06-declarations.md`, `09-interfaces.md`, `10-enumerations.md`, `11-error-handling.md`, `15-concurrency.md`, `16-attributes-and-annotations.md` | `spec-audit` |
| RFC 0025 | `docs/rfc/0025-source-backed-core-library-architecture.md` | `rfc` |
| RFC 0024 | `docs/rfc/0024-standard-marker-authority.md` | `rfc` |
| Conformance tests | `tests/conformance/corpus/core/**` | `verification` |

## Security And Safety Impact

The `Copy` marker is compiler-verified: a type with a `deinit` method cannot
be `Copy`. The `Sendable` and `Shared` markers are auto-derived structurally,
making thread safety opt-out by default. The `Linear` marker prevents
accidental drops of resources that must be explicitly consumed.

The `unwrap` methods on `Maybe` trap on `None`. This is a deliberate escape
hatch, not a safety hole: the trap is a controlled panic, not undefined
behavior.

The `FromError` trait ensures that error propagation across error domains is
explicit and checked at the signature level, preventing silent error
swallowing.

## Drawbacks And Risks

1. **API surface is large.** The full `Maybe` method surface is ~30 methods.
   This is a maintenance burden for the core library implementation.
   Mitigation: most methods are 2-line combinators that can be mechanically
   generated or implemented in a single pass.

2. **Prelude additions are hard to reverse.** Once a name is in the prelude,
   removing it breaks every program. Mitigation: the prelude is governed by
   RFC; additions require a separate RFC.

3. **Spec drift resolution touches many chapters.** Updating all spec
   references is a large mechanical change. Mitigation: the changes are
   mechanical (path updates, `Option` → `Maybe`, `Result` removal) and can
   be done in one pass.

4. **`Debug` derive requires compiler support.** The compiler must derive
   `Debug` for aggregates. This is a new compiler feature. Mitigation:
   `Debug` can be manually implemented initially; derive is a follow-up.

5. **Error-union combinators require language support.** Methods on `T | E`
   unions may require new language features (methods on union types).
   Mitigation: combinators can be provided as free functions initially.

## Alternatives Considered

### Separate `Result<T, E>` enum (Rust)

Introduce a `Result<T, E>` enum alongside `T | E` error unions. Rejected
because ZOM already has language-level error unions with `raises` clauses
and `?!`/`!!`/`?:` operators. A separate `Result` type would create two
error rails and force conversions between them.

### `Option<T>` instead of `Maybe<T>` (Rust naming)

Use `Option<T>` as the ZOM language name. Rejected because `Maybe<T>` matches
the `zc::Maybe` C++ support library, is shorter, and the spec's `Option<T>`
was only an example, not a normative definition.

### Single-module approach (Swift)

Put everything in one implicitly-imported module. Rejected because ZOM's
ownership model makes the heap/no-heap boundary meaningful.

### No core library (Zig)

Require explicit imports for everything. Rejected because `Maybe`, error-union
support, markers, and `Iterator` are pervasive enough that implicit
availability reduces friction without hiding anything.

### `Send`/`Sync` naming (Rust) instead of `Sendable`/`Shared`

Use Rust's `Send` and `Sync` naming. Rejected because the spec already uses
`Sendable` and `Shared` throughout (Ch.03, Ch.04, Ch.09, Ch.12, Ch.16).

## Compatibility And Rollout

This RFC is a specification, not an implementation. The rollout plan:

1. Accept this RFC (status: DRAFT → ACCEPTED).
2. Update spec chapters to resolve drift (mechanical path updates,
   `Option` → `Maybe`, `Result` removal, `Show` → `Display`).
3. Implement core library source files in `core/src/core/`.
4. Add conformance tests for each core type and interface.
5. Move RFC 0025 and RFC 0024 to LANDED once the core library is
   implemented and verified.

No existing code is broken by this RFC because the core library is currently
declaration-only (only `Copy` and `Linear` markers exist).

## Documentation And Teaching Plan

- Update `docs/spec/chapters/03-types.md` to reference `core::maybe::Maybe`
  instead of showing `Option<T>` as a bare enum.
- Update `docs/spec/chapters/10-enumerations.md` to remove `Result<T, E>`
  and `Option<T>` examples (use other enum examples).
- Update `docs/spec/chapters/09-interfaces.md` to reference the operator
  interfaces defined in this RFC and replace `Show` with `Display`.
- Update `docs/spec/chapters/15-concurrency.md` to define `Atomic<T>` or
  reference `core::atomic`.
- Update `docs/spec/chapters/16-attributes-and-annotations.md` to reference
  `core::marker::Sendable` and `core::marker::Shared` instead of
  `std::marker::*`.
- Add a `docs/design/language/core-library.md` design note explaining the
  module structure and prelude.
- Update `core/README.md` to reflect the expanded core library.

## Operational Readiness

None. This RFC is a specification; it has no runtime, CLI, or CI impact
until implementation begins.

## Acceptance Criteria

- [ ] RFC reviewed and accepted (status: ACCEPTED).
- [ ] All spec drift references resolved (no `std::marker::*`, no
      `::core::Value`, no undefined `Atomic<T>`, no `Option<T>` or
      `Result<T, E>` in spec examples).
- [ ] Core library source files exist for every module listed in
      "Module Paths".
- [ ] Conformance tests exist for `Maybe`, error-union combinators, marker
      interfaces, operator interfaces, iterator interfaces, conversion
      interfaces, and formatting interfaces.
- [ ] `core/README.md` documents the module structure and prelude.
- [ ] `docs/design/language/core-library.md` design note exists.

## Implementation Plan

1. Update spec chapters to resolve drift (mechanical).
2. Implement `core::maybe::Maybe` with full method surface.
3. Implement `core::result` (`FromError`, `Error`, error-union combinators).
4. Implement `core::marker` interfaces (`Drop`, `Sendable`, `Shared`,
   `Sized`, `Phantom<T>`).
5. Implement `core::cmp` (`Ordering`, `Eq`, `Ord`).
6. Implement `core::ops` operator interfaces.
7. Implement `core::iter` iterator interfaces.
8. Implement `core::convert` conversion interfaces.
9. Implement `core::fmt` formatting interfaces.
10. Implement `core::default::Default`.
11. Implement `core::hash` (`Hash`, `Hasher`).
12. Implement `core::mem` (`sizeOf`, `alignOf`, `swap`, `replace`, `take`).
13. Implement `core::atomic` (`AtomicBool`, `AtomicI32`, `AtomicUsize`).
14. Implement `core::duration::Duration`.
15. Implement primitive inherent methods.
16. Add conformance tests for all of the above.

## Test Plan

- Build: `cmake --preset sanitizer && cmake --build --preset sanitizer`
- Unit tests: `ctest --preset default -L unittest`
- Lit tests: `ctest --preset default -L lit`
- Conformance: `ctest --preset default -R conformance-ast-coverage`
- Format: `python3 scripts/check-format.py`
- Spec alignment: `/skill spec-alignment`

## Open Questions

1. **Error-union combinator mechanism:** Methods on `T | E` unions may
   require new language features. Should combinators be provided as methods
   on union types (requires language support) or as free functions (works
   today)? Leaning: free functions initially, methods when the language
   supports them.

2. **`Fn`/`FnMut`/`FnOnce` hierarchy:** ZOM function types encode capture
   discipline in the type system. Does the core library need explicit `Fn`
   interfaces, or are function types sufficient? Leaning: function types are
   sufficient; no `Fn` interfaces needed.

3. **`String` trait:** ZOM has `str` (unsized UTF-8 view) and will have
   `String` (owned, growable) in `std`. Should `core` define a `String`
   trait or interface for generic code that works with both? Leaning: no,
   use `AsRef<str>` and `Into<String>` bounds.

4. **`Cell`/`RefCell` in core:** Interior mutability types are useful but
   may not be needed immediately. Should they be in the initial core
   library or deferred? Leaning: defer until a concrete use case exists.

5. **`Future`/async in core:** The spec has a concurrency chapter (Ch.15)
   but the async trait surface should land with the runtime, not ahead of
   it. Leaning: defer to a concurrency RFC.

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-10-01 | DRAFT | Initial draft. |
