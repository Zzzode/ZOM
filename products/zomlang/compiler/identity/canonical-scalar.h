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

#include "zc/core/common.h"
#include "zc/core/string.h"

namespace zomlang::compiler::identity {

class CanonicalEncoder;

/// \brief Closed validation domains for canonical identity text.
enum class CanonicalScalarDomain : uint8_t {
  PathSegment,
  PackageName,
  TargetName,
  DependencyAlias,
  FeatureName,
  TargetComponentName,
  TargetFeatureName,
  SemanticEnvironmentName,
  SemanticIdentifier,
  ModulePathSegment,
  DeclaredDefinitionName
};

/// \brief Move-only, domain-validated canonical identity text.
template <CanonicalScalarDomain Domain>
class CanonicalScalar final {
public:
  CanonicalScalar(CanonicalScalar&&) noexcept = default;
  CanonicalScalar& operator=(CanonicalScalar&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalScalar);

  /// \brief Validates source text and normalizes it to Unicode NFC.
  ZC_NODISCARD static zc::Maybe<CanonicalScalar> fromSource(zc::StringPtr input);

  /// \brief Admits text only when it is already valid and in Unicode NFC.
  ZC_NODISCARD static zc::Maybe<CanonicalScalar> fromCanonical(zc::StringPtr input);

  /// \brief Creates an explicit owned duplicate of this move-only scalar.
  ZC_NODISCARD CanonicalScalar clone() const;

  /// \brief Creates an owned duplicate whose storage comes from `resource`.
  /// \param resource Resource that must outlive the returned scalar.
  /// \return A byte-identical scalar owned by `resource`.
  ZC_NODISCARD CanonicalScalar clone(zc::MemoryResource& resource) const;

  /// \brief Returns the validated NFC text.
  ZC_NODISCARD zc::StringPtr text() const noexcept;

  /// \brief Encodes this scalar using the canonical text rule.
  void encode(CanonicalEncoder& encoder) const;

  bool operator==(const CanonicalScalar& other) const noexcept;
  bool operator!=(const CanonicalScalar& other) const noexcept { return !(*this == other); }
  bool operator<(const CanonicalScalar& other) const noexcept;

private:
  explicit CanonicalScalar(zc::String&& canonical) noexcept;

  zc::String value;
};

using CanonicalPathSegment = CanonicalScalar<CanonicalScalarDomain::PathSegment>;
using PackageName = CanonicalScalar<CanonicalScalarDomain::PackageName>;
using TargetName = CanonicalScalar<CanonicalScalarDomain::TargetName>;
using DependencyAlias = CanonicalScalar<CanonicalScalarDomain::DependencyAlias>;
using FeatureName = CanonicalScalar<CanonicalScalarDomain::FeatureName>;
using TargetComponentName = CanonicalScalar<CanonicalScalarDomain::TargetComponentName>;
using TargetFeatureName = CanonicalScalar<CanonicalScalarDomain::TargetFeatureName>;
using SemanticEnvironmentName = CanonicalScalar<CanonicalScalarDomain::SemanticEnvironmentName>;
using SemanticIdentifier = CanonicalScalar<CanonicalScalarDomain::SemanticIdentifier>;
using ModulePathSegment = CanonicalScalar<CanonicalScalarDomain::ModulePathSegment>;
using DeclaredDefinitionName = CanonicalScalar<CanonicalScalarDomain::DeclaredDefinitionName>;

extern template class CanonicalScalar<CanonicalScalarDomain::PathSegment>;
extern template class CanonicalScalar<CanonicalScalarDomain::PackageName>;
extern template class CanonicalScalar<CanonicalScalarDomain::TargetName>;
extern template class CanonicalScalar<CanonicalScalarDomain::DependencyAlias>;
extern template class CanonicalScalar<CanonicalScalarDomain::FeatureName>;
extern template class CanonicalScalar<CanonicalScalarDomain::TargetComponentName>;
extern template class CanonicalScalar<CanonicalScalarDomain::TargetFeatureName>;
extern template class CanonicalScalar<CanonicalScalarDomain::SemanticEnvironmentName>;
extern template class CanonicalScalar<CanonicalScalarDomain::SemanticIdentifier>;
extern template class CanonicalScalar<CanonicalScalarDomain::ModulePathSegment>;
extern template class CanonicalScalar<CanonicalScalarDomain::DeclaredDefinitionName>;

}  // namespace zomlang::compiler::identity
