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

/// \brief Credential-free canonical absolute `https` or `ssh` URL.
class CanonicalUrl final {
public:
  CanonicalUrl(CanonicalUrl&&) noexcept = default;
  CanonicalUrl& operator=(CanonicalUrl&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalUrl);

  /// \brief Validates and canonicalizes an absolute hierarchical source URL.
  ZC_NODISCARD static zc::Maybe<CanonicalUrl> fromSource(zc::StringPtr input);

  /// \brief Admits a URL only when it is already in canonical form.
  ZC_NODISCARD static zc::Maybe<CanonicalUrl> fromCanonical(zc::StringPtr input);

  /// \brief Creates an explicit owned duplicate of this move-only URL.
  ZC_NODISCARD CanonicalUrl clone() const;

  /// \brief Creates an owned duplicate whose storage comes from `resource`.
  /// \param resource Resource that must outlive the returned URL.
  /// \return A byte-identical URL owned by `resource`.
  ZC_NODISCARD CanonicalUrl clone(zc::MemoryResource& resource) const;

  /// \brief Returns the canonical credential-free URL text.
  ZC_NODISCARD zc::StringPtr text() const noexcept;

  /// \brief Encodes the complete canonical URL as text.
  void encode(CanonicalEncoder& encoder) const;

  bool operator==(const CanonicalUrl& other) const noexcept;
  bool operator!=(const CanonicalUrl& other) const noexcept { return !(*this == other); }
  bool operator<(const CanonicalUrl& other) const noexcept;

private:
  explicit CanonicalUrl(zc::String&& canonical) noexcept;

  zc::String value;
};

}  // namespace zomlang::compiler::identity
