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
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "compiler/driver/package/package-compilation-request.h"
#include "compiler/identity/key/crate-key.h"
#include "compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::ir {

enum class TargetFeatureState : uint8_t { Enabled = 0x01, Disabled = 0x02 };
enum class BackendPanicStrategy : uint8_t { Unwind = 0x01, Abort = 0x02 };
enum class ObjectFormat : uint8_t { Elf = 0x01, MachO = 0x02, Coff = 0x03, Wasm = 0x04 };

class CanonicalTargetFeature final {
public:
  ZC_NODISCARD static zc::Maybe<CanonicalTargetFeature> from(zc::StringPtr name,
                                                             TargetFeatureState state);
  CanonicalTargetFeature(CanonicalTargetFeature&&) noexcept = default;
  CanonicalTargetFeature& operator=(CanonicalTargetFeature&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalTargetFeature);

  ZC_NODISCARD CanonicalTargetFeature clone() const;
  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD TargetFeatureState state() const noexcept;

private:
  CanonicalTargetFeature(zc::String&& name, TargetFeatureState state) noexcept;
  zc::String nameValue;
  TargetFeatureState stateValue;
};

/// \brief RFC 0010 backend/runtime target facts with no host-derived defaults.
class CanonicalTargetSpec final {
public:
  ZC_NODISCARD static zc::Maybe<CanonicalTargetSpec> from(
      zc::StringPtr triple, zc::StringPtr llvmDataLayout, zc::StringPtr cpu,
      zc::Vector<CanonicalTargetFeature>&& features, zc::StringPtr runtimeAbiProfile,
      BackendPanicStrategy panicStrategy, ObjectFormat objectFormat);

  CanonicalTargetSpec(CanonicalTargetSpec&&) noexcept = default;
  CanonicalTargetSpec& operator=(CanonicalTargetSpec&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalTargetSpec);

  ZC_NODISCARD CanonicalTargetSpec clone() const;
  ZC_NODISCARD zc::StringPtr triple() const noexcept;
  ZC_NODISCARD zc::StringPtr llvmDataLayout() const noexcept;
  ZC_NODISCARD zc::StringPtr cpu() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalTargetFeature> features() const noexcept;
  ZC_NODISCARD zc::StringPtr runtimeAbiProfile() const noexcept;
  ZC_NODISCARD BackendPanicStrategy panicStrategy() const noexcept;
  ZC_NODISCARD ObjectFormat objectFormat() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& targetSpecId() const noexcept;

private:
  CanonicalTargetSpec(zc::String&& triple, zc::String&& dataLayout, zc::String&& cpu,
                      zc::Vector<CanonicalTargetFeature>&& features, zc::String&& runtimeAbi,
                      BackendPanicStrategy panic, ObjectFormat object,
                      const identity::Sha256Digest& id) noexcept;
  zc::String tripleValue;
  zc::String dataLayoutValue;
  zc::String cpuValue;
  zc::Vector<CanonicalTargetFeature> featureValues;
  zc::String runtimeAbiValue;
  BackendPanicStrategy panicValue;
  ObjectFormat objectValue;
  identity::Sha256Digest idValue;
};

class RegisteredTargetProfileRecord final {
public:
  ZC_NODISCARD static zc::Maybe<RegisteredTargetProfileRecord> from(
      driver::package::RegisteredTargetProfileName&& name,
      identity::CanonicalTargetSpecificationKey&& semanticProjection,
      zc::Vector<identity::TargetFeatureName>&& semanticFeatures,
      zc::Vector<CanonicalTargetSpec>&& specifications);

  RegisteredTargetProfileRecord(RegisteredTargetProfileRecord&&) noexcept = default;
  RegisteredTargetProfileRecord& operator=(RegisteredTargetProfileRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(RegisteredTargetProfileRecord);

  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD const identity::CanonicalTargetSpecificationKey& semanticProjection() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::TargetFeatureName> semanticFeatures() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalTargetSpec> specifications() const noexcept;

private:
  RegisteredTargetProfileRecord(driver::package::RegisteredTargetProfileName&& name,
                                identity::CanonicalTargetSpecificationKey&& semanticProjection,
                                zc::Vector<identity::TargetFeatureName>&& semanticFeatures,
                                zc::Vector<CanonicalTargetSpec>&& specifications) noexcept;
  driver::package::RegisteredTargetProfileName nameValue;
  identity::CanonicalTargetSpecificationKey projectionValue;
  zc::Vector<identity::TargetFeatureName> semanticFeatureValues;
  zc::Vector<CanonicalTargetSpec> specificationValues;
  friend class TargetRegistrySnapshot;
};

enum class TargetSelectionVerificationIssue : uint8_t {
  CapabilityUnavailable = 0x01,
  InvalidFact = 0x02,
  RegistryRevisionMismatch = 0x03,
  ProjectionMismatch = 0x04,
};

class VerifiedTargetSelection final {
public:
  VerifiedTargetSelection(VerifiedTargetSelection&&) noexcept = default;
  VerifiedTargetSelection& operator=(VerifiedTargetSelection&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedTargetSelection);

  ZC_NODISCARD const driver::package::RegisteredTargetSelection& packageSelection() const noexcept;
  ZC_NODISCARD const CanonicalTargetSpec& targetSpec() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& targetSpecId() const noexcept;

private:
  VerifiedTargetSelection(driver::package::RegisteredTargetSelection&& packageSelection,
                          CanonicalTargetSpec&& spec) noexcept;
  driver::package::RegisteredTargetSelection packageSelectionValue;
  CanonicalTargetSpec specificationValue;
  friend class TargetRegistrySnapshot;
};

using TargetSelectionVerificationResult =
    zc::OneOf<VerifiedTargetSelection, TargetSelectionVerificationIssue>;

/// \brief Immutable RFC 0010 registry and sole backend target verifier.
class TargetRegistrySnapshot final {
public:
  ZC_NODISCARD static zc::Maybe<TargetRegistrySnapshot> from(
      driver::package::RegisteredTargetProfileName&& hostProfile,
      zc::Vector<RegisteredTargetProfileRecord>&& profiles);

  TargetRegistrySnapshot(TargetRegistrySnapshot&&) noexcept = default;
  TargetRegistrySnapshot& operator=(TargetRegistrySnapshot&&) noexcept = default;
  ZC_DISALLOW_COPY(TargetRegistrySnapshot);

  ZC_NODISCARD const identity::Sha256Digest& revision() const noexcept;
  ZC_NODISCARD zc::Maybe<driver::package::RegisteredTargetService> packageTargetService() const;
  ZC_NODISCARD TargetSelectionVerificationResult
  verify(const driver::package::RegisteredTargetSelection& selection) const;

private:
  TargetRegistrySnapshot(driver::package::RegisteredTargetProfileName&& hostProfile,
                         zc::Vector<RegisteredTargetProfileRecord>&& profiles,
                         const identity::Sha256Digest& revision) noexcept;
  driver::package::RegisteredTargetProfileName hostProfileValue;
  zc::Vector<RegisteredTargetProfileRecord> profileValues;
  identity::Sha256Digest revisionValue;
};

}  // namespace zomlang::compiler::ir
