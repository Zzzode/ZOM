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

#include "zc/core/refcount.h"

#include "zc/ztest/gtest.h"

namespace zc {

struct SetTrueInDestructor : public Refcounted, EnableAddRefToThis<SetTrueInDestructor> {
  SetTrueInDestructor(bool* ptr) : ptr(ptr) {}
  ~SetTrueInDestructor() { *ptr = true; }

  zc::Rc<SetTrueInDestructor> newRef() { return addRefToThis(); }

  bool* ptr;
};

TEST(Refcount, Basic) {
  bool b = false;
  Own<SetTrueInDestructor> ref1 = zc::refcounted<SetTrueInDestructor>(&b);
  EXPECT_FALSE(ref1->isShared());
  Own<SetTrueInDestructor> ref2 = zc::addRef(*ref1);
  EXPECT_TRUE(ref1->isShared());
  Own<SetTrueInDestructor> ref3 = zc::addRef(*ref2);
  EXPECT_TRUE(ref1->isShared());

  EXPECT_FALSE(b);
  ref1 = Own<SetTrueInDestructor>();
  EXPECT_TRUE(ref2->isShared());
  EXPECT_FALSE(b);
  ref3 = Own<SetTrueInDestructor>();
  EXPECT_FALSE(ref2->isShared());
  EXPECT_FALSE(b);
  ref2 = Own<SetTrueInDestructor>();
  EXPECT_TRUE(b);

#ifdef ZC_DEBUG
  b = false;
  SetTrueInDestructor obj(&b);
  EXPECT_ANY_THROW(addRef(obj));
#endif
}

ZC_TEST("Rc") {
  bool b = false;

  Rc<SetTrueInDestructor> ref1 = zc::rc<SetTrueInDestructor>(&b);
  EXPECT_FALSE(ref1->isShared());
  EXPECT_TRUE(ref1 != nullptr);
  EXPECT_FALSE(ref1 == nullptr);

  Rc<SetTrueInDestructor> ref2 = ref1.addRef();
  EXPECT_TRUE(ref1->isShared());
  EXPECT_TRUE(ref1 == ref2);

  {
    Rc<SetTrueInDestructor> ref3 = ref2.addRef();
    EXPECT_TRUE(ref3->isShared());
    // ref3 is dropped
  }

  EXPECT_FALSE(b);

  // start dropping references one by one

  EXPECT_TRUE(ref2->isShared());
  ref1 = nullptr;
  EXPECT_TRUE(ref1 == nullptr);
  EXPECT_FALSE(ref2->isShared());
  EXPECT_FALSE(b);
  EXPECT_FALSE(ref1 == ref2);

  ref2 = nullptr;
  EXPECT_TRUE(ref1 == ref2);

  // last reference dropped, SetTrueInDestructor destructor should execute
  EXPECT_TRUE(b);
}

ZC_TEST("Rc Own interop") {
  bool b = false;

  Rc<SetTrueInDestructor> ref1 = zc::rc<SetTrueInDestructor>(&b);

  EXPECT_FALSE(b);
  auto own = ref1.toOwn();
  EXPECT_TRUE(ref1 == nullptr);
  EXPECT_TRUE(own.get() != nullptr);

  EXPECT_FALSE(b);
  own = nullptr;
  EXPECT_TRUE(b);
}

struct Child : public SetTrueInDestructor {
  Child(bool* ptr) : SetTrueInDestructor(ptr) {}
};

ZC_TEST("Rc inheritance") {
  bool b = false;

  auto child = zc::rc<Child>(&b);

  // up casting works automatically
  zc::Rc<SetTrueInDestructor> parent = child.addRef();

  auto down = parent.downcast<Child>();
  EXPECT_TRUE(parent == nullptr);
  EXPECT_TRUE(down != nullptr);

  EXPECT_FALSE(b);
  child = nullptr;
  EXPECT_FALSE(b);
  down = nullptr;
  EXPECT_TRUE(b);
}

ZC_TEST("Refcounted::EnableAddRefToThis") {
  bool b = false;

  auto ref1 = zc::rc<SetTrueInDestructor>(&b);
  EXPECT_FALSE(ref1->isShared());

  auto ref2 = ref1->newRef();
  EXPECT_TRUE(ref2->isShared());
  EXPECT_TRUE(ref1->isShared());
  EXPECT_FALSE(b);

  ref1 = nullptr;
  EXPECT_FALSE(ref2->isShared());
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

struct SetTrueInDestructor2 {
  // Like above but doesn't inherit Refcounted.

  SetTrueInDestructor2(bool* ptr) : ptr(ptr) {}
  ~SetTrueInDestructor2() { *ptr = true; }

  bool* ptr;
};

ZC_TEST("RefcountedWrapper") {
  {
    bool b = false;
    Own<RefcountedWrapper<SetTrueInDestructor2>> w = refcountedWrapper<SetTrueInDestructor2>(&b);
    ZC_EXPECT(!b);

    Own<SetTrueInDestructor2> ref1 = w->addWrappedRef();
    Own<SetTrueInDestructor2> ref2 = w->addWrappedRef();

    ZC_EXPECT(ref1.get() == &w->getWrapped());
    ZC_EXPECT(ref1.get() == ref2.get());

    ZC_EXPECT(!b);

    w = nullptr;
    ref1 = nullptr;

    ZC_EXPECT(!b);

    ref2 = nullptr;

    ZC_EXPECT(b);
  }

  // Wrap Own<T>.
  {
    bool b = false;
    Own<RefcountedWrapper<Own<SetTrueInDestructor2>>> w =
        refcountedWrapper<SetTrueInDestructor2>(zc::heap<SetTrueInDestructor2>(&b));
    ZC_EXPECT(!b);

    Own<SetTrueInDestructor2> ref1 = w->addWrappedRef();
    Own<SetTrueInDestructor2> ref2 = w->addWrappedRef();

    ZC_EXPECT(ref1.get() == &w->getWrapped());
    ZC_EXPECT(ref1.get() == ref2.get());

    ZC_EXPECT(!b);

    w = nullptr;
    ref1 = nullptr;

    ZC_EXPECT(!b);

    ref2 = nullptr;

    ZC_EXPECT(b);
  }

  // Try wrapping an `int` to really demonstrate the wrapped type can be anything.
  {
    Own<RefcountedWrapper<int>> w = refcountedWrapper<int>(123);
    int* ptr = &w->getWrapped();
    ZC_EXPECT(*ptr == 123);

    Own<int> ref1 = w->addWrappedRef();
    Own<int> ref2 = w->addWrappedRef();

    ZC_EXPECT(ref1.get() == ptr);
    ZC_EXPECT(ref2.get() == ptr);

    w = nullptr;
    ref1 = nullptr;

    ZC_EXPECT(*ref2 == 123);
  }
}

struct AtomicSetTrueInDestructor : public AtomicRefcounted,
                                   EnableAddRefToThis<AtomicSetTrueInDestructor> {
  AtomicSetTrueInDestructor(bool* ptr) : ptr(ptr) {}
  ~AtomicSetTrueInDestructor() { *ptr = true; }

  zc::Arc<AtomicSetTrueInDestructor> newRef() const { return addRefToThis(); }

  bool* ptr;
};

ZC_TEST("Arc") {
  bool b = false;

  zc::Arc<AtomicSetTrueInDestructor> ref1 = zc::arc<AtomicSetTrueInDestructor>(&b);
  EXPECT_FALSE(ref1->isShared());
  EXPECT_TRUE(ref1 != nullptr);
  EXPECT_FALSE(ref1 == nullptr);

  zc::Arc<AtomicSetTrueInDestructor> ref2 = ref1.addRef();

  // can be always cast to Arc<const T>
  zc::Arc<const AtomicSetTrueInDestructor> ref3 = ref1.addRef();

  // addRef works for const references too
  zc::Arc<const AtomicSetTrueInDestructor> ref4 = ref3.addRef();

  ref1 = nullptr;
  EXPECT_TRUE(ref1 == nullptr);
  ref2 = nullptr;
  EXPECT_TRUE(ref2 == nullptr);
  ref3 = nullptr;
  EXPECT_TRUE(ref3 == nullptr);

  EXPECT_FALSE(b);
  ref4 = nullptr;
  EXPECT_TRUE(b);
}

ZC_TEST("AtomicRefcounted::EnableAddRefToThis") {
  bool b = false;

  zc::Arc<AtomicSetTrueInDestructor> ref1 = zc::arc<AtomicSetTrueInDestructor>(&b);
  EXPECT_FALSE(ref1->isShared());

  zc::Arc<AtomicSetTrueInDestructor> ref2 = ref1->newRef();
  EXPECT_TRUE(ref2->isShared());
  EXPECT_TRUE(ref1->isShared());
  EXPECT_FALSE(b);

  ref1 = nullptr;
  EXPECT_FALSE(ref2->isShared());
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

ZC_TEST("Arc Own interop") {
  bool b = false;

  zc::Arc<AtomicSetTrueInDestructor> ref1 = zc::arc<AtomicSetTrueInDestructor>(&b);

  EXPECT_FALSE(b);
  auto own = ref1.toOwn();
  EXPECT_TRUE(ref1 == nullptr);
  EXPECT_TRUE(own.get() != nullptr);

  EXPECT_FALSE(b);
  own = nullptr;
  EXPECT_TRUE(b);
}

ZC_TEST("Arc disown / reown") {
  bool b = false;
  const AtomicSetTrueInDestructor* ptr = nullptr;

  {
    zc::Arc<AtomicSetTrueInDestructor> ref = zc::arc<AtomicSetTrueInDestructor>(&b);
    ptr = ref.disown();
  }

  ZC_EXPECT(b == false);

  { auto ref = zc::Arc<AtomicSetTrueInDestructor>::reown(ptr); }

  ZC_EXPECT(b == true);
}

ZC_TEST("Rc move assignment and comparison") {
  bool b1 = false, b2 = false;

  Rc<SetTrueInDestructor> ref1 = zc::rc<SetTrueInDestructor>(&b1);
  Rc<SetTrueInDestructor> ref2 = zc::rc<SetTrueInDestructor>(&b2);

  // Test inequality
  ZC_EXPECT(!(ref1 == ref2));

  // Test move assignment
  ref2 = zc::mv(ref1);
  ZC_EXPECT(ref1 == nullptr);
  ZC_EXPECT(ref2 != nullptr);
  ZC_EXPECT(b2 == true);  // Original ref2 object should be destroyed

  ref2 = nullptr;
  ZC_EXPECT(b1 == true);  // ref1's object should be destroyed
}

ZC_TEST("Arc move assignment and comparison") {
  bool b1 = false, b2 = false;

  Arc<AtomicSetTrueInDestructor> ref1 = zc::arc<AtomicSetTrueInDestructor>(&b1);
  Arc<AtomicSetTrueInDestructor> ref2 = zc::arc<AtomicSetTrueInDestructor>(&b2);

  // Test inequality
  ZC_EXPECT(!(ref1 == ref2));

  // Test move assignment
  ref2 = zc::mv(ref1);
  ZC_EXPECT(ref1 == nullptr);
  ZC_EXPECT(ref2 != nullptr);
  ZC_EXPECT(b2 == true);  // Original ref2 object should be destroyed

  ref2 = nullptr;
  ZC_EXPECT(b1 == true);  // ref1's object should be destroyed
}

ZC_TEST("Rc downcast") {
  bool b = false;

  auto child = zc::rc<Child>(&b);
  auto parent = child.addRef();

  // Test successful downcast
  auto downcast = parent.downcast<Child>();
  ZC_EXPECT(parent == nullptr);  // Original should be nullified
  ZC_EXPECT(downcast != nullptr);

  ZC_EXPECT(!b);
  child = nullptr;
  ZC_EXPECT(!b);
  downcast = nullptr;
  ZC_EXPECT(b);
}

ZC_TEST("Arc downcast") {
  bool b = false;

  struct AtomicChild : public AtomicSetTrueInDestructor {
    AtomicChild(bool* ptr) : AtomicSetTrueInDestructor(ptr) {}
  };

  auto child = zc::arc<AtomicChild>(&b);
  auto parent = child.addRef();

  // Test successful downcast
  auto downcast = parent.downcast<AtomicChild>();
  ZC_EXPECT(parent == nullptr);  // Original should be nullified
  ZC_EXPECT(downcast != nullptr);

  ZC_EXPECT(!b);
  child = nullptr;
  ZC_EXPECT(!b);
  downcast = nullptr;
  ZC_EXPECT(b);
}

ZC_TEST("atomicAddRefWeak") {
  bool b = false;

  auto ref = zc::arc<AtomicSetTrueInDestructor>(&b);
  const AtomicSetTrueInDestructor* ptr = ref.get();

  // Test successful weak ref while object is alive
  auto weakRef = zc::atomicAddRefWeak(*ptr);
  ZC_EXPECT(weakRef != zc::none);
  ZC_IF_SOME(strongRef, weakRef) { ZC_EXPECT(strongRef.get() == ptr); }

  ZC_EXPECT(!b);
  ref = nullptr;
  ZC_EXPECT(!b);  // Object still alive due to weak ref

  weakRef = zc::none;
  ZC_EXPECT(b);  // Now object is destroyed
}

ZC_TEST("RefcountedWrapper edge cases") {
  // Test with primitive type
  {
    auto wrapper = zc::refcountedWrapper<int>(42);
    ZC_EXPECT(wrapper->getWrapped() == 42);

    auto ref1 = wrapper->addWrappedRef();
    auto ref2 = wrapper->addWrappedRef();

    ZC_EXPECT(*ref1 == 42);
    ZC_EXPECT(*ref2 == 42);
    ZC_EXPECT(ref1.get() == ref2.get());

    // Modify through one reference
    *ref1 = 100;
    ZC_EXPECT(*ref2 == 100);
    ZC_EXPECT(wrapper->getWrapped() == 100);
  }

  // Test with const wrapper
  {
    auto wrapper = zc::refcountedWrapper<int>(123);
    const auto& constWrapper = *wrapper;

    ZC_EXPECT(constWrapper.getWrapped() == 123);

    auto ref = wrapper->addWrappedRef();
    ZC_EXPECT(*ref == 123);
  }
}

ZC_TEST("Rc and Arc null operations") {
  // Test Rc with nullptr
  {
    Rc<SetTrueInDestructor> ref(nullptr);
    ZC_EXPECT(ref == nullptr);
    ZC_EXPECT(ref.get() == nullptr);

    auto ref2 = ref.addRef();
    ZC_EXPECT(ref2 == nullptr);

    auto own = ref.toOwn();
    ZC_EXPECT(own.get() == nullptr);
  }

  // Test Arc with nullptr
  {
    Arc<AtomicSetTrueInDestructor> ref(nullptr);
    ZC_EXPECT(ref == nullptr);
    ZC_EXPECT(ref.get() == nullptr);

    auto ref2 = ref.addRef();
    ZC_EXPECT(ref2 == nullptr);

    auto own = ref.toOwn();
    ZC_EXPECT(own.get() == nullptr);

    const AtomicSetTrueInDestructor* ptr = ref.disown();
    ZC_EXPECT(ptr == nullptr);
  }
}

ZC_TEST("Rc and Arc get() method") {
  bool b1 = false, b2 = false;

  // Test Rc get()
  {
    auto ref = zc::rc<SetTrueInDestructor>(&b1);
    SetTrueInDestructor* ptr = ref.get();
    ZC_EXPECT(ptr != nullptr);
    ZC_EXPECT(ptr->ptr == &b1);

    const auto& constRef = ref;
    const SetTrueInDestructor* constPtr = constRef.get();
    ZC_EXPECT(constPtr == ptr);
  }
  ZC_EXPECT(b1 == true);

  // Test Arc get()
  {
    auto ref = zc::arc<AtomicSetTrueInDestructor>(&b2);
    const AtomicSetTrueInDestructor* ptr = ref.get();
    ZC_EXPECT(ptr != nullptr);
    ZC_EXPECT(ptr->ptr == &b2);
  }
  ZC_EXPECT(b2 == true);
}

}  // namespace zc
