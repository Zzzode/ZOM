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
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/ast.h"

namespace zomlang {
namespace compiler {
namespace ast {

class Expression;

class Statement : public Node {
public:
  explicit Statement(SyntaxKind kind = SyntaxKind::kStatement) noexcept;
  ~Statement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Statement);
};

class VariableDeclaration : public Statement {
public:
  VariableDeclaration(zc::String type, zc::String name, zc::Own<Expression> initializer);
  ~VariableDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(VariableDeclaration);

  const zc::StringPtr getType() const;
  const zc::StringPtr getName() const;
  const Expression* getInitializer() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang