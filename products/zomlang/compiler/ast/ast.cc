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

#include "zomlang/compiler/ast/ast.h"

#include "zc/core/memory.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// BinaryExpression::Impl

struct BinaryExpression::Impl {
  zc::Own<Expression> left;
  zc::String op;
  zc::Own<Expression> right;

  Impl(zc::Own<Expression> l, zc::String o, zc::Own<Expression> r)
      : left(zc::mv(l)), op(zc::mv(o)), right(zc::mv(r)) {}
};

// ================================================================================
// BinaryExpression

BinaryExpression::BinaryExpression(zc::Own<Expression> left, zc::String op,
                                   zc::Own<Expression> right)
    : impl(zc::heap<Impl>(zc::mv(left), zc::mv(op), zc::mv(right))) {}

BinaryExpression::~BinaryExpression() noexcept = default;

const Expression* BinaryExpression::getLeft() const { return impl->left.get(); }

zc::StringPtr BinaryExpression::getOp() const { return impl->op; }

const Expression* BinaryExpression::getRight() const { return impl->right.get(); }

// ================================================================================
// VariableDeclaration::Impl

struct VariableDeclaration::Impl {
  zc::String type;
  zc::String name;
  zc::Own<Expression> initializer;

  Impl(zc::String t, zc::String n, zc::Own<Expression> init)
      : type(zc::mv(t)), name(zc::mv(n)), initializer(zc::mv(init)) {}
};

// ================================================================================
// VariableDeclaration

VariableDeclaration::VariableDeclaration(zc::String type, zc::String name,
                                         zc::Own<Expression> initializer)
    : impl(zc::heap<Impl>(zc::mv(type), zc::mv(name), zc::mv(initializer))) {}

VariableDeclaration::~VariableDeclaration() noexcept = default;

zc::StringPtr VariableDeclaration::getType() const { return impl->type; }

zc::StringPtr VariableDeclaration::getName() const { return impl->name; }

const Expression* VariableDeclaration::getInitializer() const {
  return impl->initializer.get();  // 返回裸指针
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang