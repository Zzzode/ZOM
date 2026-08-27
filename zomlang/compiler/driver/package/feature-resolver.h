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
#include "zc/core/vector.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/identity/sorted-feature-set.h"

namespace zomlang::compiler::driver::package {

enum class FeatureActivationDomain : uint8_t { Target = 0x01, Build = 0x02 };
enum class FeatureIssue : uint8_t {
  UnknownFeature = 0x01,
  UnknownDependency = 0x02,
  DependencyNotOptional = 0x03,
  DuplicateEdge = 0x04,
  Cycle = 0x05,
  RequestedFeatureMissing = 0x06
};

/// \brief One dependency alias activated by the package feature fixed point.
class ActivatedDependency final {
public:
  ZC_NODISCARD static ActivatedDependency from(identity::DependencyAlias&& alias,
                                               identity::SortedFeatureSet&& requestedFeatures);

  ActivatedDependency(ActivatedDependency&&) noexcept = default;
  ActivatedDependency& operator=(ActivatedDependency&&) noexcept = default;
  ZC_DISALLOW_COPY(ActivatedDependency);

  ZC_NODISCARD zc::StringPtr alias() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::FeatureName> requestedFeatures() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  ActivatedDependency(identity::DependencyAlias&& alias,
                      identity::SortedFeatureSet&& requestedFeatures) noexcept;

  identity::DependencyAlias aliasValue;
  identity::SortedFeatureSet featureValues;
};

/// \brief Complete local feature closure for one package and activation domain.
class ExpandedFeatureActivation final {
public:
  ZC_NODISCARD static ExpandedFeatureActivation from(
      FeatureActivationDomain domain, identity::SortedFeatureSet&& activeFeatures,
      zc::Vector<ActivatedDependency>&& activatedDependencies);

  ExpandedFeatureActivation(ExpandedFeatureActivation&&) noexcept = default;
  ExpandedFeatureActivation& operator=(ExpandedFeatureActivation&&) noexcept = default;
  ZC_DISALLOW_COPY(ExpandedFeatureActivation);

  ZC_NODISCARD FeatureActivationDomain domain() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::FeatureName> activeFeatures() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ActivatedDependency> activatedDependencies() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  ExpandedFeatureActivation(FeatureActivationDomain domain,
                            identity::SortedFeatureSet&& activeFeatures,
                            zc::Vector<ActivatedDependency>&& activatedDependencies) noexcept;

  FeatureActivationDomain domainValue;
  identity::SortedFeatureSet activeFeatureValues;
  zc::Vector<ActivatedDependency> activatedDependencyValues;
};

using FeatureExpansionResult = zc::OneOf<ExpandedFeatureActivation, FeatureIssue>;

/// \brief Computes the deterministic additive feature closure for one normalized package.
class FeatureResolver final {
public:
  ZC_NODISCARD static FeatureExpansionResult expand(
      const NormalizedManifest& manifest, FeatureActivationDomain domain,
      zc::ArrayPtr<const identity::FeatureName> requested, bool useDefaultFeatures);
  ZC_NODISCARD static FeatureExpansionResult expand(
      const CanonicalManifestRecord& manifest, FeatureActivationDomain domain,
      zc::ArrayPtr<const identity::FeatureName> requested, bool useDefaultFeatures);
  /// \brief Expands canonical features with every owned result allocated by `resource`.
  ZC_NODISCARD static FeatureExpansionResult expand(
      zc::MemoryResource& resource, const CanonicalManifestRecord& manifest,
      FeatureActivationDomain domain, zc::ArrayPtr<const identity::FeatureName> requested,
      bool useDefaultFeatures);
};

}  // namespace zomlang::compiler::driver::package
