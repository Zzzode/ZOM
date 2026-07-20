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
  const zc::ArrayPtr<const DefinitionInventoryEntry> domains[] = {
      inventory.definitions(), inventory.genericParameters(), inventory.callableParameters(),
      inventory.ownerLocalBindings(), inventory.anonymousEntities()};
  for (const auto domain : domains) {
    for (const auto& entry : domain) {
      if (entry.node == node) { return entry; }
    }
  }
  ZC_UNREACHABLE;
}

const PatternBindingSite& patternSite(const DefinitionInventoryEntry& entry) {
  ZC_REQUIRE(entry.site.value().is<PatternBindingSite>());
  return entry.site.value().get<PatternBindingSite>();
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
  ZC_EXPECT(inventory.definitions().size() == 2);
  ZC_EXPECT(inventory.ownerLocalBindings().size() == 1);

  const auto& global = definitionFor(inventory, globalPattern);
  ZC_EXPECT(global.kind == identity::DefinitionKind::Constant);
  ZC_EXPECT(global.moduleNode == module);
  ZC_EXPECT(global.parentPath.empty());
  ZC_EXPECT(patternSite(global).introducer == globalDeclarator);
  ZC_EXPECT(patternSite(global).patternPath.empty());

  const auto& functionEntry = definitionFor(inventory, function);
  ZC_EXPECT(functionEntry.kind == identity::DefinitionKind::Function);
  ZC_EXPECT(functionEntry.parentPath.empty());
  ZC_REQUIRE(functionEntry.site.value().is<DeclarationDefinitionSite>());
  ZC_EXPECT(functionEntry.site.value().get<DeclarationDefinitionSite>().node == function);

  const auto& local = definitionFor(inventory, localPattern);
  ZC_EXPECT(local.kind == identity::DefinitionKind::Local);
  ZC_EXPECT(local.parentPath.size() == 1);
  ZC_EXPECT(local.parentPath[0].kind == StructuralIdentityParentKind::Definition);
  ZC_EXPECT(local.parentPath[0].node == function);
  ZC_EXPECT(patternSite(local).introducer == localDeclarator);
  ZC_EXPECT(patternSite(local).patternPath.empty());
}

ZC_TEST("DefinitionInventory.ClassifiesBindingsByDeclarationSlot") {
  TestFixture fix;
  zc::Vector<ast::NodeId> localPatterns;
  auto makeLocal = [&](zc::StringPtr name) {
    const auto pattern = fix.makeBindingPattern(name);
    localPatterns.add(pattern);
    const auto declarator = fix.makeVariableDeclarator(pattern);
    zc::Vector<ast::NodeId> declarators;
    declarators.add(declarator);
    return fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(declarators)));
  };
  auto blockWith = [&](ast::NodeId statement) {
    zc::Vector<ast::NodeId> statements;
    statements.add(statement);
    return fix.makeBlockStmt(fix.makeNodeList(statements));
  };

  const auto modulePattern = fix.makeBindingPattern("module_value"_zc);
  const auto moduleDeclarator = fix.makeVariableDeclarator(modulePattern);
  zc::Vector<ast::NodeId> moduleDeclarators;
  moduleDeclarators.add(moduleDeclarator);
  const auto moduleLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(moduleDeclarators)));

  const auto plainBlock = blockWith(makeLocal("block_value"_zc));
  const auto whileStatement =
      fix.makeWhileStmt(fix.makeIdentExpr("condition"_zc), blockWith(makeLocal("while_value"_zc)));
  const auto forStatement =
      fix.makeForStmt(makeLocal("for_value"_zc), fix.makeIdentExpr("condition"_zc), ast::NodeId(),
                      fix.makeBlockStmt(ast::NodeList()));
  const auto forInBinding = fix.makeWildcardPattern();
  ast::NodePayload forInPayload;
  forInPayload.words[ast::kForInStatementBindingWord] = forInBinding.value;
  forInPayload.words[ast::kForInStatementExpressionWord] = fix.makeIdentExpr("items"_zc).value;
  forInPayload.words[ast::kForInStatementBodyWord] = blockWith(makeLocal("for_in_value"_zc)).value;
  const auto forInStatement =
      fix.builder().makeNode(ast::SyntaxKind::ForInStatement, source::SourceRange(), forInPayload);
  const auto matchArm =
      fix.makeMatchArm(fix.makeWildcardPattern(), blockWith(makeLocal("match_value"_zc)));
  zc::Vector<ast::NodeId> matchArms;
  matchArms.add(matchArm);
  const auto matchStatement =
      fix.makeMatchStmt(fix.makeIdentExpr("subject"_zc), fix.makeNodeList(matchArms));
  const auto unsafeBlock = fix.makeUnsafeBlockExpr(blockWith(makeLocal("unsafe_value"_zc)));

  const auto module = fix.makeModuleDecl("inventory"_zc);
  zc::Vector<ast::NodeId> sourceItems;
  sourceItems.add(fix.makeStatementListItem(moduleLet));
  sourceItems.add(fix.makeStatementListItem(plainBlock));
  sourceItems.add(fix.makeStatementListItem(whileStatement));
  sourceItems.add(fix.makeStatementListItem(forStatement));
  sourceItems.add(fix.makeStatementListItem(forInStatement));
  sourceItems.add(fix.makeStatementListItem(matchStatement));
  sourceItems.add(fix.makeStatementListItem(unsafeBlock));
  fix.makeSourceFile(module, fix.makeNodeList(sourceItems));

  const auto inventory = DefinitionInventory::collect(fix.finishTree());
  ZC_EXPECT(inventory.definitions().size() == 1);
  ZC_EXPECT(inventory.ownerLocalBindings().size() == 6);
  ZC_EXPECT(definitionFor(inventory, modulePattern).kind == identity::DefinitionKind::Static);
  ZC_REQUIRE(localPatterns.size() == 6);
  for (const auto pattern : localPatterns) {
    ZC_EXPECT(definitionFor(inventory, pattern).kind == identity::DefinitionKind::Local);
  }
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
  const auto impl = fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Target"_zc),
                                               fix.makeNamedTypeExpr("Selectable"_zc), memberList);

  zc::Vector<ast::NodeId> declarations;
  declarations.add(impl);
  const ast::Tree tree = fix.buildSourceFile("inventory"_zc, declarations);
  const auto inventory = DefinitionInventory::collect(tree);

  ZC_EXPECT(inventory.impls().size() == 1);
  ZC_EXPECT(inventory.impls()[0].node == impl);
  ZC_EXPECT(inventory.definitions().size() == 1);
  ZC_EXPECT(inventory.ownerLocalBindings().size() == 1);
  const auto& methodEntry = definitionFor(inventory, method);
  ZC_EXPECT(methodEntry.kind == identity::DefinitionKind::Method);
  ZC_EXPECT(methodEntry.parentPath.size() == 1);
  ZC_EXPECT(methodEntry.parentPath[0].kind == StructuralIdentityParentKind::Impl);
  ZC_EXPECT(methodEntry.parentPath[0].node == impl);

  const auto& binding = definitionFor(inventory, armPattern);
  ZC_EXPECT(binding.kind == identity::DefinitionKind::PatternBinding);
  ZC_EXPECT(binding.parentPath.size() == 2);
  ZC_EXPECT(binding.parentPath[1].node == method);
  ZC_EXPECT(patternSite(binding).introducer == arm);
  ZC_EXPECT(patternSite(binding).patternPath.empty());
}

ZC_TEST("DefinitionInventory.RecordsExactPatternIntroducersAndSchemaPaths") {
  TestFixture fix;

  const auto first = fix.makeIdentifierPattern("first"_zc);
  const auto nested = fix.makeIdentifierPattern("nested"_zc);
  const auto whole = fix.makeBindingPattern("whole"_zc, false, nested);
  zc::Vector<ast::NodeId> tupleElements;
  tupleElements.add(first);
  tupleElements.add(whole);
  const auto tuple = fix.makeTuplePattern(fix.makeNodeList(tupleElements));
  const auto declarator = fix.makeVariableDeclarator(tuple);
  zc::Vector<ast::NodeId> declarators;
  declarators.add(declarator);
  const auto declarationList = fix.makeVariableDeclaratorList(fix.makeNodeList(declarators));
  const auto global =
      fix.makeLetStmt(declarationList, static_cast<uint8_t>(ast::BindingDeclarationKind::Const));

  const auto loopBinding = fix.makeIdentifierPattern("item"_zc);
  ast::NodePayload forPayload;
  forPayload.words[ast::kForInStatementBindingWord] = loopBinding.value;
  forPayload.words[ast::kForInStatementExpressionWord] = fix.makeIdentExpr("items"_zc).value;
  forPayload.words[ast::kForInStatementBodyWord] = fix.makeBlockStmt(ast::NodeList()).value;
  const auto forIn =
      fix.builder().makeNode(ast::SyntaxKind::ForInStatement, source::SourceRange(), forPayload);

  const auto armBinding = fix.makeIdentifierPattern("selected"_zc);
  const auto arm = fix.makeMatchArm(armBinding, fix.makeBlockStmt(ast::NodeList()));
  zc::Vector<ast::NodeId> arms;
  arms.add(arm);
  const auto match = fix.makeMatchStmt(fix.makeIdentExpr("value"_zc), fix.makeNodeList(arms));
  zc::Vector<ast::NodeId> statements;
  statements.add(forIn);
  statements.add(match);
  const auto function =
      fix.makeFunctionDecl("run"_zc, fix.makeBlockStmt(fix.makeNodeList(statements)));

  const auto module = fix.makeModuleDecl("inventory"_zc);
  zc::Vector<ast::NodeId> sourceItems;
  sourceItems.add(fix.makeStatementListItem(global));
  sourceItems.add(fix.makeStatementListItem(function));
  fix.makeSourceFile(module, fix.makeNodeList(sourceItems));
  const auto inventory = DefinitionInventory::collect(fix.finishTree());
  ZC_EXPECT(inventory.definitions().size() == 4);
  ZC_EXPECT(inventory.ownerLocalBindings().size() == 2);

  const auto& firstSite = patternSite(definitionFor(inventory, first));
  ZC_REQUIRE(firstSite.patternPath.size() == 2);
  ZC_EXPECT(firstSite.introducer == declarator);
  ZC_EXPECT(firstSite.patternPath[0] == 0);
  ZC_EXPECT(firstSite.patternPath[1] == 0);

  const auto& wholeSite = patternSite(definitionFor(inventory, whole));
  ZC_REQUIRE(wholeSite.patternPath.size() == 2);
  ZC_EXPECT(wholeSite.patternPath[0] == 0);
  ZC_EXPECT(wholeSite.patternPath[1] == 1);

  const auto& nestedSite = patternSite(definitionFor(inventory, nested));
  ZC_REQUIRE(nestedSite.patternPath.size() == 3);
  ZC_EXPECT(nestedSite.patternPath[0] == 0);
  ZC_EXPECT(nestedSite.patternPath[1] == 1);
  ZC_EXPECT(nestedSite.patternPath[2] == 3);

  const auto& loopSite = patternSite(definitionFor(inventory, loopBinding));
  ZC_EXPECT(loopSite.introducer == forIn);
  ZC_EXPECT(loopSite.patternPath.empty());
  const auto& armSite = patternSite(definitionFor(inventory, armBinding));
  ZC_EXPECT(armSite.introducer == arm);
  ZC_EXPECT(armSite.patternPath.empty());

  const auto clone = inventory.clone();
  const auto& clonedFirst = patternSite(definitionFor(clone, first));
  ZC_REQUIRE(clonedFirst.patternPath.size() == 2);
  ZC_EXPECT(clonedFirst.introducer == declarator);
  ZC_EXPECT(clonedFirst.patternPath[0] == 0);
  ZC_EXPECT(clonedFirst.patternPath[1] == 0);
}

ZC_TEST("DefinitionInventory.RecordsNestedStructArrayEnumAndRestPaths") {
  TestFixture fix;
  auto& builder = fix.builder();

  ast::NodePayload shortPropertyPayload;
  shortPropertyPayload.words[ast::kPatternPropertyNameWord] =
      builder.internIdent("short_value"_zc).value;
  shortPropertyPayload.words[ast::kPatternPropertyShortFormWord] = 1;
  const auto shortProperty = builder.makeNode(ast::SyntaxKind::PatternProperty,
                                              source::SourceRange(), shortPropertyPayload);

  const auto enumLeaf = fix.makeIdentifierPattern("enum_value"_zc);
  zc::Vector<ast::NodeId> enumArguments;
  enumArguments.add(enumLeaf);
  const auto enumPattern = fix.makeEnumPattern("Choice"_zc, fix.makeNodeList(enumArguments));
  ast::NodePayload arrayRestPayload;
  arrayRestPayload.words[ast::kRestPatternBindingWord] = builder.internIdent("array_rest"_zc).value;
  const auto arrayRest =
      builder.makeNode(ast::SyntaxKind::RestPattern, source::SourceRange(), arrayRestPayload);
  zc::Vector<ast::NodeId> arrayElements;
  arrayElements.add(enumPattern);
  const auto arrayElementList = fix.makeNodeList(arrayElements);
  ast::NodePayload arrayPayload;
  arrayPayload.words[ast::kArrayPatternPatsFirstWord] = arrayElementList.first;
  arrayPayload.words[ast::kArrayPatternPatsSizeWord] = arrayElementList.size;
  arrayPayload.words[ast::kArrayPatternRestWord] = arrayRest.value;
  const auto arrayPattern =
      builder.makeNode(ast::SyntaxKind::ArrayPattern, source::SourceRange(), arrayPayload);

  ast::NodePayload longPropertyPayload;
  longPropertyPayload.words[ast::kPatternPropertyNameWord] = builder.internIdent("items"_zc).value;
  longPropertyPayload.words[ast::kPatternPropertyPatWord] = arrayPattern.value;
  const auto longProperty = builder.makeNode(ast::SyntaxKind::PatternProperty,
                                             source::SourceRange(), longPropertyPayload);
  ast::NodePayload structRestPayload;
  structRestPayload.words[ast::kRestPatternBindingWord] =
      builder.internIdent("struct_rest"_zc).value;
  const auto structRest =
      builder.makeNode(ast::SyntaxKind::RestPattern, source::SourceRange(), structRestPayload);
  zc::Vector<ast::NodeId> fields;
  fields.add(shortProperty);
  fields.add(longProperty);
  const auto fieldList = fix.makeNodeList(fields);
  ast::NodePayload structPayload;
  structPayload.words[ast::kStructPatternTyPathWord] = fix.makeModulePath("Record"_zc).value;
  structPayload.words[ast::kStructPatternFieldsFirstWord] = fieldList.first;
  structPayload.words[ast::kStructPatternFieldsSizeWord] = fieldList.size;
  structPayload.words[ast::kStructPatternRestWord] = structRest.value;
  const auto structPattern =
      builder.makeNode(ast::SyntaxKind::StructPattern, source::SourceRange(), structPayload);

  const auto declarator = fix.makeVariableDeclarator(structPattern);
  zc::Vector<ast::NodeId> declarators;
  declarators.add(declarator);
  const auto list = fix.makeVariableDeclaratorList(fix.makeNodeList(declarators));
  zc::Vector<ast::NodeId> declarations;
  declarations.add(fix.makeLetStmt(list, static_cast<uint8_t>(ast::BindingDeclarationKind::Const)));
  const auto inventory =
      DefinitionInventory::collect(fix.buildSourceFile("inventory"_zc, declarations));

  const auto& shortSite = patternSite(definitionFor(inventory, shortProperty));
  ZC_REQUIRE(shortSite.patternPath.size() == 2);
  ZC_EXPECT(shortSite.patternPath[0] == 1);
  ZC_EXPECT(shortSite.patternPath[1] == 0);
  const auto& enumSite = patternSite(definitionFor(inventory, enumLeaf));
  const uint32_t expectedEnumPath[] = {1, 1, 2, 0, 0, 1, 0};
  ZC_REQUIRE(enumSite.patternPath.size() == zc::size(expectedEnumPath));
  for (size_t index = 0; index < zc::size(expectedEnumPath); ++index) {
    ZC_EXPECT(enumSite.patternPath[index] == expectedEnumPath[index]);
  }
  const auto& arrayRestSite = patternSite(definitionFor(inventory, arrayRest));
  const uint32_t expectedArrayRestPath[] = {1, 1, 2, 1};
  ZC_REQUIRE(arrayRestSite.patternPath.size() == zc::size(expectedArrayRestPath));
  for (size_t index = 0; index < zc::size(expectedArrayRestPath); ++index) {
    ZC_EXPECT(arrayRestSite.patternPath[index] == expectedArrayRestPath[index]);
  }
  const auto& structRestSite = patternSite(definitionFor(inventory, structRest));
  ZC_REQUIRE(structRestSite.patternPath.size() == 1);
  ZC_EXPECT(structRestSite.patternPath[0] == 2);
  ZC_EXPECT(structRestSite.introducer == declarator);
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
  ZC_EXPECT(inventory.definitions().size() == 1);
  ZC_EXPECT(inventory.anonymousEntities().size() == 1);
  const auto& closure = definitionFor(inventory, lambda);

  ZC_EXPECT(closure.kind == identity::DefinitionKind::Closure);
  ZC_EXPECT(closure.nameKind == InventoryDefinitionNameKind::Anonymous);
  ZC_IF_SOME(role, closure.anonymousRole) {
    ZC_EXPECT(role == AnonymousSyntaxRole::Lambda);
  } else {
    ZC_EXPECT(false);
  }
}

ZC_TEST("DefinitionInventory.RecordsConstructorAndParameterParents") {
  TestFixture fix;

  auto& builder = fix.builder();
  ast::NodePayload parameterPayload;
  parameterPayload.words[ast::kFunctionParameterDeclNameWord] =
      builder.internIdent("value"_zc).value;
  const auto parameter = builder.makeNode(ast::SyntaxKind::FunctionParameterDecl,
                                          source::SourceRange(), parameterPayload);
  zc::Vector<ast::NodeId> parameters;
  parameters.add(parameter);
  const auto parameterNodes = builder.makeList(parameters.asPtr());
  ast::NodePayload parameterListPayload;
  parameterListPayload.words[ast::kFunctionParameterListNparamsWord] = 1;
  parameterListPayload.words[ast::kFunctionParameterListParamsFirstWord] = parameterNodes.first;
  parameterListPayload.words[ast::kFunctionParameterListParamsSizeWord] = parameterNodes.size;
  const auto parameterList = builder.makeNode(ast::SyntaxKind::FunctionParameterList,
                                              source::SourceRange(), parameterListPayload);

  ast::NodePayload constructorPayload;
  constructorPayload.words[ast::kConstructorDeclNameWord] = builder.internIdent("init"_zc).value;
  constructorPayload.words[ast::kConstructorDeclParamsIdWord] = parameterList.value;
  constructorPayload.words[ast::kConstructorDeclBodyWord] =
      fix.makeBlockStmt(ast::NodeList()).value;
  const auto constructor =
      builder.makeNode(ast::SyntaxKind::ConstructorDecl, source::SourceRange(), constructorPayload);

  ast::NodePayload emptyParameterListPayload;
  const auto emptyParameterList = builder.makeNode(
      ast::SyntaxKind::FunctionParameterList, source::SourceRange(), emptyParameterListPayload);
  ast::NodePayload destructorPayload;
  destructorPayload.words[ast::kDestructorDeclNameWord] = builder.internIdent("deinit"_zc).value;
  destructorPayload.words[ast::kDestructorDeclParamsIdWord] = emptyParameterList.value;
  destructorPayload.words[ast::kDestructorDeclBodyWord] = fix.makeBlockStmt(ast::NodeList()).value;
  const auto destructor =
      builder.makeNode(ast::SyntaxKind::DestructorDecl, source::SourceRange(), destructorPayload);
  zc::Vector<ast::NodeId> members;
  members.add(constructor);
  members.add(destructor);
  const auto classNode = fix.makeClassDecl("Owner"_zc, ast::NodeId(),
                                           fix.makeClassMemberList(fix.makeNodeList(members)));
  zc::Vector<ast::NodeId> declarations;
  declarations.add(classNode);
  const ast::Tree tree = fix.buildSourceFile("inventory"_zc, declarations);

  const auto inventory = DefinitionInventory::collect(tree);
  ZC_EXPECT(inventory.definitions().size() == 3);
  ZC_EXPECT(inventory.callableParameters().size() == 1);
  const auto& constructorEntry = definitionFor(inventory, constructor);
  ZC_EXPECT(constructorEntry.kind == identity::DefinitionKind::Constructor);
  ZC_EXPECT(constructorEntry.parentPath.size() == 1);
  ZC_EXPECT(constructorEntry.parentPath[0].node == classNode);

  const auto& parameterEntry = definitionFor(inventory, parameter);
  ZC_EXPECT(parameterEntry.kind == identity::DefinitionKind::Parameter);
  ZC_EXPECT(parameterEntry.parentPath.size() == 2);
  ZC_EXPECT(parameterEntry.parentPath[0].node == classNode);
  ZC_EXPECT(parameterEntry.parentPath[1].node == constructor);

  const auto& destructorEntry = definitionFor(inventory, destructor);
  ZC_EXPECT(destructorEntry.kind == identity::DefinitionKind::Destructor);
  ZC_EXPECT(destructorEntry.parentPath.size() == 1);
  ZC_EXPECT(destructorEntry.parentPath[0].node == classNode);
}

}  // namespace zomlang::compiler::binder
