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

#include "zomlang/compiler/binder/binder.h"

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/symbol-table.h"

namespace zomlang {
namespace compiler {
namespace binder {

class BinderTest {
public:
  BinderTest() : diagEngine(sourceManager) {
    // Initialize infrastructure
  }

  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine diagEngine;
  symbol::SymbolTable symbolTable;
};

// Friend class to access protected members of Binder
class TestBinder : public Binder {
public:
  TestBinder(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diagEng)
      : Binder(symbolTable, diagEng) {}

  using Binder::bindBlockStatement;
  using Binder::bindClassDeclaration;
  using Binder::bindFunctionDeclaration;
  using Binder::checkContextualIdentifier;
};

ZC_TEST("BinderTest.VariableDeclaration") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  // Create a variable declaration: var x = 42;
  auto name = ast::factory::createIdentifier("x"_zc);
  auto init = ast::factory::createIntegerLiteral(42);
  auto binding = ast::factory::createBindingElement(zc::mv(name), zc::none, zc::mv(init));

  zc::Vector<zc::Own<ast::BindingElement>> bindings;
  bindings.add(zc::mv(binding));

  auto varDecl = ast::factory::createVariableDeclarationList(zc::mv(bindings));

  // Wrap in variable declaration statement
  auto declStmt = ast::factory::createVariableStatement(zc::mv(varDecl));

  // Bind the declaration
  // Note: In a real scenario, this would be part of a larger AST traversal
  // For unit testing, we might need to expose some internal methods or wrap in a block

  // Create a block to serve as container
  zc::Vector<zc::Own<ast::Statement>> stmts;
  stmts.add(zc::mv(declStmt));
  auto block = ast::factory::createBlockStatement(zc::mv(stmts));

  binder.bindBlockStatement(*block);

  ZC_EXPECT(!test.diagEngine.hasErrors());
}

ZC_TEST("BinderTest.FunctionDeclaration") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  // Create function: func foo() {}
  auto name = ast::factory::createIdentifier("foo"_zc);
  auto body = ast::factory::createBlockStatement({});

  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<ast::BindingElement>> params;

  auto funcDecl = ast::factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams),
                                                          zc::mv(params), zc::none, zc::mv(body));

  binder.bindFunctionDeclaration(*funcDecl);

  ZC_EXPECT(!test.diagEngine.hasErrors());
}

ZC_TEST("BinderTest.ClassDeclaration") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  // Create class: class MyClass {}
  auto name = ast::factory::createIdentifier("MyClass"_zc);
  zc::Vector<zc::Own<ast::ClassElement>> members;

  auto classDecl =
      ast::factory::createClassDeclaration(zc::mv(name), {}, zc::none, zc::mv(members));

  binder.bindClassDeclaration(*classDecl);

  ZC_EXPECT(!test.diagEngine.hasErrors());
}

ZC_TEST("BinderTest.ContextualKeywords") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  // Test await as identifier in normal context (should be allowed)
  auto name = ast::factory::createIdentifier("await"_zc);
  binder.checkContextualIdentifier(*name);
  ZC_EXPECT(!test.diagEngine.hasErrors());

  // Note: To test failure cases, we'd need to set up context flags which are internal
  // or construct a full async function AST
}

ZC_TEST("BinderTest.ReservedWords") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  // Test reserved word usage
  auto name = ast::factory::createIdentifier("class"_zc);
  binder.checkContextualIdentifier(*name);
  // Should not error here as checkContextualIdentifier only checks await/yield/etc.
  // Real reserved word checking happens in parser or specific binder methods

  ZC_EXPECT(!test.diagEngine.hasErrors());
}

ZC_TEST("BinderTest.ScopeResolution") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  // Create function: func foo() { var x = 1; }
  auto name = ast::factory::createIdentifier("foo"_zc);

  // Body: { var x = 1; }
  auto varName = ast::factory::createIdentifier("x"_zc);
  auto init = ast::factory::createIntegerLiteral(1);
  auto binding = ast::factory::createBindingElement(zc::mv(varName), zc::none, zc::mv(init));
  zc::Vector<zc::Own<ast::BindingElement>> bindings;
  bindings.add(zc::mv(binding));
  auto varDecl = ast::factory::createVariableDeclarationList(zc::mv(bindings));
  auto declStmt = ast::factory::createVariableStatement(zc::mv(varDecl));

  zc::Vector<zc::Own<ast::Statement>> stmts;
  stmts.add(zc::mv(declStmt));
  auto body = ast::factory::createBlockStatement(zc::mv(stmts));

  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<ast::BindingElement>> params;

  auto funcDecl = ast::factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams),
                                                          zc::mv(params), zc::none, zc::mv(body));

  binder.bindFunctionDeclaration(*funcDecl);

  // Check that symbols were created
  ZC_EXPECT(!test.diagEngine.hasErrors());

  // Verify scope structure
  // Note: Since we don't have direct access to internal scope stack or symbol table lookups easily
  // exposed we primarily rely on no errors during binding for this unit test level
}

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
