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

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/ast.h"

namespace zomlang {
namespace compiler {
namespace ast {
/// Forward declaration
class SourceFile;
class Statement;

namespace factory {
/// Create a new SourceFile Node.
zc::Own<SourceFile> createSourceFile(const zc::StringPtr fileName,
                                     zc::Vector<zc::Own<ast::Statement>>&& statements);

template <typename T>
const ast::NodeList<T> createNodeList(zc::Vector<zc::Own<T>>&& list) {
  return ast::NodeList<T>(zc::mv(list));
}

}  // namespace factory
}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
