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

#pragma once

#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/frozen-registry.h"
#include "zomlang/compiler/identity/identity-invariant.h"
#include "zomlang/compiler/identity/source-snapshot.h"

namespace zomlang::compiler::identity {

/// \brief The unique ordered family of RFC 0011 registries for one semantic context.
class SemanticIdentityRegistrySet final {
public:
  SemanticIdentityRegistrySet(SemanticIdentityRegistrySet&&) noexcept = default;
  SemanticIdentityRegistrySet& operator=(SemanticIdentityRegistrySet&&) noexcept = default;
  ZC_DISALLOW_COPY(SemanticIdentityRegistrySet);

  /// \brief Claims the sole RFC 0011 registry family for one factory-issued context.
  ZC_NODISCARD static zc::Maybe<SemanticIdentityRegistrySet> create(SemanticContextFactory& factory,
                                                                    SemanticContextBrand context);

  ZC_NODISCARD FrozenRegistryFailure collectPackage(PackageKey&& key,
                                                    uint32_t traversalOrdinal = 0);
  ZC_NODISCARD FrozenRegistryFailure freezePackages();
  ZC_NODISCARD FrozenRegistryFailure collectCrate(CrateKey&& key, uint32_t traversalOrdinal = 0);
  ZC_NODISCARD FrozenRegistryFailure freezeCrates();
  ZC_NODISCARD FrozenRegistryFailure collectSourceFile(ImmutableSourceSnapshot&& snapshot,
                                                       uint32_t traversalOrdinal = 0);
  ZC_NODISCARD FrozenRegistryFailure freezeSourceFiles();
  ZC_NODISCARD FrozenRegistryFailure collectModule(ModuleKey&& key, uint32_t traversalOrdinal = 0);
  ZC_NODISCARD FrozenRegistryFailure freezeModules();
  /// \brief Admits stable definition and implementation records outermost first.
  ZC_NODISCARD FrozenRegistryFailure collectDefinition(
      DefinitionIdentityRecord&& record,
      zc::Maybe<OverloadHeaderAuthority>&& overloadHeaderAuthority, uint32_t traversalOrdinal = 0);
  ZC_NODISCARD FrozenRegistryFailure collectImpl(ImplIdentityRecord&& record,
                                                 uint32_t traversalOrdinal = 0);
  /// \brief Freezes both halves of the mixed stable-owner authority catalog.
  ZC_NODISCARD FrozenRegistryFailure freezeStableIdentities();
  ZC_NODISCARD FrozenRegistryFailure
  collectGenericParameter(GenericParameterIdentityRecord&& record, uint32_t traversalOrdinal = 0);
  ZC_NODISCARD FrozenRegistryFailure
  collectCallableParameter(CallableParameterIdentityRecord&& record, uint32_t traversalOrdinal = 0);
  ZC_NODISCARD FrozenRegistryFailure freezeGenericParameters();
  ZC_NODISCARD FrozenRegistryFailure freezeCallableParameters();

  ZC_NODISCARD const PackageRegistry& packages() const noexcept;
  ZC_NODISCARD const CrateRegistry& crates() const noexcept;
  ZC_NODISCARD const SourceFileRegistry& sourceFiles() const noexcept;
  ZC_NODISCARD const ModuleRegistry& modules() const noexcept;
  ZC_NODISCARD const DefinitionRegistry& definitions() const noexcept;
  ZC_NODISCARD const ImplRegistry& impls() const noexcept;
  ZC_NODISCARD const GenericParameterRegistry& genericParameters() const noexcept;
  ZC_NODISCARD const CallableParameterRegistry& callableParameters() const noexcept;
  /// \brief Returns the semantic context that owns this complete registry family.
  ZC_NODISCARD SemanticContextBrand context() const noexcept;
  ZC_NODISCARD zc::Maybe<const ImmutableSourceSnapshot&> sourceSnapshot(SourceFileId source) const;
  ZC_NODISCARD zc::Maybe<SourceSpan> sourceSpan(SourceFileId source, uint64_t byteStart,
                                                uint64_t byteEnd) const;
  ZC_NODISCARD zc::ArrayPtr<const ImmutableSourceSnapshot> sourceSnapshots() const noexcept;
  void sortIdentityInvariants();
  ZC_NODISCARD zc::ArrayPtr<const IdentityInvariant> identityInvariants() const noexcept;

private:
  explicit SemanticIdentityRegistrySet(SemanticContextBrand context) noexcept;
  FrozenRegistryFailure recordFailure(FrozenRegistryFailure failure, IdentityAllocationPhase phase,
                                      IdentityApiSite apiSite,
                                      zc::Maybe<zc::Array<uint8_t>>&& structuralInputKey,
                                      uint32_t traversalOrdinal);

  SemanticContextBrand owner;
  PackageRegistry packageRegistry;
  CrateRegistry crateRegistry;
  SourceFileRegistry sourceFileRegistry;
  ModuleRegistry moduleRegistry;
  DefinitionRegistry definitionRegistry;
  ImplRegistry implRegistry;
  GenericParameterRegistry genericParameterRegistry;
  CallableParameterRegistry callableParameterRegistry;
  zc::Vector<ImmutableSourceSnapshot> sourceSnapshotValues;
  IdentityInvariantCollector invariantCollector;
};

}  // namespace zomlang::compiler::identity
