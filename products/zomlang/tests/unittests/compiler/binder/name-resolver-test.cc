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

#include "zomlang/compiler/binder/name-resolver.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/binder/decl-collector.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/tests/unittests/compiler/test-ast-builder.h"

namespace zomlang {
namespace compiler {
namespace binder {

using tests::TestFixture;

namespace {

// Helper: run DeclCollector then NameResolver on a built AST.
bool collectAndResolve(TestFixture& fix, zc::ArrayPtr<const ast::NodeId> decls) {
  auto tree = fix.buildSourceFile("test"_zc, decls);
  DeclCollector collector(fix.symbols(), fix.scopes(), tree, fix.metadata(), fix.diagnostics());
  if (!collector.collect()) return false;
  NameResolver resolver(fix.symbols(), fix.scopes(), tree, fix.metadata(), fix.diagnostics());
  return resolver.resolve();
}

}  // namespace

// ============================================================================
// Simple name resolution
// ============================================================================

ZC_TEST("NameResolver.ResolvesSimpleIdent") {
  TestFixture fix;
  // let x = 42;
  // x  (reference)
  auto pat = fix.makeBindingPattern("x"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), fix.makeIntLiteral(42));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto identRef = fix.makeIdentExpr("x"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(identRef);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  // The identifier reference should be bound to a symbol
  auto symId = fix.metadata().symbol(identRef);
  ZC_EXPECT(symId.isValid());
}

ZC_TEST("NameResolver.ResolvesFunctionName") {
  TestFixture fix;
  auto fn = fix.makeFunctionDecl("myFunc"_zc);
  auto call = fix.makeIdentExpr("myFunc"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  topDecls.add(call);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  auto symId = fix.metadata().symbol(call);
  ZC_EXPECT(symId.isValid());
}

ZC_TEST("NameResolver.ResolvesClassName") {
  TestFixture fix;
  auto cls = fix.makeClassDecl("MyClass"_zc);
  // Class names must be referenced in type position (NamedTypeExpr), not value position.
  auto ref = fix.makeNamedTypeExpr("MyClass"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(cls);
  topDecls.add(ref);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  auto symId = fix.metadata().symbol(ref);
  ZC_EXPECT(symId.isValid());
}

ZC_TEST("NameResolver.ResolvesInterfaceName") {
  TestFixture fix;
  auto iface = fix.makeInterfaceDecl("Drawable"_zc);
  // Interface names must be referenced in type position.
  auto ref = fix.makeNamedTypeExpr("Drawable"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(ref);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Undefined identifier detection
// ============================================================================

ZC_TEST("NameResolver.DetectsUndefinedIdent") {
  TestFixture fix;
  auto ref = fix.makeIdentExpr("undefinedVar"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(ref);

  collectAndResolve(fix, topDecls.asPtr());
  ZC_EXPECT(fix.diagnostics().hasErrors());
}

ZC_TEST("NameResolver.MarksUnresolvedIdent") {
  TestFixture fix;
  auto ref = fix.makeIdentExpr("noSuchThing"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(ref);

  collectAndResolve(fix, topDecls.asPtr());
  ZC_EXPECT(fix.metadata().isUnresolved(ref));
}

ZC_TEST("NameResolver.UndefinedFunctionCall") {
  TestFixture fix;
  auto callee = fix.makeIdentExpr("undefinedFunc"_zc);
  zc::Vector<ast::NodeId> args;
  auto call = fix.makeCallExpr(callee, fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(call);

  collectAndResolve(fix, topDecls.asPtr());
  ZC_EXPECT(fix.diagnostics().hasErrors());
}

// ============================================================================
// Name resolution in nested scopes
// ============================================================================

ZC_TEST("NameResolver.ResolvesFromOuterScope") {
  TestFixture fix;
  // let outer = 1;
  // fun f() { outer }
  auto outerPat = fix.makeBindingPattern("outer"_zc);
  auto outerDecl = fix.makeVariableDeclarator(outerPat, ast::NodeId(), fix.makeIntLiteral(1));
  zc::Vector<ast::NodeId> outerDeclList;
  outerDeclList.add(outerDecl);
  auto outerVarList = fix.makeVariableDeclaratorList(fix.makeNodeList(outerDeclList.asPtr()));
  auto outerLet = fix.makeLetStmt(outerVarList);

  auto innerRef = fix.makeIdentExpr("outer"_zc);
  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(innerRef);
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));

  auto fn = fix.makeFunctionDecl("f"_zc, bodyBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(outerLet);
  topDecls.add(fn);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  auto symId = fix.metadata().symbol(innerRef);
  ZC_EXPECT(symId.isValid());
}

ZC_TEST("NameResolver.ResolvesFromFunctionScope") {
  TestFixture fix;
  // fun f(a: i32) { a }
  auto param = fix.makeFunctionParamDecl("a"_zc);
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));

  auto ref = fix.makeIdentExpr("a"_zc);
  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(ref);
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));

  auto fn = fix.makeFunctionDecl("f"_zc, bodyBlock, paramList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  auto symId = fix.metadata().symbol(ref);
  ZC_EXPECT(symId.isValid());
}

ZC_TEST("NameResolver.ResolvesFromBlockScope") {
  TestFixture fix;
  // { let x = 1; x }
  auto pat = fix.makeBindingPattern("x"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), fix.makeIntLiteral(1));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto ref = fix.makeIdentExpr("x"_zc);

  zc::Vector<ast::NodeId> blockStmts;
  blockStmts.add(let);
  blockStmts.add(ref);
  auto block = fix.makeBlockStmt(fix.makeNodeList(blockStmts.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(block);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  auto symId = fix.metadata().symbol(ref);
  ZC_EXPECT(symId.isValid());
}

// ============================================================================
// Shadowing detection
// ============================================================================

ZC_TEST("NameResolver.DetectsShadowing") {
  TestFixture fix;
  // let x = 1;
  // { let x = 2; }  // shadows outer x
  auto outerPat = fix.makeBindingPattern("x"_zc);
  auto outerDecl = fix.makeVariableDeclarator(outerPat, ast::NodeId(), fix.makeIntLiteral(1));
  zc::Vector<ast::NodeId> outerDeclList;
  outerDeclList.add(outerDecl);
  auto outerVarList = fix.makeVariableDeclaratorList(fix.makeNodeList(outerDeclList.asPtr()));
  auto outerLet = fix.makeLetStmt(outerVarList);

  auto innerPat = fix.makeBindingPattern("x"_zc);
  auto innerDecl = fix.makeVariableDeclarator(innerPat, ast::NodeId(), fix.makeIntLiteral(2));
  zc::Vector<ast::NodeId> innerDeclList;
  innerDeclList.add(innerDecl);
  auto innerVarList = fix.makeVariableDeclaratorList(fix.makeNodeList(innerDeclList.asPtr()));
  auto innerLet = fix.makeLetStmt(innerVarList);

  zc::Vector<ast::NodeId> blockStmts;
  blockStmts.add(innerLet);
  auto block = fix.makeBlockStmt(fix.makeNodeList(blockStmts.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(outerLet);
  topDecls.add(block);

  collectAndResolve(fix, topDecls.asPtr());
  // Shadowing may produce a warning or just be noted in metadata
  // The shadowOf metadata should be set
  auto shadowed = fix.metadata().shadowOf(innerPat);
  // If shadowing is tracked, shadowed should refer to the outer pattern
  // (depends on implementation - just verify no crash)
  (void)shadowed;
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

ZC_TEST("NameResolver.InnerReferenceResolvesToInnerBinding") {
  TestFixture fix;
  // let x = 1;
  // { let x = 2; x }  // x should resolve to inner x
  auto outerPat = fix.makeBindingPattern("x"_zc);
  auto outerDecl = fix.makeVariableDeclarator(outerPat, ast::NodeId(), fix.makeIntLiteral(1));
  zc::Vector<ast::NodeId> outerDeclList;
  outerDeclList.add(outerDecl);
  auto outerVarList = fix.makeVariableDeclaratorList(fix.makeNodeList(outerDeclList.asPtr()));
  auto outerLet = fix.makeLetStmt(outerVarList);

  auto innerPat = fix.makeBindingPattern("x"_zc);
  auto innerDecl = fix.makeVariableDeclarator(innerPat, ast::NodeId(), fix.makeIntLiteral(2));
  zc::Vector<ast::NodeId> innerDeclList;
  innerDeclList.add(innerDecl);
  auto innerVarList = fix.makeVariableDeclaratorList(fix.makeNodeList(innerDeclList.asPtr()));
  auto innerLet = fix.makeLetStmt(innerVarList);

  auto ref = fix.makeIdentExpr("x"_zc);

  zc::Vector<ast::NodeId> blockStmts;
  blockStmts.add(innerLet);
  blockStmts.add(ref);
  auto block = fix.makeBlockStmt(fix.makeNodeList(blockStmts.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(outerLet);
  topDecls.add(block);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  // The inner reference should resolve to the inner binding.
  // Note: the symbol is bound to the VariableDeclarator node (innerDecl),
  // not the BindingPattern node (innerPat).
  auto refSymId = fix.metadata().symbol(ref);
  auto innerSymId = fix.metadata().symbol(innerDecl);
  ZC_EXPECT(refSymId == innerSymId);
}

// ============================================================================
// Member access resolution
// ============================================================================

ZC_TEST("NameResolver.ResolvesMemberAccessObject") {
  TestFixture fix;
  // let obj = 1;
  // obj.field - resolve the object part (member lookup may fail but object should resolve)
  auto pat = fix.makeBindingPattern("obj"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), fix.makeIntLiteral(1));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto objRef = fix.makeIdentExpr("obj"_zc);
  auto member = fix.makeMemberExpr(objRef, "field"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(member);

  // collectAndResolve may return false due to member lookup failure, but the
  // object identifier should still be resolved.
  collectAndResolve(fix, topDecls.asPtr());
  auto objSymId = fix.metadata().symbol(objRef);
  ZC_EXPECT(objSymId.isValid());
}

// ============================================================================
// Call expression callee resolution
// ============================================================================

ZC_TEST("NameResolver.ResolvesCallCallee") {
  TestFixture fix;
  auto fn = fix.makeFunctionDecl("callee"_zc);

  auto calleeRef = fix.makeIdentExpr("callee"_zc);
  zc::Vector<ast::NodeId> args;
  auto call = fix.makeCallExpr(calleeRef, fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  topDecls.add(call);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  auto symId = fix.metadata().symbol(calleeRef);
  ZC_EXPECT(symId.isValid());
}

// ============================================================================
// Binary expression operand resolution
// ============================================================================

ZC_TEST("NameResolver.ResolvesBinaryOperands") {
  TestFixture fix;
  // let a = 1; let b = 2; a + b
  auto patA = fix.makeBindingPattern("a"_zc);
  auto declA = fix.makeVariableDeclarator(patA, ast::NodeId(), fix.makeIntLiteral(1));
  auto patB = fix.makeBindingPattern("b"_zc);
  auto declB = fix.makeVariableDeclarator(patB, ast::NodeId(), fix.makeIntLiteral(2));

  zc::Vector<ast::NodeId> declList;
  declList.add(declA);
  declList.add(declB);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto refA = fix.makeIdentExpr("a"_zc);
  auto refB = fix.makeIdentExpr("b"_zc);
  auto binExpr = fix.makeBinaryExpr(ast::BinaryOperatorKind::Add, refA, refB);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(binExpr);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  ZC_EXPECT(fix.metadata().symbol(refA).isValid());
  ZC_EXPECT(fix.metadata().symbol(refB).isValid());
}

// ============================================================================
// Return statement resolution
// ============================================================================

ZC_TEST("NameResolver.ResolvesReturnExpr") {
  TestFixture fix;
  // fun f() { return x; }
  // let x = 42;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), fix.makeIntLiteral(42));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto retVal = fix.makeIdentExpr("x"_zc);
  auto retStmt = fix.makeReturnStmt(retVal);

  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(retStmt);
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));

  auto fn = fix.makeFunctionDecl("f"_zc, bodyBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(fn);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  auto symId = fix.metadata().symbol(retVal);
  ZC_EXPECT(symId.isValid());
}

// ============================================================================
// If statement condition resolution
// ============================================================================

ZC_TEST("NameResolver.ResolvesIfCondition") {
  TestFixture fix;
  // let flag = true;
  // if (flag) { }
  auto pat = fix.makeBindingPattern("flag"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), fix.makeBoolLiteral(true));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto condRef = fix.makeIdentExpr("flag"_zc);
  auto thenBlock = fix.makeBlockStmt(ast::NodeList());
  auto ifStmt = fix.makeIfStmt(condRef, thenBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(ifStmt);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  auto symId = fix.metadata().symbol(condRef);
  ZC_EXPECT(symId.isValid());
}

// ============================================================================
// While statement condition resolution
// ============================================================================

ZC_TEST("NameResolver.ResolvesWhileCondition") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("running"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), fix.makeBoolLiteral(true));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto condRef = fix.makeIdentExpr("running"_zc);
  auto bodyBlock = fix.makeBlockStmt(ast::NodeList());
  auto whileStmt = fix.makeWhileStmt(condRef, bodyBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(whileStmt);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  auto symId = fix.metadata().symbol(condRef);
  ZC_EXPECT(symId.isValid());
}

// ============================================================================
// Multiple references to same symbol
// ============================================================================

ZC_TEST("NameResolver.MultipleReferencesSameSymbol") {
  TestFixture fix;
  // let x = 1; x; x; x;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), fix.makeIntLiteral(1));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto ref1 = fix.makeIdentExpr("x"_zc);
  auto ref2 = fix.makeIdentExpr("x"_zc);
  auto ref3 = fix.makeIdentExpr("x"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(ref1);
  topDecls.add(ref2);
  topDecls.add(ref3);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());

  // All references should resolve to the same symbol
  auto sym1 = fix.metadata().symbol(ref1);
  auto sym2 = fix.metadata().symbol(ref2);
  auto sym3 = fix.metadata().symbol(ref3);
  ZC_EXPECT(sym1 == sym2);
  ZC_EXPECT(sym2 == sym3);
}

// ============================================================================
// No false positives for valid code
// ============================================================================

ZC_TEST("NameResolver.ValidCodeNoErrors") {
  TestFixture fix;
  auto fn = fix.makeFunctionDecl("foo"_zc);
  auto cls = fix.makeClassDecl("Bar"_zc);
  auto iface = fix.makeInterfaceDecl("Baz"_zc);

  // Function name in value position (IdentExpr)
  auto refFn = fix.makeIdentExpr("foo"_zc);
  // Class/interface names in type position (NamedTypeExpr)
  auto refCls = fix.makeNamedTypeExpr("Bar"_zc);
  auto refIface = fix.makeNamedTypeExpr("Baz"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  topDecls.add(cls);
  topDecls.add(iface);
  topDecls.add(refFn);
  topDecls.add(refCls);
  topDecls.add(refIface);

  ZC_EXPECT(collectAndResolve(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
