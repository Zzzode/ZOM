// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/materialized-export-surface-verifier.h"

#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

struct SurfaceOrderKey final {
  SurfaceOrderKey(Namespace nameSpace, zc::String&& name) noexcept
      : nameSpace(nameSpace), name(zc::mv(name)) {}
  SurfaceOrderKey(SurfaceOrderKey&&) noexcept = default;
  SurfaceOrderKey& operator=(SurfaceOrderKey&&) noexcept = default;
  ZC_DISALLOW_COPY(SurfaceOrderKey);

  bool operator==(const SurfaceOrderKey& other) const noexcept {
    return nameSpace == other.nameSpace && name == other.name;
  }
  bool operator<(const SurfaceOrderKey& other) const noexcept {
    if (nameSpace != other.nameSpace) {
      return static_cast<uint8_t>(nameSpace) < static_cast<uint8_t>(other.nameSpace);
    }
    return name < other.name;
  }

  Namespace nameSpace;
  zc::String name;
};

bool sameKey(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool sameSource(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().encode().asPtr() == right.source().encode().asPtr() &&
         left.byteStart() == right.byteStart() && left.byteEnd() == right.byteEnd();
}

bool isSurfaceImport(const identity::SemanticImportBindingKey& binding) {
  return binding.operation() == identity::SemanticImportOperation::Import ||
         binding.operation() == identity::SemanticImportOperation::ForeignReexport;
}

size_t surfaceImportCount(const BoundModuleSkeleton& stableWitness) {
  size_t result = 0;
  for (const auto& stable : stableWitness.imports().values()) {
    if (isSurfaceImport(stable.queryKey().binding())) { ++result; }
  }
  return result;
}

bool isModuleSurfaceDefinition(identity::DefinitionKind kind) {
  switch (kind) {
    case identity::DefinitionKind::ModuleAlias:
    case identity::DefinitionKind::Function:
    case identity::DefinitionKind::Class:
    case identity::DefinitionKind::Struct:
    case identity::DefinitionKind::Interface:
    case identity::DefinitionKind::Enum:
    case identity::DefinitionKind::Error:
    case identity::DefinitionKind::TypeAlias:
    case identity::DefinitionKind::Constant:
    case identity::DefinitionKind::Static:
      return true;
    case identity::DefinitionKind::Method:
    case identity::DefinitionKind::Constructor:
    case identity::DefinitionKind::Destructor:
    case identity::DefinitionKind::AssociatedType:
    case identity::DefinitionKind::Field:
    case identity::DefinitionKind::EnumVariant:
    case identity::DefinitionKind::Parameter:
    case identity::DefinitionKind::TypeParameter:
    case identity::DefinitionKind::Local:
    case identity::DefinitionKind::PatternBinding:
    case identity::DefinitionKind::Closure:
      return false;
  }
  return false;
}

bool sameTarget(const BindingTarget& left, const BindingTarget& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<DefinitionBindingTarget>()) {
    return rightValue.is<DefinitionBindingTarget>() &&
           leftValue.get<DefinitionBindingTarget>().definition ==
               rightValue.get<DefinitionBindingTarget>().definition;
  }
  if (leftValue.is<SemanticImportBindingTarget>()) {
    return rightValue.is<SemanticImportBindingTarget>() &&
           leftValue.get<SemanticImportBindingTarget>().binding ==
               rightValue.get<SemanticImportBindingTarget>().binding;
  }
  return leftValue.is<ModuleBindingTarget>() && rightValue.is<ModuleBindingTarget>() &&
         leftValue.get<ModuleBindingTarget>().module ==
             rightValue.get<ModuleBindingTarget>().module;
}

zc::Maybe<const MaterializedDefinitionIdentityEntry&> definitionIdentity(
    zc::ArrayPtr<const MaterializedDefinitionIdentityEntry> identities,
    identity::DefId definition) {
  zc::Maybe<const MaterializedDefinitionIdentityEntry&> result;
  for (const auto& identity : identities) {
    if (identity.handle() != definition) { continue; }
    if (result != zc::none) { return zc::none; }
    result = identity;
  }
  return result;
}

zc::Maybe<const MaterializedDefinitionIdentityEntry&> dependencyDefinitionIdentity(
    zc::ArrayPtr<const MaterializedDependencyExportSurface> dependencies,
    identity::DefId definition) {
  zc::Maybe<const MaterializedDefinitionIdentityEntry&> result;
  for (const auto& dependency : dependencies) {
    for (const auto& identity : dependency.definitions) {
      if (identity.handle() != definition) { continue; }
      if (result != zc::none) { return zc::none; }
      result = identity;
    }
  }
  return result;
}

zc::Maybe<const identity::ModuleKey&> moduleKeyFor(
    const identity::ModuleKey& localKey, identity::ModuleId localModule,
    zc::ArrayPtr<const MaterializedDependencyExportSurface> dependencies,
    identity::ModuleId module) {
  if (module == localModule) { return localKey; }
  zc::Maybe<const identity::ModuleKey&> result;
  for (const auto& dependency : dependencies) {
    if (dependency.module != module) { continue; }
    if (result != zc::none) { return zc::none; }
    result = dependency.moduleKey;
  }
  return result;
}

zc::Maybe<const DefinitionFact&> definitionFact(zc::ArrayPtr<const DefinitionFact> definitions,
                                                identity::DefId definition) {
  zc::Maybe<const DefinitionFact&> result;
  for (const auto& candidate : definitions) {
    if (candidate.identity != definition) { continue; }
    if (result != zc::none) { return zc::none; }
    result = candidate;
  }
  return result;
}

bool encodeTarget(identity::CanonicalEncoder& encoder,
                  zc::ArrayPtr<const MaterializedDefinitionIdentityEntry> identities,
                  zc::ArrayPtr<const MaterializedDependencyExportSurface> dependencies,
                  const identity::ModuleKey& localModuleKey, identity::ModuleId localModule,
                  const BindingTarget& target) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    auto identity = definitionIdentity(identities, value.get<DefinitionBindingTarget>().definition);
    if (identity == zc::none) {
      identity = dependencyDefinitionIdentity(dependencies,
                                              value.get<DefinitionBindingTarget>().definition);
    }
    if (identity == zc::none) { return false; }
    encoder.encodeUint8(0x01);
    ZC_ASSERT_NONNULL(identity).key().encode(encoder);
    return true;
  }
  if (value.is<SemanticImportBindingTarget>()) {
    encoder.encodeUint8(0x05);
    encoder.encodeByteString(value.get<SemanticImportBindingTarget>().binding.encode().asPtr());
    return true;
  }
  if (!value.is<ModuleBindingTarget>()) { return false; }
  auto moduleKey = moduleKeyFor(localModuleKey, localModule, dependencies,
                                value.get<ModuleBindingTarget>().module);
  if (moduleKey == zc::none) { return false; }
  encoder.encodeUint8(0x06);
  ZC_ASSERT_NONNULL(moduleKey).encode(encoder);
  return true;
}

bool encodeEntry(identity::CanonicalEncoder& encoder,
                 zc::ArrayPtr<const MaterializedDefinitionIdentityEntry> identities,
                 zc::ArrayPtr<const MaterializedDependencyExportSurface> dependencies,
                 const identity::ModuleKey& module, identity::ModuleId localModule,
                 const ExportSurfaceEntry& entry) {
  encoder.encodeUint8(static_cast<uint8_t>(entry.name.nameSpace()));
  entry.name.name().encode(encoder);
  if (!encodeTarget(encoder, identities, dependencies, module, localModule,
                    entry.bindingIdentity) ||
      !encodeTarget(encoder, identities, dependencies, module, localModule,
                    entry.canonicalTarget)) {
    return false;
  }
  if (entry.visibility.value().is<ModuleVisibility>()) {
    if (entry.visibility.value().get<ModuleVisibility>().module == identity::ModuleId()) {
      return false;
    }
    encoder.encodeUint8(0x01);
    module.encode(encoder);
  } else {
    encoder.encodeUint8(0x02);
  }
  encoder.encodeBool(entry.exported);
  entry.bindingSpan.encode(encoder);
  entry.canonicalDeclarationSpan.encode(encoder);
  ZC_IF_SOME(alias, entry.aliasSpan) {
    encoder.encodeSome();
    alias.encode(encoder);
  } else {
    encoder.encodeNone();
  }
  ZC_IF_SOME(exportSpan, entry.exportSpan) {
    encoder.encodeSome();
    exportSpan.encode(encoder);
  } else {
    encoder.encodeNone();
  }
  encoder.encodeSequenceSize(entry.reexportChain.size());
  for (const auto& step : entry.reexportChain) {
    if (step.module == identity::ModuleId() ||
        !sameTarget(step.canonicalTarget, entry.canonicalTarget) ||
        !encodeTarget(encoder, identities, dependencies, module, localModule,
                      step.bindingIdentity) ||
        !encodeTarget(encoder, identities, dependencies, module, localModule,
                      step.canonicalTarget)) {
      return false;
    }
    auto stepModule = moduleKeyFor(module, localModule, dependencies, step.module);
    if (stepModule == zc::none) { return false; }
    ZC_ASSERT_NONNULL(stepModule).encode(encoder);
    step.exportSpan.encode(encoder);
  }
  return true;
}

zc::Maybe<zc::Array<uint8_t>> encodeEntries(
    zc::ArrayPtr<const MaterializedDefinitionIdentityEntry> identities,
    zc::ArrayPtr<const MaterializedDependencyExportSurface> dependencies,
    const identity::ModuleKey& module, identity::ModuleId localModule,
    zc::ArrayPtr<const ExportSurfaceEntry> entries) {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(entries.size());
  for (const auto& entry : entries) {
    encoder.encodeUint8(static_cast<uint8_t>(entry.name.nameSpace()));
    entry.name.name().encode(encoder);
    if (!encodeEntry(encoder, identities, dependencies, module, localModule, entry)) {
      return zc::none;
    }
  }
  return encoder.finish();
}

ExportSurfaceEntry cloneEntry(const ExportSurfaceEntry& entry) {
  zc::Maybe<identity::SourceSpan> aliasSpan;
  ZC_IF_SOME(value, entry.aliasSpan) { aliasSpan = value.clone(); }
  zc::Maybe<identity::SourceSpan> exportSpan;
  ZC_IF_SOME(value, entry.exportSpan) { exportSpan = value.clone(); }
  zc::Vector<ReexportProvenanceStep> chain;
  for (const auto& step : entry.reexportChain) {
    chain.add(ReexportProvenanceStep{step.module, step.bindingIdentity.clone(),
                                     step.canonicalTarget.clone(), step.exportSpan.clone()});
  }
  const auto& visibility = entry.visibility.value();
  auto copiedVisibility =
      visibility.is<ModuleVisibility>()
          ? VisibilityEnvelope::module(visibility.get<ModuleVisibility>().module)
          : VisibilityEnvelope::external();
  return ExportSurfaceEntry(entry.name.clone(), entry.bindingIdentity.clone(),
                            entry.canonicalTarget.clone(), zc::mv(copiedVisibility), entry.exported,
                            entry.bindingSpan.clone(), entry.canonicalDeclarationSpan.clone(),
                            zc::mv(aliasSpan), zc::mv(exportSpan), zc::mv(chain));
}

}  // namespace

zc::Maybe<VerifiedExportSurface> MaterializedExportSurfaceVerifier::from(
    identity::SemanticContextBrand context, const identity::SemanticContextFingerprint& fingerprint,
    const identity::ModuleKey& moduleKey, identity::ModuleId module,
    const identity::CompilationUnitIdentity& compilationUnitKey,
    identity::CompilationUnitId compilationUnit, const identity::SourceFileKey& source,
    const BoundModuleSkeleton& stableWitness,
    zc::ArrayPtr<const MaterializedDefinitionIdentityEntry> identities,
    zc::ArrayPtr<const DefinitionFact> definitions,
    zc::ArrayPtr<const MaterializedDependencyExportSurface> dependencies,
    zc::ArrayPtr<const ImportBindingFact> imports,
    zc::ArrayPtr<const LocalExportFact> localExports) {
  if (!context.isValid() || !module.belongsTo(context) || !compilationUnit.belongsTo(context) ||
      !sameKey(moduleKey, stableWitness.module()) ||
      moduleKey.crate().unit().encode().asPtr() != compilationUnitKey.encode().asPtr() ||
      !source.belongsTo(moduleKey.crate()) || surfaceImportCount(stableWitness) != imports.size()) {
    return zc::none;
  }
  const auto stableExports = stableWitness.localExports().values();
  if (stableExports.size() != localExports.size()) { return zc::none; }

  zc::Vector<ExportSurfaceEntry> unordered;
  zc::TreeMap<SurfaceOrderKey, size_t> order;
  for (const auto& definition : definitions) {
    if (definition.activation != DefinitionActivation::ModuleSkeleton ||
        !isModuleSurfaceDefinition(definition.kind)) {
      continue;
    }
    if (!definition.source.belongsTo(source) ||
        definitionIdentity(identities, definition.identity) == zc::none) {
      return zc::none;
    }
    auto name = BindingNameKey::from(definition.nameSpace, definition.name.clone());
    if (name == zc::none) { return zc::none; }
    auto key = SurfaceOrderKey(ZC_ASSERT_NONNULL(name).nameSpace(),
                               zc::str(ZC_ASSERT_NONNULL(name).name().text()));
    if (order.find(key) != zc::none) { return zc::none; }
    zc::Maybe<identity::SourceSpan> noAlias;
    zc::Maybe<identity::SourceSpan> noExport;
    zc::Vector<ReexportProvenanceStep> noChain;
    const auto index = unordered.size();
    unordered.add(ExportSurfaceEntry(
        zc::mv(ZC_ASSERT_NONNULL(name)), BindingTarget::definition(definition.identity),
        BindingTarget::definition(definition.identity), VisibilityEnvelope::module(module), false,
        definition.source.clone(), definition.source.clone(), zc::mv(noAlias), zc::mv(noExport),
        zc::mv(noChain)));
    order.insert(zc::mv(key), index);
  }

  for (size_t index = 0; index < stableExports.size(); ++index) {
    const auto& stableExport = stableExports[index];
    const auto& fact = localExports[index];
    const auto& stableBinding = stableExport.binding().value();
    const auto& stableCanonical = stableExport.canonicalTarget().value();
    if (!sameKey(stableExport.declaringModule(), moduleKey)) { return zc::none; }
    if (stableBinding.is<StableSemanticImportBindingTarget>()) {
      const auto& importBinding =
          stableBinding.get<StableSemanticImportBindingTarget>().import.binding();
      if (!stableCanonical.is<StableDefinitionBindingTarget>() ||
          !fact.sourceBinding.value().is<SemanticImportBindingTarget>() ||
          fact.sourceBinding.value().get<SemanticImportBindingTarget>().binding != importBinding ||
          !fact.bindingSpan.belongsTo(source) || !fact.exportSpan.belongsTo(source) ||
          fact.reexportChain.size() != 1 || stableExport.reexportChain().values().size() != 1) {
        return zc::none;
      }
      const ImportBindingFact* imported = nullptr;
      for (const auto& candidate : imports) {
        if (candidate.binding != importBinding) { continue; }
        if (imported != nullptr) { return zc::none; }
        imported = &candidate;
      }
      if (imported == nullptr || imported->kind != ImportBindingKind::ForeignReexport ||
          !sameTarget(fact.canonicalTarget, imported->canonicalTarget) ||
          !sameSource(fact.bindingSpan, imported->declarationSpan) ||
          !sameSource(fact.exportSpan, fact.reexportChain[0].exportSpan)) {
        return zc::none;
      }
      auto entryName =
          BindingNameKey::from(stableExport.name().nameSpace(), stableExport.name().name().clone());
      if (entryName == zc::none) { return zc::none; }
      auto orderKey = SurfaceOrderKey(ZC_ASSERT_NONNULL(entryName).nameSpace(),
                                      zc::str(ZC_ASSERT_NONNULL(entryName).name().text()));
      if (order.find(orderKey) != zc::none) { return zc::none; }
      zc::Maybe<identity::SourceSpan> aliasSpan;
      ZC_IF_SOME(value, fact.aliasSpan) { aliasSpan = value.clone(); }
      zc::Maybe<identity::SourceSpan> exportSpan = fact.exportSpan.clone();
      zc::Vector<ReexportProvenanceStep> chain;
      chain.add(fact.reexportChain[0].clone());
      const auto position = unordered.size();
      unordered.add(
          ExportSurfaceEntry(zc::mv(ZC_ASSERT_NONNULL(entryName)), fact.sourceBinding.clone(),
                             fact.canonicalTarget.clone(), VisibilityEnvelope::external(), true,
                             fact.bindingSpan.clone(), fact.canonicalDeclarationSpan.clone(),
                             zc::mv(aliasSpan), zc::mv(exportSpan), zc::mv(chain)));
      order.insert(zc::mv(orderKey), position);
      continue;
    }
    if (!stableBinding.is<StableDefinitionBindingTarget>() ||
        !stableCanonical.is<StableDefinitionBindingTarget>() ||
        !sameTarget(fact.sourceBinding, fact.canonicalTarget) ||
        !fact.bindingSpan.belongsTo(source) || !fact.canonicalDeclarationSpan.belongsTo(source) ||
        !fact.exportSpan.belongsTo(source)) {
      return zc::none;
    }
    const auto& bindingKey =
        stableBinding.get<StableDefinitionBindingTarget>().definition.definition();
    auto identity = definitionIdentity(
        identities, fact.sourceBinding.value().get<DefinitionBindingTarget>().definition);
    if (identity == zc::none || ZC_ASSERT_NONNULL(identity).key() != bindingKey ||
        ZC_ASSERT_NONNULL(identity).key() !=
            stableCanonical.get<StableDefinitionBindingTarget>().definition.definition()) {
      return zc::none;
    }
    auto definition = definitionFact(definitions, ZC_ASSERT_NONNULL(identity).handle());
    if (definition == zc::none || !fact.bindingSpan.belongsTo(source) ||
        !fact.canonicalDeclarationSpan.belongsTo(source)) {
      return zc::none;
    }
    const auto& chain = stableExport.reexportChain().values();
    auto exportedName = stableExport.name().clone();
    auto entryName = BindingNameKey::from(exportedName.nameSpace(), exportedName.name().clone());
    if (entryName == zc::none) { return zc::none; }
    auto orderKey = SurfaceOrderKey(ZC_ASSERT_NONNULL(entryName).nameSpace(),
                                    zc::str(ZC_ASSERT_NONNULL(entryName).name().text()));
    if (chain.size() == 0) {
      auto existing = order.find(orderKey);
      if (existing == zc::none || unordered[ZC_ASSERT_NONNULL(existing)]
                                          .bindingIdentity.value()
                                          .get<DefinitionBindingTarget>()
                                          .definition != ZC_ASSERT_NONNULL(identity).handle()) {
        return zc::none;
      }
      auto& entry = unordered[ZC_ASSERT_NONNULL(existing)];
      entry.visibility = VisibilityEnvelope::external();
      entry.exported = true;
      entry.exportSpan = fact.exportSpan.clone();
      continue;
    }
    if (chain.size() != 1 || order.find(orderKey) != zc::none ||
        !sameTarget(fact.sourceBinding, fact.canonicalTarget)) {
      return zc::none;
    }
    zc::Maybe<identity::SourceSpan> aliasSpan;
    ZC_IF_SOME(value, fact.aliasSpan) { aliasSpan = value.clone(); }
    zc::Maybe<identity::SourceSpan> exportSpan = fact.exportSpan.clone();
    zc::Vector<ReexportProvenanceStep> copiedChain;
    for (const auto& step : fact.reexportChain) {
      if (step.module != module || !sameTarget(step.bindingIdentity, fact.sourceBinding) ||
          !sameTarget(step.canonicalTarget, fact.canonicalTarget) ||
          !step.exportSpan.belongsTo(source)) {
        return zc::none;
      }
      copiedChain.add(ReexportProvenanceStep{step.module, step.bindingIdentity.clone(),
                                             step.canonicalTarget.clone(),
                                             step.exportSpan.clone()});
    }
    if (copiedChain.size() != 1) { return zc::none; }
    const auto position = unordered.size();
    unordered.add(ExportSurfaceEntry(zc::mv(ZC_ASSERT_NONNULL(entryName)),
                                     fact.sourceBinding.clone(), fact.canonicalTarget.clone(),
                                     VisibilityEnvelope::external(), true, fact.bindingSpan.clone(),
                                     fact.canonicalDeclarationSpan.clone(), zc::mv(aliasSpan),
                                     zc::mv(exportSpan), zc::mv(copiedChain)));
    order.insert(zc::mv(orderKey), position);
  }

  for (size_t index = 0; index < imports.size(); ++index) {
    const auto& fact = imports[index];
    const StableImportFact* stableImport = nullptr;
    for (const auto& candidate : stableWitness.imports().values()) {
      if (candidate.queryKey().binding() != fact.binding) { continue; }
      if (stableImport != nullptr) { return zc::none; }
      stableImport = &candidate;
    }
    const auto& binding = fact.binding;
    const bool isImport = binding.operation() == identity::SemanticImportOperation::Import;
    if (stableImport == nullptr ||
        !ZC_ASSERT_NONNULL(stableImport).target().value().is<StableSemanticImportBindingTarget>() ||
        (fact.kind == ImportBindingKind::Import) != isImport ||
        fact.sourceModule == identity::ModuleId() || fact.sourceModule == module ||
        !fact.declarationSpan.belongsTo(source)) {
      return zc::none;
    }
    auto nameSpace = BindingNameKey::from(
        binding.localNamespace() == identity::DefinitionNamespace::Value  ? Namespace::Value
        : binding.localNamespace() == identity::DefinitionNamespace::Type ? Namespace::Type
                                                                          : Namespace::Module,
        binding.localName().clone());
    if (nameSpace == zc::none) { return zc::none; }
    auto orderKey = SurfaceOrderKey(ZC_ASSERT_NONNULL(nameSpace).nameSpace(),
                                    zc::str(ZC_ASSERT_NONNULL(nameSpace).name().text()));
    auto existing = order.find(orderKey);
    if (existing != zc::none) {
      const auto& entry = unordered[ZC_ASSERT_NONNULL(existing)];
      if (isImport || !entry.exported ||
          !sameTarget(entry.bindingIdentity, BindingTarget::semanticImport(binding.clone())) ||
          !sameTarget(entry.canonicalTarget, fact.canonicalTarget)) {
        return zc::none;
      }
      continue;
    }
    const MaterializedDependencyExportSurface* dependency = nullptr;
    for (const auto& candidate : dependencies) {
      if (candidate.module != fact.sourceModule ||
          candidate.surface.revision().digest() != fact.sourceRevision.digest()) {
        continue;
      }
      if (dependency != nullptr) { return zc::none; }
      dependency = &candidate;
    }
    if (dependency == nullptr) { return zc::none; }
    const ExportSurfaceEntry* sourceEntry = nullptr;
    const auto& canonicalTarget = fact.canonicalTarget.value();
    const bool importsModule = canonicalTarget.is<ModuleBindingTarget>();
    if (importsModule) {
      if (canonicalTarget.get<ModuleBindingTarget>().module != fact.sourceModule) {
        return zc::none;
      }
    } else {
      for (const auto& candidate : dependency->surface.exports()) {
        if (candidate.name.nameSpace() !=
                (binding.sourceNamespace() == identity::DefinitionNamespace::Value
                     ? Namespace::Value
                 : binding.sourceNamespace() == identity::DefinitionNamespace::Type
                     ? Namespace::Type
                     : Namespace::Module) ||
            candidate.name.name().text() != binding.sourceName().text() ||
            !sameTarget(candidate.canonicalTarget, fact.canonicalTarget)) {
          continue;
        }
        if (sourceEntry != nullptr) { return zc::none; }
        sourceEntry = &candidate;
      }
      if (sourceEntry == nullptr) { return zc::none; }
    }
    zc::Maybe<identity::SourceSpan> aliasSpan;
    ZC_IF_SOME(value, fact.aliasSpan) { aliasSpan = value.clone(); }
    zc::Maybe<identity::SourceSpan> exportSpan;
    zc::Vector<ReexportProvenanceStep> chain;
    if (!isImport) {
      if (fact.reexportChain.size() != 1 || fact.reexportChain[0].module != module ||
          !sameTarget(fact.reexportChain[0].bindingIdentity,
                      BindingTarget::semanticImport(binding.clone())) ||
          !sameTarget(fact.reexportChain[0].canonicalTarget, fact.canonicalTarget) ||
          !sameSource(fact.reexportChain[0].exportSpan, fact.declarationSpan)) {
        return zc::none;
      }
      exportSpan = fact.declarationSpan.clone();
      chain.add(fact.reexportChain[0].clone());
    } else if (fact.reexportChain.size() != 0) {
      return zc::none;
    }
    const auto position = unordered.size();
    unordered.add(ExportSurfaceEntry(
        zc::mv(ZC_ASSERT_NONNULL(nameSpace)), BindingTarget::semanticImport(binding.clone()),
        fact.canonicalTarget.clone(),
        isImport ? VisibilityEnvelope::module(module) : VisibilityEnvelope::external(), !isImport,
        fact.declarationSpan.clone(),
        importsModule ? fact.declarationSpan.clone()
                      : sourceEntry->canonicalDeclarationSpan.clone(),
        zc::mv(aliasSpan), zc::mv(exportSpan), zc::mv(chain)));
    order.insert(zc::mv(orderKey), position);
  }

  zc::Vector<ExportSurfaceEntry> visible;
  zc::Vector<ExportSurfaceEntry> exports;
  for (const auto& ordered : order) {
    auto entry = zc::mv(unordered[ordered.value]);
    if (entry.exported) { exports.add(cloneEntry(entry)); }
    visible.add(zc::mv(entry));
  }
  auto encodedVisible = encodeEntries(identities, dependencies, moduleKey, module, visible.asPtr());
  auto encodedExports = encodeEntries(identities, dependencies, moduleKey, module, exports.asPtr());
  if (encodedVisible == zc::none || encodedExports == zc::none) { return zc::none; }
  const auto moduleBytes = moduleKey.encode();
  const auto unitBytes = compilationUnitKey.encode();
  auto revision = ExportSurfaceRevision::computeFramed(
      fingerprint.digest(), moduleBytes.asPtr(), unitBytes.asPtr(),
      ZC_ASSERT_NONNULL(encodedVisible).asPtr(), ZC_ASSERT_NONNULL(encodedExports).asPtr());
  if (revision == zc::none) { return zc::none; }
  return VerifiedExportSurface::fromVerified(module, compilationUnit, ZC_ASSERT_NONNULL(revision),
                                             zc::mv(visible), zc::mv(exports));
}

}  // namespace zomlang::compiler::binder
