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
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/statement.h"

namespace zomlang {
namespace compiler {
namespace ast {

class SourceFile : public Node {
public:
  explicit SourceFile(const zc::StringPtr fileName,
                      zc::Vector<zc::Own<ast::Statement>>&& statements) noexcept;
  ~SourceFile() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(SourceFile);

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ImportDeclaration : public Statement {
public:
  ImportDeclaration() noexcept;
  // TODO: Define constructor and methods for ImportDeclaration
  ~ImportDeclaration() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(ImportDeclaration);
};

class ExportDeclaration : public Statement {
public:
  ExportDeclaration() noexcept;
  // TODO: Define constructor and methods for ExportDeclaration
  ~ExportDeclaration() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(ExportDeclaration);
};

class ModulePath : public Node {
public:
  // TODO: Define constructor and methods for ModulePath
  ModulePath() noexcept;
  ~ModulePath() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(ModulePath);
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
