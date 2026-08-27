---
paths:
  - "**/*.cc"
  - "**/*.h"
  - "libraries/zc/**"
---

# C++ and ZC Library Rules

All C++ code in the project must follow these rules. `zc` types are the default;
`std` is banned except where a genuine substitute does not exist.

---

## C++ Standard

- **C++20 only.** No vendor extensions. No C++23 features — we target GCC 12+ /
  Clang 15+ / Xcode 15+ per `/AGENTS.md` § Environment Requirements.
- Prefer `constexpr` for everything computable at compile time.
- Use `concept`s for template constraints instead of `enable_if` / SFINAE.

---

## File Structure

- **Headers**: `.h`. Implementations: `.cc`. Never inline-heavy `.cc`-in-headers.
- Comply with `.clang-format` at all times. Run `python scripts/check-format.py`
  before submitting.
- **Every non-trivial class uses the Pimpl idiom:**

```cpp
// my_class.h
class MyClass {
public:
    MyClass();
    ~MyClass();
    MyClass(MyClass&&) noexcept;
    MyClass& operator=(MyClass&&) noexcept;
    ZC_DISALLOW_COPY(MyClass);

    /// \brief Do something.
    void doSomething();

private:
    struct Impl;
    zc::Own<Impl> impl;
};
```

The implementation of `Impl` lives **only** in `my_class.cc`.

---

## ZC Type Mandates

### Ownership

| Goal | Type | Forbidden Alternative |
|---|---|---|
| Exclusive owning pointer | `zc::Own<T>` | `T*`, `std::unique_ptr<T>`, `malloc`/`free` |
| Non-owning reference to one object | `zc::Ptr<T>` / `T&` | `T*` in public API surfaces |
| Non-owning slice of bytes | `zc::ArrayPtr<T>` / `zc::StringPtr` | `T*` + size parameter pair |
| Nullable value | `zc::Maybe<T>` with `ZC_IF_SOME` | Returning `std::nullptr_t` magic values, out-params with bool success |
| Dynamic array | `zc::Vector<T>` (stores `T` directly) | `zc::Vector<zc::Own<T>>` (redundant ownership) |
| Tagged union | `zc::OneOf<A, B, C…>` | `enum class` + `void*` pairs or manual variant |
| Owning UTF-8 string | `zc::String`, moved **only** via `zc::mv` | `std::string`, implicit string copies |
| String construction / concat | `zc::str(a, b, c, …)` | `operator+` string chains, `strcat` |

### Common Anti-Patterns (Block on Review)

1. ❌ `zc::Vector<zc::Own<T>>` — `Vector<T>` handles ownership directly.
2. ❌ `zc::String copy = owned;` — `String` is non-copyable; use `zc::mv(owned)`.
3. ❌ Returning a local `T[10]` via `zc::ArrayPtr<T>` — dangling pointer.
4. ❌ `maybe.orDefault(0)` silently hides failures — use `ZC_IF_SOME`.
5. ❌ Adding public raw pointer fields (`T*`) to a class.
6. ❌ `const_cast<T&>(const_obj)` — **strictly prohibited.**

### Raw Pointer Exceptions

Raw pointers are permitted **only** inside:

- FFI bindings (`extern "C"`) where the C interface requires them.
- The `zc` library internals where a type's semantics fundamentally require raw
  arithmetic (e.g. `ArrayPtr` implementation itself).

Even then, wrap the raw pointer in an RAII type or a zc `Own<T, CustomDisposer>`
before it escapes the owning scope.

---

## Naming

| Kind | Convention | Example |
|---|---|---|
| Classes, structs, enums, concepts | `PascalCase` | `Parser`, `SyntaxKind` |
| Functions, variables, fields | `camelCase` | `parseExpression()`, `currentToken_` |
| Compile-time constants / enum variants | `kCamelCase` | `kMaxRecursionDepth` |
| Private fields | `_camelCase` suffix with underscore | `_impl`, `_privateField` |
| Prohibited anywhere | trailing `_` on property names | `field_` (rejected) |
| Macros from zc | `ZC_` prefix | `ZC_IF_SOME`, `ZC_LIKELY` |
| ZOM-specific macros | `ZOM_` prefix | `ZOM_DIAGNOSTIC`, `ZOM_ASSERT` |

---

## `std::` Usage

Ban by default. If you must use `std::`:

1. Add a comment `// std:: required — zc has no X yet` directly above the include.
2. File a tracking issue to implement the missing construct in `zc/`.
3. Keep the usage scoped to one `.cc` file. Do not leak `std::` types into headers.

Known exceptions (documented here — do not add without amending this list):

- `zomlang/compiler/driver/package/manifest-parser.cc` may use
  `std::string_view` and non-owning raw pointers returned by the vendored
  toml++ API. The adapter must not expose either through a ZOM header or retain
  a pointer beyond the owning `toml::parse_result`. RFC 0012 and its
  implementation tracker own removal if toml++ gains a zc-native adapter.

---

## Comments

- **English only.** No Chinese / mixed-language comments.
- Public API: `/// \brief Doxygen triple-slash style.` Always include `\param`,
  `\return`, and `\throws` when applicable.
- Implementation notes: `// double-slash, English, imperative mood.`
- No "how" comments for code that is already self-documenting; comments explain
  the *why*, *trade-off*, or *non-obvious invariant*.
- Comments describing the obvious (e.g. `// increment i`) are rejected on review.

---

## Exceptions and Asserts

- Use `zc::Exception` with `zc::str(...)` for error messages, never `std::exception`
  or naked `throw const char*`.
- Use `ZC_IREQUIRE(cond, "message")` for invariant checks that must never fail.
- `ZC_ASSERT_PTR_COUNTERS`, `ZC_LIFETIMEBOUND`, `ZC_WARN_UNUSED_RESULT` — use
  wherever applicable; do not remove them "to make the build quieter."

---

## Singletons / Globals

**Strictly prohibited.** Every piece of state must be owned transitively by
`CompilerSession`, `Driver`, or an explicit owning object. No function-local
static caches, no `static Parser* g_parser`, no hidden mutable globals. If a
dependency is needed, thread it through the object graph.
