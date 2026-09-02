// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "compiler/ide/snapshot-outline.h"

namespace zomlang::compiler::ide {

zc::Maybe<SnapshotOutlineCategory> projectOutlineCategory(ast::SyntaxKind kind) noexcept {
  // Only symbol-bearing top-level declaration kinds map to a category; every
  // other kind (including non-symbol declaration-range kinds such as parameter
  // and member lists) returns none and is not emitted.
  switch (kind) {
    case ast::SyntaxKind::FunctionDecl:
      return SnapshotOutlineCategory::Function;
    case ast::SyntaxKind::ClassDecl:
      return SnapshotOutlineCategory::Class;
    case ast::SyntaxKind::StructDecl:
      return SnapshotOutlineCategory::Struct;
    case ast::SyntaxKind::InterfaceDecl:
      return SnapshotOutlineCategory::Interface;
    case ast::SyntaxKind::EnumDeclaration:
      return SnapshotOutlineCategory::Enum;
    case ast::SyntaxKind::ModuleDeclaration:
      return SnapshotOutlineCategory::Module;
    case ast::SyntaxKind::ImportDeclaration:
      return SnapshotOutlineCategory::Import;
    case ast::SyntaxKind::ExportDeclaration:
      return SnapshotOutlineCategory::Export;
    case ast::SyntaxKind::AliasDecl:
      return SnapshotOutlineCategory::TypeAlias;
    case ast::SyntaxKind::StandaloneImplDecl:
    case ast::SyntaxKind::MarkerImpl:
      return SnapshotOutlineCategory::Implementation;
    case ast::SyntaxKind::ErrorDecl:
      return SnapshotOutlineCategory::Error;
    case ast::SyntaxKind::LetStmt:
      return SnapshotOutlineCategory::Variable;
    default:
      return zc::none;
  }
}

}  // namespace zomlang::compiler::ide
