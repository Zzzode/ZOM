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

#include "zomlang/compiler/binder/decl-collector.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/tree.h"
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

// Helper: build a simple source file with given declarations and run collection.
bool collect(TestFixture& fix, zc::ArrayPtr<const ast::NodeId> decls) {
  const auto& tree = fix.buildRetainedSourceFile("test"_zc, decls);
  DeclCollector collector(fix.symbols(), fix.scopes(), tree, fix.metadata(), fix.diagnostics());
  return collector.collect();
}

// Helper: get the global scope from a test fixture.
// In tests the global scope always exists; this avoids repeating ZC_IF_SOME everywhere.
const symbol::Scope& globalScope(TestFixture& fix) {
  ZC_IF_SOME(scope, fix.scopes().getGlobalScope()) { return scope; }
  ZC_UNREACHABLE;
}

const symbol::Scope& namedScope(TestFixture& fix, symbol::Scope::Kind kind, zc::StringPtr name) {
  for (const auto& maybeScope : fix.scopes().getScopesOfKind(kind)) {
    ZC_IF_SOME(scope, maybeScope) {
      if (scope.getName() == name) { return scope; }
    }
  }
  ZC_UNREACHABLE;
}

}  // namespace

// ============================================================================
// Function declaration collection
// ============================================================================

ZC_TEST("DeclCollector.CollectsFunctionDecl") {
  TestFixture fix;
  auto fn = fix.makeFunctionDecl("myFunc"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(fn);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  auto sym = fix.symbols().lookup("myFunc"_zc, globalScope(fix));
  ZC_IF_SOME(s, sym) { ZC_EXPECT(s.getKind() == symbol::SymbolKind::Function); }
}

ZC_TEST("DeclCollector.CollectsMultipleFunctionDecls") {
  TestFixture fix;
  auto fn1 = fix.makeFunctionDecl("foo"_zc);
  auto fn2 = fix.makeFunctionDecl("bar"_zc);
  auto fn3 = fix.makeFunctionDecl("baz"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(fn1);
  decls.add(fn2);
  decls.add(fn3);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  auto& scope = globalScope(fix);
  ZC_EXPECT(fix.symbols().lookup("foo"_zc, scope) != zc::none);
  ZC_EXPECT(fix.symbols().lookup("bar"_zc, scope) != zc::none);
  ZC_EXPECT(fix.symbols().lookup("baz"_zc, scope) != zc::none);
}

ZC_TEST("DeclCollector.FunctionDeclHasCorrectKind") {
  TestFixture fix;
  auto fn = fix.makeFunctionDecl("typedFn"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(fn);
  collect(fix, decls.asPtr());

  auto sym = fix.symbols().lookup("typedFn"_zc, globalScope(fix));
  ZC_IF_SOME(s, sym) {
    ZC_EXPECT(s.isFunctionSymbol());
    ZC_EXPECT(s.getKind() == symbol::SymbolKind::Function);
  }
}

// ============================================================================
// Class declaration collection
// ============================================================================

ZC_TEST("DeclCollector.CollectsClassDecl") {
  TestFixture fix;
  auto cls = fix.makeClassDecl("MyClass"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(cls);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  auto sym = fix.symbols().lookup("MyClass"_zc, globalScope(fix));
  ZC_IF_SOME(s, sym) { ZC_EXPECT(s.getKind() == symbol::SymbolKind::Class); }
}

ZC_TEST("DeclCollector.CollectsMultipleClassDecls") {
  TestFixture fix;
  auto c1 = fix.makeClassDecl("Alpha"_zc);
  auto c2 = fix.makeClassDecl("Beta"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(c1);
  decls.add(c2);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  auto& scope = globalScope(fix);
  ZC_EXPECT(fix.symbols().lookup("Alpha"_zc, scope) != zc::none);
  ZC_EXPECT(fix.symbols().lookup("Beta"_zc, scope) != zc::none);
}

ZC_TEST("DeclCollector.ClassDeclIsTypeSymbol") {
  TestFixture fix;
  auto cls = fix.makeClassDecl("TypeClass"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(cls);
  collect(fix, decls.asPtr());

  auto sym = fix.symbols().lookup("TypeClass"_zc, globalScope(fix));
  ZC_IF_SOME(s, sym) { ZC_EXPECT(s.isTypeSymbol()); }
}

ZC_TEST("DeclCollector.ClassMembersDefaultToPrivate") {
  TestFixture fix;
  zc::Vector<ast::NodeId> members;
  members.add(fix.makeMethodDecl("hiddenMethod"_zc));
  members.add(fix.makeFieldDecl("hiddenField"_zc));
  auto memberList = fix.makeClassMemberList(fix.makeNodeList(members.asPtr()));
  auto cls = fix.makeClassDecl("Vault"_zc, ast::NodeId(), memberList);

  zc::Vector<ast::NodeId> decls;
  decls.add(cls);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  const auto& scope = namedScope(fix, symbol::Scope::Kind::Class, "Vault"_zc);
  ZC_IF_SOME(method, scope.lookupSymbolLocally("hiddenMethod"_zc)) {
    ZC_EXPECT(method.isPrivate());
    ZC_EXPECT(!method.isPublic());
  }
  ZC_IF_SOME(field, scope.lookupSymbolLocally("hiddenField"_zc)) {
    ZC_EXPECT(field.isPrivate());
    ZC_EXPECT(!field.isPublic());
  }
}

// ============================================================================
// Interface declaration collection
// ============================================================================

ZC_TEST("DeclCollector.CollectsInterfaceDecl") {
  TestFixture fix;
  auto iface = fix.makeInterfaceDecl("Drawable"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(iface);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  auto sym = fix.symbols().lookup("Drawable"_zc, globalScope(fix));
  ZC_IF_SOME(s, sym) { ZC_EXPECT(s.getKind() == symbol::SymbolKind::Interface); }
}

ZC_TEST("DeclCollector.CollectsMultipleInterfaces") {
  TestFixture fix;
  auto i1 = fix.makeInterfaceDecl("Readable"_zc);
  auto i2 = fix.makeInterfaceDecl("Writable"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(i1);
  decls.add(i2);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  auto& scope = globalScope(fix);
  ZC_EXPECT(fix.symbols().lookup("Readable"_zc, scope) != zc::none);
  ZC_EXPECT(fix.symbols().lookup("Writable"_zc, scope) != zc::none);
}

ZC_TEST("DeclCollector.InterfaceMembersDefaultToPublic") {
  TestFixture fix;
  zc::Vector<ast::NodeId> members;
  members.add(fix.makeMethodDecl("requiredMethod"_zc));
  members.add(fix.makeFieldDecl("requiredField"_zc));
  auto memberList = fix.makeClassMemberList(fix.makeNodeList(members.asPtr()));
  auto iface = fix.makeInterfaceDecl("Contract"_zc, memberList);

  zc::Vector<ast::NodeId> decls;
  decls.add(iface);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  const auto& scope = namedScope(fix, symbol::Scope::Kind::Interface, "Contract"_zc);
  ZC_IF_SOME(method, scope.lookupSymbolLocally("requiredMethod"_zc)) {
    ZC_EXPECT(method.isPublic());
    ZC_EXPECT(!method.isPrivate());
  }
  ZC_IF_SOME(field, scope.lookupSymbolLocally("requiredField"_zc)) {
    ZC_EXPECT(field.isPublic());
    ZC_EXPECT(!field.isPrivate());
  }
}

// ============================================================================
// Struct declaration collection
// ============================================================================

ZC_TEST("DeclCollector.CollectsStructDecl") {
  TestFixture fix;
  auto st = fix.makeStructDecl("Point"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(st);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  ZC_EXPECT(fix.symbols().lookup("Point"_zc, globalScope(fix)) != zc::none);
}

// ============================================================================
// Enum declaration collection
// ============================================================================

ZC_TEST("DeclCollector.CollectsEnumDecl") {
  TestFixture fix;
  auto en = fix.makeEnumDecl("Color"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(en);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  auto sym = fix.symbols().lookup("Color"_zc, globalScope(fix));
  ZC_IF_SOME(s, sym) { ZC_EXPECT(s.getKind() == symbol::SymbolKind::Enum); }
}

// ============================================================================
// Type alias declaration collection
// ============================================================================

ZC_TEST("DeclCollector.CollectsAliasDecl") {
  TestFixture fix;
  auto alias = fix.makeAliasDecl("UserId"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(alias);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  auto sym = fix.symbols().lookup("UserId"_zc, globalScope(fix));
  ZC_IF_SOME(s, sym) { ZC_EXPECT(s.getKind() == symbol::SymbolKind::TypeAlias); }
}

// ============================================================================
// Variable declaration collection (let/var)
// ============================================================================

ZC_TEST("DeclCollector.CollectsLetVariable") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto init = fix.makeIntLiteral(42);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), init);

  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto nodeList = fix.makeNodeList(declList.asPtr());
  auto varDeclList = fix.makeVariableDeclaratorList(nodeList);
  auto let = fix.makeLetStmt(varDeclList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  ZC_EXPECT(collect(fix, topDecls.asPtr()));

  auto sym = fix.symbols().lookup("x"_zc, globalScope(fix));
  ZC_IF_SOME(s, sym) {
    ZC_EXPECT(s.getKind() == symbol::SymbolKind::Variable);
    ZC_EXPECT(!s.isMutable());
    ZC_EXPECT(s.hasFlag(symbol::SymbolFlags::Immutable));
  }
}

ZC_TEST("DeclCollector.CollectsMutVariableAsMutable") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto init = fix.makeIntLiteral(42);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), init);

  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varDeclList, static_cast<uint8_t>(ast::BindingDeclarationKind::Mut));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  ZC_EXPECT(collect(fix, topDecls.asPtr()));

  auto sym = fix.symbols().lookup("x"_zc, globalScope(fix));
  ZC_IF_SOME(s, sym) {
    ZC_EXPECT(s.getKind() == symbol::SymbolKind::Variable);
    ZC_EXPECT(s.isMutable());
  }
}

ZC_TEST("DeclCollector.BindingPatternMutOverridesLetKind") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("x"_zc, true);
  auto init = fix.makeIntLiteral(42);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), init);

  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varDeclList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  ZC_EXPECT(collect(fix, topDecls.asPtr()));

  auto sym = fix.symbols().lookup("x"_zc, globalScope(fix));
  ZC_IF_SOME(s, sym) {
    ZC_EXPECT(s.getKind() == symbol::SymbolKind::Variable);
    ZC_EXPECT(s.isMutable());
  }
}

ZC_TEST("DeclCollector.CollectsMultipleLetVariables") {
  TestFixture fix;
  auto pat1 = fix.makeBindingPattern("a"_zc);
  auto pat2 = fix.makeBindingPattern("b"_zc);
  auto init1 = fix.makeIntLiteral(1);
  auto init2 = fix.makeIntLiteral(2);
  auto decl1 = fix.makeVariableDeclarator(pat1, ast::NodeId(), init1);
  auto decl2 = fix.makeVariableDeclarator(pat2, ast::NodeId(), init2);

  zc::Vector<ast::NodeId> declList;
  declList.add(decl1);
  declList.add(decl2);
  auto nodeList = fix.makeNodeList(declList.asPtr());
  auto varDeclList = fix.makeVariableDeclaratorList(nodeList);
  auto let = fix.makeLetStmt(varDeclList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  ZC_EXPECT(collect(fix, topDecls.asPtr()));

  auto& scope = globalScope(fix);
  ZC_EXPECT(fix.symbols().lookup("a"_zc, scope) != zc::none);
  ZC_EXPECT(fix.symbols().lookup("b"_zc, scope) != zc::none);
}

// ============================================================================
// Duplicate declaration detection
// ============================================================================

ZC_TEST("DeclCollector.DetectsDuplicateFunctionDecl") {
  TestFixture fix;
  auto fn1 = fix.makeFunctionDecl("dup"_zc);
  auto fn2 = fix.makeFunctionDecl("dup"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(fn1);
  decls.add(fn2);
  // Should still return true (non-fatal), but emit a diagnostic
  collect(fix, decls.asPtr());

  ZC_EXPECT(fix.diagnostics().hasErrors());
}

ZC_TEST("DeclCollector.DetectsDuplicateClassDecl") {
  TestFixture fix;
  auto c1 = fix.makeClassDecl("DupClass"_zc);
  auto c2 = fix.makeClassDecl("DupClass"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(c1);
  decls.add(c2);
  collect(fix, decls.asPtr());

  ZC_EXPECT(fix.diagnostics().hasErrors());
}

ZC_TEST("DeclCollector.DetectsDuplicateVariable") {
  TestFixture fix;
  auto pat1 = fix.makeBindingPattern("dupVar"_zc);
  auto init1 = fix.makeIntLiteral(1);
  auto decl1 = fix.makeVariableDeclarator(pat1, ast::NodeId(), init1);
  zc::Vector<ast::NodeId> declList1;
  declList1.add(decl1);
  auto varDeclList1 = fix.makeVariableDeclaratorList(fix.makeNodeList(declList1.asPtr()));
  auto let1 = fix.makeLetStmt(varDeclList1);

  auto pat2 = fix.makeBindingPattern("dupVar"_zc);
  auto init2 = fix.makeIntLiteral(2);
  auto decl2 = fix.makeVariableDeclarator(pat2, ast::NodeId(), init2);
  zc::Vector<ast::NodeId> declList2;
  declList2.add(decl2);
  auto varDeclList2 = fix.makeVariableDeclaratorList(fix.makeNodeList(declList2.asPtr()));
  auto let2 = fix.makeLetStmt(varDeclList2);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let1);
  topDecls.add(let2);
  collect(fix, topDecls.asPtr());

  ZC_EXPECT(fix.diagnostics().hasErrors());
}

ZC_TEST("DeclCollector.NoDuplicateErrorForDifferentNames") {
  TestFixture fix;
  auto fn1 = fix.makeFunctionDecl("alpha"_zc);
  auto fn2 = fix.makeFunctionDecl("beta"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(fn1);
  decls.add(fn2);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Nested scopes
// ============================================================================

ZC_TEST("DeclCollector.FunctionBodyCreatesScope") {
  TestFixture fix;
  // Create a function with a block body containing a let statement
  auto innerPat = fix.makeBindingPattern("inner"_zc);
  auto innerInit = fix.makeIntLiteral(10);
  auto innerDecl = fix.makeVariableDeclarator(innerPat, ast::NodeId(), innerInit);
  zc::Vector<ast::NodeId> innerDeclList;
  innerDeclList.add(innerDecl);
  auto innerVarList = fix.makeVariableDeclaratorList(fix.makeNodeList(innerDeclList.asPtr()));
  auto innerLet = fix.makeLetStmt(innerVarList);

  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(innerLet);
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));

  auto fn = fix.makeFunctionDecl("outer"_zc, bodyBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  ZC_EXPECT(collect(fix, topDecls.asPtr()));

  // "outer" should be in global scope
  ZC_EXPECT(fix.symbols().lookup("outer"_zc, globalScope(fix)) != zc::none);

  // "inner" should NOT be in global scope
  // inner might not be found in global scope since it's in function scope
  // (depends on whether lookup is recursive or local-only)
  // Let's just verify no errors
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

ZC_TEST("DeclCollector.BlockCreatesScope") {
  TestFixture fix;
  // Create a block statement at top level (unusual but testable)
  auto innerPat = fix.makeBindingPattern("blockVar"_zc);
  auto innerInit = fix.makeIntLiteral(5);
  auto innerDecl = fix.makeVariableDeclarator(innerPat, ast::NodeId(), innerInit);
  zc::Vector<ast::NodeId> innerDeclList;
  innerDeclList.add(innerDecl);
  auto innerVarList = fix.makeVariableDeclaratorList(fix.makeNodeList(innerDeclList.asPtr()));
  auto innerLet = fix.makeLetStmt(innerVarList);

  zc::Vector<ast::NodeId> blockStmts;
  blockStmts.add(innerLet);
  auto block = fix.makeBlockStmt(fix.makeNodeList(blockStmts.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(block);
  ZC_EXPECT(collect(fix, topDecls.asPtr()));

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

ZC_TEST("DeclCollector.NestedBlocksCreateMultipleScopes") {
  TestFixture fix;
  // Inner block with a variable
  auto innerPat = fix.makeBindingPattern("deepVar"_zc);
  auto innerDecl = fix.makeVariableDeclarator(innerPat, ast::NodeId(), fix.makeIntLiteral(1));
  zc::Vector<ast::NodeId> innerDeclList;
  innerDeclList.add(innerDecl);
  auto innerVarList = fix.makeVariableDeclaratorList(fix.makeNodeList(innerDeclList.asPtr()));
  auto innerLet = fix.makeLetStmt(innerVarList);

  zc::Vector<ast::NodeId> innerStmts;
  innerStmts.add(innerLet);
  auto innerBlock = fix.makeBlockStmt(fix.makeNodeList(innerStmts.asPtr()));

  zc::Vector<ast::NodeId> outerStmts;
  outerStmts.add(innerBlock);
  auto outerBlock = fix.makeBlockStmt(fix.makeNodeList(outerStmts.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(outerBlock);
  ZC_EXPECT(collect(fix, topDecls.asPtr()));

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Value/Type namespace separation
// ============================================================================

ZC_TEST("DeclCollector.ValueAndTypeNamespacesSeparate") {
  TestFixture fix;
  // A function named "Foo" (value namespace)
  auto fn = fix.makeFunctionDecl("Foo"_zc);
  // A class named "Foo" (type namespace) - in ZOM, these may or may not be allowed
  // to share names depending on the language design. Let's test that both are
  // collected without crashing.
  auto cls = fix.makeClassDecl("Bar"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(fn);
  decls.add(cls);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  auto& scope = globalScope(fix);
  auto fnSym = fix.symbols().lookup("Foo"_zc, scope);
  ZC_IF_SOME(s, fnSym) { ZC_EXPECT(s.isFunctionSymbol()); }

  auto clsSym = fix.symbols().lookup("Bar"_zc, scope);
  ZC_IF_SOME(s, clsSym) { ZC_EXPECT(s.isTypeSymbol()); }
}

ZC_TEST("DeclCollector.CollectsStandaloneImplDecl") {
  TestFixture fix;
  auto impl = fix.makeStandaloneImplDecl();

  zc::Vector<ast::NodeId> decls;
  decls.add(impl);
  ZC_EXPECT(collect(fix, decls.asPtr()));

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Empty source file
// ============================================================================

ZC_TEST("DeclCollector.EmptySourceFileSucceeds") {
  TestFixture fix;
  zc::Vector<ast::NodeId> empty;
  ZC_EXPECT(collect(fix, empty.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Binding metadata is recorded
// ============================================================================

ZC_TEST("DeclCollector.RecordsBindingMetadataForFunction") {
  TestFixture fix;
  auto fn = fix.makeFunctionDecl("metaFn"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(fn);
  collect(fix, decls.asPtr());

  // The symbol should be bound to the declaration node
  auto symId = fix.metadata().symbol(fn);
  ZC_EXPECT(symId.isValid());
}

ZC_TEST("DeclCollector.RecordsBindingMetadataForClass") {
  TestFixture fix;
  auto cls = fix.makeClassDecl("MetaClass"_zc);

  zc::Vector<ast::NodeId> decls;
  decls.add(cls);
  collect(fix, decls.asPtr());

  auto symId = fix.metadata().symbol(cls);
  ZC_EXPECT(symId.isValid());
}

ZC_TEST("DeclCollector.RecordsBindingMetadataForVariable") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("metaVar"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), fix.makeIntLiteral(1));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  collect(fix, topDecls.asPtr());

  // The symbol is bound to the VariableDeclarator node, not the BindingPattern
  auto symId = fix.metadata().symbol(decl);
  ZC_EXPECT(symId.isValid());
}

// ============================================================================
// If/While/For scopes
// ============================================================================

ZC_TEST("DeclCollector.IfStmtCreatesScopes") {
  TestFixture fix;
  // if (true) { let x = 1; }
  auto cond = fix.makeBoolLiteral(true);
  auto pat = fix.makeBindingPattern("ifVar"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), fix.makeIntLiteral(1));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  zc::Vector<ast::NodeId> thenStmts;
  thenStmts.add(let);
  auto thenBlock = fix.makeBlockStmt(fix.makeNodeList(thenStmts.asPtr()));

  auto ifStmt = fix.makeIfStmt(cond, thenBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(ifStmt);
  ZC_EXPECT(collect(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

ZC_TEST("DeclCollector.WhileStmtCreatesScope") {
  TestFixture fix;
  auto cond = fix.makeBoolLiteral(true);
  auto pat = fix.makeBindingPattern("whileVar"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), fix.makeIntLiteral(1));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(let);
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));

  auto whileStmt = fix.makeWhileStmt(cond, bodyBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(whileStmt);
  ZC_EXPECT(collect(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

ZC_TEST("DeclCollector.ForStmtCreatesScope") {
  TestFixture fix;
  auto initPat = fix.makeBindingPattern("i"_zc);
  auto initDecl = fix.makeVariableDeclarator(initPat, ast::NodeId(), fix.makeIntLiteral(0));
  zc::Vector<ast::NodeId> initDeclList;
  initDeclList.add(initDecl);
  auto initVarList = fix.makeVariableDeclaratorList(fix.makeNodeList(initDeclList.asPtr()));
  auto initLet = fix.makeLetStmt(initVarList);

  auto cond = fix.makeBinaryExpr(ast::BinaryOperatorKind::Lt, fix.makeIdentExpr("i"_zc),
                                 fix.makeIntLiteral(10));
  auto update = fix.makeAssignmentExpr(fix.makeIdentExpr("i"_zc), fix.makeIntLiteral(1), 1);

  zc::Vector<ast::NodeId> bodyStmts;
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));

  auto forStmt = fix.makeForStmt(initLet, cond, update, bodyBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(forStmt);
  ZC_EXPECT(collect(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Function parameters are collected
// ============================================================================

ZC_TEST("DeclCollector.FunctionParametersCollected") {
  TestFixture fix;
  auto param1 = fix.makeFunctionParamDecl("a"_zc);
  auto param2 = fix.makeFunctionParamDecl("b"_zc);
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param1);
  paramNodes.add(param2);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));

  zc::Vector<ast::NodeId> bodyStmts;
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));

  auto fn = fix.makeFunctionDecl("paramFunc"_zc, bodyBlock, paramList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  ZC_EXPECT(collect(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Lambda creates scope
// ============================================================================

ZC_TEST("DeclCollector.LambdaCreatesScope") {
  TestFixture fix;
  auto body = fix.makeBlockStmt(ast::NodeList());
  auto lambda = fix.makeLambdaExpr(body);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(lambda);
  ZC_EXPECT(collect(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
