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

#include "zomlang/compiler/ast/expression.h"

#include "zc/core/memory.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// BinaryExpression::Impl

struct BinaryExpression::Impl {
  const zc::Own<Expression> left;
  const zc::String op;
  const zc::Own<Expression> right;

  Impl(zc::Own<Expression> l, zc::String o, zc::Own<Expression> r)
      : left(zc::mv(l)), op(zc::mv(o)), right(zc::mv(r)) {}
};

// ================================================================================
// BinaryExpression

BinaryExpression::BinaryExpression(zc::Own<Expression> left, zc::String op,
                                   zc::Own<Expression> right)
    : impl(zc::heap<Impl>(zc::mv(left), zc::mv(op), zc::mv(right))) {}

BinaryExpression::~BinaryExpression() noexcept(false) = default;

const Expression* BinaryExpression::getLeft() const { return impl->left.get(); }

const zc::StringPtr BinaryExpression::getOp() const { return impl->op.asPtr(); }

const Expression* BinaryExpression::getRight() const { return impl->right.get(); }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang