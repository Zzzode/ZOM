# Chapter 11 — Error Handling

**Normative** | Version 1.0.0 | Last updated: 2026-06-25

This chapter defines ZOM's error handling model. It is a normative
specification; conforming implementations must reproduce every behavior
described here unless explicitly marked non-normative.

---

## 11.0 Philosophy — Explicit, Typed, Union-Based

ZOM's error handling rests on three non-negotiable principles that together
deliver deterministic cleanup, zero-cost abstraction, and call-site
visibility for every failure path in a program.

**Principle 1 — Typed errors by default.** Every function that can fail
declares which failures it can produce. The compiler verifies that callers
either handle each variant or explicitly propagate it into their own
declared error set. There is no hidden, untyped failure channel.

**Principle 2 — Union-based representation.** An error-returning function
returns a structural tagged union `T | E1 | E2 | ... | En`. Success occupies
tag 0; each distinct error variant occupies a consecutive tag starting at 1.
No vtable indirection or separate nominal `Result` enum is required — the
nominal `Result<T, E>` type alias (Prelude, Chapter 25) is layout-identical
to the bare union `T | E`.

**Principle 3 — Explicit propagation, implicit destructuring.** The `?!`
postfix operator provides syntactic sugar for the repeated "match on union,
return error variant, continue with success value" pattern. It expands to a
`match` expression; destructors run deterministically for every scope exit,
whether by normal return or by `?!`-triggered early return.

### Permanent Rejection (Normative)

ZOM permanently rejects the `try`/`catch`/`throw` family of language
mechanisms. These patterns are fundamentally incompatible with: (a)
deterministic linear resource cleanup — destructors must run in lock-step
with lexical scope exit, not as a side-effect of an unwinder whose behavior
depends on caller context (see TBD-6 and Chapter 15); (b) the explicit
marker system, which requires per-call-site annotation of propagation so
reviewers can trace every failure path; and (c) the zero-cost abstraction
goal, since exception-table generation and personality-routine dispatch
impose measurable binary-size and branch-prediction overhead on the happy
path. This decision is edition-locked: it cannot be reversed in a
point-release, and any future reintroduction requires a new language
edition ratified by unanimous vote of the language board. Proposals that
introduce `try` in expression position, `catch` as a block keyword, or
`throw` as a statement keyword are out of scope for this and all future
editions unless explicitly reopened.

### High-Level Architecture

```mermaid
graph LR
    CODE[User source] --> RAISES[Declared raises: A, B]
    RAISES --> RETURN_TYPE[Function return: T | A | B]
    RETURN_TYPE --> CALLSITE[Call site]
    CALLSITE --> EXPLICIT[Explicit match]
    CALLSITE --> SUGAR[Propagate via ?!]
    CALLSITE --> DISCARD[Discard via !!]
    EXPLICIT --> CONTROL[Downstream control flow]
    SUGAR --> PROP[Early return of error variant]
    DISCARD --> PANIC[Unwrap-or-panic]
```

### Cross-References

- **Chapter 03 Types**: Structural union types `T | E`, the `Option` type
  alias, and the `never` / bottom type (return type of `panic!`).
- **Chapter 12 Generics**: Bounds and `where` clauses for the `Try`
  interface and generic `raises` signatures.
- **Chapter 13 Modules**: The `FunctionTypeSymbol` includes `raises` as a
  structural component — two functions with different `raises` sets are
  distinct types even if name, parameters, and success type coincide.
- **Chapter 15 Concurrency**: `scope.cancel` semantics, cooperative
  cancellation, `CancelError` in `raises` sets, and the destructor
  non-interruption guarantee (TBD-6).
- **Chapter 16 Attributes**: `#[zom::derive(Error)]`, `#[zom::panic(...)]`,
  `#[zom::oom(...)]`, `#[zom::error(trace)]`, and the derive schema system.
- **Chapter 17 Grammar**: Formal EBNF for the `raises` clause, the `?!` and
  `!!` postfix operators, union type syntax, and inner/outer attribute
  forms. This chapter describes semantics only; grammar is owned by Ch.17.

---

## 11.1 Error Union Type (Structural)

A function that can fail with error types `E1..En` and succeeds with `T`
returns the structural union `T | E1 | ... | En`. This union is a
first-class structural type — not a nominal enum.

### Runtime Layout

- Payload: `max(sizeof(T), sizeof(E1), ..., sizeof(En))` bytes, aligned to
  `max(alignof(T), alignof(E1), ..., alignof(En))`.
- Discriminant tag: the smallest unsigned integer with at least `n+1`
  distinct values (a single `u8` for `n < 255`). Tag is stored at offset 0;
  payload begins at the next aligned offset satisfying all member alignments.

Tag assignments are deterministic and stable across compilation units for
the same canonical union type: tag `0` is always the success variant; tags
`1..n` are error variants in canonical order.

### Nominal Aliases (Prelude)

```zom
type Result<T, E> = T | E;        // nominal alias; same layout as bare union
type ParseResult<T> = T | ParseError;   // common convenience
```

Users may write either form — both produce identical canonical types with
no conversion cost.

### Construction and Implicit Coercion

ZOM does not provide `Ok(...)` / `Err(...)` constructors. Instead, the
compiler injects implicit coercions in function-return and assignment
positions:

- A value of type `T` coerces into `T | E1 | ... | En` with tag `0`
  (implicit success path).
- A value of type `Ei` for any `i` in `1..n` coerces with tag `i`
  (implicit error path).

These coercions fire automatically at function return and explicit
assignment. They never fire transitively or inbound through function
parameters.

### Ambiguity Restriction

If any concrete type appears both in the success position and in the error
set of the same union (e.g. `i32 | i32`, or an alias that collapses to the
same type), the compiler issues **ZOM0950 ErrorUnionAmbiguous** — the tag
cannot be determined at compile time. Fix: wrap one side in a newtype, or
refactor the signature to make success and error domains disjoint.

---

## 11.2 The `raises` Clause (Syntactic Sugar)

The `raises` keyword provides human-readable, first-class syntax for
declaring a function's error set. It is pure syntactic sugar over an
explicit union return type; both forms produce the same `FunctionTypeSymbol`
with identical type ID.

### Forms

```zom
fun parse(json: str) -> Ast raises ParseError { ... }
fun connect(host: str, port: u16) -> Socket raises[NetError, TimeoutError] { ... }

// Equivalent explicit union forms — identical types.
fun parse(json: str) -> (Ast | ParseError) { ... }
fun connect(host: str, port: u16) -> (Socket | NetError | TimeoutError) { ... }
```

Desugaring: `... -> T raises E` => `... -> (T | E)`;
`... -> T raises[A, B, C]` => `... -> (T | A | B | C)`;
`... -> T raises[]` => `... -> T`.

### `raises` Is Part of Function Identity

Two function types differing only in their `raises` clause are distinct
types. Widening (assigning a function whose error set is a strict subset of
the target) is allowed. Narrowing is not. **ZOM0951 RaisesSignatureMismatch**
fires when function-pointer assignment or trait-method impl declares an
incompatible error set. Trait impls may widen their error sets; they may
not narrow them.

### Error Subtype Inheritance

If error type `E` is a subtype of `F`, declaring `raises E` does NOT
automatically widen the union to `raises F`. Unions contain exact concrete
types; callers must upcast explicitly at match sites.

### Generic `raises`

```zom
// Execute a user block inside a DB transaction. Caller errors propagate;
// DbError is added for machinery failures.
fun with_db<T, E>(block: fun() -> T raises E) -> T raises[E, DbError] { ... }
```

See Chapter 12 Generics for bounds and `where` clause rules.

---

## 11.3 The `?!` Postfix Operator (Early-Return Sugar)

`?!` is postfix control-flow sugar applied to error unions or `Try`
implementors. It eliminates boilerplate matching at every call site.

### Desugaring

Given `expr : T | E`, the form `expr?!` desugars exactly to:

```zom
match expr {
    Ok(v)  -> v,
    Err(e) -> return Err(e),
}
```

where `Ok(v)` matches tag `0` and `Err(e)` matches any tag in `1..n`. The
`return` targets the nearest enclosing function; `?!` does not cross `async`
block boundaries.

### Error-Set Compatibility

The inner expression's error variants must coerce into the enclosing
function's declared return union. If the enclosing function declares a
strict superset, the compiler automatically widens the tag. Otherwise
**ZOM0952 CannotPropagateError** fires, listing missing variants and
suggesting either adding them to `raises` or an explicit `match`.

### Chaining and Associativity

`?!` is a PostfixOp (Chapter 17) with the same precedence as `.` and `[ ]`,
left-associative:

```zom
get_config()?.read()?.parse()?
```

desugars left-to-right into three nested `match` expressions.

### Non-Union Types

Applying `?!` to a type that is neither an error union nor a `Try`
implementor produces **ZOM0953 NotATryType**.

### `?!` Desugaring Flowchart

```mermaid
flowchart TD
    EXPR[expr?!] --> CHECK[type(expr)]
    CHECK --> |T \| E union| DESUGAR[match expr<br/>tag 0 → bind T<br/>tag N → return Err]
    CHECK --> |impl Try| DISPATCH[Try::branch(expr)<br/>Residual → fromResidual + return<br/>Output → continue]
    CHECK --> |neither| E0904[ZOM0953 NotATryType]
    DESUGAR --> WIDEN{errors in enclosing raises?}
    WIDEN --> |yes| OK[compile]
    WIDEN --> |no| E0903[ZOM0952 CannotPropagateError]
    DISPATCH --> COERCE{Residual coerces?}
    COERCE --> |yes| OK
    COERCE --> |no| E0905[ZOM0954 TryResidualMismatch]
```

---

## 11.4 The Open `Try` Interface

ZOM exposes the machinery behind `?!` as an open interface in
`zom::ops::Try` (core prelude). Any custom type can opt into `?!`
propagation by implementing it.

### Declaration

```zom
interface Try {
    type Output;           // success type — tag 0
    type Residual;         // error/branch type

    fun fromOutput(value: Self::Output) -> Self;
    fun fromResidual(residual: Self::Residual) -> Self;
    fun branch(this) -> Self::Output | Self::Residual;
}
```

### Standard Implementations (Prelude)

1. **`impl<T, E> Try for (T | E)`**: `Output = T`, `Residual = E`. Base
   case for built-in unions.
2. **`impl<T> Try for Option<T>`**: `Output = T`, `Residual = ()`. Allows
   `option?!` primarily inside functions returning `Option<U>`. In other
   contexts, **ZOM0954 TryResidualMismatch** fires since `()` usually
   does not coerce into the enclosing raises set.
3. **`impl<T> Try for Poll<T>`**: `Output = T`,
   `Residual = Poll::Pending`. Supports poll-style async code —
   `poll_result()?!` propagates `Pending` without unnecessary re-wakes.

### User Implementations

Users may implement `Try` for crate-local types under standard orphan
rules (Chapter 13). A canonical example: a `Parsed<T>` wrapper that
attaches parser metadata to both paths; `Try` lets callers use `?!`
without losing metadata.

### Object-Safety Restriction

`Try` is **not object-safe** (associated types appear in method signatures
without going through `self`). `dyn Try` is a compile error. This is
intentional: `?!` desugaring requires monomorphization-time access to the
concrete `Output` and `Residual` types to re-wrap the residual into the
enclosing function's return union.

---

## 11.5 The `!!` Dual-Axis Operator

`!!` has two distinct semantic roles determined by grammatical position
(parser resolves via context; see Chapter 17).

### Type Position: Double Error Union `T!!E`

`!!` between two types augments an existing result type with an additional
error set:

- If `R` has success `T` and error set `{A1..Am}`, and `E` denotes
  `{B1..Bn}`, then `R!!E` expands to `T | A1 | ... | Am | B1 | ... | Bn`.
- If `R` is a plain type `T` (empty error set), `T!!E = T | B1 | ... | Bn`.
- Duplicates are removed during canonicalization.

```zom
// Layer RetryError onto any fallible result.
type Retried<T, E, R> = (T raises E)!!RetryError;   // => T | E | RetryError
```

This is a pure type-level expansion with no runtime cost.

### Expression Position: Unwrap-or-Panic `expr!!`

Postfix `!!` on an expression desugars to:

```zom
match expr {
    Ok(v) -> v,
    Err(e) -> panic!("Called !! on error value: {e:?}"),
}
```

The panic message includes source location, `Debug` formatting of the
error, and a captured backtrace (Section 11.11).

### Appropriate Usage

`expr!!` is appropriate only in: (1) test code inside
`#[zom::cfg(test)]` modules, (2) prototype code during exploratory
development, and (3) cases where surrounding invariants logically
guarantee the error branch is unreachable (with an explanatory comment).

**ZOM0955 UnwrapInProduction** is a warning for any `expr!!` outside
`#[zom::cfg(test)]` under the release profile. It is promotable to error
via `#![zom::deny(unwrap_in_production)]` or `--deny ZOM0955`.

---

## 11.6 Panic Strategy (TBD-1)

A panic is an unrecoverable programmer-fault condition. It is NOT the
mechanism for expected failures — those belong in `raises`. Panic sources:
`expr!!` on error, `panic!()` macro, out-of-bounds indexing, integer
overflow in debug mode, `unreachable!()` / `todo!()`, `.unwrap()` on
`None` or error union, OOM under `#![zom::oom(panic)]`, and assertion
macros.

### Profile Defaults

| Profile | Default | Semantics |
|---|---|---|
| `dev` | `unwind` | Stack unwound frame by frame; destructors run in reverse order; `PanicInfo` with backtrace printed at thread/task boundary. |
| `release` | `abort` | Process terminates immediately via platform abort; no unwinding, no destructor runs; max performance + min binary size. |

### Overrides

Crate root (overrides both profiles simultaneously):
```zom
#![zom::panic(abort)]   // or #![zom::panic(unwind)]
```

Per-profile in `Zom.toml`:
```toml
[profile.dev]     panic = "unwind"
[profile.release] panic = "abort"
```

If both a crate attribute and `Zom.toml` specify a mode for the same
(crate, profile) pair, **ZOM0963 PanicStrategyInconsistent** fires — the
compiler never silently picks a winner.

### Destructor Interaction

With `panic = "unwind"`: a panic inside a destructor while another panic
is unwinding triggers immediate abort — no recovery from nested panics.
With `panic = "abort"`: no destructors run at all; OS-level resources
are still reclaimed by the kernel, but user-level `Drop` logic (buffer
flushes, checkpoint writes) does not execute.

---

## 11.7 `zom::panic::catch_unwind` (TBD-7)

`catch_unwind` captures an unwinding panic and converts it to a typed
result. Its primary and strongly recommended use is as an FFI safety
barrier.

### Signature

```zom
use zom::panic::PanicInfo;
fun catch_unwind<T>(f: fn() -> T) -> T | PanicInfo;
```

`PanicInfo` fields: `message: str`, `file: str`, `line: u32`,
`column: u32`, `backtrace: Option<&Backtrace>`.

### Semantics

- `panic = "unwind"` + `f` panics: unwinder stops at `catch_unwind`
  frame; all drops in `f` have run; returns `PanicInfo`.
- `panic = "abort"`: `catch_unwind` is a no-op — a panic aborts before
  the function can observe it. The compiler emits an informational note.
- `f` returns normally: returns the success value.

### FFI Boundary Example

```zom
#[export]
fun zom_plugin_entry(input: *const u8, len: usize) -> i32 {
    match zom::panic::catch_unwind(|| real_entry(input, len)) {
        Ok(code) -> code,
        Err(p) -> {
            eprintln!("zom plugin panicked: {}", p.message);
            eprintln!("{}", p.backtrace.map_or_else(|| "<no bt>", |bt| bt.fmt()));
            return -1;
        }
    }
}
```

### Lint Restriction

**ZOM0962 CatchUnwindOutsideFfi** warns when `catch_unwind` is called
inside a function that is neither `extern "C"` / `#[export]` nor inside
a `#[zom::error_boundary]` module. The intent: keep unwinder catch
points sparse and auditable. Use `#[zom::error_boundary]` for
request-scoped handlers that legitimately convert panics to 500s.

---

## 11.8 OOM Strategy (TBD-5)

Out-of-memory is a finite-resource exhaustion, not a bug. In ZOM,
allocation failure is a typed error by default — not a panic.

### Default Behavior

All stdlib heap-allocation functions return `T | Alloc.Error` when the
allocation can fail:

```zom
let v: Vec<u8> | Alloc.Error = Vec::with_capacity(1_000_000_000);
let b: Box<BigStruct> | Alloc.Error = Box::new(BigStruct { ... });
```

Convenience macros (`vec![x; N]`, etc.) apply implicit `!!` for
prototyping; **ZOM0955** catches these in release.

### `Alloc.Error` Definition

```zom
enum Alloc.Error {
    CapacityOverflow,     // requested size overflows usize math
    OutOfMemory,          // allocator returned null
    UnsupportedLayout,    // zero-size or invalid alignment
}
```

Non-exhaustive; minor releases may add variants without breakage.

### Static Check

**ZOM0958 AllocLayoutInvalid** is a compile-time diagnostic for
statically-detectable invalid layouts (size 0 with non-drop type,
alignment not a power of two, exceeding `MAX_ALIGN`). Distinct from the
runtime variant for dynamically-computed layouts.

### Global Opt-Out

```zom
#![zom::oom(panic)]
```

Under this attribute, every allocation return type maps from
`T | Alloc.Error` to plain `T`. Any OOM triggers an immediate panic
embedding the size and layout. Typical uses: embedded targets with
fixed-size heaps where OOM triggers a power-cycle, or rapid prototyping.

### Style Lint

**ZOM0959 OomPanicWithoutAttr** fires when a crate lacks the
`#![zom::oom(panic)]` attribute but unwraps every `Alloc.Error` via
`!!`. Suggests either adding the attribute or handling OOM explicitly.

---

## 11.9 The `Error` Interface and `#[zom::derive(Error)]` (TBD-8)

The `Error` interface is the standard hook for user-defined error types
to participate in automatic printing, chain formatting, backtrace
capture, and `dyn Error` interop. It lives in `zom::error::Error` and
is re-exported by the prelude.

### Declaration

```zom
interface Error {
    fun message(this: &Self) -> str;
    fun source(this: &Self) -> Option<dyn Error>;
    fun backtrace(this: &Self) -> Option<&Backtrace>;
}
```

`Error` is object-safe; `dyn Error` is the standard way for libraries
to accept arbitrary error values.

### `#[zom::derive(Error)]`

Auto-generates a correct, low-boilerplate implementation. Applies to
enums and structs only; applying to union/class/alias/primitive yields
**ZOM0960 DeriveErrorOnNonEnumStruct**.

#### `message()` Generation

- Unit enum variant / unit struct: `"<TypeName>::<VariantName>"` or
  `"<StructName>"`.
- Named fields: `"<TypeName>::<VariantName> (<k1>=<Debug(v1)>, ...)"` in
  declaration order. Fields marked `#[skip]` are omitted.
- Tuple struct: `"<StructName>(<Debug(v1)>, ...)"`.
- `#[message = "..."]` on a variant or struct overrides the default;
  `{field_name}` placeholders are substituted at runtime.

#### `source()` Generation

- Default returns `None`.
- Exactly one field with `#[source]` enables auto-generated `source()`.
  Valid field types: `E where E: Error`, `Box<dyn Error>`,
  `Option<E> where E: Error`, `Option<Box<dyn Error>>`.
- Multiple `#[source]` fields, or a `#[source]` field whose type does
  not impl `Error`, produce **ZOM0961 ErrorFieldInvalid**.

#### `backtrace()` Generation

Backtrace capture is controlled by three priority-ordered mechanisms:

1. `ZOM_BACKTRACE=errors` env var — force-capture for every `Error`
   instance process-wide.
2. Crate-level: `#![zom::error(trace)]` — enable for all instances in
   the crate.
3. Variant/struct-level: `#[zom::error(trace)]` — enable for one
   variant or struct, overriding a crate-level attribute of opposite
   polarity.

When enabled, the compiler either:

- Stores in a user-declared `#[backtrace]` field of type
  `Option<Backtrace>` or `Backtrace`. Wrong type => **ZOM0961**.
- Or stashes in a compiler-managed side table keyed by the error's
  allocation identity. For stack-allocated errors the compiler
  transparently moves to a pinned heap slot for stable identity.

---

## 11.10 `main()` and Top-Level Execution (TBD-9)

ZOM's `main` supports `raises` natively and is implicitly async — no
separate `async fun main` syntax. Zero Function Color design (Chapter 15).

### Valid Signatures

```zom
// No errors, synchronous body.
fun main() { println!("Hello, world!"); }

// Recommended — typed errors, ?! propagation.
fun main() raises AppError {
    let cfg = parse_config("app.toml")?;
    let db = connect_db(&cfg.db_url)?;
    run_app(cfg, db)?;
}

// Implicitly async — await works directly in main body.
fun main() raises AppError {
    let r = http::get("https://example.com").await?;
    println!("{}", r.body);
}
```

Omitting `raises` is equivalent to `fun main() -> ()`.

### Implicit Async Contract

If the body contains `.await`, the compiler auto-initializes the default
executor on the calling thread; no `async` keyword or `block_on` call
required. If no `.await`, the runtime initializes in minimal
synchronous-only configuration. See Chapter 15 for executor details.

### Runtime Exit Contract

After standard initialization (stdlib hooks, logger, backtrace engine,
panic handler — order non-normative):

1. `main` returns `()` => `exit(0)`, no stderr output.
2. `main` returns an error variant:
   - If impls `Error`: print `message()`, then the chain of `.source()`
     (each indented, prefixed `Caused by: `), then `.backtrace()` if
     present.
   - Else: `Debug` format to stderr.
   - Call `exit(1)`.
3. `main` panics: print panic info + backtrace to stderr, `exit(101)`.
   Exit code 101 is reserved for panic.

**ZOM0957 MainNonZero** is an info-level, non-suppressible diagnostic
emitted whenever a crate's `main` declares a non-empty `raises` clause.

Custom entry points via `#[zom::start]` are specified in Chapter 16.

---

## 11.11 Backtrace Capture Strategy (TBD-10)

ZOM separates backtrace capture into two independent channels: panic
backtraces and error backtraces, each with its own defaults and opt-in
mechanisms.

### Environment Variable

`ZOM_BACKTRACE` overrides both channels at runtime:

| Value | Effect |
|---|---|
| unset | Follow per-channel defaults. |
| `0` / `off` | Suppress all capture, both panic and error. |
| `1` / `short` | Panics only; traces truncated to 16 filtered frames. |
| `2` / `full` | Panics only; untruncated traces. |
| `errors` | Full panic traces + force-capture for every `Error` in process. |

Unknown values are treated as `0` and produce one runtime warning.

### Panic Backtraces

Always captured by default (subject to env override and `panic = "abort"`
restriction). After capture, the trace is filtered to strip: frames
inside `panic!` / `begin_panic` / personality routines; `__zom_`-prefixed
zomrt frames; libunwind / platform unwinder frames.

### Error Backtraces

Opt-in by default, controlled by the three-tier attribute system in
Section 11.9. Capture happens **once** at instance construction — never
duplicated on clone or move.

### Profile Defaults

| Profile | Panic backtrace default | Error backtrace default |
|---|---|---|
| `dev` | Always captured, full. | Implicitly enabled for any crate containing at least one `#[zom::derive(Error)]`. |
| `release` | Always captured, full (panic is rare enough that cost is negligible). | Disabled; opt-in only via attribute or env. Typical size win: 2–15%; perf win: 0–5%. |

### Platform Restrictions

Backtrace capture requires a working unwinder and frame-pointer/DWARF CFI
metadata. `panic = "abort"` on targets without frame-pointer unwinding
(e.g. some `wasm32-unknown-unknown` configurations) and rare embedded
targets without metadata fall through to a runtime warning.

**ZOM0964 BacktraceUnavailable** is a one-time warning printed to stderr
per process; subsequent `backtrace()` calls return `None`. This is a
runtime warning, not a compile error — the same crate may target both
native and constrained builds without conditional compilation.

---

## 11.12 Cooperative Cancellation and Destructors (TBD-6)

Detailed cancellation semantics live in Chapter 15. The error-handling
contract is specified here because it interacts with `raises`, destructor
invariants, and the `?!` operator.

`scope.cancel_all()` performs exactly two actions:

1. Atomically sets `Scope.cancel = true` — every `.await` site may
   observe this flag.
2. Wakes every pending task on the scope. On next poll, each task
   observes the flag either in its own poll loop or via a `CancelError`
   raised from an async stdlib function.

Cancellation **never** force-interrupts a running destructor, never
longjmps out of user code, and never injects synthetic control flow
between sequential statements. If a task is mid-`Drop` when
`cancel_all()` fires, the drop body runs to completion. This is a
language guarantee — the compiler will not reorder or split drop blocks
around cancellation points.

### `CancelError` in `raises`

Async stdlib operations include `CancelError` in their `raises` set.
Callers propagate it via standard `?!`; it is just another error
variant. Whether a function can be cancelled is therefore visible in its
signature.

### v2 Extension Point

v1 deliberately omits a `CancelSafe` marker. Valid use cases exist for
distinguishing drop-safe from rollback-required types, and this is
scheduled for v2 evaluation. It is normative that v1 does not require
any cancel-safety marker and does not reject any type from scoped
cancellation.

---

## 11.13 Diagnostic Code Table

16 new ZOM09xx codes are defined by this chapter. Severity, lint levels,
and help text are owned by the central registry (Architecture §8); this
table is the authoritative list of names and meanings.

| Code | Name | Meaning |
|---|---|---|
| ZOM0950 | ErrorUnionAmbiguous | Ok and error variants share overlapping concrete types in `T \| E`. Wrap one side in a newtype or refactor. |
| ZOM0951 | RaisesSignatureMismatch | Function-pointer assignment or trait impl has an incompatible raises set. Impls may only widen, not narrow. |
| ZOM0952 | CannotPropagateError | `?!` residual type not in enclosing raises. Add it or use explicit `match`. |
| ZOM0953 | NotATryType | `?!` applied to a type that is neither an error union nor a `Try` implementor. |
| ZOM0954 | TryResidualMismatch | A `Try::Residual` cannot coerce into the enclosing return union. Widen raises or switch to `match`. |
| ZOM0955 | UnwrapInProduction | `expr!!` used outside `#[zom::cfg(test)]` in release profile. Warn; promotable to error. |
| ZOM0956 | ErrorDiscardedSilently | Error-union return value never matched / ?!'d / !!'d. May indicate swallowed error; bind to `_ = ...` if deliberate. |
| ZOM0957 | MainNonZero | Crate's `main` has a non-empty `raises`. Info-level, non-suppressible reminder about exit(1). |
| ZOM0958 | AllocLayoutInvalid | Static compile-time check detected invalid allocation size/alignment. |
| ZOM0959 | OomPanicWithoutAttr | Style lint: all `Alloc.Error` unions are `!!`-unwrapped but crate lacks `#![zom::oom(panic)]`. |
| ZOM0960 | DeriveErrorOnNonEnumStruct | `#[zom::derive(Error)]` applied to a non-enum/non-struct type. |
| ZOM0961 | ErrorFieldInvalid | `#[source]` or `#[backtrace]` on a field whose type does not satisfy the required interface. |
| ZOM0962 | CatchUnwindOutsideFfi | `catch_unwind` used outside `extern "C"` / `#[zom::error_boundary]` context. Warning. |
| ZOM0963 | PanicStrategyInconsistent | Crate-level `#![zom::panic(X)]` conflicts with `[profile.*].panic` in `Zom.toml`. |
| ZOM0964 | BacktraceUnavailable | Backtrace capture requested but unsupported on current platform/panic mode. Warning once. |
| ZOM0965 | UndefinedBehaviorOnUnwind | `extern "C"` function lacks `#[zom::error_boundary]` or internal `catch_unwind`. Unwind across boundary = UB. Warning. |

---

## 11.14 End-to-End Example — HTTP Server

```zom
// ---- errors.zom ----

#[zom::derive(Error)]
enum ConfigError {
    IoError(#[source] std.io.Error),
    ParseError(String),
}

#[zom::derive(Error)]
enum DbError {
    ConnectionError(String),
    QueryError(#[source] sql.Error),
}

#[zom::derive(Error)]
#[zom::error(trace)]
enum AppError {
    Config(#[source] ConfigError),
    Database(#[source] DbError),
}

// ---- config.zom ----
fun load_config() -> Config raises ConfigError {
    let text = std.fs.read_to_string("app.toml")?!;
    parse_toml::<Config>(text)?!
}

// ---- db.zom ----
fun connect(url: &str) -> DbPool raises DbError {
    match DbPool::open(url) {
        Ok(pool) -> pool,
        Err(e) -> return Err(DbError::ConnectionError(
            format!("failed to connect to {url}: {e}")
        )),
    }
}

fun query(pool: &DbPool, sql: &str) -> Rows raises DbError {
    pool.exec(sql)?!
}

// ---- main.zom ----
use errors::AppError;
use config::load_config;
use db::connect;
use http::{self, Request, Response};

fun handle(req: Request) -> Response raises AppError {
    match req.path() {
        "/users" -> {
            let cfg = load_config()?!;
            let db = connect(&cfg.db_url).map_err(AppError::Database)?!;
            let rows = db::query(&db, "SELECT * FROM users")
                .map_err(AppError::Database)?!;
            Response::json(rows)
        },
        _ -> Response::not_found(),
    }
}

fun main() raises AppError {
    let cfg = match load_config() {
        Ok(c) -> c,
        Err(e) -> return Err(AppError::Config(e)),
    };

    let db = connect(&cfg.db_url).map_err(AppError::Database)?!;

    http::serve(cfg.http_addr, db, handle).await?!;

    println!("Server exited normally.");
}
```

Design points: typed error hierarchy per subsystem with `AppError` as
top-level wrapper via `#[source]`; `raises` on every fallible boundary
for auditability; `?!` for straight-line propagation and `match`/
`map_err` where context must be added; `main` with `raises AppError` so
the runtime handles exit code and formatted printing.

---

## 11.15 Grammar Cross-References

Formal EBNF lives in Chapter 17. This section summarizes user-facing
forms and their grammatical categories for cross-reference.

### Function Signature with `raises`

```ebnf
FunctionSig ::= 'fun' Identifier Generics '(' ParamList ')'
                ( '->' Type )? RaisesClause? Block
RaisesClause ::= 'raises' ( Type | '[' Type ( ',' Type )* ','? ']' )
```

The `raises` keyword binds to the return type. Desugaring inserts
raised types into the return union before type checking.

### Postfix `?!` and `!!`

```ebnf
PostfixOp ::= '?!' | '!!' | '.' Identifier | '[' Expr ']' | '(' ArgList ')'
```

Both occupy the Postfix tier — tighter than any prefix/infix operator
(including unary `!` for negation and `*` for dereference).
Left-associative.

### Union Type Syntax

```ebnf
UnionType ::= Type '|' Type
```

`|` is the lowest-precedence type operator. Parenthesize in
precedence-ambiguous positions. See Chapter 03 for canonicalization
and subtype rules.

### Attribute Forms

```ebnf
OuterAttr ::= '#' '[' AttrPath ( '(' TokenTree ')' )? ']'
InnerAttr ::= '#' '!' '[' AttrPath ( '(' TokenTree ')' )? ']'
```

Crate-level (inner): `#![zom::panic(...)]`, `#![zom::oom(panic)]`,
`#![zom::error(trace)]`.

Type-level (outer): `#[zom::derive(Error)]`, `#[zom::error(trace)]`,
and helper attributes `#[source]`, `#[backtrace]`,
`#[message = "..."]`, `#[skip]`.

---

## 11.16 TBD Conformance Map

| TBD | Frozen Design | Specified In |
|---|---|---|
| TBD-1 | Debug: unwind / Release: abort; `#![zom::panic(...)]` override | §11.6 Panic Strategy |
| TBD-2 | `Result<T,E>` = structural `T \| E`; `raises[]` sugar; `?!` = match+return; all compile to tagged union IR | §11.1, §11.2, §11.3 |
| TBD-3 | `T!!E` type = double error union; `expr!!` = unwrap-or-panic | §11.5 `!!` Dual-Axis Operator |
| TBD-4 | Open `interface Try` with associated types; user-extensible; NOT object-safe | §11.4 The Open `Try` Interface |
| TBD-5 | Value-error default for OOM (`Alloc.Error`); `#![zom::oom(panic)]` opt-out | §11.8 OOM Strategy |
| TBD-6 | Cancellation = cooperative flag + awaken; never force-interrupt destructors; no CancelSafe in v1 | §11.12 Cooperative Cancellation |
| TBD-7 | `catch_unwind` provided; lint restricts to FFI / error boundary | §11.7 `catch_unwind` |
| TBD-8 | `Error` interface; `#[zom::derive(Error)]` auto-generation rules | §11.9 `Error` Interface and Derive |
| TBD-9 | `fun main() raises E` supported; implicitly async; runtime exit contract | §11.10 `main()` and Top-Level Execution |
| TBD-10 | Panic backtrace always-on; error backtrace opt-in; Debug ON / Release OFF; `ZOM_BACKTRACE` env | §11.11 Backtrace Capture Strategy |
