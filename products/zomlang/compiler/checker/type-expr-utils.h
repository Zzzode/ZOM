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
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/type/existential-type.h"

namespace zomlang {
namespace compiler {

namespace checker {

/// \brief Return the simple marker names attached to a DynTypeExpr node.
zc::Vector<zc::StringPtr> dynMarkerNames(const ast::Tree& tree, const ast::Node& node);

/// \brief Return the associated type binding names attached to a DynTypeExpr node.
zc::Vector<zc::StringPtr> dynAssocBindingNames(const ast::Tree& tree, const ast::Node& node);

/// \brief Return the associated type binding names attached to a DynTypeAssocBindingList node.
zc::Vector<zc::StringPtr> dynAssocBindingListNames(const ast::Tree& tree, ast::NodeId bindingsId);

/// \brief Return the first duplicated dyn associated type binding name, if any.
zc::Maybe<zc::StringPtr> findDuplicateDynAssocBindingName(const ast::Tree& tree,
                                                          ast::NodeId bindingsId);

/// \brief Return true if a DynTypeAssocBindingList contains the given associated type name.
bool dynAssocBindingListContains(const ast::Tree& tree, ast::NodeId bindingsId,
                                 zc::StringPtr assocName);

/// \brief Resolve associated type bindings attached to a DynTypeExpr node.
template <typename ResolveTypeFn>
zc::Vector<type::ExistentialType::AssocBinding> dynAssocBindings(const ast::Tree& tree,
                                                                 const ast::Node& node,
                                                                 ResolveTypeFn&& resolveType) {
  zc::Vector<type::ExistentialType::AssocBinding> result;
  auto bindingsId = ast::NodeId(node.payload.words[ast::kDynTypeExprAssocBindingsIdWord]);
  if (!tree.contains(bindingsId)) { return result; }

  const auto& bindingsNode = tree.node(bindingsId);
  if (bindingsNode.kind != ast::SyntaxKind::DynTypeAssocBindingList) { return result; }

  ast::NodeList bindings;
  bindings.first = bindingsNode.payload.words[ast::kDynTypeAssocBindingListBindingsFirstWord];
  bindings.size = bindingsNode.payload.words[ast::kDynTypeAssocBindingListBindingsSizeWord];
  for (ast::NodeId bindingId : tree.list(bindings)) {
    if (!tree.contains(bindingId)) { continue; }
    const auto& binding = tree.node(bindingId);
    if (binding.kind != ast::SyntaxKind::DynTypeAssocBinding) { continue; }

    auto name = tree.ident(ast::IdentId(binding.payload.words[ast::kDynTypeAssocBindingNameWord]));
    auto tyId = ast::NodeId(binding.payload.words[ast::kDynTypeAssocBindingTyWord]);
    auto bindingTy = resolveType(tyId);
    if (bindingTy) { result.add(type::ExistentialType::AssocBinding{name, zc::mv(bindingTy)}); }
  }
  return result;
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
