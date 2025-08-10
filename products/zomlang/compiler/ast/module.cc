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
#include "zomlang/compiler/ast/statement.h"

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

// ================================================================================
// ImportDeclaration::Impl
struct ImportDeclaration::Impl {
  Impl(zc::Own<ModulePath>&& modulePath, zc::Maybe<zc::String> alias) noexcept
      : modulePath(zc::mv(modulePath)), alias(zc::mv(alias)) {}

  const zc::Own<ModulePath> modulePath;
  const zc::Maybe<zc::String> alias;
};

// ================================================================================
// ImportDeclaration
ImportDeclaration::ImportDeclaration(zc::Own<ModulePath>&& modulePath,
                                     zc::Maybe<zc::String> alias) noexcept
    : Statement(SyntaxKind::kImportDeclaration),
      impl(zc::heap<Impl>(zc::mv(modulePath), zc::mv(alias))) {}

ImportDeclaration::~ImportDeclaration() noexcept(false) = default;

const ModulePath& ImportDeclaration::getModulePath() const { return *impl->modulePath; }

zc::Maybe<zc::StringPtr> ImportDeclaration::getAlias() const {
  ZC_IF_SOME(alias, impl->alias) { return alias.asPtr(); }
  else { return zc::none; }
}

// ================================================================================
// ExportDeclaration::Impl
struct ExportDeclaration::Impl {
  struct SimpleExport {
    zc::String identifier;

    explicit SimpleExport(zc::String&& identifier) : identifier(zc::mv(identifier)) {}
  };

  struct RenameExport {
    zc::String identifier;
    zc::String alias;
    zc::Own<ModulePath> modulePath;

    RenameExport(zc::String&& identifier, zc::String&& alias, zc::Own<ModulePath>&& modulePath)
        : identifier(zc::mv(identifier)), alias(zc::mv(alias)), modulePath(zc::mv(modulePath)) {}
  };

  zc::OneOf<SimpleExport, RenameExport> exportType;

  explicit Impl(zc::String identifier) : exportType(SimpleExport(zc::mv(identifier))) {}

  Impl(zc::String identifier, zc::String alias, zc::Own<ModulePath>&& modulePath)
      : exportType(RenameExport(zc::mv(identifier), zc::mv(alias), zc::mv(modulePath))) {}
};

// ================================================================================
// ExportDeclaration
ExportDeclaration::ExportDeclaration(zc::String&& identifier) noexcept
    : Statement(SyntaxKind::kExportDeclaration), impl(zc::heap<Impl>(zc::mv(identifier))) {}

ExportDeclaration::ExportDeclaration(zc::String&& identifier, zc::String&& alias,
                                     zc::Own<ModulePath>&& modulePath) noexcept
    : Statement(SyntaxKind::kExportDeclaration),
      impl(zc::heap<Impl>(zc::mv(identifier), zc::mv(alias), zc::mv(modulePath))) {}

ExportDeclaration::~ExportDeclaration() noexcept(false) = default;

zc::StringPtr ExportDeclaration::getIdentifier() const {
  ZC_SWITCH_ONEOF(impl->exportType) {
    ZC_CASE_ONEOF(simple, Impl::SimpleExport) { return simple.identifier; }
    ZC_CASE_ONEOF(rename, Impl::RenameExport) { return rename.identifier; }
  }
  ZC_UNREACHABLE;
}

bool ExportDeclaration::isRename() const { return impl->exportType.is<Impl::RenameExport>(); }

zc::Maybe<zc::StringPtr> ExportDeclaration::getAlias() const {
  ZC_SWITCH_ONEOF(impl->exportType) {
    ZC_CASE_ONEOF(simple, Impl::SimpleExport) { return zc::none; }
    ZC_CASE_ONEOF(rename, Impl::RenameExport) { return rename.alias.asPtr(); }
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

// ================================================================================
// ModulePath::Impl
struct ModulePath::Impl {
  explicit Impl(zc::Vector<zc::String>&& identifiers) noexcept : identifiers(zc::mv(identifiers)) {}

  const zc::Vector<zc::String> identifiers;
};

// ================================================================================
// ModulePath
ModulePath::ModulePath(zc::Vector<zc::String>&& identifiers) noexcept
    : Node(SyntaxKind::kModulePath), impl(zc::heap<Impl>(zc::mv(identifiers))) {}

ModulePath::~ModulePath() noexcept(false) = default;

zc::ArrayPtr<const zc::String> ModulePath::getIdentifiers() const {
  return impl->identifiers.asPtr();
}

zc::String ModulePath::toString() const {
  zc::String result;
  for (size_t i = 0; i < impl->identifiers.size(); ++i) {
    if (i > 0) { result = zc::str(result, "."); }
    result = zc::str(result, impl->identifiers[i]);
  }
  return result;
}
}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
