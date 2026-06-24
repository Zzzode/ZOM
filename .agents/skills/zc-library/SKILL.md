---
name: zc-library
description: Correct use of zc core library types (Own, Ptr, ArrayPtr, Vector, String, Maybe, OneOf), Pimpl, and memory/ownership conventions. Trigger when writing or reviewing C++ code.
---

# ZC Library Usage Skill

Ensures every piece of C++ code in the repo uses `zc` types correctly,
never falls back to `std` for things zc already provides, and obeys the
ownership / Pimpl / naming rules codified in `.agents/rules/cpp-zc.md`.

---

## Type Mapping (Owed)

Refer to this table *before* writing any new type signature. If the target
type is not on the right side, you are doing it wrong.

| Intention | ZC Type | Notes |
|---|---|---|
| Owning heap object, single owner | `zc::Own<T>` | Construct via `zc::heap<T>(args)` |
| Owning heap object, custom dispose | `zc::Own<T, D>` | e.g. FFI handles |
| Non-owning pointer, can be null | `zc::Ptr<T>` | Public API surface; never raw `T*` |
| Non-owning reference, never null | `T&` (const-qualified where possible) | — |
| Read-only contiguous buffer | `zc::ArrayPtr<const T>` or `zc::StringPtr` | Prefer over `(ptr, len)` pairs |
| Mutable contiguous buffer | `zc::ArrayPtr<T>` | Never returned from a function returning a local |
| Nullable value with none-state | `zc::Maybe<T>` | Bind with `ZC_IF_SOME`, not `.orDefault()` |
| Tagged union / sum type | `zc::OneOf<A, B, C, …>` | Never `enum + union` |
| Resizable owned sequence | `zc::Vector<T>` | Stores `T` directly, never `zc::Own<T>` |
| Owning UTF-8 string | `zc::String` | Moved exclusively via `zc::mv(s)`; never copied |
| String build / concatenation | `zc::str(a, b, c, …)` | Never `operator+` chains or `strcat` |

---

## Anti-Pattern Scanner

When reviewing any C++ diff, scan for these patterns and block until fixed:

1. **`zc::Vector<zc::Own<T>>`** — redundant. Drop to `zc::Vector<T>`.
2. **Returning `zc::ArrayPtr<T>` pointing at a stack array** — dangling.
3. **`zc::String copy = ownedString;`** — `String` is non-copyable. Use `zc::mv`.
4. **`std::string`, `std::vector`, `std::unique_ptr`, `std::optional`** —
   unless wrapped in the explicit "std:: required — zc has no X yet" comment
   and the tracking-issue link per `.agents/rules/cpp-zc.md` § `std::` Usage.
5. **Raw `T*` in a public header or a class member that is clearly owning.**
6. **`const_cast`** — strictly prohibited. No exceptions.
7. **`maybe.orDefault(someDefault)`** silently hides failure paths. Mandate
   `ZC_IF_SOME` and explicit error propagation.
8. **Singletons / globals / function-local static mutable state.**
   Thread through `CompilerSession` / owning fixture instead.

---

## Pimpl Recipe

Every non-trivial public class uses the Pimpl idiom. Copy-and-paste this:

```cpp
// in MyClass.h
class MyClass {
public:
    MyClass();
    ~MyClass();
    MyClass(MyClass&&) noexcept;
    MyClass& operator=(MyClass&&) noexcept;
    ZC_DISALLOW_COPY(MyClass);

    /// \brief Brief description.
    /// \param x Describe x.
    /// \return Describe the return.
    int doSomething(double x);

private:
    struct Impl;
    zc::Own<Impl> impl;
};

// in MyClass.cc
struct MyClass::Impl {
    zc::Vector<int> buf;
    zc::String name;
};

MyClass::MyClass() : impl(zc::heap<Impl>()) {}
MyClass::~MyClass() = default;
MyClass::MyClass(MyClass&&) noexcept = default;
MyClass& MyClass::operator=(MyClass&&) noexcept = default;

int MyClass::doSomething(double x) { /* use impl-> */ }
```

**Rule of thumb:** if `Impl` would fit inside 3 lines and has no
destructor-side effects, you can skip Pimpl for that *specific* class.
Justify the exception in a review comment.

---

## Error / Assert Hygiene

- Use `zc::Exception(zc::str("..."))` for user-facing / recoverable errors.
- Use `ZC_IREQUIRE(cond, "invariant violated: ...")` for impossible paths.
- Use `ZC_LIKELY` / `ZC_UNLIKELY` on hot-path branches (profile first).
- Never `throw 0`, `throw "literal"`, `std::runtime_error`.

---

## When to Escalate

If you encounter a use case that genuinely has no clean `zc` idiom:

1. Do **not** hack around it in-place with raw pointers or `std::`.
2. Document the gap precisely (what you need, why the existing zc types fail).
3. Delegate to the `runtime-memory` subagent to add the missing abstraction
   to `zc/` first, then come back and use it.
4. Update the "Known exceptions" table in `.agents/rules/cpp-zc.md` if a
   temporary `std::` escape hatch is truly unavoidable.
