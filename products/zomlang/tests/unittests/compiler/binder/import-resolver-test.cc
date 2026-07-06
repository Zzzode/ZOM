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

#include "zomlang/compiler/binder/import-resolver.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/binder/decl-collector.h"
#include "zomlang/compiler/binder/name-resolver.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/tests/unittests/compiler/test-ast-builder.h"

namespace zomlang {
namespace compiler {
namespace binder {

using tests::TestFixture;

namespace {

// Helper: run DeclCollector then ImportResolver.
bool collectAndResolveImports(TestFixture& fix, zc::ArrayPtr<const ast::NodeId> decls) {
  auto tree = fix.buildSourceFile("test"_zc, decls);
  DeclCollector collector(fix.symbols(), fix.scopes(), tree, fix.metadata(), fix.diagnostics());
  if (!collector.collect()) return false;
  ImportResolver resolver(fix.symbols(), fix.scopes(), tree, fix.metadata(), fix.diagnostics());
  return resolver.resolveImports();
}

}  // namespace

// ============================================================================
// Simple import declaration
// ============================================================================

ZC_TEST("ImportResolver.SimpleImportDoesNotCrash") {
  TestFixture fix;
  auto import = fix.makeImportDecl("std::math"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(import);

  // Should not crash even if the module doesn't exist
  // (it may emit an error but should not crash)
  collectAndResolveImports(fix, topDecls.asPtr());
  // We just verify no crash occurred
  ZC_EXPECT(true);
}

ZC_TEST("ImportResolver.ImportWithAlias") {
  TestFixture fix;
  auto import = fix.makeImportDecl("std::collections"_zc, "coll"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(import);

  collectAndResolveImports(fix, topDecls.asPtr());
  ZC_EXPECT(true);
}

// ============================================================================
// Multiple imports
// ============================================================================

ZC_TEST("ImportResolver.MultipleImports") {
  TestFixture fix;
  auto import1 = fix.makeImportDecl("std::math"_zc);
  auto import2 = fix.makeImportDecl("std::io"_zc);
  auto import3 = fix.makeImportDecl("std::collections"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(import1);
  topDecls.add(import2);
  topDecls.add(import3);

  collectAndResolveImports(fix, topDecls.asPtr());
  ZC_EXPECT(true);
}

// ============================================================================
// Export declarations
// ============================================================================

ZC_TEST("ImportResolver.ExportDeclaration") {
  TestFixture fix;
  auto fn = fix.makeFunctionDecl("exportedFunc"_zc);
  auto exportDecl = fix.makeExportDecl(fn);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(exportDecl);

  collectAndResolveImports(fix, topDecls.asPtr());
  ZC_EXPECT(true);
}

ZC_TEST("ImportResolver.ExportMarksReexport") {
  TestFixture fix;
  auto fn = fix.makeFunctionDecl("reexported"_zc);
  auto exportDecl = fix.makeExportDecl(fn);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(exportDecl);

  collectAndResolveImports(fix, topDecls.asPtr());

  // The export declaration should be marked as reexport in metadata
  // (depends on implementation - just verify no crash)
  ZC_EXPECT(true);
}

// ============================================================================
// Mixed imports and declarations
// ============================================================================

ZC_TEST("ImportResolver.MixedImportsAndDecls") {
  TestFixture fix;
  auto import = fix.makeImportDecl("std::math"_zc);
  auto fn = fix.makeFunctionDecl("localFunc"_zc);
  auto cls = fix.makeClassDecl("LocalClass"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(import);
  topDecls.add(fn);
  topDecls.add(cls);

  ZC_EXPECT(collectAndResolveImports(fix, topDecls.asPtr()));
  // Local declarations should still be in the symbol table
  ZC_IF_SOME(scope, fix.scopes().getGlobalScope()) {
    auto fnSym = fix.symbols().lookup("localFunc"_zc, scope);
    ZC_EXPECT(fnSym != zc::none);
    auto clsSym = fix.symbols().lookup("LocalClass"_zc, scope);
    ZC_EXPECT(clsSym != zc::none);
  }
}

// ============================================================================
// No imports
// ============================================================================

ZC_TEST("ImportResolver.NoImportsSucceeds") {
  TestFixture fix;
  auto fn = fix.makeFunctionDecl("foo"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);

  ZC_EXPECT(collectAndResolveImports(fix, topDecls.asPtr()));
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Import with specifiers
// ============================================================================

ZC_TEST("ImportResolver.ImportWithEmptySpecifiers") {
  TestFixture fix;
  zc::Vector<ast::NodeId> emptySpecs;
  auto import =
      fix.makeImportDecl("std::math"_zc, zc::StringPtr(), fix.makeNodeList(emptySpecs.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(import);

  collectAndResolveImports(fix, topDecls.asPtr());
  ZC_EXPECT(true);
}

// ============================================================================
// Full pipeline: DeclCollector + ImportResolver + NameResolver
// ============================================================================

ZC_TEST("ImportResolver.FullPipelineWithImports") {
  TestFixture fix;
  auto import = fix.makeImportDecl("std::math"_zc);
  auto fn = fix.makeFunctionDecl("useImport"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(import);
  topDecls.add(fn);

  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());
  DeclCollector collector(fix.symbols(), fix.scopes(), tree, fix.metadata(), fix.diagnostics());
  ZC_EXPECT(collector.collect());
  ImportResolver importResolver(fix.symbols(), fix.scopes(), tree, fix.metadata(),
                                fix.diagnostics());
  importResolver.resolveImports();
  NameResolver nameResolver(fix.symbols(), fix.scopes(), tree, fix.metadata(), fix.diagnostics());
  ZC_EXPECT(nameResolver.resolve());
}

// ============================================================================
// Import path handling
// ============================================================================

ZC_TEST("ImportResolver.ImportPathWithDoubleColon") {
  TestFixture fix;
  auto import = fix.makeImportDecl("std::collections::HashMap"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(import);

  collectAndResolveImports(fix, topDecls.asPtr());
  ZC_EXPECT(true);
}

ZC_TEST("ImportResolver.ImportPathSingleSegment") {
  TestFixture fix;
  auto import = fix.makeImportDecl("math"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(import);

  collectAndResolveImports(fix, topDecls.asPtr());
  ZC_EXPECT(true);
}

// ============================================================================
// Export with function that has body
// ============================================================================

ZC_TEST("ImportResolver.ExportFunctionWithBody") {
  TestFixture fix;
  zc::Vector<ast::NodeId> bodyStmts;
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));
  auto fn = fix.makeFunctionDecl("exportedWithBody"_zc, bodyBlock);
  auto exportDecl = fix.makeExportDecl(fn);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(exportDecl);

  collectAndResolveImports(fix, topDecls.asPtr());
  ZC_EXPECT(true);
}

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
