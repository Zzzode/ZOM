// Copyright (c) 2021 Cloudflare, Inc. and contributors
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

#include "zc/core/common.h"

ZC_BEGIN_HEADER

namespace zc {

template <typename T>
class ListLink;

template <typename T, typename MaybeConstT, ListLink<T> T::*link>
class ListIterator;

namespace _ {  // (private)

ZC_NORETURN(void throwDoubleAdd());
ZC_NORETURN(void throwRemovedNotPresent());
ZC_NORETURN(void throwRemovedWrongList());
ZC_NORETURN(void throwDestroyedWhileInList());

}  // namespace _

template <typename T, ListLink<T> T::*link>
class List {
  // A linked list that does no memory allocation.
  //
  // The list contains elements of type T that are allocated elsewhere. An existing object of type
  // T can be added to the list and removed again without doing any heap allocation. This is
  // achieved by requiring that T contains a field of type ListLink<T>. A pointer-to-member to
  // this field is the second parameter to the `List` template.
  //
  // zc::List is ideally suited to situations where an object wants to be able to "add itself" to
  // a list of objects waiting for a notification, with the ability to remove itself early if it
  // wants to stop waiting. With traditional STL containers, these operations would require memory
  // allocation.
  //
  // Example:
  //
  //     struct Item {
  //       ListLink<Waiter> link;
  //       // ... other members ...
  //     };
  //
  //     zc::List<Item, &Item::link> itemList;
  //
  //     Item foo;
  //     itemList.add(foo);
  //     itemList.remove(foo);
  //
  // Note that you MUST manually remove an element from the list before destroying it. ListLinks
  // do not automatically unlink themselves because this could lead to subtle thread-safety bugs
  // if the List is guarded by a mutex, and that mutex is not currently locked. Normally, you should
  // have T's destructor remove it from any lists. You can use `link.isLinked()` to check if the
  // item is currently in a list.
  //
  // zc::List is a doubly-linked list in order to allow O(1) removal of any element given only a
  // reference to the element. However, it only supports forward iteration.
  //
  // When iterating over a zc::List, you can safely remove current element which the iterator
  // points to without breaking the iteration. However, removing any *other* element could
  // invalidate the iterator.

public:
  List() = default;
  ZC_DISALLOW_COPY_AND_MOVE(List);

  bool empty() const { return head == zc::none; }

  size_t size() const { return listSize; }

  void add(T& element) {
    if ((element.*link).prev != nullptr) _::throwDoubleAdd();
    *tail = element;
    (element.*link).prev = tail;
    tail = &((element.*link).next);
    ++listSize;
  }

  void addFront(T& element) {
    if ((element.*link).prev != nullptr) _::throwDoubleAdd();
    (element.*link).next = head;
    (element.*link).prev = &head;
    ZC_IF_SOME(oldHead, head) { (oldHead.*link).prev = &(element.*link).next; }
    else { tail = &(element.*link).next; }
    head = element;
    ++listSize;
  }

  void remove(T& element) {
    if ((element.*link).prev == nullptr) _::throwRemovedNotPresent();
    *((element.*link).prev) = (element.*link).next;
    ZC_IF_SOME(n, (element.*link).next) { (n.*link).prev = (element.*link).prev; }
    else {
      if (tail != &((element.*link).next)) _::throwRemovedWrongList();
      tail = (element.*link).prev;
    }
    (element.*link).next = zc::none;
    (element.*link).prev = nullptr;
    --listSize;
  }

  typedef ListIterator<T, T, link> Iterator;
  typedef ListIterator<T, const T, link> ConstIterator;

  Iterator begin() { return Iterator(head); }
  Iterator end() { return Iterator(zc::none); }
  ConstIterator begin() const { return ConstIterator(head); }
  ConstIterator end() const { return ConstIterator(zc::none); }

  T& front() { return *begin(); }
  const T& front() const { return *begin(); }

private:
  Maybe<T&> head;
  Maybe<T&>* tail = &head;
  size_t listSize = 0;
};

template <typename T>
class ListLink {
public:
  ListLink() : next(zc::none), prev(nullptr) {}
  ~ListLink() noexcept {
    // Intentionally `noexcept` because we want to crash if a dangling pointer was left in a list.
    if (prev != nullptr) _::throwDestroyedWhileInList();
  }
  ZC_DISALLOW_COPY_AND_MOVE(ListLink);

  bool isLinked() const { return prev != nullptr; }

private:
  Maybe<T&> next;
  Maybe<T&>* prev;

  template <typename U, ListLink<U> U::*link>
  friend class List;
  template <typename U, typename MaybeConstU, ListLink<U> U::*link>
  friend class ListIterator;
};

template <typename T, typename MaybeConstT, ListLink<T> T::*link>
class ListIterator {
public:
  ListIterator() = default;

  MaybeConstT& operator*() {
    ZC_IREQUIRE(current != zc::none, "tried to dereference end of list");
    return *_::readMaybe(current);
  }
  const T& operator*() const {
    ZC_IREQUIRE(current != zc::none, "tried to dereference end of list");
    return *_::readMaybe(current);
  }
  MaybeConstT* operator->() {
    ZC_IREQUIRE(current != zc::none, "tried to dereference end of list");
    return _::readMaybe(current);
  }
  const T* operator->() const {
    ZC_IREQUIRE(current != zc::none, "tried to dereference end of list");
    return _::readMaybe(current);
  }

  inline ListIterator& operator++() {
    current = next;
    next = current.map([](MaybeConstT& obj) -> zc::Maybe<MaybeConstT&> { return (obj.*link).next; })
               .orDefault(zc::none);
    return *this;
  }
  inline ListIterator operator++(int) {
    ListIterator result = *this;
    ++*this;
    return result;
  }

  inline bool operator==(const ListIterator& other) const {
    return _::readMaybe(current) == _::readMaybe(other.current);
  }

private:
  Maybe<MaybeConstT&> current;

  Maybe<MaybeConstT&> next;
  // so that the current item can be removed from the list without invalidating the iterator

  explicit ListIterator(Maybe<MaybeConstT&> start)
      : current(start),
        next(start.map([](MaybeConstT& obj) -> zc::Maybe<MaybeConstT&> { return (obj.*link).next; })
                 .orDefault(zc::none)) {}
  friend class List<T, link>;
};

}  // namespace zc

ZC_END_HEADER
