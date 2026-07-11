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

#include "zomlang/compiler/checker/type-expr-utils.h"

#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/tree.h"

namespace zomlang {
namespace compiler {
namespace checker {

zc::Vector<zc::StringPtr> dynMarkerNames(const ast::Tree& tree, const ast::Node& node) {
  zc::Vector<zc::StringPtr> result;
  auto markersId = ast::NodeId(node.payload.words[ast::kDynTypeExprMarkersIdWord]);
  if (!tree.contains(markersId)) { return result; }
  const auto& markersNode = tree.node(markersId);
  if (markersNode.kind != ast::SyntaxKind::DynTypeMarkerList) { return result; }

  ast::NodeList markers;
  markers.first = markersNode.payload.words[ast::kDynTypeMarkerListMarkersFirstWord];
  markers.size = markersNode.payload.words[ast::kDynTypeMarkerListMarkersSizeWord];
  for (ast::NodeId markerId : tree.list(markers)) {
    if (!tree.contains(markerId)) { continue; }
    const auto& marker = tree.node(markerId);
    if (marker.kind != ast::SyntaxKind::AttributePath) { continue; }
    ast::IdentList segments;
    segments.first = marker.payload.words[ast::kAttributePathSegmentsFirstWord];
    segments.size = marker.payload.words[ast::kAttributePathSegmentsSizeWord];
    auto names = tree.identList(segments);
    if (names.size() == 0) { continue; }
    result.add(tree.ident(names.back()));
  }
  return result;
}

zc::Vector<zc::StringPtr> dynAssocBindingNames(const ast::Tree& tree, const ast::Node& node) {
  auto bindingsId = ast::NodeId(node.payload.words[ast::kDynTypeExprAssocBindingsIdWord]);
  return dynAssocBindingListNames(tree, bindingsId);
}

zc::Vector<zc::StringPtr> dynAssocBindingListNames(const ast::Tree& tree, ast::NodeId bindingsId) {
  zc::Vector<zc::StringPtr> result;
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
    result.add(tree.ident(ast::IdentId(binding.payload.words[ast::kDynTypeAssocBindingNameWord])));
  }
  return result;
}

zc::Maybe<zc::StringPtr> findDuplicateDynAssocBindingName(const ast::Tree& tree,
                                                          ast::NodeId bindingsId) {
  auto names = dynAssocBindingListNames(tree, bindingsId);
  for (size_t i = 0; i < names.size(); ++i) {
    for (size_t j = i + 1; j < names.size(); ++j) {
      if (names[i] == names[j]) { return names[i]; }
    }
  }
  return zc::none;
}

bool dynAssocBindingListContains(const ast::Tree& tree, ast::NodeId bindingsId,
                                 zc::StringPtr assocName) {
  if (!tree.contains(bindingsId)) { return false; }

  const auto& bindingsNode = tree.node(bindingsId);
  if (bindingsNode.kind != ast::SyntaxKind::DynTypeAssocBindingList) { return false; }

  ast::NodeList bindings;
  bindings.first = bindingsNode.payload.words[ast::kDynTypeAssocBindingListBindingsFirstWord];
  bindings.size = bindingsNode.payload.words[ast::kDynTypeAssocBindingListBindingsSizeWord];
  for (ast::NodeId bindingId : tree.list(bindings)) {
    if (!tree.contains(bindingId)) { continue; }
    const auto& binding = tree.node(bindingId);
    if (binding.kind != ast::SyntaxKind::DynTypeAssocBinding) { continue; }
    auto name = tree.ident(ast::IdentId(binding.payload.words[ast::kDynTypeAssocBindingNameWord]));
    if (name == assocName) { return true; }
  }

  return false;
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
