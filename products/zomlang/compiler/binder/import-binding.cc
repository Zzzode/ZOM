// Copyright (c) 2026 Zode.Z. All rights reserved
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

#include "zomlang/compiler/binder/import-binding.h"

#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

BindingTarget cloneTarget(const BindingTarget& target) { return target.clone(); }

zc::Maybe<identity::SourceSpan> cloneSpan(const zc::Maybe<identity::SourceSpan>& span) {
  ZC_IF_SOME(value, span) { return value.clone(); }
  return zc::none;
}

zc::Vector<ReexportProvenanceStep> cloneChain(zc::ArrayPtr<const ReexportProvenanceStep> chain) {
  zc::Vector<ReexportProvenanceStep> result(chain.size());
  for (const auto& step : chain) {
    result.add(ReexportProvenanceStep{step.module, cloneTarget(step.bindingIdentity),
                                      cloneTarget(step.canonicalTarget), step.exportSpan.clone()});
  }
  return result;
}

identity::DeclaredDefinitionName bindingName(const identity::SemanticIdentifier& name) {
  auto result = identity::DeclaredDefinitionName::fromCanonical(name.text());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

zc::String bindingKey(const ImportBindingNameProjection& name) {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(name.nameSpace()));
  name.name().encode(encoder);
  return zc::encodeHex(encoder.finish().asPtr());
}

BinderInvariantFact invariant(identity::ModuleId module, uint32_t ordinal = 0) {
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  return BinderInvariantFact{BinderInvariantKind::InvalidBindingFact, module, zc::mv(noRange),
                             BinderEmitterSite::ImportBinding, ordinal};
}

uint64_t emitterOrdinal(uint32_t schemaPreorderOrdinal) {
  return (uint64_t(static_cast<uint8_t>(BinderEmitterSite::ImportBinding)) << 56) |
         (uint64_t(schemaPreorderOrdinal) << 16);
}

enum class ProjectionOperationKind : uint8_t { ModuleAlias, Import, LocalExport };

struct ProjectionOperation final {
  ProjectionOperationKind kind;
  size_t index;
  uint32_t schemaPreorderOrdinal;
  ast::NodeId node;
};

bool isKnownImportKind(ImportBindingKind kind) {
  switch (kind) {
    case ImportBindingKind::Import:
    case ImportBindingKind::ForeignReexport:
      return true;
  }
  return false;
}

bool isKnownNamespace(Namespace nameSpace) {
  switch (nameSpace) {
    case Namespace::Value:
    case Namespace::Type:
    case Namespace::Module:
    case Namespace::Label:
    case Namespace::Attribute:
      return true;
  }
  return false;
}

}  // namespace

ImportBindingNameProjection::ImportBindingNameProjection(
    Namespace nameSpace, identity::DeclaredDefinitionName&& name) noexcept
    : namespaceValue(nameSpace), nameValue(zc::mv(name)) {}

ImportBindingNameProjection ImportBindingNameProjection::clone() const {
  return ImportBindingNameProjection(namespaceValue, nameValue.clone());
}

Namespace ImportBindingNameProjection::nameSpace() const noexcept { return namespaceValue; }

const identity::DeclaredDefinitionName& ImportBindingNameProjection::name() const noexcept {
  return nameValue;
}

ModuleScopeBindingProjection::ModuleScopeBindingProjection(
    ast::NodeId node, ImportBindingNameProjection&& name, BindingTarget&& bindingIdentity,
    BindingTarget&& canonicalTarget, BindingOrigin origin, identity::SourceSpan&& declarationSpan,
    zc::Maybe<identity::SourceSpan>&& aliasSpan, identity::SourceSpan&& canonicalDeclarationSpan,
    zc::Vector<ReexportProvenanceStep>&& reexportChain) noexcept
    : node(node),
      name(zc::mv(name)),
      binding(zc::mv(bindingIdentity), zc::mv(canonicalTarget), this->name.nameSpace(), origin,
              zc::mv(declarationSpan), zc::mv(aliasSpan)),
      canonicalDeclarationSpan(zc::mv(canonicalDeclarationSpan)),
      reexportChain(zc::mv(reexportChain)) {}

ModuleScopeBindingProjection ModuleScopeBindingProjection::clone() const {
  return ModuleScopeBindingProjection(node, name.clone(), cloneTarget(binding.bindingIdentity),
                                      cloneTarget(binding.canonicalTarget), binding.origin,
                                      binding.declarationSpan.clone(), cloneSpan(binding.aliasSpan),
                                      canonicalDeclarationSpan.clone(),
                                      cloneChain(reexportChain.asPtr()));
}

ImportSurfaceSeed::ImportSurfaceSeed(
    ImportBindingNameProjection&& name, BindingTarget&& bindingIdentity,
    BindingTarget&& canonicalTarget, VisibilityEnvelope&& visibility, bool exported,
    identity::SourceSpan&& bindingSpan, identity::SourceSpan&& canonicalDeclarationSpan,
    zc::Maybe<identity::SourceSpan>&& aliasSpan, zc::Maybe<identity::SourceSpan>&& exportSpan,
    zc::Vector<ReexportProvenanceStep>&& reexportChain) noexcept
    : name(zc::mv(name)),
      bindingIdentity(zc::mv(bindingIdentity)),
      canonicalTarget(zc::mv(canonicalTarget)),
      visibility(zc::mv(visibility)),
      exported(exported),
      bindingSpan(zc::mv(bindingSpan)),
      canonicalDeclarationSpan(zc::mv(canonicalDeclarationSpan)),
      aliasSpan(zc::mv(aliasSpan)),
      exportSpan(zc::mv(exportSpan)),
      reexportChain(zc::mv(reexportChain)) {}

ResolvedModuleAliasProjection::ResolvedModuleAliasProjection(
    ast::NodeId node, uint32_t schemaPreorderOrdinal, identity::DefId alias,
    ImportBindingNameProjection&& localName, identity::ModuleId target,
    ModuleAliasExportNamesRevision targetExportNamesRevision,
    identity::SourceSpan&& declarationSpan, identity::SourceSpan&& targetSpan,
    bool exported) noexcept
    : node(node),
      schemaPreorderOrdinal(schemaPreorderOrdinal),
      alias(alias),
      localName(zc::mv(localName)),
      target(target),
      targetExportNamesRevision(targetExportNamesRevision),
      declarationSpan(zc::mv(declarationSpan)),
      targetSpan(zc::mv(targetSpan)),
      exported(exported) {}

ResolvedImportBindingProjection::ResolvedImportBindingProjection(
    ast::NodeId node, uint32_t schemaPreorderOrdinal, identity::SemanticImportBindingKey&& binding,
    ImportBindingNameProjection&& localName, BindingTarget&& canonicalTarget,
    identity::ModuleId sourceModule, ExportSurfaceRevision sourceRevision, ImportBindingKind kind,
    identity::SourceSpan&& declarationSpan, zc::Maybe<identity::SourceSpan>&& aliasSpan,
    identity::SourceSpan&& canonicalDeclarationSpan, zc::Maybe<identity::SourceSpan>&& exportSpan,
    zc::Vector<ReexportProvenanceStep>&& sourceReexportChain) noexcept
    : node(node),
      schemaPreorderOrdinal(schemaPreorderOrdinal),
      binding(zc::mv(binding)),
      localName(zc::mv(localName)),
      canonicalTarget(zc::mv(canonicalTarget)),
      sourceModule(sourceModule),
      sourceRevision(sourceRevision),
      kind(kind),
      declarationSpan(zc::mv(declarationSpan)),
      aliasSpan(zc::mv(aliasSpan)),
      canonicalDeclarationSpan(zc::mv(canonicalDeclarationSpan)),
      exportSpan(zc::mv(exportSpan)),
      sourceReexportChain(zc::mv(sourceReexportChain)) {}

LocalExportBindingProjection::LocalExportBindingProjection(
    ast::NodeId node, uint32_t schemaPreorderOrdinal, identity::SemanticIdentifier&& sourceName,
    identity::SemanticIdentifier&& exportedName, identity::SourceSpan&& sourceNameSpan,
    identity::SourceSpan&& declarationSpan, zc::Maybe<identity::SourceSpan>&& aliasSpan,
    identity::SourceSpan&& exportSpan) noexcept
    : node(node),
      schemaPreorderOrdinal(schemaPreorderOrdinal),
      sourceName(zc::mv(sourceName)),
      exportedName(zc::mv(exportedName)),
      sourceNameSpan(zc::mv(sourceNameSpan)),
      declarationSpan(zc::mv(declarationSpan)),
      aliasSpan(zc::mv(aliasSpan)),
      exportSpan(zc::mv(exportSpan)) {}

ImportBindingFailureProjection::ImportBindingFailureProjection(
    ast::NodeId node, identity::DeclaredDefinitionName&& name, BindingFailureRef&& failure) noexcept
    : node(node), name(zc::mv(name)), failure(zc::mv(failure)) {}

ImportBindingProjectionResult ImportBindingProjector::project(
    ImportBindingProjectionInput&& input) {
  zc::TreeMap<uint64_t, uint8_t> syntaxSlots;
  zc::TreeMap<uint64_t, uint8_t> schemaSlots;
  zc::TreeMap<uint32_t, uint32_t> syntaxOrdinals;
  zc::TreeMap<uint32_t, uint32_t> ordinalSyntax;
  zc::TreeMap<zc::String, ProjectionOperation> operations;

  const auto registerOperation = [&](ProjectionOperationKind kind, size_t index, uint32_t ordinal,
                                     ast::NodeId node, uint8_t slot) -> bool {
    const uint64_t syntaxSlot = (uint64_t(node.value) << 8) | slot;
    const uint64_t schemaSlot = (uint64_t(ordinal) << 8) | slot;
    auto existingOrdinal = syntaxOrdinals.find(node.value);
    auto existingSyntax = ordinalSyntax.find(ordinal);
    bool mismatchedSite = false;
    ZC_IF_SOME(value, existingOrdinal) { mismatchedSite = value != ordinal; }
    ZC_IF_SOME(value, existingSyntax) { mismatchedSite = mismatchedSite || value != node.value; }
    if (!node || mismatchedSite || syntaxSlots.find(syntaxSlot) != zc::none ||
        schemaSlots.find(schemaSlot) != zc::none) {
      return false;
    }
    if (existingOrdinal == zc::none) { syntaxOrdinals.insert(node.value, ordinal); }
    if (existingSyntax == zc::none) { ordinalSyntax.insert(ordinal, node.value); }
    syntaxSlots.insert(syntaxSlot, 1);
    schemaSlots.insert(schemaSlot, 1);
    identity::CanonicalEncoder encoder;
    encoder.encodeUint64(ordinal);
    encoder.encodeUint64(node.value);
    encoder.encodeUint8(slot);
    operations.insert(zc::encodeHex(encoder.finish().asPtr()),
                      ProjectionOperation{kind, index, ordinal, node});
    return true;
  };

  for (size_t index = 0; index < input.moduleAliases.size(); ++index) {
    const auto& item = input.moduleAliases[index];
    if (!isKnownNamespace(item.localName.nameSpace()) ||
        !registerOperation(ProjectionOperationKind::ModuleAlias, index, item.schemaPreorderOrdinal,
                           item.node, static_cast<uint8_t>(Namespace::Module))) {
      return invariant(input.currentModule, item.schemaPreorderOrdinal);
    }
  }
  for (size_t index = 0; index < input.imports.size(); ++index) {
    const auto& item = input.imports[index];
    if (!isKnownNamespace(item.localName.nameSpace()) || !isKnownImportKind(item.kind) ||
        (item.kind == ImportBindingKind::Import && item.exportSpan != zc::none) ||
        (item.kind == ImportBindingKind::ForeignReexport && item.exportSpan == zc::none) ||
        !registerOperation(ProjectionOperationKind::Import, index, item.schemaPreorderOrdinal,
                           item.node, static_cast<uint8_t>(item.localName.nameSpace()))) {
      return invariant(input.currentModule, item.schemaPreorderOrdinal);
    }
  }
  for (size_t index = 0; index < input.localExports.size(); ++index) {
    const auto& item = input.localExports[index];
    if (!registerOperation(ProjectionOperationKind::LocalExport, index, item.schemaPreorderOrdinal,
                           item.node, 0)) {
      return invariant(input.currentModule, item.schemaPreorderOrdinal);
    }
  }

  zc::Vector<ModuleScopeBindingProjection> availableBindings(input.existingModuleBindings.size() +
                                                             operations.size());
  zc::TreeMap<zc::String, size_t> bindingIndices;
  for (auto& existing : input.existingModuleBindings) {
    if (!existing.node || !isKnownNamespace(existing.name.nameSpace())) {
      return invariant(input.currentModule);
    }
    auto key = bindingKey(existing.name);
    if (bindingIndices.find(key) != zc::none) { return invariant(input.currentModule); }
    const size_t index = availableBindings.size();
    bindingIndices.insert(zc::mv(key), index);
    availableBindings.add(zc::mv(existing));
  }

  ImportBindingProjectionCandidate candidate;
  const auto addFailure = [&](ast::NodeId node, const identity::DeclaredDefinitionName& name,
                              BinderDiagnosticCode diagnostic, const identity::SourceSpan& primary,
                              uint32_t ordinal, zc::Maybe<const identity::SourceSpan&> previous) {
    zc::Vector<BindingDiagnosticNoteRef> notes;
    ZC_IF_SOME(previousValue, previous) {
      notes.add(BindingDiagnosticNoteRef{BinderDiagnosticCode::PreviousDeclarationHere,
                                         previousValue.clone()});
    }
    candidate.sourceFailures.add(ImportBindingFailureProjection(
        node, name.clone(),
        BindingFailureRef{diagnostic, primary.clone(), emitterOrdinal(ordinal), zc::mv(notes)}));
  };

  const auto installBinding = [&](ModuleScopeBindingProjection&& binding,
                                  uint32_t ordinal) -> bool {
    auto key = bindingKey(binding.name);
    auto previous = bindingIndices.find(key);
    if (previous != zc::none) {
      ZC_IF_SOME(previousIndex, previous) {
        addFailure(binding.node, binding.name.name(), BinderDiagnosticCode::DuplicateIdentifier,
                   binding.binding.declarationSpan, ordinal,
                   availableBindings[previousIndex].binding.declarationSpan);
      }
      return false;
    }
    const size_t availableIndex = availableBindings.size();
    bindingIndices.insert(zc::mv(key), availableIndex);
    availableBindings.add(binding.clone());
    candidate.moduleScopeBindings.add(zc::mv(binding));
    return true;
  };

  for (const auto& ordered : operations) {
    const auto operation = ordered.value;
    if (operation.kind == ProjectionOperationKind::ModuleAlias) {
      auto& item = input.moduleAliases[operation.index];
      if (item.localName.nameSpace() != Namespace::Module) {
        return invariant(input.currentModule, operation.schemaPreorderOrdinal);
      }

      candidate.moduleAliases.add(
          ModuleAliasBindingFact{item.node, item.alias, item.target, item.targetExportNamesRevision,
                                 item.declarationSpan.clone(), item.targetSpan.clone()});
      zc::Maybe<identity::SourceSpan> noAlias;
      zc::Vector<ReexportProvenanceStep> noChain;
      auto binding = ModuleScopeBindingProjection(
          item.node, item.localName.clone(), BindingTarget::definition(item.alias),
          BindingTarget::module(item.target), BindingOrigin::LocalDeclaration,
          item.declarationSpan.clone(), zc::mv(noAlias), item.targetSpan.clone(), zc::mv(noChain));
      if (!installBinding(zc::mv(binding), operation.schemaPreorderOrdinal)) { continue; }

      zc::Maybe<identity::SourceSpan> noSurfaceAlias;
      zc::Maybe<identity::SourceSpan> exportSpan;
      if (item.exported) { exportSpan = item.declarationSpan.clone(); }
      zc::Vector<ReexportProvenanceStep> noSurfaceChain;
      candidate.surfaceSeeds.add(
          ImportSurfaceSeed(item.localName.clone(), BindingTarget::definition(item.alias),
                            BindingTarget::module(item.target),
                            item.exported ? VisibilityEnvelope::external()
                                          : VisibilityEnvelope::module(input.currentModule),
                            item.exported, item.declarationSpan.clone(), item.targetSpan.clone(),
                            zc::mv(noSurfaceAlias), zc::mv(exportSpan), zc::mv(noSurfaceChain)));
      continue;
    }

    if (operation.kind == ProjectionOperationKind::Import) {
      auto& item = input.imports[operation.index];
      auto chain = cloneChain(item.sourceReexportChain.asPtr());
      auto bindingIdentity = BindingTarget::semanticImport(item.binding.clone());
      if (item.kind == ImportBindingKind::ForeignReexport) {
        ZC_IF_SOME(exportSpan, item.exportSpan) {
          chain.add(ReexportProvenanceStep{input.currentModule, bindingIdentity.clone(),
                                           cloneTarget(item.canonicalTarget), exportSpan.clone()});
        }
      }
      candidate.imports.add(ImportBindingFact{
          item.node, item.binding.clone(), cloneTarget(item.canonicalTarget), item.sourceModule,
          item.sourceRevision, item.kind, item.declarationSpan.clone(), cloneSpan(item.aliasSpan),
          cloneChain(chain.asPtr())});

      const auto origin = item.kind == ImportBindingKind::Import ? BindingOrigin::ImportAlias
                                                                 : BindingOrigin::ReexportAlias;
      auto binding = ModuleScopeBindingProjection(
          item.node, item.localName.clone(), bindingIdentity.clone(),
          cloneTarget(item.canonicalTarget), origin, item.declarationSpan.clone(),
          cloneSpan(item.aliasSpan), item.canonicalDeclarationSpan.clone(),
          cloneChain(chain.asPtr()));
      if (!installBinding(zc::mv(binding), operation.schemaPreorderOrdinal) ||
          item.kind == ImportBindingKind::Import) {
        continue;
      }

      candidate.surfaceSeeds.add(ImportSurfaceSeed(
          item.localName.clone(), zc::mv(bindingIdentity), cloneTarget(item.canonicalTarget),
          VisibilityEnvelope::external(), true, item.declarationSpan.clone(),
          item.canonicalDeclarationSpan.clone(), cloneSpan(item.aliasSpan),
          cloneSpan(item.exportSpan), cloneChain(chain.asPtr())));
      continue;
    }

    auto& item = input.localExports[operation.index];
    size_t resolvedIndex = availableBindings.size();
    for (size_t index = 0; index < availableBindings.size(); ++index) {
      if (availableBindings[index].name.name().text() != item.sourceName.text()) { continue; }
      if (resolvedIndex != availableBindings.size()) {
        return invariant(input.currentModule, operation.schemaPreorderOrdinal);
      }
      resolvedIndex = index;
    }
    if (resolvedIndex == availableBindings.size()) {
      auto sourceName = bindingName(item.sourceName);
      addFailure(item.node, sourceName, BinderDiagnosticCode::UndefinedIdentifier,
                 item.sourceNameSpan, operation.schemaPreorderOrdinal, zc::none);
      continue;
    }
    const auto& source = availableBindings[resolvedIndex];
    auto exportedName =
        ImportBindingNameProjection(source.name.nameSpace(), bindingName(item.exportedName));
    auto chain = cloneChain(source.reexportChain.asPtr());
    chain.add(ReexportProvenanceStep{
        input.currentModule, cloneTarget(source.binding.bindingIdentity),
        cloneTarget(source.binding.canonicalTarget), item.exportSpan.clone()});
    candidate.localExports.add(LocalExportFact{
        item.node, cloneTarget(source.binding.bindingIdentity),
        cloneTarget(source.binding.canonicalTarget), source.binding.declarationSpan.clone(),
        source.canonicalDeclarationSpan.clone(), cloneSpan(item.aliasSpan), item.exportSpan.clone(),
        cloneChain(chain.asPtr())});

    auto binding = ModuleScopeBindingProjection(
        item.node, exportedName.clone(), cloneTarget(source.binding.bindingIdentity),
        cloneTarget(source.binding.canonicalTarget), BindingOrigin::ReexportAlias,
        item.declarationSpan.clone(), cloneSpan(item.aliasSpan),
        source.canonicalDeclarationSpan.clone(), cloneChain(chain.asPtr()));
    if (!installBinding(zc::mv(binding), operation.schemaPreorderOrdinal)) { continue; }

    candidate.surfaceSeeds.add(ImportSurfaceSeed(
        zc::mv(exportedName), cloneTarget(source.binding.bindingIdentity),
        cloneTarget(source.binding.canonicalTarget), VisibilityEnvelope::external(), true,
        item.declarationSpan.clone(), source.canonicalDeclarationSpan.clone(),
        cloneSpan(item.aliasSpan), item.exportSpan.clone(), cloneChain(chain.asPtr())));
  }

  return candidate;
}

}  // namespace zomlang::compiler::binder
