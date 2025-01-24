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

#include "zc/core/memory.h"

#include <signal.h>
#include <zc/ztest/gtest.h>

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/function.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"

namespace zc {
namespace {

TEST(Memory, OwnConst) {
  Own<int> i = heap<int>(2);
  EXPECT_EQ(2, *i);

  Own<const int> ci = mv(i);
  EXPECT_EQ(2, *ci);

  Own<const int> ci2 = heap<const int>(3);
  EXPECT_EQ(3, *ci2);
}

TEST(Memory, CanConvert) {
  struct Super {
    virtual ~Super() {}
  };
  struct Sub : public Super {};

  static_assert(canConvert<Own<Sub>, Own<Super>>(), "failure");
  static_assert(!canConvert<Own<Super>, Own<Sub>>(), "failure");
}

struct Nested {
  Nested(bool& destroyed) : destroyed(destroyed) {}
  ~Nested() { destroyed = true; }

  bool& destroyed;
  Own<Nested> nested;
};

TEST(Memory, AssignNested) {
  bool destroyed1 = false, destroyed2 = false;
  auto nested = heap<Nested>(destroyed1);
  nested->nested = heap<Nested>(destroyed2);
  EXPECT_FALSE(destroyed1 || destroyed2);
  nested = zc::mv(nested->nested);
  EXPECT_TRUE(destroyed1);
  EXPECT_FALSE(destroyed2);
  nested = nullptr;
  EXPECT_TRUE(destroyed1 && destroyed2);
}

struct DestructionOrderRecorder {
  DestructionOrderRecorder(uint& counter, uint& recordTo) : counter(counter), recordTo(recordTo) {}
  ~DestructionOrderRecorder() { recordTo = ++counter; }

  uint& counter;
  uint& recordTo;
};

TEST(Memory, Attach) {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = zc::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = zc::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = zc::heap<DestructionOrderRecorder>(counter, destroyed3);

  auto ptr = obj1.get();

  Own<DestructionOrderRecorder> combined = obj1.attach(zc::mv(obj2), zc::mv(obj3));

  ZC_EXPECT(combined.get() == ptr);

  ZC_EXPECT(obj1.get() == nullptr);
  ZC_EXPECT(obj2.get() == nullptr);
  ZC_EXPECT(obj3.get() == nullptr);
  ZC_EXPECT(destroyed1 == 0);
  ZC_EXPECT(destroyed2 == 0);
  ZC_EXPECT(destroyed3 == 0);

  combined = nullptr;

  ZC_EXPECT(destroyed1 == 1, destroyed1);
  ZC_EXPECT(destroyed2 == 2, destroyed2);
  ZC_EXPECT(destroyed3 == 3, destroyed3);
}

TEST(Memory, AttachNested) {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = zc::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = zc::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = zc::heap<DestructionOrderRecorder>(counter, destroyed3);

  auto ptr = obj1.get();

  Own<DestructionOrderRecorder> combined = obj1.attach(zc::mv(obj2)).attach(zc::mv(obj3));

  ZC_EXPECT(combined.get() == ptr);

  ZC_EXPECT(obj1.get() == nullptr);
  ZC_EXPECT(obj2.get() == nullptr);
  ZC_EXPECT(obj3.get() == nullptr);
  ZC_EXPECT(destroyed1 == 0);
  ZC_EXPECT(destroyed2 == 0);
  ZC_EXPECT(destroyed3 == 0);

  combined = nullptr;

  ZC_EXPECT(destroyed1 == 1, destroyed1);
  ZC_EXPECT(destroyed2 == 2, destroyed2);
  ZC_EXPECT(destroyed3 == 3, destroyed3);
}

ZC_TEST("attachRef") {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = zc::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = zc::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = zc::heap<DestructionOrderRecorder>(counter, destroyed3);

  int i = 123;

  Own<int> combined = attachRef(i, zc::mv(obj1), zc::mv(obj2), zc::mv(obj3));

  ZC_EXPECT(combined.get() == &i);

  ZC_EXPECT(obj1.get() == nullptr);
  ZC_EXPECT(obj2.get() == nullptr);
  ZC_EXPECT(obj3.get() == nullptr);
  ZC_EXPECT(destroyed1 == 0);
  ZC_EXPECT(destroyed2 == 0);
  ZC_EXPECT(destroyed3 == 0);

  combined = nullptr;

  ZC_EXPECT(destroyed1 == 1, destroyed1);
  ZC_EXPECT(destroyed2 == 2, destroyed2);
  ZC_EXPECT(destroyed3 == 3, destroyed3);
}

ZC_TEST("attachVal") {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = zc::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = zc::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = zc::heap<DestructionOrderRecorder>(counter, destroyed3);

  int i = 123;

  Own<int> combined = attachVal(i, zc::mv(obj1), zc::mv(obj2), zc::mv(obj3));

  int* ptr = combined.get();
  ZC_EXPECT(ptr != &i);
  ZC_EXPECT(*ptr == i);

  ZC_EXPECT(obj1.get() == nullptr);
  ZC_EXPECT(obj2.get() == nullptr);
  ZC_EXPECT(obj3.get() == nullptr);
  ZC_EXPECT(destroyed1 == 0);
  ZC_EXPECT(destroyed2 == 0);
  ZC_EXPECT(destroyed3 == 0);

  combined = nullptr;

  ZC_EXPECT(destroyed1 == 1, destroyed1);
  ZC_EXPECT(destroyed2 == 2, destroyed2);
  ZC_EXPECT(destroyed3 == 3, destroyed3);
}

struct StaticType {
  int i;
};

struct DynamicType1 {
  virtual void foo() {}

  int j;

  DynamicType1(int j) : j(j) {}
};

struct DynamicType2 {
  virtual void bar() {}

  int k;

  DynamicType2(int k) : k(k) {}
};

struct SingularDerivedDynamic final : public DynamicType1 {
  SingularDerivedDynamic(int j, bool& destructorCalled)
      : DynamicType1(j), destructorCalled(destructorCalled) {}

  ~SingularDerivedDynamic() { destructorCalled = true; }
  ZC_DISALLOW_COPY_AND_MOVE(SingularDerivedDynamic);

  bool& destructorCalled;
};

struct MultipleDerivedDynamic final : public DynamicType1, public DynamicType2 {
  MultipleDerivedDynamic(int j, int k, bool& destructorCalled)
      : DynamicType1(j), DynamicType2(k), destructorCalled(destructorCalled) {}

  ~MultipleDerivedDynamic() { destructorCalled = true; }

  ZC_DISALLOW_COPY_AND_MOVE(MultipleDerivedDynamic);

  bool& destructorCalled;
};

TEST(Memory, OwnVoid) {
  {
    Own<StaticType> ptr = heap<StaticType>({123});
    StaticType* addr = ptr.get();
    Own<void> voidPtr = zc::mv(ptr);
    ZC_EXPECT(voidPtr.get() == implicitCast<void*>(addr));
  }

  {
    bool destructorCalled = false;
    Own<SingularDerivedDynamic> ptr = heap<SingularDerivedDynamic>(123, destructorCalled);
    SingularDerivedDynamic* addr = ptr.get();
    Own<void> voidPtr = zc::mv(ptr);
    ZC_EXPECT(voidPtr.get() == implicitCast<void*>(addr));
  }

  {
    bool destructorCalled = false;
    Own<MultipleDerivedDynamic> ptr = heap<MultipleDerivedDynamic>(123, 456, destructorCalled);
    MultipleDerivedDynamic* addr = ptr.get();
    Own<void> voidPtr = zc::mv(ptr);
    ZC_EXPECT(voidPtr.get() == implicitCast<void*>(addr));

    ZC_EXPECT(!destructorCalled);
    voidPtr = nullptr;
    ZC_EXPECT(destructorCalled);
  }

  {
    bool destructorCalled = false;
    Own<MultipleDerivedDynamic> ptr = heap<MultipleDerivedDynamic>(123, 456, destructorCalled);
    MultipleDerivedDynamic* addr = ptr.get();
    Own<DynamicType2> basePtr = zc::mv(ptr);
    DynamicType2* baseAddr = basePtr.get();

    // On most (all?) C++ ABIs, the second base class in a multiply-inherited class is offset from
    // the beginning of the object (assuming the first base class has non-zero size). We use this
    // fact here to verify that then casting to Own<void> does in fact result in a pointer that
    // points to the start of the overall object, not the base class. We expect that the pointers
    // are different here to prove that the test below is non-trivial.
    //
    // If there is some other ABI where these pointers are the same, and thus this expectation
    // fails, then it's no problem to #ifdef out the expectation on that platform.
    ZC_EXPECT(static_cast<void*>(baseAddr) != static_cast<void*>(addr));

    Own<void> voidPtr = zc::mv(basePtr);
    ZC_EXPECT(voidPtr.get() == static_cast<void*>(addr));

    ZC_EXPECT(!destructorCalled);
    voidPtr = nullptr;
    ZC_EXPECT(destructorCalled);
  }

  {
    Maybe<Own<void>> maybe;
    maybe = Own<void>(&maybe, NullDisposer::instance);
    ZC_EXPECT(ZC_ASSERT_NONNULL(maybe).get() == &maybe);
    maybe = zc::none;
    ZC_EXPECT(maybe == zc::none);
  }
}

TEST(Memory, OwnConstVoid) {
  {
    Own<StaticType> ptr = heap<StaticType>({123});
    StaticType* addr = ptr.get();
    Own<const void> voidPtr = zc::mv(ptr);
    ZC_EXPECT(voidPtr.get() == implicitCast<void*>(addr));
  }

  {
    bool destructorCalled = false;
    Own<SingularDerivedDynamic> ptr = heap<SingularDerivedDynamic>(123, destructorCalled);
    SingularDerivedDynamic* addr = ptr.get();
    Own<const void> voidPtr = zc::mv(ptr);
    ZC_EXPECT(voidPtr.get() == implicitCast<void*>(addr));
  }

  {
    bool destructorCalled = false;
    Own<MultipleDerivedDynamic> ptr = heap<MultipleDerivedDynamic>(123, 456, destructorCalled);
    MultipleDerivedDynamic* addr = ptr.get();
    Own<const void> voidPtr = zc::mv(ptr);
    ZC_EXPECT(voidPtr.get() == implicitCast<void*>(addr));

    ZC_EXPECT(!destructorCalled);
    voidPtr = nullptr;
    ZC_EXPECT(destructorCalled);
  }

  {
    bool destructorCalled = false;
    Own<MultipleDerivedDynamic> ptr = heap<MultipleDerivedDynamic>(123, 456, destructorCalled);
    MultipleDerivedDynamic* addr = ptr.get();
    Own<DynamicType2> basePtr = zc::mv(ptr);
    DynamicType2* baseAddr = basePtr.get();

    // On most (all?) C++ ABIs, the second base class in a multiply-inherited class is offset from
    // the beginning of the object (assuming the first base class has non-zero size). We use this
    // fact here to verify that then casting to Own<void> does in fact result in a pointer that
    // points to the start of the overall object, not the base class. We expect that the pointers
    // are different here to prove that the test below is non-trivial.
    //
    // If there is some other ABI where these pointers are the same, and thus this expectation
    // fails, then it's no problem to #ifdef out the expectation on that platform.
    ZC_EXPECT(static_cast<void*>(baseAddr) != static_cast<void*>(addr));

    Own<const void> voidPtr = zc::mv(basePtr);
    ZC_EXPECT(voidPtr.get() == static_cast<void*>(addr));

    ZC_EXPECT(!destructorCalled);
    voidPtr = nullptr;
    ZC_EXPECT(destructorCalled);
  }

  {
    Maybe<Own<const void>> maybe;
    maybe = Own<const void>(&maybe, NullDisposer::instance);
    ZC_EXPECT(ZC_ASSERT_NONNULL(maybe).get() == &maybe);
    maybe = zc::none;
    ZC_EXPECT(maybe == zc::none);
  }

  {
    bool destructorCalled = false;
    Own<SingularDerivedDynamic> ptr = heap<SingularDerivedDynamic>(123, destructorCalled);
    SingularDerivedDynamic* addr = ptr.get();

    ZC_EXPECT(ptr.disown(&_::HeapDisposer<SingularDerivedDynamic>::instance) == addr);
    ZC_EXPECT(!destructorCalled);
    ptr = nullptr;
    ZC_EXPECT(!destructorCalled);

    _::HeapDisposer<SingularDerivedDynamic>::instance.dispose(addr);
    ZC_EXPECT(destructorCalled);
  }
}

struct IncompleteType;
ZC_DECLARE_NON_POLYMORPHIC(IncompleteType)

template <typename T, typename U>
struct IncompleteTemplate;
template <typename T, typename U>
ZC_DECLARE_NON_POLYMORPHIC(IncompleteTemplate<T, U>)

struct IncompleteDisposer : public Disposer {
  mutable void* sawPtr = nullptr;

  void disposeImpl(void* pointer) const override { sawPtr = pointer; }
};

ZC_TEST("Own<IncompleteType>") {
  static int i;
  void* ptr = &i;

  {
    IncompleteDisposer disposer;

    {
      zc::Own<IncompleteType> foo(reinterpret_cast<IncompleteType*>(ptr), disposer);
      zc::Own<IncompleteType> bar = zc::mv(foo);
    }

    ZC_EXPECT(disposer.sawPtr == ptr);
  }

  {
    IncompleteDisposer disposer;

    {
      zc::Own<IncompleteTemplate<int, char>> foo(
          reinterpret_cast<IncompleteTemplate<int, char>*>(ptr), disposer);
      zc::Own<IncompleteTemplate<int, char>> bar = zc::mv(foo);
    }

    ZC_EXPECT(disposer.sawPtr == ptr);
  }
}

ZC_TEST("Own with static disposer") {
  static int* disposedPtr = nullptr;
  struct MyDisposer {
    static void dispose(int* value) {
      ZC_EXPECT(disposedPtr == nullptr);
      disposedPtr = value;
    };
  };

  int i;

  {
    Own<int, MyDisposer> ptr(&i);
    ZC_EXPECT(disposedPtr == nullptr);
  }
  ZC_EXPECT(disposedPtr == &i);
  disposedPtr = nullptr;

  {
    Own<int, MyDisposer> ptr(&i);
    ZC_EXPECT(disposedPtr == nullptr);
    Own<int, MyDisposer> ptr2(zc::mv(ptr));
    ZC_EXPECT(disposedPtr == nullptr);
  }
  ZC_EXPECT(disposedPtr == &i);
  disposedPtr = nullptr;

  {
    Own<int, MyDisposer> ptr2;
    {
      Own<int, MyDisposer> ptr(&i);
      ZC_EXPECT(disposedPtr == nullptr);
      ptr2 = zc::mv(ptr);
      ZC_EXPECT(disposedPtr == nullptr);
    }
    ZC_EXPECT(disposedPtr == nullptr);
  }
  ZC_EXPECT(disposedPtr == &i);
}

ZC_TEST("Maybe<Own<T>>") {
  Maybe<Own<int>> m = heap<int>(123);
  ZC_EXPECT(m != zc::none);
  Maybe<int&> mRef = m;
  ZC_EXPECT(ZC_ASSERT_NONNULL(mRef) == 123);
  ZC_EXPECT(&ZC_ASSERT_NONNULL(mRef) == ZC_ASSERT_NONNULL(m).get());
}

int* sawIntPtr = nullptr;

void freeInt(int* ptr) {
  sawIntPtr = ptr;
  delete ptr;
}

void freeChar(char* c) { delete c; }

void free(StaticType* ptr) { delete ptr; }

void free(const char* ptr) {}

ZC_TEST("disposeWith") {
  auto i = new int(1);
  {
    auto p = disposeWith<freeInt>(i);
    ZC_EXPECT(sawIntPtr == nullptr);
  }
  ZC_EXPECT(sawIntPtr == i);
  {
    auto c = new char('a');
    auto p = disposeWith<freeChar>(c);
  }
  {
    // Explicit cast required to avoid ambiguity when overloads are present.
    auto s = new StaticType{1};
    auto p = disposeWith<static_cast<void (*)(StaticType*)>(free)>(s);
  }
  {
    const char c = 'a';
    auto p2 = disposeWith<static_cast<void (*)(const char*)>(free)>(&c);
  }
}

// TODO(test):  More tests.

struct Obj {
  Obj(zc::StringPtr name) : name(zc::str(name)) {}
  Obj(Obj&&) = default;

  zc::String name;

  ZC_DISALLOW_COPY(Obj);
};

struct PtrHolder {
  zc::Ptr<Obj> ptr;
};

ZC_TEST("zc::Pin<T> basic properties") {
  // zc::Pin<T> guarantees that T won't move or disappear while there are active pointers.

  // pin constructor is a simple argument pass through
  zc::Pin<Obj> pin("a");

  // pin is a smart pointer and can be used so
  ZC_EXPECT(pin->name == "a"_zc);

  // pin can be auto converted to Ptr<T>
  zc::Ptr<Obj> ptr1 = pin;
  ZC_EXPECT(ptr1 == pin);
  ZC_EXPECT(pin == ptr1);

  // Ptr<T> is a smart pointer too
  ZC_EXPECT(ptr1->name == "a"_zc);

  // you can have more than one Ptr<T> pointing to the same object
  zc::Ptr<Obj> ptr2 = pin;
  ZC_EXPECT(ptr1 == ptr2);
  ZC_EXPECT(ptr2->name == "a"_zc);

  // when leaving the scope ptrs will be destroyed first,
  // so pin will be destroyed without problems
}

ZC_TEST("moving zc::Pin<T>") {
  zc::Pin<Obj> pin("a");

  // you can move pin around as long as there are no pointers to it
  zc::Pin<Obj> pin2(zc::mv(pin));

  // data belongs to a new pin now
  ZC_EXPECT(pin2->name == "a"_zc);

  // it is C++ and old pin still points to a valid object
  ZC_EXPECT(pin->name == ""_zc);

  // you can add new pointers to the pin with asPtr() method as well
  zc::Ptr<Obj> ptr1 = pin2.asPtr();
  ZC_EXPECT(ptr1 == pin2);
  ZC_EXPECT(ptr1->name == "a"_zc);

  {
    // you can copy pointers
    zc::Ptr<Obj> ptr2 = ptr1;
    ZC_EXPECT(ptr2 == ptr1);
    ZC_EXPECT(ptr2->name == "a"_zc);

    // ptr2 will be auto-destroyed
  }

  // you can move the pin again if all pointers are destroyed
  ptr1 = nullptr;
  zc::Pin<Obj> pin3(zc::mv(pin2));
  ZC_EXPECT(pin3->name == "a"_zc);
}

struct Obj2 : public Obj {
  Obj2(zc::StringPtr name, int size) : Obj(name), size(size) {}
  int size;
};

ZC_TEST("zc::Ptr<T> subtyping") {
  // pin the child
  zc::Pin<Obj2> pin("obj2", 42);

  // pointer to the child
  zc::Ptr<Obj2> ptr1 = pin;
  ZC_EXPECT(ptr1->name == "obj2"_zc);
  ZC_EXPECT(ptr1->size == 42);

  // pointer to the base
  zc::Ptr<Obj> ptr2 = pin;
  ZC_EXPECT(ptr2->name == "obj2"_zc);
  ZC_EXPECT(ptr2 == pin);
  ZC_EXPECT(ptr1 == ptr2);

  // pointers can be converted to the base type too
  zc::Ptr<Obj> ptr3 = zc::mv(ptr1);
  ZC_EXPECT(ptr3->name == "obj2"_zc);
  ZC_EXPECT(ptr3 == pin);
}

#ifdef ZC_ASSERT_PTR_COUNTERS
ZC_TEST("zc::Pin<T> destroyed with active ptrs crashed") {
  PtrHolder* holder = nullptr;

  ZC_EXPECT_SIGNAL(SIGABRT, {
    zc::Pin<Obj> obj("b");
    // create a pointer and leak it
    holder = new PtrHolder{obj};
    // destroying a pin when exiting scope crashes
  });
}

ZC_TEST("zc::Pin<T> moved with active ptrs crashes") {
  ZC_EXPECT_SIGNAL(SIGABRT, {
    zc::Pin<Obj> obj("b");
    auto ptr = obj.asPtr();
    // moving a pin with active reference crashes
    zc::Pin<Obj> obj2(zc::mv(obj));
  });
}
#endif

}  // namespace

}  // namespace zc
