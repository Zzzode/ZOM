// Copyright (c) 2018 Kenton Varda and contributors
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

#include "zc/core/map.h"

#include <zc/ztest/test.h>

namespace zc {
namespace _ {
namespace {

ZC_TEST("HashMap") {
  HashMap<String, int> map;

  zc::String ownFoo = zc::str("foo");
  const char* origFoo = ownFoo.begin();
  map.insert(zc::mv(ownFoo), 123);
  map.insert(zc::str("bar"), 456);

  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find("foo"_zc)) == 123);
  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find("bar"_zc)) == 456);
  ZC_EXPECT(map.find("baz"_zc) == zc::none);

  map.upsert(zc::str("foo"), 789, [](int& old, uint newValue) {
    ZC_EXPECT(old == 123);
    ZC_EXPECT(newValue == 789);
    old = 4321;
  });

  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find("foo"_zc)) == 4321);
  ZC_EXPECT(ZC_ASSERT_NONNULL(map.findEntry("foo"_zc)).key.begin() == origFoo);

  map.upsert(zc::str("foo"), 321);

  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find("foo"_zc)) == 321);
  ZC_EXPECT(ZC_ASSERT_NONNULL(map.findEntry("foo"_zc)).key.begin() == origFoo);

  ZC_EXPECT(map.findOrCreate("foo"_zc, []() -> HashMap<String, int>::Entry {
    ZC_FAIL_ASSERT("shouldn't have been called");
  }) == 321);
  ZC_EXPECT(map.findOrCreate("baz"_zc, []() {
    return HashMap<String, int>::Entry{zc::str("baz"), 654};
  }) == 654);
  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find("baz"_zc)) == 654);

  ZC_EXPECT(map.erase("bar"_zc));
  ZC_EXPECT(map.erase("baz"_zc));
  ZC_EXPECT(!map.erase("qux"_zc));

  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find("foo"_zc)) == 321);
  ZC_EXPECT(map.size() == 1);
  ZC_EXPECT(map.begin()->key == "foo");
  auto iter = map.begin();
  ++iter;
  ZC_EXPECT(iter == map.end());

  map.erase(*map.begin());
  ZC_EXPECT(map.size() == 0);
}

ZC_TEST("TreeMap") {
  TreeMap<String, int> map;

  zc::String ownFoo = zc::str("foo");
  const char* origFoo = ownFoo.begin();
  map.insert(zc::mv(ownFoo), 123);
  map.insert(zc::str("bar"), 456);

  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find("foo"_zc)) == 123);
  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find("bar"_zc)) == 456);
  ZC_EXPECT(map.find("baz"_zc) == zc::none);

  map.upsert(zc::str("foo"), 789, [](int& old, uint newValue) {
    ZC_EXPECT(old == 123);
    ZC_EXPECT(newValue == 789);
    old = 4321;
  });

  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find("foo"_zc)) == 4321);
  ZC_EXPECT(ZC_ASSERT_NONNULL(map.findEntry("foo"_zc)).key.begin() == origFoo);

  map.upsert(zc::str("foo"), 321);

  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find("foo"_zc)) == 321);
  ZC_EXPECT(ZC_ASSERT_NONNULL(map.findEntry("foo"_zc)).key.begin() == origFoo);

  ZC_EXPECT(map.findOrCreate("foo"_zc, []() -> TreeMap<String, int>::Entry {
    ZC_FAIL_ASSERT("shouldn't have been called");
  }) == 321);
  ZC_EXPECT(map.findOrCreate("baz"_zc, []() {
    return TreeMap<String, int>::Entry{zc::str("baz"), 654};
  }) == 654);
  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find("baz"_zc)) == 654);

  ZC_EXPECT(map.erase("bar"_zc));
  ZC_EXPECT(map.erase("baz"_zc));
  ZC_EXPECT(!map.erase("qux"_zc));

  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find("foo"_zc)) == 321);
  ZC_EXPECT(map.size() == 1);
  ZC_EXPECT(map.begin()->key == "foo");
  auto iter = map.begin();
  ++iter;
  ZC_EXPECT(iter == map.end());

  map.erase(*map.begin());
  ZC_EXPECT(map.size() == 0);
}

ZC_TEST("TreeMap range") {
  TreeMap<String, int> map;

  map.insert(zc::str("foo"), 1);
  map.insert(zc::str("bar"), 2);
  map.insert(zc::str("baz"), 3);
  map.insert(zc::str("qux"), 4);
  map.insert(zc::str("corge"), 5);

  {
    auto ordered = ZC_MAP(e, map)->zc::StringPtr { return e.key; };
    ZC_ASSERT(ordered.size() == 5);
    ZC_EXPECT(ordered[0] == "bar");
    ZC_EXPECT(ordered[1] == "baz");
    ZC_EXPECT(ordered[2] == "corge");
    ZC_EXPECT(ordered[3] == "foo");
    ZC_EXPECT(ordered[4] == "qux");
  }

  {
    auto range = map.range("baz", "foo");
    auto iter = range.begin();
    ZC_EXPECT(iter->key == "baz");
    ++iter;
    ZC_EXPECT(iter->key == "corge");
    ++iter;
    ZC_EXPECT(iter == range.end());
  }

  map.eraseRange("baz", "foo");

  {
    auto ordered = ZC_MAP(e, map)->zc::StringPtr { return e.key; };
    ZC_ASSERT(ordered.size() == 3);
    ZC_EXPECT(ordered[0] == "bar");
    ZC_EXPECT(ordered[1] == "foo");
    ZC_EXPECT(ordered[2] == "qux");
  }
}

ZC_TEST("HashMap findOrCreate throws") {
  HashMap<int, String> m;
  try {
    m.findOrCreate(1, []() -> HashMap<int, String>::Entry { throw "foo"; });
    ZC_FAIL_ASSERT("shouldn't get here");
  } catch (const char*) {
    // expected
  }

  ZC_EXPECT(m.find(1) == zc::none);
  m.findOrCreate(1, []() { return HashMap<int, String>::Entry{1, zc::str("ok")}; });

  ZC_EXPECT(ZC_ASSERT_NONNULL(m.find(1)) == "ok");
}

template <typename MapType>
void testEraseAll(MapType& m) {
  m.insert(12, "foo");
  m.insert(83, "bar");
  m.insert(99, "baz");
  m.insert(6, "qux");
  m.insert(55, "corge");

  auto count = m.eraseAll([](int i, StringPtr s) { return i == 99 || s == "foo"; });

  ZC_EXPECT(count == 2);
  ZC_EXPECT(m.size() == 3);
  ZC_EXPECT(m.find(12) == zc::none);
  ZC_EXPECT(m.find(99) == zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(m.find(83)) == "bar");
  ZC_EXPECT(ZC_ASSERT_NONNULL(m.find(6)) == "qux");
  ZC_EXPECT(ZC_ASSERT_NONNULL(m.find(55)) == "corge");
}

ZC_TEST("HashMap eraseAll") {
  HashMap<int, StringPtr> m;
  testEraseAll(m);
}

ZC_TEST("TreeMap eraseAll") {
  TreeMap<int, StringPtr> m;
  testEraseAll(m);
}

ZC_TEST("HashMap<uint64> with int key") {
  // Make sure searching for an `int` key in a `uint64_t` table works -- i.e., the hashes are
  // consistent even though the types differ.
  zc::HashMap<uint64_t, zc::StringPtr> map;
  map.insert((uint64_t)123, "foo"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(map.find((int)123)) == "foo"_zc);

  // But also make sure that the upper bits of a 64-bit integer do affect the hash.
  ZC_EXPECT(zc::hashCode(0x1200000001ull) != zc::hashCode(0x3400000001ull));
  ZC_EXPECT(zc::hashCode(0x1200000001ull) != zc::hashCode(1));
}

}  // namespace
}  // namespace _
}  // namespace zc
