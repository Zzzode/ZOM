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

#include "zomlang/compiler/ast/module.h"

#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/ast/visitor.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// SourceFile::Impl
struct SourceFile::Impl {
  Impl(zc::String&& fileName, zc::Vector<zc::Own<ast::Statement>>&& statements) noexcept
      : fileName(zc::mv(fileName)), statements(zc::mv(statements)) {}

  /// Identifier of the module buffer.
  const zc::String fileName;
  /// List of toplevel statements in the module.
  const NodeList<ast::Statement> statements;
};

// ================================================================================
// SourceFile
SourceFile::SourceFile(zc::String&& fileName,
                       zc::Vector<zc::Own<ast::Statement>>&& statements) noexcept
    : Node(SyntaxKind::kSourceFile), impl(zc::heap<Impl>(zc::mv(fileName), zc::mv(statements))) {}

SourceFile::~SourceFile() noexcept(false) = default;

const NodeList<Statement>& SourceFile::getStatements() const { return impl->statements; }

zc::StringPtr SourceFile::getFileName() const { return impl->fileName; }

void SourceFile::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ImportDeclaration::Impl
struct ImportDeclaration::Impl {
  Impl(zc::Own<ModulePath>&& modulePath, zc::Maybe<zc::Own<ast::Identifier>> alias) noexcept
      : modulePath(zc::mv(modulePath)), alias(zc::mv(alias)) {}

  const zc::Own<ModulePath> modulePath;
  const zc::Maybe<zc::Own<ast::Identifier>> alias;
};

// ================================================================================
// ImportDeclaration
ImportDeclaration::ImportDeclaration(zc::Own<ModulePath>&& modulePath,
                                     zc::Maybe<zc::Own<ast::Identifier>> alias) noexcept
    : Statement(SyntaxKind::kImportDeclaration),
      impl(zc::heap<Impl>(zc::mv(modulePath), zc::mv(alias))) {}

ImportDeclaration::~ImportDeclaration() noexcept(false) = default;

const ModulePath& ImportDeclaration::getModulePath() const { return *impl->modulePath; }

zc::Maybe<const ast::Identifier&> ImportDeclaration::getAlias() const {
  ZC_IF_SOME(alias, impl->alias) { return *alias; }
  return zc::none;
}

void ImportDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ExportDeclaration::Impl
struct ExportDeclaration::Impl {
  struct SimpleExport {
    zc::Own<ast::Identifier> identifier;

    explicit SimpleExport(zc::Own<ast::Identifier>&& identifier) : identifier(zc::mv(identifier)) {}
  };

  struct RenameExport {
    zc::Own<ast::Identifier> identifier;
    zc::Own<ast::Identifier> alias;
    zc::Own<ModulePath> modulePath;

    RenameExport(zc::Own<ast::Identifier>&& identifier, zc::Own<ast::Identifier>&& alias,
                 zc::Own<ModulePath>&& modulePath)
        : identifier(zc::mv(identifier)), alias(zc::mv(alias)), modulePath(zc::mv(modulePath)) {}
  };

  zc::OneOf<SimpleExport, RenameExport> exportType;

  explicit Impl(zc::Own<ast::Identifier>&& identifier)
      : exportType(SimpleExport(zc::mv(identifier))) {}

  Impl(zc::Own<ast::Identifier>&& identifier, zc::Own<ast::Identifier>&& alias,
       zc::Own<ModulePath>&& modulePath)
      : exportType(RenameExport(zc::mv(identifier), zc::mv(alias), zc::mv(modulePath))) {}
};

// ================================================================================
// ExportDeclaration
ExportDeclaration::ExportDeclaration(zc::Own<ast::Identifier>&& identifier) noexcept
    : Statement(SyntaxKind::kExportDeclaration), impl(zc::heap<Impl>(zc::mv(identifier))) {}

ExportDeclaration::ExportDeclaration(zc::Own<ast::Identifier>&& identifier,
                                     zc::Own<ast::Identifier>&& alias,
                                     zc::Own<ModulePath>&& modulePath) noexcept
    : Statement(SyntaxKind::kExportDeclaration),
      impl(zc::heap<Impl>(zc::mv(identifier), zc::mv(alias), zc::mv(modulePath))) {}

ExportDeclaration::~ExportDeclaration() noexcept(false) = default;

const ast::Identifier& ExportDeclaration::getIdentifier() const {
  ZC_SWITCH_ONEOF(impl->exportType) {
    ZC_CASE_ONEOF(simple, Impl::SimpleExport) { return *simple.identifier; }
    ZC_CASE_ONEOF(rename, Impl::RenameExport) { return *rename.identifier; }
  }
  ZC_UNREACHABLE;
}

bool ExportDeclaration::isRename() const { return impl->exportType.is<Impl::RenameExport>(); }

zc::Maybe<const ast::Identifier&> ExportDeclaration::getAlias() const {
  ZC_SWITCH_ONEOF(impl->exportType) {
    ZC_CASE_ONEOF(simple, Impl::SimpleExport) { return zc::none; }
    ZC_CASE_ONEOF(rename, Impl::RenameExport) { return *rename.alias; }
  }
  ZC_UNREACHABLE;
}

zc::Maybe<const ModulePath&> ExportDeclaration::getModulePath() const {
  ZC_SWITCH_ONEOF(impl->exportType) {
    ZC_CASE_ONEOF(simple, Impl::SimpleExport) { return zc::none; }
    ZC_CASE_ONEOF(rename, Impl::RenameExport) { return *rename.modulePath; }
  }
  ZC_UNREACHABLE;
}

void ExportDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ModulePath::Impl
struct ModulePath::Impl {
  explicit Impl(zc::Vector<zc::Own<ast::Identifier>>&& identifiers) noexcept
      : identifiers(zc::mv(identifiers)) {}

  const NodeList<Identifier> identifiers;
};

// ================================================================================
// ModulePath
ModulePath::ModulePath(zc::Vector<zc::Own<ast::Identifier>>&& identifiers) noexcept
    : Node(SyntaxKind::kModulePath), impl(zc::heap<Impl>(zc::mv(identifiers))) {}

ModulePath::~ModulePath() noexcept(false) = default;

const NodeList<Identifier>& ModulePath::getIdentifiers() const { return impl->identifiers; }

void ModulePath::accept(Visitor& visitor) const { visitor.visit(*this); }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
