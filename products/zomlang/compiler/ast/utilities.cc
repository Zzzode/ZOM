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

#include "zomlang/compiler/ast/utilities.h"

#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/kinds.h"

namespace zomlang {
namespace compiler {
namespace ast {

bool isPropertyNameLiteral(const ast::Node& node) {
  switch (node.getKind()) {
    case SyntaxKind::Identifier:
      return true;
    default:
      return false;
  }
}

bool isDeclaration(const ast::Node& node) {
  const ast::SyntaxKind kind = node.getKind();

  return kind == ast::SyntaxKind::FunctionExpression || kind == ast::SyntaxKind::BindingElement ||
         kind == ast::SyntaxKind::ClassDeclaration ||
         kind == ast::SyntaxKind::ConstructorDeclaration ||
         kind == ast::SyntaxKind::EnumDeclaration || kind == ast::SyntaxKind::EnumMember ||
         kind == ast::SyntaxKind::ExportDeclaration ||
         kind == ast::SyntaxKind::FunctionDeclaration ||
         kind == ast::SyntaxKind::FunctionExpression || kind == ast::SyntaxKind::GetAccessor ||
         kind == ast::SyntaxKind::ImportDeclaration ||
         kind == ast::SyntaxKind::InterfaceDeclaration ||
         kind == ast::SyntaxKind::MethodDeclaration || kind == ast::SyntaxKind::MethodSignature ||

         kind == ast::SyntaxKind::ParameterDeclaration ||
         kind == ast::SyntaxKind::PropertyDeclaration ||
         kind == ast::SyntaxKind::PropertySignature || kind == ast::SyntaxKind::SetAccessor ||
         kind == ast::SyntaxKind::AliasDeclaration ||
         kind == ast::SyntaxKind::TypeParameterDeclaration ||
         kind == ast::SyntaxKind::VariableDeclarationList;
}

bool isExportable(const ast::Node& node) {
  auto kind = node.getKind();

  // Top-level declarations can be exported
  if (kind == ast::SyntaxKind::VariableDeclarationList ||
      kind == ast::SyntaxKind::FunctionDeclaration || kind == ast::SyntaxKind::ClassDeclaration ||
      kind == ast::SyntaxKind::InterfaceDeclaration || kind == ast::SyntaxKind::StructDeclaration ||
      kind == ast::SyntaxKind::EnumDeclaration || kind == ast::SyntaxKind::AliasDeclaration ||
      kind == ast::SyntaxKind::ErrorDeclaration) {
    return true;
  }

  return false;
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
