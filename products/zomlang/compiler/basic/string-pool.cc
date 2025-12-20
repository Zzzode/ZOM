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

#include <new>

namespace zomlang {
namespace compiler {
namespace basic {

StringPool::StringPool() {}

zc::StringPtr StringPool::intern(zc::StringPtr str) {
  auto locked = state.lockExclusive();
  ZC_IF_SOME(s, locked->strings.find(str)) { return s; }

  zc::StringPtr copy = locked->arena.copyString(str);
  locked->strings.insert(copy);
  return copy;
}

zc::StringPtr StringPool::intern(zc::ArrayPtr<const char> str) {
  auto locked = state.lockExclusive();
  ZC_IF_SOME(s, locked->strings.find(str)) { return s; }

  // Manually copy string to arena
  char* buffer = locked->arena.allocateArray<char>(str.size() + 1).begin();
  memcpy(buffer, str.begin(), str.size());
  buffer[str.size()] = '\0';

  zc::StringPtr copy(buffer, str.size());
  locked->strings.insert(copy);
  return copy;
}

zc::StringPtr StringPool::intern(char c) {
  const char s[1] = {c};
  return intern(zc::arrayPtr(s, 1));
}

zc::StringPtr StringPool::intern(const char* str) { return intern(zc::StringPtr(str)); }

zc::StringPtr StringPool::intern(const zc::String& str) { return intern(zc::StringPtr(str)); }

void StringPool::clear() {
  auto locked = state.lockExclusive();
  locked->strings.clear();
  locked->arena.~Arena();
  new (&locked->arena) zc::Arena();
}

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
