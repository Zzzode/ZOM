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

#include "zomlang/compiler/identity/semantic-identity-registry-set.h"

namespace zomlang::compiler::identity {
namespace {

IdentityInvariantKind invariantKind(FrozenRegistryFailure failure) {
  switch (failure) {
    case FrozenRegistryFailure::InvalidContext:
    case FrozenRegistryFailure::InvalidHandle:
      return IdentityInvariantKind::InvalidHandle;
    case FrozenRegistryFailure::ForeignContext:
      return IdentityInvariantKind::ForeignContext;
    case FrozenRegistryFailure::SlotOutOfRange:
      return IdentityInvariantKind::SlotOutOfRange;
    case FrozenRegistryFailure::DuplicateCanonicalKey:
      return IdentityInvariantKind::DuplicateCanonicalKey;
    case FrozenRegistryFailure::DigestCollision:
      return IdentityInvariantKind::DigestCollision;
    case FrozenRegistryFailure::InvalidAuthority:
      return IdentityInvariantKind::NonCanonicalEncoding;
    case FrozenRegistryFailure::UnknownOwner:
    case FrozenRegistryFailure::OwnerModuleMismatch:
    case FrozenRegistryFailure::OwnerPrefixMismatch:
    case FrozenRegistryFailure::RepeatedOwner:
    case FrozenRegistryFailure::SelfOwner:
    case FrozenRegistryFailure::AncestorMismatch:
      return IdentityInvariantKind::AncestorMismatch;
    case FrozenRegistryFailure::PostFreezeMutation:
      return IdentityInvariantKind::PostFreezeMutation;
    case FrozenRegistryFailure::RegistryNotFrozen:
      return IdentityInvariantKind::AncestorMismatch;
    case FrozenRegistryFailure::None:
      break;
  }
  ZC_UNREACHABLE
}

template <typename Registry>
zc::Maybe<zc::Array<uint8_t>> copyFailureKey(const Registry& registry) {
  ZC_IF_SOME(value, registry.failureStructuralKey()) { return zc::heapArray(value); }
  return zc::none;
}

bool sameModule(const ModuleKey& left, const ModuleKey& right) {
  const auto leftBytes = left.encode();
  const auto rightBytes = right.encode();
  return leftBytes.asPtr() == rightBytes.asPtr();
}

bool sameOwner(const EnclosingStableOwnerKey& left, const EnclosingStableOwnerKey& right) {
  const auto leftBytes = left.encode();
  const auto rightBytes = right.encode();
  return leftBytes.asPtr() == rightBytes.asPtr();
}

template <typename OwnerRecord, typename ChildRecord>
FrozenRegistryFailure validateOwnerRecord(const OwnerRecord& owner, const ChildRecord& child,
                                          size_t ownerIndex) {
  if (!sameModule(owner.module(), child.module())) {
    return FrozenRegistryFailure::OwnerModuleMismatch;
  }
  if (owner.owners().size() != ownerIndex) { return FrozenRegistryFailure::OwnerPrefixMismatch; }
  for (size_t prefixIndex = 0; prefixIndex < ownerIndex; ++prefixIndex) {
    if (!sameOwner(owner.owners()[prefixIndex], child.owners()[prefixIndex])) {
      return FrozenRegistryFailure::OwnerPrefixMismatch;
    }
  }
  return FrozenRegistryFailure::None;
}

template <typename ChildRecord>
FrozenRegistryFailure validateOwners(const ChildRecord& child,
                                     const DefinitionRegistry& definitions,
                                     const ImplRegistry& implementations) {
  for (size_t ownerIndex = 0; ownerIndex < child.owners().size(); ++ownerIndex) {
    const auto& owner = child.owners()[ownerIndex];
    for (size_t previousIndex = 0; previousIndex < ownerIndex; ++previousIndex) {
      if (sameOwner(child.owners()[previousIndex], owner)) {
        return FrozenRegistryFailure::RepeatedOwner;
      }
    }

    if (owner.kind() == EnclosingStableOwnerKind::Definition) {
      ZC_IF_SOME(key, owner.definitionKey()) {
        ZC_IF_SOME(authority, definitions.admittedAuthority(key)) {
          const auto result = validateOwnerRecord(authority.record(), child, ownerIndex);
          if (result != FrozenRegistryFailure::None) { return result; }
          continue;
        }
      }
      return FrozenRegistryFailure::UnknownOwner;
    }

    ZC_IF_SOME(key, owner.implKey()) {
      ZC_IF_SOME(authority, implementations.admittedAuthority(key)) {
        const auto result = validateOwnerRecord(authority.record(), child, ownerIndex);
        if (result != FrozenRegistryFailure::None) { return result; }
        continue;
      }
    }
    return FrozenRegistryFailure::UnknownOwner;
  }
  return FrozenRegistryFailure::None;
}

bool hasSelfOwner(const DefinitionIdentityRecord& record, const DefinitionKey& key) {
  for (const auto& owner : record.owners()) {
    if (owner.kind() != EnclosingStableOwnerKind::Definition) { continue; }
    ZC_IF_SOME(ownerKey, owner.definitionKey()) {
      if (ownerKey == key) { return true; }
    }
  }
  return false;
}

bool hasSelfOwner(const ImplIdentityRecord& record, const ImplKey& key) {
  for (const auto& owner : record.owners()) {
    if (owner.kind() != EnclosingStableOwnerKind::Implementation) { continue; }
    ZC_IF_SOME(ownerKey, owner.implKey()) {
      if (ownerKey == key) { return true; }
    }
  }
  return false;
}

}  // namespace

SemanticIdentityRegistrySet::SemanticIdentityRegistrySet(SemanticContextBrand context) noexcept
    : owner(context),
      packageRegistry(context),
      crateRegistry(context),
      sourceFileRegistry(context),
      moduleRegistry(context),
      definitionRegistry(context),
      implRegistry(context),
      genericParameterRegistry(context),
      callableParameterRegistry(context) {}

zc::Maybe<SemanticIdentityRegistrySet> SemanticIdentityRegistrySet::create(
    SemanticContextFactory& factory, SemanticContextBrand context) {
  if (!factory.claimIdentityRegistrySet(context)) { return zc::none; }
  return SemanticIdentityRegistrySet(context);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::recordFailure(
    FrozenRegistryFailure failure, IdentityAllocationPhase phase, IdentityApiSite apiSite,
    zc::Maybe<zc::Array<uint8_t>>&& structuralInputKey, uint32_t traversalOrdinal) {
  if (failure == FrozenRegistryFailure::None) { return failure; }
  zc::Maybe<UnbrandedSourceRange> noRange;
  auto invariant =
      IdentityInvariant::from(invariantKind(failure), phase, zc::mv(structuralInputKey),
                              zc::mv(noRange), apiSite, traversalOrdinal);
  ZC_IF_SOME(value, invariant) { invariantCollector.add(zc::mv(value)); }
  return failure;
}

FrozenRegistryFailure SemanticIdentityRegistrySet::collectPackage(PackageKey&& key,
                                                                  uint32_t traversalOrdinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = key.encode();
  return recordFailure(packageRegistry.collect(zc::mv(key)), IdentityAllocationPhase::Package,
                       IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::freezePackages() {
  const auto result = packageRegistry.freeze();
  return recordFailure(result, IdentityAllocationPhase::Package, IdentityApiSite::PackageFreeze,
                       copyFailureKey(packageRegistry), 0);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::collectCrate(CrateKey&& key,
                                                                uint32_t traversalOrdinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = key.encode();
  if (!packageRegistry.isFrozen()) {
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen, IdentityAllocationPhase::Crate,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  if (packageRegistry.find(key.package()) == zc::none) {
    return recordFailure(FrozenRegistryFailure::AncestorMismatch, IdentityAllocationPhase::Crate,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  return recordFailure(crateRegistry.collect(zc::mv(key)), IdentityAllocationPhase::Crate,
                       IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::freezeCrates() {
  if (!packageRegistry.isFrozen()) {
    zc::Maybe<zc::Array<uint8_t>> noStructural;
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen, IdentityAllocationPhase::Crate,
                         IdentityApiSite::CrateFreeze, zc::mv(noStructural), 0);
  }
  const auto result = crateRegistry.freeze();
  return recordFailure(result, IdentityAllocationPhase::Crate, IdentityApiSite::CrateFreeze,
                       copyFailureKey(crateRegistry), 0);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::collectSourceFile(
    ImmutableSourceSnapshot&& snapshot, uint32_t traversalOrdinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = snapshot.source().encode();
  if (!crateRegistry.isFrozen()) {
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen, IdentityAllocationPhase::Source,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  if (crateRegistry.find(snapshot.source().crate()) == zc::none) {
    return recordFailure(FrozenRegistryFailure::AncestorMismatch, IdentityAllocationPhase::Source,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  auto result = sourceFileRegistry.collect(snapshot.source().clone());
  if (result == FrozenRegistryFailure::None) { sourceSnapshotValues.add(zc::mv(snapshot)); }
  return recordFailure(result, IdentityAllocationPhase::Source, IdentityApiSite::RegistryMutation,
                       zc::mv(structural), traversalOrdinal);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::freezeSourceFiles() {
  if (!crateRegistry.isFrozen()) {
    zc::Maybe<zc::Array<uint8_t>> noStructural;
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen, IdentityAllocationPhase::Source,
                         IdentityApiSite::SourceFreeze, zc::mv(noStructural), 0);
  }
  const auto result = sourceFileRegistry.freeze();
  return recordFailure(result, IdentityAllocationPhase::Source, IdentityApiSite::SourceFreeze,
                       copyFailureKey(sourceFileRegistry), 0);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::collectModule(ModuleKey&& key,
                                                                 uint32_t traversalOrdinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = key.encode();
  if (!sourceFileRegistry.isFrozen()) {
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen, IdentityAllocationPhase::Module,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  if (crateRegistry.find(key.crate()) == zc::none) {
    return recordFailure(FrozenRegistryFailure::AncestorMismatch, IdentityAllocationPhase::Module,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  return recordFailure(moduleRegistry.collect(zc::mv(key)), IdentityAllocationPhase::Module,
                       IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::freezeModules() {
  if (!sourceFileRegistry.isFrozen()) {
    zc::Maybe<zc::Array<uint8_t>> noStructural;
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen, IdentityAllocationPhase::Module,
                         IdentityApiSite::ModuleFreeze, zc::mv(noStructural), 0);
  }
  const auto result = moduleRegistry.freeze();
  return recordFailure(result, IdentityAllocationPhase::Module, IdentityApiSite::ModuleFreeze,
                       copyFailureKey(moduleRegistry), 0);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::collectDefinition(
    DefinitionIdentityRecord&& record, zc::Maybe<OverloadHeaderAuthority>&& overloadHeaderAuthority,
    uint32_t traversalOrdinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = record.encode();
  if (!moduleRegistry.isFrozen()) {
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::Definition, IdentityApiSite::RegistryMutation,
                         zc::mv(structural), traversalOrdinal);
  }
  if (moduleRegistry.find(record.module()) == zc::none) {
    return recordFailure(FrozenRegistryFailure::AncestorMismatch,
                         IdentityAllocationPhase::Definition, IdentityApiSite::RegistryMutation,
                         zc::mv(structural), traversalOrdinal);
  }
  const auto ownerResult = validateOwners(record, definitionRegistry, implRegistry);
  if (ownerResult != FrozenRegistryFailure::None) {
    return recordFailure(ownerResult, IdentityAllocationPhase::Definition,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  const auto key = DefinitionKey::compute(record);
  if (hasSelfOwner(record, key)) {
    return recordFailure(FrozenRegistryFailure::SelfOwner, IdentityAllocationPhase::Definition,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  auto authority =
      DefinitionIdentityAuthority::from(zc::mv(record), zc::mv(overloadHeaderAuthority));
  if (authority == zc::none) {
    return recordFailure(FrozenRegistryFailure::InvalidAuthority,
                         IdentityAllocationPhase::Definition, IdentityApiSite::RegistryMutation,
                         zc::mv(structural), traversalOrdinal);
  }
  ZC_IF_SOME(value, authority) {
    return recordFailure(definitionRegistry.collect(zc::mv(value)),
                         IdentityAllocationPhase::Definition, IdentityApiSite::RegistryMutation,
                         zc::mv(structural), traversalOrdinal);
  }
  ZC_UNREACHABLE
}

FrozenRegistryFailure SemanticIdentityRegistrySet::collectImpl(ImplIdentityRecord&& record,
                                                               uint32_t traversalOrdinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = record.encode();
  if (!moduleRegistry.isFrozen()) {
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen, IdentityAllocationPhase::Impl,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  if (moduleRegistry.find(record.module()) == zc::none) {
    return recordFailure(FrozenRegistryFailure::AncestorMismatch, IdentityAllocationPhase::Impl,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  const auto ownerResult = validateOwners(record, definitionRegistry, implRegistry);
  if (ownerResult != FrozenRegistryFailure::None) {
    return recordFailure(ownerResult, IdentityAllocationPhase::Impl,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  const auto key = ImplKey::compute(record);
  if (hasSelfOwner(record, key)) {
    return recordFailure(FrozenRegistryFailure::SelfOwner, IdentityAllocationPhase::Impl,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  auto authority = ImplIdentityAuthority::from(zc::mv(record));
  return recordFailure(implRegistry.collect(zc::mv(authority)), IdentityAllocationPhase::Impl,
                       IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::freezeStableIdentities() {
  if (!moduleRegistry.isFrozen()) {
    zc::Maybe<zc::Array<uint8_t>> noStructural;
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::Definition, IdentityApiSite::DefinitionFreeze,
                         zc::mv(noStructural), 0);
  }
  const auto definitionResult = definitionRegistry.freeze();
  if (definitionResult != FrozenRegistryFailure::None) {
    return recordFailure(definitionResult, IdentityAllocationPhase::Definition,
                         IdentityApiSite::DefinitionFreeze, copyFailureKey(definitionRegistry), 0);
  }
  const auto implResult = implRegistry.freeze();
  return recordFailure(implResult, IdentityAllocationPhase::Impl, IdentityApiSite::ImplFreeze,
                       copyFailureKey(implRegistry), 0);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::collectGenericParameter(
    GenericParameterIdentityRecord&& record, uint32_t traversalOrdinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = record.encode();
  if (!definitionRegistry.isFrozen() || !implRegistry.isFrozen()) {
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::GenericParameter,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  bool ownerExists = false;
  if (record.owner().kind() == StableGenericParameterOwnerKind::Definition) {
    ZC_IF_SOME(key, record.owner().definitionKey()) {
      ownerExists = definitionRegistry.admittedAuthority(key) != zc::none;
    }
  } else {
    ZC_IF_SOME(key, record.owner().implKey()) {
      ownerExists = implRegistry.admittedAuthority(key) != zc::none;
    }
  }
  if (!ownerExists) {
    return recordFailure(FrozenRegistryFailure::UnknownOwner,
                         IdentityAllocationPhase::GenericParameter,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  auto authority = GenericParameterAuthority::from(zc::mv(record));
  return recordFailure(genericParameterRegistry.collect(zc::mv(authority)),
                       IdentityAllocationPhase::GenericParameter, IdentityApiSite::RegistryMutation,
                       zc::mv(structural), traversalOrdinal);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::collectCallableParameter(
    CallableParameterIdentityRecord&& record, uint32_t traversalOrdinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = record.encode();
  if (!definitionRegistry.isFrozen() || !implRegistry.isFrozen()) {
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::CallableParameter,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  bool positionExists = false;
  ZC_IF_SOME(owner, definitionRegistry.admittedAuthority(record.owner())) {
    ZC_IF_SOME(overload, owner.overloadHeaderAuthority()) {
      if (record.position().kind() == CallableParameterPositionKind::Receiver) {
        positionExists = overload.header().receiver() != zc::none;
      } else {
        ZC_IF_SOME(ordinal, record.position().ordinal()) {
          positionExists = ordinal < overload.header().parameters().size();
        }
      }
    }
  }
  if (!positionExists) {
    return recordFailure(FrozenRegistryFailure::InvalidAuthority,
                         IdentityAllocationPhase::CallableParameter,
                         IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
  }
  auto authority = CallableParameterAuthority::from(zc::mv(record));
  return recordFailure(callableParameterRegistry.collect(zc::mv(authority)),
                       IdentityAllocationPhase::CallableParameter,
                       IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::freezeGenericParameters() {
  if (!definitionRegistry.isFrozen() || !implRegistry.isFrozen()) {
    zc::Maybe<zc::Array<uint8_t>> noStructural;
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::GenericParameter,
                         IdentityApiSite::GenericParameterFreeze, zc::mv(noStructural), 0);
  }
  const auto result = genericParameterRegistry.freeze();
  return recordFailure(result, IdentityAllocationPhase::GenericParameter,
                       IdentityApiSite::GenericParameterFreeze,
                       copyFailureKey(genericParameterRegistry), 0);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::freezeCallableParameters() {
  if (!genericParameterRegistry.isFrozen()) {
    zc::Maybe<zc::Array<uint8_t>> noStructural;
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::CallableParameter,
                         IdentityApiSite::CallableParameterFreeze, zc::mv(noStructural), 0);
  }
  const auto result = callableParameterRegistry.freeze();
  return recordFailure(result, IdentityAllocationPhase::CallableParameter,
                       IdentityApiSite::CallableParameterFreeze,
                       copyFailureKey(callableParameterRegistry), 0);
}

const PackageRegistry& SemanticIdentityRegistrySet::packages() const noexcept {
  return packageRegistry;
}
const CrateRegistry& SemanticIdentityRegistrySet::crates() const noexcept { return crateRegistry; }
const SourceFileRegistry& SemanticIdentityRegistrySet::sourceFiles() const noexcept {
  return sourceFileRegistry;
}
const ModuleRegistry& SemanticIdentityRegistrySet::modules() const noexcept {
  return moduleRegistry;
}
const DefinitionRegistry& SemanticIdentityRegistrySet::definitions() const noexcept {
  return definitionRegistry;
}
const ImplRegistry& SemanticIdentityRegistrySet::impls() const noexcept { return implRegistry; }
const GenericParameterRegistry& SemanticIdentityRegistrySet::genericParameters() const noexcept {
  return genericParameterRegistry;
}
const CallableParameterRegistry& SemanticIdentityRegistrySet::callableParameters() const noexcept {
  return callableParameterRegistry;
}
SemanticContextBrand SemanticIdentityRegistrySet::context() const noexcept { return owner; }

zc::Maybe<const ImmutableSourceSnapshot&> SemanticIdentityRegistrySet::sourceSnapshot(
    SourceFileId source) const {
  auto key = sourceFileRegistry.lookup(source);
  ZC_IF_SOME(sourceKey, key) {
    for (const auto& snapshot : sourceSnapshotValues) {
      if (snapshot.source().sameAs(sourceKey)) { return snapshot; }
    }
  }
  return zc::none;
}

zc::Maybe<SourceSpan> SemanticIdentityRegistrySet::sourceSpan(SourceFileId source,
                                                              uint64_t byteStart,
                                                              uint64_t byteEnd) const {
  ZC_IF_SOME(snapshot, sourceSnapshot(source)) { return snapshot.span(byteStart, byteEnd); }
  return zc::none;
}

zc::ArrayPtr<const ImmutableSourceSnapshot> SemanticIdentityRegistrySet::sourceSnapshots()
    const noexcept {
  return sourceSnapshotValues.asPtr();
}

void SemanticIdentityRegistrySet::sortIdentityInvariants() { invariantCollector.sort(); }

zc::ArrayPtr<const IdentityInvariant> SemanticIdentityRegistrySet::identityInvariants()
    const noexcept {
  return invariantCollector.facts();
}

}  // namespace zomlang::compiler::identity
