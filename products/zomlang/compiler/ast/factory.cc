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

#include "zomlang/compiler/ast/factory.h"

#include "zomlang/compiler/ast/module.h"

namespace zomlang {
namespace compiler {
namespace ast {
namespace factory {

zc::Own<SourceFile> createSourceFile(const zc::StringPtr fileName,
                                     zc::Vector<zc::Own<ast::Statement>>&& statements) {
  return zc::heap<SourceFile>(fileName, zc::mv(statements));
}

}  // namespace factory
}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
