# Chapter 25 — Standard Prelude

> **Normative**
> This chapter defines the standard prelude — the collection of symbols the compiler auto-injects into every crate's root scope. The prelude exists to reduce boilerplate; it is deliberately small (fewer than 30 direct items and fewer than 100 transitive items). Implementations MUST NOT add implementation-specific symbols to the prelude without an edition gate.

---

## 25.0  Purpose

Every ZOM crate implicitly imports the standard prelude. Conceptually the compiler performs the equivalent of an `import std.prelude.v1.{ ... }` at the crate root scope, with the imported symbols assigned the *lowest* possible lookup priority. User-declared names and user-written imports always win over prelude names.

The prelude is *not* a language feature in the sense that the symbols it contains could not be authored in ordinary ZOM source code. Every symbol in the prelude is an ordinary item declared in the `std` crate under the `std::prelude::v1` module. The prelude mechanism is purely a convenience injection.

---

## 25.1  Prelude Symbol List

The items below are re-exported from `std::prelude::v1` and injected into every crate root. The list is normative. Implementations MUST inject exactly these symbols and no others in the default prelude, except under the edition-gated mechanism of §25.4.

### 25.1.1  Core nominal types

-   `Option<T>` — enum with variants `Some(T)` and `None`. Rust-style optional value.
-   `Result<T, E>` — enum with variants `Ok(T)` and `Err(E)`. Nominally wraps the nominal surface of the `T | E` union type for pattern-matching convenience.
-   `Vec<T>` — heap-allocated growable array.
-   `VecDeque<T>` — double-ended queue with amortized O(1) push and pop at both ends.
-   `String` — owned heap-allocated UTF-8 string.
-   `StrSlice<'a>` — borrowed UTF-8 string slice. Nominal alias of `&'a [u8]` with validated UTF-8 encoding.
-   `Box<T>` — unique owned heap pointer. Linear.
-   `Rc<T>` — single-threaded reference-counted shared pointer. `!Sendable`.
-   `Arc<T>` — atomic reference-counted shared pointer. Sendable and Shared.
-   `Cell<T>` — interior-mutable single-threaded cell.
-   `RefCell<T>` — single-threaded dynamically-borrowed interior-mutable cell with runtime borrow tracking.
-   `Mutex<T>` — multi-threaded locking interior-mutable cell.
-   `RwLock<T>` — multi-threaded read-write lock interior-mutable cell.

### 25.1.2  Core marker traits

These markers are automatically attached by the compiler to primitive and user-declared types as appropriate. They appear in error messages and are usable in trait bounds without explicit import.

-   `Sendable` — values of the type may be transferred across thread or worker boundaries.
-   `Shared` — values of the type may be shared across threads through `&T`, `Arc<T>`, and similar shared indirection.
-   `Linear` — single ownership; values must be consumed explicitly; they cannot be dropped silently. `drop()` is the explicit consumption.
-   `Copy` — bitwise-copy semantics; values are implicitly duplicated on assignment.
-   `Drop` — the type carries an explicit destructor.
-   `Sized` — compile-time-known size. The default for all type parameters unless `?Sized` appears in the bound.

### 25.1.3  Functions (top-level convenience)

-   `print(format: FormatString, args: ...)` — writes formatted output to the standard output stream.
-   `println(format: FormatString, args: ...)` — writes formatted output to the standard output stream followed by a newline.
-   `eprint(format: FormatString, args: ...)` — writes formatted output to the standard error stream.
-   `eprintln(format: FormatString, args: ...)` — writes formatted output to the standard error stream followed by a newline.
-   `dbg(expr)` — wraps an expression; prints `[file:line:col] expr = Debug(expr)` to the standard error stream; passes the value through unchanged. Non-intrusive debug helper.
-   `drop<T: Linear>(value: T)` — explicit linear consumption; permits the value to be deallocated.
-   `own<T: Linear>(value: T) -> T` — explicit ownership take (identity function with semantic weight for Linear types).
-   `borrow<T>(value: &T) -> Borrowed<T>` — creates an abstract borrow token.
-   `todo!()` — panics at runtime with the message "not yet implemented".
-   `unreachable!()` — panics with the message "entered unreachable code"; treated as unreachable in codegen.
-   `compile_error!(message: str)` — compile-time error if the macro invocation is reached during monomorphization.
-   `env!(var_name: str) -> &'static str` — evaluates an environment variable at compile time; compile error if the variable is not defined.
-   `file!() -> &'static str` — the source file path at the invocation site.
-   `line!() -> u32` — the source line number at the invocation site.
-   `column!() -> u32` — the source column number at the invocation site.
-   `module_path!() -> &'static str` — the fully-qualified module path at the invocation site.

### 25.1.4  Panic macro

-   `panic!(format: FormatString, args: ...)` — raises a panic per the strategy defined in Chapter 11 (error handling). The macro is defined in `std::macros` but is available everywhere through the prelude.

---

## 25.2  Injection Ordering and Precedence

The prelude is injected into the crate root as if an implicit import block appeared immediately after the crate-root `module` declaration and before the first user `import` statement. The following priority rules apply when a name is resolvable through multiple routes. Lower numeric rank wins lookup ties.

| Priority rank | Resolution source |
|:---:|:---|
| 1 (highest) | Local declarations in the current scope. |
| 2 | Explicit named imports (`import a.{B}`). |
| 3 | Namespace imports (`import a as n`). |
| 4 (lowest) | **Prelude.** |

An important consequence: if the user writes `import my_crate_utils.{Vec}` the user's `Vec` wins with no warning and no ambiguity error. If the user wrote the same name accidentally, confusing type errors may follow, but the rule is deliberately simple — the user always wins over the prelude. This avoids a cascade of warnings when the prelude grows symbols across editions. A `prelude_shadow` lint (non-fatal, in the 0100–0199 band) MAY be emitted at the user's option to warn about prelude shadowing.

---

## 25.3  `no_std` Mode

When the package manifest declares `[package].no_std = true` or the `--no-std` compiler flag is set, the `std` crate is not auto-available. The prelude is replaced by the `core` prelude. The symbol set of the `core` prelude is:

**Minimal nominal types**
-   `Option<T>`
-   `Result<T, E>`

No other nominal types are included. Heap-allocating types such as `Vec`, `String`, `Box`, `Rc`, `Arc`, interior-mutability types, and I/O functions are absent. Users who need them must explicitly `import alloc.{...}` from the `alloc` crate when an allocator is available.

**All core markers** — the full set from §25.1.2 (Sendable, Shared, Linear, Copy, Drop, Sized).

**All macros** — `todo!`, `unreachable!`, `compile_error!`, `env!`, `file!`, `line!`, `column!`, `module_path!`, `panic!`.

**Core functions** — `dbg`, `drop`, `own`, `borrow`, and the `compile_error!` family.

**Explicitly absent** — `print`, `println`, `eprint`, `eprintln`, `Vec`, `String`, `Box`, `Rc`, `Arc`, `Cell`, `RefCell`, `Mutex`, `RwLock`, and every other heap or I/O type.

A crate compiled under `no_std` MUST provide its own `#[panic_handler]` function (return type `never`) and, if unwinding is supported, its own `#[eh_personality]` function. The compiler MUST emit a diagnostic in the 0900–0999 band when these are missing.

When additionally `no_core = true` is set (freestanding mode) the compiler provides no symbols at all except primitive types and their automatically-derived marker bits. This mode is intended for kernels, embedded runtime startup code, and similar freestanding environments.

---

## 25.4  Edition-Gated Symbols

The prelude symbol set is edition-gated. Symbols may be added to or removed from the prelude only at edition boundaries.

**Addition.** A symbol added to the prelude in a new edition is simply not present for crates compiled against older editions. For example: if `HashMap` is introduced into the prelude of edition 2028, a crate with `edition = "2026"` does not observe `HashMap` in its prelude. The user of the older crate may still `import std.collections.{HashMap}` explicitly.

**Removal.** A symbol may be removed from the prelude only in a new edition, and only after the symbol has produced a deprecation lint for at least one full edition cycle prior to removal. The deprecation lint MUST be emitted for every reference to a symbol reached through the prelude (not for explicit imports) during the transition editions.

**Edition-specific prelude modules.** The compiler maintains per-edition prelude modules under `std::prelude::v${EDITION_YEAR}`, re-exported from `std::prelude::v1` for backward compatibility. Edition gates are resolved by the compiler based on the crate's declared edition.

---

## 25.5  Macro Expansion Ordering

The macros in the prelude (`todo!`, `unreachable!`, `compile_error!`, `env!`, `file!`, `line!`, `column!`, `module_path!`, `panic!`, `print*!`, `eprint*!`, `dbg!`) are expanded during the macro expansion pass that runs after parsing and before binding. Their expansion ordering with respect to user-defined macros is normative:

1.  Prelude macros have lower priority than user-declared macros in the current scope. If a crate declares a local macro `todo!` with the same name, the user's macro wins — exactly the same priority rule as for non-macro symbols (§25.2).
2.  The built-in macros `file!`, `line!`, `column!`, and `module_path!` are expanded at the *use* site, not at the definition site. A macro-expanded use of `file!` inside a user-written macro body therefore reports the location of the user macro's call site, not the user macro's definition site.
3.  `compile_error!` is the only prelude symbol whose expansion terminates compilation with a non-recoverable error that bypasses all later compiler passes. It is processed immediately when its enclosing item is monomorphized. If a generic function body contains `compile_error!` but is never instantiated, no error is emitted.

---

## 25.6  The `Borrowed<T>` Token

The `borrow<T>` function (§25.1.3) returns a value of type `Borrowed<T>`. This type is part of the prelude by transitive closure through the function signature; it does not require a separate listing. The `Borrowed<T>` type is a zero-sized compile-time token. It carries no runtime representation and is erased by codegen. Its purpose is purely semantic: to mark a programmer-intent borrow in ZOM's &T-free ownership model.

Users may explicitly import `Borrowed` from `std::ownership::Borrowed` when needed in type signatures, because the prelude injects it only transitively (as the return type of `borrow`). Implementations SHOULD emit a diagnostic hint when `Borrowed<T>` is named in source without a visible binding, suggesting `import std.ownership.{Borrowed}`.

---

## 25.7  Debug Implementation Contract for `dbg`

The `dbg` macro has a normative behaviour contract beyond its signature:

-   `dbg(expr)` evaluates `expr` exactly once. It does not double-evaluate and does not short-circuit.
-   `dbg(expr)` preserves the value category of `expr` — linear values remain linear, owned values remain owned, and the macro may be used in expression position anywhere the original expression was legal.
-   Side effects of formatting the debug representation MUST NOT affect program semantics. In `--release` mode (or equivalent optimization flag), implementations MAY elide the debug printing entirely, while still preserving the single evaluation and value-category semantics of the wrapped expression.
-   The format of the printed output is implementation-defined. The normative minimum is three whitespace-separated fields: source location (file:line:col), the source text of the expression, and a `Debug`-trait formatted rendering of the value.

---

## 25.8  Linear Prelude Functions

The three functions `drop`, `own`, and `borrow` work together to form the explicit ownership vocabulary for Linear types in ZOM. Their interaction is normative:

-   `drop(x)` is the only way to consume a `Linear` value without invoking a specific method on it. Calling `drop(x)` moves `x`; after the call, `x` is no longer in scope and cannot be referenced. The type checker MUST reject code that uses a linear variable after `drop`.
-   `own(x)` is the identity function on Linear values. Its only purpose is to explicitly mark an ownership transfer at call-site boundaries. The type system treats `own(x)` as a use and move of `x`.
-   `borrow(x)` is a compile-time assertion that the caller is not moving `x`. The returned token `Borrowed<T>` can be used as a witness in function signatures to mark call-by-borrow calling convention in contexts where the programmer wants to document intent without relying on implicit &T syntax.

Implementations MUST NOT rely on implicit drops for Linear types at scope exit. Every linear value must have a syntactically visible consumer (either a method call, a `drop(...)` call, a move into another linear value, or an `own(...)` transfer) before its enclosing scope ends.

---

## 25.9  Conformance Checklist

An implementation claiming conformance to this chapter MUST:

1.  Inject the exact symbol set of §25.1 into every crate root scope.
2.  Apply the priority ordering of §25.2 with prelude symbols at the lowest rank.
3.  Under `no_std` mode, inject the reduced symbol set of §25.3 and absent the listed symbols.
4.  Under both `no_std` and `no_core` modes, emit the appropriate diagnostics for missing `#[panic_handler]` and `#[eh_personality]`.
5.  Honor edition gates for addition and removal of prelude symbols per §25.4.
6.  Implement `dbg!`, `drop`, `own`, `borrow`, and the four source-position macros with the normative contracts of §§25.5–25.8.
7.  Never add symbols to the prelude outside the edition mechanism or user-defined prelude overrides.
