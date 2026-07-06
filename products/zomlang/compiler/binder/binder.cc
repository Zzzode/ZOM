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

#include "zomlang/compiler/binder/binder.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/decl-collector.h"
#include "zomlang/compiler/binder/import-resolver.h"
#include "zomlang/compiler/binder/name-resolver.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/symbol/symbol-table.h"

namespace zomlang {
namespace compiler {
namespace binder {

struct Binder::Impl {
  Impl(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diagnosticEngine,
       const ast::Tree& tree, ast::BindingMetadata& metadata) noexcept
      : symbolTable(symbolTable),
        diagnosticEngine(diagnosticEngine),
        tree(tree),
        metadata(metadata) {}

  symbol::SymbolTable& symbolTable;
  diagnostics::DiagnosticEngine& diagnosticEngine;
  const ast::Tree& tree;
  ast::BindingMetadata& metadata;
};

Binder::Binder(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diagnosticEngine,
               const ast::Tree& tree, ast::BindingMetadata& metadata) noexcept
    : impl(zc::heap<Impl>(symbolTable, diagnosticEngine, tree, metadata)) {}

Binder::~Binder() noexcept(false) = default;

bool Binder::bind() {
  impl->metadata.resizeFor(impl->tree);

  ZC_IREQUIRE(impl->tree.contains(impl->tree.root()), "cannot bind a tree without a valid root");

  // Write parent metadata for all nodes before running collectors.
  // This enables efficient parent traversal during name resolution and checking.
  ast::visitTreePreOrder(
      impl->tree, impl->tree.root(), [this](ast::NodeId nodeId, const ast::Node&) {
        ast::visitChildNodeIds(
            impl->tree, impl->tree.node(nodeId),
            [this, nodeId](ast::NodeId childId) { impl->metadata.setParent(childId, nodeId); });
      });

  // Phase 1: Collect declarations
  DeclCollector collector(impl->symbolTable, impl->symbolTable.getScopeManager(), impl->tree,
                          impl->metadata, impl->diagnosticEngine);
  if (!collector.collect()) return false;

  // Phase 1.5: Resolve imports
  ImportResolver importResolver(impl->symbolTable, impl->symbolTable.getScopeManager(), impl->tree,
                                impl->metadata, impl->diagnosticEngine);
  if (!importResolver.resolveImports()) return false;

  // Phase 2: Resolve names
  NameResolver resolver(impl->symbolTable, impl->symbolTable.getScopeManager(), impl->tree,
                        impl->metadata, impl->diagnosticEngine);
  if (!resolver.resolve()) return false;

  return !impl->diagnosticEngine.hasErrors();
}

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
