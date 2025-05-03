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

#include "zc/core/common.h"
#include "zc/core/map.h"
#include "zc/core/string.h"

namespace zomlang {
namespace checker {

struct Symbol {
  zc::String name;
  zc::String type;
  // Add more properties as needed
};

class SymbolTable {
public:
  void Insert(zc::String name, zc::Own<Symbol> symbol) {
    symbols.insert(zc::mv(name), zc::mv(symbol));
  }

  Symbol* Lookup(const zc::String& name) {
    ZC_IF_SOME(it, symbols.find(name)) { return it.get(); }
    return nullptr;
  }

private:
  zc::HashMap<zc::String, zc::Own<Symbol>> symbols;
};

}  // namespace checker
}  // namespace zomlang
