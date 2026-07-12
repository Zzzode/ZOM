// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#include "zomlang/compiler/binder/definition-identity-map.h"

#include "zc/core/vector.h"

namespace zomlang::compiler::binder {

struct DefinitionIdentityMap::Impl final {
  struct Entry final {
    ast::NodeId node;
    identity::DefId definition;
  };
  zc::Vector<Entry> entries;
};

DefinitionIdentityMap::DefinitionIdentityMap() noexcept : impl(zc::heap<Impl>()) {}
DefinitionIdentityMap::~DefinitionIdentityMap() noexcept(false) = default;
DefinitionIdentityMap::DefinitionIdentityMap(DefinitionIdentityMap&&) noexcept = default;
DefinitionIdentityMap& DefinitionIdentityMap::operator=(DefinitionIdentityMap&&) noexcept = default;

bool DefinitionIdentityMap::insert(ast::NodeId node, identity::DefId definition) {
  if (!node || !definition.isValid() || find(node) != zc::none) { return false; }
  impl->entries.add(Impl::Entry{node, definition});
  return true;
}

zc::Maybe<identity::DefId> DefinitionIdentityMap::find(ast::NodeId node) const {
  for (const auto& entry : impl->entries) {
    if (entry.node == node) { return entry.definition; }
  }
  return zc::none;
}

size_t DefinitionIdentityMap::size() const noexcept { return impl->entries.size(); }

}  // namespace zomlang::compiler::binder
