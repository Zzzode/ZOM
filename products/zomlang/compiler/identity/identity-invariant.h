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

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/identity/source-snapshot.h"

namespace zomlang::compiler::identity {

enum class IdentityAllocationPhase : uint8_t {
  Context = 0x01,
  Registry = 0x02,
  Encoding = 0x03,
  Package = 0x04,
  Crate = 0x05,
  Source = 0x06,
  Module = 0x07,
  Definition = 0x08,
  Impl = 0x09,
  GenericParameter = 0x0a,
  CallableParameter = 0x0b,
  SemanticType = 0x0c
};

enum class IdentityInvariantKind : uint8_t {
  InvalidHandle = 0x01,
  ForeignContext = 0x02,
  ForeignRegistry = 0x03,
  SlotOutOfRange = 0x04,
  AncestorMismatch = 0x05,
  InvalidSourceRange = 0x06,
  DuplicateCanonicalKey = 0x07,
  InvalidClosedValue = 0x08,
  PostFreezeMutation = 0x09,
  BrandExhausted = 0x0a,
  DuplicateSingletonStore = 0x0b,
  NonCanonicalEncoding = 0x0c,
  DigestCollision = 0x0d
};

enum class IdentityApiSite : uint8_t {
  ContextBrandIssue = 0x01,
  RegistryBrandIssue = 0x02,
  CanonicalEncode = 0x03,
  PackageFreeze = 0x04,
  CrateFreeze = 0x05,
  SourceFreeze = 0x06,
  ModuleFreeze = 0x07,
  DefinitionFreeze = 0x08,
  ImplFreeze = 0x09,
  SemanticTypeStoreCreate = 0x0a,
  HandleLookup = 0x0b,
  RegistryMutation = 0x0c,
  GenericParameterFreeze = 0x0d,
  CallableParameterFreeze = 0x0e
};

/// \brief One complete structured RFC 0011 invariant fact.
class IdentityInvariant final {
public:
  IdentityInvariant(IdentityInvariant&&) noexcept = default;
  IdentityInvariant& operator=(IdentityInvariant&&) noexcept = default;
  ZC_DISALLOW_COPY(IdentityInvariant);

  ZC_NODISCARD static zc::Maybe<IdentityInvariant> from(
      IdentityInvariantKind kind, IdentityAllocationPhase phase,
      zc::Maybe<zc::Array<uint8_t>>&& structuralInputKey,
      zc::Maybe<UnbrandedSourceRange>&& diagnosticRange, IdentityApiSite apiSite,
      uint32_t inputTraversalOrdinal);
  ZC_NODISCARD IdentityInvariant clone() const;

  ZC_NODISCARD IdentityInvariantKind kind() const noexcept;
  ZC_NODISCARD IdentityAllocationPhase phase() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const uint8_t>> structuralInputKey() const;
  ZC_NODISCARD zc::Maybe<const UnbrandedSourceRange&> diagnosticRange() const;
  ZC_NODISCARD IdentityApiSite apiSite() const noexcept;
  ZC_NODISCARD uint32_t inputTraversalOrdinal() const noexcept;

private:
  IdentityInvariant(IdentityInvariantKind kind, IdentityAllocationPhase phase,
                    zc::Maybe<zc::Array<uint8_t>>&& structuralInputKey,
                    zc::Maybe<UnbrandedSourceRange>&& diagnosticRange, IdentityApiSite apiSite,
                    uint32_t inputTraversalOrdinal) noexcept;

  IdentityInvariantKind kindValue;
  IdentityAllocationPhase phaseValue;
  zc::Maybe<zc::Array<uint8_t>> structuralInputKeyValue;
  zc::Maybe<UnbrandedSourceRange> diagnosticRangeValue;
  IdentityApiSite apiSiteValue;
  uint32_t traversalOrdinalValue;
};

/// \brief Retains and deterministically sorts every identity invariant fact.
class IdentityInvariantCollector final {
public:
  IdentityInvariantCollector() noexcept;
  ~IdentityInvariantCollector() noexcept(false);
  IdentityInvariantCollector(IdentityInvariantCollector&&) noexcept;
  IdentityInvariantCollector& operator=(IdentityInvariantCollector&&) noexcept;
  ZC_DISALLOW_COPY(IdentityInvariantCollector);

  void add(IdentityInvariant&& invariant);
  void sort();
  ZC_NODISCARD zc::ArrayPtr<const IdentityInvariant> facts() const noexcept;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::identity
