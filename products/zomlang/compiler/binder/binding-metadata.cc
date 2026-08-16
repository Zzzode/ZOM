// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/binding-metadata.h"

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/core/diagnostic.h"
#include "zomlang/compiler/identity/identity-diagnostic-adapter.h"
#include "zomlang/compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::binder {
namespace {

constexpr char kSurfaceDomain[] = "zom.binding-export-surface";
constexpr char kAllocationDumpDomain[] = "zom.binding-allocation-dump";

void appendUint64(zc::Vector<uint8_t>& bytes, uint64_t value) {
  for (uint32_t shift = 56;; shift -= 8) {
    bytes.add(static_cast<uint8_t>(value >> shift));
    if (shift == 0) { break; }
  }
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t count = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < count; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

bool sameRange(zc::Maybe<const identity::UnbrandedSourceRange&> left,
               zc::Maybe<const identity::UnbrandedSourceRange&> right) {
  if (left == zc::none) { return right == zc::none; }
  if (right == zc::none) { return false; }
  ZC_IF_SOME(leftValue, left) {
    ZC_IF_SOME(rightValue, right) {
      const auto leftBytes = leftValue.encode();
      const auto rightBytes = rightValue.encode();
      return leftBytes.asPtr() == rightBytes.asPtr();
    }
  }
  ZC_UNREACHABLE
}

}  // namespace

ScopeId::ScopeId(identity::ModuleId module, uint32_t index) noexcept
    : moduleValue(module), indexValue(index) {}
identity::ModuleId ScopeId::module() const noexcept { return moduleValue; }
uint32_t ScopeId::index() const noexcept { return indexValue; }
bool ScopeId::belongsTo(identity::SemanticContextBrand context) const noexcept {
  return moduleValue.belongsTo(context);
}
bool ScopeId::operator==(const ScopeId& other) const noexcept {
  return moduleValue == other.moduleValue && indexValue == other.indexValue;
}

LabelOwner::LabelOwner(LabelOwnerValue&& value) noexcept : valueValue(zc::mv(value)) {}
LabelOwner LabelOwner::module(identity::ModuleId value) {
  return LabelOwner(LabelOwnerValue(ModuleLabelOwner{value}));
}
LabelOwner LabelOwner::callable(identity::DefId value) {
  return LabelOwner(LabelOwnerValue(CallableLabelOwner{value}));
}
LabelOwner LabelOwner::anonymous(identity::ModuleId module, AnonymousOwnerLocalKey&& value) {
  return LabelOwner(LabelOwnerValue(AnonymousLabelOwner{module, zc::mv(value)}));
}
LabelOwner LabelOwner::clone() const {
  if (valueValue.is<ModuleLabelOwner>()) {
    return module(valueValue.get<ModuleLabelOwner>().module);
  }
  if (valueValue.is<CallableLabelOwner>()) {
    return callable(valueValue.get<CallableLabelOwner>().callable);
  }
  const auto& owner = valueValue.get<AnonymousLabelOwner>();
  return anonymous(owner.module, owner.anonymous.clone());
}
const LabelOwnerValue& LabelOwner::value() const noexcept { return valueValue; }
bool LabelOwner::belongsTo(identity::SemanticContextBrand context) const noexcept {
  if (valueValue.is<ModuleLabelOwner>()) {
    return valueValue.get<ModuleLabelOwner>().module.belongsTo(context);
  }
  if (valueValue.is<CallableLabelOwner>()) {
    return valueValue.get<CallableLabelOwner>().callable.belongsTo(context);
  }
  return valueValue.get<AnonymousLabelOwner>().module.belongsTo(context);
}
bool LabelOwner::operator==(const LabelOwner& other) const noexcept {
  if (valueValue.is<ModuleLabelOwner>()) {
    return other.valueValue.is<ModuleLabelOwner>() &&
           valueValue.get<ModuleLabelOwner>().module ==
               other.valueValue.get<ModuleLabelOwner>().module;
  }
  if (valueValue.is<CallableLabelOwner>()) {
    return other.valueValue.is<CallableLabelOwner>() &&
           valueValue.get<CallableLabelOwner>().callable ==
               other.valueValue.get<CallableLabelOwner>().callable;
  }
  if (!other.valueValue.is<AnonymousLabelOwner>()) { return false; }
  const auto& left = valueValue.get<AnonymousLabelOwner>();
  const auto& right = other.valueValue.get<AnonymousLabelOwner>();
  return left.module == right.module && left.anonymous == right.anonymous;
}

LabelId::LabelId(LabelOwner&& owner, uint32_t index) noexcept
    : ownerValue(zc::mv(owner)), indexValue(index) {}
LabelId LabelId::clone() const { return LabelId(ownerValue.clone(), indexValue); }
const LabelOwner& LabelId::owner() const noexcept { return ownerValue; }
uint32_t LabelId::index() const noexcept { return indexValue; }
bool LabelId::belongsTo(identity::SemanticContextBrand context) const noexcept {
  return ownerValue.belongsTo(context);
}
bool LabelId::operator==(const LabelId& other) const noexcept {
  return ownerValue == other.ownerValue && indexValue == other.indexValue;
}

LabelTarget::LabelTarget(LabelTargetValue&& value) noexcept : valueValue(zc::mv(value)) {}
LabelTarget LabelTarget::block(ScopeId scope) {
  return LabelTarget(LabelTargetValue(BlockLabelTarget{scope}));
}
LabelTarget LabelTarget::loop(ScopeId scope) {
  return LabelTarget(LabelTargetValue(LoopLabelTarget{scope}));
}
LabelTarget LabelTarget::clone() const {
  if (valueValue.is<BlockLabelTarget>()) { return block(valueValue.get<BlockLabelTarget>().scope); }
  return loop(valueValue.get<LoopLabelTarget>().scope);
}
const LabelTargetValue& LabelTarget::value() const noexcept { return valueValue; }
bool LabelTarget::belongsTo(identity::SemanticContextBrand context) const noexcept {
  if (valueValue.is<BlockLabelTarget>()) {
    return valueValue.get<BlockLabelTarget>().scope.belongsTo(context);
  }
  return valueValue.get<LoopLabelTarget>().scope.belongsTo(context);
}
bool LabelTarget::operator==(const LabelTarget& other) const noexcept {
  if (valueValue.is<BlockLabelTarget>() != other.valueValue.is<BlockLabelTarget>()) {
    return false;
  }
  if (valueValue.is<BlockLabelTarget>()) {
    return valueValue.get<BlockLabelTarget>().scope ==
           other.valueValue.get<BlockLabelTarget>().scope;
  }
  return valueValue.get<LoopLabelTarget>().scope == other.valueValue.get<LoopLabelTarget>().scope;
}

ScopeOwner::ScopeOwner(ScopeOwnerValue&& value) noexcept : valueValue(zc::mv(value)) {}
ScopeOwner ScopeOwner::module(identity::ModuleId value) {
  return ScopeOwner(ScopeOwnerValue(ModuleScopeOwner{value}));
}
ScopeOwner ScopeOwner::definition(identity::DefId value) {
  return ScopeOwner(ScopeOwnerValue(DefinitionScopeOwner{value}));
}
ScopeOwner ScopeOwner::implementation(ImplOccurrenceId value) {
  return ScopeOwner(ScopeOwnerValue(ImplScopeOwner{value}));
}
ScopeOwner ScopeOwner::anonymous(AnonymousOwnerLocalKey&& value) {
  return ScopeOwner(ScopeOwnerValue(AnonymousScopeOwner{zc::mv(value)}));
}
ScopeOwner ScopeOwner::clone() const {
  if (valueValue.is<ModuleScopeOwner>()) {
    return module(valueValue.get<ModuleScopeOwner>().module);
  }
  if (valueValue.is<DefinitionScopeOwner>()) {
    return definition(valueValue.get<DefinitionScopeOwner>().definition);
  }
  if (valueValue.is<ImplScopeOwner>()) {
    return implementation(valueValue.get<ImplScopeOwner>().occurrence);
  }
  return anonymous(valueValue.get<AnonymousScopeOwner>().anonymous.clone());
}
const ScopeOwnerValue& ScopeOwner::value() const noexcept { return valueValue; }
bool ScopeOwner::operator==(const ScopeOwner& other) const noexcept {
  if (valueValue.is<ModuleScopeOwner>()) {
    return other.valueValue.is<ModuleScopeOwner>() &&
           valueValue.get<ModuleScopeOwner>().module ==
               other.valueValue.get<ModuleScopeOwner>().module;
  }
  if (valueValue.is<DefinitionScopeOwner>()) {
    return other.valueValue.is<DefinitionScopeOwner>() &&
           valueValue.get<DefinitionScopeOwner>().definition ==
               other.valueValue.get<DefinitionScopeOwner>().definition;
  }
  if (valueValue.is<ImplScopeOwner>()) {
    return other.valueValue.is<ImplScopeOwner>() &&
           valueValue.get<ImplScopeOwner>().occurrence ==
               other.valueValue.get<ImplScopeOwner>().occurrence;
  }
  return other.valueValue.is<AnonymousScopeOwner>() &&
         valueValue.get<AnonymousScopeOwner>().anonymous ==
             other.valueValue.get<AnonymousScopeOwner>().anonymous;
}

BindingTarget::BindingTarget(BindingTargetValue&& value) noexcept : valueValue(zc::mv(value)) {}
BindingTarget BindingTarget::definition(identity::DefId value) {
  return BindingTarget(BindingTargetValue(DefinitionBindingTarget{value}));
}
BindingTarget BindingTarget::genericParameter(identity::GenericParameterId value) {
  return BindingTarget(BindingTargetValue(GenericParameterBindingTarget{value}));
}
BindingTarget BindingTarget::callableParameter(identity::CallableParameterId value) {
  return BindingTarget(BindingTargetValue(CallableParameterBindingTarget{value}));
}
BindingTarget BindingTarget::ownerLocal(OwnerLocalBindingId value) {
  return BindingTarget(BindingTargetValue(OwnerLocalBindingTarget{value}));
}
BindingTarget BindingTarget::semanticImport(identity::ImportBindingKey&& value) {
  return BindingTarget(BindingTargetValue(SemanticImportBindingTarget{zc::mv(value)}));
}
BindingTarget BindingTarget::module(identity::ModuleId value) {
  return BindingTarget(BindingTargetValue(ModuleBindingTarget{value}));
}
BindingTarget BindingTarget::clone() const {
  if (valueValue.is<DefinitionBindingTarget>()) {
    return definition(valueValue.get<DefinitionBindingTarget>().definition);
  }
  if (valueValue.is<GenericParameterBindingTarget>()) {
    return genericParameter(valueValue.get<GenericParameterBindingTarget>().parameter);
  }
  if (valueValue.is<CallableParameterBindingTarget>()) {
    return callableParameter(valueValue.get<CallableParameterBindingTarget>().parameter);
  }
  if (valueValue.is<OwnerLocalBindingTarget>()) {
    return ownerLocal(valueValue.get<OwnerLocalBindingTarget>().binding);
  }
  if (valueValue.is<SemanticImportBindingTarget>()) {
    return semanticImport(valueValue.get<SemanticImportBindingTarget>().binding.clone());
  }
  return module(valueValue.get<ModuleBindingTarget>().module);
}
const BindingTargetValue& BindingTarget::value() const noexcept { return valueValue; }

BindingNameKey::BindingNameKey(Namespace nameSpace,
                               identity::DeclaredDefinitionName&& name) noexcept
    : namespaceValue(nameSpace), nameValue(zc::mv(name)) {}
zc::Maybe<BindingNameKey> BindingNameKey::from(Namespace nameSpace,
                                               identity::DeclaredDefinitionName&& name) noexcept {
  if (nameSpace < Namespace::Value || nameSpace > Namespace::Attribute) { return zc::none; }
  return BindingNameKey(nameSpace, zc::mv(name));
}
BindingNameKey BindingNameKey::clone() const {
  return BindingNameKey(namespaceValue, nameValue.clone());
}
Namespace BindingNameKey::nameSpace() const noexcept { return namespaceValue; }
const identity::DeclaredDefinitionName& BindingNameKey::name() const noexcept { return nameValue; }

NameBinding::NameBinding(BindingTarget&& bindingIdentity, BindingTarget&& canonicalTarget,
                         Namespace nameSpace, BindingOrigin origin,
                         identity::SourceSpan&& declarationSpan,
                         zc::Maybe<identity::SourceSpan>&& aliasSpan) noexcept
    : bindingIdentity(zc::mv(bindingIdentity)),
      canonicalTarget(zc::mv(canonicalTarget)),
      nameSpace(nameSpace),
      origin(origin),
      declarationSpan(zc::mv(declarationSpan)),
      aliasSpan(zc::mv(aliasSpan)) {}

ScopeBindingEntry::ScopeBindingEntry(BindingNameKey&& name, NameBinding&& binding) noexcept
    : name(zc::mv(name)), binding(zc::mv(binding)) {}

ScopeRecord::ScopeRecord(ScopeId id, zc::Maybe<ScopeId>&& parent, ScopeOwner&& owner,
                         ScopeKind kind, zc::Vector<ScopeBindingEntry>&& bindings,
                         identity::SourceSpan&& source) noexcept
    : id(id),
      parent(zc::mv(parent)),
      owner(zc::mv(owner)),
      kind(kind),
      bindings(zc::mv(bindings)),
      source(zc::mv(source)) {}

zc::Array<uint8_t> frameBindingAllocationDump(
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> scopeRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> labelRecords) {
  zc::Vector<uint8_t> bytes;
  for (size_t index = 0; index < sizeof(kAllocationDumpDomain) - 1; ++index) {
    bytes.add(static_cast<uint8_t>(kAllocationDumpDomain[index]));
  }
  bytes.add(0x00);
  appendUint64(bytes, scopeRecords.size());
  for (const auto record : scopeRecords) {
    appendUint64(bytes, record.size());
    bytes.addAll(record);
  }
  appendUint64(bytes, labelRecords.size());
  for (const auto record : labelRecords) {
    appendUint64(bytes, record.size());
    bytes.addAll(record);
  }
  return bytes.releaseAsArray();
}

zc::Array<uint8_t> frameBindingExtensionSequences(
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> selfTypeRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> thisBindingRecords) {
  zc::Vector<uint8_t> bytes;
  appendUint64(bytes, selfTypeRecords.size());
  for (const auto record : selfTypeRecords) { bytes.addAll(record); }
  appendUint64(bytes, thisBindingRecords.size());
  for (const auto record : thisBindingRecords) { bytes.addAll(record); }
  return bytes.releaseAsArray();
}

DefinitionFact::DefinitionFact(identity::DefId identity, DefinitionSite&& site,
                               identity::DefinitionKind kind,
                               identity::DeclaredDefinitionName&& name, Namespace nameSpace,
                               ScopeId declaringScope, identity::SourceSpan&& source,
                               DefinitionActivation activation,
                               zc::Maybe<MemberVisibility>&& memberVisibility) noexcept
    : identity(identity),
      site(zc::mv(site)),
      kind(kind),
      name(zc::mv(name)),
      nameSpace(nameSpace),
      declaringScope(declaringScope),
      source(zc::mv(source)),
      activation(activation),
      memberVisibility(zc::mv(memberVisibility)) {}

VisibilityEnvelope::VisibilityEnvelope(VisibilityEnvelopeValue&& value) noexcept
    : valueValue(zc::mv(value)) {}
VisibilityEnvelope VisibilityEnvelope::module(identity::ModuleId value) {
  return VisibilityEnvelope(VisibilityEnvelopeValue(ModuleVisibility{value}));
}
VisibilityEnvelope VisibilityEnvelope::external() {
  return VisibilityEnvelope(VisibilityEnvelopeValue(ExternalVisibility{}));
}
VisibilityEnvelope VisibilityEnvelope::clone() const {
  if (valueValue.is<ModuleVisibility>()) {
    return module(valueValue.get<ModuleVisibility>().module);
  }
  return external();
}
const VisibilityEnvelopeValue& VisibilityEnvelope::value() const noexcept { return valueValue; }

ReexportProvenanceStep ReexportProvenanceStep::clone() const {
  return ReexportProvenanceStep{module, bindingIdentity.clone(), canonicalTarget.clone(),
                                exportSpan.clone()};
}

ExportSurfaceEntry::ExportSurfaceEntry(BindingNameKey&& name, BindingTarget&& bindingIdentity,
                                       BindingTarget&& canonicalTarget,
                                       VisibilityEnvelope&& visibility, bool exported,
                                       identity::SourceSpan&& bindingSpan,
                                       identity::SourceSpan&& canonicalDeclarationSpan,
                                       zc::Maybe<identity::SourceSpan>&& aliasSpan,
                                       zc::Maybe<identity::SourceSpan>&& exportSpan,
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

ExportSurfaceEntry ExportSurfaceEntry::clone() const {
  zc::Maybe<identity::SourceSpan> clonedAliasSpan;
  ZC_IF_SOME(value, aliasSpan) { clonedAliasSpan = value.clone(); }
  zc::Maybe<identity::SourceSpan> clonedExportSpan;
  ZC_IF_SOME(value, exportSpan) { clonedExportSpan = value.clone(); }
  zc::Vector<ReexportProvenanceStep> clonedChain(reexportChain.size());
  for (const auto& step : reexportChain) { clonedChain.add(step.clone()); }
  return ExportSurfaceEntry(name.clone(), bindingIdentity.clone(), canonicalTarget.clone(),
                            visibility.clone(), exported, bindingSpan.clone(),
                            canonicalDeclarationSpan.clone(), zc::mv(clonedAliasSpan),
                            zc::mv(clonedExportSpan), zc::mv(clonedChain));
}

ExportSurfaceRevision::ExportSurfaceRevision(const identity::Sha256Digest& digest) noexcept
    : value(digest) {}
ExportSurfaceRevision ExportSurfaceRevision::fromDigest(
    const identity::Sha256Digest& digest) noexcept {
  return ExportSurfaceRevision(digest);
}
const identity::Sha256Digest& ExportSurfaceRevision::digest() const noexcept { return value; }
zc::Maybe<ExportSurfaceRevision> ExportSurfaceRevision::computeFramed(
    const identity::Sha256Digest& semanticContextFingerprint,
    zc::ArrayPtr<const uint8_t> encodedModule, zc::ArrayPtr<const uint8_t> encodedPackage,
    zc::ArrayPtr<const uint8_t> encodedVisibleEntries, zc::ArrayPtr<const uint8_t> encodedExports) {
  zc::Vector<uint8_t> bytes;
  for (size_t index = 0; index < sizeof(kSurfaceDomain) - 1; ++index) {
    bytes.add(static_cast<uint8_t>(kSurfaceDomain[index]));
  }
  bytes.add(0);
  bytes.addAll(semanticContextFingerprint.bytes());
  bytes.addAll(encodedModule);
  bytes.addAll(encodedPackage);
  bytes.addAll(encodedVisibleEntries);
  bytes.addAll(encodedExports);
  ZC_IF_SOME(digest, identity::sha256(bytes.asPtr())) { return ExportSurfaceRevision(digest); }
  return zc::none;
}

ModuleAliasExportNamesRevision::ModuleAliasExportNamesRevision(
    const identity::Sha256Digest& digest) noexcept
    : value(digest) {}
ModuleAliasExportNamesRevision ModuleAliasExportNamesRevision::fromDigest(
    const identity::Sha256Digest& digest) noexcept {
  return ModuleAliasExportNamesRevision(digest);
}
const identity::Sha256Digest& ModuleAliasExportNamesRevision::digest() const noexcept {
  return value;
}

diagnostics::DiagID binderInvariantDiagnosticId(BinderInvariantKind kind) {
  using diagnostics::DiagID;
  switch (kind) {
    case BinderInvariantKind::MalformedScopeGraph:
      return DiagID::BinderMalformedScopeGraph;
    case BinderInvariantKind::MissingRequiredResolution:
      return DiagID::BinderMissingRequiredResolution;
    case BinderInvariantKind::AliasCycle:
      return DiagID::BinderAliasCycle;
    case BinderInvariantKind::InvalidBindingFact:
      return DiagID::BinderInvalidFact;
    case BinderInvariantKind::InvalidEmitterOrdinal:
      return DiagID::BinderInvalidEmitterOrdinal;
  }
  ZC_UNREACHABLE
}

BinderInvariantDiagnosticGroup::BinderInvariantDiagnosticGroup(
    diagnostics::DiagID diagnosticId, zc::Maybe<identity::UnbrandedSourceRange>&& diagnosticRange,
    uint64_t occurrenceCount) noexcept
    : idValue(diagnosticId), rangeValue(zc::mv(diagnosticRange)), countValue(occurrenceCount) {}

diagnostics::DiagID BinderInvariantDiagnosticGroup::diagnosticId() const noexcept {
  return idValue;
}
zc::Maybe<const identity::UnbrandedSourceRange&> BinderInvariantDiagnosticGroup::diagnosticRange()
    const {
  ZC_IF_SOME(value, rangeValue) { return value; }
  return zc::none;
}
uint64_t BinderInvariantDiagnosticGroup::occurrenceCount() const noexcept { return countValue; }

zc::Maybe<zc::Vector<BinderInvariantDiagnosticGroup>> groupBinderInvariants(
    zc::ArrayPtr<const BinderInvariantFact> facts) {
  zc::Vector<size_t> order;
  zc::Vector<zc::Array<uint8_t>> rangeBytes;
  zc::Vector<bool> hasRange;
  for (size_t index = 0; index < facts.size(); ++index) {
    if (index != 0 && facts[index].module != facts[0].module) { return zc::none; }
    order.add(index);
    ZC_IF_SOME(range, facts[index].diagnosticRange) {
      hasRange.add(true);
      rangeBytes.add(range.encode());
    } else {
      hasRange.add(false);
      rangeBytes.add(zc::heapArray<uint8_t>(0));
    }
  }
  const auto less = [&](size_t left, size_t right) {
    const auto leftKind = static_cast<uint8_t>(facts[left].kind);
    const auto rightKind = static_cast<uint8_t>(facts[right].kind);
    if (leftKind != rightKind) { return leftKind < rightKind; }
    if (hasRange[left] != hasRange[right]) { return !hasRange[left]; }
    const int rangeOrder = compareBytes(rangeBytes[left].asPtr(), rangeBytes[right].asPtr());
    if (rangeOrder != 0) { return rangeOrder < 0; }
    const auto leftSite = static_cast<uint8_t>(facts[left].emitterSite);
    const auto rightSite = static_cast<uint8_t>(facts[right].emitterSite);
    if (leftSite != rightSite) { return leftSite < rightSite; }
    return facts[left].schemaPreorderOrdinal < facts[right].schemaPreorderOrdinal;
  };
  for (size_t index = 1; index < order.size(); ++index) {
    const size_t current = order[index];
    size_t insertion = index;
    while (insertion > 0 && less(current, order[insertion - 1])) {
      order[insertion] = order[insertion - 1];
      --insertion;
    }
    order[insertion] = current;
  }
  zc::Vector<BinderInvariantDiagnosticGroup> groups;
  for (const auto index : order) {
    const auto id = binderInvariantDiagnosticId(facts[index].kind);
    zc::Maybe<const identity::UnbrandedSourceRange&> range;
    ZC_IF_SOME(value, facts[index].diagnosticRange) { range = value; }
    if (groups.size() != 0 && groups.back().diagnosticId() == id &&
        sameRange(groups.back().diagnosticRange(), range)) {
      ++groups.back().countValue;
      continue;
    }
    zc::Maybe<identity::UnbrandedSourceRange> ownedRange;
    ZC_IF_SOME(value, range) { ownedRange = value.clone(); }
    groups.add(BinderInvariantDiagnosticGroup(id, zc::mv(ownedRange), 1));
  }
  return groups;
}

void emitBinderInvariantGroups(
    diagnostics::DiagnosticEngine& diagnostics,
    zc::ArrayPtr<const BinderInvariantDiagnosticGroup> groups,
    zc::Maybe<const identity::IdentityDiagnosticLocationResolver&> locationResolver) {
  for (const auto& group : groups) {
    source::SourceLoc location;
    ZC_IF_SOME(range, group.diagnosticRange()) {
      ZC_IF_SOME(resolver, locationResolver) {
        ZC_IF_SOME(resolved, resolver.resolve(range)) { location = resolved; }
      }
    }
    diagnostics.emit(
        diagnostics::Diagnostic(group.diagnosticId(), location, zc::str(group.occurrenceCount())));
  }
}

void emitBinderInvariant(
    diagnostics::DiagnosticEngine& diagnostics, const BinderInvariantFact& fact,
    zc::Maybe<const identity::IdentityDiagnosticLocationResolver&> locationResolver) {
  source::SourceLoc location;
  ZC_IF_SOME(range, fact.diagnosticRange) {
    ZC_IF_SOME(resolver, locationResolver) {
      ZC_IF_SOME(resolved, resolver.resolve(range)) { location = resolved; }
    }
  }
  diagnostics.emit(
      diagnostics::Diagnostic(binderInvariantDiagnosticId(fact.kind), location, zc::str(1u)));
}

}  // namespace zomlang::compiler::binder
