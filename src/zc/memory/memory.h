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
//
// Copyright (c) 2024 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#ifndef ZC_MEMORY_MEMORY_H_
#define ZC_MEMORY_MEMORY_H_

#include "src/zc/base/common.h"

#ifdef ZC_DEBUG
#include <atomic>  // std::atomic for ZC_ASSERT_PTR_COUNTERS
#endif

ZC_BEGIN_HEADER

namespace zc {

template <typename T>
inline constexpr bool _zc_internal_isPolymorphic(T*) {
  // If you get a compiler error here complaining that T is incomplete, it's
  // because you are trying to use zc::Own<T> with a type that has only been
  // forward-declared. Since ZC doesn't know if the type might be involved in
  // inheritance (especially multiple inheritance), it doesn't know how to
  // correctly call the disposer to destroy the type, since the object's true
  // memory address may differ from the address used to point to a superclass.
  //
  // However, if you know for sure that T is NOT polymorphic (i.e. it doesn't
  // have a vtable and isn't involved in inheritance), then you can use
  // ZC_DECLARE_NON_POLYMORPHIC(T) to declare this to ZC without actually
  // completing the type. Place this macro invocation either in the global
  // scope, or in the same namespace as T is defined.
  return __is_polymorphic(T);
}

#define ZC_DECLARE_NON_POLYMORPHIC(...) \
  constexpr bool _zc_internal_isPolymorphic(__VA_ARGS__*) { return false; }
// If you want to use zc::Own<T> for an incomplete type T that you know is not
// polymorphic, then write `ZC_DECLARE_NON_POLYMORPHIC(T)` either at the global
// scope or in the same namespace as T is declared.
//
// This also works for templates, e.g.:
//
//     template <typename X, typename Y>
//     struct MyType;
//     template <typename X, typename Y>
//     ZC_DECLARE_NON_POLYMORPHIC(MyType<X, Y>)

namespace _ {  // private

template <typename T>
struct RefOrVoid_ {
  using Type = T&;
};
template <>
struct RefOrVoid_<void> {
  using Type = void;
};
template <>
struct RefOrVoid_<const void> {
  using Type = void;
};

template <typename T>
using RefOrVoid = typename RefOrVoid_<T>::Type;
// Evaluates to T&, unless T is `void`, in which case evaluates to `void`.
//
// This is a hack needed to avoid defining Own<void> as a totally separate
// class.

template <typename T,
          bool isPolymorphic = _zc_internal_isPolymorphic((T*)nullptr)>
struct CastToVoid_;

template <typename T>
struct CastToVoid_<T, false> {
  static void* apply(T* ptr) { return static_cast<void*>(ptr); }
  static const void* applyConst(T* ptr) {
    const T* cptr = ptr;
    return static_cast<const void*>(cptr);
  }
};

template <typename T>
struct CastToVoid_<T, true> {
  static void* apply(T* ptr) { return dynamic_cast<void*>(ptr); }
  static const void* applyConst(T* ptr) {
    const T* cptr = ptr;
    return dynamic_cast<const void*>(cptr);
  }
};

template <typename T>
void* castToVoid(T* ptr) {
  return CastToVoid_<T>::apply(ptr);
}

template <typename T>
const void* castToConstVoid(T* ptr) {
  return CastToVoid_<T>::applyConst(ptr);
}

void throwWrongDisposerError();

}  // namespace _

// =======================================================================================
// Disposer -- Implementation details.

class Disposer {
  // Abstract interface for a thing that "disposes" of objects, where
  // "disposing" usually means calling the destructor followed by freeing the
  // underlying memory. `Own<T>` encapsulates an object pointer with
  // the corresponding Disposer.
  //
  // Few developers will ever touch this interface. It is primarily useful for
  // those implementing custom memory allocators.

 protected:
  // Do not declare a destructor, as doing so will force a global initializer
  // for each HeapDisposer instance. Eww!

  virtual void disposeImpl(void* pointer) const = 0;
  // Disposes of the object, given a pointer to the beginning of the object. If
  // the object is polymorphic, this pointer is determined by
  // dynamic_cast<void*>(). For non-polymorphic types, Own<T> does not allow
  // any casting, so the pointer exactly matches the original one given to
  // Own<T>.

 public:
  template <typename T>
  void dispose(T* object) const;
  // Helper wrapper around disposeImpl().
  //
  // If T is polymorphic, calls `disposeImpl(dynamic_cast<void*>(object))`,
  // otherwise calls `disposeImpl(implicitCast<void*>(object))`.
  //
  // Callers must not call dispose() on the same pointer twice, even if the
  // first call throws an exception.

 private:
  template <typename T,
            bool polymorphic = _zc_internal_isPolymorphic((T*)nullptr)>
  struct Dispose_;
};

template <typename T>
class DestructorOnlyDisposer : public Disposer {
  // A disposer that merely calls the type's destructor and nothing else.

 public:
  static const DestructorOnlyDisposer instance;

  void disposeImpl(void* pointer) const override {
    reinterpret_cast<T*>(pointer)->~T();
  }
};

template <typename T>
const DestructorOnlyDisposer<T> DestructorOnlyDisposer<T>::instance =
    DestructorOnlyDisposer<T>();

class NullDisposer : public Disposer {
  // A disposer that does nothing.

 public:
  static const NullDisposer instance;

  void disposeImpl(ZC_UNUSED void* pointer) const override {}
};

// =======================================================================================
// Ptr Counters

#ifdef ZC_DEBUG
#define ZC_ASSERT_PTR_COUNTERS
// When defined, keeps track of active Ptr<T> instances and asserts validity of
// their ownership
#endif

namespace _ {

#ifdef ZC_ASSERT_PTR_COUNTERS

void atomicPtrCounterAssertionFailed(const char* const);

class AtomicPtrCounter {
  // AtomicPtrCounter uses atomic operations to keep track of active pointers.
  // Since no other memory location is observed, memory_order_relaxed is used.

 public:
  void dec() {
    size_t prevCount = count.fetch_sub(1, std::memory_order_relaxed);
    if (prevCount == 0) ZC_UNLIKELY {
        atomicPtrCounterAssertionFailed("unbalanced inc/dec");
      }
  }

  void inc() { count.fetch_add(1, std::memory_order_relaxed); }

  void assertEmpty() {
    size_t c = count.load(std::memory_order_relaxed);
    if (c != 0) ZC_UNLIKELY {
        atomicPtrCounterAssertionFailed("active pointers exist");
      }
  }

 private:
  std::atomic<size_t> count = 0;
};

using PtrCounter = AtomicPtrCounter;
// Default counter type to use

#endif
}  // namespace _

// =======================================================================================
// Own<T> -- An owned pointer.

template <typename T, typename StaticDisposer = std::nullptr_t>
class Own;

template <typename T>
class Own<T, std::nullptr_t> {
  // A transferable title to a T. When an Own<T> goes out of scope, the object's
  // Disposer is called to dispose of it. An Own<T> can be efficiently passed by
  // move, without relocating the underlying object; this transfers ownership.
  //
  // This is much like std::unique_ptr, except:
  // - You cannot release(). An owned object is not necessarily allocated with
  // new (see next
  //   point), so it would be hard to use release() correctly.
  // - The deleter is made polymorphic by virtual call rather than by template.
  // This is much
  //   more powerful -- it allows the use of custom allocators, freelists, etc.
  //   This could _almost_ be accomplished with unique_ptr by forcing everyone
  //   to use something like std::unique_ptr<T, zc::Deleter>, except that things
  //   get hairy in the presence of multiple inheritance and upcasting, and
  //   anyway if you force everyone to use a custom deleter then you've lost any
  //   benefit to interoperating with the "standard" unique_ptr.

 public:
  ZC_DISALLOW_COPY(Own);
  Own() : disposer(nullptr), ptr(nullptr) {}
  Own(Own&& other) noexcept : disposer(other.disposer), ptr(other.ptr) {
    other.ptr = nullptr;
  }
  Own(Own<RemoveConstOrDisable<T>>&& other) noexcept
      : disposer(other.disposer), ptr(other.ptr) {
    other.ptr = nullptr;
  }
  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  Own(Own<U>&& other) noexcept
      : disposer(other.disposer), ptr(cast(other.ptr)) {
    other.ptr = nullptr;
  }
  template <typename U, typename StaticDisposer,
            typename = EnableIf<canConvert<U*, T*>()>>
  Own(Own<U, StaticDisposer>&& other) noexcept;
  // Convert statically-disposed Own to dynamically-disposed Own.
  Own(T* ptr, const Disposer& disposer) noexcept
      : disposer(&disposer), ptr(ptr) {}

  ~Own() noexcept(false) { dispose(); }

  Own& operator=(Own&& other) {
    // Move-assignment operator.

    // Careful, this might own `other`. Therefore we have to transfer the
    // pointers first, then dispose.
    const Disposer* disposerCopy = disposer;
    T* ptrCopy = ptr;
    disposer = other.disposer;
    ptr = other.ptr;
    other.ptr = nullptr;
    if (ptrCopy != nullptr) {
      disposerCopy->dispose(const_cast<RemoveConst<T>*>(ptrCopy));
    }
    return *this;
  }

  Own& operator=(std::nullptr_t) {
    dispose();
    return *this;
  }

  template <typename... Attachments>
  Own<T> attach(Attachments&&... attachments) ZC_WARN_UNUSED_RESULT;
  // Returns an Own<T> which points to the same object but which also ensures
  // that all values passed to `attachments` remain alive until after this
  // object is destroyed. Normally `attachments` are other `Own<?>`s pointing to
  // objects that this one depends on.
  //
  // Note that attachments will eventually be destroyed in the order they are
  // listed. Hence, foo.attach(bar, baz) is equivalent to (but more efficient
  // than) foo.attach(bar).attach(baz).

  template <typename U>
  Own<U> downcast() {
    // Downcast the pointer to Own<U>, destroying the original pointer. If this
    // pointer does not actually point at an instance of U, the results are
    // undefined (throws an exception in debug mode if RTTI is enabled,
    // otherwise you're on your own).

    Own<U> result;
    if (ptr != nullptr) {
      result.ptr = &zc::downcast<U>(*ptr);
      result.disposer = disposer;
      ptr = nullptr;
    }
    return result;
  }

#define NULLCHECK ZC_IREQUIRE(ptr != nullptr, "null Own<> dereference")
  T* operator->() {
    NULLCHECK;
    return ptr;
  }
  const T* operator->() const {
    NULLCHECK;
    return ptr;
  }
  _::RefOrVoid<T> operator*() {
    NULLCHECK;
    return *ptr;
  }
  _::RefOrVoid<const T> operator*() const {
    NULLCHECK;
    return *ptr;
  }
#undef NULLCHECK
  T* get() { return ptr; }
  const T* get() const { return ptr; }
  operator T*() { return ptr; }
  operator const T*() const { return ptr; }

  // Surrenders ownership of the underlying object to the caller. The caller
  // must pass in the correct disposer to prove that they know how the object is
  // meant to be disposed of.
  T* disown(const Disposer* d) {
    if (d != disposer) _::throwWrongDisposerError();
    T* ptrCopy = ptr;
    ptr = nullptr;
    return ptrCopy;
  }

 private:
  const Disposer* disposer;  // Only valid if ptr != nullptr.
  T* ptr;

  explicit Own(std::nullptr_t) : disposer(nullptr), ptr(nullptr) {}

  bool operator==(std::nullptr_t) { return ptr == nullptr; }
  // Only called by Maybe<Own<T>>.

  void dispose() {
    // Make sure that if an exception is thrown, we are left with a null ptr, so
    // we won't possibly dispose again.
    T* ptrCopy = ptr;
    if (ptrCopy != nullptr) {
      ptr = nullptr;
      disposer->dispose(const_cast<RemoveConst<T>*>(ptrCopy));
    }
  }

  template <typename U>
  static T* cast(U* ptr) {
    static_assert(
        _zc_internal_isPolymorphic((T*)nullptr),
        "Casting owned pointers requires that the target type is polymorphic.");
    return ptr;
  }

  template <typename, typename>
  friend class Own;
  friend class Maybe<Own<T>>;
};

template <>
template <typename U>
void* Own<void>::cast(U* ptr) {
  return _::castToVoid(ptr);
}

template <>
template <typename U>
const void* Own<const void>::cast(U* ptr) {
  return _::castToConstVoid(ptr);
}

template <typename T, typename StaticDisposer>
class Own {
  // If a `StaticDisposer` is specified (which is not the norm), then the object
  // will be deleted by calling StaticDisposer::dispose(pointer). The pointer
  // passed to `dispose()` could be a superclass of `T`, if the pointer has been
  // upcast.
  //
  // This type can be useful for micro-optimization, if you've found that you
  // are doing excessive heap allocations to the point where the virtual call on
  // destruction is costing non-negligible resources. You should avoid this
  // unless you have a specific need, because it precludes a lot of power.

 public:
  ZC_DISALLOW_COPY(Own);
  Own() : ptr(nullptr) {}
  Own(Own&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }
  Own(Own<RemoveConstOrDisable<T>, StaticDisposer>&& other) noexcept
      : ptr(other.ptr) {
    other.ptr = nullptr;
  }
  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  Own(Own<U, StaticDisposer>&& other) noexcept : ptr(cast(other.ptr)) {
    other.ptr = nullptr;
  }
  explicit Own(T* ptr) noexcept : ptr(ptr) {}

  ~Own() noexcept(false) { dispose(); }

  Own& operator=(Own&& other) {
    // Move-assignment operator.

    // Carefully, this might own `other`. Therefore, we have to transfer the
    // pointers first, then dispose.
    T* ptrCopy = ptr;
    ptr = other.ptr;
    other.ptr = nullptr;
    if (ptrCopy != nullptr) {
      StaticDisposer::dispose(ptrCopy);
    }
    return *this;
  }

  Own& operator=(std::nullptr_t) {
    dispose();
    return *this;
  }

  template <typename U>
  Own<U, StaticDisposer> downcast() {
    // Downcast the pointer to Own<U>, destroying the original pointer. If this
    // pointer does not actually point at an instance of U, the results are
    // undefined (throws an exception in debug mode if RTTI is enabled,
    // otherwise you're on your own).

    Own<U, StaticDisposer> result;
    if (ptr != nullptr) {
      result.ptr = &zc::downcast<U>(*ptr);
      ptr = nullptr;
    }
    return result;
  }

#define NULLCHECK ZC_IREQUIRE(ptr != nullptr, "null Own<> dereference")
  T* operator->() {
    NULLCHECK;
    return ptr;
  }
  const T* operator->() const {
    NULLCHECK;
    return ptr;
  }
  _::RefOrVoid<T> operator*() {
    NULLCHECK;
    return *ptr;
  }
  _::RefOrVoid<const T> operator*() const {
    NULLCHECK;
    return *ptr;
  }
#undef NULLCHECK
  T* get() { return ptr; }
  const T* get() const { return ptr; }
  operator T*() { return ptr; }
  operator const T*() const { return ptr; }

  // Surrenders ownership of the underlying object to the caller. The caller
  // must pass in the correct disposer to prove that they know how the object is
  // meant to be disposed of.
  template <typename SD>
  T* disown() {
    static_assert(zc::isSameType<StaticDisposer, SD>(),
                  "disposer must be the same as Own's disposer");
    T* ptrCopy = ptr;
    ptr = nullptr;
    return ptrCopy;
  }

 private:
  T* ptr;

  explicit Own(std::nullptr_t) : ptr(nullptr) {}

  bool operator==(std::nullptr_t) { return ptr == nullptr; }
  // Only called by Maybe<Own<T>>.

  void dispose() {
    // Make sure that if an exception is thrown, we are left with a null ptr, so
    // we won't possibly dispose again.
    T* ptrCopy = ptr;
    if (ptrCopy != nullptr) {
      ptr = nullptr;
      StaticDisposer::dispose(ptrCopy);
    }
  }

  template <typename U>
  static inline T* cast(U* ptr) {
    return ptr;
  }

  template <typename, typename>
  friend class Own;
  friend class Maybe<Own<T, StaticDisposer>>;
};

namespace _ {  // private

template <typename T, typename D>
class OwnOwn {
 public:
  OwnOwn(Own<T, D>&& value) noexcept : value(zc::mv(value)) {}

  Own<T, D>& operator*() & { return value; }
  const Own<T, D>& operator*() const& { return value; }
  Own<T, D>&& operator*() && { return zc::mv(value); }
  const Own<T, D>&& operator*() const&& { return zc::mv(value); }
  Own<T, D>* operator->() { return &value; }
  const Own<T, D>* operator->() const { return &value; }
  operator Own<T, D>*() { return value ? &value : nullptr; }
  operator const Own<T, D>*() const { return value ? &value : nullptr; }

 private:
  Own<T, D> value;
};

template <typename T, typename D>
OwnOwn<T, D> readMaybe(Maybe<Own<T, D>>&& maybe) {
  return OwnOwn<T, D>(zc::mv(maybe.ptr));
}
template <typename T, typename D>
Own<T, D>* readMaybe(Maybe<Own<T, D>>& maybe) {
  return maybe.ptr ? &maybe.ptr : nullptr;
}
template <typename T, typename D>
const Own<T, D>* readMaybe(const Maybe<Own<T, D>>& maybe) {
  return maybe.ptr ? &maybe.ptr : nullptr;
}

}  // namespace _

template <typename T, typename D>
class Maybe<Own<T, D>> {
 public:
  Maybe() : ptr(nullptr) {}
  Maybe(Own<T, D>&& t) noexcept : ptr(zc::mv(t)) {}
  Maybe(Maybe&& other) noexcept : ptr(zc::mv(other.ptr)) {}

  template <typename U>
  Maybe(Maybe<Own<U, D>>&& other) : ptr(mv(other.ptr)) {}
  template <typename U>
  Maybe(Own<U, D>&& other) : ptr(mv(other)) {}

  Maybe(zc::None) noexcept : ptr(nullptr) {}

  Own<T, D>& emplace(Own<T, D> value) {
    // Assign the Maybe to the given value and return the content. This avoids
    // the need to do a ZC_ASSERT_NONNULL() immediately after setting the Maybe
    // just to read it back again.
    ptr = zc::mv(value);
    return ptr;
  }

  template <typename U = T>
  operator NoInfer<Maybe<U&>>() {
    return ptr.get();
  }
  template <typename U = T>
  operator NoInfer<Maybe<const U&>>() const {
    return ptr.get();
  }
  // Implicit conversion to `Maybe<U&>`. The weird templating is to make sure
  // that `Maybe<Own<void>>` can be instantiated with the compiler complaining
  // about forming references to void -- the use of templates here will cause
  // SFINAE to kick in and hide these, whereas if they are not templates then
  // SFINAE isn't applied and so they are considered errors.

  Maybe& operator=(Maybe&& other) {
    ptr = zc::mv(other.ptr);
    return *this;
  }

  bool operator==(zc::None) const { return ptr == nullptr; }

  Own<T, D>& orDefault(Own<T, D>& defaultValue) {
    if (ptr == nullptr) {
      return defaultValue;
    }
    return ptr;
  }
  const Own<T, D>& orDefault(const Own<T, D>& defaultValue) const {
    if (ptr == nullptr) {
      return defaultValue;
    }
    return ptr;
  }

  template <typename F,
            typename Result = decltype(instance<bool>() ? instance<Own<T, D>>()
                                                        : instance<F>()())>
  Result orDefault(F&& lazyDefaultValue) && {
    if (ptr == nullptr) {
      return lazyDefaultValue();
    }
    return zc::mv(ptr);
  }

  template <typename Func>
  auto map(Func&& f) & -> Maybe<decltype(f(instance<Own<T, D>&>()))> {
    if (ptr == nullptr) {
      return zc::none;
    }
    return f(ptr);
  }

  template <typename Func>
  auto map(
      Func&& f) const& -> Maybe<decltype(f(instance<const Own<T, D>&>()))> {
    if (ptr == nullptr) {
      return zc::none;
    }
    return f(ptr);
  }

  template <typename Func>
  auto map(Func&& f) && -> Maybe<decltype(f(instance<Own<T, D>&&>()))> {
    if (ptr == nullptr) {
      return zc::none;
    }
    return f(zc::mv(ptr));
  }

  template <typename Func>
  auto map(
      Func&& f) const&& -> Maybe<decltype(f(instance<const Own<T, D>&&>()))> {
    if (ptr == nullptr) {
      return zc::none;
    }
    return f(zc::mv(ptr));
  }

 private:
  Own<T, D> ptr;

  template <typename U>
  friend class Maybe;
  template <typename U, typename D2>
  friend _::OwnOwn<U, D2> _::readMaybe(Maybe<Own<U, D2>>&& maybe);
  template <typename U, typename D2>
  friend Own<U, D2>* _::readMaybe(Maybe<Own<U, D2>>& maybe);
  template <typename U, typename D2>
  friend const Own<U, D2>* _::readMaybe(const Maybe<Own<U, D2>>& maybe);
};

namespace _ {  // private

template <typename T>
class HeapDisposer final : public Disposer {
 public:
  void disposeImpl(void* pointer) const override {
    delete reinterpret_cast<T*>(pointer);
  }

  static const HeapDisposer instance;
};

#if _MSC_VER && _MSC_VER < 1920 && !defined(__clang__)
template <typename T>
__declspec(selectany) const HeapDisposer<T> HeapDisposer<T>::instance =
    HeapDisposer<T>();
// On MSVC 2017 we suddenly started seeing a linker error on one specific
// specialization of `HeapDisposer::instance` when seemingly-unrelated code was
// modified. Explicitly specifying
// `__declspec(selectany)` seems to fix it. But why? Shouldn't template members
// have `selectany` behavior by default? We don't know. It works and we're
// moving on.
#else
template <typename T>
const HeapDisposer<T> HeapDisposer<T>::instance = HeapDisposer<T>();
#endif

template <typename T, void (*F)(T*)>
class CustomDisposer : public Disposer {
 public:
  void disposeImpl(void* pointer) const override {
    (*F)(reinterpret_cast<T*>(pointer));
  }
};

template <typename T, void (*F)(T*)>
static constexpr CustomDisposer<T, F> CUSTOM_DISPOSER_INSTANCE{};

}  // namespace _

template <typename T, typename... Params>
Own<T> heap(Params&&... params) {
  // heap<T>(...) allocates a T on the heap, forwarding the parameters to its
  // constructor. The exact heap implementation is unspecified -- for now it is
  // operator new, but you should not assume this. (Since we know the object
  // size at delete time, we could actually implement an allocator that is more
  // efficient than operator new.)

  return Own<T>(new T(zc::fwd<Params>(params)...),
                _::HeapDisposer<T>::instance);
}

template <typename T>
Own<Decay<T>> heap(T&& orig) {
  // Allocate a copy (or move) of the argument on the heap.
  //
  // The purpose of this overload is to allow you to omit the template parameter
  // as there is only one argument and the purpose is to copy it.

  using T2 = Decay<T>;
  return Own<T2>(new T2(zc::fwd<T>(orig)), _::HeapDisposer<T2>::instance);
}

template <auto F, typename T>
Own<T> disposeWith(T* ptr) {
  // Associate a pre-allocated raw pointer with a corresponding disposal
  // function. The first template parameter should be a function pointer e.g.
  // disposeWith<freeInt>(new int(0)).

  return Own<T>(ptr, _::CUSTOM_DISPOSER_INSTANCE<T, F>);
}

template <typename T, typename... Attachments>
Own<Decay<T>> attachVal(T&& value, Attachments&&... attachments);
// Returns an Own<T> that takes ownership of `value` and `attachments`, and
// points to `value`.
//
// This is equivalent to heap(value).attach(attachments), but only does one
// allocation rather than two.

template <typename T, typename... Attachments>
Own<T> attachRef(T& value, Attachments&&... attachments);
// Like attach() but `value` is not moved; the resulting Own<T> points to its
// existing location. This is preferred if `value` is already owned by one of
// `attachments`.
//
// This is equivalent to Own<T>(&value,
// zc::NullDisposer::instance).attach(attachments), but is easier to write and
// allocates slightly less memory.

// =======================================================================================
// SpaceFor<T> -- assists in manual allocation

template <typename T>
class SpaceFor {
  // A class which has the same size and alignment as T but does not call its
  // constructor or destructor automatically. Instead, call construct() to
  // construct a T in the space, which returns an Own<T> which will take care of
  // calling T's destructor later.

 public:
  SpaceFor() {}
  ~SpaceFor() {}

  template <typename... Params>
  Own<T> construct(Params&&... params) {
    ctor(value, zc::fwd<Params>(params)...);
    return Own<T>(&value, DestructorOnlyDisposer<T>::instance);
  }

 private:
  union {
    T value;
  };
};

// =======================================================================================
// Pin<T>

template <typename T>
class Ptr;

template <typename T>
class Pin {
  // Pin<T> is a smart, in-place storage for T.
  //
  // Pin<T> should be created on the stack or used as a data member. It should
  // not be allocated on the heap. Pin<T> is integrated with Ptr<T>, and is
  // legal to move/destroy only when there are no active pointers. When
  // ZC_ASSERT_PTR_COUNTERS is defined, pointers are tracked and validity of
  // these operations are asserted. Zero-overhead replacement for T if
  // ZC_ASSERT_PTR_COUNTERS is not defined.

 public:
  template <typename... Params>
  Pin(Params&&... params) : t(zc::fwd<Params>(params)...) {}
  // Create new Pin<T> using the corresponding T constructor.

  Pin(Pin<T>&& other) : t(zc::mv(other.t)) {
    // Move T's ownership.
    // Undefined behavior when live pointers exist, asserted when
    // ZC_ASSERT_PTR_COUNTERS is defined.
#ifdef ZC_ASSERT_PTR_COUNTERS
    other.ptrCounter.assertEmpty();
#endif
  }

  ~Pin() {
    // Destroy a Pin with an underlying object.
    // Undefined behavior when live pointers exist, asserted when
    // ZC_ASSERT_PTR_COUNTERS are defined.
#ifdef ZC_ASSERT_PTR_COUNTERS
    ptrCounter.assertEmpty();
#endif
  }

  T* operator->() { return get(); }
  const T* operator->() const { return get(); }

  operator Ptr<T>() { return Ptr<T>(this); }
  // Pin<T> can be implicitly converted to Ptr<T> to obtain new pointers.

  Ptr<T> asPtr() { return Ptr<T>(this); }
  // Explicit convenience method to create new pointers.

  template <typename U, typename = EnableIf<canConvert<T*, U*>()>>
  operator Ptr<U>() {
    return Ptr<U>(this);
  }
  // Pin<T> can be implicitly converted to pointers of compatible types.

  template <typename U, typename = EnableIf<canConvert<T*, U*>()>>
  Ptr<U> asPtr() {
    return Ptr<U>(this);
  }
  // Explicit convenience method to create new pointers of compatible types.

  void* operator new(size_t count) = delete;
  void* operator new[](size_t count) = delete;
  // Pin<T> can't be heap allocated, only local or data field usage is ok.

 private:
  ZC_DISALLOW_COPY(Pin);

  Pin(T&& t) : t(zc::mv(t)) {}

  T t;
#ifdef ZC_ASSERT_PTR_COUNTERS
  _::PtrCounter ptrCounter;
#endif

  T* get() { return &t; }
  const T* get() const { return &t; }

  template <typename>
  friend class Ptr;
};

// =======================================================================================
// Ptr<T>

template <typename T>
class Ptr {
  // Ptr<T> is a smart alternative to T&.
  //
  // When used together with Pin<T> it keeps track of active pointers.
  // Asserts lifetime constraints when ZC_ASSERT_PTR_COUNTERS are defined.
  // Zero-overhead alternative for T& if ZC_ASSERT_PTR_COUNTERS is not defined.

 public:
  ~Ptr() {
    if (ptr == nullptr) {
      // the value was moved out
      return;
    }
#ifdef ZC_ASSERT_PTR_COUNTERS
    counter->dec();
#endif
  }

#ifdef ZC_ASSERT_PTR_COUNTERS
  Ptr(Ptr&& other) : ptr(other.ptr), counter(other.counter) {
    other.ptr = nullptr;
  }
#else
  Ptr(Ptr&& other) : ptr(other.ptr) { other.ptr = nullptr; }
#endif

#ifdef ZC_ASSERT_PTR_COUNTERS
  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  Ptr(Ptr<U>&& other) : ptr(other.ptr), counter(other.counter) {
    other.ptr = nullptr;
  }
#else
  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  Ptr(Ptr<U>&& other) : ptr(other.ptr) {
    other.ptr = nullptr;
  }
#endif

// Ptr<T> can be freely copied.
#ifdef ZC_ASSERT_PTR_COUNTERS
  Ptr(const Ptr& other) : ptr(other.ptr), counter(other.counter) {
    counter->inc();
  }
#else
  Ptr(const Ptr& other) : ptr(other.ptr) {}
#endif

  void operator=(ZC_UNUSED std::nullptr_t other) {
    if (ptr != nullptr) {
#ifdef ZC_ASSERT_PTR_COUNTERS
      counter->dec();
      counter = nullptr;
#endif
      ptr = nullptr;
    }
  }

  T* operator->() { return get(); }
  const T* operator->() const { return get(); }

  bool operator==(const Pin<T>& other) const { return get() == other.get(); }
  bool operator==(const Ptr<T>& other) const { return get() == other.get(); }
  bool operator==(const T* const other) const { return get() == other; }

  template <typename U>
  bool operator==(const Pin<U>& other) const {
    return get() == other.get();
  }

  template <typename U>
  bool operator==(const Ptr<U>& other) const {
    return get() == other.get();
  }

  T& asRef() { return *get(); }
  // Obtain a `T&` reference.
  // This is an unsafe operation and should be avoided unless absolutely
  // necessary. It is undefined behavior to use the reference after the object
  // managed by this Ptr<T> ceased to exist.

 private:
#ifdef ZC_ASSERT_PTR_COUNTERS
  Ptr(Pin<T>* pin) : ptr(pin->get()), counter(&pin->ptrCounter) {
    counter->inc();
  }
#else
  Ptr(Pin<T>* pin) : ptr(pin->get()) {}
#endif

#ifdef ZC_ASSERT_PTR_COUNTERS
  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  Ptr(Pin<U>* pin) : ptr(pin->get()), counter(&pin->ptrCounter) {
    counter->inc();
  }
#else
  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  Ptr(Pin<U>* pin) : ptr(pin->get()) {}
#endif

  T* ptr;
#ifdef ZC_ASSERT_PTR_COUNTERS
  _::PtrCounter* counter;
#endif

  T* get() { return ptr; }
  const T* get() const { return ptr; }

  template <typename>
  friend class Ptr;
  template <typename>
  friend class Pin;
};

// =======================================================================================
// Inline implementation details

template <typename T>
struct Disposer::Dispose_<T, true> {
  static void dispose(T* object, const Disposer& disposer) {
    // Note that dynamic_cast<void*> does not require RTTI to be enabled,
    // because the offset to the top of the object is in the vtable -- as it
    // obviously needs to be to correctly implement operator delete.
    disposer.disposeImpl(dynamic_cast<void*>(object));
  }
};
template <typename T>
struct Disposer::Dispose_<T, false> {
  static void dispose(T* object, const Disposer& disposer) {
    disposer.disposeImpl(static_cast<void*>(object));
  }
};

template <typename T>
void Disposer::dispose(T* object) const {
  Dispose_<T>::dispose(object, *this);
}

namespace _ {  // private

template <typename... T>
struct OwnedBundle;

template <>
struct OwnedBundle<> {};

template <typename First, typename... Rest>
struct OwnedBundle<First, Rest...> : public OwnedBundle<Rest...> {
  OwnedBundle(First&& first, Rest&&... rest)
      : OwnedBundle<Rest...>(zc::fwd<Rest>(rest)...),
        first(zc::fwd<First>(first)) {}

  // Note that it's intentional that `first` is destroyed before `rest`. This
  // way, doing ptr.attach(foo, bar, baz) is equivalent to
  // ptr.attach(foo).attach(bar).attach(baz) in terms of destruction order
  // (although the former does fewer allocations).
  Decay<First> first;
};

template <typename... T>
struct DisposableOwnedBundle final : Disposer, public OwnedBundle<T...> {
  DisposableOwnedBundle(T&&... values)
      : OwnedBundle<T...>(zc::fwd<T>(values)...) {}
  void disposeImpl(ZC_UNUSED void* pointer) const override { delete this; }
};

template <typename T, typename StaticDisposer>
class StaticDisposerAdapter final : public Disposer {
  // Adapts a static disposer to be called dynamically.
 public:
  void disposeImpl(void* pointer) const override {
    StaticDisposer::dispose(reinterpret_cast<T*>(pointer));
  }

  static const StaticDisposerAdapter instance;
};

template <typename T, typename D>
const StaticDisposerAdapter<T, D> StaticDisposerAdapter<T, D>::instance =
    StaticDisposerAdapter<T, D>();

}  // namespace _

template <typename T>
template <typename... Attachments>
Own<T> Own<T>::attach(Attachments&&... attachments) {
  T* ptrCopy = ptr;

  ZC_IREQUIRE(ptrCopy != nullptr, "cannot attach to null pointer");

  // HACK: If someone accidentally calls .attach() on a null pointer in opt
  // mode, try our best to
  //   accomplish reasonable behavior: We turn the pointer non-null but still
  //   invalid, so that the disposer will still be called when the pointer goes
  //   out of scope.
  if (ptrCopy == nullptr) ptrCopy = reinterpret_cast<T*>(1);

  auto bundle = new _::DisposableOwnedBundle<Own<T>, Attachments...>(
      zc::mv(*this), zc::fwd<Attachments>(attachments)...);
  return Own<T>(ptrCopy, *bundle);
}

template <typename T, typename... Attachments>
Own<T> attachRef(T& value, Attachments&&... attachments) {
  auto bundle = new _::DisposableOwnedBundle<Attachments...>(
      zc::fwd<Attachments>(attachments)...);
  return Own<T>(&value, *bundle);
}

template <typename T, typename... Attachments>
Own<Decay<T>> attachVal(T&& value, Attachments&&... attachments) {
  auto bundle = new _::DisposableOwnedBundle<T, Attachments...>(
      zc::fwd<T>(value), zc::fwd<Attachments>(attachments)...);
  return Own<Decay<T>>(&bundle->first, *bundle);
}

template <typename T>
template <typename U, typename StaticDisposer, typename>
Own<T>::Own(Own<U, StaticDisposer>&& other) noexcept : ptr(cast(other.ptr)) {
  if (_::castToVoid(other.ptr) != reinterpret_cast<void*>(other.ptr)) {
    // Oh dangit, there's some sort of multiple inheritance going on and
    // `StaticDisposerAdapter` won't actually work because it'll receive a
    // pointer pointing to the top of the object, which isn't exactly the same
    // as the `U*` pointer it wants. We have no choice but to allocate a dynamic
    // disposer here.
    disposer =
        new _::DisposableOwnedBundle<Own<U, StaticDisposer>>(zc::mv(other));
  } else {
    disposer = &_::StaticDisposerAdapter<U, StaticDisposer>::instance;
    other.ptr = nullptr;
  }
}

}  // namespace zc

ZC_END_HEADER

#endif  // ZC_MEMORY_MEMORY_H_
