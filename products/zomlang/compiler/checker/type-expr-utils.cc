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

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
