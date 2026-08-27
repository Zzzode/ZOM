// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "compiler/lir/lir-stores.h"

namespace zomlang::compiler::lir {
namespace {

// Every store issues one-based handles from a count that is at least one, so
// `fromOrdinal` can never reject; this helper turns the never-none result into
// the branded handle without hiding a real failure behind a default.
template <typename Id>
Id requireOrdinal(uint32_t ordinal) {
  auto handle = Id::fromOrdinal(ordinal);
  ZC_IF_SOME(value, handle) { return value; }
  ZC_UNREACHABLE;
}

}  // namespace

LirValueTypeId LirValueTypeStore::intern(const LirValueType& type) {
  for (size_t index = 0; index < records.size(); ++index) {
    if (records[index] == type) {
      return requireOrdinal<LirValueTypeId>(static_cast<uint32_t>(index + 1));
    }
  }
  records.add(type);
  return requireOrdinal<LirValueTypeId>(static_cast<uint32_t>(records.size()));
}

zc::Maybe<const LirValueType&> LirValueTypeStore::lookup(LirValueTypeId id) const noexcept {
  if (!id.isValid() || id.ordinal() > records.size()) { return zc::none; }
  return records[id.ordinal() - 1];
}

LayoutId LayoutStore::intern(const StorageLayout& layout) {
  for (size_t index = 0; index < records.size(); ++index) {
    if (records[index] == layout) {
      return requireOrdinal<LayoutId>(static_cast<uint32_t>(index + 1));
    }
  }
  records.add(layout);
  return requireOrdinal<LayoutId>(static_cast<uint32_t>(records.size()));
}

zc::Maybe<const StorageLayout&> LayoutStore::lookup(LayoutId id) const noexcept {
  if (!id.isValid() || id.ordinal() > records.size()) { return zc::none; }
  return records[id.ordinal() - 1];
}

FnAbiId FnAbiStore::intern(const FnAbi& abi) {
  for (size_t index = 0; index < records.size(); ++index) {
    if (records[index] == abi) { return requireOrdinal<FnAbiId>(static_cast<uint32_t>(index + 1)); }
  }
  records.add(abi.clone());
  return requireOrdinal<FnAbiId>(static_cast<uint32_t>(records.size()));
}

zc::Maybe<const FnAbi&> FnAbiStore::lookup(FnAbiId id) const noexcept {
  if (!id.isValid() || id.ordinal() > records.size()) { return zc::none; }
  return records[id.ordinal() - 1];
}

RuntimeSymbolId RuntimeSymbolStore::intern(const RuntimeSymbol& symbol) {
  for (size_t index = 0; index < records.size(); ++index) {
    if (records[index] == symbol) {
      return requireOrdinal<RuntimeSymbolId>(static_cast<uint32_t>(index + 1));
    }
  }
  records.add(symbol.clone());
  return requireOrdinal<RuntimeSymbolId>(static_cast<uint32_t>(records.size()));
}

zc::Maybe<const RuntimeSymbol&> RuntimeSymbolStore::lookup(RuntimeSymbolId id) const noexcept {
  if (!id.isValid() || id.ordinal() > records.size()) { return zc::none; }
  return records[id.ordinal() - 1];
}

LirSourceLocationId LirSourceLocationStore::intern(const LirSourceLocation& location) {
  for (size_t index = 0; index < records.size(); ++index) {
    if (records[index] == location) {
      return requireOrdinal<LirSourceLocationId>(static_cast<uint32_t>(index + 1));
    }
  }
  records.add(location);
  return requireOrdinal<LirSourceLocationId>(static_cast<uint32_t>(records.size()));
}

zc::Maybe<const LirSourceLocation&> LirSourceLocationStore::lookup(
    LirSourceLocationId id) const noexcept {
  if (!id.isValid() || id.ordinal() > records.size()) { return zc::none; }
  return records[id.ordinal() - 1];
}

}  // namespace zomlang::compiler::lir
