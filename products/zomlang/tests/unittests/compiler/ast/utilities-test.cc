// Copyright (c) 2025 Zode.Z. All rights reserved
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

/// \file
/// \brief Unit tests for AST utility functions.
///
/// This file contains ztest-based unit tests for the utility functions in utilities.h,
/// testing syntax kind classification and other helpers.

#include "zomlang/compiler/ast/utilities.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/kinds.h"

namespace zomlang {
namespace compiler {
namespace ast {

ZC_TEST("ASTUtilities: isKeyword") {
  // Test boundary conditions
  ZC_EXPECT(isKeyword(SyntaxKind::AbstractKeyword));
  ZC_EXPECT(isKeyword(SyntaxKind::NullKeyword));

  // Test values inside range
  ZC_EXPECT(isKeyword(SyntaxKind::IfKeyword));
  ZC_EXPECT(isKeyword(SyntaxKind::ReturnKeyword));
  ZC_EXPECT(isKeyword(SyntaxKind::ClassKeyword));
  ZC_EXPECT(isKeyword(SyntaxKind::TypeKeyword));

  // Test values outside range
  ZC_EXPECT(!isKeyword(SyntaxKind::Identifier));
  ZC_EXPECT(!isKeyword(SyntaxKind::Plus));
  ZC_EXPECT(!isKeyword(SyntaxKind::LeftParen));
}

ZC_TEST("ASTUtilities: isReservedKeyword") {
  // Test boundary conditions
  ZC_EXPECT(isReservedKeyword(SyntaxKind::AbstractKeyword));
  ZC_EXPECT(isReservedKeyword(SyntaxKind::NullKeyword));

  // Test values inside range
  ZC_EXPECT(isReservedKeyword(SyntaxKind::IfKeyword));
  ZC_EXPECT(isReservedKeyword(SyntaxKind::WhileKeyword));

  // Test values outside range
  ZC_EXPECT(!isReservedKeyword(SyntaxKind::Identifier));
  ZC_EXPECT(!isReservedKeyword(SyntaxKind::Plus));
}

ZC_TEST("ASTUtilities: isIdentifierOrKeyword") {
  // Test Identifier
  ZC_EXPECT(isIdentifierOrKeyword(SyntaxKind::Identifier));

  // Test Keywords
  ZC_EXPECT(isIdentifierOrKeyword(SyntaxKind::IfKeyword));
  ZC_EXPECT(isIdentifierOrKeyword(SyntaxKind::ClassKeyword));

  // Test Non-Identifier/Non-Keyword
  ZC_EXPECT(!isIdentifierOrKeyword(SyntaxKind::Plus));
  ZC_EXPECT(!isIdentifierOrKeyword(SyntaxKind::LeftParen));
  ZC_EXPECT(!isIdentifierOrKeyword(SyntaxKind::StringLiteral));
}

ZC_TEST("ASTUtilities: isPunctuation") {
  // Test boundary conditions
  ZC_EXPECT(isPunctuation(SyntaxKind::LeftParen));
  ZC_EXPECT(isPunctuation(SyntaxKind::RightBracket));

  // Test values inside range
  ZC_EXPECT(isPunctuation(SyntaxKind::Semicolon));
  ZC_EXPECT(isPunctuation(SyntaxKind::Comma));
  ZC_EXPECT(isPunctuation(SyntaxKind::LeftBrace));

  // Test values outside range
  ZC_EXPECT(!isPunctuation(SyntaxKind::Identifier));
  ZC_EXPECT(!isPunctuation(SyntaxKind::IfKeyword));
  ZC_EXPECT(!isPunctuation(SyntaxKind::Plus));
}

ZC_TEST("ASTUtilities: isStatement") {
  // Test boundary conditions
  // Note: VariableStatement is FirstStatement and DefaultClause is LastStatement
  ZC_EXPECT(isStatement(SyntaxKind::VariableStatement));
  ZC_EXPECT(isStatement(SyntaxKind::DefaultClause));

  // Test values inside range
  ZC_EXPECT(isStatement(SyntaxKind::IfStatement));
  ZC_EXPECT(isStatement(SyntaxKind::ReturnStatement));
  ZC_EXPECT(isStatement(SyntaxKind::BlockStatement));

  // Test values outside range
  ZC_EXPECT(!isStatement(SyntaxKind::Identifier));
  ZC_EXPECT(!isStatement(SyntaxKind::IfKeyword));
  ZC_EXPECT(!isStatement(SyntaxKind::SourceFile));
  ZC_EXPECT(!isStatement(SyntaxKind::BinaryExpression));
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
