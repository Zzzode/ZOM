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
#include "zc/core/string.h"

namespace zomlang::compiler::identity {

class CanonicalEncoder;

/// \brief Validated Semantic Versioning 2.0.0 identity text.
class ResolvedVersion final {
public:
  ResolvedVersion(ResolvedVersion&&) noexcept = default;
  ResolvedVersion& operator=(ResolvedVersion&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedVersion);

  /// \brief Validates a complete SemVer 2.0.0 value without rewriting its text.
  ZC_NODISCARD static zc::Maybe<ResolvedVersion> fromCanonical(zc::StringPtr input);

  /// \brief Validates SemVer text and owns the accepted bytes through `resource`.
  /// \param resource Resource that must outlive the returned version.
  ZC_NODISCARD static zc::Maybe<ResolvedVersion> fromCanonical(zc::MemoryResource& resource,
                                                               zc::StringPtr input);

  /// \brief Creates an explicit owned duplicate of this move-only version.
  ZC_NODISCARD ResolvedVersion clone() const;

  /// \brief Creates an owned duplicate whose storage comes from `resource`.
  /// \param resource Resource that must outlive the returned version.
  /// \return A byte-identical version owned by `resource`.
  ZC_NODISCARD ResolvedVersion clone(zc::MemoryResource& resource) const;

  /// \brief Returns the complete validated version text.
  ZC_NODISCARD zc::StringPtr text() const noexcept;

  /// \brief Encodes the complete version as canonical text.
  void encode(CanonicalEncoder& encoder) const;

  bool operator==(const ResolvedVersion& other) const noexcept;
  bool operator!=(const ResolvedVersion& other) const noexcept { return !(*this == other); }
  bool operator<(const ResolvedVersion& other) const noexcept;

private:
  explicit ResolvedVersion(zc::String&& canonical) noexcept;

  zc::String value;
};

}  // namespace zomlang::compiler::identity
