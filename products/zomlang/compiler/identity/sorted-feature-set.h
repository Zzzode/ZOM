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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/canonical-scalar.h"

namespace zomlang::compiler::identity {

class CanonicalEncoder;

/// \brief Unique feature names sorted by canonical encoded bytes.
class SortedFeatureSet final {
public:
  SortedFeatureSet(SortedFeatureSet&&) noexcept = default;
  SortedFeatureSet& operator=(SortedFeatureSet&&) noexcept = default;
  ZC_DISALLOW_COPY(SortedFeatureSet);

  /// \brief Sorts a feature sequence canonically and rejects duplicate values.
  ZC_NODISCARD static zc::Maybe<SortedFeatureSet> from(zc::Vector<FeatureName>&& input);

  /// \brief Creates an explicit owned duplicate of this move-only set.
  ZC_NODISCARD SortedFeatureSet clone() const;

  /// \brief Creates an owned duplicate whose storage comes from `resource`.
  /// \param resource Resource that must outlive the returned set.
  /// \return A byte-identical feature set owned by `resource`.
  ZC_NODISCARD SortedFeatureSet clone(zc::MemoryResource& resource) const;

  /// \brief Returns the sorted unique values.
  ZC_NODISCARD zc::ArrayPtr<const FeatureName> values() const noexcept;

  /// \brief Encodes the sequence count followed by every sorted feature.
  void encode(CanonicalEncoder& encoder) const;

private:
  explicit SortedFeatureSet(zc::Vector<FeatureName>&& canonical) noexcept;

  zc::Vector<FeatureName> features;
};

/// \brief Unique semantic target features sorted by canonical encoded bytes.
class SortedTargetFeatureSet final {
public:
  SortedTargetFeatureSet(SortedTargetFeatureSet&&) noexcept = default;
  SortedTargetFeatureSet& operator=(SortedTargetFeatureSet&&) noexcept = default;
  ZC_DISALLOW_COPY(SortedTargetFeatureSet);

  /// \brief Sorts a target-feature sequence canonically and rejects duplicate values.
  ZC_NODISCARD static zc::Maybe<SortedTargetFeatureSet> from(zc::Vector<TargetFeatureName>&& input);

  /// \brief Creates an explicit owned duplicate of this move-only set.
  ZC_NODISCARD SortedTargetFeatureSet clone() const;

  /// \brief Returns the sorted unique values.
  ZC_NODISCARD zc::ArrayPtr<const TargetFeatureName> values() const noexcept;

  /// \brief Encodes the sequence count followed by every sorted target feature.
  void encode(CanonicalEncoder& encoder) const;

private:
  explicit SortedTargetFeatureSet(zc::Vector<TargetFeatureName>&& canonical) noexcept;

  zc::Vector<TargetFeatureName> features;
};

}  // namespace zomlang::compiler::identity
