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

#ifndef ZC_CONTAINERS_ARRAY_H_
#define ZC_CONTAINERS_ARRAY_H_

#include <cstring>
#include <initializer_list>

#include "src/zc/memory/memory.h"

ZC_BEGIN_HEADER

namespace zc {

// =======================================================================================
// ArrayDisposer -- Implementation details.

class ArrayDisposer {
  // Much like Disposer from memory.h.

 protected:
  // Do not declare a destructor, as doing so will force a global initializer
  // for HeapArrayDisposer::instance.

  virtual void disposeImpl(void* firstElement, size_t elementSize,
                           size_t elementCount, size_t capacity,
                           void (*destroyElement)(void*)) const = 0;
  // Disposes of the array. `destroyElement` invokes the destructor of each
  // element, or is nullptr if the elements have trivial destructors. `capacity`
  // is the amount of space that was allocated while `elementCount` is the
  // number of elements that were actually constructed; these are always the
  // same number for Array<T> but may be different when using ArrayBuilder<T>.

 public:
  template <typename T>
  void dispose(T* firstElement, size_t elementCount, size_t capacity) const;
  // Helper wrapper around disposeImpl().
  //
  // Callers must not call dispose() on the same array twice, even if the first
  // call throws an exception.

 private:
  template <typename T,
            bool hasTrivialDestructor = ZC_HAS_TRIVIAL_DESTRUCTOR(T)>
  struct Dispose_;
};

class ExceptionSafeArrayUtil {
  // Utility class that assists in constructing or destroying elements of an
  // array, where the constructor or destructor could throw exceptions. In case
  // of an exception, ExceptionSafeArrayUtil's destructor will call destructors
  // on all elements that have been constructed but not destroyed. Remember
  // that destructors that throw exceptions are required to use UnwindDetector
  // to detect unwind and avoid exceptions in this case. Therefore, no more
  // than one exception will be thrown (and the program will not terminate).

 public:
  ExceptionSafeArrayUtil(void* ptr, size_t elementSize,
                         size_t constructedElementCount,
                         void (*destroyElement)(void*))
      : pos(reinterpret_cast<byte*>(ptr) +
            elementSize * constructedElementCount),
        elementSize(elementSize),
        constructedElementCount(constructedElementCount),
        destroyElement(destroyElement) {}
  ZC_DISALLOW_COPY_AND_MOVE(ExceptionSafeArrayUtil);

  ~ExceptionSafeArrayUtil() noexcept(false) {
    if (constructedElementCount > 0) destroyAll();
  }

  void construct(size_t count, void (*constructElement)(void*));
  // Construct the given number of elements.

  void destroyAll();
  // Destroy all elements. Call this immediately before ExceptionSafeArrayUtil
  // goes out-of-scope to ensure that one element throwing an exception does not
  // prevent the others from being destroyed.

  void release() { constructedElementCount = 0; }
  // Prevent ExceptionSafeArrayUtil's destructor from destroying the constructed
  // elements. Call this after you've successfully finished constructing.

 private:
  byte* pos;
  size_t elementSize;
  size_t constructedElementCount;
  void (*destroyElement)(void*);
};

class DestructorOnlyArrayDisposer : public ArrayDisposer {
 public:
  static const DestructorOnlyArrayDisposer instance;

  void disposeImpl(void* firstElement, size_t elementSize, size_t elementCount,
                   size_t capacity,
                   void (*destroyElement)(void*)) const override;
};

class NullArrayDisposer : public ArrayDisposer {
  // An ArrayDisposer that does nothing. Can be used to construct a fake Arrays
  // that doesn't actually own its content.

 public:
  static const NullArrayDisposer instance;

  void disposeImpl(void* firstElement, size_t elementSize, size_t elementCount,
                   size_t capacity,
                   void (*destroyElement)(void*)) const override;
};

// =======================================================================================
// Array

template <typename T>
class Array {
  // An owned array which will automatically be disposed of (using an
  // ArrayDisposer) in the destructor. Can be moved, but not copied. Much like
  // Own<T>, but for arrays rather than single objects.

 public:
  Array() : ptr(nullptr), size_(0), disposer(nullptr) {}
  Array(decltype(nullptr)) : ptr(nullptr), size_(0), disposer(nullptr) {}
  Array(Array&& other) noexcept
      : ptr(other.ptr), size_(other.size_), disposer(other.disposer) {
    other.ptr = nullptr;
    other.size_ = 0;
  }
  Array(Array<RemoveConstOrDisable<T>>&& other) noexcept
      : ptr(other.ptr), size_(other.size_), disposer(other.disposer) {
    other.ptr = nullptr;
    other.size_ = 0;
  }
  Array(T* firstElement ZC_LIFETIMEBOUND, size_t size,
        const ArrayDisposer& disposer)
      : ptr(firstElement), size_(size), disposer(&disposer) {}

  ZC_DISALLOW_COPY(Array);
  ~Array() noexcept { dispose(); }

  operator ArrayPtr<T>() ZC_LIFETIMEBOUND { return ArrayPtr<T>(ptr, size_); }
  operator ArrayPtr<const T>() const ZC_LIFETIMEBOUND {
    return ArrayPtr<T>(ptr, size_);
  }
  ArrayPtr<T> asPtr() ZC_LIFETIMEBOUND { return ArrayPtr<T>(ptr, size_); }
  ArrayPtr<const T> asPtr() const ZC_LIFETIMEBOUND {
    return ArrayPtr<T>(ptr, size_);
  }

  constexpr size_t size() const { return size_; }
  constexpr T& operator[](size_t index) ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(index < size_, "Out-of-bounds Array access.");
    return ptr[index];
  }
  constexpr const T& operator[](size_t index) const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(index < size_, "Out-of-bounds Array access.");
    return ptr[index];
  }

  constexpr const T* begin() const ZC_LIFETIMEBOUND { return ptr; }
  constexpr const T* end() const ZC_LIFETIMEBOUND { return ptr + size_; }
  constexpr const T& front() const ZC_LIFETIMEBOUND { return *ptr; }
  constexpr const T& back() const ZC_LIFETIMEBOUND {
    return *(ptr + size_ - 1);
  }
  constexpr T* begin() ZC_LIFETIMEBOUND { return ptr; }
  constexpr T* end() ZC_LIFETIMEBOUND { return ptr + size_; }
  constexpr T& front() ZC_LIFETIMEBOUND { return *ptr; }
  constexpr T& back() ZC_LIFETIMEBOUND { return *(ptr + size_ - 1); }

  template <typename U>
  bool operator==(const U& other) const {
    return asPtr() == other;
  }

  ArrayPtr<T> slice(size_t start, size_t end) ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(start <= end && end <= size_, "Out-of-bounds Array::slice().");
    return ArrayPtr<T>(ptr + start, end - start);
  }
  ArrayPtr<const T> slice(size_t start, size_t end) const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(start <= end && end <= size_, "Out-of-bounds Array::slice().");
    return ArrayPtr<const T>(ptr + start, end - start);
  }
  ArrayPtr<T> slice(size_t start) ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(start <= size_, "Out-of-bounds ArrayPtr::slice().");
    return ArrayPtr<T>(ptr + start, size_ - start);
  }
  ArrayPtr<const T> slice(size_t start) const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(start <= size_, "Out-of-bounds ArrayPtr::slice().");
    return ArrayPtr<const T>(ptr + start, size_ - start);
  }

  ArrayPtr<T> first(size_t count) ZC_LIFETIMEBOUND { return slice(0, count); }
  ArrayPtr<const T> first(size_t count) const ZC_LIFETIMEBOUND {
    return slice(0, count);
  }

  ArrayPtr<const byte> asBytes() const ZC_LIFETIMEBOUND {
    return asPtr().asBytes();
  }
  ArrayPtr<PropagateConst<T, byte>> asBytes() ZC_LIFETIMEBOUND {
    return asPtr().asBytes();
  }
  ArrayPtr<const char> asChars() const ZC_LIFETIMEBOUND {
    return asPtr().asChars();
  }
  ArrayPtr<PropagateConst<T, char>> asChars() ZC_LIFETIMEBOUND {
    return asPtr().asChars();
  }

  Array<PropagateConst<T, byte>> releaseAsBytes() {
    // Like asBytes() but transfers ownership.
    static_assert(sizeof(T) == sizeof(byte),
                  "releaseAsBytes() only possible on arrays with byte-size "
                  "elements (e.g. chars).");
    if (disposer == nullptr) return nullptr;
    Array<PropagateConst<T, byte>> result(
        reinterpret_cast<PropagateConst<T, byte>*>(ptr), size_, *disposer);
    ptr = nullptr;
    size_ = 0;
    return result;
  }
  Array<PropagateConst<T, char>> releaseAsChars() {
    // Like asChars() but transfers ownership.
    static_assert(sizeof(T) == sizeof(PropagateConst<T, char>),
                  "releaseAsChars() only possible on arrays with char-size "
                  "elements (e.g. bytes).");
    if (disposer == nullptr) return nullptr;
    Array<PropagateConst<T, char>> result(
        reinterpret_cast<PropagateConst<T, char>*>(ptr), size_, *disposer);
    ptr = nullptr;
    size_ = 0;
    return result;
  }

  constexpr bool operator==(decltype(nullptr)) const { return size_ == 0; }

  Array& operator=(decltype(nullptr)) {
    dispose();
    return *this;
  }

  Array& operator=(Array&& other) {
    dispose();
    ptr = other.ptr;
    size_ = other.size_;
    disposer = other.disposer;
    other.ptr = nullptr;
    other.size_ = 0;
    return *this;
  }

  template <typename... Attachments>
  Array<T> attach(Attachments&&... attachments) ZC_WARN_UNUSED_RESULT;
  // Like Own<T>::attach(), but attaches to an Array.

  template <typename U>
  auto as() {
    return U::from(this);
  }
  // Syntax sugar for invoking U::from.
  // Used to chain conversion calls rather than wrap with function.

 private:
  T* ptr;
  size_t size_;
  const ArrayDisposer* disposer;

  void dispose() {
    // Make sure that if an exception is thrown, we are left with a null ptr, so
    // we won't possibly dispose again.
    T* ptrCopy = ptr;
    size_t sizeCopy = size_;
    if (ptrCopy != nullptr) {
      ptr = nullptr;
      size_ = 0;
      disposer->dispose(ptrCopy, sizeCopy, sizeCopy);
    }
  }

  template <typename U>
  friend class Array;
  template <typename U>
  friend class ArrayBuilder;
};

static_assert(!canMemcpy<Array<char>>(), "canMemcpy<>() is broken");

namespace _ {  // private

class HeapArrayDisposer final : public ArrayDisposer {
 public:
  template <typename T>
  static T* allocate(size_t count);
  template <typename T>
  static T* allocateUninitialized(size_t count);

  static const HeapArrayDisposer instance;

 private:
  static void* allocateImpl(size_t elementSize, size_t elementCount,
                            size_t capacity, void (*constructElement)(void*),
                            void (*destroyElement)(void*));
  // Allocates and constructs the array. Both function pointers are null if the
  // constructor is trivial, otherwise destroyElement is null if the constructor
  // doesn't throw.

  void disposeImpl(void* firstElement, size_t elementSize, size_t elementCount,
                   size_t capacity,
                   void (*destroyElement)(void*)) const override;

  template <typename T,
            bool hasTrivialConstructor = ZC_HAS_TRIVIAL_CONSTRUCTOR(T),
            bool hasNothrowConstructor = ZC_HAS_NOTHROW_CONSTRUCTOR(T)>
  struct Allocate_;
};

}  // namespace _

template <typename T>
Array<T> heapArray(size_t size) {
  // Much like `heap<T>()` from memory.h, allocates a new array on the heap.

  return Array<T>(_::HeapArrayDisposer::allocate<T>(size), size,
                  _::HeapArrayDisposer::instance);
}

template <typename T>
Array<T> heapArray(const T* content, size_t size);
template <typename T>
Array<T> heapArray(ArrayPtr<T> content);
template <typename T>
Array<T> heapArray(ArrayPtr<const T> content);
template <typename T, typename Iterator>
Array<T> heapArray(Iterator begin, Iterator end);
template <typename T>
Array<T> heapArray(std::initializer_list<T> init);
// Allocate a heap array containing a copy of the given content.

template <typename T, typename = EnableIf<ZC_HAS_TRIVIAL_CONSTRUCTOR(T)>>
Array<T> heapArray(size_t size, T t) {
  // Allocate array pre-filled with t.
  // TODO: implement for complex T types without creating `size` instances
  // first.
  Array<T> array = heapArray<T>(size);
  array.asPtr().fill(t);
  return array;
}

template <typename T, typename Container>
Array<T> heapArrayFromIterable(Container&& a) {
  return heapArray<T>(a.begin(), a.end());
}
template <typename T>
Array<T> heapArrayFromIterable(Array<T>&& a) {
  return mv(a);
}

// =======================================================================================
// ArrayBuilder

template <typename T>
class ArrayBuilder {
  // Class which lets you build an Array<T> specifying the exact constructor
  // arguments for each element, rather than starting by default-constructing
  // them.

 public:
  ArrayBuilder() : ptr(nullptr), pos(nullptr), endPtr(nullptr) {}
  ArrayBuilder(decltype(nullptr))
      : ptr(nullptr), pos(nullptr), endPtr(nullptr) {}
  explicit ArrayBuilder(RemoveConst<T>* firstElement, size_t capacity,
                        const ArrayDisposer& disposer)
      : ptr(firstElement),
        pos(firstElement),
        endPtr(firstElement + capacity),
        disposer(&disposer) {}
  ArrayBuilder(ArrayBuilder&& other)
      : ptr(other.ptr),
        pos(other.pos),
        endPtr(other.endPtr),
        disposer(other.disposer) {
    other.ptr = nullptr;
    other.pos = nullptr;
    other.endPtr = nullptr;
  }
  ArrayBuilder(Array<T>&& other)
      : ptr(other.ptr),
        pos(other.ptr + other.size_),
        endPtr(pos),
        disposer(other.disposer) {
    // Create an already-full ArrayBuilder from an Array of the same type. This
    // constructor primarily exists to enable Vector<T> to be constructed from
    // Array<T>.
    other.ptr = nullptr;
    other.size_ = 0;
  }
  ZC_DISALLOW_COPY(ArrayBuilder);
  ~ArrayBuilder() noexcept(false) { dispose(); }

  operator ArrayPtr<T>() ZC_LIFETIMEBOUND { return arrayPtr(ptr, pos); }
  operator ArrayPtr<const T>() const ZC_LIFETIMEBOUND {
    return arrayPtr(ptr, pos);
  }
  ArrayPtr<T> asPtr() ZC_LIFETIMEBOUND { return arrayPtr(ptr, pos); }
  ArrayPtr<const T> asPtr() const ZC_LIFETIMEBOUND {
    return arrayPtr(ptr, pos);
  }

  size_t size() const { return pos - ptr; }
  size_t capacity() const { return endPtr - ptr; }
  T& operator[](size_t index) ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(index < implicitCast<size_t>(pos - ptr),
                "Out-of-bounds Array access.");
    return ptr[index];
  }
  const T& operator[](size_t index) const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(index < implicitCast<size_t>(pos - ptr),
                "Out-of-bounds Array access.");
    return ptr[index];
  }

  const T* begin() const ZC_LIFETIMEBOUND { return ptr; }
  const T* end() const ZC_LIFETIMEBOUND { return pos; }
  const T& front() const ZC_LIFETIMEBOUND { return *ptr; }
  const T& back() const ZC_LIFETIMEBOUND { return *(pos - 1); }
  T* begin() ZC_LIFETIMEBOUND { return ptr; }
  T* end() ZC_LIFETIMEBOUND { return pos; }
  T& front() ZC_LIFETIMEBOUND { return *ptr; }
  T& back() ZC_LIFETIMEBOUND { return *(pos - 1); }

  ArrayBuilder& operator=(ArrayBuilder&& other) {
    dispose();
    ptr = other.ptr;
    pos = other.pos;
    endPtr = other.endPtr;
    disposer = other.disposer;
    other.ptr = nullptr;
    other.pos = nullptr;
    other.endPtr = nullptr;
    return *this;
  }
  ArrayBuilder& operator=(decltype(nullptr)) {
    dispose();
    return *this;
  }

  template <typename... Params>
  T& add(Params&&... params) ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(pos < endPtr, "Added too many elements to ArrayBuilder.");
    ctor(*pos, zc::fwd<Params>(params)...);
    return *pos++;
  }

  template <typename Container>
  void addAll(Container&& container) {
    addAll<decltype(container.begin()), !isReference<Container>()>(
        container.begin(), container.end());
  }

  template <typename Iterator, bool move = false>
  void addAll(Iterator start, Iterator end);

  void removeLast() {
    ZC_IREQUIRE(pos > ptr, "No elements present to remove.");
    zc::dtor(*--pos);
  }

  void truncate(size_t size) {
    ZC_IREQUIRE(size <= this->size(), "can't use truncate() to expand");

    T* target = ptr + size;
    if (ZC_HAS_TRIVIAL_DESTRUCTOR(T)) {
      // const_cast is safe here because the member won't ever be dereferenced
      // because it points to the end of the segment.
      pos = const_cast<RemoveConst<T>*>(target);
    } else {
      while (pos > target) {
        zc::dtor(*--pos);
      }
    }
  }

  void clear() {
    if (ZC_HAS_TRIVIAL_DESTRUCTOR(T)) {
      // const_cast is safe here because the member won't ever be dereferenced
      // because it points to the end of the segment.
      pos = const_cast<RemoveConst<T>*>(ptr);
    } else {
      while (pos > ptr) {
        zc::dtor(*--pos);
      }
    }
  }

  void resize(size_t size) {
    ZC_IREQUIRE(size <= capacity(), "can't resize past capacity");

    T* target = ptr + size;
    if (target > pos) {
      // expand
      if (ZC_HAS_TRIVIAL_CONSTRUCTOR(T)) {
        // const_cast is safe here because the member won't ever be dereferenced
        // because it points to the end of the segment.
        pos = const_cast<RemoveConst<T>*>(target);
      } else {
        while (pos < target) {
          zc::ctor(*pos++);
        }
      }
    } else {
      // truncate
      if (ZC_HAS_TRIVIAL_DESTRUCTOR(T)) {
        // const_cast is safe here because the member won't ever be dereferenced
        // because it points to the end of the segment.
        pos = const_cast<RemoveConst<T>*>(target);
      } else {
        while (pos > target) {
          zc::dtor(*--pos);
        }
      }
    }
  }

  Array<T> finish() {
    // We could safely remove this check if we assume that the disposer
    // implementation doesn't need to know the original capacity, as is the case
    // with HeapArrayDisposer since it uses operator new() or if we created a
    // custom disposer for ArrayBuilder which stores the capacity in a prefix.
    // But that would make it hard to write cleverer heap allocators, and anyway
    // this check might catch bugs. Probably people should use Vector if they
    // want to build arrays without knowing the final size in advance.
    ZC_IREQUIRE(pos == endPtr, "ArrayBuilder::finish() called prematurely.");
    Array<T> result(reinterpret_cast<T*>(ptr), pos - ptr, *disposer);
    ptr = nullptr;
    pos = nullptr;
    endPtr = nullptr;
    return result;
  }

  bool isFull() const { return pos == endPtr; }

 private:
  T* ptr;
  RemoveConst<T>* pos;
  T* endPtr;
  const ArrayDisposer* disposer = &NullArrayDisposer::instance;

  void dispose() {
    // Make sure that if an exception is thrown, we are left with a null ptr, so
    // we won't possibly dispose again.
    T* ptrCopy = ptr;
    T* posCopy = pos;
    T* endCopy = endPtr;
    if (ptrCopy != nullptr) {
      ptr = nullptr;
      pos = nullptr;
      endPtr = nullptr;
      disposer->dispose(ptrCopy, posCopy - ptrCopy, endCopy - ptrCopy);
    }
  }
};

template <typename T>
ArrayBuilder<T> heapArrayBuilder(size_t size) {
  // Like `heapArray<T>()` but does not default-construct the elements. You
  // must construct them manually by calling `add()`.

  return ArrayBuilder<T>(
      _::HeapArrayDisposer::allocateUninitialized<RemoveConst<T>>(size), size,
      _::HeapArrayDisposer::instance);
}

// =======================================================================================
// Inline Arrays

template <typename T, size_t fixedSize>
class FixedArray {
  // A fixed-width array whose storage is allocated rather than on the
  // heap.

 public:
  constexpr size_t size() const { return fixedSize; }
  constexpr T* begin() ZC_LIFETIMEBOUND { return content; }
  constexpr T* end() ZC_LIFETIMEBOUND { return content + fixedSize; }
  constexpr const T* begin() const ZC_LIFETIMEBOUND { return content; }
  constexpr const T* end() const ZC_LIFETIMEBOUND {
    return content + fixedSize;
  }

  constexpr operator ArrayPtr<T>() ZC_LIFETIMEBOUND { return asPtr(); }
  constexpr operator ArrayPtr<const T>() const ZC_LIFETIMEBOUND {
    return asPtr();
  }

  constexpr ArrayPtr<T> asPtr() ZC_LIFETIMEBOUND {
    return arrayPtr(content, fixedSize);
  }
  constexpr ArrayPtr<const T> asPtr() const ZC_LIFETIMEBOUND {
    return arrayPtr(content, fixedSize);
  }

  constexpr T& operator[](size_t index) ZC_LIFETIMEBOUND {
    return content[index];
  }
  constexpr const T& operator[](size_t index) const ZC_LIFETIMEBOUND {
    return content[index];
  }

  void fill(T t) { asPtr().fill(t); }

 private:
  T content[fixedSize];
};

template <typename T, size_t fixedSize>
class CappedArray {
  // Like `FixedArray` but can be dynamically resized as long as the size does
  // not exceed the limit specified by the template parameter.
  //
  // TODO(someday):  Don't construct elements past currentSize?

 public:
  ZC_CONSTEXPR CappedArray() : currentSize(fixedSize) {}
  explicit constexpr CappedArray(size_t s) : currentSize(s) {}

  size_t size() const { return currentSize; }
  void setSize(size_t s) {
    ZC_IREQUIRE(s <= fixedSize);
    currentSize = s;
  }
  T* begin() ZC_LIFETIMEBOUND { return content; }
  T* end() ZC_LIFETIMEBOUND { return content + currentSize; }
  const T* begin() const ZC_LIFETIMEBOUND { return content; }
  const T* end() const ZC_LIFETIMEBOUND { return content + currentSize; }

  operator ArrayPtr<T>() ZC_LIFETIMEBOUND { return asPtr(); }
  operator ArrayPtr<const T>() const ZC_LIFETIMEBOUND { return asPtr(); }
  ArrayPtr<T> asPtr() ZC_LIFETIMEBOUND {
    return arrayPtr(content, currentSize);
  }
  ArrayPtr<const T> asPtr() const ZC_LIFETIMEBOUND {
    return arrayPtr(content, currentSize);
  }

  T& operator[](size_t index) ZC_LIFETIMEBOUND { return content[index]; }
  const T& operator[](size_t index) const ZC_LIFETIMEBOUND {
    return content[index];
  }

  void fill(T t) { asPtr().fill(t); }

 private:
  size_t currentSize;
  T content[fixedSize];
};

// =======================================================================================
// ZC_MAP

#define ZC_MAP(elementName, array)                     \
  ::zc::_::Mapper<ZC_DECLTYPE_REF(array)>(array)* [&]( \
      typename ::zc::_::Mapper<ZC_DECLTYPE_REF(array)>::Element elementName)
// Applies some function to every element of an array, returning an Array of the
// results,  with nice syntax. Example:
//
//     StringPtr foo = "abcd";
//     Array<char> bar = ZC_MAP(c, foo) -> char { return c + 1; };
//     ZC_ASSERT(str(bar) == "bcde");

namespace _ {  // private

template <typename T>
struct Mapper {
  T array;
  Mapper(T&& array) : array(zc::fwd<T>(array)) {}
  template <typename Func>
  auto operator*(Func&& func) -> Array<decltype(func(*array.begin()))> {
    auto builder =
        heapArrayBuilder<decltype(func(*array.begin()))>(array.size());
    for (auto iter = array.begin(); iter != array.end(); ++iter) {
      builder.add(func(*iter));
    }
    return builder.finish();
  }
  using Element = decltype(*zc::instance<T>().begin());
};

template <typename T, size_t s>
struct Mapper<T (&)[s]> {
  T* array;
  Mapper(T* array) : array(array) {}
  template <typename Func>
  auto operator*(Func&& func) -> Array<decltype(func(*array))> {
    auto builder = heapArrayBuilder<decltype(func(*array))>(s);
    for (size_t i = 0; i < s; i++) {
      builder.add(func(array[i]));
    }
    return builder.finish();
  }
  using Element = decltype(*array)&;
};

}  // namespace _

// =======================================================================================
// Inline implementation details

template <typename T>
struct ArrayDisposer::Dispose_<T, true> {
  static void dispose(T* firstElement, size_t elementCount, size_t capacity,
                      const ArrayDisposer& disposer) {
    disposer.disposeImpl(const_cast<RemoveConst<T>*>(firstElement), sizeof(T),
                         elementCount, capacity, nullptr);
  }
};
template <typename T>
struct ArrayDisposer::Dispose_<T, false> {
  static void destruct(void* ptr) { zc::dtor(*reinterpret_cast<T*>(ptr)); }

  static void dispose(T* firstElement, size_t elementCount, size_t capacity,
                      const ArrayDisposer& disposer) {
    disposer.disposeImpl(const_cast<RemoveConst<T>*>(firstElement), sizeof(T),
                         elementCount, capacity, &destruct);
  }
};

template <typename T>
void ArrayDisposer::dispose(T* firstElement, size_t elementCount,
                            size_t capacity) const {
  Dispose_<T>::dispose(firstElement, elementCount, capacity, *this);
}

namespace _ {  // private

template <typename T>
struct HeapArrayDisposer::Allocate_<T, true, true> {
  static T* allocate(size_t elementCount, size_t capacity) {
    return reinterpret_cast<T*>(
        allocateImpl(sizeof(T), elementCount, capacity, nullptr, nullptr));
  }
};
template <typename T>
struct HeapArrayDisposer::Allocate_<T, false, true> {
  static void construct(void* ptr) { zc::ctor(*reinterpret_cast<T*>(ptr)); }
  static T* allocate(size_t elementCount, size_t capacity) {
    return reinterpret_cast<T*>(
        allocateImpl(sizeof(T), elementCount, capacity, &construct, nullptr));
  }
};
template <typename T>
struct HeapArrayDisposer::Allocate_<T, false, false> {
  static void construct(void* ptr) { zc::ctor(*reinterpret_cast<T*>(ptr)); }
  static void destruct(void* ptr) { zc::dtor(*reinterpret_cast<T*>(ptr)); }
  static T* allocate(size_t elementCount, size_t capacity) {
    return reinterpret_cast<T*>(
        allocateImpl(sizeof(T), elementCount, capacity, &construct, &destruct));
  }
};

template <typename T>
T* HeapArrayDisposer::allocate(size_t count) {
  return Allocate_<T>::allocate(count, count);
}

template <typename T>
T* HeapArrayDisposer::allocateUninitialized(size_t count) {
  return Allocate_<T, true, true>::allocate(0, count);
}

template <typename Element, typename Iterator, bool move,
          bool = canMemcpy<Element>()>
struct CopyConstructArray_;

template <typename T, bool move>
struct CopyConstructArray_<T, T*, move, true> {
  static T* apply(T* __restrict__ pos, T* start, T* end) {
    if (end != start) {
      memcpy(pos, start,
             reinterpret_cast<byte*>(end) - reinterpret_cast<byte*>(start));
    }
    return pos + (end - start);
  }
};

template <typename T>
struct CopyConstructArray_<T, const T*, false, true> {
  static T* apply(T* __restrict__ pos, const T* start, const T* end) {
    if (end != start) {
      memcpy(pos, start,
             reinterpret_cast<const byte*>(end) -
                 reinterpret_cast<const byte*>(start));
    }
    return pos + (end - start);
  }
};

template <typename T, typename Iterator, bool move>
struct CopyConstructArray_<T, Iterator, move, true> {
  static T* apply(T* __restrict__ pos, Iterator start, Iterator end) {
    // Since both the copy constructor and assignment operator are trivial, we
    // know that assignment is equivalent to copy-constructing. So we can make
    // this case somewhat easier for the compiler to optimize.
    while (start != end) {
      *pos++ = *start++;
    }
    return pos;
  }
};

template <typename T, typename Iterator>
struct CopyConstructArray_<T, Iterator, false, false> {
  struct ExceptionGuard {
    T* start;
    T* pos;
    explicit ExceptionGuard(T* pos) : start(pos), pos(pos) {}
    ~ExceptionGuard() noexcept(false) {
      while (pos > start) {
        dtor(*--pos);
      }
    }
  };

  static T* apply(T* __restrict__ pos, Iterator start, Iterator end) {
    // Verify that T can be *implicitly* constructed from the source values.
    if (false) implicitCast<T>(*start);

    if (noexcept(T(*start))) {
      while (start != end) {
        ctor(*pos++, *start++);
      }
      return pos;
    } else {
      // Crap. This is complicated.
      ExceptionGuard guard(pos);
      while (start != end) {
        ctor(*guard.pos, *start++);
        ++guard.pos;
      }
      guard.start = guard.pos;
      return guard.pos;
    }
  }
};

template <typename T, typename Iterator>
struct CopyConstructArray_<T, Iterator, true, false> {
  // Actually move-construct.

  struct ExceptionGuard {
    T* start;
    T* pos;
    explicit ExceptionGuard(T* pos) : start(pos), pos(pos) {}
    ~ExceptionGuard() noexcept(false) {
      while (pos > start) {
        dtor(*--pos);
      }
    }
  };

  static T* apply(T* __restrict__ pos, Iterator start, Iterator end) {
    // Verify that T can be *implicitly* constructed from the source values.
    if (false) (void)implicitCast<T>(zc::mv(*start));

    if (noexcept(T(zc::mv(*start)))) {
      while (start != end) {
        ctor(*pos++, zc::mv(*start++));
      }
      return pos;
    } else {
      // Crap. This is complicated.
      ExceptionGuard guard(pos);
      while (start != end) {
        ctor(*guard.pos, zc::mv(*start++));
        ++guard.pos;
      }
      guard.start = guard.pos;
      return guard.pos;
    }
  }
};

}  // namespace _

template <typename T>
template <typename Iterator, bool move>
void ArrayBuilder<T>::addAll(Iterator start, Iterator end) {
  pos = _::CopyConstructArray_<RemoveConst<T>, Decay<Iterator>, move>::apply(
      pos, start, end);
}

template <typename T>
Array<T> heapArray(const T* content, size_t size) {
  ArrayBuilder<T> builder = heapArrayBuilder<T>(size);
  builder.addAll(content, content + size);
  return builder.finish();
}

template <typename T>
Array<T> heapArray(T* content, size_t size) {
  ArrayBuilder<T> builder = heapArrayBuilder<T>(size);
  builder.addAll(content, content + size);
  return builder.finish();
}

template <typename T>
Array<T> heapArray(ArrayPtr<T> content) {
  ArrayBuilder<T> builder = heapArrayBuilder<T>(content.size());
  builder.addAll(content);
  return builder.finish();
}

template <typename T>
Array<T> heapArray(ArrayPtr<const T> content) {
  ArrayBuilder<T> builder = heapArrayBuilder<T>(content.size());
  builder.addAll(content);
  return builder.finish();
}

template <typename T, typename Iterator>
Array<T> heapArray(Iterator begin, Iterator end) {
  ArrayBuilder<T> builder = heapArrayBuilder<T>(end - begin);
  builder.addAll(begin, end);
  return builder.finish();
}

template <typename T>
Array<T> heapArray(std::initializer_list<T> init) {
  return heapArray<T>(init.begin(), init.end());
}

template <typename T, typename... Params>
Array<Decay<T>> arr(T&& param1, Params&&... params) {
  ArrayBuilder<Decay<T>> builder =
      heapArrayBuilder<Decay<T>>(sizeof...(params) + 1);
  (builder.add(zc::fwd<T>(param1)), ..., builder.add(zc::fwd<Params>(params)));
  return builder.finish();
}
template <typename T, typename... Params>
Array<Decay<T>> arrOf(Params&&... params) {
  ArrayBuilder<Decay<T>> builder =
      heapArrayBuilder<Decay<T>>(sizeof...(params));
  (..., builder.add(zc::fwd<Params>(params)));
  return builder.finish();
}

namespace _ {  // private

template <typename... T>
struct ArrayDisposableOwnedBundle final : public ArrayDisposer,
                                          public OwnedBundle<T...> {
  ArrayDisposableOwnedBundle(T&&... values)
      : OwnedBundle<T...>(zc::fwd<T>(values)...) {}
  void disposeImpl(void*, size_t, size_t, size_t,
                   void (*)(void*)) const override {
    delete this;
  }
};

}  // namespace _

template <typename T>
template <typename... Attachments>
Array<T> Array<T>::attach(Attachments&&... attachments) {
  T* ptrCopy = ptr;
  auto sizeCopy = size_;

  ZC_IREQUIRE(ptrCopy != nullptr, "cannot attach to null pointer");

  // HACK: If someone accidentally calls .attach() on a null pointer in opt
  // mode, try our best to
  //   accomplish reasonable behavior: We turn the pointer non-null but still
  //   invalid, so that the disposer will still be called when the pointer goes
  //   out of scope.
  if (ptrCopy == nullptr) ptrCopy = reinterpret_cast<T*>(1);

  auto bundle = new _::ArrayDisposableOwnedBundle<Array<T>, Attachments...>(
      zc::mv(*this), zc::fwd<Attachments>(attachments)...);
  return Array<T>(ptrCopy, sizeCopy, *bundle);
}

template <typename T>
template <typename... Attachments>
Array<T> ArrayPtr<T>::attach(Attachments&&... attachments) const {
  T* ptrCopy = ptr;

  ZC_IREQUIRE(ptrCopy != nullptr, "cannot attach to null pointer");

  // HACK: If someone accidentally calls .attach() on a null pointer in opt
  // mode, try our best to
  //   accomplish reasonable behavior: We turn the pointer non-null but still
  //   invalid, so that the disposer will still be called when the pointer goes
  //   out of scope.
  if (ptrCopy == nullptr) ptrCopy = reinterpret_cast<T*>(1);

  auto bundle = new _::ArrayDisposableOwnedBundle<Attachments...>(
      zc::fwd<Attachments>(attachments)...);
  return Array<T>(ptrCopy, size_, *bundle);
}

}  // namespace zc

ZC_END_HEADER

#endif
