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

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/symbol-table.h"

namespace zomlang {
namespace compiler {
namespace binder {

namespace {

ast::NodePayload makeSourceFilePayload(ast::TreeBuilder& builder, ast::NodeId module,
                                       ast::NodeList statements) {
  ast::NodePayload payload;
  payload.words[ast::kSourceFileFileNameWord] = builder.internString("test.zom"_zc).value;
  payload.words[ast::kSourceFileModuleWord] = module.value;
  payload.words[ast::kSourceFileStatementsFirstWord] = statements.first;
  payload.words[ast::kSourceFileStatementsSizeWord] = statements.size;
  return payload;
}

}  // namespace

ZC_TEST("Binder.WritesParentMetadataForSourceFileChildren") {
  ast::TreeBuilder builder;

  const ast::NodeId module =
      builder.makeNode(ast::SyntaxKind::ModuleDeclaration, source::SourceRange());
  const ast::NodeId import =
      builder.makeNode(ast::SyntaxKind::ImportDeclaration, source::SourceRange());

  // FunctionDecl needs a valid name IdentId, otherwise DeclCollector crashes.
  ast::NodePayload funcPayload;
  funcPayload.words[ast::kFunctionDeclNameWord] = builder.internIdent("testFunc"_zc).value;
  const ast::NodeId declaration =
      builder.makeNode(ast::SyntaxKind::FunctionDecl, source::SourceRange(), funcPayload);
  ast::NodePayload importItemPayload;
  importItemPayload.words[ast::kStatementListItemItemWord] = import.value;
  const ast::NodeId importItem = builder.makeNode(ast::SyntaxKind::StatementListItem,
                                                  source::SourceRange(), importItemPayload);
  ast::NodePayload declarationItemPayload;
  declarationItemPayload.words[ast::kStatementListItemItemWord] = declaration.value;
  const ast::NodeId declarationItem = builder.makeNode(
      ast::SyntaxKind::StatementListItem, source::SourceRange(), declarationItemPayload);

  zc::Vector<ast::NodeId> statements;
  statements.add(importItem);
  statements.add(declarationItem);
  const ast::NodeList statementList = builder.makeList(statements.asPtr());

  const ast::NodeId root = builder.makeNode(ast::SyntaxKind::SourceFile, source::SourceRange(),
                                            makeSourceFilePayload(builder, module, statementList));
  builder.setRoot(root);
  ast::Tree tree = builder.finish();

  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine diagnostics(sourceManager);
  symbol::SymbolTable symbols;
  ast::BindingMetadata metadata;

  Binder binder(symbols, diagnostics, tree, metadata);

  ZC_EXPECT(binder.bind());
  ZC_EXPECT(metadata.parent(root) == ast::NodeId());
  ZC_EXPECT(metadata.parent(module) == root);
  ZC_EXPECT(metadata.parent(importItem) == root);
  ZC_EXPECT(metadata.parent(declarationItem) == root);
  ZC_EXPECT(metadata.parent(import) == importItem);
  ZC_EXPECT(metadata.parent(declaration) == declarationItem);
}

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
