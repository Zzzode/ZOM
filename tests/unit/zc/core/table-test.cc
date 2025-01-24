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

#include "zc/core/table.h"

#include <stdlib.h>
#include <zc/ztest/test.h>

#include <set>
#include <unordered_set>

#include "zc/core/hash.h"
#include "zc/core/time.h"

namespace zc {
namespace _ {
namespace {

#if defined(ZC_DEBUG) && !__OPTIMIZE__
static constexpr uint MEDIUM_PRIME = 619;
static constexpr uint BIG_PRIME = 6143;
#else
static constexpr uint MEDIUM_PRIME = 6143;
static constexpr uint BIG_PRIME = 101363;
#endif
// Some of the tests build large tables. These numbers are used as the table sizes. We use primes
// to avoid any unintended aliasing affects -- this is probably just paranoia, but why not?
//
// We use smaller values for debug builds to keep runtime down.

ZC_TEST("_::tryReserveSize() works") {
  {
    Vector<int> vec;
    tryReserveSize(vec, "foo"_zc);
    ZC_EXPECT(vec.capacity() == 4);  // Vectors always grow by powers of two.
  }
  {
    Vector<int> vec;
    tryReserveSize(vec, 123);
    ZC_EXPECT(vec.capacity() == 0);
  }
}

class StringHasher {
public:
  StringPtr keyForRow(StringPtr s) const { return s; }

  bool matches(StringPtr a, StringPtr b) const { return a == b; }
  uint hashCode(StringPtr str) const { return zc::hashCode(str); }
};

ZC_TEST("simple table") {
  Table<StringPtr, HashIndex<StringHasher>> table;

  ZC_EXPECT(table.find("foo") == zc::none);

  ZC_EXPECT(table.size() == 0);
  ZC_EXPECT(table.insert("foo") == "foo");
  ZC_EXPECT(table.size() == 1);
  ZC_EXPECT(table.insert("bar") == "bar");
  ZC_EXPECT(table.size() == 2);

  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("foo")) == "foo");
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("bar")) == "bar");
  ZC_EXPECT(table.find("fop") == zc::none);
  ZC_EXPECT(table.find("baq") == zc::none);

  {
    StringPtr& ref = table.insert("baz");
    ZC_EXPECT(ref == "baz");
    StringPtr& ref2 = ZC_ASSERT_NONNULL(table.find("baz"));
    ZC_EXPECT(&ref == &ref2);
  }

  ZC_EXPECT(table.size() == 3);

  {
    auto iter = table.begin();
    ZC_EXPECT(*iter++ == "foo");
    ZC_EXPECT(*iter++ == "bar");
    ZC_EXPECT(*iter++ == "baz");
    ZC_EXPECT(iter == table.end());
  }

  ZC_EXPECT(table.eraseMatch("foo"));
  ZC_EXPECT(table.size() == 2);
  ZC_EXPECT(table.find("foo") == zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("bar")) == "bar");
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("baz")) == "baz");

  {
    auto iter = table.begin();
    ZC_EXPECT(*iter++ == "baz");
    ZC_EXPECT(*iter++ == "bar");
    ZC_EXPECT(iter == table.end());
  }

  {
    auto& row =
        table.upsert("qux", [&](StringPtr&, StringPtr&&) { ZC_FAIL_ASSERT("shouldn't get here"); });

    auto copy = zc::str("qux");
    table.upsert(StringPtr(copy), [&](StringPtr& existing, StringPtr&& param) {
      ZC_EXPECT(param.begin() == copy.begin());
      ZC_EXPECT(&existing == &row);
    });

    auto& found = ZC_ASSERT_NONNULL(table.find("qux"));
    ZC_EXPECT(&found == &row);
  }

  StringPtr STRS[] = {"corge"_zc, "grault"_zc, "garply"_zc};
  table.insertAll(ArrayPtr<StringPtr>(STRS));
  ZC_EXPECT(table.size() == 6);
  ZC_EXPECT(table.find("corge") != zc::none);
  ZC_EXPECT(table.find("grault") != zc::none);
  ZC_EXPECT(table.find("garply") != zc::none);

  ZC_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert("bar"));

  ZC_EXPECT(table.size() == 6);

  ZC_EXPECT(table.insert("baa") == "baa");

  ZC_EXPECT(table.eraseAll([](StringPtr s) { return s.startsWith("ba"); }) == 3);
  ZC_EXPECT(table.size() == 4);

  {
    auto iter = table.begin();
    ZC_EXPECT(*iter++ == "garply");
    ZC_EXPECT(*iter++ == "grault");
    ZC_EXPECT(*iter++ == "qux");
    ZC_EXPECT(*iter++ == "corge");
    ZC_EXPECT(iter == table.end());
  }

  auto& graultRow = table.begin()[1];
  zc::StringPtr origGrault = graultRow;

  ZC_EXPECT(&table.findOrCreate("grault", [&]() -> zc::StringPtr {
    ZC_FAIL_ASSERT("shouldn't have called this");
  }) == &graultRow);
  ZC_EXPECT(graultRow.begin() == origGrault.begin());
  ZC_EXPECT(&ZC_ASSERT_NONNULL(table.find("grault")) == &graultRow);
  ZC_EXPECT(table.find("waldo") == zc::none);
  ZC_EXPECT(table.size() == 4);

  zc::String searchWaldo = zc::str("waldo");
  zc::String insertWaldo = zc::str("waldo");

  auto& waldo = table.findOrCreate(searchWaldo, [&]() -> zc::StringPtr { return insertWaldo; });
  ZC_EXPECT(waldo == "waldo");
  ZC_EXPECT(waldo.begin() == insertWaldo.begin());
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("grault")) == "grault");
  ZC_EXPECT(&ZC_ASSERT_NONNULL(table.find("waldo")) == &waldo);
  ZC_EXPECT(table.size() == 5);

  {
    auto iter = table.begin();
    ZC_EXPECT(*iter++ == "garply");
    ZC_EXPECT(*iter++ == "grault");
    ZC_EXPECT(*iter++ == "qux");
    ZC_EXPECT(*iter++ == "corge");
    ZC_EXPECT(*iter++ == "waldo");
    ZC_EXPECT(iter == table.end());
  }
}

class BadHasher {
  // String hash that always returns the same hash code. This should not affect correctness, only
  // performance.
public:
  StringPtr keyForRow(StringPtr s) const { return s; }

  bool matches(StringPtr a, StringPtr b) const { return a == b; }
  uint hashCode(StringPtr str) const { return 1234; }
};

ZC_TEST("hash tables when hash is always same") {
  Table<StringPtr, HashIndex<BadHasher>> table;

  ZC_EXPECT(table.size() == 0);
  ZC_EXPECT(table.insert("foo") == "foo");
  ZC_EXPECT(table.size() == 1);
  ZC_EXPECT(table.insert("bar") == "bar");
  ZC_EXPECT(table.size() == 2);

  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("foo")) == "foo");
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("bar")) == "bar");
  ZC_EXPECT(table.find("fop") == zc::none);
  ZC_EXPECT(table.find("baq") == zc::none);

  {
    StringPtr& ref = table.insert("baz");
    ZC_EXPECT(ref == "baz");
    StringPtr& ref2 = ZC_ASSERT_NONNULL(table.find("baz"));
    ZC_EXPECT(&ref == &ref2);
  }

  ZC_EXPECT(table.size() == 3);

  {
    auto iter = table.begin();
    ZC_EXPECT(*iter++ == "foo");
    ZC_EXPECT(*iter++ == "bar");
    ZC_EXPECT(*iter++ == "baz");
    ZC_EXPECT(iter == table.end());
  }

  ZC_EXPECT(table.eraseMatch("foo"));
  ZC_EXPECT(table.size() == 2);
  ZC_EXPECT(table.find("foo") == zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("bar")) == "bar");
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("baz")) == "baz");

  {
    auto iter = table.begin();
    ZC_EXPECT(*iter++ == "baz");
    ZC_EXPECT(*iter++ == "bar");
    ZC_EXPECT(iter == table.end());
  }

  {
    auto& row =
        table.upsert("qux", [&](StringPtr&, StringPtr&&) { ZC_FAIL_ASSERT("shouldn't get here"); });

    auto copy = zc::str("qux");
    table.upsert(StringPtr(copy), [&](StringPtr& existing, StringPtr&& param) {
      ZC_EXPECT(param.begin() == copy.begin());
      ZC_EXPECT(&existing == &row);
    });

    auto& found = ZC_ASSERT_NONNULL(table.find("qux"));
    ZC_EXPECT(&found == &row);
  }

  StringPtr STRS[] = {"corge"_zc, "grault"_zc, "garply"_zc};
  table.insertAll(ArrayPtr<StringPtr>(STRS));
  ZC_EXPECT(table.size() == 6);
  ZC_EXPECT(table.find("corge") != zc::none);
  ZC_EXPECT(table.find("grault") != zc::none);
  ZC_EXPECT(table.find("garply") != zc::none);

  ZC_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert("bar"));
}

class IntHasher {
  // Dumb integer hasher that just returns the integer itself.
public:
  uint keyForRow(uint i) const { return i; }

  bool matches(uint a, uint b) const { return a == b; }
  uint hashCode(uint i) const { return zc::hashCode(i); }
};

ZC_TEST("HashIndex with many erasures doesn't keep growing") {
  HashIndex<IntHasher> index;

  zc::ArrayPtr<uint> rows = nullptr;

  for (uint i : zc::zeroTo(1000000)) {
    ZC_ASSERT(index.insert(rows, 0, i) == zc::none);
    index.erase(rows, 0, i);
  }

  ZC_ASSERT(index.capacity() < 10);
}

struct SiPair {
  zc::StringPtr str;
  uint i;

  inline bool operator==(SiPair other) const { return str == other.str && i == other.i; }
};

class SiPairStringHasher {
public:
  StringPtr keyForRow(SiPair s) const { return s.str; }

  bool matches(SiPair a, StringPtr b) const { return a.str == b; }
  uint hashCode(StringPtr str) const { return inner.hashCode(str); }

private:
  StringHasher inner;
};

class SiPairIntHasher {
public:
  uint keyForRow(SiPair s) const { return s.i; }

  bool matches(SiPair a, uint b) const { return a.i == b; }
  uint hashCode(uint i) const { return inner.hashCode(i); }

private:
  IntHasher inner;
};

ZC_TEST("double-index table") {
  Table<SiPair, HashIndex<SiPairStringHasher>, HashIndex<SiPairIntHasher>> table;

  ZC_EXPECT(table.size() == 0);
  ZC_EXPECT(table.insert({"foo", 123}) == (SiPair{"foo", 123}));
  ZC_EXPECT(table.size() == 1);
  ZC_EXPECT(table.insert({"bar", 456}) == (SiPair{"bar", 456}));
  ZC_EXPECT(table.size() == 2);

  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<HashIndex<SiPairStringHasher>>("foo")) ==
            (SiPair{"foo", 123}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<HashIndex<SiPairIntHasher>>(123)) == (SiPair{"foo", 123}));

  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<0>("foo")) == (SiPair{"foo", 123}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<1>(123)) == (SiPair{"foo", 123}));

  ZC_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert({"foo", 111}));
  ZC_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert({"qux", 123}));

  ZC_EXPECT(table.size() == 2);
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<0>("foo")) == (SiPair{"foo", 123}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<1>(123)) == (SiPair{"foo", 123}));

  ZC_EXPECT(table.findOrCreate<0>("foo", []() -> SiPair {
    ZC_FAIL_ASSERT("shouldn't have called this");
  }) == (SiPair{"foo", 123}));
  ZC_EXPECT(table.size() == 2);
  ZC_EXPECT_THROW_MESSAGE(
      "inserted row already exists in table",
      table.findOrCreate<0>("corge", []() -> SiPair { return {"corge", 123}; }));

  ZC_EXPECT(table.size() == 2);
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<0>("foo")) == (SiPair{"foo", 123}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<1>(123)) == (SiPair{"foo", 123}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<0>("bar")) == (SiPair{"bar", 456}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<1>(456)) == (SiPair{"bar", 456}));
  ZC_EXPECT(table.find<0>("corge") == zc::none);

  ZC_EXPECT(table.findOrCreate<0>("corge", []() -> SiPair { return {"corge", 789}; }) ==
            (SiPair{"corge", 789}));

  ZC_EXPECT(table.size() == 3);
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<0>("foo")) == (SiPair{"foo", 123}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<1>(123)) == (SiPair{"foo", 123}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<0>("bar")) == (SiPair{"bar", 456}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<1>(456)) == (SiPair{"bar", 456}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<0>("corge")) == (SiPair{"corge", 789}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<1>(789)) == (SiPair{"corge", 789}));

  ZC_EXPECT(table.findOrCreate<1>(234, []() -> SiPair { return {"grault", 234}; }) ==
            (SiPair{"grault", 234}));

  ZC_EXPECT(table.size() == 4);
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<0>("foo")) == (SiPair{"foo", 123}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<1>(123)) == (SiPair{"foo", 123}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<0>("bar")) == (SiPair{"bar", 456}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<1>(456)) == (SiPair{"bar", 456}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<0>("corge")) == (SiPair{"corge", 789}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<1>(789)) == (SiPair{"corge", 789}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<0>("grault")) == (SiPair{"grault", 234}));
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find<1>(234)) == (SiPair{"grault", 234}));
}

class UintHasher {
public:
  uint keyForRow(uint i) const { return i; }

  bool matches(uint a, uint b) const { return a == b; }
  uint hashCode(uint i) const { return zc::hashCode(i); }
};

ZC_TEST("benchmark: zc::Table<uint, HashIndex>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  for (auto step : STEP) {
    ZC_CONTEXT(step);
    Table<uint, HashIndex<UintHasher>> table;
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      uint value = ZC_ASSERT_NONNULL(table.find(i * 5 + 123));
      ZC_ASSERT(value == i * 5 + 123);
      ZC_ASSERT(table.find(i * 5 + 122) == zc::none);
      ZC_ASSERT(table.find(i * 5 + 124) == zc::none);
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) { table.erase(ZC_ASSERT_NONNULL(table.find(i * 5 + 123))); }
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        ZC_ASSERT(table.find(i * 5 + 123) == zc::none);
      } else {
        uint value = ZC_ASSERT_NONNULL(table.find(i * 5 + 123));
        ZC_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

ZC_TEST("benchmark: std::unordered_set<uint>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  for (auto step : STEP) {
    ZC_CONTEXT(step);
    std::unordered_set<uint> table;
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      auto iter = table.find(i * 5 + 123);
      ZC_ASSERT(iter != table.end());
      uint value = *iter;
      ZC_ASSERT(value == i * 5 + 123);
      ZC_ASSERT(table.find(i * 5 + 122) == table.end());
      ZC_ASSERT(table.find(i * 5 + 124) == table.end());
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) { ZC_ASSERT(table.erase(i * 5 + 123) > 0); }
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        ZC_ASSERT(table.find(i * 5 + 123) == table.end());
      } else {
        auto iter = table.find(i * 5 + 123);
        ZC_ASSERT(iter != table.end());
        uint value = *iter;
        ZC_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

ZC_TEST("benchmark: zc::Table<StringPtr, HashIndex>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  zc::Vector<String> strings(SOME_PRIME);
  for (uint i : zc::zeroTo(SOME_PRIME)) { strings.add(zc::str(i * 5 + 123)); }

  for (auto step : STEP) {
    ZC_CONTEXT(step);
    Table<StringPtr, HashIndex<StringHasher>> table;
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(strings[j]);
    }
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      StringPtr value = ZC_ASSERT_NONNULL(table.find(strings[i]));
      ZC_ASSERT(value == strings[i]);
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) { table.erase(ZC_ASSERT_NONNULL(table.find(strings[i]))); }
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        ZC_ASSERT(table.find(strings[i]) == zc::none);
      } else {
        StringPtr value = ZC_ASSERT_NONNULL(table.find(strings[i]));
        ZC_ASSERT(value == strings[i]);
      }
    }
  }
}

struct StlStringHash {
  inline size_t operator()(StringPtr str) const { return zc::hashCode(str); }
};

ZC_TEST("benchmark: std::unordered_set<StringPtr>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  zc::Vector<String> strings(SOME_PRIME);
  for (uint i : zc::zeroTo(SOME_PRIME)) { strings.add(zc::str(i * 5 + 123)); }

  for (auto step : STEP) {
    ZC_CONTEXT(step);
    std::unordered_set<StringPtr, StlStringHash> table;
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(strings[j]);
    }
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      auto iter = table.find(strings[i]);
      ZC_ASSERT(iter != table.end());
      StringPtr value = *iter;
      ZC_ASSERT(value == strings[i]);
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) { ZC_ASSERT(table.erase(strings[i]) > 0); }
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        ZC_ASSERT(table.find(strings[i]) == table.end());
      } else {
        auto iter = table.find(strings[i]);
        ZC_ASSERT(iter != table.end());
        StringPtr value = *iter;
        ZC_ASSERT(value == strings[i]);
      }
    }
  }
}

// =======================================================================================

ZC_TEST("B-tree internals") {
  {
    BTreeImpl::Leaf leaf;
    memset(&leaf, 0, sizeof(leaf));

    for (auto i : zc::indices(leaf.rows)) {
      ZC_CONTEXT(i);

      ZC_EXPECT(leaf.size() == i);

      if (i < zc::size(leaf.rows) / 2) {
#ifdef ZC_DEBUG
        ZC_EXPECT_THROW(FAILED, leaf.isHalfFull());
#endif
        ZC_EXPECT(!leaf.isMostlyFull());
      }

      if (i == zc::size(leaf.rows) / 2) {
        ZC_EXPECT(leaf.isHalfFull());
        ZC_EXPECT(!leaf.isMostlyFull());
      }

      if (i > zc::size(leaf.rows) / 2) {
        ZC_EXPECT(!leaf.isHalfFull());
        ZC_EXPECT(leaf.isMostlyFull());
      }

      if (i == zc::size(leaf.rows)) {
        ZC_EXPECT(leaf.isFull());
      } else {
        ZC_EXPECT(!leaf.isFull());
      }

      leaf.rows[i] = 1;
    }
    ZC_EXPECT(leaf.size() == zc::size(leaf.rows));
  }

  {
    BTreeImpl::Parent parent;
    memset(&parent, 0, sizeof(parent));

    for (auto i : zc::indices(parent.keys)) {
      ZC_CONTEXT(i);

      ZC_EXPECT(parent.keyCount() == i);

      if (i < zc::size(parent.keys) / 2) {
#ifdef ZC_DEBUG
        ZC_EXPECT_THROW(FAILED, parent.isHalfFull());
#endif
        ZC_EXPECT(!parent.isMostlyFull());
      }

      if (i == zc::size(parent.keys) / 2) {
        ZC_EXPECT(parent.isHalfFull());
        ZC_EXPECT(!parent.isMostlyFull());
      }

      if (i > zc::size(parent.keys) / 2) {
        ZC_EXPECT(!parent.isHalfFull());
        ZC_EXPECT(parent.isMostlyFull());
      }

      if (i == zc::size(parent.keys)) {
        ZC_EXPECT(parent.isFull());
      } else {
        ZC_EXPECT(!parent.isFull());
      }

      parent.keys[i] = 1;
    }
    ZC_EXPECT(parent.keyCount() == zc::size(parent.keys));
  }
}

class StringCompare {
public:
  StringPtr keyForRow(StringPtr s) const { return s; }

  bool isBefore(StringPtr a, StringPtr b) const { return a < b; }
  bool matches(StringPtr a, StringPtr b) const { return a == b; }
};

ZC_TEST("simple tree table") {
  Table<StringPtr, TreeIndex<StringCompare>> table;

  ZC_EXPECT(table.find("foo") == zc::none);

  ZC_EXPECT(table.size() == 0);
  ZC_EXPECT(table.insert("foo") == "foo");
  ZC_EXPECT(table.size() == 1);
  ZC_EXPECT(table.insert("bar") == "bar");
  ZC_EXPECT(table.size() == 2);

  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("foo")) == "foo");
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("bar")) == "bar");
  ZC_EXPECT(table.find("fop") == zc::none);
  ZC_EXPECT(table.find("baq") == zc::none);

  {
    StringPtr& ref = table.insert("baz");
    ZC_EXPECT(ref == "baz");
    StringPtr& ref2 = ZC_ASSERT_NONNULL(table.find("baz"));
    ZC_EXPECT(&ref == &ref2);
  }

  ZC_EXPECT(table.size() == 3);

  {
    auto range = table.ordered();
    auto iter = range.begin();
    ZC_EXPECT(*iter++ == "bar");
    ZC_EXPECT(*iter++ == "baz");
    ZC_EXPECT(*iter++ == "foo");
    ZC_EXPECT(iter == range.end());
  }

  ZC_EXPECT(table.eraseMatch("foo"));
  ZC_EXPECT(table.size() == 2);
  ZC_EXPECT(table.find("foo") == zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("bar")) == "bar");
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("baz")) == "baz");

  {
    auto range = table.ordered();
    auto iter = range.begin();
    ZC_EXPECT(*iter++ == "bar");
    ZC_EXPECT(*iter++ == "baz");
    ZC_EXPECT(iter == range.end());
  }

  {
    auto& row =
        table.upsert("qux", [&](StringPtr&, StringPtr&&) { ZC_FAIL_ASSERT("shouldn't get here"); });

    auto copy = zc::str("qux");
    table.upsert(StringPtr(copy), [&](StringPtr& existing, StringPtr&& param) {
      ZC_EXPECT(param.begin() == copy.begin());
      ZC_EXPECT(&existing == &row);
    });

    auto& found = ZC_ASSERT_NONNULL(table.find("qux"));
    ZC_EXPECT(&found == &row);
  }

  StringPtr STRS[] = {"corge"_zc, "grault"_zc, "garply"_zc};
  table.insertAll(ArrayPtr<StringPtr>(STRS));
  ZC_EXPECT(table.size() == 6);
  ZC_EXPECT(table.find("corge") != zc::none);
  ZC_EXPECT(table.find("grault") != zc::none);
  ZC_EXPECT(table.find("garply") != zc::none);

  ZC_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert("bar"));

  ZC_EXPECT(table.size() == 6);

  ZC_EXPECT(table.insert("baa") == "baa");

  ZC_EXPECT(table.eraseAll([](StringPtr s) { return s.startsWith("ba"); }) == 3);
  ZC_EXPECT(table.size() == 4);

  {
    auto range = table.ordered();
    auto iter = range.begin();
    ZC_EXPECT(*iter++ == "corge");
    ZC_EXPECT(*iter++ == "garply");
    ZC_EXPECT(*iter++ == "grault");
    ZC_EXPECT(*iter++ == "qux");
    ZC_EXPECT(iter == range.end());
  }

  {
    auto range = table.range("foo", "har");
    auto iter = range.begin();
    ZC_EXPECT(*iter++ == "garply");
    ZC_EXPECT(*iter++ == "grault");
    ZC_EXPECT(iter == range.end());
  }

  {
    auto range = table.range("garply", "grault");
    auto iter = range.begin();
    ZC_EXPECT(*iter++ == "garply");
    ZC_EXPECT(iter == range.end());
  }

  {
    auto iter = table.seek("garply");
    ZC_EXPECT(*iter++ == "garply");
    ZC_EXPECT(*iter++ == "grault");
    ZC_EXPECT(*iter++ == "qux");
    ZC_EXPECT(iter == table.ordered().end());
  }

  {
    auto iter = table.seek("gorply");
    ZC_EXPECT(*iter++ == "grault");
    ZC_EXPECT(*iter++ == "qux");
    ZC_EXPECT(iter == table.ordered().end());
  }

  auto& graultRow = table.begin()[1];
  zc::StringPtr origGrault = graultRow;

  ZC_EXPECT(&table.findOrCreate("grault", [&]() -> zc::StringPtr {
    ZC_FAIL_ASSERT("shouldn't have called this");
  }) == &graultRow);
  ZC_EXPECT(graultRow.begin() == origGrault.begin());
  ZC_EXPECT(&ZC_ASSERT_NONNULL(table.find("grault")) == &graultRow);
  ZC_EXPECT(table.find("waldo") == zc::none);
  ZC_EXPECT(table.size() == 4);

  zc::String searchWaldo = zc::str("waldo");
  zc::String insertWaldo = zc::str("waldo");

  auto& waldo = table.findOrCreate(searchWaldo, [&]() -> zc::StringPtr { return insertWaldo; });
  ZC_EXPECT(waldo == "waldo");
  ZC_EXPECT(waldo.begin() == insertWaldo.begin());
  ZC_EXPECT(ZC_ASSERT_NONNULL(table.find("grault")) == "grault");
  ZC_EXPECT(&ZC_ASSERT_NONNULL(table.find("waldo")) == &waldo);
  ZC_EXPECT(table.size() == 5);

  {
    auto iter = table.begin();
    ZC_EXPECT(*iter++ == "garply");
    ZC_EXPECT(*iter++ == "grault");
    ZC_EXPECT(*iter++ == "qux");
    ZC_EXPECT(*iter++ == "corge");
    ZC_EXPECT(*iter++ == "waldo");
    ZC_EXPECT(iter == table.end());
  }

  // Verify that move constructor/assignment work.
  Table<StringPtr, TreeIndex<StringCompare>> other(zc::mv(table));
  ZC_EXPECT(other.size() == 5);
  ZC_EXPECT(table.size() == 0);
  ZC_EXPECT(table.begin() == table.end());
  {
    auto iter = other.begin();
    ZC_EXPECT(*iter++ == "garply");
    ZC_EXPECT(*iter++ == "grault");
    ZC_EXPECT(*iter++ == "qux");
    ZC_EXPECT(*iter++ == "corge");
    ZC_EXPECT(*iter++ == "waldo");
    ZC_EXPECT(iter == other.end());
  }

  table = zc::mv(other);
  ZC_EXPECT(other.size() == 0);
  ZC_EXPECT(table.size() == 5);
  {
    auto iter = table.begin();
    ZC_EXPECT(*iter++ == "garply");
    ZC_EXPECT(*iter++ == "grault");
    ZC_EXPECT(*iter++ == "qux");
    ZC_EXPECT(*iter++ == "corge");
    ZC_EXPECT(*iter++ == "waldo");
    ZC_EXPECT(iter == table.end());
  }
  ZC_EXPECT(other.begin() == other.end());
}

class UintCompare {
public:
  uint keyForRow(uint i) const { return i; }

  bool isBefore(uint a, uint b) const { return a < b; }
  bool matches(uint a, uint b) const { return a == b; }
};

ZC_TEST("large tree table") {
  constexpr uint SOME_PRIME = MEDIUM_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  for (auto step : STEP) {
    ZC_CONTEXT(step);
    Table<uint, TreeIndex<UintCompare>> table;
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      uint value = ZC_ASSERT_NONNULL(table.find(i * 5 + 123));
      ZC_ASSERT(value == i * 5 + 123);
      ZC_ASSERT(table.find(i * 5 + 122) == zc::none);
      ZC_ASSERT(table.find(i * 5 + 124) == zc::none);
    }
    table.verify();

    {
      auto range = table.ordered();
      auto iter = range.begin();
      for (uint i : zc::zeroTo(SOME_PRIME)) { ZC_ASSERT(*iter++ == i * 5 + 123); }
      ZC_ASSERT(iter == range.end());
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      ZC_CONTEXT(i);
      if (i % 2 == 0 || i % 7 == 0) {
        table.erase(ZC_ASSERT_NONNULL(table.find(i * 5 + 123), i));
        table.verify();
      }
    }

    {
      auto range = table.ordered();
      auto iter = range.begin();
      for (uint i : zc::zeroTo(SOME_PRIME)) {
        if (i % 2 == 0 || i % 7 == 0) {
          // erased
          ZC_ASSERT(table.find(i * 5 + 123) == zc::none);
        } else {
          uint value = ZC_ASSERT_NONNULL(table.find(i * 5 + 123));
          ZC_ASSERT(value == i * 5 + 123);
          ZC_ASSERT(*iter++ == i * 5 + 123);
        }
      }
      ZC_ASSERT(iter == range.end());
    }
  }
}

ZC_TEST("TreeIndex fuzz test") {
  // A test which randomly modifies a TreeIndex to try to discover buggy state changes.

  uint seed = (zc::systemPreciseCalendarClock().now() - zc::UNIX_EPOCH) / zc::NANOSECONDS;
  ZC_CONTEXT(seed);  // print the seed if the test fails
  srand(seed);

  Table<uint, TreeIndex<UintCompare>> table;

  auto randomInsert = [&]() { table.upsert(rand(), [](auto&&, auto&&) {}); };
  auto randomErase = [&]() {
    if (table.size() > 0) {
      auto& row = table.begin()[rand() % table.size()];
      table.erase(row);
    }
  };
  auto randomLookup = [&]() {
    if (table.size() > 0) {
      auto& row = table.begin()[rand() % table.size()];
      auto& found = ZC_ASSERT_NONNULL(table.find(row));
      ZC_ASSERT(&found == &row);
    }
  };

  // First pass: focus on insertions, aim to do 2x as many insertions as deletions.
  for (auto i ZC_UNUSED : zc::zeroTo(1000)) {
    switch (rand() % 4) {
      case 0:
      case 1:
        randomInsert();
        break;
      case 2:
        randomErase();
        break;
      case 3:
        randomLookup();
        break;
    }

    table.verify();
  }

  // Second pass: focus on deletions, aim to do 2x as many deletions as insertions.
  for (auto i ZC_UNUSED : zc::zeroTo(1000)) {
    switch (rand() % 4) {
      case 0:
        randomInsert();
        break;
      case 1:
      case 2:
        randomErase();
        break;
      case 3:
        randomLookup();
        break;
    }

    table.verify();
  }
}

ZC_TEST("TreeIndex clear() leaves tree in valid state") {
  // A test which ensures that calling clear() does not break the internal state of a TreeIndex.
  // It used to be the case that clearing a non-empty tree would leave it thinking that it had room
  // for one more node than it really did, causing it to write and read beyond the end of its
  // internal array of nodes.
  Table<uint, TreeIndex<UintCompare>> table;

  // Insert at least one value to allocate an initial set of tree nodes.
  table.upsert(1, [](auto&&, auto&&) {});
  ZC_EXPECT(table.find(1) != zc::none);
  table.clear();

  // Insert enough values to force writes/reads beyond the end of the tree's internal node array.
  for (uint i = 0; i < 29; ++i) {
    table.upsert(i, [](auto&&, auto&&) {});
  }
  for (uint i = 0; i < 29; ++i) { ZC_EXPECT(table.find(i) != zc::none); }
}

ZC_TEST("benchmark: zc::Table<uint, TreeIndex>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  for (auto step : STEP) {
    ZC_CONTEXT(step);
    Table<uint, TreeIndex<UintCompare>> table;
    table.reserve(SOME_PRIME);
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      uint value = ZC_ASSERT_NONNULL(table.find(i * 5 + 123));
      ZC_ASSERT(value == i * 5 + 123);
      ZC_ASSERT(table.find(i * 5 + 122) == zc::none);
      ZC_ASSERT(table.find(i * 5 + 124) == zc::none);
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) { table.erase(ZC_ASSERT_NONNULL(table.find(i * 5 + 123))); }
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        ZC_ASSERT(table.find(i * 5 + 123) == zc::none);
      } else {
        uint value = ZC_ASSERT_NONNULL(table.find(i * 5 + 123));
        ZC_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

ZC_TEST("benchmark: std::set<uint>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  for (auto step : STEP) {
    ZC_CONTEXT(step);
    std::set<uint> table;
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      auto iter = table.find(i * 5 + 123);
      ZC_ASSERT(iter != table.end());
      uint value = *iter;
      ZC_ASSERT(value == i * 5 + 123);
      ZC_ASSERT(table.find(i * 5 + 122) == table.end());
      ZC_ASSERT(table.find(i * 5 + 124) == table.end());
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) { ZC_ASSERT(table.erase(i * 5 + 123) > 0); }
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        ZC_ASSERT(table.find(i * 5 + 123) == table.end());
      } else {
        auto iter = table.find(i * 5 + 123);
        ZC_ASSERT(iter != table.end());
        uint value = *iter;
        ZC_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

ZC_TEST("benchmark: zc::Table<StringPtr, TreeIndex>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  zc::Vector<String> strings(SOME_PRIME);
  for (uint i : zc::zeroTo(SOME_PRIME)) { strings.add(zc::str(i * 5 + 123)); }

  for (auto step : STEP) {
    ZC_CONTEXT(step);
    Table<StringPtr, TreeIndex<StringCompare>> table;
    table.reserve(SOME_PRIME);
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(strings[j]);
    }
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      StringPtr value = ZC_ASSERT_NONNULL(table.find(strings[i]));
      ZC_ASSERT(value == strings[i]);
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) { table.erase(ZC_ASSERT_NONNULL(table.find(strings[i]))); }
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        ZC_ASSERT(table.find(strings[i]) == zc::none);
      } else {
        auto& value = ZC_ASSERT_NONNULL(table.find(strings[i]));
        ZC_ASSERT(value == strings[i]);
      }
    }
  }
}

ZC_TEST("benchmark: std::set<StringPtr>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  zc::Vector<String> strings(SOME_PRIME);
  for (uint i : zc::zeroTo(SOME_PRIME)) { strings.add(zc::str(i * 5 + 123)); }

  for (auto step : STEP) {
    ZC_CONTEXT(step);
    std::set<StringPtr> table;
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(strings[j]);
    }
    for (uint i : zc::zeroTo(SOME_PRIME)) {
      auto iter = table.find(strings[i]);
      ZC_ASSERT(iter != table.end());
      StringPtr value = *iter;
      ZC_ASSERT(value == strings[i]);
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) { ZC_ASSERT(table.erase(strings[i]) > 0); }
    }

    for (uint i : zc::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        ZC_ASSERT(table.find(strings[i]) == table.end());
      } else {
        auto iter = table.find(strings[i]);
        ZC_ASSERT(iter != table.end());
        StringPtr value = *iter;
        ZC_ASSERT(value == strings[i]);
      }
    }
  }
}

// =======================================================================================

ZC_TEST("insertion order index") {
  Table<uint, InsertionOrderIndex> table;

  {
    auto range = table.ordered();
    ZC_EXPECT(range.begin() == range.end());
  }

  table.insert(12);
  table.insert(34);
  table.insert(56);
  table.insert(78);

  {
    auto range = table.ordered();
    auto iter = range.begin();
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 12);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 34);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 56);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 78);
    ZC_EXPECT(iter == range.end());
    ZC_EXPECT(*--iter == 78);
    ZC_EXPECT(*--iter == 56);
    ZC_EXPECT(*--iter == 34);
    ZC_EXPECT(*--iter == 12);
    ZC_EXPECT(iter == range.begin());
  }

  table.erase(table.begin()[1]);

  {
    auto range = table.ordered();
    auto iter = range.begin();
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 12);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 56);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 78);
    ZC_EXPECT(iter == range.end());
    ZC_EXPECT(*--iter == 78);
    ZC_EXPECT(*--iter == 56);
    ZC_EXPECT(*--iter == 12);
    ZC_EXPECT(iter == range.begin());
  }

  // Allocate enough more elements to cause a resize.
  table.insert(111);
  table.insert(222);
  table.insert(333);
  table.insert(444);
  table.insert(555);
  table.insert(666);
  table.insert(777);
  table.insert(888);
  table.insert(999);

  {
    auto range = table.ordered();
    auto iter = range.begin();
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 12);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 56);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 78);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 111);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 222);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 333);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 444);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 555);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 666);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 777);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 888);
    ZC_ASSERT(iter != range.end());
    ZC_EXPECT(*iter++ == 999);
    ZC_EXPECT(iter == range.end());
  }

  // Remove everything.
  while (table.size() > 0) { table.erase(*table.begin()); }

  {
    auto range = table.ordered();
    ZC_EXPECT(range.begin() == range.end());
  }
}

ZC_TEST("insertion order index is movable") {
  using UintTable = Table<uint, InsertionOrderIndex>;

  zc::Maybe<UintTable> myTable;

  {
    UintTable yourTable;

    yourTable.insert(12);
    yourTable.insert(34);
    yourTable.insert(56);
    yourTable.insert(78);
    yourTable.insert(111);
    yourTable.insert(222);
    yourTable.insert(333);
    yourTable.insert(444);
    yourTable.insert(555);
    yourTable.insert(666);
    yourTable.insert(777);
    yourTable.insert(888);
    yourTable.insert(999);

    myTable = zc::mv(yourTable);
  }

  auto& table = ZC_ASSERT_NONNULL(myTable);

  // At one time the following induced a segfault/double-free, due to incorrect memory management in
  // InsertionOrderIndex's move ctor and dtor.
  auto range = table.ordered();
  auto iter = range.begin();
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 12);
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 34);
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 56);
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 78);
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 111);
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 222);
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 333);
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 444);
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 555);
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 666);
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 777);
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 888);
  ZC_ASSERT(iter != range.end());
  ZC_EXPECT(*iter++ == 999);
  ZC_EXPECT(iter == range.end());
}

// =======================================================================================
// Test bug where insertion failure on a later index in the table would not be rolled back
// correctly if a previous index was TreeIndex.

class StringLengthCompare {
  // Considers two strings equal if they have the same length.
public:
  inline size_t keyForRow(StringPtr entry) const { return entry.size(); }

  inline bool matches(StringPtr e, size_t key) const { return e.size() == key; }

  inline bool isBefore(StringPtr e, size_t key) const { return e.size() < key; }

  uint hashCode(size_t size) const { return zc::hashCode(size); }
};

ZC_TEST("HashIndex rollback on insertion failure") {
  // Test that when an insertion produces a duplicate on a later index, changes to previous indexes
  // are properly rolled back.

  Table<StringPtr, HashIndex<StringHasher>, HashIndex<StringLengthCompare>> table;
  table.insert("a"_zc);
  table.insert("ab"_zc);
  table.insert("abc"_zc);

  {
    // We use upsert() so that we don't throw an exception from the duplicate, but this exercises
    // the same logic as a duplicate insert() other than throwing.
    zc::StringPtr& found = table.upsert("xyz"_zc, [&](StringPtr& existing, StringPtr&& param) {
      ZC_EXPECT(existing == "abc");
      ZC_EXPECT(param == "xyz");
    });
    ZC_EXPECT(found == "abc");

    table.erase(found);
  }

  table.insert("xyz"_zc);

  {
    zc::StringPtr& found = table.upsert("tuv"_zc, [&](StringPtr& existing, StringPtr&& param) {
      ZC_EXPECT(existing == "xyz");
      ZC_EXPECT(param == "tuv");
    });
    ZC_EXPECT(found == "xyz");
  }
}

ZC_TEST("TreeIndex rollback on insertion failure") {
  // Test that when an insertion produces a duplicate on a later index, changes to previous indexes
  // are properly rolled back.

  Table<StringPtr, TreeIndex<StringCompare>, TreeIndex<StringLengthCompare>> table;
  table.insert("a"_zc);
  table.insert("ab"_zc);
  table.insert("abc"_zc);

  {
    // We use upsert() so that we don't throw an exception from the duplicate, but this exercises
    // the same logic as a duplicate insert() other than throwing.
    zc::StringPtr& found = table.upsert("xyz"_zc, [&](StringPtr& existing, StringPtr&& param) {
      ZC_EXPECT(existing == "abc");
      ZC_EXPECT(param == "xyz");
    });
    ZC_EXPECT(found == "abc");

    table.erase(found);
  }

  table.insert("xyz"_zc);

  {
    zc::StringPtr& found = table.upsert("tuv"_zc, [&](StringPtr& existing, StringPtr&& param) {
      ZC_EXPECT(existing == "xyz");
      ZC_EXPECT(param == "tuv");
    });
    ZC_EXPECT(found == "xyz");
  }
}

}  // namespace
}  // namespace _
}  // namespace zc
