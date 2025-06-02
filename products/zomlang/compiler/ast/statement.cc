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

#include "zomlang/compiler/ast/statement.h"

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/expression.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// Statement
Statement::Statement() noexcept = default;
Statement::~Statement() noexcept(false) = default;

// ================================================================================
// VariableDeclaration::Impl

struct VariableDeclaration::Impl {
  const zc::String type;
  const zc::String name;
  const zc::Own<Expression> initializer;

  Impl(zc::String t, zc::String n, zc::Own<Expression> init)
      : type(zc::mv(t)), name(zc::mv(n)), initializer(zc::mv(init)) {}
};

// ================================================================================
// VariableDeclaration

VariableDeclaration::VariableDeclaration(zc::String type, zc::String name,
                                         zc::Own<Expression> initializer)
    : impl(zc::heap<Impl>(zc::mv(type), zc::mv(name), zc::mv(initializer))) {}

VariableDeclaration::~VariableDeclaration() noexcept(false) = default;

const zc::StringPtr VariableDeclaration::getType() const { return impl->type; }

const zc::StringPtr VariableDeclaration::getName() const { return impl->name; }

const Expression* VariableDeclaration::getInitializer() const { return impl->initializer.get(); }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
