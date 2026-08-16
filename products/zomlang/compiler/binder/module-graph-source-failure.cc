// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/module-graph-source-failure.h"

#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

bool findToolchainRootPath(const ast::Tree& tree, ast::NodeId current, ast::NodeId target,
                           zc::Vector<uint32_t>& path) {
  if (current == target) { return true; }
  uint32_t childIndex = 0;
  bool found = false;
  ast::visitChildNodeIds(tree, tree.node(current), [&](ast::NodeId child) {
    const uint32_t currentIndex = childIndex++;
    if (found || !tree.contains(child)) { return; }
    path.add(currentIndex);
    if (findToolchainRootPath(tree, child, target, path)) {
      found = true;
      return;
    }
    path.removeLast();
  });
  return found;
}

}  // namespace

ModuleGraphModule::ModuleGraphModule(identity::ModuleKey&& key, identity::ModuleId module) noexcept
    : keyValue(zc::mv(key)), moduleValue(module) {}

const identity::ModuleKey& ModuleGraphModule::key() const noexcept { return keyValue; }

identity::ModuleId ModuleGraphModule::module() const noexcept { return moduleValue; }

struct ModuleGraphSourceFailure::Impl final {
  Impl(identity::ModuleKey&& module, identity::SourceFileKey&& source,
       LocalSyntaxPath&& declaredNamePath, uint32_t schemaPreorderOrdinal,
       diagnostics::ModuleRootArgument&& argument) noexcept
      : module(zc::mv(module)),
        source(zc::mv(source)),
        declaredNamePath(zc::mv(declaredNamePath)),
        schemaPreorderOrdinal(schemaPreorderOrdinal),
        argument(zc::mv(argument)) {}

  identity::ModuleKey module;
  identity::SourceFileKey source;
  LocalSyntaxPath declaredNamePath;
  uint32_t schemaPreorderOrdinal;
  diagnostics::ModuleRootArgument argument;
};

ModuleGraphSourceFailure::~ModuleGraphSourceFailure() noexcept(false) = default;
ModuleGraphSourceFailure::ModuleGraphSourceFailure(ModuleGraphSourceFailure&&) noexcept = default;
ModuleGraphSourceFailure& ModuleGraphSourceFailure::operator=(ModuleGraphSourceFailure&&) noexcept =
    default;

ModuleGraphSourceFailure::ModuleGraphSourceFailure(
    identity::ModuleKey&& module, identity::SourceFileKey&& source,
    LocalSyntaxPath&& declaredNamePath, uint32_t schemaPreorderOrdinal,
    diagnostics::ModuleRootArgument&& argument) noexcept
    : impl(zc::heap<Impl>(zc::mv(module), zc::mv(source), zc::mv(declaredNamePath),
                          schemaPreorderOrdinal, zc::mv(argument))) {}

const identity::ModuleKey& ModuleGraphSourceFailure::module() const noexcept {
  return impl->module;
}

const identity::SourceFileKey& ModuleGraphSourceFailure::source() const noexcept {
  return impl->source;
}

const LocalSyntaxPath& ModuleGraphSourceFailure::declaredNamePath() const noexcept {
  return impl->declaredNamePath;
}

uint32_t ModuleGraphSourceFailure::schemaPreorderOrdinal() const noexcept {
  return impl->schemaPreorderOrdinal;
}

const diagnostics::ModuleRootArgument& ModuleGraphSourceFailure::argument()
    const noexcept {
  return impl->argument;
}

zc::Maybe<ModuleGraphSourceFailure>
ModuleGraphSourceFailureBuilder::buildToolchainModuleRootReserved(
    const ModuleGraphModule& module, const ParsedModuleGraphInput& parsed) {
  if (module.module() != parsed.module || module.key().path().size() != 1 ||
      module.key().crate().unit().kind() != identity::CompilationUnitKind::UserPackage ||
      !parsed.parsedModule.source().belongsTo(module.key().crate())) {
    return zc::none;
  }
  const auto& tree = parsed.parsedModule.tree();
  if (!tree.contains(tree.root()) || tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
    return zc::none;
  }
  const auto& source = tree.node(tree.root());
  const ast::NodeId declaration(source.payload.words[ast::kSourceFileModuleWord]);
  if (!tree.contains(declaration) ||
      tree.node(declaration).kind != ast::SyntaxKind::ModuleDeclaration) {
    return zc::none;
  }
  const auto& declarationNode = tree.node(declaration);
  const auto form = static_cast<ast::ModuleDeclarationForm>(
      declarationNode.payload.words[ast::kModuleDeclarationFormWord]);
  if (form == ast::ModuleDeclarationForm::Alias) { return zc::none; }

  auto segment = identity::ModulePathSegment::fromSource(tree.ident(
      ast::IdentId(declarationNode.payload.words[ast::kModuleDeclarationDeclaredNameWord])));
  if (segment == zc::none) { return zc::none; }
  zc::Vector<identity::ModulePathSegment> argumentPath;
  ZC_IF_SOME(value, segment) { argumentPath.add(zc::mv(value)); }
  auto argument = diagnostics::ModuleRootArgument::fromCanonicalPath(zc::mv(argumentPath));
  if (argument == zc::none) { return zc::none; }

  zc::Vector<uint32_t> pathComponents;
  if (!findToolchainRootPath(tree, tree.root(), declaration, pathComponents)) { return zc::none; }
  auto declaredNamePath = LocalSyntaxPath::from(zc::mv(pathComponents));
  if (declaredNamePath == zc::none) { return zc::none; }

  uint32_t schemaPreorderOrdinal = 0;
  bool foundOrdinal = false;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (foundOrdinal) { return; }
    if (node == declaration) {
      foundOrdinal = true;
      return;
    }
    ++schemaPreorderOrdinal;
  });
  if (!foundOrdinal) { return zc::none; }
  ZC_IF_SOME(path, declaredNamePath) {
    ZC_IF_SOME(argumentValue, argument) {
      return ModuleGraphSourceFailure(module.key().clone(), parsed.parsedModule.source().clone(),
                                      zc::mv(path), schemaPreorderOrdinal, zc::mv(argumentValue));
    }
  }
  return zc::none;
}

}  // namespace zomlang::compiler::binder
