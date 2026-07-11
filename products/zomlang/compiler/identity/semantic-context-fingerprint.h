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
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/semantic-identity-registry-set.h"
#include "zomlang/compiler/identity/source-snapshot.h"

namespace zomlang::compiler::identity {

/// \brief Immutable source content participating in semantic context identity.
class SourceContentIdentity final {
public:
  SourceContentIdentity(SourceContentIdentity&&) noexcept = default;
  SourceContentIdentity& operator=(SourceContentIdentity&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceContentIdentity);

  ZC_NODISCARD static SourceContentIdentity from(const ImmutableSourceSnapshot& snapshot);
  ZC_NODISCARD bool sameSourceAs(const SourceContentIdentity& other) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  SourceContentIdentity(SourceFileKey&& source, const Sha256Digest& contentDigest) noexcept;

  SourceFileKey sourceValue;
  Sha256Digest digestValue;
};

/// \brief Deterministic digest of one closed semantic compilation graph.
class SemanticContextFingerprint final {
public:
  /// \brief Computes the RFC 0011 domain-separated fingerprint.
  /// \return None when any supposedly unique sequence contains duplicate encodings.
  ZC_NODISCARD static zc::Maybe<SemanticContextFingerprint> compute(
      zc::ArrayPtr<const PackageKey> packages,
      zc::ArrayPtr<const PackageDependencyEdgeKey> packageEdges,
      zc::ArrayPtr<const CrateKey> crates,
      zc::ArrayPtr<const CrateDependencyEdgeKey> crateEdges,
      zc::ArrayPtr<const SourceContentIdentity> sourceContents,
      zc::ArrayPtr<const ModuleKey> modules);

  /// \brief Computes from the frozen context registries and resolved edge inventories.
  ZC_NODISCARD static zc::Maybe<SemanticContextFingerprint> compute(
      const SemanticIdentityRegistrySet& registries,
      zc::ArrayPtr<const PackageDependencyEdgeKey> packageEdges,
      zc::ArrayPtr<const CrateDependencyEdgeKey> crateEdges);

  ZC_NODISCARD const Sha256Digest& digest() const noexcept;

private:
  explicit SemanticContextFingerprint(const Sha256Digest& digest) noexcept;

  Sha256Digest value;
};

}  // namespace zomlang::compiler::identity
