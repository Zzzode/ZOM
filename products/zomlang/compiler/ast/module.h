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

class ModulePath;

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
  ImportDeclaration(zc::Own<ModulePath>&& modulePath,
                    zc::Maybe<zc::StringPtr> alias = zc::none) noexcept;
  ~ImportDeclaration() noexcept(false) override;

  const ModulePath& getModulePath() const;
  zc::Maybe<zc::StringPtr> getAlias() const;

  ZC_DISALLOW_COPY_AND_MOVE(ImportDeclaration);

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ExportDeclaration : public Statement {
public:
  // 简单导出：export identifier
  explicit ExportDeclaration(zc::StringPtr identifier) noexcept;

  // 重命名导出：export identifier as alias from modulePath
  ExportDeclaration(zc::StringPtr identifier, zc::StringPtr alias,
                    zc::Own<ModulePath>&& modulePath) noexcept;

  ~ExportDeclaration() noexcept(false) override;

  zc::StringPtr getIdentifier() const;
  bool isRename() const;
  zc::Maybe<zc::StringPtr> getAlias() const;
  zc::Maybe<const ModulePath&> getModulePath() const;

  ZC_DISALLOW_COPY_AND_MOVE(ExportDeclaration);

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ModulePath : public Node {
public:
  explicit ModulePath(zc::Vector<zc::StringPtr>&& identifiers) noexcept;
  ~ModulePath() noexcept(false) override;

  const zc::Vector<zc::StringPtr>& getIdentifiers() const;
  zc::String toString() const;

  ZC_DISALLOW_COPY_AND_MOVE(ModulePath);

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
