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
// ImportDeclaration
ImportDeclaration::ImportDeclaration() noexcept = default;

ImportDeclaration::~ImportDeclaration() noexcept(false) = default;

// ================================================================================
// ExportDeclaration
ExportDeclaration::ExportDeclaration() noexcept = default;

ExportDeclaration::~ExportDeclaration() noexcept(false) = default;

// ================================================================================
// ModulePath
ModulePath::ModulePath() noexcept = default;

ModulePath::~ModulePath() noexcept(false) = default;

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
