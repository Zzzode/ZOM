# Chapter 25 — Standard Prelude and Library

> **Normative**
> This chapter defines the standard prelude — the collection of symbols the compiler auto-injects into every crate's root scope — and the module structure of the `std` standard library crate. The prelude is deliberately small (fewer than 30 direct items and fewer than 100 transitive items). Implementations MUST NOT add implementation-specific symbols to the prelude without an edition gate.

---

## 25.0  Purpose

Every ZOM crate implicitly imports the standard prelude. Conceptually the compiler performs the equivalent of an `import std.prelude.v1.{ ... }` at the crate root scope, with the imported symbols assigned the *lowest* possible lookup priority. User-declared names and user-written imports always win over prelude names.

The prelude is *not* a language feature in the sense that the symbols it contains could not be authored in ordinary ZOM source code. Every symbol in the prelude is an ordinary item declared in the `std` crate under the `std::prelude::v1` module. The prelude mechanism is purely a convenience injection.

The full `std` crate provides a far richer surface (see §25.10). The prelude is a carefully curated subset designed so that ordinary code can be written without explicit imports for the most commonly used types and functions.

---

## 25.1  Prelude Symbol List

The items below are re-exported from `std::prelude::v1` and injected into every crate root. The list is normative. Implementations MUST inject exactly these symbols and no others in the default prelude, except under the edition-gated mechanism of §25.4.

### 25.1.1  Core nominal types

-   `Maybe<T>` — optional value. Either contains a `T` or is empty. Construct with a `T` value or with the `none` sentinel. Pattern-match via `if let` or the `ZC_IF_SOME` macro family. Replaces the null-pointer idiom with a type-safe alternative.
-   `OneOf<T...>` — tagged union (sum type) over a closed set of types. Use `is<T>()`, `get<T>()`, `tryGet<T>()`, and the `ZC_SWITCH_ONEOF` / `ZC_CASE_ONEOF` macros for exhaustive variant dispatch.
-   `Own<T>` — unique owned heap pointer. Linear (single ownership). The owned object is destroyed when the `Own<T>` goes out of scope. Use `heap<T>(...)` to construct, `attach()` to bundle lifetime dependencies.
-   `Ptr<T>` — borrowed (non-owning) smart pointer. Created from `Pin<T>` or compatible references. Tracks active borrow counts when `ZC_ASSERT_PTR_COUNTERS` is enabled; zero-overhead otherwise.
-   `Array<T>` — owned heap-allocated fixed-capacity array. Use `heapArray<T>(size)` or `ArrayBuilder<T>` to construct.
-   `ArrayPtr<T>` — borrowed non-owning view into contiguous `T` elements. Supports slicing, iteration, and bounds-checked indexing. The const-qualified alias `ArrayPtr<const char>` serves as the string slice type.
-   `Vector<T>` — heap-allocated growable array. Amortized O(1) push at the back. Use `add()` / `removeLast()` for mutation.
-   `String` — owned heap-allocated NUL-terminated UTF-8 string. Use `heapString(...)` to allocate. Movable, not copyable.
-   `StringPtr` — borrowed NUL-terminated UTF-8 string view. Construct from string literals, `String`, or `ConstString`. Zero allocation.
-   `ConstString` — owned immutable NUL-terminated UTF-8 string. May reference a string literal without allocation (via `NullArrayDisposer`) or own a heap buffer via move from `String`.
-   `Rc<T>` — single-threaded reference-counted shared pointer. `!Sendable`. Use `rc<T>(...)` to construct. Subclass `Refcounted` to enable intrusive refcounting.
-   `Arc<T>` — atomic reference-counted shared pointer. Sendable and Shared. Subclass `AtomicRefcounted` to enable intrusive atomic refcounting.
-   `MutexGuarded<T>` — multi-threaded locking interior-mutable cell. Wraps a value of type `T` protected by a `Mutex` with exclusive (write) and shared (read) lock modes. Use `lock()` / `lockShared()` or the `ZC_LOCK` macro.

### 25.1.2  Core marker traits

These markers are automatically attached by the compiler to primitive and user-declared types as appropriate. They appear in error messages and are usable in trait bounds without explicit import.

-   `Sendable` — values of the type may be transferred across thread or worker boundaries.
-   `Shared` — values of the type may be shared across threads through `&T`, `Arc<T>`, and similar shared indirection.
-   `Linear` — single ownership; values must be consumed explicitly; they cannot be dropped silently. `Own<T>` destruction is the explicit consumption.
-   `Copy` — bitwise-copy semantics; values are implicitly duplicated on assignment.
-   `Drop` — the type carries an explicit destructor.
-   `Sized` — compile-time-known size. The default for all type parameters.

### 25.1.3  Functions (top-level convenience)

-   `heap<T>(params...) -> Own<T>` — allocates a `T` on the heap, returning an `Own<T>`.
-   `heapString(value) -> String` — allocates a copy of the given value on the heap as a `String`.
-   `str(params...) -> String` — concatenates an arbitrary sequence of stringifiable values into one heap `String`.
-   `delimited(container, delim) -> Delimited<T>` — wraps an iterable container with a delimiter for stringification. Use with `str()`.
-   `addRef<T: Refcounted>(obj) -> Own<T>` — increments the reference count of a refcounted object, returning a new `Own<T>`.
-   `downcast<T>(obj) -> Own<T>` — downcasts an owned polymorphic pointer, destroying the original. Throws on type mismatch in debug mode.
-   `attachVal<T>(value, attachments...) -> Own<T>` — returns an `Own<T>` that owns both `value` and `attachments`, pointing to `value`.
-   `attachRef<T>(value, attachments...) -> Own<T>` — like `attach()` but `value` is not moved; the resulting `Own<T>` points to its existing location.
-   `disposeWith<freeFunc, T>(ptr) -> Own<T>` — associates a pre-allocated raw pointer with a custom disposal function.
-   `runCatchingExceptions(func) -> Maybe<Exception>` — executes `func`, catching and returning any thrown `Exception`, or `none` on success.
-   `print(format: FormatString, args: ...)` — writes formatted output to the standard output stream.
-   `println(format: FormatString, args: ...)` — writes formatted output to the standard output stream followed by a newline.
-   `eprint(format: FormatString, args: ...)` — writes formatted output to the standard error stream.
-   `eprintln(format: FormatString, args: ...)` — writes formatted output to the standard error stream followed by a newline.

### 25.1.4  Macros

-   `ZC_ASSERT(condition, args...)` — throws a fatal `Exception` if `condition` is false. Use to detect bugs in the surrounding code. May be followed by a recovery block.
-   `ZC_REQUIRE(condition, args...)` — like `ZC_ASSERT` but semantically checks preconditions (caller is buggy).
-   `ZC_LOG(severity, args...)` — writes a log message at the given severity (`INFO`, `WARNING`, `ERROR`, `FATAL`, `DBG`). Intercepted by `ExceptionCallback`.
-   `ZC_DBG(args...)` — shorthand for `ZC_LOG(DBG, ...)`. Intended for temporary debug output.
-   `ZC_UNIMPLEMENTED(args...)` — throws an `Exception` of type `UNIMPLEMENTED`.
-   `ZC_SYSCALL(code, args...)` — executes a system call; negative return is treated as an error with `errno`. EINTR is retried automatically.
-   `ZC_CONTEXT(args...)` — attaches contextual information to any exception thrown from the enclosing scope.
-   `ZC_IF_SOME(var, maybe) { ... } else { ... }` — pattern-matches a `Maybe<T>`. If the Maybe contains a value, binds it to `var` and executes the then-branch.
-   `ZC_MAP(e, container) { ... }` — maps a container into an `Array`, applying the block to each element.
-   `ZC_STRINGIFY(expr)` — defines a stringifier for a custom type so it can be passed to `str()` and `ZC_LOG`.
-   `ZC_SWITCH_ONEOF(value)` — switch over a `OneOf` variant.
-   `ZC_CASE_ONEOF(name, Type)` — case label within `ZC_SWITCH_ONEOF`.
-   `ZC_MAIN(MyMainClass)` — declares `main()` using the given class, which must accept `ProcessContext&` and provide `getMain() -> MainFunc`.
-   `ZC_DEFER(code)` — executes `code` when the enclosing scope exits (scope guard).
-   `ZC_ON_SCOPE_SUCCESS(code)` — executes `code` only if the scope exits normally (not via exception).
-   `ZC_ON_SCOPE_FAILURE(code)` — executes `code` only if the scope exits due to an exception.
-   `todo!()` — panics at runtime with the message "not yet implemented".
-   `unreachable!()` — panics with the message "entered unreachable code"; treated as unreachable in codegen.
-   `compile_error!(message: str)` — compile-time error if the macro invocation is reached during monomorphization.
-   `env!(var_name: str) -> &'static str` — evaluates an environment variable at compile time; compile error if the variable is not defined.
-   `file!() -> &'static str` — the source file path at the invocation site.
-   `line!() -> u32` — the source line number at the invocation site.
-   `column!() -> u32` — the source column number at the invocation site.
-   `module_path!() -> &'static str` — the fully-qualified module path at the invocation site.

### 25.1.5  Panic macro

-   `panic!(format: FormatString, args: ...)` — raises a panic per the strategy defined in Chapter 11 (error handling). The macro is defined in `std::macros` but is available everywhere through the prelude. Equivalent to `throwFatalException(...)` in the zc runtime.

---

## 25.2  Injection Ordering and Precedence

The prelude is injected into the crate root as if an implicit import block appeared immediately after the crate-root `module` declaration and before the first user `import` statement. The following priority rules apply when a name is resolvable through multiple routes. Lower numeric rank wins lookup ties.

| Priority rank | Resolution source |
|:---:|:---|
| 1 (highest) | Local declarations in the current scope. |
| 2 | Explicit named imports (`import a.{B}`). |
| 3 | Namespace imports (`import a as n`). |
| 4 (lowest) | **Prelude.** |

An important consequence: if the user writes `import my_crate_utils.{Vector}` the user's `Vector` wins with no warning and no ambiguity error. If the user wrote the same name accidentally, confusing type errors may follow, but the rule is deliberately simple — the user always wins over the prelude. This avoids a cascade of warnings when the prelude grows symbols across editions. A `prelude_shadow` lint (non-fatal, in the 0100–0199 band) MAY be emitted at the user's option to warn about prelude shadowing.

---

## 25.3  `no_std` Mode

When the package manifest declares `[package].no_std = true` or the `--no-std` compiler flag is set, the `std` crate is not auto-available. The prelude is replaced by the `core` prelude. The symbol set of the `core` prelude is:

**Minimal nominal types**
-   `Maybe<T>`
-   `OneOf<T...>`

No other nominal types are included. Heap-allocating types such as `Vector`, `String`, `Own`, `Rc`, `Arc`, interior-mutability types, and I/O functions are absent. Users who need them must explicitly `import alloc.{...}` from the `alloc` crate when an allocator is available.

**All core markers** — the full set from §25.1.2 (Sendable, Shared, Linear, Copy, Drop, Sized).

**All macros** — `todo!`, `unreachable!`, `compile_error!`, `env!`, `file!`, `line!`, `column!`, `module_path!`, `panic!`.

**Core functions** — `ZC_ASSERT`, `ZC_REQUIRE`, `ZC_LOG`, `ZC_DBG`, `ZC_UNIMPLEMENTED`, `ZC_CONTEXT`, `ZC_IF_SOME`, `ZC_STRINGIFY`, `ZC_DEFER`, `ZC_ON_SCOPE_SUCCESS`, `ZC_ON_SCOPE_FAILURE`, and the `compile_error!` family.

**Explicitly absent** — `print`, `println`, `eprint`, `eprintln`, `Vector`, `String`, `StringPtr`, `ConstString`, `Own`, `Ptr`, `Array`, `ArrayPtr`, `Rc`, `Arc`, `MutexGuarded`, `Function`, `Tuple`, `SourceLocation`, `heap`, `heapString`, `str`, `delimited`, `addRef`, `downcast`, `attachVal`, `attachRef`, `disposeWith`, `runCatchingExceptions`, `ZC_MAIN`, `ZC_SYSCALL`, `ZC_MAP`, `ZC_SWITCH_ONEOF`, `ZC_CASE_ONEOF`, and every other heap or I/O type.

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

The macros in the prelude (`todo!`, `unreachable!`, `compile_error!`, `env!`, `file!`, `line!`, `column!`, `module_path!`, `panic!`, `print*!`, `eprint*!`, `ZC_ASSERT`, `ZC_REQUIRE`, `ZC_LOG`, `ZC_DBG`, `ZC_UNIMPLEMENTED`, `ZC_SYSCALL`, `ZC_CONTEXT`, `ZC_IF_SOME`, `ZC_STRINGIFY`, `ZC_MAIN`, `ZC_DEFER`, `ZC_ON_SCOPE_SUCCESS`, `ZC_ON_SCOPE_FAILURE`, `ZC_MAP`, `ZC_SWITCH_ONEOF`, `ZC_CASE_ONEOF`) are expanded during the macro expansion pass that runs after parsing and before binding. Their expansion ordering with respect to user-defined macros is normative:

1.  Prelude macros have lower priority than user-declared macros in the current scope. If a crate declares a local macro `todo!` with the same name, the user's macro wins — exactly the same priority rule as for non-macro symbols (§25.2).
2.  The built-in macros `file!`, `line!`, `column!`, and `module_path!` are expanded at the *use* site, not at the definition site. A macro-expanded use of `file!` inside a user-written macro body therefore reports the location of the user macro's call site, not the user macro's definition site.
3.  `compile_error!` is the only prelude symbol whose expansion terminates compilation with a non-recoverable error that bypasses all later compiler passes. It is processed immediately when its enclosing item is monomorphized. If a generic function body contains `compile_error!` but is never instantiated, no error is emitted.
4.  `ZC_ASSERT`, `ZC_REQUIRE`, and related assertion macros expand to code that constructs a `zc::Exception` and invokes `throwFatalException()`. The message text passed to these macros is stringified at the expansion site using `str()` and the `ZC_STRINGIFY` machinery.

---

## 25.6  The `Borrowed<T>` Token

The `borrow<T>` function (§25.1.3) returns a value of type `Borrowed<T>`. This type is part of the prelude by transitive closure through the function signature; it does not require a separate listing. The `Borrowed<T>` type is a zero-sized compile-time token. It carries no runtime representation and is erased by codegen. Its purpose is purely semantic: to mark a programmer-intent borrow as a compile-time assertion.

Note: ZOM also has first-class reference types `&T` and `&mut T` (see Ch.03 §Reference Types) as well as the `Ptr<T>` / `Pin<T>` ownership-tracking types from the zc memory model. `Borrowed<T>` is orthogonal to `&T` and `Ptr<T>`: the latter are runtime pointer types providing actual aliased access, while `Borrowed<T>` is a compile-time-only token that documents borrowing intent in the type system. They may be used together or independently.

Users may explicitly import `Borrowed` from `std::ownership::Borrowed` when needed in type signatures, because the prelude injects it only transitively (as the return type of `borrow`). Implementations SHOULD emit a diagnostic hint when `Borrowed<T>` is named in source without a visible binding, suggesting `import std.ownership.{Borrowed}`.

---

## 25.7  Debug Implementation Contract for `ZC_DBG` and `ZC_LOG`

The `ZC_DBG` and `ZC_LOG` macros have a normative behaviour contract beyond their signatures:

-   `ZC_LOG(severity, args...)` evaluates each argument exactly once. It does not double-evaluate and does not short-circuit.
-   `ZC_LOG` preserves the value category of each argument — linear values remain linear, owned values remain owned.
-   Side effects of formatting the debug representation MUST NOT affect program semantics. In `--release` mode (or equivalent optimization flag), implementations MAY elide the log printing entirely for `DBG` severity, while still preserving the single evaluation and value-category semantics of the wrapped expressions.
-   The format of the printed output is implementation-defined. The normative minimum is: source location (file:line), severity, and a space-separated rendering of each argument via its `ZC_STRINGIFY` stringifier.
-   Log messages are routed through the `ExceptionCallback` system (§25.9). Users may implement `ExceptionCallback::logMessage()` to intercept and redirect log output.

---

## 25.8  Linear Prelude Functions and Ownership

The `Own<T>` type and related functions form the explicit ownership vocabulary for Linear types in ZOM. Their interaction is normative:

-   `Own<T>` destruction is the only way to consume a `Linear` value without invoking a specific method on it. When an `Own<T>` goes out of scope, its `Disposer` is called, destroying the owned object. After the owning scope ends, the value cannot be referenced. The type checker MUST reject code that uses a linear value after its `Own<T>` has been destroyed.
-   `heap<T>(params...)` is the primary way to construct an `Own<T>`. It allocates the object on the heap and returns ownership.
-   `attachVal(value, attachments...)` and `attachRef(value, attachments...)` bundle lifetime dependencies: the returned `Own<T>` keeps `attachments` alive until the owned object is destroyed.
-   `downcast<U>(owned)` performs a checked downcast of an `Own<T>` to `Own<U>`, transferring ownership. Throws on type mismatch in debug mode.
-   `Ptr<T>` (obtained from `Pin<T>::asPtr()` or compatible conversions) provides a non-owning view. The `Pin<T>` type enables safe stack allocation with borrow tracking.

Implementations MUST NOT rely on implicit drops for Linear types at scope exit. Every linear value must have a syntactically visible consumer (either a method call, an `Own<T>` destruction, a move into another linear value, or an explicit `disposeWith` / `attachVal` transfer) before its enclosing scope ends.

---

## 25.9  Exception System and Error Handling

The standard library's error handling is built on the `Exception` type and the `ExceptionCallback` system:

-   `Exception` carries a type tag (`FAILED`, `OVERLOADED`, `DISCONNECTED`, `UNIMPLEMENTED`), source location (file:line), a human-readable description, a captured stack trace, and an optional context chain.
-   `throwFatalException(exc)` — raises a fatal exception via the current `ExceptionCallback`. If the callback returns, `abort()` is called.
-   `throwRecoverableException(exc)` — raises a recoverable exception. If the callback returns, execution continues normally (permitting garbage output).
-   `ExceptionCallback` — stack-allocated listener that intercepts exceptions and log messages. The most recently registered callback on the current thread is called first. Default implementations chain to the next callback.
-   `CanceledException` — special exception type for force-unwinding a stack (e.g., fiber cancellation). `runCatchingExceptions()` does NOT catch this.
-   `UnwindDetector` — utility for detecting whether a destructor is being called due to stack unwinding. Use `ZC_ON_SCOPE_SUCCESS` and `ZC_ON_SCOPE_FAILURE` for declarative scope-guard patterns.
-   `getStackTrace(space, ignoreCount)` — captures the current call stack into the provided buffer.
-   `stringifyStackTrace(trace)` — converts a raw stack trace to a human-readable string with file and line information. May invoke subprocesses (e.g., `addr2line`).
-   `printStackTraceOnCrash()` — registers signal handlers for common crash signals to attempt printing a stack trace. Called automatically by `ZC_MAIN`.

---

## 25.10  Standard Library Module Structure

The full `std` crate is organized into the following top-level modules. Items marked with a dagger (†) are re-exported into the prelude. All other items require explicit `import std.module.{Item}`.

### 25.10.1  `std::prelude`

Edition-gated re-export modules. `v1` is the current edition's prelude. Future editions add `v${YEAR}` modules.

### 25.10.2  `std::memory` — Ownership and allocation

| Symbol | Description |
|:---|:---|
| `Own<T>` † | Unique owned heap pointer. |
| `Ptr<T>` † | Borrowed non-owning smart pointer. |
| `Pin<T>` † | Stack-allocated with pinning and borrow tracking. |
| `Disposer` | Abstract interface for object disposal. |
| `NullDisposer` | Disposer that does nothing. Useful for stack-referencing Own. |
| `DestructorOnlyDisposer<T>` | Disposer that only calls the destructor. |
| `SpaceFor<T>` | Manual allocation helper: size/alignment-compatible storage without construction. |
| `heap<T>()` † | Allocate on heap, return `Own<T>`. |
| `attachVal()` † | Bundle value + attachments into one `Own`. |
| `attachRef()` † | Reference + attachments into one `Own`. |
| `disposeWith()` † | Raw pointer + custom disposer → `Own`. |
| `downcast<T>()` † | Checked downcast of `Own`. |

### 25.10.3  `std::refcount` — Reference-counted sharing

| Symbol | Description |
|:---|:---|
| `Refcounted` | Base class for intrusive non-atomic refcounting. |
| `AtomicRefcounted` | Base class for intrusive atomic refcounting. |
| `Rc<T>` † | Non-atomic refcounted shared pointer. `!Sendable`. |
| `Arc<T>` † | Atomic refcounted shared pointer. Sendable + Shared. |
| `addRef()` † | Increment refcount, return new `Own`. |
| `rc<T>()` | Construct a new `Rc<T>` in-place. |
| `arc<T>()` | Construct a new `Arc<T>` in-place. |

### 25.10.4  `std::option` — Optional values

| Symbol | Description |
|:---|:---|
| `Maybe<T>` † | Optional value (present or absent). |
| `none` † | The empty-Maybe sentinel. |
| `ZC_IF_SOME` † | Pattern-match a Maybe. |

### 25.10.5  `std::variant` — Sum types

| Symbol | Description |
|:---|:---|
| `OneOf<T...>` † | Tagged union over closed type set. |
| `ZC_SWITCH_ONEOF` † | Switch over OneOf variants. |
| `ZC_CASE_ONEOF` † | Case label for OneOf switch. |

### 25.10.6  `std::array` — Arrays and vectors

| Symbol | Description |
|:---|:---|
| `Array<T>` † | Owned fixed-capacity heap array. |
| `ArrayPtr<T>` † | Borrowed array view (slice). |
| `ArrayBuilder<T>` | Builder for incrementally constructing an `Array<T>`. |
| `CappedArray<T, N>` | Fixed-size array with runtime-used-length tracking. |
| `FixedArray<T, N>` | Compile-time fixed-size array. |
| `Vector<T>` † | Growable heap array. |
| `heapArray<T>(size)` | Allocate an `Array<T>` of given size. |
| `heapArrayBuilder<T>(capacity)` | Create an `ArrayBuilder<T>`. |

### 25.10.7  `std::string` — String types

| Symbol | Description |
|:---|:---|
| `String` † | Owned heap-allocated NUL-terminated UTF-8. |
| `StringPtr` † | Borrowed NUL-terminated UTF-8 view. |
| `ConstString` † | Owned immutable string (may alias literals). |
| `StringTree` | Efficient concatenation tree for string building. |
| `LiteralStringConst` | Compile-time string literal type. |
| `heapString()` † | Allocate a String. |
| `str()` † | Concatenate stringifiable values. |
| `delimited()` † | Wrap container with delimiter for stringification. |
| `ZC_STRINGIFY` † | Define a stringifier for a type. |

### 25.10.8  `std::collections` — Indexed collections

| Symbol | Description |
|:---|:---|
| `Table<Row, Indexes...>` | Indexed row store. Alternative to map/set. Supports hash and tree indexes, multiple indexes (bimap), deterministic iteration. |
| `HashMap<Key, Value>` | Hash-based key/value map. |
| `List<T, link>` | Intrusive doubly-linked list (no allocation for add/remove). |
| `Tuple<T...>` | Compile-time heterogeneous tuple. |

### 25.10.9  `std::exception` — Error handling

| Symbol | Description |
|:---|:---|
| `Exception` † | Exception type with type tag, location, description, stack trace, context chain. |
| `Exception::Type` | `FAILED`, `OVERLOADED`, `DISCONNECTED`, `UNIMPLEMENTED`. |
| `CanceledException` | Stack-cancellation exception. Not caught by `runCatchingExceptions`. |
| `ExceptionCallback` † | Stack-allocated listener for exceptions and log messages. |
| `UnwindDetector` | Detects destructor-during-unwind. |
| `throwFatalException()` | Throw via callback, abort if callback returns. |
| `throwRecoverableException()` | Throw via callback, continue if callback returns. |
| `runCatchingExceptions()` † | Execute function, catch and return exception. |
| `getCaughtExceptionAsKj()` | Get current exception as `zc::Exception` from a catch block. |
| `getStackTrace()` | Capture current stack trace. |
| `stringifyStackTrace()` | Convert stack trace to string. |
| `printStackTraceOnCrash()` | Register signal handlers for crash stack traces. |

### 25.10.10  `std::debug` — Assertions and logging

| Symbol | Description |
|:---|:---|
| `ZC_ASSERT` † | Assert condition, throw on failure. |
| `ZC_REQUIRE` † | Check precondition, throw on failure. |
| `ZC_LOG` † | Log message at severity. |
| `ZC_DBG` † | Debug logging shorthand. |
| `ZC_UNIMPLEMENTED` † | Throw UNIMPLEMENTED exception. |
| `ZC_SYSCALL` † | System call with error handling. |
| `ZC_CONTEXT` † | Attach context to exceptions from scope. |
| `ZC_DEFER` † | Scope guard: execute code at scope exit. |
| `ZC_ON_SCOPE_SUCCESS` † | Execute code only on normal scope exit. |
| `ZC_ON_SCOPE_FAILURE` † | Execute code only on exceptional scope exit. |
| `LogSeverity` | `INFO`, `WARNING`, `ERROR`, `FATAL`, `DBG`. |

### 25.10.11  `std::io` — Input/Output streams

| Symbol | Description |
|:---|:---|
| `InputStream` | Abstract byte input stream. |
| `OutputStream` | Abstract byte output stream. |
| `BufferedInputStream` | Buffered wrapper for `InputStream`. |
| `BufferedOutputStream` | Buffered wrapper for `OutputStream`. |
| `ArrayInputStream` | `InputStream` reading from an `ArrayPtr<const byte>`. |
| `ArrayOutputStream` | `OutputStream` writing into a growable `Array<byte>`. |
| `StringInputStream` | `InputStream` reading from a `StringPtr`. |
| `print()` † | Formatted stdout. |
| `println()` † | Formatted stdout + newline. |
| `eprint()` † | Formatted stderr. |
| `eprintln()` † | Formatted stderr + newline. |

### 25.10.12  `std::fs` — Filesystem

| Symbol | Description |
|:---|:---|
| `Path` | Filesystem path type with platform-aware handling. |
| `File` | Open file handle. Implements `InputStream` and `OutputStream`. |
| `Directory` | Directory listing and traversal. |
| `exists(path)` | Check if a path exists. |
| `remove(path)` | Delete a file. |
| `rename(from, to)` | Rename a file. |
| `createDirectory(path)` | Create a directory. |
| `listDirectory(path)` | Iterate directory entries. |

### 25.10.13  `std::sync` — Concurrency primitives

| Symbol | Description |
|:---|:---|
| `Mutex` | Low-level mutual exclusion primitive. Supports exclusive and shared locking. |
| `MutexGuarded<T>` † | Value protected by a `Mutex`. |
| `ZC_LOCK` | Lock a `MutexGuarded` with RAII guard. |
| `ConditionVariable` | Thread blocking and notification. |
| `Thread` | OS thread abstraction. |
| `ThreadPool` | Work-stealing thread pool. |

### 25.10.14  `std::time` — Time and timers

| Symbol | Description |
|:---|:---|
| `TimePoint` | Absolute point in time (monotonic clock). |
| `Duration` | Time interval. Supports arithmetic. |
| `Timer` | One-shot or recurring timer. |
| `TimerWheel` | Hashed timing wheel for efficient timeout management. |
| `now()` | Current monotonic time. |
| `sleep(duration)` | Block current thread for duration. |

### 25.10.15  `std::hash` — Hashing

| Symbol | Description |
|:---|:---|
| `hashCode(value)` | Compute hash code for a value. |
| `ZC_HASHCODE` | Define `hashCode()` for a custom type. |
| `Hasher` | Incremental hash computation interface. |

### 25.10.16  `std::encoding` — Text encoding

| Symbol | Description |
|:---|:---|
| `Utf8` | UTF-8 validation and transcoding utilities. |
| `Utf16` | UTF-16 encoding support. |
| `Ascii` | ASCII validation and classification. |

### 25.10.17  `std::net` — Networking

| Symbol | Description |
|:---|:---|
| `IpAddress` | IPv4 or IPv6 address. |
| `Cidr` | IP address range (CIDR notation). |
| `SocketAddress` | IP + port pair. |
| `TcpListener` | TCP listening socket. |
| `TcpConnection` | TCP connected socket (implements streams). |
| `UdpSocket` | UDP socket. |

### 25.10.18  `std::process` — Process and entry point

| Symbol | Description |
|:---|:---|
| `ProcessContext` | Abstract interface for program context (name, exit, logging). |
| `TopLevelProcessContext` | Concrete implementation for real process entry. |
| `MainFunc` | Type alias for the main function signature. |
| `MainBuilder` | Builder for argument-parsing main functions. |
| `runMainAndExit()` | Execute main function, handle exceptions, exit. |
| `ZC_MAIN` † | Declare main() from a class. |

### 25.10.19  `std::functional` — Callable wrappers

| Symbol | Description |
|:---|:---|
| `Function<Signature>` | Type-erased callable wrapper. Movable, not copyable. |
| `ZC_BIND_METHOD` | Bind a member function to an object reference. |

### 25.10.20  `std::source_location` — Source position

| Symbol | Description |
|:---|:---|
| `SourceLocation` | File name, function name, line, column of a source position. |
| `NoopSourceLocation` | Zero-cost placeholder when source location tracking is disabled. |

### 25.10.21  `std::arena` — Arena allocation

| Symbol | Description |
|:---|:---|
| `Arena` | Bump allocator with bulk deallocation. |
| `Arena::Scoped` | RAII scope for arena reset. |

### 25.10.22  `std::glob` — Pattern matching

| Symbol | Description |
|:---|:---|
| `GlobFilter` | Shell-style glob pattern matching (supports `*`, `?`, `[...]`). |

### 25.10.23  `std::units` — Type-safe units

| Symbol | Description |
|:---|:---|
| Unit types | `Bytes`, `Kilobytes`, `Megabytes`, `Seconds`, `Milliseconds`, `Microseconds`, `Nanoseconds`. Compile-time dimensional analysis. |

### 25.10.24  `std::ownership` — Compile-time ownership tokens

| Symbol | Description |
|:---|:---|
| `Borrowed<T>` | Zero-sized compile-time borrow token. |
| `borrow<T>(value)` † | Create a `Borrowed<T>` token. |
| `own<T: Linear>(value)` † | Identity function with semantic weight for Linear types. |

---

## 25.11  Conformance Checklist

An implementation claiming conformance to this chapter MUST:

1.  Inject the exact symbol set of §25.1 into every crate root scope.
2.  Apply the priority ordering of §25.2 with prelude symbols at the lowest rank.
3.  Under `no_std` mode, inject the reduced symbol set of §25.3 and absent the listed symbols.
4.  Under both `no_std` and `no_core` modes, emit the appropriate diagnostics for missing `#[panic_handler]` and `#[eh_personality]`.
5.  Honor edition gates for addition and removal of prelude symbols per §25.4.
6.  Implement `ZC_DBG`, `ZC_LOG`, `ZC_ASSERT`, `ZC_REQUIRE`, `heap`, `heapString`, `str`, `delimited`, `addRef`, `downcast`, `attachVal`, `attachRef`, `disposeWith`, `runCatchingExceptions`, `borrow`, `own`, and the four source-position macros with the normative contracts of §§25.5–25.9.
7.  Provide the full standard library module structure of §25.10 with all listed symbols.
8.  Never add symbols to the prelude outside the edition mechanism or user-defined prelude overrides.
9.  Ensure `Maybe<T>` and `OneOf<T...>` are the primary optional and sum types in the prelude — no separate `Option` or `Result` types are part of the prelude contract. The `Result<T, E>` type, if provided by an implementation, lives in `std::result` and is NOT auto-imported by the prelude.
