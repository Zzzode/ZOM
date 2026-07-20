// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/binding-diagnostic-adapter.h"
#include "zomlang/compiler/binder/import-binding.h"
#include "zomlang/compiler/binder/internal/binding-candidate-codec.h"
#include "zomlang/compiler/binder/internal/binding-skeleton.h"
#include "zomlang/compiler/binder/internal/binding-verifier.h"
#include "zomlang/compiler/binder/internal/body-binding.h"
#include "zomlang/compiler/binder/internal/closure-free-variables.h"
#include "zomlang/compiler/binder/internal/control-transfer.h"
#include "zomlang/compiler/binder/internal/label-facts.h"
#include "zomlang/compiler/binder/internal/scope-arena.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

BinderInvariantFact failure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                            BinderEmitterSite site, uint32_t ordinal = 0) {
  return BinderInvariantFact{kind, input.module(), zc::none, site, ordinal};
}

BinderInvariantFact builderFailure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                                   uint32_t ordinal = 0) {
  return failure(input, kind, BinderEmitterSite::ModuleSkeleton, ordinal);
}

BinderInvariantFact bodyBuilderFailure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                                       uint32_t ordinal = 0) {
  return failure(input, kind, BinderEmitterSite::BodyBinding, ordinal);
}
enum class PendingFailureKind : uint8_t {
  Duplicate,
  ImportBinding,
  BodyLookup,
  LabelDuplicate,
  ControlTransfer
};

struct PendingFailureRef final {
  PendingFailureKind kind;
  size_t index;
};

struct PendingFailureOrderKey final {
  uint64_t start;
  uint64_t end;
  uint16_t diagnostic;
  uint8_t emitterSite;
  uint32_t schemaPreorderOrdinal;
  size_t sequence;

  bool operator==(const PendingFailureOrderKey& other) const noexcept {
    return start == other.start && end == other.end && diagnostic == other.diagnostic &&
           emitterSite == other.emitterSite &&
           schemaPreorderOrdinal == other.schemaPreorderOrdinal && sequence == other.sequence;
  }
  bool operator<(const PendingFailureOrderKey& other) const noexcept {
    if (start != other.start) { return start < other.start; }
    if (end != other.end) { return end < other.end; }
    if (diagnostic != other.diagnostic) { return diagnostic < other.diagnostic; }
    if (emitterSite != other.emitterSite) { return emitterSite < other.emitterSite; }
    if (schemaPreorderOrdinal != other.schemaPreorderOrdinal) {
      return schemaPreorderOrdinal < other.schemaPreorderOrdinal;
    }
    return sequence < other.sequence;
  }
};

struct SurfaceBindingOrderKey final {
  SurfaceBindingOrderKey(Namespace nameSpace, zc::String&& name) noexcept
      : nameSpace(nameSpace), name(zc::mv(name)) {}
  SurfaceBindingOrderKey(SurfaceBindingOrderKey&&) noexcept = default;
  SurfaceBindingOrderKey& operator=(SurfaceBindingOrderKey&&) noexcept = default;
  ZC_DISALLOW_COPY(SurfaceBindingOrderKey);

  bool operator==(const SurfaceBindingOrderKey& other) const noexcept {
    return nameSpace == other.nameSpace && name == other.name;
  }
  bool operator<(const SurfaceBindingOrderKey& other) const noexcept {
    if (nameSpace != other.nameSpace) {
      return static_cast<uint8_t>(nameSpace) < static_cast<uint8_t>(other.nameSpace);
    }
    return name < other.name;
  }

  Namespace nameSpace;
  zc::String name;
};
BindingTarget cloneTarget(const BindingTarget& target) { return target.clone(); }

VisibilityEnvelope cloneVisibility(const VisibilityEnvelope& visibility) {
  const auto& value = visibility.value();
  if (value.is<ModuleVisibility>()) {
    return VisibilityEnvelope::module(value.get<ModuleVisibility>().module);
  }
  return VisibilityEnvelope::external();
}

ExportSurfaceEntry cloneEntry(const ExportSurfaceEntry& entry) {
  zc::Maybe<identity::SourceSpan> aliasSpan;
  ZC_IF_SOME(value, entry.aliasSpan) { aliasSpan = value.clone(); }
  zc::Maybe<identity::SourceSpan> exportSpan;
  ZC_IF_SOME(value, entry.exportSpan) { exportSpan = value.clone(); }
  zc::Vector<ReexportProvenanceStep> chain;
  for (const auto& step : entry.reexportChain) {
    chain.add(ReexportProvenanceStep{step.module, cloneTarget(step.bindingIdentity),
                                     cloneTarget(step.canonicalTarget), step.exportSpan.clone()});
  }
  return ExportSurfaceEntry(
      entry.name.clone(), cloneTarget(entry.bindingIdentity), cloneTarget(entry.canonicalTarget),
      cloneVisibility(entry.visibility), entry.exported, entry.bindingSpan.clone(),
      entry.canonicalDeclarationSpan.clone(), zc::mv(aliasSpan), zc::mv(exportSpan), zc::mv(chain));
}

zc::Maybe<zc::Vector<uint32_t>> schemaPreorderOrdinals(const ast::Tree& tree) {
  zc::Vector<uint32_t> ordinals;
  ordinals.resize(tree.nodeCount() + 1);
  for (auto& value : ordinals) { value = UINT32_MAX; }
  uint32_t ordinal = 0;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (node.value >= ordinals.size() || ordinals[node.value] != UINT32_MAX) { return; }
    ordinals[node.value] = ordinal++;
  });
  if (ordinal != tree.nodeCount()) { return zc::none; }
  return zc::mv(ordinals);
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
  return rightValue.is<ModuleBindingTarget>() && leftValue.get<ModuleBindingTarget>().module ==
                                                     rightValue.get<ModuleBindingTarget>().module;
}

}  // namespace

BindingCandidateResult BindingBuilder::build(const VerifiedBindingInput& input,
                                             diagnostics::DiagnosticEngine& diagnostics) {
  return buildCandidate(input, diagnostics);
}

BindingCandidateResult BindingBuilder::buildCandidate(
    const VerifiedBindingInput& input, zc::Maybe<diagnostics::DiagnosticEngine&> diagnostics) {
  const auto& tree = input.tree();
  auto arenaResult = ScopeArenaBuilder::build(input);
  if (!arenaResult.is<ScopeArenaCandidate>()) {
    return zc::mv(arenaResult.get<BinderInvariantFact>());
  }
  auto arena = zc::mv(arenaResult.get<ScopeArenaCandidate>());
  if (arena.scopes.empty() || arena.scopes[0].kind != ScopeKind::Module) {
    return builderFailure(input, BinderInvariantKind::MalformedScopeGraph);
  }
  auto skeletonResult = BindingSkeletonBuilder::build(input, arena);
  if (!skeletonResult.is<DefinitionSkeletonCandidate>()) {
    return zc::mv(skeletonResult.get<BinderInvariantFact>());
  }
  auto skeleton = zc::mv(skeletonResult.get<DefinitionSkeletonCandidate>());

  ImportBindingProjectionInput projectionInput{
      input.module(), zc::Vector<ModuleScopeBindingProjection>(),
      zc::Vector<ResolvedModuleAliasProjection>(), zc::Vector<ResolvedImportBindingProjection>(),
      zc::Vector<LocalExportBindingProjection>()};
  for (const auto& entry : arena.scopes[0].bindings) {
    const auto& identityValue = entry.binding.bindingIdentity.value();
    if (entry.binding.origin != BindingOrigin::LocalDeclaration ||
        !identityValue.is<DefinitionBindingTarget>() ||
        !sameTarget(entry.binding.bindingIdentity, entry.binding.canonicalTarget)) {
      return builderFailure(input, BinderInvariantKind::InvalidBindingFact);
    }
    const auto identity = identityValue.get<DefinitionBindingTarget>().definition;
    ast::NodeId node;
    for (const auto& frozen : input.definitions().definitions()) {
      if (frozen.definition == identity) {
        node = frozen.node;
        break;
      }
    }
    if (!node) { return builderFailure(input, BinderInvariantKind::InvalidBindingFact); }
    zc::Maybe<identity::SourceSpan> aliasSpan;
    zc::Vector<ReexportProvenanceStep> chain;
    projectionInput.existingModuleBindings.add(ModuleScopeBindingProjection(
        node, ImportBindingNameProjection(entry.name.nameSpace(), entry.name.name().clone()),
        entry.binding.bindingIdentity.clone(), entry.binding.canonicalTarget.clone(),
        entry.binding.origin, entry.binding.declarationSpan.clone(), zc::mv(aliasSpan),
        entry.binding.declarationSpan.clone(), zc::mv(chain)));
  }
  for (const auto& alias : input.resolvedModuleAliases()) {
    projectionInput.moduleAliases.add(ResolvedModuleAliasProjection(
        alias.syntax(), alias.schemaPreorderOrdinal(), alias.alias(),
        ImportBindingNameProjection(alias.localName().nameSpace(),
                                    alias.localName().name().clone()),
        alias.targetModule(), alias.targetRevision(), alias.declarationSpan().clone(),
        alias.targetSpan().clone(), alias.exported()));
  }
  for (const auto& import : input.resolvedImports()) {
    zc::Maybe<identity::SourceSpan> aliasSpan;
    ZC_IF_SOME(value, import.aliasSpan()) { aliasSpan = value.clone(); }
    zc::Maybe<identity::SourceSpan> exportSpan;
    ZC_IF_SOME(value, import.exportSpan()) { exportSpan = value.clone(); }
    zc::Vector<ReexportProvenanceStep> chain;
    for (const auto& step : import.sourceReexportChain()) { chain.add(step.clone()); }
    projectionInput.imports.add(ResolvedImportBindingProjection(
        import.syntax(), import.schemaPreorderOrdinal(), import.binding().clone(),
        ImportBindingNameProjection(import.localName().nameSpace(),
                                    import.localName().name().clone()),
        import.canonicalTarget().clone(), import.sourceModule(), import.sourceRevision(),
        import.kind(), import.declarationSpan().clone(), zc::mv(aliasSpan),
        import.canonicalDeclarationSpan().clone(), zc::mv(exportSpan), zc::mv(chain)));
  }
  for (const auto& localExport : input.localExportSpecifiers()) {
    zc::Maybe<identity::SourceSpan> aliasSpan;
    ZC_IF_SOME(value, localExport.aliasSpan()) { aliasSpan = value.clone(); }
    projectionInput.localExports.add(LocalExportBindingProjection(
        localExport.syntax(), localExport.schemaPreorderOrdinal(), localExport.sourceName().clone(),
        localExport.exportedName().clone(), localExport.sourceNameSpan().clone(),
        localExport.declarationSpan().clone(), zc::mv(aliasSpan),
        localExport.exportSpan().clone()));
  }
  auto projectionResult = ImportBindingProjector::project(zc::mv(projectionInput));
  if (!projectionResult.is<ImportBindingProjectionCandidate>()) {
    return zc::mv(projectionResult.get<BinderInvariantFact>());
  }
  auto projection = zc::mv(projectionResult.get<ImportBindingProjectionCandidate>());

  const auto projectedNamespace = [&](const FrozenDefinitionEntry& frozen) -> zc::Maybe<Namespace> {
    for (const auto& binding : projection.moduleScopeBindings) {
      const auto& identityValue = binding.binding.bindingIdentity.value();
      if (identityValue.is<DefinitionBindingTarget>() &&
          identityValue.get<DefinitionBindingTarget>().definition == frozen.definition) {
        return binding.name.nameSpace();
      }
    }
    for (const auto& alias : input.resolvedModuleAliases()) {
      if (alias.alias() == frozen.definition) { return Namespace::Module; }
    }
    return zc::none;
  };
  zc::TreeMap<zc::String, size_t> definitionOrder;
  const auto frozenDefinitions = input.definitions().definitions();
  for (size_t index = 0; index < frozenDefinitions.size(); ++index) {
    const auto bytes = frozenDefinitions[index].key.encode();
    definitionOrder.insert(zc::str(bytes.asChars()), index);
  }
  zc::Vector<DefinitionFact> completeDefinitions;
  for (const auto& ordered : definitionOrder) {
    const auto& frozen = frozenDefinitions[ordered.value];
    size_t existing = skeleton.definitions.size();
    for (size_t index = 0; index < skeleton.definitions.size(); ++index) {
      if (skeleton.definitions[index].identity == frozen.definition) {
        existing = index;
        break;
      }
    }
    if (existing != skeleton.definitions.size()) {
      completeDefinitions.add(zc::mv(skeleton.definitions[existing]));
      continue;
    }
    const auto kind = frozen.record.kind();
    if (kind != identity::DefinitionKind::ModuleAlias || frozen.bindingName == zc::none) {
      return builderFailure(input, BinderInvariantKind::InvalidBindingFact, frozen.node.value);
    }
    const auto nameSpace = projectedNamespace(frozen);
    if (nameSpace == zc::none) {
      return builderFailure(input, BinderInvariantKind::MissingRequiredResolution,
                            frozen.node.value);
    }
    zc::Maybe<MemberVisibility> noMemberVisibility;
    ZC_IF_SOME(namespaceValue, nameSpace) {
      completeDefinitions.add(DefinitionFact(
          frozen.definition, frozen.site.clone(), kind,
          ZC_ASSERT_NONNULL(frozen.bindingName).clone(), namespaceValue, arena.scopes[0].id,
          frozen.source.clone(), DefinitionActivation::ImportSurface, zc::mv(noMemberVisibility)));
    }
  }
  skeleton.definitions = zc::mv(completeDefinitions);
  for (auto& binding : projection.moduleScopeBindings) {
    zc::Maybe<identity::SourceSpan> aliasSpan;
    ZC_IF_SOME(value, binding.binding.aliasSpan) { aliasSpan = value.clone(); }
    arena.scopes[0].bindings.add(
        ScopeBindingEntry(BindingNameKey(binding.name.nameSpace(), binding.name.name().clone()),
                          NameBinding(binding.binding.bindingIdentity.clone(),
                                      binding.binding.canonicalTarget.clone(),
                                      binding.binding.nameSpace, binding.binding.origin,
                                      binding.binding.declarationSpan.clone(), zc::mv(aliasSpan))));
  }
  ZC_IF_SOME(prelude, input.preludeSurface()) {
    for (const auto& entry : prelude.visibleEntries()) {
      bool shadowed = false;
      for (const auto& existing : arena.scopes[0].bindings) {
        if (existing.name.nameSpace() == entry.name.nameSpace() &&
            existing.name.name() == entry.name.name()) {
          shadowed = true;
          break;
        }
      }
      if (shadowed) { continue; }

      zc::Maybe<identity::SourceSpan> noAlias;
      arena.scopes[0].bindings.add(ScopeBindingEntry(
          BindingNameKey(entry.name.nameSpace(), entry.name.name().clone()),
          NameBinding(entry.bindingIdentity.clone(), entry.canonicalTarget.clone(),
                      entry.name.nameSpace(), BindingOrigin::Prelude,
                      input.parsedModule().rootSpan().clone(), zc::mv(noAlias))));

      zc::Maybe<identity::SourceSpan> noSurfaceAlias;
      zc::Maybe<identity::SourceSpan> noExportSpan;
      zc::Vector<ReexportProvenanceStep> chain;
      for (const auto& step : entry.reexportChain) { chain.add(step.clone()); }
      projection.surfaceSeeds.add(ImportSurfaceSeed(
          ImportBindingNameProjection(entry.name.nameSpace(), entry.name.name().clone()),
          entry.bindingIdentity.clone(), entry.canonicalTarget.clone(),
          VisibilityEnvelope::module(input.module()), false,
          input.parsedModule().rootSpan().clone(), entry.canonicalDeclarationSpan.clone(),
          zc::mv(noSurfaceAlias), zc::mv(noExportSpan), zc::mv(chain)));
    }
  }

  zc::TreeMap<SurfaceBindingOrderKey, size_t> moduleBindingOrder;
  for (size_t index = 0; index < arena.scopes[0].bindings.size(); ++index) {
    const auto& entry = arena.scopes[0].bindings[index];
    auto key = SurfaceBindingOrderKey(entry.name.nameSpace(), zc::str(entry.name.name().text()));
    if (moduleBindingOrder.find(key) != zc::none) {
      return builderFailure(input, BinderInvariantKind::InvalidBindingFact);
    }
    moduleBindingOrder.insert(zc::mv(key), index);
  }
  zc::Vector<ScopeBindingEntry> orderedModuleBindings(arena.scopes[0].bindings.size());
  for (const auto& ordered : moduleBindingOrder) {
    orderedModuleBindings.add(zc::mv(arena.scopes[0].bindings[ordered.value]));
  }
  arena.scopes[0].bindings = zc::mv(orderedModuleBindings);
  auto bodyResult = BodyBindingBuilder::build(input, arena, skeleton);
  if (!bodyResult.is<BodyBindingCandidate>()) {
    return zc::mv(bodyResult.get<BinderInvariantFact>());
  }
  auto body = zc::mv(bodyResult.get<BodyBindingCandidate>());
  auto labelResult = LabelBuilder::build(input, arena);
  if (!labelResult.is<LabelFactsCandidate>()) {
    return zc::mv(labelResult.get<BinderInvariantFact>());
  }
  auto labels = zc::mv(labelResult.get<LabelFactsCandidate>());
  auto controlResult = ControlTransferBuilder::build(input, arena, labels);
  if (!controlResult.is<ControlTransferCandidate>()) {
    return zc::mv(controlResult.get<BinderInvariantFact>());
  }
  auto control = zc::mv(controlResult.get<ControlTransferCandidate>());
  for (auto& binding : control.nodeBindings) { body.nodeBindings.add(zc::mv(binding)); }

  auto schemaOrdinalsResult = schemaPreorderOrdinals(tree);
  if (schemaOrdinalsResult == zc::none) {
    return bodyBuilderFailure(input, BinderInvariantKind::InvalidEmitterOrdinal);
  }
  auto schemaOrdinals = zc::mv(ZC_ASSERT_NONNULL(schemaOrdinalsResult));
  zc::TreeMap<PendingFailureOrderKey, PendingFailureRef> failureOrder;
  size_t sequence = 0;
  for (size_t index = 0; index < skeleton.duplicates.size(); ++index) {
    const auto& duplicate = skeleton.duplicates[index];
    if (!tree.contains(duplicate.primaryNode) ||
        schemaOrdinals[duplicate.primaryNode.value] == UINT32_MAX) {
      return builderFailure(input, BinderInvariantKind::InvalidEmitterOrdinal);
    }
    const auto ordinal = schemaOrdinals[duplicate.primaryNode.value];
    failureOrder.insert(
        PendingFailureOrderKey{duplicate.primary.byteStart(), duplicate.primary.byteEnd(),
                               static_cast<uint16_t>(duplicate.diagnostic),
                               static_cast<uint8_t>(duplicate.emitterSite), ordinal, sequence++},
        PendingFailureRef{PendingFailureKind::Duplicate, index});
  }
  for (size_t index = 0; index < projection.sourceFailures.size(); ++index) {
    const auto& importFailure = projection.sourceFailures[index];
    if (!tree.contains(importFailure.node) ||
        schemaOrdinals[importFailure.node.value] == UINT32_MAX) {
      return builderFailure(input, BinderInvariantKind::InvalidEmitterOrdinal);
    }
    const auto ordinal = schemaOrdinals[importFailure.node.value];
    failureOrder.insert(
        PendingFailureOrderKey{
            importFailure.failure.primary.byteStart(), importFailure.failure.primary.byteEnd(),
            static_cast<uint16_t>(importFailure.failure.diagnostic),
            static_cast<uint8_t>(BinderEmitterSite::ImportBinding), ordinal, sequence++},
        PendingFailureRef{PendingFailureKind::ImportBinding, index});
  }
  for (size_t index = 0; index < body.failures.size(); ++index) {
    const auto& bodyFailure = body.failures[index];
    failureOrder.insert(
        PendingFailureOrderKey{bodyFailure.source.byteStart(), bodyFailure.source.byteEnd(),
                               static_cast<uint16_t>(bodyFailure.diagnostic),
                               static_cast<uint8_t>(bodyFailure.emitterSite),
                               bodyFailure.schemaPreorderOrdinal, sequence++},
        PendingFailureRef{PendingFailureKind::BodyLookup, index});
  }
  for (size_t index = 0; index < labels.duplicates.size(); ++index) {
    const auto& duplicate = labels.duplicates[index];
    failureOrder.insert(
        PendingFailureOrderKey{duplicate.primary.byteStart(), duplicate.primary.byteEnd(),
                               static_cast<uint16_t>(BinderDiagnosticCode::DuplicateIdentifier),
                               static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure),
                               duplicate.schemaPreorderOrdinal, sequence++},
        PendingFailureRef{PendingFailureKind::LabelDuplicate, index});
  }
  for (size_t index = 0; index < control.failures.size(); ++index) {
    const auto& controlFailure = control.failures[index];
    failureOrder.insert(
        PendingFailureOrderKey{controlFailure.source.byteStart(), controlFailure.source.byteEnd(),
                               static_cast<uint16_t>(controlFailure.diagnostic),
                               static_cast<uint8_t>(controlFailure.emitterSite),
                               controlFailure.schemaPreorderOrdinal, sequence++},
        PendingFailureRef{PendingFailureKind::ControlTransfer, index});
  }

  zc::Vector<BindingFailureRef> sourceFailures;
  uint8_t previousSite = 0;
  uint32_t previousOrdinal = 0;
  uint16_t localOrdinal = 0;
  bool hasPrevious = false;
  for (const auto& ordered : failureOrder) {
    const auto site = ordered.key.emitterSite;
    const auto schemaOrdinal = ordered.key.schemaPreorderOrdinal;
    if (hasPrevious && site == previousSite && schemaOrdinal == previousOrdinal) {
      if (localOrdinal == UINT16_MAX) {
        return builderFailure(input, BinderInvariantKind::InvalidEmitterOrdinal, schemaOrdinal);
      }
      ++localOrdinal;
    } else {
      localOrdinal = 0;
    }
    hasPrevious = true;
    previousSite = site;
    previousOrdinal = schemaOrdinal;
    const uint64_t emitterOrdinal =
        (uint64_t(site) << 56) | (uint64_t(schemaOrdinal) << 16) | localOrdinal;

    if (ordered.value.kind == PendingFailureKind::Duplicate) {
      const auto& duplicate = skeleton.duplicates[ordered.value.index];
      auto primaryLocation = input.parsedModule().sourceLocFor(duplicate.primary);
      auto previousLocation = input.parsedModule().sourceLocFor(duplicate.previous);
      if (primaryLocation == zc::none || previousLocation == zc::none) {
        return failure(input, BinderInvariantKind::InvalidBindingFact, duplicate.emitterSite,
                       schemaOrdinal);
      }
      ZC_IF_SOME(engine, diagnostics) {
        bool emitted = false;
        ZC_IF_SOME(primary, primaryLocation) {
          ZC_IF_SOME(previous, previousLocation) {
            emitted = BindingDiagnosticAdapter::emitRedeclaration(
                engine, duplicate.diagnostic, primary, previous,
                VerifiedIdentifierArgument::from(duplicate.name));
          }
        }
        if (!emitted) {
          return failure(input, BinderInvariantKind::InvalidBindingFact, duplicate.emitterSite,
                         schemaOrdinal);
        }
      }
      zc::Vector<BindingDiagnosticNoteRef> notes;
      notes.add(BindingDiagnosticNoteRef{BinderDiagnosticCode::PreviousDeclarationHere,
                                         duplicate.previous.clone()});
      sourceFailures.add(BindingFailureRef{duplicate.diagnostic, duplicate.primary.clone(),
                                           emitterOrdinal, zc::mv(notes)});
      continue;
    }

    if (ordered.value.kind == PendingFailureKind::ImportBinding) {
      const auto& importFailure = projection.sourceFailures[ordered.value.index];
      auto primaryLocation = input.parsedModule().sourceLocFor(importFailure.failure.primary);
      if (primaryLocation == zc::none) {
        return failure(input, BinderInvariantKind::InvalidBindingFact,
                       BinderEmitterSite::ImportBinding, schemaOrdinal);
      }
      ZC_IF_SOME(engine, diagnostics) {
        bool emitted = false;
        ZC_IF_SOME(primary, primaryLocation) {
          if (importFailure.failure.diagnostic == BinderDiagnosticCode::DuplicateIdentifier &&
              importFailure.failure.notes.size() == 1) {
            auto previousLocation =
                input.parsedModule().sourceLocFor(importFailure.failure.notes[0].source);
            ZC_IF_SOME(previous, previousLocation) {
              emitted = BindingDiagnosticAdapter::emitRedeclaration(
                  engine, importFailure.failure.diagnostic, primary, previous,
                  VerifiedIdentifierArgument::from(importFailure.name));
            }
          } else if (importFailure.failure.diagnostic ==
                         BinderDiagnosticCode::UndefinedIdentifier &&
                     importFailure.failure.notes.empty()) {
            emitted = BindingDiagnosticAdapter::emitLookupFailure(
                engine, importFailure.failure.diagnostic, primary,
                VerifiedIdentifierArgument::from(importFailure.name), Namespace::Value);
          }
        }
        if (!emitted) {
          return failure(input, BinderInvariantKind::InvalidBindingFact,
                         BinderEmitterSite::ImportBinding, schemaOrdinal);
        }
      }
      zc::Vector<BindingDiagnosticNoteRef> notes;
      for (const auto& note : importFailure.failure.notes) {
        notes.add(BindingDiagnosticNoteRef{note.diagnostic, note.source.clone()});
      }
      sourceFailures.add(BindingFailureRef{importFailure.failure.diagnostic,
                                           importFailure.failure.primary.clone(), emitterOrdinal,
                                           zc::mv(notes)});
      continue;
    }

    if (ordered.value.kind == PendingFailureKind::LabelDuplicate) {
      const auto& duplicate = labels.duplicates[ordered.value.index];
      auto primaryLocation = input.parsedModule().sourceLocFor(duplicate.primary);
      auto previousLocation = input.parsedModule().sourceLocFor(duplicate.previous);
      if (primaryLocation == zc::none || previousLocation == zc::none) {
        return failure(input, BinderInvariantKind::InvalidBindingFact,
                       BinderEmitterSite::LabelAndClosure, schemaOrdinal);
      }
      ZC_IF_SOME(engine, diagnostics) {
        ZC_IF_SOME(primary, primaryLocation) {
          ZC_IF_SOME(previous, previousLocation) {
            if (!BindingDiagnosticAdapter::emitRedeclaration(
                    engine, BinderDiagnosticCode::DuplicateIdentifier, primary, previous,
                    VerifiedIdentifierArgument::from(duplicate.name))) {
              return failure(input, BinderInvariantKind::InvalidBindingFact,
                             BinderEmitterSite::LabelAndClosure, schemaOrdinal);
            }
          }
        }
      }
      zc::Vector<BindingDiagnosticNoteRef> notes;
      notes.add(BindingDiagnosticNoteRef{BinderDiagnosticCode::PreviousDeclarationHere,
                                         duplicate.previous.clone()});
      sourceFailures.add(BindingFailureRef{BinderDiagnosticCode::DuplicateIdentifier,
                                           duplicate.primary.clone(), emitterOrdinal,
                                           zc::mv(notes)});
      continue;
    }

    if (ordered.value.kind == PendingFailureKind::ControlTransfer) {
      const auto& controlFailure = control.failures[ordered.value.index];
      auto primaryLocation = input.parsedModule().sourceLocFor(controlFailure.source);
      if (primaryLocation == zc::none) {
        return failure(input, BinderInvariantKind::InvalidBindingFact, controlFailure.emitterSite,
                       schemaOrdinal);
      }
      ZC_IF_SOME(engine, diagnostics) {
        bool emitted = false;
        ZC_IF_SOME(primary, primaryLocation) {
          ZC_IF_SOME(label, controlFailure.label) {
            emitted = BindingDiagnosticAdapter::emitLabelLookupFailure(
                engine, controlFailure.diagnostic, primary,
                VerifiedIdentifierArgument::from(label));
          } else {
            emitted = BindingDiagnosticAdapter::emitControlTransferFailure(
                engine, controlFailure.diagnostic, primary);
          }
        }
        if (!emitted) {
          return failure(input, BinderInvariantKind::InvalidBindingFact, controlFailure.emitterSite,
                         schemaOrdinal);
        }
      }
      zc::Vector<BindingDiagnosticNoteRef> noNotes;
      const size_t failureIndex = sourceFailures.size();
      sourceFailures.add(BindingFailureRef{controlFailure.diagnostic, controlFailure.source.clone(),
                                           emitterOrdinal, zc::mv(noNotes)});
      body.nodeBindings.add(BindingResolution{
          controlFailure.node, BindingResolutionValue(FailedBindingResolution{failureIndex})});
      continue;
    }

    const auto& bodyFailure = body.failures[ordered.value.index];
    auto primaryLocation = input.parsedModule().sourceLocFor(bodyFailure.source);
    if (primaryLocation == zc::none) {
      return failure(input, BinderInvariantKind::InvalidBindingFact, bodyFailure.emitterSite,
                     schemaOrdinal);
    }
    ZC_IF_SOME(engine, diagnostics) {
      bool emitted = false;
      ZC_IF_SOME(primary, primaryLocation) {
        emitted = BindingDiagnosticAdapter::emitLookupFailure(
            engine, bodyFailure.diagnostic, primary,
            VerifiedIdentifierArgument::from(bodyFailure.name), bodyFailure.expectedNamespace);
      }
      if (!emitted) {
        return failure(input, BinderInvariantKind::InvalidBindingFact, bodyFailure.emitterSite,
                       schemaOrdinal);
      }
    }
    zc::Vector<BindingDiagnosticNoteRef> noNotes;
    const size_t failureIndex = sourceFailures.size();
    sourceFailures.add(BindingFailureRef{bodyFailure.diagnostic, bodyFailure.source.clone(),
                                         emitterOrdinal, zc::mv(noNotes)});
    body.nodeBindings.add(BindingResolution{
        bodyFailure.node, BindingResolutionValue(FailedBindingResolution{failureIndex})});
  }

  zc::TreeMap<uint32_t, size_t> bindingOrder;
  for (size_t index = 0; index < body.nodeBindings.size(); ++index) {
    const auto node = body.nodeBindings[index].node;
    if (!tree.contains(node) || bindingOrder.find(node.value) != zc::none) {
      const uint32_t ordinal =
          tree.contains(node) ? schemaOrdinals[node.value] : static_cast<uint32_t>(0);
      return bodyBuilderFailure(input, BinderInvariantKind::InvalidBindingFact, ordinal);
    }
    bindingOrder.insert(node.value, index);
  }
  zc::Vector<BindingResolution> nodeBindings;
  for (const auto& ordered : bindingOrder) {
    nodeBindings.add(zc::mv(body.nodeBindings[ordered.value]));
  }

  auto closureResult = ClosureFreeVariableBuilder::build(
      input, arena, skeleton.definitions.asPtr(), skeleton.callableParameters.asPtr(),
      skeleton.ownerLocalBindings.asPtr(), nodeBindings.asPtr(), body.thisBindings.asPtr());
  if (!closureResult.is<zc::Vector<ClosureFreeVariableFact>>()) {
    return zc::mv(closureResult.get<BinderInvariantFact>());
  }
  auto closureFreeVariables = zc::mv(closureResult.get<zc::Vector<ClosureFreeVariableFact>>());

  zc::Vector<ExportSurfaceEntry> unorderedEntries;
  zc::TreeMap<SurfaceBindingOrderKey, size_t> surfaceOrder;
  for (auto& seed : skeleton.moduleSurfaceSeeds) {
    zc::Maybe<identity::SourceSpan> noSurfaceAlias;
    zc::Vector<ReexportProvenanceStep> noChain;
    auto entry = ExportSurfaceEntry(
        zc::mv(seed.name), BindingTarget::definition(seed.identity),
        BindingTarget::definition(seed.identity),
        seed.exported ? VisibilityEnvelope::external() : VisibilityEnvelope::module(input.module()),
        seed.exported, seed.source.clone(), seed.source.clone(), zc::mv(noSurfaceAlias),
        zc::mv(seed.exportSpan), zc::mv(noChain));
    const size_t index = unorderedEntries.size();
    auto key = SurfaceBindingOrderKey(entry.name.nameSpace(), zc::str(entry.name.name().text()));
    if (surfaceOrder.find(key) != zc::none) {
      return builderFailure(input, BinderInvariantKind::InvalidBindingFact);
    }
    surfaceOrder.insert(zc::mv(key), index);
    unorderedEntries.add(zc::mv(entry));
  }
  for (auto& seed : projection.surfaceSeeds) {
    zc::Maybe<identity::SourceSpan> aliasSpan;
    ZC_IF_SOME(value, seed.aliasSpan) { aliasSpan = value.clone(); }
    zc::Maybe<identity::SourceSpan> exportSpan;
    ZC_IF_SOME(value, seed.exportSpan) { exportSpan = value.clone(); }
    zc::Vector<ReexportProvenanceStep> chain;
    for (const auto& step : seed.reexportChain) { chain.add(step.clone()); }
    auto entry = ExportSurfaceEntry(BindingNameKey(seed.name.nameSpace(), seed.name.name().clone()),
                                    seed.bindingIdentity.clone(), seed.canonicalTarget.clone(),
                                    seed.visibility.clone(), seed.exported,
                                    seed.bindingSpan.clone(), seed.canonicalDeclarationSpan.clone(),
                                    zc::mv(aliasSpan), zc::mv(exportSpan), zc::mv(chain));
    const size_t index = unorderedEntries.size();
    auto key = SurfaceBindingOrderKey(entry.name.nameSpace(), zc::str(entry.name.name().text()));
    if (surfaceOrder.find(key) != zc::none) {
      return builderFailure(input, BinderInvariantKind::InvalidBindingFact);
    }
    surfaceOrder.insert(zc::mv(key), index);
    unorderedEntries.add(zc::mv(entry));
  }
  zc::Vector<ExportSurfaceEntry> visibleEntries;
  zc::Vector<ExportSurfaceEntry> exports;
  for (const auto& ordered : surfaceOrder) {
    auto& entry = unorderedEntries[ordered.value];
    if (entry.exported) { exports.add(cloneEntry(entry)); }
    visibleEntries.add(zc::mv(entry));
  }
  auto encodedVisible = encodeBindingSurfaceMap(input, visibleEntries.asPtr());
  auto encodedExports = encodeBindingSurfaceMap(input, exports.asPtr());
  if (encodedVisible == zc::none || encodedExports == zc::none) {
    return builderFailure(input, BinderInvariantKind::InvalidBindingFact);
  }
  ZC_IF_SOME(visible, encodedVisible) {
    ZC_IF_SOME(external, encodedExports) {
      const auto moduleBytes = input.moduleKey().encode();
      const auto packageBytes = input.packageKey().encode();
      auto revision = ExportSurfaceRevision::computeFramed(
          input.semanticFingerprint().digest(), moduleBytes.asPtr(), packageBytes.asPtr(),
          visible.asPtr(), external.asPtr());
      ZC_IF_SOME(revisionValue, revision) {
        ExportSurfaceCandidate surface(input.module(), input.package(), revisionValue,
                                       zc::mv(visibleEntries), zc::mv(exports));
        BindingMetadataCandidate candidate(input.semanticContext(), input.module(),
                                           zc::mv(arena.nodeScopes), zc::mv(skeleton.definitions),
                                           zc::mv(skeleton.impls), zc::mv(arena.scopes),
                                           zc::mv(surface));
        candidate.genericParameters = zc::mv(skeleton.genericParameters);
        candidate.callableParameters = zc::mv(skeleton.callableParameters);
        candidate.ownerLocalBindings = zc::mv(skeleton.ownerLocalBindings);
        candidate.sourceFailures = zc::mv(sourceFailures);
        candidate.nodeBindings = zc::mv(nodeBindings);
        candidate.selfTypes = zc::mv(body.selfTypes);
        candidate.thisBindings = zc::mv(body.thisBindings);
        candidate.deferredMembers = zc::mv(body.deferredMembers);
        candidate.labels = zc::mv(labels.labels);
        candidate.controlTransfers = zc::mv(control.controlTransfers);
        candidate.shadowTargets = zc::mv(body.shadowTargets);
        candidate.closureFreeVariables = zc::mv(closureFreeVariables);
        candidate.explicitClosureCaptures = zc::mv(body.explicitClosureCaptures);
        candidate.moduleAliases = zc::mv(projection.moduleAliases);
        candidate.imports = zc::mv(projection.imports);
        candidate.localExports = zc::mv(projection.localExports);
        return candidate;
      }
    }
  }
  return builderFailure(input, BinderInvariantKind::InvalidBindingFact);
}

}  // namespace zomlang::compiler::binder
