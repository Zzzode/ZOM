// Copyright (c) 2025 Zode.Z. All rights reserved
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

#include "zomlang/compiler/ast/statement.h"

#include "zc/core/memory.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/type.h"

namespace zomlang {
namespace compiler {
namespace ast {

ZC_TEST("StatementTest.VariableDeclaration") {
  zc::Vector<zc::Own<BindingElement>> bindings;
  auto name = factory::createIdentifier(zc::str("x"));
  auto binding = factory::createBindingElement(zc::mv(name));
  bindings.add(zc::mv(binding));

  auto decl = factory::createVariableDeclaration(zc::mv(bindings));
  ZC_EXPECT(decl->getKind() == SyntaxKind::kVariableDeclaration);
  ZC_EXPECT(decl->isStatement());
  ZC_EXPECT(decl->getBindings().size() == 1);
}

ZC_TEST("StatementTest.FunctionDeclaration") {
  auto name = factory::createIdentifier(zc::str("foo"));
  zc::Vector<zc::Own<TypeParameter>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;
  auto body = factory::createBlockStatement({});
  auto decl = factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(params),
                                                 zc::none, zc::mv(body));

  ZC_EXPECT(decl->getKind() == SyntaxKind::kFunctionDeclaration);
  ZC_EXPECT(decl->getName() != nullptr);
  // Skip name check for now
  ZC_EXPECT(decl->getBody() != nullptr);
}

ZC_TEST("StatementTest.IfStatement") {
  auto cond = factory::createBooleanLiteral(true);
  auto thenStmt = factory::createEmptyStatement();
  auto stmt = factory::createIfStatement(zc::mv(cond), zc::mv(thenStmt), zc::none);

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kIfStatement);
  auto ifStmt = static_cast<IfStatement*>(stmt.get());
  ZC_EXPECT(ifStmt->getCondition() != nullptr);
  ZC_EXPECT(ifStmt->getThenStatement() != nullptr);
  ZC_EXPECT(ifStmt->getElseStatement() == nullptr);
}

ZC_TEST("StatementTest.BlockStatement") {
  zc::Vector<zc::Own<Statement>> statements;
  statements.add(factory::createEmptyStatement());
  auto block = factory::createBlockStatement(zc::mv(statements));

  ZC_EXPECT(block->getKind() == SyntaxKind::kBlockStatement);
  ZC_EXPECT(block->getStatements().size() == 1);
}

ZC_TEST("StatementTest.ReturnStatement") {
  auto expr = factory::createFloatLiteral(42.0);
  auto stmt = factory::createReturnStatement(zc::mv(expr));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kReturnStatement);
  auto returnStmt = static_cast<ReturnStatement*>(stmt.get());
  ZC_EXPECT(returnStmt->getExpression() != nullptr);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang