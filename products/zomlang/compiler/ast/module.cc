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
struct SourceFile::Impl : private NodeImpl {
  Impl(zc::String&& fileName, zc::Vector<zc::Own<ast::Statement>>&& statements) noexcept
      : NodeImpl(SyntaxKind::SourceFile),
        fileName(zc::mv(fileName)),
        statements(zc::mv(statements)) {}

  /// Identifier of the module buffer.
  const zc::String fileName;
  /// List of toplevel statements in the module.
  const NodeList<ast::Statement> statements;

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// SourceFile
SourceFile::SourceFile(zc::String&& fileName,
                       zc::Vector<zc::Own<ast::Statement>>&& statements) noexcept
    : Node(), impl(zc::heap<Impl>(zc::mv(fileName), zc::mv(statements))) {}

SourceFile::~SourceFile() noexcept(false) = default;

const NodeList<Statement>& SourceFile::getStatements() const { return impl->statements; }

zc::StringPtr SourceFile::getFileName() const { return impl->fileName; }

SyntaxKind SourceFile::getKind() const { return SyntaxKind::SourceFile; }

void SourceFile::accept(Visitor& visitor) const { visitor.visit(*this); }

void SourceFile::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& SourceFile::getSourceRange() const { return impl->getSourceRange(); }

// ================================================================================
// ImportDeclaration::Impl
struct ImportDeclaration::Impl : private NodeImpl {
  Impl(zc::Own<ModulePath>&& modulePath, zc::Maybe<zc::Own<ast::Identifier>> alias) noexcept
      : NodeImpl(SyntaxKind::ImportDeclaration),
        modulePath(zc::mv(modulePath)),
        alias(zc::mv(alias)) {}

  const zc::Own<ModulePath> modulePath;
  const zc::Maybe<zc::Own<ast::Identifier>> alias;

  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// ImportDeclaration
ImportDeclaration::ImportDeclaration(zc::Own<ModulePath>&& modulePath,
                                     zc::Maybe<zc::Own<ast::Identifier>> alias) noexcept
    : Statement(), impl(zc::heap<Impl>(zc::mv(modulePath), zc::mv(alias))) {}

ImportDeclaration::~ImportDeclaration() noexcept(false) = default;

const ModulePath& ImportDeclaration::getModulePath() const { return *impl->modulePath; }

zc::Maybe<const ast::Identifier&> ImportDeclaration::getAlias() const { return impl->alias; }

SyntaxKind ImportDeclaration::getKind() const { return impl->getKind(); }

void ImportDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ImportDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

void ImportDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ExportDeclaration::Impl
struct ExportDeclaration::Impl : private NodeImpl {
  explicit Impl(zc::Own<ast::Expression>&& exportPath,
                zc::Maybe<zc::Own<ast::Identifier>> alias) noexcept
      : NodeImpl(SyntaxKind::ExportDeclaration),
        exportPath(zc::mv(exportPath)),
        alias(zc::mv(alias)) {}

  const zc::Own<ast::Expression> exportPath;
  const zc::Maybe<zc::Own<ast::Identifier>> alias;

  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// ExportDeclaration
ExportDeclaration::ExportDeclaration(zc::Own<ast::Expression>&& exportPath,
                                     zc::Maybe<zc::Own<ast::Identifier>> alias) noexcept
    : Statement(), impl(zc::heap<Impl>(zc::mv(exportPath), zc::mv(alias))) {}

ExportDeclaration::~ExportDeclaration() noexcept(false) = default;

const ast::Expression& ExportDeclaration::getExportPath() const { return *impl->exportPath; }

zc::Maybe<const ast::Identifier&> ExportDeclaration::getAlias() const {
  ZC_IF_SOME(alias, impl->alias) { return *alias; }
  return zc::none;
}

SyntaxKind ExportDeclaration::getKind() const { return impl->getKind(); }

void ExportDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ExportDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

void ExportDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ModulePath::Impl
struct ModulePath::Impl : private NodeImpl {
  explicit Impl(zc::Own<ast::StringLiteral>&& stringLiteral) noexcept
      : NodeImpl(SyntaxKind::ModulePath), stringLiteral(zc::mv(stringLiteral)) {}

  const zc::Own<ast::StringLiteral> stringLiteral;

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// ModulePath
ModulePath::ModulePath(zc::Own<ast::StringLiteral>&& stringLiteral) noexcept
    : Node(), impl(zc::heap<Impl>(zc::mv(stringLiteral))) {}

ModulePath::~ModulePath() noexcept(false) = default;

const ast::StringLiteral& ModulePath::getStringLiteral() const { return *impl->stringLiteral; }

SyntaxKind ModulePath::getKind() const { return SyntaxKind::ModulePath; }

void ModulePath::accept(Visitor& visitor) const { visitor.visit(*this); }

void ModulePath::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ModulePath::getSourceRange() const { return impl->getSourceRange(); }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
