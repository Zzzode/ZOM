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

#include "src/zc/core/array.h"

#include <src/zc/ztest/gtest.h>

#include <list>
#include <span>
#include <string>

#include "src/zc/core/debug.h"

namespace zc {
namespace {

struct TestObject {
  TestObject() {
    index = count;
    ZC_ASSERT(index != throwAt);
    ++count;
  }
  TestObject(const TestObject& other) {
    ZC_ASSERT(other.index != throwAt);
    index = -1;
    copiedCount++;
  }
  ~TestObject() noexcept(false) {
    if (index == -1) {
      --copiedCount;
    } else {
      --count;
      EXPECT_EQ(index, count);
      ZC_ASSERT(count != throwAt);
    }
  }

  int index;

  static int count;
  static int copiedCount;
  static int throwAt;
};

int TestObject::count = 0;
int TestObject::copiedCount = 0;
int TestObject::throwAt = -1;

struct TestNoexceptObject {
  TestNoexceptObject() noexcept {
    index = count;
    ++count;
  }
  TestNoexceptObject(const TestNoexceptObject& other) noexcept {
    index = -1;
    copiedCount++;
  }
  ~TestNoexceptObject() noexcept {
    if (index == -1) {
      --copiedCount;
    } else {
      --count;
      EXPECT_EQ(index, count);
    }
  }

  int index;

  static int count;
  static int copiedCount;
};

int TestNoexceptObject::count = 0;
int TestNoexceptObject::copiedCount = 0;

TEST(Array, TrivialConstructor) {
  //  char* ptr;
  {
    Array<char> chars = heapArray<char>(32);
    //    ptr = chars.begin();
    chars[0] = 12;
    chars[1] = 34;
  }

  {
    Array<char> chars = heapArray<char>(32);

    // TODO(test):  The following doesn't work in opt mode -- I guess some allocators zero the
    //   memory?  Is there some other way we can test this?  Maybe override malloc()?
    //    // Somewhat hacky:  We can't guarantee that the new array is allocated in the same place,
    //    but
    //    // any reasonable allocator is highly likely to do so.  If it does, then we expect that
    //    the
    //    // memory has not been initialized.
    //    if (chars.begin() == ptr) {
    //      EXPECT_NE(chars[0], 0);
    //      EXPECT_NE(chars[1], 0);
    //    }
  }

  {
    Array<char> chars = heapArray<char>(32, 'x');
    for (char c : chars) EXPECT_EQ('x', c);
  }
}

TEST(Array, ComplexConstructor) {
  TestObject::count = 0;
  TestObject::throwAt = -1;

  {
    Array<TestObject> array = heapArray<TestObject>(32);
    EXPECT_EQ(32, TestObject::count);
  }
  EXPECT_EQ(0, TestObject::count);
}
TEST(Array, ThrowingConstructor) {
  TestObject::count = 0;
  TestObject::throwAt = 16;

  // If a constructor throws, the previous elements should still be destroyed.
  EXPECT_ANY_THROW(heapArray<TestObject>(32));
  EXPECT_EQ(0, TestObject::count);
}

TEST(Array, ThrowingDestructor) {
  TestObject::count = 0;
  TestObject::throwAt = -1;

  Array<TestObject> array = heapArray<TestObject>(32);
  EXPECT_EQ(32, TestObject::count);

  // If a destructor throws, all elements should still be destroyed.
  TestObject::throwAt = 16;
  EXPECT_ANY_THROW(array = nullptr);
  EXPECT_EQ(0, TestObject::count);
}

TEST(Array, AraryBuilder) {
  TestObject::count = 0;
  TestObject::throwAt = -1;

  Array<TestObject> array;

  {
    ArrayBuilder<TestObject> builder = heapArrayBuilder<TestObject>(32);

    for (int i = 0; i < 32; i++) {
      EXPECT_EQ(i, TestObject::count);
      builder.add();
    }

    EXPECT_EQ(32, TestObject::count);
    array = builder.finish();
    EXPECT_EQ(32, TestObject::count);
  }

  EXPECT_EQ(32, TestObject::count);
  array = nullptr;
  EXPECT_EQ(0, TestObject::count);
}

TEST(Array, AraryBuilderAddAll) {
  {
    // Trivial case.
    char text[] = "foo";
    ArrayBuilder<char> builder = heapArrayBuilder<char>(5);
    builder.add('<');
    builder.addAll(text, text + 3);
    builder.add('>');
    auto array = builder.finish();
    EXPECT_EQ("<foo>", std::string(array.begin(), array.end()));
  }

  {
    // Trivial case, const.
    const char* text = "foo";
    ArrayBuilder<char> builder = heapArrayBuilder<char>(5);
    builder.add('<');
    builder.addAll(text, text + 3);
    builder.add('>');
    auto array = builder.finish();
    EXPECT_EQ("<foo>", std::string(array.begin(), array.end()));
  }

  {
    // Trivial case, non-pointer iterator.
    std::list<char> text = {'f', 'o', 'o'};
    ArrayBuilder<char> builder = heapArrayBuilder<char>(5);
    builder.add('<');
    builder.addAll(text);
    builder.add('>');
    auto array = builder.finish();
    EXPECT_EQ("<foo>", std::string(array.begin(), array.end()));
  }

  {
    // Complex case.
    std::string strs[] = {"foo", "bar", "baz"};
    ArrayBuilder<std::string> builder = heapArrayBuilder<std::string>(5);
    builder.add("qux");
    builder.addAll(strs, strs + 3);
    builder.add("quux");
    auto array = builder.finish();
    EXPECT_EQ("qux", array[0]);
    EXPECT_EQ("foo", array[1]);
    EXPECT_EQ("bar", array[2]);
    EXPECT_EQ("baz", array[3]);
    EXPECT_EQ("quux", array[4]);
  }

  {
    // Complex case, noexcept.
    TestNoexceptObject::count = 0;
    TestNoexceptObject::copiedCount = 0;
    TestNoexceptObject objs[3];
    EXPECT_EQ(3, TestNoexceptObject::count);
    EXPECT_EQ(0, TestNoexceptObject::copiedCount);
    ArrayBuilder<TestNoexceptObject> builder = heapArrayBuilder<TestNoexceptObject>(3);
    EXPECT_EQ(3, TestNoexceptObject::count);
    EXPECT_EQ(0, TestNoexceptObject::copiedCount);
    builder.addAll(objs, objs + 3);
    EXPECT_EQ(3, TestNoexceptObject::count);
    EXPECT_EQ(3, TestNoexceptObject::copiedCount);
    auto array = builder.finish();
    EXPECT_EQ(3, TestNoexceptObject::count);
    EXPECT_EQ(3, TestNoexceptObject::copiedCount);
  }
  EXPECT_EQ(0, TestNoexceptObject::count);
  EXPECT_EQ(0, TestNoexceptObject::copiedCount);

  {
    // Complex case, exceptions possible.
    TestObject::count = 0;
    TestObject::copiedCount = 0;
    TestObject::throwAt = -1;
    TestObject objs[3];
    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(0, TestObject::copiedCount);
    ArrayBuilder<TestObject> builder = heapArrayBuilder<TestObject>(3);
    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(0, TestObject::copiedCount);
    builder.addAll(objs, objs + 3);
    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(3, TestObject::copiedCount);
    auto array = builder.finish();
    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(3, TestObject::copiedCount);
  }
  EXPECT_EQ(0, TestObject::count);
  EXPECT_EQ(0, TestObject::copiedCount);

  {
    // Complex case, exceptions occur.
    TestObject::count = 0;
    TestObject::copiedCount = 0;
    TestObject::throwAt = -1;
    TestObject objs[3];
    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(0, TestObject::copiedCount);

    TestObject::throwAt = 1;

    ArrayBuilder<TestObject> builder = heapArrayBuilder<TestObject>(3);
    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(0, TestObject::copiedCount);

    EXPECT_ANY_THROW(builder.addAll(objs, objs + 3));
    TestObject::throwAt = -1;

    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(0, TestObject::copiedCount);
  }
  EXPECT_EQ(0, TestObject::count);
  EXPECT_EQ(0, TestObject::copiedCount);
}

TEST(Array, HeapCopy) {
  {
    Array<char> copy = heapArray("foo", 3);
    EXPECT_EQ(3u, copy.size());
    EXPECT_EQ("foo", std::string(copy.begin(), 3));
  }
  {
    Array<char> copy = heapArray(ArrayPtr<const char>("bar", 3));
    EXPECT_EQ(3u, copy.size());
    EXPECT_EQ("bar", std::string(copy.begin(), 3));
  }
  {
    const char* ptr = "baz";
    Array<char> copy = heapArray<char>(ptr, ptr + 3);
    EXPECT_EQ(3u, copy.size());
    EXPECT_EQ("baz", std::string(copy.begin(), 3));
  }
}

TEST(Array, OwnConst) {
  ArrayBuilder<int> builder = heapArrayBuilder<int>(2);
  int x[2] = {123, 234};
  builder.addAll(x, x + 2);

  Array<int> i = builder.finish();  // heapArray<int>({123, 234});
  ASSERT_EQ(2u, i.size());
  EXPECT_EQ(123, i[0]);
  EXPECT_EQ(234, i[1]);

  Array<const int> ci = mv(i);
  ASSERT_EQ(2u, ci.size());
  EXPECT_EQ(123, ci[0]);
  EXPECT_EQ(234, ci[1]);

  Array<const int> ci2 = heapArray<const int>({345, 456});
  ASSERT_EQ(2u, ci2.size());
  EXPECT_EQ(345, ci2[0]);
  EXPECT_EQ(456, ci2[1]);
}

TEST(Array, Map) {
  StringPtr foo = "abcd";
  Array<char> bar = ZC_MAP(c, foo)->char { return c + 1; };
  EXPECT_STREQ("bcde", str(bar).cStr());
}

TEST(Array, MapRawArray) {
  uint foo[4] = {1, 2, 3, 4};
  Array<uint> bar = ZC_MAP(i, foo)->uint { return i * i; };
  ASSERT_EQ(4, bar.size());
  EXPECT_EQ(1, bar[0]);
  EXPECT_EQ(4, bar[1]);
  EXPECT_EQ(9, bar[2]);
  EXPECT_EQ(16, bar[3]);
}

TEST(Array, ReleaseAsBytesOrChars) {
  {
    Array<char> chars = zc::heapArray<char>("foo", 3);
    Array<byte> bytes = chars.releaseAsBytes();
    EXPECT_TRUE(chars == nullptr);
    ASSERT_EQ(3, bytes.size());
    EXPECT_EQ('f', bytes[0]);
    EXPECT_EQ('o', bytes[1]);
    EXPECT_EQ('o', bytes[2]);

    chars = bytes.releaseAsChars();
    EXPECT_TRUE(bytes == nullptr);
    ASSERT_EQ(3, chars.size());
    EXPECT_EQ('f', chars[0]);
    EXPECT_EQ('o', chars[1]);
    EXPECT_EQ('o', chars[2]);
  }
  {
    Array<const char> chars = zc::heapArray<char>("foo", 3);
    Array<const byte> bytes = chars.releaseAsBytes();
    EXPECT_TRUE(chars == nullptr);
    ASSERT_EQ(3, bytes.size());
    EXPECT_EQ('f', bytes[0]);
    EXPECT_EQ('o', bytes[1]);
    EXPECT_EQ('o', bytes[2]);

    chars = bytes.releaseAsChars();
    EXPECT_TRUE(bytes == nullptr);
    ASSERT_EQ(3, chars.size());
    EXPECT_EQ('f', chars[0]);
    EXPECT_EQ('o', chars[1]);
    EXPECT_EQ('o', chars[2]);
  }
}

ZC_TEST("zc::arr()") {
  zc::Array<zc::String> array = zc::arr(zc::str("foo"), zc::str(123));
  ZC_EXPECT(array == zc::ArrayPtr<const zc::StringPtr>({"foo", "123"}));
}

struct ImmovableInt {
  ImmovableInt(int i) : i(i) {}
  ZC_DISALLOW_COPY_AND_MOVE(ImmovableInt);
  int i;
};

ZC_TEST("zc::arrOf()") {
  zc::Array<ImmovableInt> array = zc::arrOf<ImmovableInt>(123, 456, 789);
  ZC_ASSERT(array.size() == 3);
  ZC_EXPECT(array[0].i == 123);
  ZC_EXPECT(array[1].i == 456);
  ZC_EXPECT(array[2].i == 789);
}

struct DestructionOrderRecorder {
  DestructionOrderRecorder(uint& counter, uint& recordTo) : counter(counter), recordTo(recordTo) {}
  ~DestructionOrderRecorder() { recordTo = ++counter; }

  uint& counter;
  uint& recordTo;
};

TEST(Array, Attach) {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = zc::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = zc::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = zc::heap<DestructionOrderRecorder>(counter, destroyed3);

  auto builder = zc::heapArrayBuilder<Own<DestructionOrderRecorder>>(1);
  builder.add(zc::mv(obj1));
  auto arr = builder.finish();
  auto ptr = arr.begin();

  Array<Own<DestructionOrderRecorder>> combined = arr.attach(zc::mv(obj2), zc::mv(obj3));

  ZC_EXPECT(combined.begin() == ptr);

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

TEST(Array, AttachNested) {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = zc::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = zc::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = zc::heap<DestructionOrderRecorder>(counter, destroyed3);

  auto builder = zc::heapArrayBuilder<Own<DestructionOrderRecorder>>(1);
  builder.add(zc::mv(obj1));
  auto arr = builder.finish();
  auto ptr = arr.begin();

  Array<Own<DestructionOrderRecorder>> combined = arr.attach(zc::mv(obj2)).attach(zc::mv(obj3));

  ZC_EXPECT(combined.begin() == ptr);
  ZC_EXPECT(combined.size() == 1);

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

TEST(Array, AttachFromArrayPtr) {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = zc::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = zc::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = zc::heap<DestructionOrderRecorder>(counter, destroyed3);

  auto builder = zc::heapArrayBuilder<Own<DestructionOrderRecorder>>(1);
  builder.add(zc::mv(obj1));
  auto arr = builder.finish();
  auto ptr = arr.begin();

  Array<Own<DestructionOrderRecorder>> combined =
      arr.asPtr().attach(zc::mv(obj2)).attach(zc::mv(obj3));
  ZC_EXPECT(arr != nullptr);

  ZC_EXPECT(combined.begin() == ptr);

  ZC_EXPECT(obj1.get() == nullptr);
  ZC_EXPECT(obj2.get() == nullptr);
  ZC_EXPECT(obj3.get() == nullptr);
  ZC_EXPECT(destroyed1 == 0);
  ZC_EXPECT(destroyed2 == 0);
  ZC_EXPECT(destroyed3 == 0);

  combined = nullptr;

  ZC_EXPECT(destroyed2 == 1, destroyed2);
  ZC_EXPECT(destroyed3 == 2, destroyed3);

  arr = nullptr;

  ZC_EXPECT(destroyed1 == 3, destroyed1);
}

struct Std {
  template <typename T>
  static std::span<T> from(Array<T>* arr) {
    return std::span<T>(arr->begin(), arr->size());
  }
};

ZC_TEST("Array::as<Std>") {
  zc::Array<int> arr = zc::arr(1, 2, 4);
  std::span<int> stdArr = arr.as<Std>();
  ZC_EXPECT(stdArr.size() == 3);
}

ZC_TEST("Array::slice(start, end)") {
  zc::Array<int> arr = zc::arr(0, 1, 2, 3);

  // full slice
  ZC_EXPECT(arr.slice(0, 4) == arr);
  // slice from only start
  ZC_EXPECT(arr.slice(1, 4) == zc::arr(1, 2, 3));
  // slice from only end
  ZC_EXPECT(arr.slice(0, 3) == zc::arr(0, 1, 2));
  // slice from start and end
  ZC_EXPECT(arr.slice(1, 3) == zc::arr(1, 2));

  // empty slices
  for (auto i : zc::zeroTo(arr.size())) { ZC_EXPECT(arr.slice(i, i).size() == 0); }

#ifdef ZC_DEBUG
  // start > end
  ZC_EXPECT_THROW(FAILED, arr.slice(2, 1));
  // end > size
  ZC_EXPECT_THROW(FAILED, arr.slice(2, 5));
#endif
}

ZC_TEST("Array::slice(start, end) const") {
  const zc::Array<int> arr = zc::arr(0, 1, 2, 3);

  // full slice
  ZC_EXPECT(arr.slice(0, 4) == arr);
  // slice from only start
  ZC_EXPECT(arr.slice(1, 4) == zc::arr(1, 2, 3));
  // slice from only end
  ZC_EXPECT(arr.slice(0, 3) == zc::arr(0, 1, 2));
  // slice from start and end
  ZC_EXPECT(arr.slice(1, 3) == zc::arr(1, 2));

  // empty slices
  for (auto i : zc::zeroTo(arr.size())) { ZC_EXPECT(arr.slice(i, i).size() == 0); }

#ifdef ZC_DEBUG
  // start > end
  ZC_EXPECT_THROW(FAILED, arr.slice(2, 1));
  // end > size
  ZC_EXPECT_THROW(FAILED, arr.slice(2, 5));
#endif
}

ZC_TEST("Array::slice(start)") {
  zc::Array<int> arr = zc::arr(0, 1, 2, 3);

  ZC_EXPECT(arr.slice(0) == arr);
  ZC_EXPECT(arr.slice(1) == zc::arr(1, 2, 3));
  ZC_EXPECT(arr.slice(2) == zc::arr(2, 3));
  ZC_EXPECT(arr.slice(3) == zc::arr(3));
  ZC_EXPECT(arr.slice(4).size() == 0);

#ifdef ZC_DEBUG
  // start > size
  ZC_EXPECT_THROW(FAILED, arr.slice(5));
#endif
}

ZC_TEST("Array::slice(start) const") {
  const zc::Array<int> arr = zc::arr(0, 1, 2, 3);

  ZC_EXPECT(arr.slice(0) == arr);
  ZC_EXPECT(arr.slice(1) == zc::arr(1, 2, 3));
  ZC_EXPECT(arr.slice(2) == zc::arr(2, 3));
  ZC_EXPECT(arr.slice(3) == zc::arr(3));
  ZC_EXPECT(arr.slice(4).size() == 0);

#ifdef ZC_DEBUG
  // start > size
  ZC_EXPECT_THROW(FAILED, arr.slice(5));
#endif
}

ZC_TEST("FixedArray::fill") {
  FixedArray<int64_t, 10> arr;
  arr.fill(42);
  for (int64_t x : arr) { ZC_EXPECT(x == 42); }
}

ZC_TEST("CappedArray::fill") {
  CappedArray<int64_t, 10> arr;
  arr.fill(42);
  for (int64_t x : arr) { ZC_EXPECT(x == 42); }
}

}  // namespace
}  // namespace zc
