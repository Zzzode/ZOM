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

#include "zomlang/compiler/ast/ast.h"

#include "zc/core/memory.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// Node::Impl

struct Node::Impl {
  source::SourceRange range;
  SyntaxKind kind;
  const Node* parent = nullptr;

  Impl(SyntaxKind kind) : kind(kind) {}
};

Node::Node(SyntaxKind kind) noexcept : impl(zc::heap<Impl>(kind)) {}
Node::~Node() noexcept(false) = default;

// Node sourceRange method implementation
void Node::setSourceRange(const source::SourceRange&& range) { impl->range = zc::mv(range); }
const source::SourceRange Node::sourceRange() const { return impl->range; }

// Node kind method implementations
SyntaxKind Node::getKind() const { return impl->kind; }

bool Node::isStatement() const {
  return impl->kind == SyntaxKind::kStatement || impl->kind == SyntaxKind::kImportDeclaration ||
         impl->kind == SyntaxKind::kExportDeclaration ||
         impl->kind == SyntaxKind::kVariableDeclaration ||
         impl->kind == SyntaxKind::kFunctionDeclaration ||
         impl->kind == SyntaxKind::kClassDeclaration ||
         impl->kind == SyntaxKind::kInterfaceDeclaration ||
         impl->kind == SyntaxKind::kStructDeclaration ||
         impl->kind == SyntaxKind::kEnumDeclaration ||
         impl->kind == SyntaxKind::kErrorDeclaration ||
         impl->kind == SyntaxKind::kAliasDeclaration || impl->kind == SyntaxKind::kBlockStatement ||
         impl->kind == SyntaxKind::kEmptyStatement ||
         impl->kind == SyntaxKind::kExpressionStatement || impl->kind == SyntaxKind::kIfStatement ||
         impl->kind == SyntaxKind::kWhileStatement || impl->kind == SyntaxKind::kForStatement ||
         impl->kind == SyntaxKind::kBreakStatement ||
         impl->kind == SyntaxKind::kContinueStatement ||
         impl->kind == SyntaxKind::kReturnStatement || impl->kind == SyntaxKind::kMatchStatement ||
         impl->kind == SyntaxKind::kDebuggerStatement;
}

bool Node::isExpression() const {
  return impl->kind == SyntaxKind::kExpression || impl->kind == SyntaxKind::kPrimaryExpression ||
         impl->kind == SyntaxKind::kBinaryExpression ||
         impl->kind == SyntaxKind::kUnaryExpression ||
         impl->kind == SyntaxKind::kAssignmentExpression ||
         impl->kind == SyntaxKind::kConditionalExpression ||
         impl->kind == SyntaxKind::kCallExpression || impl->kind == SyntaxKind::kMemberExpression ||
         impl->kind == SyntaxKind::kArrayLiteralExpression ||
         impl->kind == SyntaxKind::kObjectLiteralExpression ||
         impl->kind == SyntaxKind::kUpdateExpression || impl->kind == SyntaxKind::kCastExpression ||
         impl->kind == SyntaxKind::kAwaitExpression || impl->kind == SyntaxKind::kVoidExpression ||
         impl->kind == SyntaxKind::kTypeOfExpression ||
         impl->kind == SyntaxKind::kOptionalExpression || impl->kind == SyntaxKind::kIdentifier ||
         impl->kind == SyntaxKind::kBindingIdentifier || impl->kind == SyntaxKind::kLiteral ||
         impl->kind == SyntaxKind::kStringLiteral || impl->kind == SyntaxKind::kIntegerLiteral ||
         impl->kind == SyntaxKind::kFloatLiteral || impl->kind == SyntaxKind::kBooleanLiteral ||
         impl->kind == SyntaxKind::kNullLiteral;
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
