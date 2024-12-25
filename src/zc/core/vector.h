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

#pragma once

#include "src/zc/core/array.h"

ZC_BEGIN_HEADER

namespace zc {

template <typename T>
class Vector {
  // Similar to std::vector, but based on ZC framework.
  //
  // This implementation always uses move constructors when growing the backing array.  If the
  // move constructor throws, the Vector is left in an inconsistent state.  This is acceptable
  // under ZC exception theory which assumes that exceptions leave things in inconsistent states.

  // TODO(someday): Allow specifying a custom allocator.

public:
  inline Vector() = default;
  inline explicit Vector(size_t capacity) : builder(heapArrayBuilder<T>(capacity)) {}
  inline Vector(Array<T>&& array) : builder(zc::mv(array)) {}

  inline operator ArrayPtr<T>() ZC_LIFETIMEBOUND { return builder; }
  inline operator ArrayPtr<const T>() const ZC_LIFETIMEBOUND { return builder; }
  inline ArrayPtr<T> asPtr() ZC_LIFETIMEBOUND { return builder.asPtr(); }
  inline ArrayPtr<const T> asPtr() const ZC_LIFETIMEBOUND { return builder.asPtr(); }

  inline size_t size() const { return builder.size(); }
  inline bool empty() const { return size() == 0; }
  inline size_t capacity() const { return builder.capacity(); }
  inline T& operator[](size_t index) ZC_LIFETIMEBOUND { return builder[index]; }
  inline const T& operator[](size_t index) const ZC_LIFETIMEBOUND { return builder[index]; }

  inline const T* begin() const ZC_LIFETIMEBOUND { return builder.begin(); }
  inline const T* end() const ZC_LIFETIMEBOUND { return builder.end(); }
  inline const T& front() const ZC_LIFETIMEBOUND { return builder.front(); }
  inline const T& back() const ZC_LIFETIMEBOUND { return builder.back(); }
  inline T* begin() ZC_LIFETIMEBOUND { return builder.begin(); }
  inline T* end() ZC_LIFETIMEBOUND { return builder.end(); }
  inline T& front() ZC_LIFETIMEBOUND { return builder.front(); }
  inline T& back() ZC_LIFETIMEBOUND { return builder.back(); }

  inline Array<T> releaseAsArray() {
    // TODO(perf):  Avoid a copy/move by allowing Array<T> to point to incomplete space?
    if (!builder.isFull()) { setCapacity(size()); }
    return builder.finish();
  }

  template <typename U>
  inline bool operator==(const U& other) const {
    return asPtr() == other;
  }

  inline ArrayPtr<T> slice(size_t start, size_t end) ZC_LIFETIMEBOUND {
    return asPtr().slice(start, end);
  }
  inline ArrayPtr<const T> slice(size_t start, size_t end) const ZC_LIFETIMEBOUND {
    return asPtr().slice(start, end);
  }

  inline ArrayPtr<T> first(size_t count) ZC_LIFETIMEBOUND { return slice(0, count); }
  inline ArrayPtr<const T> first(size_t count) const ZC_LIFETIMEBOUND { return slice(0, count); }

  template <typename... Params>
  inline T& add(Params&&... params) ZC_LIFETIMEBOUND {
    if (builder.isFull()) grow();
    return builder.add(zc::fwd<Params>(params)...);
  }

  template <typename Iterator>
  inline void addAll(Iterator begin, Iterator end) {
    size_t needed = builder.size() + (end - begin);
    if (needed > builder.capacity()) grow(needed);
    builder.addAll(begin, end);
  }

  template <typename Container>
  inline void addAll(Container&& container) {
    addAll(container.begin(), container.end());
  }

  inline void removeLast() { builder.removeLast(); }

  inline void resize(size_t size) {
    if (size > builder.capacity()) grow(size);
    builder.resize(size);
  }

  inline void operator=(decltype(nullptr)) { builder = nullptr; }

  inline void clear() { builder.clear(); }

  inline void truncate(size_t size) { builder.truncate(size); }

  inline void reserve(size_t size) {
    if (size > builder.capacity()) { grow(size); }
  }

private:
  ArrayBuilder<T> builder;

  void grow(size_t minCapacity = 0) {
    setCapacity(zc::max(minCapacity, capacity() == 0 ? 4 : capacity() * 2));
  }
  void setCapacity(size_t newSize) {
    if (builder.size() > newSize) { builder.truncate(newSize); }
    ArrayBuilder<T> newBuilder = heapArrayBuilder<T>(newSize);
    newBuilder.addAll(zc::mv(builder));
    builder = zc::mv(newBuilder);
  }
};

template <typename T>
inline auto ZC_STRINGIFY(const Vector<T>& v) -> decltype(toCharSequence(v.asPtr())) {
  return toCharSequence(v.asPtr());
}

}  // namespace zc

ZC_END_HEADER
