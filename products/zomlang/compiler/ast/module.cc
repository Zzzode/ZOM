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
  Impl(const zc::StringPtr fileName, zc::Vector<zc::Own<ast::Statement>>&& statements) noexcept
      : fileName(fileName), statements(zc::mv(statements)) {}

  /// Identifier of the module buffer.
  const zc::StringPtr fileName;
  /// List of toplevel statements in the module.
  const NodeList<ast::Statement> statements;
};

// ================================================================================
// SourceFile
SourceFile::SourceFile(const zc::StringPtr fileName,
                       zc::Vector<zc::Own<ast::Statement>>&& statements) noexcept
    : impl(zc::heap<Impl>(fileName, zc::mv(statements))) {}

SourceFile::~SourceFile() noexcept(false) = default;

// ================================================================================
// ImportDeclaration::Impl
struct ImportDeclaration::Impl {
  Impl(zc::Own<ModulePath>&& modulePath, zc::Maybe<zc::StringPtr> alias) noexcept
      : modulePath(zc::mv(modulePath)), alias(alias) {}

  const zc::Own<ModulePath> modulePath;
  const zc::Maybe<zc::StringPtr> alias;
};

// ================================================================================
// ImportDeclaration
ImportDeclaration::ImportDeclaration(zc::Own<ModulePath>&& modulePath,
                                     zc::Maybe<zc::StringPtr> alias) noexcept
    : impl(zc::heap<Impl>(zc::mv(modulePath), alias)) {}

ImportDeclaration::~ImportDeclaration() noexcept(false) = default;

const ModulePath& ImportDeclaration::getModulePath() const { return *impl->modulePath; }

zc::Maybe<zc::StringPtr> ImportDeclaration::getAlias() const { return impl->alias; }

// ================================================================================
// ExportDeclaration::Impl
struct ExportDeclaration::Impl {
  struct SimpleExport {
    zc::StringPtr identifier;

    explicit SimpleExport(zc::StringPtr identifier) : identifier(identifier) {}
  };

  struct RenameExport {
    zc::StringPtr identifier;
    zc::StringPtr alias;
    zc::Own<ModulePath> modulePath;

    RenameExport(zc::StringPtr identifier, zc::StringPtr alias, zc::Own<ModulePath>&& modulePath)
        : identifier(identifier), alias(alias), modulePath(zc::mv(modulePath)) {}
  };

  zc::OneOf<SimpleExport, RenameExport> exportType;

  explicit Impl(zc::StringPtr identifier) : exportType(SimpleExport(identifier)) {}

  Impl(zc::StringPtr identifier, zc::StringPtr alias, zc::Own<ModulePath>&& modulePath)
      : exportType(RenameExport(identifier, alias, zc::mv(modulePath))) {}
};

// ================================================================================
// ExportDeclaration
ExportDeclaration::ExportDeclaration(zc::StringPtr identifier) noexcept
    : impl(zc::heap<Impl>(identifier)) {}

ExportDeclaration::ExportDeclaration(zc::StringPtr identifier, zc::StringPtr alias,
                                     zc::Own<ModulePath>&& modulePath) noexcept
    : impl(zc::heap<Impl>(identifier, alias, zc::mv(modulePath))) {}

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
    ZC_CASE_ONEOF(rename, Impl::RenameExport) { return rename.alias; }
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
  explicit Impl(zc::Vector<zc::StringPtr>&& identifiers) noexcept
      : identifiers(zc::mv(identifiers)) {}

  const zc::Vector<zc::StringPtr> identifiers;
};

// ================================================================================
// ModulePath
ModulePath::ModulePath(zc::Vector<zc::StringPtr>&& identifiers) noexcept
    : impl(zc::heap<Impl>(zc::mv(identifiers))) {}

ModulePath::~ModulePath() noexcept(false) = default;

const zc::Vector<zc::StringPtr>& ModulePath::getIdentifiers() const { return impl->identifiers; }

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
