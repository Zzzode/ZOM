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

#pragma once

#include <type_traits>

#include "zc/core/arena.h"
#include "zc/core/common.h"
#include "zc/core/hash.h"
#include "zc/core/mutex.h"
#include "zc/core/string.h"
#include "zc/core/table.h"

namespace zomlang {
namespace compiler {
namespace basic {

class StringPool {
public:
  // Creates a new string pool.
  explicit StringPool();
  ZC_DISALLOW_COPY_AND_MOVE(StringPool);

  // Interns a string into the pool.
  // Returns a pointer to the interned string.
  // If the string is already in the pool, the existing instance is returned.
  zc::StringPtr intern(zc::StringPtr str);

  // Interns a string from an ArrayPtr (not necessarily NUL-terminated).
  zc::StringPtr intern(zc::ArrayPtr<const char> str);

  // Interns a single character.
  zc::StringPtr intern(char c);

  // Overloads to prevent template recursion and unnecessary allocations
  zc::StringPtr intern(const char* str);
  zc::StringPtr intern(const zc::String& str);

  // Interns a concatenation of multiple values or a single non-string value.
  // Uses zc::str() formatting.
  // We use a constraint to prevent this template from matching types that have specific overloads.
  // This helps with overload resolution and error messages.
  template <typename T, typename... Args>
    requires((!std::is_same_v<std::decay_t<T>, zc::StringPtr> &&
              !std::is_same_v<std::decay_t<T>, zc::ArrayPtr<const char>> &&
              !std::is_same_v<std::decay_t<T>, char> &&
              !std::is_same_v<std::decay_t<T>, const char*> &&
              !std::is_same_v<std::decay_t<T>, zc::String>) ||
             sizeof...(Args) > 0)
  zc::StringPtr intern(T&& first, Args&&... args) {
    return intern(zc::str(zc::fwd<T>(first), zc::fwd<Args>(args)...).asPtr());
  }

  // Clears the pool, freeing all strings.
  void clear();

private:
  struct StringHash {
    zc::StringPtr keyForRow(zc::StringPtr s) const { return s; }

    bool matches(zc::StringPtr a, zc::StringPtr b) const { return a == b; }
    bool matches(zc::StringPtr a, zc::ArrayPtr<const char> b) const { return a.asArray() == b; }

    uint32_t hashCode(zc::StringPtr s) const { return zc::hashCode(s); }
    uint32_t hashCode(zc::ArrayPtr<const char> s) const { return zc::hashCode(s); }
  };

  struct PoolState {
    zc::Arena arena;
    zc::Table<zc::StringPtr, zc::HashIndex<StringHash>> strings;
  };

  zc::MutexGuarded<PoolState> state;
};

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
