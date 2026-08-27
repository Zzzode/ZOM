// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/interface/imported-signature-view-projector.h"

#include "zomlang/compiler/binder/metadata/binding-metadata.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/driver/core/query.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/ownership/surface-admission.h"

namespace zomlang::compiler::driver {
namespace {

using checker::cross_module::ImportedDefinitionBindingSelection;
using checker::cross_module::ImportedModuleTargetSelection;
using checker::cross_module::SignatureViewOrigin;
using core_library_query::CoreFinalSignatureRoot;
using core_library_query::CoreModuleInterfaceRecord;
using core_library_query::VerifiedCoreModuleInterface;

bool sameName(const binder::BindingNameKey& left, const binder::BindingNameKey& right) {
  return left.nameSpace() == right.nameSpace() && left.name() == right.name();
}

bool less(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

template <typename Value>
struct EncodedProjection final {
  Value value;
  zc::Array<uint8_t> encoded;
};

template <typename Value>
void sortEncoded(zc::Vector<EncodedProjection<Value>>& values) {
  for (size_t outer = 1; outer < values.size(); ++outer) {
    size_t index = outer;
    while (index != 0 && less(values[index].encoded.asPtr(), values[index - 1].encoded.asPtr())) {
      auto temporary = zc::mv(values[index]);
      values[index] = zc::mv(values[index - 1]);
      values[index - 1] = zc::mv(temporary);
      --index;
    }
  }
}

void append(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> bytes) {
  output.addAll(bytes);
}

bool encodeModule(zc::Vector<uint8_t>& output, identity::ModuleId module,
                  const checker::CheckerIdentityAuthority& identities) {
  ZC_IF_SOME(entry, identities.module(module)) {
    identity::CanonicalEncoder encoder;
    entry.key().encode(encoder);
    append(output, encoder.finish().asPtr());
    return true;
  }
  return false;
}

void encodeName(zc::Vector<uint8_t>& output, const binder::BindingNameKey& name) {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(name.nameSpace()));
  name.name().encode(encoder);
  append(output, encoder.finish().asPtr());
}

zc::Maybe<zc::Array<uint8_t>> encodeImportedModuleTarget(
    const checker::cross_module::ImportedModuleTarget& target,
    const checker::CheckerIdentityAuthority& identities) {
  zc::Vector<uint8_t> output;
  encodeName(output, target.name);
  if (!encodeModule(output, target.module, identities)) { return zc::none; }
  const auto& revision = target.surfaceRevision.variant();
  if (revision.is<module_interface::UserImportedBindingSurfaceRevision>()) {
    output.add(0x01);
    append(output, revision.get<module_interface::UserImportedBindingSurfaceRevision>()
                       .value.digest()
                       .bytes());
  } else {
    output.add(0x02);
    append(output, revision.get<module_interface::ToolchainCoreImportedBindingSurfaceRevision>()
                       .value.digest()
                       .bytes());
  }
  return output.releaseAsArray();
}

module_interface::ImportedInterfaceRevision coreInterfaceRevision(
    const core_library_query::CoreModuleInterfaceRevision& value) {
  return module_interface::ImportedInterfaceRevision(
      module_interface::ToolchainCoreImportedInterfaceRevision{value.clone()});
}

module_interface::ImportedBindingSurfaceRevision coreBindingSurfaceRevision(
    const core_library_query::CoreBindingSurfaceRevision& value) {
  return module_interface::ImportedBindingSurfaceRevision(
      module_interface::ToolchainCoreImportedBindingSurfaceRevision{value.clone()});
}

zc::Maybe<identity::DefId> definitionHandle(const identity::DefinitionKey& key,
                                            const checker::CheckerIdentityAuthority& identities) {
  ZC_IF_SOME(entry, identities.definition(key)) { return entry.handle(); }
  return zc::none;
}

zc::Maybe<const CoreFinalSignatureRoot&> coreRoot(
    const CoreModuleInterfaceRecord& record, identity::DefId definition,
    const checker::CheckerIdentityAuthority& identities) {
  zc::Maybe<const CoreFinalSignatureRoot&> result;
  for (const auto& candidate : record.signatureRoots()) {
    auto canonical = definitionHandle(candidate.canonicalDefinition, identities);
    if (canonical == zc::none || ZC_ASSERT_NONNULL(canonical) != definition) { continue; }
    if (result != zc::none) { return zc::none; }
    result = candidate;
  }
  return result;
}

zc::Maybe<checker::signature::SemanticSignature> coreSignature(
    const core::TypeFreeInterfaceSignatureRecord& record,
    const checker::CheckerIdentityAuthority& identities) {
  auto identity = identities.definition(record.definition());
  if (identity == zc::none) { return zc::none; }
  ZC_IF_SOME(definition, identity) {
    if (definition.record().kind() != identity::DefinitionKind::Interface) { return zc::none; }
    auto module = identities.module(definition.record().module());
    if (module == zc::none) { return zc::none; }
    ZC_IF_SOME(moduleEntry, module) {
      auto bound = identities.boundModule(moduleEntry.handle());
      if (bound == zc::none) { return zc::none; }
      ZC_IF_SOME(boundModule, bound) {
        zc::Maybe<const binder::MaterializedDefinitionInventoryEntry&> source;
        for (const auto& candidate : boundModule.definitions().definitions()) {
          if (candidate.definition != definition.handle() || candidate.key != record.definition()) {
            continue;
          }
          if (!candidate.source.belongsTo(record.source()) ||
              candidate.source.byteStart() != record.byteStart() ||
              candidate.source.byteEnd() != record.byteEnd() || source != zc::none) {
            return zc::none;
          }
          source = candidate;
        }
        ZC_IF_SOME(value, source) {
          return checker::signature::SemanticSignature{
              definition.handle(),
              identity::DefinitionKind::Interface,
              checker::signature::SignatureScope(
                  checker::signature::ModuleDefinitionSignatureScope{}),
              zc::Vector<checker::signature::SignatureModifier>(),
              zc::Vector<checker::signature::NormalizedAttributeFact>(),
              checker::signature::SemanticSignaturePayload(checker::signature::InterfaceSignature{
                  zc::Vector<checker::signature::GenericParameterSignature>(),
                  zc::Vector<checker::signature::InterfaceInstantiation>(),
                  zc::Vector<identity::DefId>(), zc::Vector<identity::DefId>(), true,
                  zc::Vector<checker::signature::ObjectSafetyCause>()}),
              value.source.clone()};
        }
      }
    }
  }
  return zc::none;
}

bool hasCoreExport(const CoreModuleInterfaceRecord& record, const binder::BindingNameKey& name,
                   const identity::DefinitionKey& definition) {
  size_t matches = 0;
  for (const auto& binding : record.exportedBindings()) {
    const auto& target = binding.canonicalTarget().value();
    if (!sameName(binding.name(), name) || !target.is<binder::StableDefinitionBindingTarget>()) {
      continue;
    }
    if (target.get<binder::StableDefinitionBindingTarget>().definition.definition() != definition) {
      continue;
    }
    ++matches;
  }
  return matches == 1;
}

bool hasDependencyEvidence(const module_graph_query::CheckerBoundModuleView& requester,
                           const binder::ImportBindingFact& import) {
  size_t matches = 0;
  for (const auto& surface : requester.dependencySurfaces()) {
    if (surface.module == import.sourceModule &&
        surface.surface.revision().digest() == import.sourceRevision.digest()) {
      ++matches;
    }
  }
  return matches == 1;
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

zc::Maybe<binder::BindingNameKey> importName(const identity::ImportBindingKey& binding,
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

zc::Maybe<checker::cross_module::ImportedSignatureModule>
ImportedSignatureViewProjector::projectCore(
    const ownership::OwnershipAdmittedBoundModule& requester,
    const VerifiedCoreModuleInterface& source, SignatureViewOrigin origin,
    zc::ArrayPtr<const ImportedDefinitionBindingSelection> definitionBindings,
    zc::ArrayPtr<const ImportedModuleTargetSelection> moduleTargetNames,
    const checker::CheckerIdentityAuthority& identities) {
  const auto& record = source.record();
  auto sourceModule = identities.module(record.module());
  if (source.context() != requester.semanticContext() ||
      source.fingerprint().digest() != requester.semanticFingerprint().digest() ||
      source.module() == requester.module() || sourceModule == zc::none) {
    return zc::none;
  }
  ZC_IF_SOME(module, sourceModule) {
    if (module.handle() != source.module()) { return zc::none; }
  }

  zc::Vector<EncodedProjection<module_interface::SignatureRootAuthorization>> rootRecords;
  zc::Vector<identity::DefId> rootDefinitions;
  for (const auto& selection : definitionBindings) {
    if (!module_interface::isSignatureRootBinding(selection.requesterBinding) ||
        !selection.sourceBinding.value().is<binder::DefinitionBindingTarget>()) {
      return zc::none;
    }
    const auto definition =
        selection.sourceBinding.value().get<binder::DefinitionBindingTarget>().definition;
    auto root = coreRoot(record, definition, identities);
    if (root == zc::none) { return zc::none; }
    ZC_IF_SOME(value, root) {
      auto canonical = definitionHandle(value.canonicalDefinition, identities);
      auto rootModule = identities.module(value.sourceModule);
      if (canonical == zc::none || rootModule == zc::none ||
          ZC_ASSERT_NONNULL(canonical) != definition) {
        return zc::none;
      }
      for (const auto& prior : rootDefinitions) {
        if (prior == definition) { return zc::none; }
      }
      binder::VisibilityEnvelope visibility =
          selection.authorizationOrigin == SignatureViewOrigin::ExplicitImport
              ? binder::VisibilityEnvelope::module(requester.module())
              : binder::VisibilityEnvelope::external();
      module_interface::SignatureRootAuthorization importedRoot{
          selection.requesterBinding.clone(),
          definition,
          zc::mv(visibility),
          ZC_ASSERT_NONNULL(rootModule).handle(),
          coreBindingSurfaceRevision(record.bindingSurfaceRevision()),
          module_interface::SignatureAuthorizationOrigin(
              module_interface::ImportedSignatureAuthorization{
                  coreInterfaceRevision(record.revision())})};
      auto encoded = ModuleInterfaceCanonicalCodec::encodeSignatureRoot(importedRoot, identities);
      if (encoded == zc::none) { return zc::none; }
      rootDefinitions.add(definition);
      rootRecords.add(EncodedProjection<module_interface::SignatureRootAuthorization>{
          zc::mv(importedRoot), zc::mv(ZC_ASSERT_NONNULL(encoded))});
    }
  }
  sortEncoded(rootRecords);

  zc::Vector<EncodedProjection<checker::cross_module::ImportedModuleTarget>> targetRecords;
  for (const auto& selection : moduleTargetNames) {
    zc::Maybe<checker::cross_module::ImportedModuleTarget> target;
    if (sameName(selection.requesterName, selection.sourceName)) {
      target = checker::cross_module::ImportedModuleTarget{
          selection.requesterName.clone(), source.module(),
          coreBindingSurfaceRevision(record.bindingSurfaceRevision())};
    } else {
      for (const auto& candidate : record.moduleTargets()) {
        if (!sameName(candidate.name, selection.sourceName)) { continue; }
        auto module = identities.module(candidate.module);
        if (module == zc::none || target != zc::none) { return zc::none; }
        ZC_IF_SOME(value, module) {
          target = checker::cross_module::ImportedModuleTarget{
              selection.requesterName.clone(), value.handle(),
              coreBindingSurfaceRevision(candidate.surfaceRevision)};
        }
      }
    }
    if (target == zc::none) { return zc::none; }
    ZC_IF_SOME(value, target) {
      auto encoded = encodeImportedModuleTarget(value, identities);
      if (encoded == zc::none) { return zc::none; }
      targetRecords.add(EncodedProjection<checker::cross_module::ImportedModuleTarget>{
          zc::mv(value), zc::mv(ZC_ASSERT_NONNULL(encoded))});
    }
  }
  sortEncoded(targetRecords);

  zc::Vector<EncodedProjection<checker::signature::SemanticSignature>> lookupRecords;
  for (const auto& recordValue : record.lookupDefinitions()) {
    auto signature = coreSignature(recordValue, identities);
    if (signature == zc::none) { return zc::none; }
    ZC_IF_SOME(value, signature) {
      bool selected = false;
      for (const auto root : rootDefinitions) {
        if (root == value.definition) {
          selected = true;
          break;
        }
      }
      if (!selected) { continue; }
      auto encoded =
          checker::signature::SignatureFactsCanonicalCodec::encodeTypeFreeInterfaceSignature(
              value,
              ZC_ASSERT_NONNULL(
                  identities.module(
                      ZC_ASSERT_NONNULL(identities.definition(value.definition)).record().module()))
                  .handle(),
              identities);
      if (encoded == zc::none) { return zc::none; }
      lookupRecords.add(EncodedProjection<checker::signature::SemanticSignature>{
          zc::mv(value), zc::mv(ZC_ASSERT_NONNULL(encoded))});
    }
  }
  if (lookupRecords.size() != rootDefinitions.size()) { return zc::none; }
  sortEncoded(lookupRecords);

  zc::Vector<zc::ArrayPtr<const uint8_t>> rootBytes(rootRecords.size());
  for (const auto& value : rootRecords) { rootBytes.add(value.encoded.asPtr()); }
  zc::Vector<zc::ArrayPtr<const uint8_t>> lookupBytes(lookupRecords.size());
  for (const auto& value : lookupRecords) { lookupBytes.add(value.encoded.asPtr()); }
  zc::Vector<zc::ArrayPtr<const uint8_t>> supportBytes;
  zc::Vector<zc::ArrayPtr<const uint8_t>> targetBytes(targetRecords.size());
  for (const auto& value : targetRecords) { targetBytes.add(value.encoded.asPtr()); }
  identity::CanonicalEncoder sourceEncoder;
  ZC_IF_SOME(value, sourceModule) { value.key().encode(sourceEncoder); }
  auto sourceBytes = sourceEncoder.finish();
  auto canonical = checker::cross_module::ImportedSignatureModuleCanonicalCodec::encodeFramed(
      origin, sourceBytes.asPtr(), coreInterfaceRevision(record.revision()),
      coreBindingSurfaceRevision(record.bindingSurfaceRevision()), rootBytes.asPtr(),
      lookupBytes.asPtr(), supportBytes.asPtr(), targetBytes.asPtr());
  if (canonical == zc::none) { return zc::none; }

  zc::Vector<module_interface::SignatureRootAuthorization> roots(rootRecords.size());
  for (auto& value : rootRecords) { roots.add(zc::mv(value.value)); }
  zc::Vector<checker::signature::SemanticSignature> definitions(lookupRecords.size());
  for (auto& value : lookupRecords) { definitions.add(zc::mv(value.value)); }
  zc::Vector<checker::signature::SemanticSignature> supportDefinitions;
  zc::Vector<checker::cross_module::ImportedModuleTarget> targets(targetRecords.size());
  for (auto& value : targetRecords) { targets.add(zc::mv(value.value)); }
  return checker::cross_module::ImportedSignatureModule::publish(
      requester.semanticContext(), requester.module(), origin, source.module(),
      coreInterfaceRevision(record.revision()),
      coreBindingSurfaceRevision(record.bindingSurfaceRevision()), zc::mv(roots),
      zc::mv(definitions), zc::mv(supportDefinitions), zc::mv(targets),
      zc::mv(ZC_ASSERT_NONNULL(canonical)));
}

zc::Maybe<checker::cross_module::ImportedSignatureView> ImportedSignatureViewProjector::build(
    const ownership::OwnershipAdmittedBoundModule& requester,
    zc::ArrayPtr<const VerifiedInterfaceSource> dependencyInterfaces,
    const type::SemanticTypeStore& semanticTypes,
    const checker::CheckerIdentityAuthority& identities) {
  if (requester.semanticContext() != identities.semanticContext() ||
      requester.semanticContext() != semanticTypes.context()) {
    return zc::none;
  }

  zc::Vector<checker::cross_module::ImportedSignatureModule> modules;
  for (const auto& interfaceSource : dependencyInterfaces) {
    if (interfaceSource.is<ToolchainCoreVerifiedInterfaceSource>()) {
      const auto& source = interfaceSource.get<ToolchainCoreVerifiedInterfaceSource>().interface;
      const auto& record = source.record();
      if (source.module() == requester.module() ||
          source.context() != requester.semanticContext() ||
          source.fingerprint().digest() != requester.semanticFingerprint().digest()) {
        return zc::none;
      }

      zc::Vector<ImportedDefinitionBindingSelection> definitions;
      zc::Vector<ImportedModuleTargetSelection> moduleTargets;
      SignatureViewOrigin origin = SignatureViewOrigin::Prelude;
      bool hasOrigin = false;
      for (const auto& import : requester.resolvedImports()) {
        if (import.sourceModule != source.module()) { continue; }
        if (!hasDependencyEvidence(requester, import)) { return zc::none; }
        auto requestedName = importName(import.binding, false);
        auto localName = importName(import.binding, true);
        if (requestedName == zc::none || localName == zc::none) { return zc::none; }
        const auto& target = import.canonicalTarget.value();
        if (target.is<binder::DefinitionBindingTarget>()) {
          const auto definition = target.get<binder::DefinitionBindingTarget>().definition;
          auto root = coreRoot(record, definition, identities);
          if (root == zc::none) { return zc::none; }
          ZC_IF_SOME(value, root) {
            if (!hasCoreExport(record, ZC_ASSERT_NONNULL(requestedName),
                               value.canonicalDefinition)) {
              return zc::none;
            }
            auto requesterBinding = binder::BindingTarget::semanticImport(import.binding.clone());
            if (hasDefinitionSelection(definitions.asPtr(), requesterBinding)) { return zc::none; }
            definitions.add(ImportedDefinitionBindingSelection{
                zc::mv(requesterBinding), binder::BindingTarget::definition(definition),
                SignatureViewOrigin::ExplicitImport});
          }
        } else if (target.is<binder::ModuleBindingTarget>()) {
          if (hasTargetSelection(moduleTargets.asPtr(), ZC_ASSERT_NONNULL(localName))) {
            return zc::none;
          }
          const auto targetModule = target.get<binder::ModuleBindingTarget>().module;
          moduleTargets.add(ImportedModuleTargetSelection{
              ZC_ASSERT_NONNULL(localName).clone(),
              targetModule == source.module() ? ZC_ASSERT_NONNULL(localName).clone()
                                              : ZC_ASSERT_NONNULL(requestedName).clone(),
              SignatureViewOrigin::ExplicitImport});
        } else {
          return zc::none;
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
        for (const auto& root : record.signatureRoots()) {
          auto definition = definitionHandle(root.canonicalDefinition, identities);
          if (definition == zc::none) { return zc::none; }
          auto requesterBinding = binder::BindingTarget::definition(ZC_ASSERT_NONNULL(definition));
          if (!hasDefinitionSelection(definitions.asPtr(), requesterBinding)) {
            definitions.add(ImportedDefinitionBindingSelection{
                zc::mv(requesterBinding),
                binder::BindingTarget::definition(ZC_ASSERT_NONNULL(definition)),
                SignatureViewOrigin::NamespaceImport});
          }
        }
        retainStrongest(SignatureViewOrigin::NamespaceImport, origin, hasOrigin);
      }

      ZC_IF_SOME(prelude, requester.preludeSurface()) {
        if (prelude.module == source.module()) {
          retainStrongest(SignatureViewOrigin::Prelude, origin, hasOrigin);
        }
      }
      if (!hasOrigin) { continue; }
      auto projected = projectCore(requester, source, origin, definitions.asPtr(),
                                   moduleTargets.asPtr(), identities);
      if (projected == zc::none) { return zc::none; }
      ZC_IF_SOME(module, projected) { modules.add(zc::mv(module)); }
      continue;
    }

    if (!interfaceSource.is<UserVerifiedInterfaceSource>()) { return zc::none; }
    const auto& source = interfaceSource.get<UserVerifiedInterfaceSource>().interface;
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
