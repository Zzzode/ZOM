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
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/statement.h"

namespace zomlang {
namespace compiler {
namespace ast {

class Identifier;

class ModuleDeclaration final : public Statement {
public:
  explicit ModuleDeclaration(zc::Own<ModulePath>&& modulePath) noexcept;
  ~ModuleDeclaration() noexcept(false);

  const ModulePath& getModulePath() const;

  NODE_METHOD_DECLARE();

  ZC_DISALLOW_COPY_AND_MOVE(ModuleDeclaration);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class SourceFile final : public Node {
public:
  explicit SourceFile(zc::String&& fileName,
                      zc::Maybe<zc::Own<ModuleDeclaration>> moduleDeclaration,
                      zc::Vector<zc::Own<ast::Statement>>&& statements) noexcept;
  ~SourceFile() noexcept(false);

  zc::Maybe<const ModuleDeclaration&> getModuleDeclaration() const;
  const NodeList<Statement>& getStatements() const;
  zc::StringPtr getFileName() const;

  NODE_METHOD_DECLARE();

  ZC_DISALLOW_COPY_AND_MOVE(SourceFile);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ModulePath final : public Node {
public:
  explicit ModulePath(zc::Vector<zc::Own<ast::Identifier>>&& segments) noexcept;
  ~ModulePath() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(ModulePath);

  const NodeList<ast::Identifier>& getSegments() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ImportSpecifier final : public Node {
public:
  ImportSpecifier(zc::Own<ast::Identifier>&& importedName,
                  zc::Maybe<zc::Own<ast::Identifier>> alias = zc::none) noexcept;
  ~ImportSpecifier() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(ImportSpecifier);

  const ast::Identifier& getImportedName() const;
  zc::Maybe<const ast::Identifier&> getAlias() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ImportDeclaration final : public Statement {
public:
  ImportDeclaration(zc::Own<ModulePath>&& modulePath, zc::Maybe<zc::Own<ast::Identifier>> alias,
                    zc::Vector<zc::Own<ImportSpecifier>>&& specifiers) noexcept;
  ~ImportDeclaration() noexcept(false);

  const ModulePath& getModulePath() const;
  zc::Maybe<const ast::Identifier&> getAlias() const;
  const NodeList<ImportSpecifier>& getSpecifiers() const;
  bool isModuleImport() const;
  bool isNamedImport() const;

  NODE_METHOD_DECLARE();

  ZC_DISALLOW_COPY_AND_MOVE(ImportDeclaration);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ExportSpecifier final : public Node {
public:
  ExportSpecifier(zc::Own<ast::Identifier>&& exportedName,
                  zc::Maybe<zc::Own<ast::Identifier>> alias = zc::none) noexcept;
  ~ExportSpecifier() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(ExportSpecifier);

  const ast::Identifier& getExportedName() const;
  zc::Maybe<const ast::Identifier&> getAlias() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ExportDeclaration final : public Statement {
public:
  ExportDeclaration(zc::Maybe<zc::Own<ModulePath>> modulePath,
                    zc::Vector<zc::Own<ExportSpecifier>>&& specifiers,
                    zc::Maybe<zc::Own<ast::Statement>> declaration = zc::none) noexcept;

  ~ExportDeclaration() noexcept(false);

  zc::Maybe<const ModulePath&> getModulePath() const;
  const NodeList<ExportSpecifier>& getSpecifiers() const;
  zc::Maybe<const ast::Statement&> getDeclaration() const;
  bool isLocalExport() const;
  bool isReExport() const;
  bool isDeclarationExport() const;

  NODE_METHOD_DECLARE();

  ZC_DISALLOW_COPY_AND_MOVE(ExportDeclaration);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
