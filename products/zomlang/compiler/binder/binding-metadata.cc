// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/binding-metadata.h"

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/identity/identity-diagnostic-adapter.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::binder {
namespace {

constexpr char kSurfaceDomain[] = "zom.binding-export-surface.v0";
constexpr char kAllocationDumpDomain[] = "zom.binding-allocation-dump.v0";

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

ScopeOwner::ScopeOwner(ScopeOwnerValue&& value) noexcept : valueValue(zc::mv(value)) {}
ScopeOwner ScopeOwner::module(identity::ModuleId value) {
  return ScopeOwner(ScopeOwnerValue(ModuleScopeOwner{value}));
}
ScopeOwner ScopeOwner::definition(identity::DefId value) {
  return ScopeOwner(ScopeOwnerValue(DefinitionScopeOwner{value}));
}
ScopeOwner ScopeOwner::implementation(identity::ImplId value) {
  return ScopeOwner(ScopeOwnerValue(ImplScopeOwner{value}));
}
const ScopeOwnerValue& ScopeOwner::value() const noexcept { return valueValue; }

BindingTarget::BindingTarget(BindingTargetValue&& value) noexcept : valueValue(zc::mv(value)) {}
BindingTarget BindingTarget::definition(identity::DefId value) {
  return BindingTarget(BindingTargetValue(DefinitionBindingTarget{value}));
}
BindingTarget BindingTarget::module(identity::ModuleId value) {
  return BindingTarget(BindingTargetValue(ModuleBindingTarget{value}));
}
const BindingTargetValue& BindingTarget::value() const noexcept { return valueValue; }

BindingNameKey::BindingNameKey(Namespace nameSpace, identity::SemanticIdentifier&& name) noexcept
    : namespaceValue(nameSpace), nameValue(zc::mv(name)) {}
BindingNameKey BindingNameKey::clone() const {
  return BindingNameKey(namespaceValue, nameValue.clone());
}
Namespace BindingNameKey::nameSpace() const noexcept { return namespaceValue; }
const identity::SemanticIdentifier& BindingNameKey::name() const noexcept { return nameValue; }

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

DefinitionFact::DefinitionFact(identity::DefId identity, DefinitionSite&& site,
                               identity::DefinitionKind kind, identity::DefinitionNameKey&& name,
                               Namespace nameSpace, ScopeId declaringScope,
                               identity::SourceSpan&& source,
                               DefinitionActivation activation) noexcept
    : identity(identity),
      site(zc::mv(site)),
      kind(kind),
      name(zc::mv(name)),
      nameSpace(nameSpace),
      declaringScope(declaringScope),
      source(zc::mv(source)),
      activation(activation) {}

VisibilityEnvelope::VisibilityEnvelope(VisibilityEnvelopeValue&& value) noexcept
    : valueValue(zc::mv(value)) {}
VisibilityEnvelope VisibilityEnvelope::module(identity::ModuleId value) {
  return VisibilityEnvelope(VisibilityEnvelopeValue(ModuleVisibility{value}));
}
VisibilityEnvelope VisibilityEnvelope::external() {
  return VisibilityEnvelope(VisibilityEnvelopeValue(ExternalVisibility{}));
}
const VisibilityEnvelopeValue& VisibilityEnvelope::value() const noexcept { return valueValue; }

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

ExportSurfaceRevision::ExportSurfaceRevision(const identity::Sha256Digest& digest) noexcept
    : value(digest) {}
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
    }
    else {
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
