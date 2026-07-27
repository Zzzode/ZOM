// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/cross-module-facts.h"

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::checker::cross_module {
namespace {

void append(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> bytes) {
  output.addAll(bytes);
}

void appendDomain(zc::Vector<uint8_t>& output, zc::StringPtr domain) {
  for (const auto byte : domain) { output.add(static_cast<uint8_t>(byte)); }
  output.add(0);
}

void appendUint64(zc::Vector<uint8_t>& output, uint64_t value) {
  for (uint32_t index = 0; index < 8; ++index) {
    output.add(static_cast<uint8_t>((value >> (56U - index * 8U)) & 0xffU));
  }
}

void appendByteString(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> bytes) {
  appendUint64(output, bytes.size());
  append(output, bytes);
}

bool less(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

bool sameName(const binder::BindingNameKey& left, const binder::BindingNameKey& right) {
  return left.nameSpace() == right.nameSpace() && left.name() == right.name();
}

bool sameModuleKey(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool validImportedRootBinding(const binder::BindingTarget& binding,
                              const identity::ModuleKey& requester,
                              const identity::ModuleKey& sourceInterface,
                              identity::ModuleId sourceModule,
                              const identity::DefinitionIdentityRecord& canonical,
                              const identity::SemanticIdentityRegistrySet& registries) {
  const auto& value = binding.value();
  if (value.is<binder::DefinitionBindingTarget>()) {
    auto record = registries.definitions().lookupRecord(
        value.get<binder::DefinitionBindingTarget>().definition);
    ZC_IF_SOME(definition, record) {
      ZC_IF_SOME(module, registries.modules().find(definition.module())) {
        return module == sourceModule;
      }
    }
    return false;
  }
  if (!value.is<binder::SemanticImportBindingTarget>()) { return false; }
  const auto& semantic = value.get<binder::SemanticImportBindingTarget>().binding;
  const bool requesterOwned = sameModuleKey(semantic.requester(), requester) &&
                              sameModuleKey(semantic.resolution().requester(), requester);
  const bool sourceOwned = sameModuleKey(semantic.requester(), sourceInterface) &&
                           sameModuleKey(semantic.resolution().requester(), sourceInterface);
  return (requesterOwned || sourceOwned) && semantic.sourceNamespace() == canonical.nameSpace() &&
         semantic.sourceName().text() == canonical.name();
}

bool appendSortedRecords(zc::Vector<uint8_t>& output,
                         zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> records) {
  appendUint64(output, records.size());
  for (size_t index = 0; index < records.size(); ++index) {
    if (records[index].size() == 0 || (index != 0 && !less(records[index - 1], records[index]))) {
      return false;
    }
    appendByteString(output, records[index]);
  }
  return true;
}

}  // namespace

ImportedSignatureViewRevision::ImportedSignatureViewRevision(
    const identity::Sha256Digest& value) noexcept
    : value(value) {}

const identity::Sha256Digest& ImportedSignatureViewRevision::digest() const noexcept {
  return value;
}

zc::Maybe<ImportedSignatureViewRevision> ImportedSignatureViewRevision::computeFramed(
    const identity::Sha256Digest& contextFingerprint,
    zc::ArrayPtr<const uint8_t> expandedRequesterModule,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> moduleRecords) {
  if (expandedRequesterModule.size() == 0) { return zc::none; }
  zc::Vector<uint8_t> preimage;
  appendDomain(preimage, "zom.imported-signature-view"_zc);
  append(preimage, contextFingerprint.bytes());
  appendByteString(preimage, expandedRequesterModule);
  if (!appendSortedRecords(preimage, moduleRecords)) { return zc::none; }
  ZC_IF_SOME(digest, identity::sha256(preimage.asPtr())) {
    return ImportedSignatureViewRevision(digest);
  }
  return zc::none;
}

CoherenceViewRevision::CoherenceViewRevision(const identity::Sha256Digest& value) noexcept
    : value(value) {}

const identity::Sha256Digest& CoherenceViewRevision::digest() const noexcept { return value; }

zc::Maybe<CoherenceViewRevision> CoherenceViewRevision::computeFramed(
    const identity::Sha256Digest& contextFingerprint,
    const identity::Sha256Digest& markerPolicyRegistryRevision,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> moduleInterfaceRevisionRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> implHeadRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> markerFactRecords) {
  zc::Vector<uint8_t> preimage;
  appendDomain(preimage, "zom.coherence-view"_zc);
  append(preimage, contextFingerprint.bytes());
  append(preimage, markerPolicyRegistryRevision.bytes());
  if (!appendSortedRecords(preimage, moduleInterfaceRevisionRecords) ||
      !appendSortedRecords(preimage, implHeadRecords) ||
      !appendSortedRecords(preimage, markerFactRecords)) {
    return zc::none;
  }
  ZC_IF_SOME(digest, identity::sha256(preimage.asPtr())) { return CoherenceViewRevision(digest); }
  return zc::none;
}

zc::Maybe<zc::Array<uint8_t>> ImportedSignatureModuleCanonicalCodec::encodeFramed(
    SignatureViewOrigin origin, zc::ArrayPtr<const uint8_t> expandedSourceModule,
    const identity::Sha256Digest& interfaceRevision,
    const identity::Sha256Digest& bindingSurfaceRevision,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> authorizedRootRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> lookupDefinitionRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> supportDefinitionRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> moduleTargetRecords) {
  if (expandedSourceModule.size() == 0) { return zc::none; }
  switch (origin) {
    case SignatureViewOrigin::ExplicitImport:
    case SignatureViewOrigin::NamespaceImport:
    case SignatureViewOrigin::Prelude:
      break;
    default:
      return zc::none;
  }
  zc::Vector<uint8_t> record;
  record.add(static_cast<uint8_t>(origin));
  append(record, expandedSourceModule);
  append(record, interfaceRevision.bytes());
  append(record, bindingSurfaceRevision.bytes());
  if (!appendSortedRecords(record, authorizedRootRecords) ||
      !appendSortedRecords(record, lookupDefinitionRecords) ||
      !appendSortedRecords(record, supportDefinitionRecords) ||
      !appendSortedRecords(record, moduleTargetRecords)) {
    return zc::none;
  }
  return record.releaseAsArray();
}

ImportedModuleTarget ImportedModuleTarget::clone() const {
  return ImportedModuleTarget{name.clone(), module, surfaceRevision};
}

ImportedDefinitionBindingSelection ImportedDefinitionBindingSelection::clone() const {
  return ImportedDefinitionBindingSelection{requesterBinding.clone(), sourceBinding.clone(),
                                            authorizationOrigin};
}

struct ImportedSignatureModule::Impl final {
  Impl(identity::SemanticContextBrand semanticContext, identity::ModuleId requester,
       SignatureViewOrigin origin, identity::ModuleId sourceModule,
       module_interface::ModuleInterfaceRevision interfaceRevision,
       binder::ExportSurfaceRevision bindingSurfaceRevision,
       zc::Vector<module_interface::SignatureRootAuthorization>&& authorizedRoots,
       zc::Vector<signature::SemanticSignature>&& lookupDefinitions,
       zc::Vector<signature::SemanticSignature>&& supportDefinitions,
       zc::Vector<ImportedModuleTarget>&& moduleTargets, zc::Array<uint8_t>&& canonicalRecord)
      : semanticContext(semanticContext),
        requester(requester),
        origin(origin),
        sourceModule(sourceModule),
        interfaceRevision(interfaceRevision),
        bindingSurfaceRevision(bindingSurfaceRevision),
        authorizedRoots(zc::mv(authorizedRoots)),
        lookupDefinitions(zc::mv(lookupDefinitions)),
        supportDefinitions(zc::mv(supportDefinitions)),
        moduleTargets(zc::mv(moduleTargets)),
        canonicalRecord(zc::mv(canonicalRecord)) {}

  identity::SemanticContextBrand semanticContext;
  identity::ModuleId requester;
  SignatureViewOrigin origin;
  identity::ModuleId sourceModule;
  module_interface::ModuleInterfaceRevision interfaceRevision;
  binder::ExportSurfaceRevision bindingSurfaceRevision;
  zc::Vector<module_interface::SignatureRootAuthorization> authorizedRoots;
  zc::Vector<signature::SemanticSignature> lookupDefinitions;
  zc::Vector<signature::SemanticSignature> supportDefinitions;
  zc::Vector<ImportedModuleTarget> moduleTargets;
  zc::Array<uint8_t> canonicalRecord;
};

ImportedSignatureModule::ImportedSignatureModule(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
ImportedSignatureModule::~ImportedSignatureModule() noexcept(false) = default;
ImportedSignatureModule::ImportedSignatureModule(ImportedSignatureModule&&) noexcept = default;
ImportedSignatureModule& ImportedSignatureModule::operator=(ImportedSignatureModule&&) noexcept =
    default;

ImportedSignatureModule ImportedSignatureModule::publish(
    identity::SemanticContextBrand semanticContext, identity::ModuleId requester,
    SignatureViewOrigin origin, identity::ModuleId sourceModule,
    module_interface::ModuleInterfaceRevision interfaceRevision,
    binder::ExportSurfaceRevision bindingSurfaceRevision,
    zc::Vector<module_interface::SignatureRootAuthorization>&& authorizedRoots,
    zc::Vector<signature::SemanticSignature>&& lookupDefinitions,
    zc::Vector<signature::SemanticSignature>&& supportDefinitions,
    zc::Vector<ImportedModuleTarget>&& moduleTargets, zc::Array<uint8_t>&& canonicalRecord) {
  return ImportedSignatureModule(
      zc::heap<Impl>(semanticContext, requester, origin, sourceModule, interfaceRevision,
                     bindingSurfaceRevision, zc::mv(authorizedRoots), zc::mv(lookupDefinitions),
                     zc::mv(supportDefinitions), zc::mv(moduleTargets), zc::mv(canonicalRecord)));
}

identity::SemanticContextBrand ImportedSignatureModule::authorizedContext() const noexcept {
  return impl->semanticContext;
}
identity::ModuleId ImportedSignatureModule::authorizedRequester() const noexcept {
  return impl->requester;
}

SignatureViewOrigin ImportedSignatureModule::origin() const noexcept { return impl->origin; }
identity::ModuleId ImportedSignatureModule::sourceModule() const noexcept {
  return impl->sourceModule;
}
const module_interface::ModuleInterfaceRevision& ImportedSignatureModule::interfaceRevision()
    const noexcept {
  return impl->interfaceRevision;
}
const binder::ExportSurfaceRevision& ImportedSignatureModule::bindingSurfaceRevision()
    const noexcept {
  return impl->bindingSurfaceRevision;
}
zc::ArrayPtr<const module_interface::SignatureRootAuthorization>
ImportedSignatureModule::authorizedRoots() const noexcept {
  return impl->authorizedRoots;
}
zc::ArrayPtr<const signature::SemanticSignature> ImportedSignatureModule::lookupDefinitions()
    const noexcept {
  return impl->lookupDefinitions;
}
zc::ArrayPtr<const signature::SemanticSignature> ImportedSignatureModule::supportDefinitions()
    const noexcept {
  return impl->supportDefinitions;
}
zc::ArrayPtr<const ImportedModuleTarget> ImportedSignatureModule::moduleTargets() const noexcept {
  return impl->moduleTargets;
}
zc::Maybe<const module_interface::SignatureRootAuthorization&>
ImportedSignatureModule::authorization(const binder::BindingTarget& binding) const noexcept {
  for (const auto& root : impl->authorizedRoots) {
    if (module_interface::sameSignatureRootBinding(root.binding, binding)) { return root; }
  }
  return zc::none;
}
zc::Maybe<const signature::SemanticSignature&> ImportedSignatureModule::lookupDefinition(
    identity::DefId definition) const noexcept {
  for (const auto& value : impl->lookupDefinitions) {
    if (value.definition == definition) { return value; }
  }
  return zc::none;
}
zc::Maybe<const signature::SemanticSignature&> ImportedSignatureModule::supportDefinition(
    identity::DefId definition) const noexcept {
  for (const auto& value : impl->supportDefinitions) {
    if (value.definition == definition) { return value; }
  }
  return zc::none;
}
zc::Maybe<const ImportedModuleTarget&> ImportedSignatureModule::moduleTarget(
    const binder::BindingNameKey& name) const noexcept {
  for (const auto& value : impl->moduleTargets) {
    if (sameName(value.name, name)) { return value; }
  }
  return zc::none;
}
zc::ArrayPtr<const uint8_t> ImportedSignatureModule::canonicalRecord() const noexcept {
  return impl->canonicalRecord;
}

struct ImportedSignatureView::Impl final {
  Impl(identity::SemanticContextBrand semanticContext,
       identity::SemanticContextFingerprint&& contextFingerprint, identity::ModuleId requester,
       ImportedSignatureViewRevision revision, zc::Vector<ImportedSignatureModule>&& modules)
      : semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        requester(requester),
        revision(revision),
        modules(zc::mv(modules)) {}

  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::ModuleId requester;
  ImportedSignatureViewRevision revision;
  zc::Vector<ImportedSignatureModule> modules;
};

ImportedSignatureView::ImportedSignatureView(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
ImportedSignatureView::~ImportedSignatureView() noexcept(false) = default;
ImportedSignatureView::ImportedSignatureView(ImportedSignatureView&&) noexcept = default;
ImportedSignatureView& ImportedSignatureView::operator=(ImportedSignatureView&&) noexcept = default;
identity::SemanticContextBrand ImportedSignatureView::semanticContext() const noexcept {
  return impl->semanticContext;
}
const identity::SemanticContextFingerprint& ImportedSignatureView::contextFingerprint()
    const noexcept {
  return impl->contextFingerprint;
}
identity::ModuleId ImportedSignatureView::requester() const noexcept { return impl->requester; }
const ImportedSignatureViewRevision& ImportedSignatureView::revision() const noexcept {
  return impl->revision;
}
zc::ArrayPtr<const ImportedSignatureModule> ImportedSignatureView::modules() const noexcept {
  return impl->modules;
}
zc::Maybe<const ImportedSignatureModule&> ImportedSignatureView::source(
    identity::ModuleId module) const noexcept {
  for (const auto& value : impl->modules) {
    if (value.sourceModule() == module) { return value; }
  }
  return zc::none;
}
zc::Maybe<const module_interface::SignatureRootAuthorization&> ImportedSignatureView::authorization(
    const binder::BindingTarget& binding) const noexcept {
  for (const auto& module : impl->modules) {
    ZC_IF_SOME(value, module.authorization(binding)) { return value; }
  }
  return zc::none;
}
zc::Maybe<const signature::SemanticSignature&> ImportedSignatureView::lookupDefinition(
    identity::DefId definition) const noexcept {
  for (const auto& module : impl->modules) {
    ZC_IF_SOME(value, module.lookupDefinition(definition)) { return value; }
  }
  return zc::none;
}
zc::Maybe<const signature::SemanticSignature&> ImportedSignatureView::supportDefinition(
    identity::DefId definition) const noexcept {
  for (const auto& module : impl->modules) {
    ZC_IF_SOME(value, module.supportDefinition(definition)) { return value; }
  }
  return zc::none;
}
zc::Maybe<const ImportedModuleTarget&> ImportedSignatureView::moduleTarget(
    identity::ModuleId sourceModule, const binder::BindingNameKey& name) const noexcept {
  ZC_IF_SOME(module, source(sourceModule)) { return module.moduleTarget(name); }
  return zc::none;
}

zc::Maybe<ImportedSignatureView> ImportedSignatureViewBuilder::build(
    identity::SemanticContextBrand semanticContext,
    const identity::SemanticContextFingerprint& contextFingerprint, identity::ModuleId requester,
    zc::Vector<ImportedSignatureModule>&& modules,
    const identity::SemanticIdentityRegistrySet& registries) {
  if (!semanticContext.isValid() || registries.context() != semanticContext) { return zc::none; }
  auto requesterKey = registries.modules().lookup(requester);
  if (requesterKey == zc::none) { return zc::none; }

  for (size_t index = 0; index < modules.size(); ++index) {
    auto sourceInterface = registries.modules().lookup(modules[index].sourceModule());
    if (modules[index].authorizedContext() != semanticContext ||
        modules[index].authorizedRequester() != requester ||
        modules[index].sourceModule() == requester || sourceInterface == zc::none ||
        modules[index].canonicalRecord().size() == 0) {
      return zc::none;
    }
    for (size_t rootIndex = 0; rootIndex < modules[index].authorizedRoots().size(); ++rootIndex) {
      const auto& root = modules[index].authorizedRoots()[rootIndex];
      auto canonicalRecord = registries.definitions().lookupRecord(root.canonicalDefinition);
      if (!module_interface::isSignatureRootBinding(root.binding) || canonicalRecord == zc::none ||
          root.bindingSurfaceRevision.digest() !=
              modules[index].bindingSurfaceRevision().digest() ||
          modules[index].lookupDefinition(root.canonicalDefinition) == zc::none) {
        return zc::none;
      }
      ZC_IF_SOME(canonical, canonicalRecord) {
        ZC_IF_SOME(sourceModule, registries.modules().find(canonical.module())) {
          ZC_IF_SOME(requesterModule, requesterKey) {
            ZC_IF_SOME(interfaceModule, sourceInterface) {
              if (sourceModule != root.sourceModule ||
                  !validImportedRootBinding(root.binding, requesterModule, interfaceModule,
                                            sourceModule, canonical, registries)) {
                return zc::none;
              }
            }
          }
        } else {
          return zc::none;
        }
      }
      const auto& origin = root.origin.variant();
      if (!origin.is<module_interface::ImportedSignatureAuthorization>() ||
          origin.get<module_interface::ImportedSignatureAuthorization>()
                  .interfaceRevision.digest() != modules[index].interfaceRevision().digest()) {
        return zc::none;
      }
      for (size_t prior = 0; prior < rootIndex; ++prior) {
        if (module_interface::sameSignatureRootBinding(
                modules[index].authorizedRoots()[prior].binding, root.binding)) {
          return zc::none;
        }
      }
    }
    for (size_t definitionIndex = 0; definitionIndex < modules[index].lookupDefinitions().size();
         ++definitionIndex) {
      const auto definition = modules[index].lookupDefinitions()[definitionIndex].definition;
      for (size_t prior = 0; prior < definitionIndex; ++prior) {
        if (modules[index].lookupDefinitions()[prior].definition == definition) { return zc::none; }
      }
      if (modules[index].supportDefinition(definition) != zc::none) { return zc::none; }
    }
    for (const auto& target : modules[index].moduleTargets()) {
      if (registries.modules().lookup(target.module) == zc::none) { return zc::none; }
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (modules[prior].sourceModule() == modules[index].sourceModule()) { return zc::none; }
    }
  }

  for (size_t outer = 1; outer < modules.size(); ++outer) {
    size_t index = outer;
    while (index != 0 &&
           less(modules[index].canonicalRecord(), modules[index - 1].canonicalRecord())) {
      auto temporary = zc::mv(modules[index]);
      modules[index] = zc::mv(modules[index - 1]);
      modules[index - 1] = zc::mv(temporary);
      --index;
    }
  }
  for (size_t index = 1; index < modules.size(); ++index) {
    if (!less(modules[index - 1].canonicalRecord(), modules[index].canonicalRecord())) {
      return zc::none;
    }
  }

  identity::CanonicalEncoder requesterEncoder;
  ZC_IF_SOME(key, requesterKey) { key.encode(requesterEncoder); }
  auto requesterBytes = requesterEncoder.finish();
  zc::Vector<zc::ArrayPtr<const uint8_t>> records(modules.size());
  for (const auto& module : modules) { records.add(module.canonicalRecord()); }
  auto revision = ImportedSignatureViewRevision::computeFramed(
      contextFingerprint.digest(), requesterBytes.asPtr(), records.asPtr());
  ZC_IF_SOME(value, revision) {
    return ImportedSignatureView(zc::heap<ImportedSignatureView::Impl>(
        semanticContext, contextFingerprint.clone(), requester, value, zc::mv(modules)));
  }
  return zc::none;
}

}  // namespace zomlang::compiler::checker::cross_module
