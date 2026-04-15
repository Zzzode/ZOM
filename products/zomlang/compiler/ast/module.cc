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
  Impl(zc::String&& fileName, zc::Maybe<zc::Own<ModuleDeclaration>> moduleDeclaration,
       zc::Vector<zc::Own<ast::Statement>>&& statements) noexcept
      : NodeImpl(SyntaxKind::SourceFile),
        fileName(zc::mv(fileName)),
        moduleDeclaration(zc::mv(moduleDeclaration)),
        statements(zc::mv(statements)) {}

  /// Identifier of the module buffer.
  const zc::String fileName;
  /// Module declaration.
  const zc::Maybe<zc::Own<ModuleDeclaration>> moduleDeclaration;
  /// List of toplevel statements in the module.
  const NodeList<ast::Statement> statements;

  // Forward NodeImpl methods
  using NodeImpl::getFlags;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// SourceFile
SourceFile::SourceFile(zc::String&& fileName,
                       zc::Maybe<zc::Own<ModuleDeclaration>> moduleDeclaration,
                       zc::Vector<zc::Own<ast::Statement>>&& statements) noexcept
    : Node(),
      impl(zc::heap<Impl>(zc::mv(fileName), zc::mv(moduleDeclaration), zc::mv(statements))) {}

SourceFile::~SourceFile() noexcept(false) = default;

zc::Maybe<const ModuleDeclaration&> SourceFile::getModuleDeclaration() const {
  ZC_IF_SOME(moduleDeclaration, impl->moduleDeclaration) { return *moduleDeclaration; }
  return zc::none;
}

const NodeList<Statement>& SourceFile::getStatements() const { return impl->statements; }

zc::StringPtr SourceFile::getFileName() const { return impl->fileName; }

SyntaxKind SourceFile::getKind() const { return SyntaxKind::SourceFile; }

NodeFlags SourceFile::getFlags() const { return NodeFlags::None; }
void SourceFile::setFlags(NodeFlags flags) {}

void SourceFile::accept(Visitor& visitor) const { visitor.visit(*this); }

void SourceFile::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& SourceFile::getSourceRange() const { return impl->getSourceRange(); }

struct ModuleDeclaration::Impl : private NodeImpl {
  explicit Impl(zc::Own<ModulePath>&& modulePath) noexcept
      : NodeImpl(SyntaxKind::ModuleDeclaration), modulePath(zc::mv(modulePath)) {}

  const zc::Own<ModulePath> modulePath;

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ModuleDeclaration::ModuleDeclaration(zc::Own<ModulePath>&& modulePath) noexcept
    : Statement(), impl(zc::heap<Impl>(zc::mv(modulePath))) {}

ModuleDeclaration::~ModuleDeclaration() noexcept(false) = default;

const ModulePath& ModuleDeclaration::getModulePath() const { return *impl->modulePath; }

SyntaxKind ModuleDeclaration::getKind() const { return impl->getKind(); }

NodeFlags ModuleDeclaration::getFlags() const { return impl->getFlags(); }
void ModuleDeclaration::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ModuleDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ModuleDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

void ModuleDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

struct ModulePath::Impl : private NodeImpl {
  explicit Impl(zc::Vector<zc::Own<ast::Identifier>>&& segments) noexcept
      : NodeImpl(SyntaxKind::ModulePath), segments(zc::mv(segments)) {}

  const NodeList<ast::Identifier> segments;

  using NodeImpl::getFlags;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ModulePath::ModulePath(zc::Vector<zc::Own<ast::Identifier>>&& segments) noexcept
    : Node(), impl(zc::heap<Impl>(zc::mv(segments))) {}

ModulePath::~ModulePath() noexcept(false) = default;

const NodeList<ast::Identifier>& ModulePath::getSegments() const { return impl->segments; }

SyntaxKind ModulePath::getKind() const { return SyntaxKind::ModulePath; }

NodeFlags ModulePath::getFlags() const { return NodeFlags::None; }
void ModulePath::setFlags(NodeFlags flags) {}

void ModulePath::accept(Visitor& visitor) const { visitor.visit(*this); }

void ModulePath::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ModulePath::getSourceRange() const { return impl->getSourceRange(); }

struct ImportSpecifier::Impl : private NodeImpl {
  Impl(zc::Own<ast::Identifier>&& importedName, zc::Maybe<zc::Own<ast::Identifier>> alias) noexcept
      : NodeImpl(SyntaxKind::ImportSpecifier),
        importedName(zc::mv(importedName)),
        alias(zc::mv(alias)) {}

  const zc::Own<ast::Identifier> importedName;
  const zc::Maybe<zc::Own<ast::Identifier>> alias;

  // Forward NodeImpl methods
  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ImportSpecifier::ImportSpecifier(zc::Own<ast::Identifier>&& importedName,
                                 zc::Maybe<zc::Own<ast::Identifier>> alias) noexcept
    : Node(), impl(zc::heap<Impl>(zc::mv(importedName), zc::mv(alias))) {}

ImportSpecifier::~ImportSpecifier() noexcept(false) = default;

const ast::Identifier& ImportSpecifier::getImportedName() const { return *impl->importedName; }

zc::Maybe<const ast::Identifier&> ImportSpecifier::getAlias() const {
  ZC_IF_SOME(alias, impl->alias) { return *alias; }
  return zc::none;
}

SyntaxKind ImportSpecifier::getKind() const { return impl->getKind(); }

NodeFlags ImportSpecifier::getFlags() const { return impl->getFlags(); }
void ImportSpecifier::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void ImportSpecifier::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ImportSpecifier::getSourceRange() const {
  return impl->getSourceRange();
}

void ImportSpecifier::accept(Visitor& visitor) const { visitor.visit(*this); }

struct ImportDeclaration::Impl : private NodeImpl {
  Impl(zc::Own<ModulePath>&& modulePath, zc::Maybe<zc::Own<ast::Identifier>> alias,
       zc::Vector<zc::Own<ImportSpecifier>>&& specifiers) noexcept
      : NodeImpl(SyntaxKind::ImportDeclaration),
        modulePath(zc::mv(modulePath)),
        alias(zc::mv(alias)),
        specifiers(zc::mv(specifiers)) {}

  const zc::Own<ModulePath> modulePath;
  const zc::Maybe<zc::Own<ast::Identifier>> alias;
  const NodeList<ImportSpecifier> specifiers;

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ImportDeclaration::ImportDeclaration(zc::Own<ModulePath>&& modulePath,
                                     zc::Maybe<zc::Own<ast::Identifier>> alias,
                                     zc::Vector<zc::Own<ImportSpecifier>>&& specifiers) noexcept
    : Statement(), impl(zc::heap<Impl>(zc::mv(modulePath), zc::mv(alias), zc::mv(specifiers))) {}

ImportDeclaration::~ImportDeclaration() noexcept(false) = default;

const ModulePath& ImportDeclaration::getModulePath() const { return *impl->modulePath; }

zc::Maybe<const ast::Identifier&> ImportDeclaration::getAlias() const {
  ZC_IF_SOME(alias, impl->alias) { return *alias; }
  return zc::none;
}

const NodeList<ImportSpecifier>& ImportDeclaration::getSpecifiers() const {
  return impl->specifiers;
}

bool ImportDeclaration::isModuleImport() const { return impl->specifiers.size() == 0; }

bool ImportDeclaration::isNamedImport() const { return impl->specifiers.size() > 0; }

SyntaxKind ImportDeclaration::getKind() const { return impl->getKind(); }

NodeFlags ImportDeclaration::getFlags() const { return impl->getFlags(); }
void ImportDeclaration::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ImportDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ImportDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

void ImportDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

struct ExportSpecifier::Impl : private NodeImpl {
  Impl(zc::Own<ast::Identifier>&& exportedName, zc::Maybe<zc::Own<ast::Identifier>> alias) noexcept
      : NodeImpl(SyntaxKind::ExportSpecifier),
        exportedName(zc::mv(exportedName)),
        alias(zc::mv(alias)) {}

  const zc::Own<ast::Identifier> exportedName;
  const zc::Maybe<zc::Own<ast::Identifier>> alias;

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ExportSpecifier::ExportSpecifier(zc::Own<ast::Identifier>&& exportedName,
                                 zc::Maybe<zc::Own<ast::Identifier>> alias) noexcept
    : Node(), impl(zc::heap<Impl>(zc::mv(exportedName), zc::mv(alias))) {}

ExportSpecifier::~ExportSpecifier() noexcept(false) = default;

const ast::Identifier& ExportSpecifier::getExportedName() const { return *impl->exportedName; }

zc::Maybe<const ast::Identifier&> ExportSpecifier::getAlias() const {
  ZC_IF_SOME(alias, impl->alias) { return *alias; }
  return zc::none;
}

SyntaxKind ExportSpecifier::getKind() const { return impl->getKind(); }

NodeFlags ExportSpecifier::getFlags() const { return impl->getFlags(); }
void ExportSpecifier::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void ExportSpecifier::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ExportSpecifier::getSourceRange() const {
  return impl->getSourceRange();
}

void ExportSpecifier::accept(Visitor& visitor) const { visitor.visit(*this); }

struct ExportDeclaration::Impl : private NodeImpl {
  Impl(zc::Maybe<zc::Own<ModulePath>> modulePath, zc::Vector<zc::Own<ExportSpecifier>>&& specifiers,
       zc::Maybe<zc::Own<ast::Statement>> declaration) noexcept
      : NodeImpl(SyntaxKind::ExportDeclaration),
        modulePath(zc::mv(modulePath)),
        specifiers(zc::mv(specifiers)),
        declaration(zc::mv(declaration)) {}

  const zc::Maybe<zc::Own<ModulePath>> modulePath;
  const NodeList<ExportSpecifier> specifiers;
  const zc::Maybe<zc::Own<ast::Statement>> declaration;

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ExportDeclaration::ExportDeclaration(zc::Maybe<zc::Own<ModulePath>> modulePath,
                                     zc::Vector<zc::Own<ExportSpecifier>>&& specifiers,
                                     zc::Maybe<zc::Own<ast::Statement>> declaration) noexcept
    : Statement(),
      impl(zc::heap<Impl>(zc::mv(modulePath), zc::mv(specifiers), zc::mv(declaration))) {}

ExportDeclaration::~ExportDeclaration() noexcept(false) = default;

zc::Maybe<const ModulePath&> ExportDeclaration::getModulePath() const {
  ZC_IF_SOME(modulePath, impl->modulePath) { return *modulePath; }
  return zc::none;
}

const NodeList<ExportSpecifier>& ExportDeclaration::getSpecifiers() const {
  return impl->specifiers;
}

zc::Maybe<const ast::Statement&> ExportDeclaration::getDeclaration() const {
  ZC_IF_SOME(declaration, impl->declaration) { return *declaration; }
  return zc::none;
}

bool ExportDeclaration::isLocalExport() const {
  return impl->declaration == zc::none && impl->modulePath == zc::none;
}

bool ExportDeclaration::isReExport() const { return impl->modulePath != zc::none; }

bool ExportDeclaration::isDeclarationExport() const { return impl->declaration != zc::none; }

SyntaxKind ExportDeclaration::getKind() const { return impl->getKind(); }

NodeFlags ExportDeclaration::getFlags() const { return impl->getFlags(); }
void ExportDeclaration::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ExportDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ExportDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

void ExportDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
