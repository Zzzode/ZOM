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

namespace zomlang {
namespace compiler {
namespace zis {

class ZIS {
public:
  virtual ~ZIS() noexcept = default;
};

class Expression : public ZIS {
public:
  ~Expression() noexcept override = default;
};

class Statement : public ZIS {
public:
  ~Statement() noexcept override = default;
};

class BinaryExpression : public Expression {
public:
  ~BinaryExpression() noexcept override = default;

private:
  zc::Own<Expression> left;
  zc::String op;
  zc::Own<Expression> right;
};

class VariableDeclaration : public Statement {
public:
  ~VariableDeclaration() noexcept override = default;

  zc::StringPtr getType() const { return type; }

  zc::StringPtr getName() const { return name; }

private:
  zc::String type;
  zc::String name;
  zc::Own<Expression> initializer;
};

// Add more ZIS node types as needed

}  // namespace zis
}  // namespace compiler
}  // namespace zomlang
