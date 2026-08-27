// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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

#include "zomlang/compiler/basic/string-pool.h"

#include "zc/ztest/test.h"

namespace zomlang {
namespace compiler {
namespace basic {
namespace {

ZC_TEST("StringPool intern StringPtr") {
  StringPool pool;
  zc::StringPtr s1 = "hello"_zc;
  zc::StringPtr s2 = pool.intern(s1);

  ZC_EXPECT(s1 == s2);
  ZC_EXPECT(s1.cStr() != s2.cStr());               // Should be a copy
  ZC_EXPECT(pool.intern(s1).cStr() == s2.cStr());  // Should return existing instance
}

ZC_TEST("StringPool intern ArrayPtr") {
  StringPool pool;
  const char buffer[] = "hello world";
  zc::ArrayPtr<const char> arr = zc::arrayPtr(buffer, 5);  // "hello"

  zc::StringPtr s = pool.intern(arr);
  ZC_EXPECT(s == "hello"_zc);
  ZC_EXPECT(pool.intern(arr).cStr() == s.cStr());
}

ZC_TEST("StringPool intern char") {
  StringPool pool;
  zc::StringPtr s = pool.intern('a');
  ZC_EXPECT(s == "a"_zc);
  ZC_EXPECT(pool.intern('a').cStr() == s.cStr());
}

ZC_TEST("StringPool intern variadic") {
  StringPool pool;
  zc::StringPtr s = pool.intern("hello", " ", "world");
  ZC_EXPECT(s == "hello world"_zc);
  ZC_EXPECT(pool.intern("hello", " ", "world").cStr() == s.cStr());

  zc::StringPtr num = pool.intern(123);
  ZC_EXPECT(num == "123"_zc);

  zc::StringPtr mixed = pool.intern("value: ", 42);
  ZC_EXPECT(mixed == "value: 42"_zc);
}

ZC_TEST("StringPool clear") {
  StringPool pool;
  pool.intern("hello");

  pool.clear();

  // Get the address of the re-interned string
  const char* addr2 = pool.intern("hello").cStr();

  // The content should be the same
  ZC_EXPECT(strcmp(addr2, "hello") == 0);

  // Since we reconstruct the arena, the addresses might be different or the same
  // depending on the allocator, but the logic is that the pool was reset.
  // However, accessing addr1 is strictly speaking "use-after-free" if the arena truly freed it.
  // So we shouldn't access addr1's content, but comparing pointer values is allowed.

  // A better test for clear() is to verify it forgets previous interns.
  // We can do this by interning a pointer, clearing, and checking if the new intern
  // returns a different address (likely, but not guaranteed by C++) OR
  // checking that we can intern a lot of strings without memory blowing up (hard to test here).

  // Let's test that we can continue to use the pool after clear.
  zc::StringPtr s3 = pool.intern("world");
  ZC_EXPECT(s3 == "world"_zc);
}

ZC_TEST("StringPool empty string") {
  StringPool pool;
  zc::StringPtr s = pool.intern("");
  ZC_EXPECT(s.size() == 0);
  ZC_EXPECT(s == ""_zc);

  // Ensure consistent behavior for empty string
  ZC_EXPECT(pool.intern(zc::StringPtr(nullptr)).size() == 0);
  ZC_EXPECT(pool.intern(zc::ArrayPtr<const char>(nullptr, size_t(0))).size() == 0);
}

ZC_TEST("StringPool large volume") {
  StringPool pool;
  for (int i = 0; i < 1000; ++i) { pool.intern(zc::str("string_", i)); }

  // Verify one of them
  ZC_EXPECT(pool.intern("string_500") == "string_500"_zc);
}

ZC_TEST("StringPool overload priority") {
  StringPool pool;

  // Should pick intern(const char*)
  zc::StringPtr s1 = pool.intern("hello");
  ZC_EXPECT(s1 == "hello"_zc);

  // Should pick intern(char)
  zc::StringPtr s2 = pool.intern('c');
  ZC_EXPECT(s2 == "c"_zc);

  // Should pick variadic template
  zc::StringPtr s3 = pool.intern("a", "b");
  ZC_EXPECT(s3 == "ab"_zc);

  // Should pick intern(StringPtr)
  zc::StringPtr s4 = pool.intern("world"_zc);
  ZC_EXPECT(s4 == "world"_zc);
}

}  // namespace
}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
