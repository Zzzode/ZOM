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

#include "zomlang/compiler/binder/definition-inventory.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/test-ast-builder.h"

namespace zomlang::compiler::binder {
namespace {

using tests::TestFixture;

const DefinitionInventoryEntry& definitionFor(const DefinitionInventory& inventory,
                                              ast::NodeId node) {
  for (const auto& entry : inventory.definitions()) {
    if (entry.node == node) { return entry; }
  }
  ZC_UNREACHABLE;
}

}  // namespace

ZC_TEST("DefinitionInventory.ClassifiesModuleAndLexicalBindings") {
  TestFixture fix;

  const auto globalPattern = fix.makeBindingPattern("global_value"_zc);
  const auto globalDeclarator = fix.makeVariableDeclarator(globalPattern);
  zc::Vector<ast::NodeId> globalDeclarators;
  globalDeclarators.add(globalDeclarator);
  const auto globalList = fix.makeVariableDeclaratorList(fix.makeNodeList(globalDeclarators));
  const auto globalConst =
      fix.makeLetStmt(globalList, static_cast<uint8_t>(ast::BindingDeclarationKind::Const));

  const auto localPattern = fix.makeBindingPattern("local_value"_zc);
  const auto localDeclarator = fix.makeVariableDeclarator(localPattern);
  zc::Vector<ast::NodeId> localDeclarators;
  localDeclarators.add(localDeclarator);
  const auto localList = fix.makeVariableDeclaratorList(fix.makeNodeList(localDeclarators));
  const auto localLet = fix.makeLetStmt(localList);
  zc::Vector<ast::NodeId> bodyStatements;
  bodyStatements.add(localLet);
  const auto body = fix.makeBlockStmt(fix.makeNodeList(bodyStatements));
  const auto function = fix.makeFunctionDecl("run"_zc, body);

  const auto module = fix.makeModuleDecl("inventory"_zc);
  zc::Vector<ast::NodeId> sourceItems;
  sourceItems.add(fix.makeStatementListItem(globalConst));
  sourceItems.add(fix.makeStatementListItem(function));
  fix.makeSourceFile(module, fix.makeNodeList(sourceItems));
  const ast::Tree tree = fix.finishTree();

  const auto inventory = DefinitionInventory::collect(tree);
  ZC_EXPECT(inventory.modules().size() == 1);
  ZC_EXPECT(inventory.modules()[0].node == module);
  ZC_EXPECT(inventory.definitions().size() == 3);

  const auto& global = definitionFor(inventory, globalPattern);
  ZC_EXPECT(global.kind == identity::DefinitionKind::Constant);
  ZC_EXPECT(global.moduleNode == module);
  ZC_EXPECT(global.parentPath.empty());

  const auto& functionEntry = definitionFor(inventory, function);
  ZC_EXPECT(functionEntry.kind == identity::DefinitionKind::Function);
  ZC_EXPECT(functionEntry.parentPath.empty());

  const auto& local = definitionFor(inventory, localPattern);
  ZC_EXPECT(local.kind == identity::DefinitionKind::Local);
  ZC_EXPECT(local.parentPath.size() == 1);
  ZC_EXPECT(local.parentPath[0].kind == StructuralIdentityParentKind::Definition);
  ZC_EXPECT(local.parentPath[0].node == function);
}

ZC_TEST("DefinitionInventory.RecordsPatternLeavesAndImplParents") {
  TestFixture fix;

  const auto armPattern = fix.makeIdentifierPattern("selected"_zc);
  const auto arm = fix.makeMatchArm(armPattern, fix.makeBlockStmt(ast::NodeList()));
  zc::Vector<ast::NodeId> arms;
  arms.add(arm);
  const auto match = fix.makeMatchStmt(fix.makeIdentExpr("value"_zc), fix.makeNodeList(arms));
  zc::Vector<ast::NodeId> methodBodyStatements;
  methodBodyStatements.add(match);
  const auto methodBody = fix.makeBlockStmt(fix.makeNodeList(methodBodyStatements));
  const auto method = fix.makeMethodDecl("select"_zc, methodBody);
  zc::Vector<ast::NodeId> members;
  members.add(method);
  const auto memberList = fix.makeClassMemberList(fix.makeNodeList(members));
  const auto impl =
      fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Target"_zc), ast::NodeId(), memberList);

  zc::Vector<ast::NodeId> declarations;
  declarations.add(impl);
  const ast::Tree tree = fix.buildSourceFile("inventory"_zc, declarations);
  const auto inventory = DefinitionInventory::collect(tree);

  ZC_EXPECT(inventory.impls().size() == 1);
  ZC_EXPECT(inventory.impls()[0].node == impl);
  const auto& methodEntry = definitionFor(inventory, method);
  ZC_EXPECT(methodEntry.kind == identity::DefinitionKind::Method);
  ZC_EXPECT(methodEntry.parentPath.size() == 1);
  ZC_EXPECT(methodEntry.parentPath[0].kind == StructuralIdentityParentKind::Impl);
  ZC_EXPECT(methodEntry.parentPath[0].node == impl);

  const auto& binding = definitionFor(inventory, armPattern);
  ZC_EXPECT(binding.kind == identity::DefinitionKind::PatternBinding);
  ZC_EXPECT(binding.parentPath.size() == 2);
  ZC_EXPECT(binding.parentPath[1].node == method);
}

ZC_TEST("DefinitionInventory.RecordsAnonymousClosureRole") {
  TestFixture fix;
  const auto lambdaBody = fix.makeIdentExpr("value"_zc);
  const auto lambda = fix.makeLambdaExpr(lambdaBody);
  const auto pattern = fix.makeBindingPattern("callback"_zc);
  const auto declarator = fix.makeVariableDeclarator(pattern, ast::NodeId(), lambda);
  zc::Vector<ast::NodeId> declarators;
  declarators.add(declarator);
  const auto list = fix.makeVariableDeclaratorList(fix.makeNodeList(declarators));
  const auto binding = fix.makeLetStmt(list);

  zc::Vector<ast::NodeId> declarations;
  declarations.add(binding);
  const ast::Tree tree = fix.buildSourceFile("inventory"_zc, declarations);
  const auto inventory = DefinitionInventory::collect(tree);
  const auto& closure = definitionFor(inventory, lambda);

  ZC_EXPECT(closure.kind == identity::DefinitionKind::Closure);
  ZC_EXPECT(closure.nameKind == InventoryDefinitionNameKind::Anonymous);
  ZC_IF_SOME(role, closure.anonymousRole) {
    ZC_EXPECT(role == identity::AnonymousDefinitionRole::Lambda);
  }
  else { ZC_EXPECT(false); }
}

}  // namespace zomlang::compiler::binder
