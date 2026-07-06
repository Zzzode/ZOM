# Interfaces

Interfaces define contracts that types can implement, enabling polymorphism and code reuse without coupling behavior to a specific class hierarchy. An interface declares a set of method signatures, property signatures, and associated type requirements; any type that satisfies those requirements — via an `impl I for T` block or an in-class heritage clause — is said to *implement* the interface.

## 9.1 Basic Interface Declaration

An interface declaration introduces a new nominal interface type. The declaration header consists of an optional modifier list, the `interface` keyword, a binding identifier, optional type parameters, and an optional heritage clause. The body enumerates the interface's required members.

```zom
interface Drawable {
    fun draw();
    get bounds() -> Rectangle;
}

interface Movable {
    fun move(deltaX: f64, deltaY: f64);
    get position() -> Point;
}
```

A minimal empty interface is valid and useful as a marker:

```zom
interface Marker {}
```

### 9.1.1 Visibility and Modifiers

Interface declarations accept a `ModifierList` prefix. The `public` modifier makes the interface visible across module boundaries; `private` restricts it to the enclosing module.

```zom
public interface Container<T> : Iterable {
    fun size() -> i32;
}
```

Individual interface members may also carry visibility modifiers (`private`, `protected`) and behavioral modifiers (`mutating`, `override`, `readonly`).

```zom
interface MixedAccess {
    protected fun helper();
    protected get context() -> Context;
    fun publicOp();
    get id() -> u64;
}
```

## 9.2 Interface Inheritance

An interface may inherit from one or more super-interfaces using the colon (`:`) syntax with `+` as the conjunction separator. This is the canonical form; the parser also accepts the legacy `extends` keyword for single inheritance, but the `:` + `+` form is preferred.

```zom
interface ReadableStream {
    fun read(buffer: u8[], offset: i32, length: i32) -> i32;
    fun close();
}

interface WritableStream {
    fun write(buffer: u8[], offset: i32, length: i32) -> i32;
    fun flush();
    fun close();
}

interface ReadWriteStream : ReadableStream + WritableStream {
    fun seek(position: i64);
    get position() -> i64;
}
```

Multiple super-interfaces are joined by `+`, which denotes logical conjunction (AND). The pipe `|` is reserved for union types and is rejected in interface heritage position.

### 9.2.1 Generic Interface Inheritance

Generic interfaces may inherit from other generic interfaces with type arguments:

```zom
interface Numeric<T> : Comparable<T> + Hash<T> {
    fun add(other: T) -> T;
}
```

## 9.3 Interface Members

The interface body contains three kinds of elements: method signatures, property signatures, and associated type declarations. All three are *required* — a type implementing the interface must provide a definition for each declared member.

### 9.3.1 Method Signatures

A method signature declares the name, parameter list, and optional return type of a method that implementors must provide. The signature ends with a semicolon (`;`). A method body (block statement) is **not permitted** inside an interface; the compiler emits a parse error if a `{ ... }` block follows the signature.

```zom
interface Writer {
    fun write_bytes(data: u8[]) -> i32;
    fun flush();
}
```

Methods may carry the `mutating` modifier to indicate that the call mutates the receiver:

```zom
interface Counter {
    mutating fun inc();
    mutating fun reset();
    get value() -> i64;
}
```

The `override` modifier is permitted on a method signature when the interface re-declares a method inherited from a super-interface, for example to tighten the return type:

```zom
interface OverrideReadonly {
    override fun toString() -> str;
    readonly get tag() -> str;
}
```

### 9.3.2 Property Signatures

Property signatures declare getter and setter requirements using the `get` and `set` keywords. A getter signature specifies the return type; a setter signature specifies the value parameter type.

```zom
interface UserRecord {
    get id() -> u64;
    get name() -> str;
    set name(v: str);
    set email(v: str);
}
```

The `readonly` modifier on a `get` signature indicates that the property is read-only (no setter obligation):

```zom
interface Named {
    readonly get name() -> str;
    readonly get id() -> u64;
}
```

### 9.3.3 Associated Types

An interface may declare associated types — type-level placeholders that each implementation must assign to a concrete type. Associated types are declared with the `type` keyword inside the interface body.

Four forms are supported:

```zom
interface Collection {
    type Item;                                    // (1) unconstrained
    type Error : Error;                           // (2) bounded
    type State = Closed | Open;                   // (3) with default
    type Iter<T>;                                 // (4) generic associated type (GAT)
}
```

The full form combines type parameters, bounds, and a default:

```zom
interface FullAssoc {
    type Element<T, U> : Show + Hash = T | U;
}
```

When an associated type carries both a bound and a default, the default must satisfy the bound.

Implementations assign associated types using the `type Name = ConcreteType;` syntax inside the `impl` block. See [§9.4 Standalone `impl I for T`](#94-standalone-impl-i-for-t-independent-implementation-blocks).

Associated type projections are resolved only when the source interface is
unique. If a type parameter has multiple bounds that declare the same
associated type name, the unqualified projection `T::Item` is ambiguous. Use
the fully qualified projection `<T as Interface>::Item` to select the source
interface explicitly.

## 9.4 Standalone `impl I for T` (Independent Implementation Blocks)

Not all behavior contracts can live inside the `class` body that declares the type. Two common cases motivate standalone implementation blocks: (a) the interface author owns the interface but does **not** own the target type (FFI types, standard-library types such as `u64`), and (b) the type author owns the type but wants to group impls into separate files for modularity (serialization, rendering, persistence in different compilation units).

### 9.4.1 Syntax

A standalone impl declaration uses the keyword `impl` followed by an interface bound list, the keyword `for`, and the target type. An optional `where`-clause constrains generic parameters.

```zom
impl Drawable for Button {
    fun draw() {
        print("Drawing " + this.text);
    }

    get bounds() -> Rectangle {
        return Rectangle(this.position, this.size);
    }
}
```

Multiple interfaces and markers may be combined with `+`:

```zom
impl Reader + Writer + Seekable for FileStream { }
```

An `impl` block may assign associated types:

```zom
class ByteReader {
    let buf: u8[];
    mut pos: i32;
}

impl Iterator for ByteReader {
    type Item = u8;

    fun hasNext() -> bool {
        return this.pos < this.buf.length;
    }

    fun next() -> u8? {
        if !this.hasNext() { return null; }
        let byte = this.buf[this.pos];
        this.pos = this.pos + 1;
        return byte;
    }
}
```

### 9.4.2 Generic Impls and Where-Clauses

Generic standalone impls may use a `where`-clause to constrain type parameters:

```zom
impl<T> Debug for Vec<T> where T: Debug {
    fun fmt(f: &mut Formatter) {
        f.write_char('[');
        for (mut i = 0; i < this.length; i = i + 1) {
            if i > 0 { f.write_str(", "); }
            Debug::fmt(this[i], f);
        }
        f.write_char(']');
    }
}
```

Note: `where`-clauses on `interface` declarations themselves are **not** supported; constraints on interface type parameters are expressed in the type parameter list directly.

### 9.4.3 Orphan Rule

An `impl I for T` block is legal if **either**: (1) `I` is declared in the current crate, OR (2) `T` is declared in the current crate. If both the interface and the target type are foreign to the current crate the compiler emits `ZOM0710 OrphanImpl`. This rule preserves coherence across crate boundaries: downstream crates cannot inject conflicting implementations for types and interfaces they do not own.

Common legitimate use cases:

- **External type + internal interface:** e.g. `impl JsonSerializable for u64` where `u64` is from the standard crate but `JsonSerializable` is local.
- **Internal type + external interface:** e.g. `impl Display for MyUuid` where `MyUuid` is local but `Display` comes from the standard library.

Per-crate coherence allows at most **one** `impl I for T` per `(I, T)` pair; if two distinct `impl I for T` blocks exist for the same nominal pair within the same crate, the compiler emits `ZOM0505 DuplicateImpl`. The cross-crate overlap case is rejected by `ZOM0714 AmbigImplOverlap` (Ch.22 §22.4).

### 9.4.4 Marker Forwarding

If the impl list includes markers (`+ Sendable`, `+ Shared`, etc.), the combined form is equivalent to writing separate `impl Sendable for T` declarations. The combined syntax is purely syntactic sugar — each marker in the list becomes a separate coherence entry.

## 9.5 Interface Inheritance & Diamond Resolution

When an interface inherits from multiple super-interfaces, a method name may be reachable through more than one path. The language specifies a deterministic set of resolution rules.

### 9.5.1 Inheritance-Conflict Resolution Rules (IR-1 .. IR-4)

**IR-1: Identical signatures are redundant.** Redundant redeclaration of a method already inherited from a superinterface is allowed but produces the `ZOM0478 RedundantInheritedMethod` warning (not an error).

**IR-2: Same name, different parameter list = independent overload.** No conflict is reported; each signature is tracked separately and dispatch selects the matching overload by argument shape.

**IR-3: Same name, same params, different return type = incompatible.** The compiler emits `ZOM0482 IncompatibleReturnType` error. The user must resolve by explicitly re-declaring the method in the child interface with the single correct return type.

**IR-4: Conflicting pure-method obligations.** If two super-interfaces declare the same method signature as abstract (no body), there is no ambiguity — the concrete type simply implements it once. The obligation is the same regardless of which path it is reached through.

### 9.5.2 Diamond Inheritance Structure

The following class diagram illustrates a canonical diamond where `D` inherits from both `B` and `C`, each of which extends the common root `A`:

```mermaid
classDiagram
    direction TB
    class A {
        <<interface>>
        +foo() pure
        +bar() pure
    }
    class B {
        <<interface>>
        +bar() pure
    }
    class C {
        <<interface>>
        +bar() pure
    }
    class D {
        <<interface>>
    }
    A <|-- B : extends
    A <|-- C : extends
    B <|-- D : extends
    C <|-- D : extends
```

Under IR-1, `D` inherits `foo()` from `A` without conflict. For `bar()`, both `B` and `C` redeclare the same signature; IR-1 treats this as redundant and emits a warning. The concrete class implementing `D` provides a single `bar()` that satisfies all inherited obligations.

### 9.5.3 Explicit Qualification Syntax

When a type implements multiple interfaces that share a method name, the user may disambiguate inside the implementing class body by invoking a specific interface's method using the `InterfaceName::method` qualified-call form:

```zom
interface IBase { fun foo() -> str; }
interface IA : IBase { fun bar() -> str; }
interface IB : IBase { fun baz() -> str; }

class C {
    let data: str;
}

impl IA for C {
    fun foo() -> str { return "IA: " + this.data; }
    fun bar() -> str { return this.foo(); }
}

impl IB for C {
    fun foo() -> str { return "IB: " + this.data; }
    fun baz() -> str { return IB::foo(this); }
}
```

The qualified form `IB::foo(this)` passes the receiver as its first argument. The resolution is static: it always dispatches to the method as defined in the named interface's impl, independent of any further subclasses of `C`.

## 9.6 Object-Safe Interfaces (dyn Prerequisite)

An interface `I` is *object-safe* iff values of type `T: I` can be coerced to `dyn I`, the existential type form defined in [Ch.03 §Existential Types](03-types.md). An object-unsafe interface is rejected at the point of an attempted coercion with the specific diagnostic matching the first rule that failed.

Object-safety is a vtable-layout property: every method on the interface must be representable as a fixed-size function pointer slot whose calling convention is identical for every implementor `T`.

### 9.6.1 Object-Safety Decision Flowchart

```mermaid
flowchart TD
    Start([Input: interface I]) --> OS0{"OS-0: If I extends J,<br/>is J object-safe?"}
    OS0 -->|No| E0[ZOM0338 DynSuperNotObjectSafe]
    OS0 -->|Yes| OS1{"OS-1: Any generic method<br/>(method-level type params)?"}
    OS1 -->|Yes| E1[ZOM0331 DynGenericMethod]
    OS1 -->|No| OS2{"OS-2: Any method returning<br/>bare Self?"}
    OS2 -->|Yes| E2[ZOM0332 DynSelfReturn]
    OS2 -->|No| OS3{"OS-3: Any method with move self<br/>#[zom::param::move]?"}
    OS3 -->|Yes| E3[ZOM0333 DynMoveSelf]
    OS3 -->|No| OS4{"OS-4: All associated types<br/>bound in dyn head?"}
    OS4 -->|No| E4[ZOM0334 DynUnassociatedType]
    OS4 -->|Yes| OS5{"OS-5: Any static method<br/>(no this receiver)?"}
    OS5 -->|Yes| E5[ZOM0335 DynStaticMethod]
    OS5 -->|No| OS6{"OS-6: Any GAT<br/>(lifetime-parametric assoc type)?"}
    OS6 -->|Yes| E6[ZOM0336 DynGatNotAllowed]
    OS6 -->|No| OS7{"OS-7: All param/return types<br/>impl Sized?"}
    OS7 -->|No| E7[ZOM0337 DynUnsizedParameter]
    OS7 -->|Yes| OK([dyn I allowed])
```

### 9.6.2 OS-0 (Inheritance Closure)

If `I : J` (I extends J) and `I` is object-safe, every superinterface `J` must also be object-safe. Otherwise the compiler emits `ZOM0338 DynSuperNotObjectSafe`.

### 9.6.3 OS-1 No Generic Methods

Methods may not introduce their own type parameters. Each distinct instantiation would otherwise require a fresh vtable slot and the set of instantiations is unbounded.

```zom
interface X { fun map<T>(f: fun(Self)->T) -> T; }   // ZOM0331 DynGenericMethod
```

### 9.6.4 OS-2 No Methods Returning Bare Self

`Self` (the concrete implementing type) cannot be returned by value because its size is not statically known behind `dyn`. `Self?` is allowed only because the `dyn` calling convention lowers it as an explicit nullable union whose success payload is materialized behind the erased data pointer. The source type remains `Self | null`; the pointer-sized representation is a dyn ABI lowering detail, not the general layout of every nullable union.

```zom
interface Cloneable { fun clone() -> Self; }           // ZOM0332 DynSelfReturn
```

### 9.6.5 OS-3 No Move-Consume Self

A receiver with the linear move attribute, `fun consume(#[zom::param::move] this)`, is forbidden. Linear move of a `dyn I` receiver requires compile-time known size, which is not available. Allowed receivers are `borrow this`, `&mut this`, and `self` passed by non-move reference.

```zom
interface Consumable { fun consume(#[zom::param::move] this); }   // ZOM0333 DynMoveSelf
```

### 9.6.6 OS-4 All Associated Types Bound in the dyn Head

If an interface declares associated types, every one must be assigned in the `dyn` head. Writing bare `dyn Iterator` leaves `Item` unknown and therefore breaks the calling convention of `next() -> Item?`; the coerced type must be `dyn Iterator<Item = T>`.

```zom
let it: dyn Iterator = make_iter();                       // ZOM0334 DynUnassociatedType
let it_ok: dyn Iterator<Item = u8> = make_iter();         // OK
```

### 9.6.7 OS-5 No Static Methods

A method lacking any form of `this` receiver has no dispatch target in the vtable. Such methods remain callable through the qualified path `I::static_method()`; they are simply excluded from the dyn vtable.

```zom
interface Factory { static fun new() -> Self; }           // ZOM0335 DynStaticMethod
```

### 9.6.8 OS-6 No Generic Associated Types (GAT)

An associated type that introduces its own lifetime or type parameters (e.g. `type Iter<'a>;`) is a GAT. GAT vtable representation is deferred to post-v1.

```zom
interface Iterable { type Iter<'a>: Iterator; }           // ZOM0336 DynGatNotAllowed
```

### 9.6.9 OS-7 All Parameters and Returns Are Sized

Every method parameter type and return type, modulo the explicit exceptions above, must impl the `Sized` marker at coercion time. DSTs such as `[T]` or unsized structs cannot flow across a vtable boundary because their layout is not static.

Diagnostic: `ZOM0337 DynUnsizedParameter`.

### 9.6.10 Examples of dyn-Compatible Interfaces

A minimal object-safe interface:

```zom
interface Writer {
    fun write_bytes(data: u8[]) -> i32;
    fun flush();
}

fun write_all(w: &mut dyn Writer, data: u8[]) {
    mut remaining = data.length;
    while remaining > 0 {
        let written = w.write_bytes(data.slice(data.length - remaining));
        remaining = remaining - written;
    }
    w.flush();
}
```

A dyn-compatible interface combined with marker bounds for cross-thread safety:

```zom
interface RpcHandler {
    fun handle(req: Request) -> Response;
}

fun dispatch<T>(h: &(dyn RpcHandler + Sendable + Shared), req: Request)
    -> Task<Response>
{
    return async { h.handle(req) };
}
```

## 9.7 Unsafe Interfaces and Unsafe Impl

Some interfaces carry semantic contracts that the compiler cannot statically verify — for example, "this allocator is safe to call concurrently from multiple threads" or "this iterator yields valid UTF-8." Such interfaces are *semantically unsafe*: implementing them correctly requires the programmer to attest to invariants that the type system cannot prove.

### 9.7.1 `unsafe impl` Syntax

The `unsafe` keyword prefixes the `impl` block to signal that the implementor is attesting to the interface's semantic contract:

```zom
/// # Safety
/// Implementors must guarantee that all methods are safe to call concurrently
/// from multiple threads without external synchronization.
interface GlobalAllocator {
    fun allocate(size: usize, align: usize) -> *mut u8;
    fun deallocate(ptr: *mut u8, size: usize, align: usize);
}

unsafe impl GlobalAllocator for MyArena {
    fun allocate(size: usize, align: usize) -> *mut u8 { /* ... */ }
    fun deallocate(ptr: *mut u8, size: usize, align: usize) { /* ... */ }
}
```

Omitting `unsafe` produces `ZOM0907 UnsafeInterfaceImplMustBeUnsafe` when the target interface is marked as requiring unsafe implementation.

### 9.7.2 Calling Methods of Semantically Unsafe Interfaces

When an interface is documented as requiring `unsafe impl`, calling its methods through a `dyn` reference may require the caller to be in an `unsafe { }` context, depending on the specific interface's documented contract. The compiler does not automatically gate all calls; the safety obligation is documented in the interface's `# Safety` doc section.

### 9.7.3 Documentation Required

Every semantically unsafe interface SHOULD include a `# Safety` section in its doc comment describing the invariants that implementors must uphold. Lint `ZOM0921 MissingUnsafeInterfaceSafetyDoc` warns when absent.

### 9.7.4 When to Use

Mark an interface as semantically unsafe when correct behavior depends on invariants that are:

- **Semantic rather than structural.** E.g., "this allocator is thread-safe" — the compiler cannot prove this from types alone.
- **Global rather than local.** E.g., "this global state has exactly one writer."
- **Protocol-based.** E.g., "methods must be called in order A then B then C."

If the invariant can be expressed in the type system (e.g., via marker bounds or associated types), prefer that approach over relying on `unsafe impl`.

## 9.8 Interfaces as Generic Bounds

Interface names and marker names both participate in the same `BoundList` syntax, shared with [Ch.12 §Generics](12-generics.md). The full `BoundList` grammar (normative in Ch.12) is:

```ebnf
BoundList = BoundItem ( "+" BoundItem )* ;

BoundItem = ( "!" )? MarkerPath
          | InterfaceName ( "<" GenericArgs ">" )?
          ;
```

A type parameter's bound list therefore has the general form `<T: Interface1<Arg> + Interface2 + Marker1 + !Marker2>`.

### 9.8.1 Negation Prefix (`!`) — Interface Bounds Are Positive Only

The `!` prefix is ONLY legal on marker bounds. Writing `!Drawable` as a bound is a semantic error emitted as `ZOM0422 NegativeInterfaceBoundNotAllowed`. Interfaces are behavioral contracts, not structural properties; the predicate "explicitly does NOT have interface I" is not meaningful in ZOM's type system because negative interface impls are deliberately not supported, and the orphan rule has no mechanism for coherently propagating negations across crate boundaries.

Marker bounds MAY use `!` to express a negative bound. For example `!Shared` means "definitely not shared" and is a valid structural predicate.

### 9.8.2 Examples

A single interface bound on a generic function (Ch.12 generic form):

```zom
fun sort<T: Comparable<T>>(arr: T[]) -> T[] {
    // standard in-place quicksort using Comparable::compareTo
    return arr;
}
```

Combining an interface bound with two positive marker bounds for thread-safety:

```zom
fun draw_all<T: Drawable + Sendable>(items: [T]) { for x in items x.draw(); }
```

Combining an interface bound, a positive marker bound, and a NEGATED marker bound to express "runnable on the local thread only, must be linear so the executor owns the task uniquely":

```zom
fun clone_into<T: Cloneable + Linear + !Shared>(x: T, target: &mut Vec<T>);
```

Attempting to negate an interface bound is an error. The following declaration is rejected with `ZOM0422 NegativeInterfaceBoundNotAllowed`:

```zom
// Incorrect — ZOM0422 NegativeInterfaceBoundNotAllowed
fun bad<T: !Drawable>(x: T);
```

### 9.8.3 Intersection Types vs. Bound Lists

The intersection operator `&` used in type expressions such as `Drawable & Rounded` (Ch.03) is structural and produces a type. The `+` separator used in bound lists such as `T: Drawable + Rounded` is predicate-level conjunction and produces a proof obligation. A type satisfies `T: I1 + I2` precisely when it satisfies both bounds simultaneously; the type expression `I1 & I2` as a standalone type is sugar for `dyn (I1 & I2)`, the existential form combining multiple object-safe interfaces.

## 9.9 Grammar Reference (Informative)

The following productions are reproduced from [Ch.17 Grammar Reference](17-grammar-reference.md) and the authoritative [`docs/design/syntax-ebnf.md`](../design/syntax-ebnf.md) for convenience.

```ebnf
InterfaceDecl  ::= ModifierList 'interface' BindingIdent TypeParameters?
                   InterfaceHeritage?
                   '{' InterfaceBody '}'

InterfaceHeritage ::= ':' InterfaceBoundList
InterfaceBoundList ::= InterfaceBound ( '+' InterfaceBound )*
InterfaceBound     ::= QualifiedPathOrIdent ( '<' TypeArgumentList '>' )?

InterfaceBody   ::= InterfaceElement*
InterfaceElement ::= ';'
                  | OuterAttributeList ModifierList 'fun' MethodSignature ';'
                  | OuterAttributeList ModifierList ('get' | 'set') PropertySignature ';'
                  | OuterAttributeList ModifierList 'type' Identifier TypeParameters?
                    ( ':' InterfaceBoundList )? ( '=' TypeExpr )? ';'

MethodSignature   ::= PropertyName CallSignature
CallSignature     ::= TypeParameters? ParameterClause FunctionSignature?
PropertySignature ::= PropertyName ParameterClause FunctionSignature?

StandaloneImplDecl ::= 'impl' TypeParameters? InterfaceBoundList 'for' TypeExpr
                       WhereClause?
                       '{' ImplMember* '}'

ImplMember     ::= ModifierList 'fun' BindingIdent TypeParameters? ParameterClause
                    FunctionSignature? ( ';' | BlockStatement )
                 | 'type' Identifier TypeParameters? '=' TypeExpr ';'
```

## 9.10 Summary

- Interfaces declare method signatures, property signatures, and associated type requirements that types satisfy via `impl I for T` blocks.
- Interface inheritance uses the colon (`:`) syntax with `+` for multiple super-interfaces (conjunction). The legacy `extends` keyword is accepted for single inheritance but `:` + `+` is preferred. Pipe `|` is rejected in heritage position.
- Interface method and property signatures end with a semicolon; method bodies are not permitted inside interface declarations.
- Associated types support four forms: unconstrained, bounded, defaulted, and generic (GAT). Full form combines type parameters, bounds, and defaults.
- Standalone `impl I for T` blocks extend interface coverage to foreign types and allow modular grouping of impls, governed by Ch.22's orphan rule (`ZOM0710 OrphanImpl`, cross-crate `ZOM0714 AmbigImplOverlap`) and the duplicate-impl coherence check (`ZOM0505 DuplicateImpl`).
- Multiple interface inheritance uses four conflict-resolution rules (IR-1..IR-4): redundant signatures warn (`ZOM0478`), independent overloads coexist, incompatible return types error (`ZOM0482`), and shared pure-method obligations converge.
- Eight object-safety rules (OS-0..OS-7) govern whether an interface can be coerced to `dyn I` (Ch.03 §Existential Types), each with a dedicated `ZOM033x` diagnostic.
- Semantically unsafe interfaces require `unsafe impl` to implement. The `unsafe` keyword appears on the `impl` block, not on the `interface` declaration. Lint `ZOM0921` warns when a `# Safety` doc section is missing.
- Interface names participate in Ch.12's generic bound lists alongside marker bounds; only marker bounds may be negated, and a negated interface bound is `ZOM0422 NegativeInterfaceBoundNotAllowed`.
