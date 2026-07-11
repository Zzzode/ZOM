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

#include "zomlang/compiler/checker/checker.h"

#include "zc/core/common.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/checker/body-checker.h"
#include "zomlang/compiler/checker/borrow-model.h"
#include "zomlang/compiler/checker/decl-signature.h"
#include "zomlang/compiler/checker/trait-resolver.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/type/constraint-set.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/compiler/type/unification.h"

namespace zomlang {
namespace compiler {

namespace symbol {
class SymbolTable;
}  // namespace symbol

namespace checker {

struct Checker::Impl {
  Impl(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diags,
       const ast::Tree& tree, const ast::BindingMetadata& metadata, type::TypeEnv& typeEnv) noexcept
      : symbolTable(symbolTable),
        diags(diags),
        tree(tree),
        metadata(metadata),
        typeEnv(typeEnv),
        constraints(zc::heap<type::ConstraintSet>()),
        unification(zc::heap<type::UnificationEngine>(typeEnv)) {}

  symbol::SymbolTable& symbolTable;
  diagnostics::DiagnosticEngine& diags;
  const ast::Tree& tree;
  const ast::BindingMetadata& metadata;
  type::TypeEnv& typeEnv;
  zc::Own<type::ConstraintSet> constraints;
  zc::Own<type::UnificationEngine> unification;
};

Checker::Checker(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diagnosticEngine,
                 const ast::Tree& tree, const ast::BindingMetadata& metadata,
                 type::TypeEnv& typeEnv) noexcept
    : impl(zc::heap<Impl>(symbolTable, diagnosticEngine, tree, metadata, typeEnv)) {}

Checker::~Checker() noexcept(false) = default;

bool Checker::check() {
  ZC_IREQUIRE(impl->tree.contains(impl->tree.root()), "cannot check a tree without a valid root");

  // Phase A: Compute declaration signatures
  DeclSignatureComputer sigComputer(impl->typeEnv, impl->symbolTable, impl->tree, impl->metadata,
                                    impl->diags);
  if (!sigComputer.computeSignatures()) return false;

  // Phase B: Check bodies
  BodyChecker bodyChecker(impl->typeEnv, *impl->unification, *impl->constraints, impl->symbolTable,
                          impl->tree, impl->metadata, impl->diags);
  if (!bodyChecker.checkBodies()) return false;

  // Trait resolution
  TraitResolver traitResolver(impl->typeEnv, impl->symbolTable, impl->tree, impl->metadata,
                              impl->diags);
  traitResolver.checkCoherence();
  if (impl->diags.hasErrors()) return false;

  BorrowCheckerPhase borrowPhase(impl->tree, impl->typeEnv, impl->metadata);
  auto borrowResult = borrowPhase.run();
  emitBorrowDiagnostics(impl->tree, borrowResult, impl->diags);

  return !impl->diags.hasErrors();
}

type::TypeEnv& Checker::getTypeEnv() { return impl->typeEnv; }

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
