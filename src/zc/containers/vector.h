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

#ifndef ZC_CONTAINERS_VECTOR_H_
#define ZC_CONTAINERS_VECTOR_H_

#include "src/zc/containers/array.h"

ZC_BEGIN_HEADER

namespace zc {

template <typename T>
class Vector {
  // Similar to std::vector, but based on ZC framework.
  //
  // This implementation always uses move constructors when growing the backing
  // array. If the move constructor throws, the Vector is left in an
  // inconsistent state. This is acceptable under ZC exception theory which
  // assumes that exceptions leave things in inconsistent states.

  // TODO(someday): Allow specifying a custom allocator.

 public:
  Vector() = default;
  explicit Vector(size_t capacity) : builder(heapArrayBuilder<T>(capacity)) {}
  Vector(Array<T>&& array) : builder(zc::mv(array)) {}

  operator ArrayPtr<T>() ZC_LIFETIMEBOUND { return builder; }
  operator ArrayPtr<const T>() const ZC_LIFETIMEBOUND { return builder; }
  ArrayPtr<T> asPtr() ZC_LIFETIMEBOUND { return builder.asPtr(); }
  ArrayPtr<const T> asPtr() const ZC_LIFETIMEBOUND { return builder.asPtr(); }

  size_t size() const { return builder.size(); }
  bool empty() const { return size() == 0; }
  size_t capacity() const { return builder.capacity(); }
  T& operator[](size_t index) ZC_LIFETIMEBOUND { return builder[index]; }
  const T& operator[](size_t index) const ZC_LIFETIMEBOUND {
    return builder[index];
  }

  const T* begin() const ZC_LIFETIMEBOUND { return builder.begin(); }
  const T* end() const ZC_LIFETIMEBOUND { return builder.end(); }
  const T& front() const ZC_LIFETIMEBOUND { return builder.front(); }
  const T& back() const ZC_LIFETIMEBOUND { return builder.back(); }
  T* begin() ZC_LIFETIMEBOUND { return builder.begin(); }
  T* end() ZC_LIFETIMEBOUND { return builder.end(); }
  T& front() ZC_LIFETIMEBOUND { return builder.front(); }
  T& back() ZC_LIFETIMEBOUND { return builder.back(); }

  Array<T> releaseAsArray() {
    // TODO(perf):  Avoid a copy/move by allowing Array<T> to point to
    // incomplete space?
    if (!builder.isFull()) {
      setCapacity(size());
    }
    return builder.finish();
  }

  template <typename U>
  bool operator==(const U& other) const {
    return asPtr() == other;
  }

  ArrayPtr<T> slice(size_t start, size_t end) ZC_LIFETIMEBOUND {
    return asPtr().slice(start, end);
  }
  ArrayPtr<const T> slice(size_t start, size_t end) const ZC_LIFETIMEBOUND {
    return asPtr().slice(start, end);
  }

  ArrayPtr<T> first(size_t count) ZC_LIFETIMEBOUND { return slice(0, count); }
  ArrayPtr<const T> first(size_t count) const ZC_LIFETIMEBOUND {
    return slice(0, count);
  }

  template <typename... Params>
  T& add(Params&&... params) ZC_LIFETIMEBOUND {
    if (builder.isFull()) grow();
    return builder.add(zc::fwd<Params>(params)...);
  }

  template <typename Iterator>
  void addAll(Iterator begin, Iterator end) {
    size_t needed = builder.size() + (end - begin);
    if (needed > builder.capacity()) grow(needed);
    builder.addAll(begin, end);
  }

  template <typename Container>
  void addAll(Container&& container) {
    addAll(container.begin(), container.end());
  }

  void removeLast() { builder.removeLast(); }

  void resize(size_t size) {
    if (size > builder.capacity()) grow(size);
    builder.resize(size);
  }

  void operator=(std::nullptr_t) { builder = nullptr; }

  void clear() { builder.clear(); }

  void truncate(size_t size) { builder.truncate(size); }

  void reserve(size_t size) {
    if (size > builder.capacity()) {
      grow(size);
    }
  }

 private:
  ArrayBuilder<T> builder;

  void grow(size_t minCapacity = 0) {
    setCapacity(zc::max(minCapacity, capacity() == 0 ? 4 : capacity() * 2));
  }
  void setCapacity(size_t newSize) {
    if (builder.size() > newSize) {
      builder.truncate(newSize);
    }
    ArrayBuilder<T> newBuilder = heapArrayBuilder<T>(newSize);
    newBuilder.addAll(zc::mv(builder));
    builder = zc::mv(newBuilder);
  }
};

template <typename T>
auto ZC_STRINGIFY(const Vector<T>& v) -> decltype(toCharSequence(v.asPtr())) {
  return toCharSequence(v.asPtr());
}

}  // namespace zc

ZC_END_HEADER

#endif  // ZC_CONTAINERS_VECTOR_H_
