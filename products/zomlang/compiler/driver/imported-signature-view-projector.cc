// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/imported-signature-view-projector.h"

#include "zomlang/compiler/binder/binding-input.h"
#include "zomlang/compiler/binder/binding-metadata.h"

namespace zomlang::compiler::driver {
namespace {

using checker::cross_module::ImportedDefinitionBindingSelection;
using checker::cross_module::ImportedModuleTargetSelection;
using checker::cross_module::SignatureViewOrigin;

bool sameName(const binder::BindingNameKey& left, const binder::BindingNameKey& right) {
  return left.nameSpace() == right.nameSpace() && left.name() == right.name();
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
    const binder::VerifiedBoundModuleInput& requester,
    zc::ArrayPtr<const VerifiedModuleInterface> dependencyInterfaces,
    const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) {
  if (requester.semanticContext() != registries.context() ||
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
      if (import.sourceModule() != source.module() ||
          import.sourceRevision().digest() != source.bindingSurface().revision().digest()) {
        continue;
      }
      const auto& target = import.canonicalTarget().value();
      if (target.is<binder::DefinitionBindingTarget>()) {
        auto exported =
            exactDefinitionExport(source, target.get<binder::DefinitionBindingTarget>().definition,
                                  import.requestedName());
        if (exported == zc::none) { return zc::none; }
        ZC_IF_SOME(binding, exported) {
          auto sourceBinding = signatureRootBinding(binding);
          if (sourceBinding == zc::none) { return zc::none; }
          ZC_IF_SOME(value, sourceBinding) {
            auto requesterBinding = binder::BindingTarget::semanticImport(import.binding().clone());
            if (hasDefinitionSelection(definitions.asPtr(), requesterBinding)) { return zc::none; }
            definitions.add(ImportedDefinitionBindingSelection{
                zc::mv(requesterBinding), value.clone(), SignatureViewOrigin::ExplicitImport});
          }
        }
      } else {
        const auto targetModule = target.get<binder::ModuleBindingTarget>().module;
        if (hasTargetSelection(moduleTargets.asPtr(), import.localName())) { return zc::none; }
        if (targetModule == source.module()) {
          moduleTargets.add(ImportedModuleTargetSelection{import.localName().clone(),
                                                          import.localName().clone(),
                                                          SignatureViewOrigin::ExplicitImport});
        } else {
          auto exported = exactModuleExport(source, targetModule, import.requestedName());
          if (exported == zc::none) { return zc::none; }
          ZC_IF_SOME(binding, exported) {
            moduleTargets.add(ImportedModuleTargetSelection{import.localName().clone(),
                                                            binding.name.clone(),
                                                            SignatureViewOrigin::ExplicitImport});
          }
        }
      }
      retainStrongest(SignatureViewOrigin::ExplicitImport, origin, hasOrigin);
    }

    for (const auto& alias : requester.resolvedModuleAliases()) {
      if (alias.targetModule() != source.module() ||
          alias.targetRevision().digest() != source.bindingSurface().revision().digest()) {
        continue;
      }
      if (hasTargetSelection(moduleTargets.asPtr(), alias.localName())) { return zc::none; }
      moduleTargets.add(ImportedModuleTargetSelection{alias.localName().clone(),
                                                      alias.localName().clone(),
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
      isPrelude = prelude.sourceModule() == source.module() &&
                  prelude.sourceRevision().digest() == source.bindingSurface().revision().digest();
    }
    if (isPrelude) {
      for (const auto& entry : requester.bindingSurface().visibleEntries()) {
        bool exactPreludeBinding = false;
        for (const auto& scope : requester.bindings().scopes()) {
          if (scope.kind != binder::ScopeKind::Module) continue;
          for (const auto& binding : scope.bindings) {
            if (sameName(binding.name, entry.name) &&
                binding.binding.origin == binder::BindingOrigin::Prelude) {
              exactPreludeBinding = true;
              break;
            }
          }
          break;
        }
        if (!exactPreludeBinding) continue;
        const auto& target = entry.canonicalTarget.value();
        if (target.is<binder::DefinitionBindingTarget>()) {
          auto sourceExport = exactDefinitionExport(
              source, target.get<binder::DefinitionBindingTarget>().definition, entry.name);
          if (sourceExport == zc::none) { return zc::none; }
          ZC_IF_SOME(exported, sourceExport) {
            auto sourceBinding = signatureRootBinding(exported);
            if (sourceBinding == zc::none ||
                !module_interface::isSignatureRootBinding(entry.bindingIdentity)) {
              return zc::none;
            }
            ZC_IF_SOME(value, sourceBinding) {
              auto requesterBinding = entry.bindingIdentity.clone();
              if (!hasDefinitionSelection(definitions.asPtr(), requesterBinding)) {
                definitions.add(ImportedDefinitionBindingSelection{
                    zc::mv(requesterBinding), value.clone(), SignatureViewOrigin::Prelude});
              }
            }
          }
        } else {
          auto sourceExport = exactModuleExport(
              source, target.get<binder::ModuleBindingTarget>().module, entry.name);
          if (sourceExport == zc::none || hasTargetSelection(moduleTargets.asPtr(), entry.name)) {
            return zc::none;
          }
          ZC_IF_SOME(exported, sourceExport) {
            moduleTargets.add(ImportedModuleTargetSelection{
                entry.name.clone(), exported.name.clone(), SignatureViewOrigin::Prelude});
          }
        }
      }
      retainStrongest(SignatureViewOrigin::Prelude, origin, hasOrigin);
    }

    if (!hasOrigin) continue;
    auto projected = source.projectImportedSignatures(
        requester, origin, definitions.asPtr(), moduleTargets.asPtr(), registries, semanticTypes);
    if (projected == zc::none) { return zc::none; }
    ZC_IF_SOME(module, projected) { modules.add(zc::mv(module)); }
  }

  return checker::cross_module::ImportedSignatureViewBuilder::build(
      requester.semanticContext(), requester.semanticFingerprint(), requester.module(),
      zc::mv(modules), registries);
}

}  // namespace zomlang::compiler::driver
