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

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/node-id.h"

namespace zomlang {
namespace compiler {

namespace ast {
class Tree;
struct Node;
}  // namespace ast

namespace checker {

/// \brief Return the simple marker names attached to a DynTypeExpr node.
zc::Vector<zc::StringPtr> dynMarkerNames(const ast::Tree& tree, const ast::Node& node);

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
