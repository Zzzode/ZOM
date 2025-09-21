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
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/statement.h"

namespace zomlang {
namespace compiler {
namespace ast {

class Identifier;
class ModulePath;

class SourceFile : public Node {
public:
  explicit SourceFile(zc::String&& fileName,
                      zc::Vector<zc::Own<ast::Statement>>&& statements) noexcept;
  ~SourceFile() noexcept(false);

  const NodeList<Statement>& getStatements() const;
  zc::StringPtr getFileName() const;

  void accept(Visitor& visitor) const override;

  ZC_DISALLOW_COPY_AND_MOVE(SourceFile);

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ImportDeclaration : public Statement {
public:
  ImportDeclaration(zc::Own<ModulePath>&& modulePath,
                    zc::Maybe<zc::Own<ast::Identifier>> alias = zc::none) noexcept;
  ~ImportDeclaration() noexcept(false);

  const ModulePath& getModulePath() const;
  zc::Maybe<const ast::Identifier&> getAlias() const;

  void accept(Visitor& visitor) const override;

  ZC_DISALLOW_COPY_AND_MOVE(ImportDeclaration);

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ExportDeclaration : public Statement {
public:
  explicit ExportDeclaration(zc::Own<ast::Identifier>&& identifier) noexcept;

  ExportDeclaration(zc::Own<ast::Identifier>&& identifier, zc::Own<ast::Identifier>&& alias,
                    zc::Own<ModulePath>&& modulePath) noexcept;

  ~ExportDeclaration() noexcept(false);

  const ast::Identifier& getIdentifier() const;
  bool isRename() const;
  zc::Maybe<const ast::Identifier&> getAlias() const;
  zc::Maybe<const ModulePath&> getModulePath() const;

  void accept(Visitor& visitor) const override;

  ZC_DISALLOW_COPY_AND_MOVE(ExportDeclaration);

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ModulePath : public Node {
public:
  explicit ModulePath(zc::Vector<zc::Own<ast::Identifier>>&& identifiers) noexcept;
  ~ModulePath() noexcept(false);

  const NodeList<Identifier>& getIdentifiers() const;

  void accept(Visitor& visitor) const override;

  ZC_DISALLOW_COPY_AND_MOVE(ModulePath);

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
