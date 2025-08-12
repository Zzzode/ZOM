// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Header that should be #included by everyone.
//
// This defines very simple utilities that are widely applicable.

#pragma once

#if defined(__GNUC__) || defined(__clang__)
#define ZC_BEGIN_SYSTEM_HEADER _Pragma("GCC system_header")
#elif defined(_MSC_VER)
#define ZC_BEGIN_SYSTEM_HEADER __pragma(warning(push, 0))
#define ZC_END_SYSTEM_HEADER __pragma(warning(pop))
#endif

#ifndef ZC_BEGIN_SYSTEM_HEADER
#define ZC_BEGIN_SYSTEM_HEADER
#endif

#ifndef ZC_END_SYSTEM_HEADER
#define ZC_END_SYSTEM_HEADER
#endif

#if !defined(ZC_HEADER_WARNINGS) || !ZC_HEADER_WARNINGS
#define ZC_BEGIN_HEADER ZC_BEGIN_SYSTEM_HEADER
#define ZC_END_HEADER ZC_END_SYSTEM_HEADER
#else
#define ZC_BEGIN_HEADER
#define ZC_END_HEADER
#endif

#ifdef __has_cpp_attribute
#define ZC_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#define ZC_HAS_CPP_ATTRIBUTE(x) 0
#endif

#ifdef __has_feature
#define ZC_HAS_COMPILER_FEATURE(x) __has_feature(x)
#else
#define ZC_HAS_COMPILER_FEATURE(x) 0
#endif

ZC_BEGIN_HEADER

#ifndef ZC_NO_COMPILER_CHECK
#if __cplusplus < 202002L && !__CDT_PARSER__
#error "This code requires C++20. Either your compiler does not support it or it is not enabled."
#ifdef __GNUC__
// Compiler claims compatibility with GCC, so presumably supports -std.
#error "Pass -std=c++20 on the compiler command line to enable C++20."
#endif
#endif

#ifdef __GNUC__
#if __clang__
#if __clang_major__ < 14
#warning "This library requires at least Clang 14.0."
#endif
#if __cplusplus >= 202002L && !(__has_include(<coroutine>) || __has_include(<experimental/coroutine>))
#warning \
    "Your compiler supports C++20 but your C++ standard library does not.  If your "\
               "system has libc++ installed (as should be the case on e.g. Mac OSX), try adding "\
               "-stdlib=libc++ to your CXXFLAGS."
#endif
#elif (__GNUC__ < 14) || (__GNUC__ == 14 && __GNUC_MINOR__ < 3)
#warning \
    "This library requires at least GCC 14.3 due to https://gcc.gnu.org/bugzilla/show_bug.cgi?id=102051."
#endif
#elif defined(_MSC_VER)
#if _MSC_VER < 1930 && !defined(__clang__)
#error "You need Visual Studio 2022 or better to compile this code."
#endif
#else
#warning \
    "I don't recognize your compiler. As of this writing, Clang, GCC, and Visual Studio "\
           "are the only known compilers with enough C++20 support for this library. "\
           "#define ZC_NO_COMPILER_CHECK to make this warning go away."
#endif
#endif

#include <stddef.h>
#include <string.h>

#include <cstring>
#include <initializer_list>

#if _WIN32
// Windows likes to define macros for min() and max(). We just can't deal with this.
// If windows.h was included already, undef these.
#undef min
#undef max
// If windows.h was not included yet, define the macro that prevents min() and max() from being
// defined.
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#if _MSC_VER < 1920
#include <intrin.h>  // __popcnt
#else
#include <intrin0.h>  // __popcnt
#endif
#endif

// =======================================================================================

namespace zc {

typedef unsigned int uint;
typedef unsigned char byte;

// =======================================================================================
// Common macros, especially for common yet compiler-specific features.

// Detect whether RTTI and exceptions are enabled, assuming they are unless we have specific
// evidence to the contrary.  Clients can always define ZC_NO_RTTI explicitly to override the
// check. As of version 2, exceptions are required, so this produces an error otherwise.

// TODO: Ideally we'd use __cpp_exceptions/__cpp_rtti not being defined as the first pass since
//   that is the standard compliant way. However, it's unclear how to use those macros (or any
//   others) to distinguish between the compiler supporting feature detection and the feature being
//   disabled vs the compiler not supporting feature detection at all.
#if defined(__has_feature)
#if !defined(ZC_NO_RTTI) && !__has_feature(cxx_rtti)
#define ZC_NO_RTTI 1
#endif
#if !__has_feature(cxx_exceptions)
#error "ZC requires C++ exceptions, please enable them"
#endif
#elif defined(__GNUC__)
#if !defined(ZC_NO_RTTI) && !__GXX_RTTI
#define ZC_NO_RTTI 1
#endif
#if !__EXCEPTIONS
#error "ZC requires C++ exceptions, please enable them"
#endif
#elif defined(_MSC_VER)
#if !defined(ZC_NO_RTTI) && !defined(_CPPRTTI)
#define ZC_NO_RTTI 1
#endif
#if !defined(_CPPUNWIND)
#error "ZC requires C++ exceptions, please enable them"
#endif
#endif

#if !defined(ZC_DEBUG) && !defined(ZC_NDEBUG)
// Heuristically decide whether to enable debug mode.  If DEBUG or NDEBUG is defined, use that.
// Otherwise, fall back to checking whether optimization is enabled.
#if defined(DEBUG) || defined(_DEBUG)
#define ZC_DEBUG
#elif defined(NDEBUG)
#define ZC_NDEBUG
#elif __OPTIMIZE__
#define ZC_NDEBUG
#else
#define ZC_DEBUG
#endif
#endif

#define ZC_DISALLOW_COPY(classname)     \
  classname(const classname&) = delete; \
  classname& operator=(const classname&) = delete
// Deletes the implicit copy constructor and assignment operator. This inhibits the compiler from
// generating the implicit move constructor and assignment operator for this class, but allows the
// code author to supply them, if they make sense to implement.
//
// This macro should not be your first choice. Instead, prefer using ZC_DISALLOW_COPY_AND_MOVE, and
// only use this macro when you have determined that you must implement move semantics for your
// type.

#define ZC_DISALLOW_COPY_AND_MOVE(classname)       \
  classname(const classname&) = delete;            \
  classname& operator=(const classname&) = delete; \
  classname(classname&&) = delete;                 \
  classname& operator=(classname&&) = delete
// Deletes the implicit copy and move constructors and assignment operators. This is useful in cases
// where the code author wants to provide an additional compile-time guard against subsequent
// maintainers casually adding move operations. This is particularly useful when implementing RAII
// classes that are intended to be completely immobile.

#ifdef __GNUC__
#define ZC_LIKELY(condition) __builtin_expect(condition, true)
#define ZC_UNLIKELY(condition) __builtin_expect(condition, false)
// Branch prediction macros.  Evaluates to the condition given, but also tells the compiler that we
// expect the condition to be true/false enough of the time that it's worth hard-coding branch
// prediction.
#else
#define ZC_LIKELY(condition) (condition)
#define ZC_UNLIKELY(condition) (condition)
#endif

#if defined(ZC_DEBUG) || __NO_INLINE__
#define ZC_ALWAYS_INLINE(...) inline __VA_ARGS__
// Don't force inline in debug mode.
#else
#if defined(_MSC_VER) && !defined(__clang__)
#define ZC_ALWAYS_INLINE(...) __forceinline __VA_ARGS__
#else
#define ZC_ALWAYS_INLINE(...) inline __VA_ARGS__ __attribute__((always_inline))
#endif
// Force a function to always be inlined.  Apply only to the prototype, not to the definition.
#endif

#define ZC_NODISCARD [[nodiscard]]

#if defined(_MSC_VER) && !defined(__clang__)
#define ZC_NOINLINE __declspec(noinline)
#else
#define ZC_NOINLINE __attribute__((noinline))
#endif

#if defined(_MSC_VER) && !__clang__
#define ZC_NORETURN(prototype) __declspec(noreturn) prototype
#define ZC_UNUSED
#define ZC_WARN_UNUSED_RESULT
// TODO(msvc): ZC_WARN_UNUSED_RESULT can use _Check_return_ on MSVC, but it's a prefix, so
//   wrapping the whole prototype is needed. http://msdn.microsoft.com/en-us/library/jj159529.aspx
//   Similarly, ZC_UNUSED could use __pragma(warning(suppress:...)), but again that's a prefix.
#else
#define ZC_NORETURN(prototype) prototype __attribute__((noreturn))
#define ZC_UNUSED __attribute__((unused))
#define ZC_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#endif

#if ZC_HAS_CPP_ATTRIBUTE(clang::lifetimebound)
// If this is generating too many false-positives, the user is responsible for disabling the
// problematic warning at the compiler switch level or by suppressing the place where the
// false-positive is reported through compiler-specific pragmas if available.
#define ZC_LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define ZC_LIFETIMEBOUND
#endif
// Annotation that indicates the returned value is referencing a resource owned by this type (e.g.
// cStr() on a std::string). Unfortunately this lifetime can only be superficial currently & cannot
// track further. For example, there's no way to get `array.asPtr().slice(5, 6))` to warn if the
// last slice exceeds the lifetime of `array`. That's because in the general case `ArrayPtr::slice`
// can't have the lifetime bound annotation since it's not wrong to do something like:
//     ArrayPtr<char> doSomething(ArrayPtr<char> foo) {
//        ...
//        return foo.slice(5, 6);
//     }
// If `ArrayPtr::slice` had a lifetime bound then the compiler would warn about this perfectly
// legitimate method. Really there needs to be 2 more annotations. One to inherit the lifetime bound
// and another to inherit the lifetime bound from a parameter (which really could be the same thing
// by allowing a syntax like `[[clang::lifetimebound(*this)]]`.
// https://clang.llvm.org/docs/AttributeReference.html#lifetimebound

#if ZC_HAS_CPP_ATTRIBUTE(clang::musttail)
#define ZC_MUSTTAIL [[clang::musttail]]
#else
#define ZC_MUSTTAIL
#endif
// Annotation for "return" statements to require that they be compiled as tail calls.
// https://clang.llvm.org/docs/AttributeReference.html#musttail

#if __clang__
#define ZC_UNUSED_MEMBER __attribute__((unused))
// Inhibits "unused" warning for member variables.  Only Clang produces such a warning, while GCC
// complains if the attribute is set on members.
#else
#define ZC_UNUSED_MEMBER
#endif

#define ZC_NO_UNIQUE_ADDRESS [[no_unique_address]]

#if ZC_HAS_COMPILER_FEATURE(thread_sanitizer) || defined(__SANITIZE_THREAD__)
#define ZC_DISABLE_TSAN __attribute__((no_sanitize("thread"), noinline))
#else
#define ZC_DISABLE_TSAN
#endif

#if __clang__
#define ZC_DEPRECATED(reason) __attribute__((deprecated(reason)))
#define ZC_UNAVAILABLE(reason) __attribute__((unavailable(reason)))
#elif __GNUC__
#define ZC_DEPRECATED(reason) __attribute__((deprecated))
#define ZC_UNAVAILABLE(reason) = delete
// If the `unavailable` attribute is not supported, just mark the method deleted, which at least
// makes it a compile-time error to try to call it. Note that on Clang, marking a method deleted
// *and* unavailable unfortunately defeats the purpose of the unavailable annotation, as the
// generic "deleted" error is reported instead.
#else
#define ZC_DEPRECATED(reason)
#define ZC_UNAVAILABLE(reason) = delete
// TODO(msvc): Again, here, MSVC prefers a prefix, __declspec(deprecated).
#endif

#if ZC_TESTING_ZC  // defined in ZC's own unit tests; others should not define this
#undef ZC_DEPRECATED
#define ZC_DEPRECATED(reason)
#endif

namespace _ {  // private

ZC_NORETURN(void inlineRequireFailure(const char* file, int line, const char* expectation,
                                      const char* macroArgs, const char* message = nullptr));

ZC_NORETURN(void unreachable());

}  // namespace _

#if _MSC_VER && !defined(__clang__) && (!defined(_MSVC_TRADITIONAL) || _MSVC_TRADITIONAL)
#define ZC_MSVC_TRADITIONAL_CPP 1
#endif

#ifdef ZC_DEBUG
#if ZC_MSVC_TRADITIONAL_CPP
#define ZC_IREQUIRE(condition, ...) \
  if (ZC_LIKELY(condition))         \
    ;                               \
  else                              \
    ::zc::_::inlineRequireFailure(__FILE__, __LINE__, #condition, "" #__VA_ARGS__, __VA_ARGS__)
// Version of ZC_DREQUIRE() which is safe to use in headers that are #included by users.  Used to
// check preconditions inside inline methods.  ZC_IREQUIRE is particularly useful in that
// it will be enabled depending on whether the application is compiled in debug mode rather than
// whether libzc is.
#else
#define ZC_IREQUIRE(condition, ...) \
  if (ZC_LIKELY(condition))         \
    ;                               \
  else                              \
    ::zc::_::inlineRequireFailure(__FILE__, __LINE__, #condition, #__VA_ARGS__, ##__VA_ARGS__)
// Version of ZC_DREQUIRE() which is safe to use in headers that are #included by users.  Used to
// check preconditions inside inline methods.  ZC_IREQUIRE is particularly useful in that
// it will be enabled depending on whether the application is compiled in debug mode rather than
// whether libzc is.
#endif
#else
#define ZC_IREQUIRE(condition, ...)
#endif

#define ZC_IASSERT ZC_IREQUIRE

#define ZC_UNREACHABLE ::zc::_::unreachable();
// Put this on code paths that cannot be reached to suppress compiler warnings about missing
// returns.

#if __clang__
#define ZC_CLANG_KNOWS_THIS_IS_UNREACHABLE_BUT_GCC_DOESNT
#else
#define ZC_CLANG_KNOWS_THIS_IS_UNREACHABLE_BUT_GCC_DOESNT ZC_UNREACHABLE
#endif

#if __clang__
#define ZC_KNOWN_UNREACHABLE(code)                                                              \
  do {                                                                                          \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wunreachable-code\"") \
        code;                                                                                   \
    _Pragma("clang diagnostic pop")                                                             \
  } while (false)
// Suppress "unreachable code" warnings on intentionally unreachable code.
#else
// TODO(someday): Add support for non-clang compilers.
#define ZC_KNOWN_UNREACHABLE(code) \
  do { code; } while (false)
#endif

#if ZC_HAS_CPP_ATTRIBUTE(fallthrough)
#define ZC_FALLTHROUGH [[fallthrough]]
#else
#define ZC_FALLTHROUGH
#endif

// #define ZC_STACK_ARRAY(type, name, size, minStack, maxStack)
//
// Allocate an array, preferably on the stack, unless it is too big.  On GCC this will use
// variable-sized arrays.  For other compilers we could just use a fixed-size array.  `minStack`
// is the stack array size to use if variable-width arrays are not supported.  `maxStack` is the
// maximum stack array size if variable-width arrays *are* supported.
#if __GNUC__ && !__clang__
#define ZC_STACK_ARRAY(type, name, size, minStack, maxStack)                                     \
  size_t name##_size = (size);                                                                   \
  bool name##_isOnStack = name##_size <= (maxStack);                                             \
  type name##_stack[zc::max(1, name##_isOnStack ? name##_size : 0)];                             \
  ::zc::Array<type> name##_heap = name##_isOnStack ? nullptr : zc::heapArray<type>(name##_size); \
  ::zc::ArrayPtr<type> name =                                                                    \
      name##_isOnStack ? zc::arrayPtr(name##_stack, name##_size) : name##_heap
#else
#define ZC_STACK_ARRAY(type, name, size, minStack, maxStack)                                     \
  size_t name##_size = (size);                                                                   \
  bool name##_isOnStack = name##_size <= (minStack);                                             \
  type name##_stack[minStack];                                                                   \
  ::zc::Array<type> name##_heap = name##_isOnStack ? nullptr : zc::heapArray<type>(name##_size); \
  ::zc::ArrayPtr<type> name =                                                                    \
      name##_isOnStack ? zc::arrayPtr(name##_stack, name##_size) : name##_heap
#endif

#define ZC_CONCAT_(x, y) x##y
#define ZC_CONCAT(x, y) ZC_CONCAT_(x, y)
#define ZC_UNIQUE_NAME(prefix) ZC_CONCAT(prefix, __LINE__)
// Create a unique identifier name.  We use concatenate __LINE__ rather than __COUNTER__ so that
// the name can be used multiple times in the same macro.

#if _MSC_VER && !defined(__clang__)

#define ZC_CONSTEXPR(...) __VA_ARGS__
// Use in cases where MSVC barfs on constexpr. A replacement keyword (e.g. "const") can be
// provided, or just leave blank to remove the keyword entirely.
//
// TODO(msvc): Remove this hack once MSVC fully supports constexpr.

#ifndef __restrict__
#define __restrict__ __restrict
// TODO(msvc): Would it be better to define a ZC_RESTRICT macro?
#endif

#pragma warning(disable : 4521 4522)
// This warning complains when there are two copy constructors, one for a const reference and
// one for a non-const reference. It is often quite necessary to do this in wrapper templates,
// therefore this warning is dumb and we disable it.

#pragma warning(disable : 4458)
// Warns when a parameter name shadows a class member. Unfortunately my code does this a lot,
// since I don't use a special name format for members.

#else  // _MSC_VER
#define ZC_CONSTEXPR(...) constexpr
#endif

// =======================================================================================
// Template metaprogramming helpers.

#define ZC_HAS_TRIVIAL_CONSTRUCTOR __is_trivially_constructible
#if __GNUC__ && !__clang__
#define ZC_HAS_NOTHROW_CONSTRUCTOR __has_nothrow_constructor
#define ZC_HAS_TRIVIAL_DESTRUCTOR __has_trivial_destructor
#else
#define ZC_HAS_NOTHROW_CONSTRUCTOR __is_nothrow_constructible
#define ZC_HAS_TRIVIAL_DESTRUCTOR __is_trivially_destructible
#endif

template <typename T>
struct NoInfer_ {
  typedef T Type;
};
template <typename T>
using NoInfer = typename NoInfer_<T>::Type;
// Use NoInfer<T>::Type in place of T for a template function parameter to prevent inference of
// the type based on the parameter value.

template <typename T>
struct RemoveConst_ {
  typedef T Type;
};
template <typename T>
struct RemoveConst_<const T> {
  typedef T Type;
};
template <typename T>
using RemoveConst = typename RemoveConst_<T>::Type;

template <typename>
struct IsLvalueReference_ {
  static constexpr bool value = false;
};
template <typename T>
struct IsLvalueReference_<T&> {
  static constexpr bool value = true;
};
template <typename T>
inline constexpr bool isLvalueReference() {
  return IsLvalueReference_<T>::value;
}

template <typename T>
struct Decay_ {
  typedef T Type;
};
template <typename T>
struct Decay_<T&> {
  typedef typename Decay_<T>::Type Type;
};
template <typename T>
struct Decay_<T&&> {
  typedef typename Decay_<T>::Type Type;
};
template <typename T>
struct Decay_<T[]> {
  typedef typename Decay_<T*>::Type Type;
};
template <typename T>
struct Decay_<const T[]> {
  typedef typename Decay_<const T*>::Type Type;
};
template <typename T, size_t s>
struct Decay_<T[s]> {
  typedef typename Decay_<T*>::Type Type;
};
template <typename T, size_t s>
struct Decay_<const T[s]> {
  typedef typename Decay_<const T*>::Type Type;
};
template <typename T>
struct Decay_<const T> {
  typedef typename Decay_<T>::Type Type;
};
template <typename T>
struct Decay_<volatile T> {
  typedef typename Decay_<T>::Type Type;
};
template <typename T>
using Decay = typename Decay_<T>::Type;

template <bool b>
struct EnableIf_;
template <>
struct EnableIf_<true> {
  typedef void Type;
};
template <bool b>
using EnableIf = typename EnableIf_<b>::Type;
// Use like:
//
//     template <typename T, typename = EnableIf<isValid<T>()>>
//     void func(T&& t);

template <typename...>
struct VoidSfinae_ {
  using Type = void;
};
template <typename... Ts>
using VoidSfinae = typename VoidSfinae_<Ts...>::Type;
// Note: VoidSfinae is std::void_t from C++17.

template <typename T>
T instance() noexcept;
// Like std::declval, but doesn't transform T into an rvalue reference.  If you want that, specify
// instance<T&&>().

struct DisallowConstCopy {
  // Inherit from this, or declare a member variable of this type, to prevent the class from being
  // copyable from a const reference -- instead, it will only be copyable from non-const references.
  // This is useful for enforcing transitive constness of contained pointers.
  //
  // For example, say you have a type T which contains a pointer.  T has non-const methods which
  // modify the value at that pointer, but T's const methods are designed to allow reading only.
  // Unfortunately, if T has a regular copy constructor, someone can simply make a copy of T and
  // then use it to modify the pointed-to value.  However, if T inherits DisallowConstCopy, then
  // callers will only be able to copy non-const instances of T.  Ideally, there is some
  // parallel type ImmutableT which is like a version of T that only has const methods, and can
  // be copied from a const T.
  //
  // Note that due to C++ rules about implicit copy constructors and assignment operators, any
  // type that contains or inherits from a type that disallows const copies will also automatically
  // disallow const copies.  Hey, cool, that's exactly what we want.

#if CAPNP_DEBUG_TYPES
  // Alas! Declaring a defaulted non-const copy constructor tickles a bug which causes GCC and
  // Clang to disagree on ABI, using different calling conventions to pass this type, leading to
  // immediate segfaults. See:
  //     https://bugs.llvm.org/show_bug.cgi?id=23764
  //     https://gcc.gnu.org/bugzilla/show_bug.cgi?id=58074
  //
  // Because of this, we can't use this technique. We guard it by CAPNP_DEBUG_TYPES so that it
  // still applies to the Cap'n Proto developers during internal testing.

  DisallowConstCopy() = default;
  DisallowConstCopy(DisallowConstCopy&) = default;
  DisallowConstCopy(DisallowConstCopy&&) = default;
  DisallowConstCopy& operator=(DisallowConstCopy&) = default;
  DisallowConstCopy& operator=(DisallowConstCopy&&) = default;
#endif
};

#if _MSC_VER && !defined(__clang__)

#define ZC_CPCAP(obj) obj = ::zc::cp(obj)
// TODO(msvc): MSVC refuses to invoke non-const versions of copy constructors in by-value lambda
// captures. Wrap your captured object in this macro to force the compiler to perform a copy.
// Example:
//
//   struct Foo: DisallowConstCopy {};
//   Foo foo;
//   auto lambda = [ZC_CPCAP(foo)] {};

#else

#define ZC_CPCAP(obj) obj
// Clang and gcc both already perform copy capturing correctly with non-const copy constructors.

#endif

template <typename T>
struct DisallowConstCopyIfNotConst : public DisallowConstCopy {
  // Inherit from this when implementing a template that contains a pointer to T and which should
  // enforce transitive constness.  If T is a const type, this has no effect.  Otherwise, it is
  // an alias for DisallowConstCopy.
};

template <typename T>
struct DisallowConstCopyIfNotConst<const T> {};

template <typename T>
struct IsConst_ {
  static constexpr bool value = false;
};
template <typename T>
struct IsConst_<const T> {
  static constexpr bool value = true;
};
template <typename T>
constexpr bool isConst() {
  return IsConst_<T>::value;
}

template <typename T>
struct EnableIfNotConst_ {
  typedef T Type;
};
template <typename T>
struct EnableIfNotConst_<const T>;
template <typename T>
using EnableIfNotConst = typename EnableIfNotConst_<T>::Type;

template <typename T>
struct EnableIfConst_;
template <typename T>
struct EnableIfConst_<const T> {
  typedef T Type;
};
template <typename T>
using EnableIfConst = typename EnableIfConst_<T>::Type;

template <typename T>
struct RemoveConstOrDisable_ {
  struct Type;
};
template <typename T>
struct RemoveConstOrDisable_<const T> {
  typedef T Type;
};
template <typename T>
using RemoveConstOrDisable = typename RemoveConstOrDisable_<T>::Type;

template <typename T>
struct IsReference_ {
  static constexpr bool value = false;
};
template <typename T>
struct IsReference_<T&> {
  static constexpr bool value = true;
};
template <typename T>
constexpr bool isReference() {
  return IsReference_<T>::value;
}

template <typename From, typename To>
struct PropagateConst_ {
  typedef To Type;
};
template <typename From, typename To>
struct PropagateConst_<const From, To> {
  typedef const To Type;
};
template <typename From, typename To>
using PropagateConst = typename PropagateConst_<From, To>::Type;

namespace _ {  // private

template <typename T>
T refIfLvalue(T&&);

}  // namespace _

#define ZC_DECLTYPE_REF(exp) decltype(::zc::_::refIfLvalue(exp))
// Like decltype(exp), but if exp is an lvalue, produces a reference type.
//
//     int i;
//     decltype(i) i1(i);                         // i1 has type int.
//     ZC_DECLTYPE_REF(i + 1) i2(i + 1);          // i2 has type int.
//     ZC_DECLTYPE_REF(i) i3(i);                  // i3 has type int&.
//     ZC_DECLTYPE_REF(zc::mv(i)) i4(zc::mv(i));  // i4 has type int.

template <typename T, typename U>
struct IsSameType_ {
  static constexpr bool value = false;
};
template <typename T>
struct IsSameType_<T, T> {
  static constexpr bool value = true;
};
template <typename T, typename U>
constexpr bool isSameType() {
  return IsSameType_<T, U>::value;
}

template <typename T>
constexpr bool isIntegral() {
  return false;
}
template <>
constexpr bool isIntegral<char>() {
  return true;
}
template <>
constexpr bool isIntegral<signed char>() {
  return true;
}
template <>
constexpr bool isIntegral<short>() {
  return true;
}
template <>
constexpr bool isIntegral<int>() {
  return true;
}
template <>
constexpr bool isIntegral<long>() {
  return true;
}
template <>
constexpr bool isIntegral<long long>() {
  return true;
}
template <>
constexpr bool isIntegral<unsigned char>() {
  return true;
}
template <>
constexpr bool isIntegral<unsigned short>() {
  return true;
}
template <>
constexpr bool isIntegral<unsigned int>() {
  return true;
}
template <>
constexpr bool isIntegral<unsigned long>() {
  return true;
}
template <>
constexpr bool isIntegral<unsigned long long>() {
  return true;
}

template <typename T>
struct CanConvert_ {
  static int sfinae(T);
  static char sfinae(...);
};

template <typename T, typename U>
constexpr bool canConvert() {
  return sizeof(CanConvert_<U>::sfinae(instance<T>())) == sizeof(int);
}

#if __GNUC__ && !__clang__ && __GNUC__ < 5
template <typename T>
constexpr bool canMemcpy() {
  // Returns true if T can be copied using memcpy instead of using the copy constructor or
  // assignment operator.

  // GCC 4 does not have __is_trivially_constructible and friends, and there doesn't seem to be
  // any reliable alternative. __has_trivial_copy() and __has_trivial_assign() return the right
  // thing at one point but later on they changed such that a deleted copy constructor was
  // considered "trivial" (apparently technically correct, though useless). So, on GCC 4 we give up
  // and assume we can't memcpy() at all, and must explicitly copy-construct everything.
  return false;
}
#define ZC_ASSERT_CAN_MEMCPY(T)
#else
template <typename T>
constexpr bool canMemcpy() {
  // Returns true if T can be copied using memcpy instead of using the copy constructor or
  // assignment operator.

  return __is_trivially_constructible(T, const T&) && __is_trivially_assignable(T&, const T&);
}
#define ZC_ASSERT_CAN_MEMCPY(T) \
  static_assert(zc::canMemcpy<T>(), "this code expects this type to be memcpy()-able");
#endif

template <typename T>
class Badge {
  // A pattern for marking individual methods such that they can only be called from a specific
  // caller class: Make the method public but give it a parameter of type `Badge<Caller>`. Only
  // `Caller` can construct one, so only `Caller` can call the method.
  //
  //     // We only allow calls from the class `Bar`.
  //     void foo(Badge<Bar>)
  //
  // The call site looks like:
  //
  //     foo({});
  //
  // This pattern also works well for declaring private constructors, but still being able to use
  // them with `zc::heap()`, etc.
  //
  // Idea from: https://awesomekling.github.io/Serenity-C++-patterns-The-Badge/
  //
  // Note that some forms of this idea make the copy constructor private as well, in order to
  // prohibit `Badge<NotMe>(*(Badge<NotMe>*)nullptr)`. However, that would prevent badges from
  // being passed through forwarding functions like `zc::heap()`, which would ruin one of the main
  // use cases for this pattern in ZC. In any case, dereferencing a null pointer is UB; there are
  // plenty of other ways to get access to private members if you're willing to go UB. For one-off
  // debugging purposes, you might as well use `#define private public` at the top of the file.
private:
  Badge() {}
  friend T;
};

// =======================================================================================
// Equivalents to std::move() and std::forward(), since these are very commonly needed and the
// std header <utility> pulls in lots of other stuff.
//
// We use abbreviated names mv and fwd because these helpers (especially mv) are so commonly used
// that the cost of typing more letters outweighs the cost of being slightly harder to understand
// when first encountered.

template <typename T>
constexpr T&& mv(T& t) noexcept {
  return static_cast<T&&>(t);
}
template <typename T>
constexpr T&& fwd(NoInfer<T>& t) noexcept {
  return static_cast<T&&>(t);
}

template <typename T>
constexpr T cp(T& t) noexcept {
  return t;
}
template <typename T>
constexpr T cp(const T& t) noexcept {
  return t;
}
// Useful to force a copy, particularly to pass into a function that expects T&&.

template <typename T, typename U, bool takeT, bool uOK = true>
struct ChooseType_;
template <typename T, typename U>
struct ChooseType_<T, U, true, true> {
  typedef T Type;
};
template <typename T, typename U>
struct ChooseType_<T, U, true, false> {
  typedef T Type;
};
template <typename T, typename U>
struct ChooseType_<T, U, false, true> {
  typedef U Type;
};

template <typename T, typename U>
using WiderType = typename ChooseType_<T, U, sizeof(T) >= sizeof(U)>::Type;

template <typename T, typename U>
inline constexpr auto min(T&& a, U&& b) -> WiderType<Decay<T>, Decay<U>> {
  return a < b ? WiderType<Decay<T>, Decay<U>>(a) : WiderType<Decay<T>, Decay<U>>(b);
}

template <typename T, typename U>
inline constexpr auto max(T&& a, U&& b) -> WiderType<Decay<T>, Decay<U>> {
  return a > b ? WiderType<Decay<T>, Decay<U>>(a) : WiderType<Decay<T>, Decay<U>>(b);
}

template <typename T, size_t s>
inline constexpr size_t size(T (&arr)[s]) {
  return s;
}
template <typename T>
inline constexpr size_t size(T&& arr) {
  return arr.size();
}
// Returns the size of the parameter, whether the parameter is a regular C array or a container
// with a `.size()` method.

class MaxValue_ {
private:
  template <typename T>
  inline constexpr T maxSigned() const {
    return (1ull << (sizeof(T) * 8 - 1)) - 1;
  }
  template <typename T>
  inline constexpr T maxUnsigned() const {
    return ~static_cast<T>(0u);
  }

public:
#define _kJ_HANDLE_TYPE(T)                                                                \
  inline constexpr operator signed T() const { return MaxValue_::maxSigned<signed T>(); } \
  inline constexpr operator unsigned T() const { return MaxValue_::maxUnsigned<unsigned T>(); }
  _kJ_HANDLE_TYPE(char) _kJ_HANDLE_TYPE(short) _kJ_HANDLE_TYPE(int) _kJ_HANDLE_TYPE(long)
      _kJ_HANDLE_TYPE(long long)
#undef _kJ_HANDLE_TYPE

          inline constexpr
          operator char() const {
    // `char` is different from both `signed char` and `unsigned char`, and may be signed or
    // unsigned on different platforms.  Ugh.
    return char(-1) < 0 ? MaxValue_::maxSigned<char>() : MaxValue_::maxUnsigned<char>();
  }
};

class MinValue_ {
private:
  template <typename T>
  inline constexpr T minSigned() const {
    return 1ull << (sizeof(T) * 8 - 1);
  }
  template <typename T>
  inline constexpr T minUnsigned() const {
    return 0u;
  }

public:
#define _kJ_HANDLE_TYPE(T)                                                                \
  inline constexpr operator signed T() const { return MinValue_::minSigned<signed T>(); } \
  inline constexpr operator unsigned T() const { return MinValue_::minUnsigned<unsigned T>(); }
  _kJ_HANDLE_TYPE(char) _kJ_HANDLE_TYPE(short) _kJ_HANDLE_TYPE(int) _kJ_HANDLE_TYPE(long)
      _kJ_HANDLE_TYPE(long long)
#undef _kJ_HANDLE_TYPE

          inline constexpr
          operator char() const {
    // `char` is different from both `signed char` and `unsigned char`, and may be signed or
    // unsigned on different platforms.  Ugh.
    return char(-1) < 0 ? MinValue_::minSigned<char>() : MinValue_::minUnsigned<char>();
  }
};

static ZC_CONSTEXPR(const) MaxValue_ maxValue = MaxValue_();
// A special constant which, when cast to an integer type, takes on the maximum possible value of
// that type.  This is useful to use as e.g. a parameter to a function because it will be robust
// in the face of changes to the parameter's type.

static ZC_CONSTEXPR(const) MinValue_ minValue = MinValue_();
// A special constant which, when cast to an integer type, takes on the minimum possible value
// of that type.  This is useful to use as e.g. a parameter to a function because it will be robust
// in the face of changes to the parameter's type.

template <typename T>
inline bool operator==(T t, MaxValue_) {
  return t == Decay<T>(maxValue);
}
template <typename T>
inline bool operator==(T t, MinValue_) {
  return t == Decay<T>(minValue);
}

template <uint bits>
inline constexpr unsigned long long maxValueForBits() {
  // Get the maximum integer representable in the given number of bits.

  // 1ull << 64 is unfortunately undefined.
  return (bits == 64 ? 0 : (1ull << bits)) - 1;
}

struct ThrowOverflow {
  // Functor which throws an exception complaining about integer overflow. Usually this is used
  // with the interfaces in units.h, but is defined here because Cap'n Proto wants to avoid
  // including units.h when not using CAPNP_DEBUG_TYPES.
  [[noreturn]] void operator()() const;
};

#if __GNUC__ || __clang__ || _MSC_VER
inline constexpr float inf() { return __builtin_huge_valf(); }
inline constexpr float nan() { return __builtin_nanf(""); }

#else
#error "Not sure how to support your compiler."
#endif

inline constexpr bool isNaN(float f) { return f != f; }
inline constexpr bool isNaN(double f) { return f != f; }

inline int popCount(unsigned int x) {
#if defined(_MSC_VER) && !defined(__clang__)
  return __popcnt(x);
  // Note: __popcnt returns unsigned int, but the value is clearly guaranteed to fit into an int
#else
  return __builtin_popcount(x);
#endif
}

// =======================================================================================
// Useful fake containers

template <typename T>
class Range {
public:
  inline constexpr Range(const T& begin, const T& end) : begin_(begin), end_(end) {}
  inline explicit constexpr Range(const T& end) : begin_(0), end_(end) {}

  class Iterator {
  public:
    Iterator() = default;
    inline Iterator(const T& value) : value(value) {}

    inline const T& operator*() const { return value; }
    inline const T& operator[](size_t index) const { return value + index; }
    inline Iterator& operator++() {
      ++value;
      return *this;
    }
    inline Iterator operator++(int) { return Iterator(value++); }
    inline Iterator& operator--() {
      --value;
      return *this;
    }
    inline Iterator operator--(int) { return Iterator(value--); }
    inline Iterator& operator+=(ptrdiff_t amount) {
      value += amount;
      return *this;
    }
    inline Iterator& operator-=(ptrdiff_t amount) {
      value -= amount;
      return *this;
    }
    inline Iterator operator+(ptrdiff_t amount) const { return Iterator(value + amount); }
    inline Iterator operator-(ptrdiff_t amount) const { return Iterator(value - amount); }
    inline ptrdiff_t operator-(const Iterator& other) const { return value - other.value; }

    inline bool operator==(const Iterator& other) const = default;
    inline bool operator<=(const Iterator& other) const = default;
    inline bool operator>=(const Iterator& other) const = default;
    inline bool operator<(const Iterator& other) const = default;
    inline bool operator>(const Iterator& other) const = default;

  private:
    T value;
  };

  inline Iterator begin() const { return Iterator(begin_); }
  inline Iterator end() const { return Iterator(end_); }

  inline auto size() const -> decltype(instance<T>() - instance<T>()) { return end_ - begin_; }

private:
  T begin_;
  T end_;
};

template <typename T, typename U>
inline constexpr Range<WiderType<Decay<T>, Decay<U>>> range(T begin, U end) {
  return Range<WiderType<Decay<T>, Decay<U>>>(begin, end);
}

template <typename T>
inline constexpr Range<Decay<T>> range(T begin, T end) {
  return Range<Decay<T>>(begin, end);
}
// Returns a fake iterable container containing all values of T from `begin` (inclusive) to `end`
// (exclusive).  Example:
//
//     // Prints 1, 2, 3, 4, 5, 6, 7, 8, 9.
//     for (int i: zc::range(1, 10)) { print(i); }

template <typename T>
inline constexpr Range<Decay<T>> zeroTo(T end) {
  return Range<Decay<T>>(end);
}
// Returns a fake iterable container containing all values of T from zero (inclusive) to `end`
// (exclusive).  Example:
//
//     // Prints 0, 1, 2, 3, 4, 5, 6, 7, 8, 9.
//     for (int i: zc::zeroTo(10)) { print(i); }

template <typename T>
inline constexpr Range<size_t> indices(T&& container) {
  // Shortcut for iterating over the indices of a container:
  //
  //     for (size_t i: zc::indices(myArray)) { handle(myArray[i]); }

  return range<size_t>(0, zc::size(container));
}

template <typename T>
class Repeat {
public:
  inline constexpr Repeat(const T& value, size_t count) : value(value), count(count) {}

  class Iterator {
  public:
    Iterator() = default;
    inline Iterator(const T& value, size_t index) : value(value), index(index) {}

    inline const T& operator*() const { return value; }
    inline const T& operator[](ptrdiff_t index) const { return value; }
    inline Iterator& operator++() {
      ++index;
      return *this;
    }
    inline Iterator operator++(int) { return Iterator(value, index++); }
    inline Iterator& operator--() {
      --index;
      return *this;
    }
    inline Iterator operator--(int) { return Iterator(value, index--); }
    inline Iterator& operator+=(ptrdiff_t amount) {
      index += amount;
      return *this;
    }
    inline Iterator& operator-=(ptrdiff_t amount) {
      index -= amount;
      return *this;
    }
    inline Iterator operator+(ptrdiff_t amount) const { return Iterator(value, index + amount); }
    inline Iterator operator-(ptrdiff_t amount) const { return Iterator(value, index - amount); }
    inline ptrdiff_t operator-(const Iterator& other) const { return index - other.index; }

    inline bool operator==(const Iterator& other) const { return index == other.index; }
    inline bool operator<=(const Iterator& other) const { return index <= other.index; }
    inline bool operator>=(const Iterator& other) const { return index >= other.index; }
    inline bool operator<(const Iterator& other) const { return index < other.index; }
    inline bool operator>(const Iterator& other) const { return index > other.index; }

  private:
    T value;
    size_t index;
  };

  inline Iterator begin() const { return Iterator(value, 0); }
  inline Iterator end() const { return Iterator(value, count); }

  inline size_t size() const { return count; }
  inline const T& operator[](ptrdiff_t) const { return value; }

private:
  T value;
  size_t count;
};

template <typename T>
inline constexpr Repeat<Decay<T>> repeat(T&& value, size_t count) {
  // Returns a fake iterable which contains `count` repeats of `value`.  Useful for e.g. creating
  // a bunch of spaces:  `zc::repeat(' ', indent * 2)`

  return Repeat<Decay<T>>(value, count);
}

template <typename Inner, class Mapping>
class MappedIterator : private Mapping {
  // An iterator that wraps some other iterator and maps the values through a mapping function.
  // The type `Mapping` must define a method `map()` which performs this mapping.

public:
  template <typename... Params>
  MappedIterator(Inner inner, Params&&... params)
      : Mapping(zc::fwd<Params>(params)...), inner(inner) {}

  inline auto operator->() const { return &Mapping::map(*inner); }
  inline decltype(auto) operator*() const { return Mapping::map(*inner); }
  inline decltype(auto) operator[](size_t index) const { return Mapping::map(inner[index]); }
  inline MappedIterator& operator++() {
    ++inner;
    return *this;
  }
  inline MappedIterator operator++(int) { return MappedIterator(inner++, *this); }
  inline MappedIterator& operator--() {
    --inner;
    return *this;
  }
  inline MappedIterator operator--(int) { return MappedIterator(inner--, *this); }
  inline MappedIterator& operator+=(ptrdiff_t amount) {
    inner += amount;
    return *this;
  }
  inline MappedIterator& operator-=(ptrdiff_t amount) {
    inner -= amount;
    return *this;
  }
  inline MappedIterator operator+(ptrdiff_t amount) const {
    return MappedIterator(inner + amount, *this);
  }
  inline MappedIterator operator-(ptrdiff_t amount) const {
    return MappedIterator(inner - amount, *this);
  }
  inline ptrdiff_t operator-(const MappedIterator& other) const { return inner - other.inner; }

  inline bool operator==(const MappedIterator& other) const { return inner == other.inner; }
  inline bool operator<=(const MappedIterator& other) const { return inner <= other.inner; }
  inline bool operator>=(const MappedIterator& other) const { return inner >= other.inner; }
  inline bool operator<(const MappedIterator& other) const { return inner < other.inner; }
  inline bool operator>(const MappedIterator& other) const { return inner > other.inner; }

private:
  Inner inner;
};

template <typename Inner, typename Mapping>
class MappedIterable : private Mapping {
  // An iterable that wraps some other iterable and maps the values through a mapping function.
  // The type `Mapping` must define a method `map()` which performs this mapping.

public:
  template <typename... Params>
  MappedIterable(Inner inner, Params&&... params)
      : Mapping(zc::fwd<Params>(params)...), inner(inner) {}

  typedef Decay<decltype(instance<Inner>().begin())> InnerIterator;
  typedef MappedIterator<InnerIterator, Mapping> Iterator;
  typedef Decay<decltype(instance<const Inner>().begin())> InnerConstIterator;
  typedef MappedIterator<InnerConstIterator, Mapping> ConstIterator;

  inline Iterator begin() { return {inner.begin(), (Mapping&)*this}; }
  inline Iterator end() { return {inner.end(), (Mapping&)*this}; }
  inline ConstIterator begin() const { return {inner.begin(), (const Mapping&)*this}; }
  inline ConstIterator end() const { return {inner.end(), (const Mapping&)*this}; }

private:
  Inner inner;
};

// =======================================================================================
// Manually invoking constructors and destructors
//
// ctor(x, ...) and dtor(x) invoke x's constructor or destructor, respectively.

// We want placement new, but we don't want to #include <new>.  operator new cannot be defined in
// a namespace, and defining it globally conflicts with the definition in <new>.  So we have to
// define a dummy type and an operator new that uses it.  The dummy type is intentionally passed
// as the last parameter so clang and GCC ABI calling conventions for empty struct struct parameters
// are compatible, and there are not segfaults trying to call clang operator new/delete from GCC or
// vice versa.

namespace _ {  // private
struct PlacementNew {};
}  // namespace _
}  // namespace zc

inline void* operator new(size_t, void* __p, zc::_::PlacementNew) noexcept { return __p; }

inline void operator delete(void*, void* __p, zc::_::PlacementNew) noexcept {}

namespace zc {

template <typename T, typename... Params>
inline void ctor(T& location, Params&&... params) {
  new (&location, _::PlacementNew()) T(zc::fwd<Params>(params)...);
}

template <typename T>
inline void dtor(T& location) {
  location.~T();
}

// =======================================================================================
// Maybe
//
// Use in cases where you want to indicate that a value may be null.  Using Maybe<T&> instead of T*
// forces the caller to handle the null case in order to satisfy the compiler, thus reliably
// preventing null pointer dereferences at runtime.
//
// Maybe<T> can be implicitly constructed from T and from zc::none.
// To read the value of a Maybe<T>, do:
//
//    ZC_IF_SOME(value, someFuncReturningMaybe()) {
//      doSomething(value);
//    } else {
//      maybeWasNone();
//    }
//
// ZC_IF_SOME's first parameter is a variable name which will be defined within the following
// block.  The variable will be a reference to the Maybe's value.
//
// Note that Maybe<T&> actually just wraps a pointer, whereas Maybe<T> wraps a T and a boolean
// indicating nullness.

template <typename T>
class Maybe;

namespace _ {  // private

template <typename T>
class NullableValue {
  // Class whose interface behaves much like T*, but actually contains an instance of T and a
  // boolean flag indicating nullness.

public:
  inline NullableValue(NullableValue&& other) : isSet(other.isSet) {
    if (isSet) { ctor(value, zc::mv(other.value)); }
  }
  inline NullableValue(const NullableValue& other) : isSet(other.isSet) {
    if (isSet) { ctor(value, other.value); }
  }
  inline NullableValue(NullableValue& other) : isSet(other.isSet) {
    if (isSet) { ctor(value, other.value); }
  }
  inline ~NullableValue()
#if _MSC_VER && !defined(__clang__)
      // TODO(msvc): MSVC has a hard time with noexcept specifier expressions that are more complex
      //   than `true` or `false`. We had a workaround for VS2015, but VS2017 regressed.
      noexcept(false)
#else
      noexcept(noexcept(instance<T&>().~T()))
#endif
  {
    if (isSet) { dtor(value); }
  }

  inline T& operator*() & { return value; }
  inline const T& operator*() const& { return value; }
  inline T&& operator*() && { return zc::mv(value); }
  inline const T&& operator*() const&& { return zc::mv(value); }
  inline T* operator->() { return &value; }
  inline const T* operator->() const { return &value; }
  inline operator T*() { return isSet ? &value : nullptr; }
  inline operator const T*() const { return isSet ? &value : nullptr; }

  template <typename... Params>
  inline T& emplace(Params&&... params) {
    if (isSet) {
      isSet = false;
      dtor(value);
    }
    ctor(value, zc::fwd<Params>(params)...);
    isSet = true;
    return value;
  }

  inline NullableValue() : isSet(false) {}
  inline NullableValue(T&& t) : isSet(true) { ctor(value, zc::mv(t)); }
  inline NullableValue(T& t) : isSet(true) { ctor(value, t); }
  inline NullableValue(const T& t) : isSet(true) { ctor(value, t); }
  template <typename U>
  inline NullableValue(NullableValue<U>&& other) : isSet(other.isSet) {
    if (isSet) { ctor(value, zc::mv(other.value)); }
  }
  template <typename U>
  inline NullableValue(const NullableValue<U>& other) : isSet(other.isSet) {
    if (isSet) { ctor(value, other.value); }
  }
  template <typename U>
  inline NullableValue(const NullableValue<U&>& other) : isSet(other.isSet) {
    if (isSet) { ctor(value, *other.ptr); }
  }
  inline NullableValue(decltype(nullptr)) : isSet(false) {}

  inline NullableValue& operator=(NullableValue&& other) {
    if (&other != this) {
      // Careful about throwing destructors/constructors here.
      if (isSet) {
        isSet = false;
        dtor(value);
      }
      if (other.isSet) {
        ctor(value, zc::mv(other.value));
        isSet = true;
      }
    }
    return *this;
  }

  inline NullableValue& operator=(NullableValue& other) {
    if (&other != this) {
      // Careful about throwing destructors/constructors here.
      if (isSet) {
        isSet = false;
        dtor(value);
      }
      if (other.isSet) {
        ctor(value, other.value);
        isSet = true;
      }
    }
    return *this;
  }

  inline NullableValue& operator=(const NullableValue& other) {
    if (&other != this) {
      // Careful about throwing destructors/constructors here.
      if (isSet) {
        isSet = false;
        dtor(value);
      }
      if (other.isSet) {
        ctor(value, other.value);
        isSet = true;
      }
    }
    return *this;
  }

  inline NullableValue& operator=(T&& other) {
    emplace(zc::mv(other));
    return *this;
  }
  inline NullableValue& operator=(T& other) {
    emplace(other);
    return *this;
  }
  inline NullableValue& operator=(const T& other) {
    emplace(other);
    return *this;
  }
  template <typename U>
  inline NullableValue& operator=(NullableValue<U>&& other) {
    if (other.isSet) {
      emplace(zc::mv(other.value));
    } else {
      *this = nullptr;
    }
    return *this;
  }
  template <typename U>
  inline NullableValue& operator=(const NullableValue<U>& other) {
    if (other.isSet) {
      emplace(other.value);
    } else {
      *this = nullptr;
    }
    return *this;
  }
  template <typename U>
  inline NullableValue& operator=(const NullableValue<U&>& other) {
    if (other.isSet) {
      emplace(other.value);
    } else {
      *this = nullptr;
    }
    return *this;
  }
  inline NullableValue& operator=(decltype(nullptr)) {
    if (isSet) {
      isSet = false;
      dtor(value);
    }
    return *this;
  }

  inline bool operator==(decltype(nullptr)) const { return !isSet; }

  NullableValue(const T* t) = delete;
  NullableValue& operator=(const T* other) = delete;
  // We used to permit assigning a Maybe<T> directly from a T*, and the assignment would check for
  // nullness. This turned out never to be useful, and sometimes to be dangerous.

private:
  bool isSet;

#if _MSC_VER && !defined(__clang__)
#pragma warning(push)
#pragma warning(disable : 4624)
// Warns that the anonymous union has a deleted destructor when T is non-trivial. This warning
// seems broken.
#endif

  union {
    T value;
  };

#if _MSC_VER && !defined(__clang__)
#pragma warning(pop)
#endif

  friend class zc::Maybe<T>;
  template <typename U>
  friend NullableValue<U>&& readMaybe(Maybe<U>&& maybe);
};

template <typename T>
inline NullableValue<T>&& readMaybe(Maybe<T>&& maybe) {
  return zc::mv(maybe.ptr);
}
template <typename T>
inline T* readMaybe(Maybe<T>& maybe) {
  return maybe.ptr;
}
template <typename T>
inline const T* readMaybe(const Maybe<T>& maybe) {
  return maybe.ptr;
}
template <typename T>
inline T* readMaybe(Maybe<T&>&& maybe) {
  return maybe.ptr;
}
template <typename T>
inline T* readMaybe(const Maybe<T&>& maybe) {
  return maybe.ptr;
}

template <typename T>
inline T* readMaybe(T* ptr) {
  return ptr;
}
// Allow ZC_IF_SOME to work on regular pointers.

#ifndef ZC_DEPRECATE_ZC_IF_MAYBE
#define ZC_DEPRECATE_ZC_IF_MAYBE 1
#endif

#if ZC_DEPRECATE_ZC_IF_MAYBE
[[deprecated("ZC_IF_MAYBE is deprecated and will be removed. Please use ZC_IF_SOME instead.")]]
constexpr int ZC_IF_MAYBE_IS_DEPRECATED = 0;
#define ZC_DEPRECATE_ZC_IF_MAYBE_STMT \
  [[maybe_unused]] int ZC_UNIQUE_NAME(deprecation) = ::zc::_::ZC_IF_MAYBE_IS_DEPRECATED
#else
#define ZC_DEPRECATE_ZC_IF_MAYBE_STMT
#endif
// Before ZC_IF_SOME, we used ZC_IF_MAYBE, which does exactly the same thing as ZC_IF_SOME, but
// provides a guaranteed-non-null pointer to the wrapped object instead of a reference. This has a
// tendency to allow authors to inadvertently use pointers where they mean to use references. For
// example, in `ZC_IF_MAYBE(obj, maybe) { ZC_LOG(INFO, obj); }` would stringify and log the address
// of the object living in `maybe`, rather than the object itself as perhaps intended.
//
// Due to this footgun, we've deprecated ZC_IF_MAYBE in favor of ZC_IF_SOME. Please use ZC_IF_SOME.

#ifndef ZC_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR
#define ZC_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR 1
#endif

#if ZC_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR
#define ZC_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR_ATTR                          \
  [[deprecated(                                                             \
      "Using nullptr as an empty Maybe is deprecated and will be removed. " \
      "Please use zc::none for this purpose instead.")]]
#else
#define ZC_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR_ATTR
#endif
// Originally, we used `nullptr` to mean "an empty Maybe" in comparisons and initializations. For
// example, `maybe == nullptr` would be true if `maybe` contained no value, and `Maybe<T>(nullptr)`
// could be used to explicitly initialize an empty Maybe. This isn't the best design, because
// `nullptr` could itself be a valid constructor parameter for the wrapped type T.
//
// Due to this flaw, we've deprecated using `nullptr` to mean "an empty Maybe" in favor of
// `zc::none`. Please use `zc::none`.

#if __GNUC__ || __clang__
// Both clang and GCC understand the GCC set of pragma directives.
#define ZC_SILENCE_DANGLING_ELSE_BEGIN \
  _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdangling-else\"")
#define ZC_SILENCE_DANGLING_ELSE_END _Pragma("GCC diagnostic pop")
#else  // __GNUC__
// I guess we'll find out if MSVC needs similar warning suppression.
#define ZC_SILENCE_DANGLING_ELSE_BEGIN
#define ZC_SILENCE_DANGLING_ELSE_END
#endif  // __GNUC__

}  // namespace _

#define ZC_IF_MAYBE(name, exp) \
  if (ZC_DEPRECATE_ZC_IF_MAYBE_STMT; auto name = ::zc::_::readMaybe(exp))

#define ZC_IF_SOME(name, exp)                                 \
  ZC_SILENCE_DANGLING_ELSE_BEGIN                              \
  if (auto ZC_UNIQUE_NAME(_##name) = ::zc::_::readMaybe(exp)) \
    if (auto& name = *ZC_UNIQUE_NAME(_##name); false) {       \
    } else                                                    \
      ZC_SILENCE_DANGLING_ELSE_END

struct None {};
static constexpr None none;
// A "none" value solely for use in comparisons with and initializations of Maybes. `zc::none` will
// compare equal to all empty Maybes, and will compare not-equal to all non-empty Maybes. If you
// construct or assign to a Maybe from `zc::none`, the constructed/assigned Maybe will be empty.

template <typename T>
inline Maybe<T> some(T&& t) {
  return Maybe<T>(zc::mv(t));
}
// Helper function to auto-deduce maybe argument.
// Useful when there is a complicated conversion chain to T to avoid spelling types out.

#if __GNUC__ || __clang__
// These two macros provide a friendly syntax to extract the value of a Maybe or return early.
//
// Use ZC_UNWRAP_OR_RETURN if you just want to return a simple value when the Maybe is null:
//
//     int foo(Maybe<int> maybe) {
//       int value = ZC_UNWRAP_OR_RETURN(maybe, -1);
//       // ... use value ...
//     }
//
// For functions returning void, omit the second parameter to ZC_UNWRAP_OR_RETURN:
//
//     void foo(Maybe<int> maybe) {
//       int value = ZC_UNWRAP_OR_RETURN(maybe);
//       // ... use value ...
//     }
//
// Use ZC_UNWRAP_OR if you want to execute a block with multiple statements.
//
//     int foo(Maybe<int> maybe) {
//       int value = ZC_UNWRAP_OR(maybe, {
//         ZC_LOG(ERROR, "problem!!!");
//         return -1;
//       });
//       // ... use value ...
//     }
//
// The block MUST return at the end or you will get a compiler error
//
// Unfortunately, these macros seem impossible to express without using GCC's non-standard
// "statement expressions" extension. IIFEs don't do the trick here because a lambda cannot
// return out of the parent scope. These macros should therefore only be used in projects that
// target GCC or GCC-compatible compilers.
//
// `__GNUC__` is not defined when using LLVM's MSVC-compatible compiler driver `clang-cl` (even
// though clang supports the required extension), hence the additional `|| __clang__`.

#define ZC_UNWRAP_OR_RETURN(value, ...)          \
  (*({                                           \
    auto _zc_result = ::zc::_::readMaybe(value); \
    if (!_zc_result) { return __VA_ARGS__; }     \
    zc::mv(_zc_result);                          \
  }))

#define ZC_UNWRAP_OR(value, block)                             \
  (*({                                                         \
    auto _zc_result = ::zc::_::readMaybe(value);               \
    if (!_zc_result) {                                         \
      block;                                                   \
      asm("ZC_UNWRAP_OR_block_is_missing_return_statement\n"); \
    }                                                          \
    zc::mv(_zc_result);                                        \
  }))
#endif

template <typename T>
class Maybe {
  // A T, or nullptr.

  // IF YOU CHANGE THIS CLASS:  Note that there is a specialization of it in memory.h.

public:
  Maybe() : ptr(nullptr) {}
  Maybe(T&& t) : ptr(zc::mv(t)) {}
  Maybe(T& t) : ptr(t) {}
  Maybe(const T& t) : ptr(t) {}
  Maybe(Maybe&& other) : ptr(zc::mv(other.ptr)) { other = zc::none; }
  Maybe(const Maybe& other) : ptr(other.ptr) {}
  Maybe(Maybe& other) : ptr(other.ptr) {}

  template <typename U>
  Maybe(Maybe<U>&& other) {
    ZC_IF_SOME(val, zc::mv(other)) {
      ptr.emplace(zc::mv(val));
      other = zc::none;
    }
  }
  template <typename U>
  Maybe(Maybe<U&>&& other) {
    ZC_IF_SOME(val, other) {
      ptr.emplace(val);
      other = zc::none;
    }
  }
  template <typename U>
  Maybe(const Maybe<U>& other) {
    ZC_IF_SOME(val, other) { ptr.emplace(val); }
  }

  ZC_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR_ATTR
  Maybe(decltype(nullptr)) : ptr(nullptr) {}

  Maybe(zc::None) : ptr(nullptr) {}

  template <typename... Params>
  inline T& emplace(Params&&... params) {
    // Replace this Maybe's content with a new value constructed by passing the given parameters to
    // T's constructor. This can be used to initialize a Maybe without copying or even moving a T.
    // Returns a reference to the newly-constructed value.

    return ptr.emplace(zc::fwd<Params>(params)...);
  }

  inline Maybe& operator=(T&& other) {
    ptr = zc::mv(other);
    return *this;
  }
  inline Maybe& operator=(T& other) {
    ptr = other;
    return *this;
  }
  inline Maybe& operator=(const T& other) {
    ptr = other;
    return *this;
  }

  inline Maybe& operator=(Maybe&& other) {
    ptr = zc::mv(other.ptr);
    other = zc::none;
    return *this;
  }
  inline Maybe& operator=(Maybe& other) {
    ptr = other.ptr;
    return *this;
  }
  inline Maybe& operator=(const Maybe& other) {
    ptr = other.ptr;
    return *this;
  }

  template <typename U>
  Maybe& operator=(Maybe<U>&& other) {
    ZC_IF_SOME(val, zc::mv(other)) {
      ptr.emplace(zc::mv(val));
      other = zc::none;
    }
    else { ptr = nullptr; }
    return *this;
  }
  template <typename U>
  Maybe& operator=(const Maybe<U>& other) {
    ZC_IF_SOME(val, other) { ptr.emplace(val); }
    else { ptr = nullptr; }
    return *this;
  }

  ZC_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR_ATTR
  inline Maybe& operator=(decltype(nullptr)) {
    ptr = nullptr;
    return *this;
  }

  ZC_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR_ATTR
  inline bool operator==(decltype(nullptr)) const { return ptr == nullptr; }

  inline Maybe& operator=(zc::None) {
    ptr = nullptr;
    return *this;
  }
  inline bool operator==(zc::None) const { return ptr == nullptr; }

  inline bool operator==(const Maybe<T>& other) const {
    if (ptr == nullptr) {
      return other == zc::none;
    } else {
      return other.ptr != nullptr && *ptr == *other.ptr;
    }
  }

  Maybe(const T* t) = delete;
  Maybe& operator=(const T* other) = delete;
  // We used to permit assigning a Maybe<T> directly from a T*, and the assignment would check for
  // nullness. This turned out never to be useful, and sometimes to be dangerous.

  T& orDefault(T& defaultValue) & {
    if (ptr == nullptr) {
      return defaultValue;
    } else {
      return *ptr;
    }
  }
  const T& orDefault(const T& defaultValue) const& {
    if (ptr == nullptr) {
      return defaultValue;
    } else {
      return *ptr;
    }
  }
  T&& orDefault(T&& defaultValue) && {
    if (ptr == nullptr) {
      return zc::mv(defaultValue);
    } else {
      return zc::mv(*ptr);
    }
  }
  const T&& orDefault(const T&& defaultValue) const&& {
    if (ptr == nullptr) {
      return zc::mv(defaultValue);
    } else {
      return zc::mv(*ptr);
    }
  }

  template <typename F,
            typename Result = decltype(instance<bool>() ? instance<T&>() : instance<F>()())>
  Result orDefault(F&& lazyDefaultValue) & {
    if (ptr == nullptr) {
      return lazyDefaultValue();
    } else {
      return *ptr;
    }
  }

  template <typename F,
            typename Result = decltype(instance<bool>() ? instance<const T&>() : instance<F>()())>
  Result orDefault(F&& lazyDefaultValue) const& {
    if (ptr == nullptr) {
      return lazyDefaultValue();
    } else {
      return *ptr;
    }
  }

  template <typename F,
            typename Result = decltype(instance<bool>() ? instance<T&&>() : instance<F>()())>
  Result orDefault(F&& lazyDefaultValue) && {
    if (ptr == nullptr) {
      return lazyDefaultValue();
    } else {
      return zc::mv(*ptr);
    }
  }

  template <typename F,
            typename Result = decltype(instance<bool>() ? instance<const T&&>() : instance<F>()())>
  Result orDefault(F&& lazyDefaultValue) const&& {
    if (ptr == nullptr) {
      return lazyDefaultValue();
    } else {
      return zc::mv(*ptr);
    }
  }

  template <typename Func>
  auto map(Func&& f) & -> Maybe<decltype(f(instance<T&>()))> {
    if (ptr == nullptr) {
      return zc::none;
    } else {
      return f(*ptr);
    }
  }

  template <typename Func>
  auto map(Func&& f) const& -> Maybe<decltype(f(instance<const T&>()))> {
    if (ptr == nullptr) {
      return zc::none;
    } else {
      return f(*ptr);
    }
  }

  template <typename Func>
  auto map(Func&& f) && -> Maybe<decltype(f(instance<T&&>()))> {
    if (ptr == nullptr) {
      return zc::none;
    } else {
      return f(zc::mv(*ptr));
    }
  }

  template <typename Func>
  auto map(Func&& f) const&& -> Maybe<decltype(f(instance<const T&&>()))> {
    if (ptr == nullptr) {
      return zc::none;
    } else {
      return f(zc::mv(*ptr));
    }
  }

private:
  _::NullableValue<T> ptr;

  template <typename U>
  friend class Maybe;
  template <typename U>
  friend _::NullableValue<U>&& _::readMaybe(Maybe<U>&& maybe);
  template <typename U>
  friend U* _::readMaybe(Maybe<U>& maybe);
  template <typename U>
  friend const U* _::readMaybe(const Maybe<U>& maybe);
};

template <typename T>
class Maybe<T&> {
public:
  constexpr Maybe() : ptr(nullptr) {}
  constexpr Maybe(T& t) : ptr(&t) {}
  constexpr Maybe(T* t) : ptr(t) {}

  inline constexpr Maybe(PropagateConst<T, Maybe>& other) : ptr(other.ptr) {}
  // Allow const copy only if `T` itself is const. Otherwise allow only non-const copy, to
  // protect transitive constness. Clang is happy for this constructor to be declared `= default`
  // since, after evaluation of `PropagateConst`, it does end up being a default-able constructor.
  // But, GCC and MSVC both complain about that, claiming this constructor cannot be declared
  // default. I don't know who is correct, but whatever, we'll write out an implementation, fine.
  //
  // Note that we can't solve this by inheriting DisallowConstCopyIfNotConst<T> because we want
  // to override the move constructor, and if we override the move constructor then we must define
  // the copy constructor here.

  inline constexpr Maybe(Maybe&& other) : ptr(other.ptr) { other.ptr = nullptr; }

  template <typename U>
  inline constexpr Maybe(Maybe<U&>& other) : ptr(other.ptr) {}
  template <typename U>
  inline constexpr Maybe(const Maybe<U&>& other) : ptr(const_cast<const U*>(other.ptr)) {}
  template <typename U>
  inline constexpr Maybe(Maybe<U&>&& other) : ptr(other.ptr) {
    other.ptr = nullptr;
  }
  template <typename U>
  inline constexpr Maybe(const Maybe<U&>&& other) = delete;
  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  constexpr Maybe(Maybe<U>& other) : ptr(other.ptr.operator U*()) {}
  template <typename U, typename = EnableIf<canConvert<const U*, T*>()>>
  constexpr Maybe(const Maybe<U>& other) : ptr(other.ptr.operator const U*()) {}

  ZC_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR_ATTR
  inline constexpr Maybe(decltype(nullptr)) : ptr(nullptr) {}

  inline constexpr Maybe(zc::None) : ptr(nullptr) {}

  ZC_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR_ATTR
  inline Maybe& operator=(decltype(nullptr)) {
    ptr = nullptr;
    return *this;
  }

  inline Maybe& operator=(T& other) {
    ptr = &other;
    return *this;
  }
  inline Maybe& operator=(T* other) {
    ptr = other;
    return *this;
  }
  inline Maybe& operator=(PropagateConst<T, Maybe>& other) {
    ptr = other.ptr;
    return *this;
  }
  inline Maybe& operator=(Maybe&& other) {
    ptr = other.ptr;
    other.ptr = nullptr;
    return *this;
  }
  template <typename U>
  inline Maybe& operator=(Maybe<U&>& other) {
    ptr = other.ptr;
    return *this;
  }
  template <typename U>
  inline Maybe& operator=(const Maybe<const U&>& other) {
    ptr = other.ptr;
    return *this;
  }
  template <typename U>
  inline Maybe& operator=(Maybe<U&>&& other) {
    ptr = other.ptr;
    other.ptr = nullptr;
    return *this;
  }
  template <typename U>
  inline Maybe& operator=(const Maybe<U&>&& other) = delete;

  ZC_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR_ATTR
  inline bool operator==(decltype(nullptr)) const { return ptr == nullptr; }

  inline bool operator==(zc::None) const { return ptr == nullptr; }

  T& orDefault(T& defaultValue) {
    if (ptr == nullptr) {
      return defaultValue;
    } else {
      return *ptr;
    }
  }
  const T& orDefault(const T& defaultValue) const {
    if (ptr == nullptr) {
      return defaultValue;
    } else {
      return *ptr;
    }
  }

  template <typename Func>
  auto map(Func&& f) -> Maybe<decltype(f(instance<T&>()))> {
    if (ptr == nullptr) {
      return zc::none;
    } else {
      return f(*ptr);
    }
  }

  template <typename Func>
  auto map(Func&& f) const -> Maybe<decltype(f(instance<const T&>()))> {
    if (ptr == nullptr) {
      return zc::none;
    } else {
      const T& ref = *ptr;
      return f(ref);
    }
  }

private:
  T* ptr;

  template <typename U>
  friend class Maybe;
  template <typename U>
  friend U* _::readMaybe(Maybe<U&>&& maybe);
  template <typename U>
  friend U* _::readMaybe(const Maybe<U&>& maybe);
};

// =======================================================================================
// ArrayPtr
//
// So common that we put it in common.h rather than array.h.

template <typename T>
class Array;

template <typename T>
class ArrayPtr : public DisallowConstCopyIfNotConst<T> {
  // A pointer to an array.  Includes a size.  Like any pointer, it doesn't own the target data,
  // and passing by value only copies the pointer, not the target.

public:
  inline constexpr ArrayPtr() : ptr(nullptr), size_(0) {}
  inline constexpr ArrayPtr(decltype(nullptr)) : ptr(nullptr), size_(0) {}
  inline constexpr ArrayPtr(T* ptr ZC_LIFETIMEBOUND, size_t size) : ptr(ptr), size_(size) {}
  inline constexpr ArrayPtr(T* begin ZC_LIFETIMEBOUND, T* end ZC_LIFETIMEBOUND)
      : ptr(begin), size_(end - begin) {}
  ArrayPtr<T>& operator=(Array<T>&&) = delete;
  ArrayPtr<T>& operator=(decltype(nullptr)) {
    ptr = nullptr;
    size_ = 0;
    return *this;
  }

#if __GNUC__ && !__clang__ && __GNUC__ >= 9
// GCC 9 added a warning when we take an initializer_list as a constructor parameter and save a
// pointer to its content in a class member. GCC apparently imagines we're going to do something
// dumb like this:
//     ArrayPtr<const int> ptr = { 1, 2, 3 };
//     foo(ptr[1]); // undefined behavior!
// Any ZC programmer should be able to recognize that this is UB, because an ArrayPtr does not own
// its content. That's not what this constructor is for, though. This constructor is meant to allow
// code like this:
//     int foo(ArrayPtr<const int> p);
//     // ... later ...
//     foo({1, 2, 3});
// In this case, the initializer_list's backing array, like any temporary, lives until the end of
// the statement `foo({1, 2, 3});`. Therefore, it lives at least until the call to foo() has
// returned, which is exactly what we care about. This usage is fine! GCC is wrong to warn.
//
// Amusingly, Clang's implementation has a similar type that they call ArrayRef which apparently
// triggers this same GCC warning. My guess is that Clang will not introduce a similar warning
// given that it triggers on their own, legitimate code.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winit-list-lifetime"
#endif
  inline ZC_CONSTEXPR()
      ArrayPtr(::std::initializer_list<RemoveConstOrDisable<T>> init ZC_LIFETIMEBOUND)
      : ptr(init.begin()), size_(init.size()) {}
#if __GNUC__ && !__clang__ && __GNUC__ >= 9
#pragma GCC diagnostic pop
#endif

  template <size_t size>
  inline constexpr ArrayPtr(ZC_LIFETIMEBOUND T (&native)[size]) : ptr(native), size_(size) {
    // Construct an ArrayPtr from a native C-style array.
    //
    // We disable this constructor for const char arrays because otherwise you would be able to
    // implicitly convert a character literal to ArrayPtr<const char>, which sounds really great,
    // except that the NUL terminator would be included, which probably isn't what you intended.
    //
    // TODO(someday): Maybe we should support character literals but explicitly chop off the NUL
    //   terminator. This could do the wrong thing if someone tries to construct an
    //   ArrayPtr<const char> from a non-NUL-terminated char array, but evidence suggests that all
    //   real use cases are in fact intending to remove the NUL terminator. It's convenient to be
    //   able to specify ArrayPtr<const char> as a parameter type and be able to accept strings
    //   as input in addition to arrays. Currently, you'll need overloading to support string
    //   literals in this case, but if you overload StringPtr, then you'll find that several
    //   conversions (e.g. from String and from a literal char array) become ambiguous! You end up
    //   having to overload for literal char arrays specifically which is cumbersome.

    static_assert(
        !isSameType<T, const char>(),
        "Can't implicitly convert literal char array to ArrayPtr because we don't know if "
        "you meant to include the NUL terminator. We may change this in the future to "
        "automatically drop the NUL terminator. For now, try explicitly converting to StringPtr, "
        "which can in turn implicitly convert to ArrayPtr<const char>.");
    static_assert(!isSameType<T, const char16_t>(), "see above");
    static_assert(!isSameType<T, const char32_t>(), "see above");
  }

  inline operator ArrayPtr<const T>() const { return ArrayPtr<const T>(ptr, size_); }
  inline ArrayPtr<const T> asConst() const { return ArrayPtr<const T>(ptr, size_); }

  inline constexpr size_t size() const { return size_; }
  inline constexpr const T& operator[](size_t index) const {
    ZC_IREQUIRE(index < size_, "Out-of-bounds ArrayPtr access.");
    return ptr[index];
  }
  inline T& operator[](size_t index) {
    ZC_IREQUIRE(index < size_, "Out-of-bounds ArrayPtr access.");
    return ptr[index];
  }

  inline constexpr T* begin() { return ptr; }
  inline constexpr T* end() { return ptr + size_; }
  inline constexpr T& front() { return *ptr; }
  inline constexpr T& back() { return *(ptr + size_ - 1); }
  inline constexpr const T* begin() const { return ptr; }
  inline constexpr const T* end() const { return ptr + size_; }
  inline constexpr const T& front() const { return *ptr; }
  inline constexpr const T& back() const { return *(ptr + size_ - 1); }

  inline constexpr ArrayPtr<const T> slice(size_t start, size_t end) const {
    ZC_IREQUIRE(start <= end && end <= size_, "Out-of-bounds ArrayPtr::slice().");
    return ArrayPtr<const T>(ptr + start, end - start);
  }
  inline constexpr ArrayPtr slice(size_t start, size_t end) {
    ZC_IREQUIRE(start <= end && end <= size_, "Out-of-bounds ArrayPtr::slice().");
    return ArrayPtr(ptr + start, end - start);
  }
  inline constexpr ArrayPtr<const T> slice(size_t start) const {
    ZC_IREQUIRE(start <= size_, "Out-of-bounds ArrayPtr::slice().");
    return ArrayPtr<const T>(ptr + start, size_ - start);
  }
  inline constexpr ArrayPtr slice(size_t start) {
    ZC_IREQUIRE(start <= size_, "Out-of-bounds ArrayPtr::slice().");
    return ArrayPtr(ptr + start, size_ - start);
  }
  inline constexpr bool startsWith(const ArrayPtr<const T>& other) const {
    return other.size() <= size_ && slice(0, other.size()) == other;
  }
  inline constexpr bool endsWith(const ArrayPtr<const T>& other) const {
    return other.size() <= size_ && slice(size_ - other.size(), size_) == other;
  }

  inline constexpr ArrayPtr first(size_t count) { return slice(0, count); }
  inline constexpr ArrayPtr<const T> first(size_t count) const { return slice(0, count); }

  inline Maybe<size_t> findFirst(const T& match) const {
    for (size_t i = 0; i < size_; i++) {
      if (ptr[i] == match) { return i; }
    }
    return zc::none;
  }
  inline Maybe<size_t> findLast(const T& match) const {
    for (size_t i = size_; i--;) {
      if (ptr[i] == match) { return i; }
    }
    return zc::none;
  }

  constexpr ArrayPtr<PropagateConst<T, byte>> asBytes() const {
    // Reinterpret the array as a byte array. This is explicitly legal under C++ aliasing
    // rules.
    ZC_ASSERT_CAN_MEMCPY(RemoveConst<T>);
    return {reinterpret_cast<PropagateConst<T, byte>*>(ptr), size_ * sizeof(T)};
  }
  inline ArrayPtr<PropagateConst<T, char>> asChars() const {
    // Reinterpret the array as a char array. This is explicitly legal under C++ aliasing
    // rules.
    ZC_ASSERT_CAN_MEMCPY(RemoveConst<T>);
    return {reinterpret_cast<PropagateConst<T, char>*>(ptr), size_ * sizeof(T)};
  }

  inline constexpr bool operator==(decltype(nullptr)) const { return size_ == 0; }

  inline constexpr bool operator==(const ArrayPtr& other) const {
    if (size_ != other.size_) return false;
#if ZC_HAS_COMPILER_FEATURE(cxx_constexpr_string_builtins)
    if (isIntegral<RemoveConst<T>>()) {
      if (size_ == 0) return true;
      return __builtin_memcmp(ptr, other.ptr, size_ * sizeof(T)) == 0;
    }
#endif
    for (size_t i = 0; i < size_; i++) {
      if (ptr[i] != other[i]) return false;
    }
    return true;
  }

  template <typename U>
  inline constexpr bool operator==(const ArrayPtr<U>& other) const {
    if (size_ != other.size()) return false;
    for (size_t i = 0; i < size_; i++) {
      if (ptr[i] != other[i]) return false;
    }
    return true;
  }

  inline constexpr bool operator<(const ArrayPtr& other) const {
    size_t comparisonSize = zc::min(size_, other.size_);
    if constexpr (isSameType<RemoveConst<T>, char>() ||
                  isSameType<RemoveConst<T>, unsigned char>()) {
#if ZC_HAS_COMPILER_FEATURE(cxx_constexpr_string_builtins)
      int ret = __builtin_memcmp(ptr, other.ptr, comparisonSize * sizeof(T));
      if (ret != 0) { return ret < 0; }
#else
      for (size_t i = 0; i < comparisonSize; ++i) {
        if (static_cast<unsigned char>(ptr[i]) != static_cast<unsigned char>(other.ptr[i])) {
          return static_cast<unsigned char>(ptr[i]) < static_cast<unsigned char>(other.ptr[i]);
        }
      }
#endif
    } else {
      for (size_t i = 0; i < comparisonSize; i++) {
        bool ret = ptr[i] == other.ptr[i];
        if (!ret) { return ptr[i] < other.ptr[i]; }
      }
    }
    // arrays are equal up to comparisonSize
    return size_ < other.size_;
  }

  inline constexpr bool operator<=(const ArrayPtr& other) const { return !(other < *this); }
  inline constexpr bool operator>=(const ArrayPtr& other) const { return other <= *this; }
  // Note that only strongly ordered types are currently supported
  inline constexpr bool operator>(const ArrayPtr& other) const { return other < *this; }

  template <typename... Attachments>
  Array<T> attach(Attachments&&... attachments) const ZC_WARN_UNUSED_RESULT;
  // Like Array<T>::attach(), but also promotes an ArrayPtr to an Array. Generally the attachment
  // should be an object that actually owns the array that the ArrayPtr is pointing at.
  //
  // You must include zc/array.h to call this.

  template <typename U>
  inline auto as() {
    return U::from(this);
  }
  // Syntax sugar for invoking U::from.
  // Used to chain conversion calls rather than wrap with function.

  template <typename U>
  inline auto as() const {
    return U::from(this);
  }
  // Syntax sugar for invoking U::from.
  // Used to chain conversion calls rather than wrap with function.

  inline void fill(T t) {
    // Fill the area by copying t over every element.

    for (size_t i = 0; i < size_; i++) { ptr[i] = t; }
    // All modern compilers are smart enough to optimize this loop for simple T's.
    // libc++ std::fill doesn't have memset specialization either.
  }

  inline void fill(ArrayPtr<const T> other) {
    // Fill the area by copying each element of other, in sequence, over every element.
    const size_t otherSize = other.size();
    ZC_IREQUIRE(otherSize > 0, "fill requires non-empty source array");
    T* __restrict__ dst = begin();
    const T* __restrict__ src = other.begin();
    size_t counter = 0;
    for (size_t s = size_, i = 0; i < s; i++) {
      dst[i] = src[counter];
      if (++counter == otherSize) counter = 0;
    }
  }

  inline void copyFrom(zc::ArrayPtr<const T> other) {
    // Copy data from the other array pointer.
    // Arrays have to be of the same size and memory area MUST NOT overlap.
    ZC_IREQUIRE(size_ == other.size(), "copy requires arrays of the same size");
    ZC_IREQUIRE(!intersects(other), "copy memory area must not overlap");
    T* __restrict__ dst = begin();
    const T* __restrict__ src = other.begin();
    for (size_t s = size_, i = 0; i < s; i++) { dst[i] = src[i]; }
  }

private:
  T* ptr;
  size_t size_;

  inline bool intersects(zc::ArrayPtr<const T> other) const {
    // Checks if memory area intersects with another pointer.

    // Memory _does not_ intersect if one of the arrays is completely on one side of the other:
    //    begin() >= other.end() || other.begin() >= end()
    // Negating the expression gets the result:
    return begin() < other.end() && other.begin() < end();
  }
};

template <>
inline Maybe<size_t> ArrayPtr<const char>::findFirst(const char& c) const {
  const char* pos = reinterpret_cast<const char*>(memchr(ptr, c, size_));
  if (pos == nullptr) {
    return zc::none;
  } else {
    return pos - ptr;
  }
}

template <>
inline Maybe<size_t> ArrayPtr<char>::findFirst(const char& c) const {
  char* pos = reinterpret_cast<char*>(memchr(ptr, c, size_));
  if (pos == nullptr) {
    return zc::none;
  } else {
    return pos - ptr;
  }
}

template <>
inline Maybe<size_t> ArrayPtr<const byte>::findFirst(const byte& c) const {
  const byte* pos = reinterpret_cast<const byte*>(memchr(ptr, c, size_));
  if (pos == nullptr) {
    return zc::none;
  } else {
    return pos - ptr;
  }
}

template <>
inline Maybe<size_t> ArrayPtr<byte>::findFirst(const byte& c) const {
  byte* pos = reinterpret_cast<byte*>(memchr(ptr, c, size_));
  if (pos == nullptr) {
    return zc::none;
  } else {
    return pos - ptr;
  }
}

// glibc has a memrchr() for reverse search but it's non-standard, so we don't bother optimizing
// findLast(), which isn't used much anyway.

template <typename T>
inline constexpr ArrayPtr<T> arrayPtr(T* ptr ZC_LIFETIMEBOUND, size_t size) {
  // Use this function to construct ArrayPtrs without writing out the type name.
  return ArrayPtr<T>(ptr, size);
}

template <typename T>
inline constexpr ArrayPtr<T> arrayPtr(T* begin ZC_LIFETIMEBOUND, T* end ZC_LIFETIMEBOUND) {
  // Use this function to construct ArrayPtrs without writing out the type name.
  return ArrayPtr<T>(begin, end);
}

template <typename T>
inline constexpr ArrayPtr<T> arrayPtr(T& t ZC_LIFETIMEBOUND) {
  // Construct ArrayPtr pointing to a single object instance.
  return arrayPtr(&t, 1);
}

template <typename T, size_t s>
inline constexpr ArrayPtr<T> arrayPtr(T (&arr)[s]) {
  // Use this function to construct ArrayPtrs without writing out the type name.
  return ArrayPtr<T>(arr);
}

template <typename... Params>
auto asBytes(Params&&... params) {
  return zc::arrayPtr(zc::fwd<Params>(params)...).asBytes();
}

// =======================================================================================
// Casts

template <typename To, typename From>
To implicitCast(From&& from) {
  // `implicitCast<T>(value)` casts `value` to type `T` only if the conversion is implicit.  Useful
  // for e.g. resolving ambiguous overloads without sacrificing type-safety.
  return zc::fwd<From>(from);
}

template <typename To, typename From>
Maybe<To&> dynamicDowncastIfAvailable(From& from) {
  // If RTTI is disabled, always returns zc::none.  Otherwise, works like dynamic_cast.  Useful
  // in situations where dynamic_cast could allow an optimization, but isn't strictly necessary
  // for correctness.  It is highly recommended that you try to arrange all your dynamic_casts
  // this way, as a dynamic_cast that is necessary for correctness implies a flaw in the interface
  // design.

  // Force a compile error if To is not a subtype of From.  Cross-casting is rare; if it is needed
  // we should have a separate cast function like dynamicCrosscastIfAvailable().
  if (false) { zc::implicitCast<From*>(zc::implicitCast<To*>(nullptr)); }

#if ZC_NO_RTTI
  return zc::none;
#else
  return dynamic_cast<To*>(&from);
#endif
}

template <typename To, typename From>
To& downcast(From& from) {
  // Down-cast a value to a sub-type, asserting that the cast is valid.  In opt mode this is a
  // static_cast, but in debug mode (when RTTI is enabled) a dynamic_cast will be used to verify
  // that the value really has the requested type.

  // Force a compile error if To is not a subtype of From.
  if (false) { zc::implicitCast<From*>(zc::implicitCast<To*>(nullptr)); }

#if !ZC_NO_RTTI
  ZC_IREQUIRE(dynamic_cast<To*>(&from) != nullptr, "Value cannot be downcast() to requested type.");
#endif

  return static_cast<To&>(from);
}

// =======================================================================================
// Defer

namespace _ {  // private

template <typename Func>
class Deferred {
public:
  Deferred(Func&& func) : maybeFunc(zc::fwd<Func>(func)) {}
  ~Deferred() noexcept(false) { run(); }
  ZC_DISALLOW_COPY(Deferred);

  Deferred(Deferred&&) = default;
  // Since we use a zc::Maybe, the default move constructor does exactly what we want it to do.

  void run() {
    // Move `maybeFunc` to the local scope so that even if we throw, we destroy the functor we had.
    auto maybeLocalFunc = zc::mv(maybeFunc);
    ZC_IF_SOME(func, maybeLocalFunc) { func(); }
  }

  void cancel() { maybeFunc = zc::none; }

private:
  zc::Maybe<Func> maybeFunc;
  // Note that `Func` may actually be an lvalue reference because `zc::defer` takes its argument via
  // universal reference. `zc::Maybe` has specializations for lvalue reference types, so this works
  // out.
};

}  // namespace _

template <typename Func>
_::Deferred<Func> defer(Func&& func) {
  // Returns an object which will invoke the given functor in its destructor. The object is not
  // copyable but is move-constructable with the semantics you'd expect. Since the return type is
  // private, you need to assign to an `auto` variable.
  //
  // The ZC_DEFER macro provides slightly more convenient syntax for the common case where you
  // want some code to run at current scope exit.
  //
  // ZC_DEFER does not support move-assignment for its returned objects. If you need to reuse the
  // variable for your deferred function object, then you will want to write your own class for that
  // purpose.

  return _::Deferred<Func>(zc::fwd<Func>(func));
}

#define ZC_DEFER(code) auto ZC_UNIQUE_NAME(_zcDefer) = ::zc::defer([&]() { code; })
// Run the given code when the function exits, whether by return or exception.

// =======================================================================================
// IsDisallowedInCoroutine

namespace _ {

template <typename T, typename = void>
struct IsDisallowedInCoroutine {
  static constexpr bool value = false;
};

template <typename T>
struct IsDisallowedInCoroutine<T, typename Decay<T>::_zc_DissalowedInCoroutine> {
  static constexpr bool value = true;
};

template <typename T>
struct IsDisallowedInCoroutine<T*, typename T::_zc_DissalowedInCoroutine> {
  static constexpr bool value = true;
};

template <typename T>
constexpr bool isDisallowedInCoroutine() {
  return IsDisallowedInCoroutine<T>::value;
}

}  // namespace _

#define ZC_DISALLOW_AS_COROUTINE_PARAM                    \
  using _zc_DissalowedInCoroutine = void;                 \
  template <typename T>                                   \
  friend constexpr bool zc::_::isDisallowedInCoroutine(); \
  template <typename T, typename>                         \
  friend struct zc::_::IsDisallowedInCoroutine
// Place in the body of a class or struct to indicate that an instance of or reference to this
// type cannot be passed as the parameter to a ZC coroutine. This makes sense, for example, for
// mutex locks, or other types which should never be held across a co_await.
//
// (Types annotated with this likely also should not be used as local variables inside coroutines,
// but there is no way for us to enforce that.)
//
// struct Foo {
//    ZC_DISALLOW_AS_COROUTINE_PARAM;
// }
//

template <typename T, typename = EnableIf<ZC_HAS_TRIVIAL_CONSTRUCTOR(T)>>
inline void memzero(T& t) {
  // Zero-initialize memory region belonging to t. Type-safe wrapper around `memset`.
  memset(&t, 0, sizeof(T));
}

namespace _ {
template <size_t N>
struct ByteLiteral {
  // Helper class to implement _zcb suffix: constexpr is not allowed to cast `char*`
  // to `unsigned char*`. To overcome this limitation every char needs to be cast individually
  // (which is apparently ok for constexpr).
  // This class may not contain any private members, or else you get a
  // misleading compiler error.

  constexpr ByteLiteral(const char (&init)[N]) {
    for (auto i = 0; i < N - 1; ++i) data[i] = (zc::byte)(init[i]);
  }
  constexpr size_t size() const { return N - 1; }
  constexpr const zc::byte* begin() const { return data; }
  zc::byte data[N - 1];  // do not store 0-terminator
};

template <>
struct ByteLiteral<1ul> {
  // Empty string specialization to avoid `data` array of 0 size.
  constexpr ByteLiteral(const char (&init)[1]) {}
  constexpr size_t size() const { return 0; }
  constexpr const zc::byte* begin() const { return nullptr; }
};

}  // namespace _

class ThreadId {
  // Unique thread identifier.
  // Implemented as thread_local address, so could be efficient even for production
  // environments especially with LTO.

public:
  static ThreadId current();
  // Obtain current thread id

  inline bool operator==(const ThreadId& other) const = default;

  void assertCurrentThread() const;
  // ZC_ASSERTs that current thread matches this identifier.

private:
  inline ThreadId(void* id) : id(id) {}
  void* id;
};

}  // namespace zc

template <zc::_::ByteLiteral s>
constexpr zc::ArrayPtr<const zc::byte> operator""_zcb() {
  // "string"_zcb creates constexpr byte array pointer to the content of the string
  // WITHOUT the trailing 0.
  return zc::ArrayPtr<const zc::byte>(s.begin(), s.size());
};

ZC_END_HEADER
