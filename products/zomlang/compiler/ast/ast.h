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

namespace zomlang {
namespace compiler {
namespace ast {

class AST {
public:
  virtual ~AST() noexcept = default;
};

class Expression : public AST {
public:
  ~Expression() noexcept override = default;
};

class Statement : public AST {
public:
  ~Statement() noexcept override = default;
};

class BinaryExpression : public Expression {
public:
  BinaryExpression(zc::Own<Expression> left, zc::String op, zc::Own<Expression> right);
  ~BinaryExpression() noexcept override;

  const Expression* getLeft() const;
  zc::StringPtr getOp() const;
  const Expression* getRight() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class VariableDeclaration : public Statement {
public:
  VariableDeclaration(zc::String type, zc::String name, zc::Own<Expression> initializer);
  ~VariableDeclaration() noexcept override;

  zc::StringPtr getType() const;
  zc::StringPtr getName() const;
  const Expression* getInitializer() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

// Add more AST node types as needed

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
