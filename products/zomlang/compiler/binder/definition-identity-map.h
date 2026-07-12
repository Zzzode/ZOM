// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/identity/frozen-registry.h"

namespace zomlang::compiler::binder {

/// \brief Immutable tree-local projection from definition-producing nodes to frozen identities.
class DefinitionIdentityMap final {
public:
  DefinitionIdentityMap() noexcept;
  ~DefinitionIdentityMap() noexcept(false);
  DefinitionIdentityMap(DefinitionIdentityMap&&) noexcept;
  DefinitionIdentityMap& operator=(DefinitionIdentityMap&&) noexcept;
  ZC_DISALLOW_COPY(DefinitionIdentityMap);

  /// \brief Adds one frozen identity before the map is published to binding.
  ZC_NODISCARD bool insert(ast::NodeId node, identity::DefId definition);
  /// \brief Looks up the frozen identity for one definition-producing syntax node.
  ZC_NODISCARD zc::Maybe<identity::DefId> find(ast::NodeId node) const;
  ZC_NODISCARD size_t size() const noexcept;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::binder
