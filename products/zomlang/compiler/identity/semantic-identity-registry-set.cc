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
    case FrozenRegistryFailure::SourceContentMismatch:
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

}  // namespace

SemanticIdentityRegistrySet::SemanticIdentityRegistrySet(SemanticContextBrand context) noexcept
    : owner(context),
      packageRegistry(context),
      crateRegistry(context),
      sourceFileRegistry(context),
      moduleRegistry(context),
      definitionRegistry(context),
      implRegistry(context) {}

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
  auto invariant = IdentityInvariant::from(invariantKind(failure), phase,
                                           zc::mv(structuralInputKey), zc::mv(noRange), apiSite,
                                           traversalOrdinal);
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
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::Crate, IdentityApiSite::RegistryMutation,
                         zc::mv(structural), traversalOrdinal);
  }
  return recordFailure(crateRegistry.collect(zc::mv(key)), IdentityAllocationPhase::Crate,
                       IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::freezeCrates() {
  if (!packageRegistry.isFrozen()) {
    zc::Maybe<zc::Array<uint8_t>> noStructural;
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::Crate, IdentityApiSite::CrateFreeze,
                         zc::mv(noStructural), 0);
  }
  const auto result = crateRegistry.freeze();
  return recordFailure(result, IdentityAllocationPhase::Crate, IdentityApiSite::CrateFreeze,
                       copyFailureKey(crateRegistry), 0);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::collectSourceFile(
    ImmutableSourceSnapshot&& snapshot, uint32_t traversalOrdinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = snapshot.source().encode();
  if (!crateRegistry.isFrozen()) {
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::Source, IdentityApiSite::RegistryMutation,
                         zc::mv(structural), traversalOrdinal);
  }
  if (!snapshot.source().acceptsContentDigest(snapshot.contentDigest())) {
    return recordFailure(FrozenRegistryFailure::SourceContentMismatch,
                         IdentityAllocationPhase::Source,
                         IdentityApiSite::RegistryMutation, zc::mv(structural),
                         traversalOrdinal);
  }
  auto result = sourceFileRegistry.collect(snapshot.source().clone());
  if (result == FrozenRegistryFailure::None) { sourceSnapshotValues.add(zc::mv(snapshot)); }
  return recordFailure(result, IdentityAllocationPhase::Source,
                       IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::freezeSourceFiles() {
  if (!crateRegistry.isFrozen()) {
    zc::Maybe<zc::Array<uint8_t>> noStructural;
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::Source, IdentityApiSite::SourceFreeze,
                         zc::mv(noStructural), 0);
  }
  const auto result = sourceFileRegistry.freeze();
  return recordFailure(result, IdentityAllocationPhase::Source, IdentityApiSite::SourceFreeze,
                       copyFailureKey(sourceFileRegistry), 0);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::collectModule(ModuleKey&& key,
                                                                 uint32_t traversalOrdinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = key.encode();
  if (!sourceFileRegistry.isFrozen()) {
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::Module, IdentityApiSite::RegistryMutation,
                         zc::mv(structural), traversalOrdinal);
  }
  return recordFailure(moduleRegistry.collect(zc::mv(key)), IdentityAllocationPhase::Module,
                       IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::freezeModules() {
  if (!sourceFileRegistry.isFrozen()) {
    zc::Maybe<zc::Array<uint8_t>> noStructural;
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::Module, IdentityApiSite::ModuleFreeze,
                         zc::mv(noStructural), 0);
  }
  const auto result = moduleRegistry.freeze();
  return recordFailure(result, IdentityAllocationPhase::Module, IdentityApiSite::ModuleFreeze,
                       copyFailureKey(moduleRegistry), 0);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::collectDefinition(DefinitionKey&& key,
                                                                     uint32_t traversalOrdinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = key.encode();
  if (!moduleRegistry.isFrozen()) {
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::Definition,
                         IdentityApiSite::RegistryMutation, zc::mv(structural),
                         traversalOrdinal);
  }
  return recordFailure(definitionRegistry.collect(zc::mv(key)),
                       IdentityAllocationPhase::Definition,
                       IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::freezeDefinitions() {
  if (!moduleRegistry.isFrozen()) {
    zc::Maybe<zc::Array<uint8_t>> noStructural;
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::Definition,
                         IdentityApiSite::DefinitionFreeze, zc::mv(noStructural), 0);
  }
  const auto result = definitionRegistry.freeze();
  return recordFailure(result, IdentityAllocationPhase::Definition,
                       IdentityApiSite::DefinitionFreeze, copyFailureKey(definitionRegistry), 0);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::collectImpl(ImplKey&& key,
                                                               uint32_t traversalOrdinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = key.encode();
  if (!definitionRegistry.isFrozen()) {
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::Impl, IdentityApiSite::RegistryMutation,
                         zc::mv(structural), traversalOrdinal);
  }
  return recordFailure(implRegistry.collect(zc::mv(key)), IdentityAllocationPhase::Impl,
                       IdentityApiSite::RegistryMutation, zc::mv(structural), traversalOrdinal);
}

FrozenRegistryFailure SemanticIdentityRegistrySet::freezeImpls() {
  if (!definitionRegistry.isFrozen()) {
    zc::Maybe<zc::Array<uint8_t>> noStructural;
    return recordFailure(FrozenRegistryFailure::RegistryNotFrozen,
                         IdentityAllocationPhase::Impl, IdentityApiSite::ImplFreeze,
                         zc::mv(noStructural), 0);
  }
  const auto result = implRegistry.freeze();
  return recordFailure(result, IdentityAllocationPhase::Impl, IdentityApiSite::ImplFreeze,
                       copyFailureKey(implRegistry), 0);
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
