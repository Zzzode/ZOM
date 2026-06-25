# Foreign Function Interface and Interoperability

This chapter specifies the mechanism by which ZOM code calls functions implemented in other languages and, conversely, exposes ZOM functions to callers written in other languages. The primary target of the v1 FFI layer is the **C calling convention and C type layout**, which serve as the cross-language lowest-common denominator. Secondary interop with Rust is achieved by routing through the same C ABI; Rust-native async bridges are deferred to the v2 roadmap.

## 18.0 Purpose and Scope

The ZOM FFI layer provides four orthogonal capabilities:

1. **Foreign imports.** Declaring functions implemented in a C (or C-compatible) shared library and calling them from ZOM source with zero overhead beyond the C call itself.
2. **Foreign exports.** Annotating ZOM functions so they are callable from C or any language that understands the C ABI, with predictable symbol names and call-frame layouts.
3. **Memory ownership discipline.** Normative rules for passing pointers, buffers, and heap-allocated objects across the boundary so that each side's allocator stays self-consistent.
4. **Panic and unwind safety.** Rules and compiler-enforced diagnostics that prevent undefined behavior when a ZOM panic escapes through the FFI boundary.

The boundary layer is depicted below:

```mermaid
graph TD
    ZOM[ZOM code<br/>linear types, markers, borrows] --> AB[FFI Boundary layer]
    AB -->|extern "C" import| CLIB[C shared library<br/>*.so / *.dll / *.dylib]
    CLIB -->|no_mangle export| AB
    AB -->|C ABI bridge| RUST[Rust crate<br/>repr(C) + C calling convention]
    RUST -->|staticlib linkage| CLIB
    AB -->|marshaling rules| MARSH[Type marshaler<br/>repr(C), FfiSafe, raw ptrs]
```

All interop paths converge on the C ABI. Calling a Rust function directly through a non-C ABI is not supported in v1; instead, users are expected to mark the Rust side with `#[no_mangle] extern "C"` and call it through the `extern "C"` import mechanism described in §18.1. A ZOM-native async bridge for Rust is planned for v2 of the language; v1 treats Rust as just another C-ABI producer.

## 18.1 `extern "C"` — Declaring Foreign Imports

An `extern` block declares one or more foreign functions whose implementation is provided by a linked external library. The normative EBNF (cross-reference Ch.17) is:

```
ExternBlock       ::= 'extern' StringLiteral? '{' ExternItem* '}'
ExternItem        ::= 'export'? 'fun' Identifier '(' FFIParamList ')'
                      ( '->' FFIType )? ( 'raises' Type )? ';'
FFIParamList      ::= ( FFIParam ( ',' FFIParam )* ','? )?
FFIParam          ::= Identifier ':' FFIType
FFIType           ::= PrimitiveType
                    | RawPointerType
                    | CCharType
                    | ExternOpaqueType
                    | '(' FFIType ')'
RawPointerType    ::= '*' ( 'const' | 'mut' )? TypeName
```

**Default ABI.** When the `StringLiteral` is omitted, the ABI defaults to `"C"`. v1 recognizes the following ABI strings: `"C"`, `"cdecl"`, `"stdcall"`, `"fastcall"`, `"vectorcall"`, `"aapcs"`, `"win64"`, and `"sysv64"`. Any other ABI literal is diagnosed as **ZOM1801 UnknownAbi**.

### Normative constraints on extern function signatures

1. **FFI-safe parameters.** Every parameter type and the optional return type must satisfy the built-in marker `FfiSafe` (§18.5). Concretely:
   - Primitives `i8`..`u64`, `f32`, `f64`, `bool`, `usize`, `isize` are FFI-safe.
   - Raw pointers `*const T` and `*mut T` are FFI-safe for any `T`.
   - Structs, unions, and enums annotated `#[repr(C)]` are FFI-safe (see §18.5).
   - A type that does not impl `FfiSafe` is rejected with **ZOM1802 NonFfiSafeType**.
2. **No ZOM Linear types.** Linear-typed parameters or return values are rejected at signature-check time (ZOM1810 LinearIncompatibleFfi) because the C caller has no ownership model and could leak or double-free the opaque payload. The opaque-pointer pattern `*const OpaqueTag` / `*mut OpaqueTag` works around this restriction: ownership is expressed through pointer identity, not through a ZOM-level Linear value.
3. **No bodies.** Functions inside an `extern` block are declarations only; writing a body is an error: **ZOM1804 ExternFunctionBody**.
4. **`raises` clause for setjmp/longjmp-style raise.** Writing `extern fun foo() raises FFIError;` declares that the foreign implementation may raise an error via the C helper `zom_raise_error`. If the foreign code panics or longjmps without a matching `raises` clause, the behavior is undefined and no diagnostic is emitted at compile time for the foreign side; the runtime, however, will trap if `--runtime-checks` is enabled.

### Example — POSIX I/O

```zom
extern "C" {
    fun open(path: *const u8, flags: i32, ...) -> i32;
    fun close(fd: i32) -> i32;
    fun read(fd: i32, buf: *mut u8, count: usize) -> isize;
    fun write(fd: i32, buf: *const u8, count: usize) -> isize;
}
```

The `...` token in the `open` declaration enables variadic arguments, which carry their own diagnostic surface: on architectures that do not support C variadic natively (for example WebAssembly with some target profiles), the compiler emits **ZOM1830 VarargsUnsupportedVariance** as a warning.

## 18.2 Exposing ZOM to Foreign Code

Three mechanisms control how ZOM functions become callable from C:

- **`#[zom::no_mangle]`** disables the ZOM name mangler for the annotated function; the resulting symbol name is the function's identifier exactly as written.
- **`#[zom::export_name = "foo_c_entry"]`** overrides the exported symbol name entirely, regardless of identifier text. Conflicts with `#[zom::no_mangle]` are diagnosed via the shared FFI attribute conflict machinery (ZOM0626).
- **`extern "C" fun ...`** on the function *definition* (not only in a block) switches the function's own calling convention to the C ABI. Without this modifier, a ZOM function uses the native ZOM calling convention and cannot be called from C.

The canonical export pattern combines all three:

```zom
#[zom::no_mangle]
extern "C" fun process_image(
    input: *const u8,
    len: usize,
    output: *mut u8,
) -> i32 {
    // ... ZOM implementation ...
    return 0;
}
```

### Panic boundary rule

If the crate's panic strategy is `"unwind"` and a panic escapes the body of an `extern "C"` function without a corresponding catch, the result is **undefined behavior**. The C ABI does not specify how to propagate unwinding frames, and foreign callers almost never expect an unwind. The compiler enforces this rule at definition time with lint **ZOM0965 UndefinedBehaviorOnUnwind**, unless:

1. the function body's **top-level statement** is a visible `catch_unwind` block, or
2. the function carries the `#[zom::error_boundary]` attribute (§18.4), which implicitly wraps the body at codegen time.

### The opaque-type pattern

When ZOM owns a structured resource and C only needs to pass a handle around, the opaque-pointer pattern lets ZOM retain type safety without leaking layout:

```zom
// ZOM side
export struct Database { conn_str: String, pool: Pool }

// The shared C header carries only a forward declaration:
//   typedef struct Database Database;

#[zom::no_mangle]
extern "C" fun database_new(url: *const u8) -> *mut Database {
    // … construct a Box<Database> and leak into a raw pointer …
}

#[zom::no_mangle]
extern "C" fun database_free(db: *mut Database) {
    // … convert the raw pointer back into a Box and let it drop
}
```

See §18.3 for the allocator-matching rule that underpins this pattern.

### Generic functions and no_mangle

Applying `#[zom::no_mangle]` to a generic function is a hard error — **ZOM1805 NoMangleGeneric** — because the compiler cannot emit a single symbol for every possible monomorphization. Users who want to expose generic instantiations to C must write a thin `extern "C"` wrapper for each concrete type.

## 18.3 Memory Ownership Across the Boundary

The following rules are normative. Any code that violates them has undefined behavior at runtime, even if it compiles without diagnostics.

1. **Pointer aliasing rules.** ZOM's internal aliasing model (equivalent to LLVM's `noalias` / `readonly`) applies at code-generation time for ZOM-owned pointers. Pointers that cross the FFI boundary in either direction follow C's default aliasing rules unless the ZOM side explicitly carries a `restrict`-equivalent annotation. Future attributes in the `zom::ffi::` namespace will expose `restrict` and other qualifiers; in v1, only `*const` vs `*mut` are semantically meaningful.
2. **Lifetime guarantees do not survive the boundary.** ZOM reference types (`borrow<T>`, `own<T>` tokens, and any lifetime-annotated reference) are not FFI-safe. Only raw pointers are. Converting `*const T ↔ borrow<T>` is permitted only when the caller can prove the lifetime statically; at the FFI boundary the **ZOM caller takes full responsibility** for the lifetime, and the compiler does not verify it.
3. **Allocator matching — "who allocates, frees."** If ZOM allocates memory with its own allocator and returns a pointer to C, C **must** return the pointer to a ZOM-exported deallocation function. Passing a ZOM-allocated pointer to the C standard library's `free()` is undefined behavior. The normative design pattern is to export a matching `*_free` function for every `*_new` function that returns a heap allocation. The converse rule also holds: a pointer allocated with C `malloc` must be freed with C `free`, not by ZOM's allocator.
4. **Marker re-establishment on return.** An `extern "C"` function that accepts a raw pointer has no marker information attached on the C side. When a ZOM wrapper converts the pointer back into a typed reference, the wrapper must re-establish `Sendable`, `Shared`, and other marker contracts. The idiomatic approach is to declare a `CWrapper<T>` newtype and provide an `unsafe impl Sendable for CWrapper<DatabaseHandle>` or similar attestation.

## 18.4 Panic Boundary and `catch_unwind`

The combination `#[zom::error_boundary]` on an `extern "C"` function is the **recommended** pattern. It is strictly safer than a manually-written `catch_unwind` because the compiler fills in the boilerplate correctly for every return type, including `unit`, primitives, and error unions.

### Canonical example

```zom
#[zom::error_boundary]
#[zom::no_mangle]
extern "C" fun plugin_init() -> i32 {
    setup_logging();
    load_config()?!;                 // now safe at the FFI boundary
    return 0;
}
```

### Low-level expansion

The attribute desugars the function body into the equivalent of:

```zom
match zom::panic::catch_unwind(|| {
    setup_logging();
    load_config()?!;
    return 0;
}) {
    Ok(res) -> res,
    Err(_payload) -> -1,
}
```

where `-1` is the default error return for integer-typed functions, `null()` for pointer-typed functions, and an all-zero value for other `repr(C)` return types. A future revision will expose a per-function override for the error default.

Writing `catch_unwind` in non-FFI user code is flagged with **ZOM0962 CatchUnwindOutsideFfi** as a warning because the pattern is almost always a code smell — the raises-clause mechanism (Ch.11) is the correct way to propagate errors within ZOM.

## 18.5 FFI-Safe Marker and `#[repr(C)]`

ZOM provides a built-in Tier-1 marker `FfiSafe` whose meaning is: "the in-memory layout of this type is identical to the equivalent C type at every offset."

### Auto-derivation

The compiler auto-implements `FfiSafe` for:

- All primitive numeric and boolean types listed in §18.1.
- All raw pointer types `*const T`, `*mut T` regardless of `T` (pointers are always a single machine word, and the pointed-to layout is opaque to the FFI layer).
- Structs, unions, and enums annotated with `#[repr(C)]` whose every field recursively impls `FfiSafe`.

A struct used as an FFI parameter **must** carry `#[repr(C)]`; missing it raises **ZOM1803 ReprCRequired**, even if the struct's fields look C-like, because without the attribute ZOM may reorder fields to minimize padding.

### Layout attributes

- **`#[repr(C)]` on structs and unions.** Forces C-compatible field ordering and padding. Field offsets match the C ABI size-and-alignment rules of the current target. Without this attribute ZOM may reorder fields; the resulting layout is **not** part of the language ABI.
- **`#[repr(IntType)]` on enums.** `#[repr(i32)] enum Color { Red, Green, Blue }` declares a C-style enum with a fixed discriminant width. Enums that cross the FFI boundary are required to carry such a repr; the absence of a fixed integer repr triggers **ZOM1803 ReprCRequired** at the call site.

### Linear / FfiSafe incompatibility

The implicit meta-rule `#[zom::marker::incompatible(Linear, FfiSafe)]` holds by negative coherence: C has no ownership model, so any type that is Linear cannot simultaneously be FfiSafe. Trying to export a function that takes or returns a Linear type through `extern "C"` is rejected with **ZOM1810 LinearIncompatibleFfi**, even if the user manually writes `impl FfiSafe for LinearType`.

## 18.6 String Conversions

C strings and ZOM strings are fundamentally different:

| Model | Type | Shape | Terminator |
|---|---|---|---|
| C string | `*const u8` / `*mut u8` | Thin pointer | NUL (`\0`) terminated |
| ZOM `StrSlice<'a>` | `StrSlice<'a>` | Fat pointer `(ptr, len)` | No NUL guarantee |
| ZOM owned | `String` | Length-prefixed owned buffer | No NUL guarantee |

The mismatch means passing a ZOM string directly to a C function that expects a NUL-terminated pointer is undefined behavior. The standard library exposes conversion helpers in `zom::ffi::cstr`:

- **`from_cstr(ptr: *const u8) -> StrSlice<'static> raises NulError`** — scans for the trailing NUL byte. Raises `NulError` (diagnostic family ZOM1820) if an interior NUL is present at a nonzero offset. Raises `StrTooLong` (ZOM1821) if the length would exceed `isize::MAX` bytes.
- **`to_cstring(s: StrSlice) -> Vec<u8>`** — appends a NUL terminator and returns an owned, heap-allocated buffer whose pointer can be passed directly to C. The buffer is dropped when the `Vec<u8>` goes out of scope; callers must ensure the pointer is not used past that point.
- **`as_cstr_parts(s: &String) -> (*const u8, usize)`** — exposes the raw pointer and length of an existing `String`'s backing storage **without** appending a NUL. Useful only when calling C APIs that take an explicit length pair.

### Common pattern

```zom
extern "C" { fun puts(s: *const u8) -> i32; }

fun print_c(s: StrSlice) {
    let buf = zom.ffi.cstr.to_cstring(s);
    puts(buf.as_ptr());
    // buf dropped here; the pointer is no longer valid
}
```

## 18.7 Diagnostic Codes (18xx family)

The 18xx range is reserved for FFI and interop diagnostics. Every code below is referenced normatively elsewhere in this chapter and must be registered in `docs/design/architecture.md` §8 and `docs/design/compiler-contracts.md` §2.

| Code | Name | Severity | Meaning |
|---|---|---|---|
| ZOM1801 | UnknownAbi | Error | `extern "X"` specifies an ABI string not supported by the current compiler or target triple. |
| ZOM1802 | NonFfiSafeType | Error | A parameter or return type in an extern block does not implement the `FfiSafe` marker. |
| ZOM1803 | ReprCRequired | Error | A struct, union, or enum used as an FFI parameter or return lacks the `#[repr(C)]` (or fixed integer repr for enums) attribute. |
| ZOM1804 | ExternFunctionBody | Error | A function declared inside an `extern` block contains a body; bodies are illegal there because the function is imported, not defined. |
| ZOM1805 | NoMangleGeneric | Error | `#[zom::no_mangle]` is applied to a generic function, which cannot have a single exported symbol for all monomorphizations. |
| ZOM1810 | LinearIncompatibleFfi | Error | A function exported or imported through `extern "C"` takes or returns a Linear-typed value, which C cannot safely own. |
| ZOM1820 | NulError | Error | A C-string conversion encounters an unexpected interior NUL byte at a nonzero offset. |
| ZOM1821 | StrTooLong | Error | A C-string length exceeds `isize::MAX`, violating the thin-pointer length contract. |
| ZOM1830 | VarargsUnsupportedVariance | Warning | `...` variadic arguments in an extern declaration require platform support that is unavailable on the current target (for example, some wasm profiles). |
