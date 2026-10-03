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

#include "zc/core/array.h"

ZC_BEGIN_HEADER

namespace zc {

template <typename T>
class Vector {
  // Similar to std::vector, but based on ZC framework.
  //
  // This implementation always uses move constructors when growing the backing array.  If the
  // move constructor throws, the Vector is left in an inconsistent state.  This is acceptable
  // under ZC exception theory which assumes that exceptions leave things in inconsistent states.

public:
  inline Vector() = default;
  inline explicit Vector(size_t capacity) : builder(heapArrayBuilder<T>(capacity)) {}
  inline Vector(Array<T>&& array) : builder(zc::mv(array)) {}

  /// \brief Construct an empty vector whose storage comes from `resource`.
  /// \param resource Resource that must outlive this vector and any released array.
  inline explicit Vector(MemoryResource& resource) : resource(resource) {}

  /// \brief Construct a reserved vector whose storage comes from `resource`.
  /// \param resource Resource that must outlive this vector and any released array.
  /// \param capacity Initial element capacity. Zero performs no allocation.
  inline Vector(MemoryResource& resource, size_t capacity)
      : builder(capacity == 0 ? ArrayBuilder<T>()
                              : resourceHeapArrayBuilder<T>(resource, capacity)),
        resource(resource) {}

  /// \brief Adopt an array and use `resource` for every later reallocation.
  /// \param resource Resource that must outlive this vector and any released array.
  /// \param array Initial storage whose own disposer remains authoritative until reallocation.
  inline Vector(MemoryResource& resource, Array<T>&& array)
      : builder(zc::mv(array)), resource(resource) {}

  inline Vector(Vector&& other) noexcept
      : builder(zc::mv(other.builder)), resource(other.resource) {}
  inline Vector& operator=(Vector&& other) {
    if (this != &other) {
      builder = zc::mv(other.builder);
      resource = other.resource;
    }
    return *this;
  }
  ZC_DISALLOW_COPY(Vector);

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

  /// \brief Append a value to the end, moving it in.
  /// \param value Value to append.
  inline void push(T value) { add(zc::mv(value)); }

  /// \brief Remove and return the last element.
  /// \return The removed element, or zc::none when the vector is empty. Never
  ///         reads past the end, matching Rust Vec::pop semantics.
  inline Maybe<T> pop() {
    if (empty()) return zc::none;
    T value = zc::mv(back());
    removeLast();
    return value;
  }

  /// \brief Check whether the vector contains an element equal to `match`.
  /// \param match Value to search for.
  /// \return True if an equal element exists.
  inline bool contains(const T& match) const { return asPtr().findFirst(match) != zc::none; }

  /// \brief Find the index of the first element equal to `match`.
  /// \param match Value to search for.
  /// \return The index, or zc::none if no equal element exists.
  inline Maybe<size_t> find(const T& match) const { return asPtr().findFirst(match); }

  /// \brief Remove the element at `index`, shifting later elements left by one.
  /// \param index Index of the element to remove. Must be less than size().
  inline void removeAt(size_t index) {
    ZC_IREQUIRE(index < size(), "Out-of-bounds Vector::removeAt().");
    for (size_t i = index; i + 1 < size(); i++) { builder[i] = zc::mv(builder[i + 1]); }
    removeLast();
  }

  /// \brief Insert `value` at `index`, shifting later elements right by one.
  /// \param index Position at which to insert. Must be at most size().
  /// \param value Value to insert.
  inline void insert(size_t index, T&& value) {
    ZC_IREQUIRE(index <= size(), "Out-of-bounds Vector::insert().");
    if (index == size()) {
      add(zc::mv(value));
      return;
    }
    add(zc::mv(value));
    T temp = zc::mv(builder[size() - 1]);
    for (size_t i = size() - 1; i > index; i--) { builder[i] = zc::mv(builder[i - 1]); }
    builder[index] = zc::mv(temp);
  }

  /// \brief Reverse the order of elements in place.
  inline void reverse() {
    size_t i = 0;
    size_t j = size();
    while (i + 1 < j) {
      --j;
      T temp = zc::mv(builder[i]);
      builder[i] = zc::mv(builder[j]);
      builder[j] = zc::mv(temp);
      ++i;
    }
  }

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
  Maybe<MemoryResource&> resource;

  void grow(size_t minCapacity = 0) {
    size_t doubledCapacity = 4;
    if (capacity() != 0) {
      ZC_IREQUIRE(capacity() <= static_cast<size_t>(zc::maxValue) / 2,
                  "Vector capacity growth overflow");
      doubledCapacity = capacity() * 2;
    }
    setCapacity(zc::max(minCapacity, doubledCapacity));
  }
  void setCapacity(size_t newSize) {
    if (builder.size() > newSize) { builder.truncate(newSize); }
    ArrayBuilder<T> newBuilder;
    if (newSize != 0) {
      ZC_IF_SOME(r, resource) {
        newBuilder = resourceHeapArrayBuilder<T>(r, newSize);
      } else {
        newBuilder = heapArrayBuilder<T>(newSize);
      }
    }
    if (newSize != 0) { newBuilder.addAll(zc::mv(builder)); }
    builder = zc::mv(newBuilder);
  }
};

template <typename T>
inline auto ZC_STRINGIFY(const Vector<T>& v) -> decltype(toCharSequence(v.asPtr())) {
  return toCharSequence(v.asPtr());
}

}  // namespace zc

ZC_END_HEADER
