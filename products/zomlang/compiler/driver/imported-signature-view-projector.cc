// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/imported-signature-view-projector.h"

#include "zomlang/compiler/binder/binding-metadata.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"

namespace zomlang::compiler::driver {
namespace {

using checker::cross_module::ImportedDefinitionBindingSelection;
using checker::cross_module::ImportedModuleTargetSelection;
using checker::cross_module::SignatureViewOrigin;

bool sameName(const binder::BindingNameKey& left, const binder::BindingNameKey& right) {
  return left.nameSpace() == right.nameSpace() && left.name() == right.name();
}

zc::Maybe<binder::Namespace> bindingNamespace(identity::DefinitionNamespace value) {
  switch (value) {
    case identity::DefinitionNamespace::Value:
      return binder::Namespace::Value;
    case identity::DefinitionNamespace::Type:
      return binder::Namespace::Type;
    case identity::DefinitionNamespace::Module:
      return binder::Namespace::Module;
  }
  ZC_UNREACHABLE
}

zc::Maybe<binder::BindingNameKey> importName(const identity::SemanticImportBindingKey& binding,
                                             bool local) {
  auto nameSpace = bindingNamespace(local ? binding.localNamespace() : binding.sourceNamespace());
  if (nameSpace == zc::none) { return zc::none; }
  return binder::BindingNameKey::from(
      ZC_ASSERT_NONNULL(nameSpace),
      local ? binding.localName().clone() : binding.sourceName().clone());
}

zc::Maybe<binder::BindingNameKey> moduleAliasName(
    const module_graph_query::CheckerBoundModuleView& requester,
    const binder::ModuleAliasBindingFact& alias) {
  const auto& tree = requester.tree();
  if (!tree.contains(alias.node)) { return zc::none; }
  const auto& syntax = tree.node(alias.node);
  if (syntax.kind != ast::SyntaxKind::ModuleDeclaration ||
      static_cast<ast::ModuleDeclarationForm>(
          syntax.payload.words[ast::kModuleDeclarationFormWord]) !=
          ast::ModuleDeclarationForm::Alias) {
    return zc::none;
  }
  const ast::IdentId identifier(syntax.payload.words[ast::kModuleDeclarationDeclaredNameWord]);
  if (!identifier) { return zc::none; }
  auto name = identity::DeclaredDefinitionName::fromCanonical(tree.ident(identifier));
  if (name == zc::none) { return zc::none; }
  return binder::BindingNameKey::from(binder::Namespace::Module, zc::mv(ZC_ASSERT_NONNULL(name)));
}

zc::Maybe<binder::BindingTarget> signatureRootBinding(const ExportedBinding& binding) {
  if (!module_interface::isSignatureRootBinding(binding.bindingIdentity) ||
      !binding.target.variant().is<DefinitionTypeEnrichedTarget>()) {
    return zc::none;
  }
  return binding.bindingIdentity.clone();
}

zc::Maybe<identity::DefId> canonicalDefinition(const ExportedBinding& binding) {
  if (!binding.target.variant().is<DefinitionTypeEnrichedTarget>()) { return zc::none; }
  return binding.target.variant().get<DefinitionTypeEnrichedTarget>().definition;
}

bool hasDefinitionSelection(zc::ArrayPtr<const ImportedDefinitionBindingSelection> selections,
                            const binder::BindingTarget& requesterBinding) {
  for (const auto& selection : selections) {
    if (module_interface::sameSignatureRootBinding(selection.requesterBinding, requesterBinding)) {
      return true;
    }
  }
  return false;
}

bool hasTargetSelection(zc::ArrayPtr<const ImportedModuleTargetSelection> selections,
                        const binder::BindingNameKey& requesterName) {
  for (const auto& selection : selections) {
    if (sameName(selection.requesterName, requesterName)) return true;
  }
  return false;
}

zc::Maybe<const ExportedBinding&> exactDefinitionExport(
    const VerifiedModuleInterface& source, identity::DefId canonical,
    zc::Maybe<const binder::BindingNameKey&> requestedName) {
  zc::Maybe<const ExportedBinding&> selected;
  for (const auto& binding : source.exportedBindings()) {
    auto definition = canonicalDefinition(binding);
    if (definition == zc::none) continue;
    ZC_IF_SOME(value, definition) {
      if (value != canonical) continue;
    }
    ZC_IF_SOME(name, requestedName) {
      if (!sameName(binding.name, name)) continue;
    }
    if (selected != zc::none) { return zc::none; }
    selected = binding;
  }
  return selected;
}

zc::Maybe<const ExportedBinding&> exactModuleExport(
    const VerifiedModuleInterface& source, identity::ModuleId canonical,
    zc::Maybe<const binder::BindingNameKey&> requestedName) {
  zc::Maybe<const ExportedBinding&> selected;
  for (const auto& binding : source.exportedBindings()) {
    const auto& target = binding.target.variant();
    if (!target.is<ModuleTypeEnrichedTarget>() ||
        target.get<ModuleTypeEnrichedTarget>().module != canonical) {
      continue;
    }
    ZC_IF_SOME(name, requestedName) {
      if (!sameName(binding.name, name)) continue;
    }
    if (selected != zc::none) { return zc::none; }
    selected = binding;
  }
  return selected;
}

void retainStrongest(SignatureViewOrigin candidate, SignatureViewOrigin& current, bool& hasOrigin) {
  if (!hasOrigin || static_cast<uint8_t>(candidate) < static_cast<uint8_t>(current)) {
    current = candidate;
    hasOrigin = true;
  }
}

}  // namespace

zc::Maybe<checker::cross_module::ImportedSignatureView> ImportedSignatureViewProjector::build(
    const module_graph_query::CheckerBoundModuleView& requester,
    zc::ArrayPtr<const VerifiedModuleInterface> dependencyInterfaces,
    const type::SemanticTypeStore& semanticTypes,
    const checker::CheckerIdentityAuthority& identities) {
  if (requester.semanticContext() != identities.semanticContext() ||
      requester.semanticContext() != semanticTypes.context()) {
    return zc::none;
  }

  zc::Vector<checker::cross_module::ImportedSignatureModule> modules;
  for (const auto& source : dependencyInterfaces) {
    if (source.module() == requester.module() ||
        source.semanticContext() != requester.semanticContext()) {
      return zc::none;
    }

    zc::Vector<ImportedDefinitionBindingSelection> definitions;
    zc::Vector<ImportedModuleTargetSelection> moduleTargets;
    SignatureViewOrigin origin = SignatureViewOrigin::Prelude;
    bool hasOrigin = false;

    for (const auto& import : requester.resolvedImports()) {
      if (import.sourceModule != source.module() ||
          import.sourceRevision.digest() != source.bindingSurface().revision().digest()) {
        continue;
      }
      const auto& target = import.canonicalTarget.value();
      auto requestedName = importName(import.binding, false);
      auto localName = importName(import.binding, true);
      if (requestedName == zc::none || localName == zc::none) { return zc::none; }
      if (target.is<binder::DefinitionBindingTarget>()) {
        auto exported =
            exactDefinitionExport(source, target.get<binder::DefinitionBindingTarget>().definition,
                                  ZC_ASSERT_NONNULL(requestedName));
        if (exported == zc::none) { return zc::none; }
        ZC_IF_SOME(binding, exported) {
          auto sourceBinding = signatureRootBinding(binding);
          if (sourceBinding == zc::none) { return zc::none; }
          ZC_IF_SOME(value, sourceBinding) {
            auto requesterBinding = binder::BindingTarget::semanticImport(import.binding.clone());
            if (hasDefinitionSelection(definitions.asPtr(), requesterBinding)) { return zc::none; }
            definitions.add(ImportedDefinitionBindingSelection{
                zc::mv(requesterBinding), value.clone(), SignatureViewOrigin::ExplicitImport});
          }
        }
      } else {
        const auto targetModule = target.get<binder::ModuleBindingTarget>().module;
        if (hasTargetSelection(moduleTargets.asPtr(), ZC_ASSERT_NONNULL(localName))) {
          return zc::none;
        }
        if (targetModule == source.module()) {
          moduleTargets.add(ImportedModuleTargetSelection{ZC_ASSERT_NONNULL(localName).clone(),
                                                          ZC_ASSERT_NONNULL(localName).clone(),
                                                          SignatureViewOrigin::ExplicitImport});
        } else {
          auto exported = exactModuleExport(source, targetModule, ZC_ASSERT_NONNULL(requestedName));
          if (exported == zc::none) { return zc::none; }
          ZC_IF_SOME(binding, exported) {
            moduleTargets.add(ImportedModuleTargetSelection{ZC_ASSERT_NONNULL(localName).clone(),
                                                            binding.name.clone(),
                                                            SignatureViewOrigin::ExplicitImport});
          }
        }
      }
      retainStrongest(SignatureViewOrigin::ExplicitImport, origin, hasOrigin);
    }

    for (const auto& alias : requester.resolvedModuleAliases()) {
      if (alias.canonicalTarget != source.module()) { continue; }
      auto localName = moduleAliasName(requester, alias);
      if (localName == zc::none ||
          hasTargetSelection(moduleTargets.asPtr(), ZC_ASSERT_NONNULL(localName))) {
        return zc::none;
      }
      moduleTargets.add(ImportedModuleTargetSelection{ZC_ASSERT_NONNULL(localName).clone(),
                                                      ZC_ASSERT_NONNULL(localName).clone(),
                                                      SignatureViewOrigin::NamespaceImport});
      for (const auto& binding : source.exportedBindings()) {
        auto sourceBinding = signatureRootBinding(binding);
        if (sourceBinding == zc::none) continue;
        ZC_IF_SOME(value, sourceBinding) {
          auto requesterBinding = value.clone();
          if (!hasDefinitionSelection(definitions.asPtr(), requesterBinding)) {
            definitions.add(ImportedDefinitionBindingSelection{
                zc::mv(requesterBinding), value.clone(), SignatureViewOrigin::NamespaceImport});
          }
        }
      }
      retainStrongest(SignatureViewOrigin::NamespaceImport, origin, hasOrigin);
    }

    bool isPrelude = false;
    ZC_IF_SOME(prelude, requester.preludeSurface()) {
      isPrelude =
          prelude.module == source.module() &&
          prelude.surface.revision().digest() == source.bindingSurface().revision().digest();
    }
    if (isPrelude) { retainStrongest(SignatureViewOrigin::Prelude, origin, hasOrigin); }

    if (!hasOrigin) continue;
    auto projected = source.projectImportedSignatures(
        requester, origin, definitions.asPtr(), moduleTargets.asPtr(), semanticTypes, identities);
    if (projected == zc::none) { return zc::none; }
    ZC_IF_SOME(module, projected) { modules.add(zc::mv(module)); }
  }

  return checker::cross_module::ImportedSignatureViewBuilder::build(
      requester.semanticContext(), requester.semanticFingerprint(), requester.module(),
      zc::mv(modules), identities);
}

}  // namespace zomlang::compiler::driver
