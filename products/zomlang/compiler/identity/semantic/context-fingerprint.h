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
#include "zomlang/compiler/identity/key/definition-key.h"
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

/// \brief Exact distribution and policy lineage for one toolchain compilation unit.
class ToolchainSemanticContextInput final {
public:
  ZC_NODISCARD static ToolchainSemanticContextInput from(
      ToolchainUnitKey toolchain, const Sha256Digest& distributionDigest,
      const Sha256Digest& policyTemplateRevision) noexcept;
  ZC_NODISCARD ToolchainSemanticContextInput clone() const noexcept;
  ZC_NODISCARD const ToolchainUnitKey& toolchain() const noexcept;
  ZC_NODISCARD const Sha256Digest& distributionDigest() const noexcept;
  ZC_NODISCARD const Sha256Digest& policyTemplateRevision() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  ToolchainSemanticContextInput(ToolchainUnitKey toolchain, const Sha256Digest& distributionDigest,
                                const Sha256Digest& policyTemplateRevision) noexcept;

  ToolchainUnitKey toolchainValue;
  Sha256Digest distributionDigestValue;
  Sha256Digest policyTemplateRevisionValue;
};

/// \brief Stable semantic coordinate for one projected Toolchain(Core) crate.
class CoreSemanticContextFingerprint final {
public:
  /// \brief Computes the narrow RFC 0025 fingerprint from an exact core projection.
  ZC_NODISCARD static zc::Maybe<CoreSemanticContextFingerprint> compute(
      const CrateKey& projectedCoreCrate);

  ZC_NODISCARD CoreSemanticContextFingerprint clone() const noexcept;
  ZC_NODISCARD const Sha256Digest& digest() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  explicit CoreSemanticContextFingerprint(const Sha256Digest& digest) noexcept;

  Sha256Digest value;
};

/// \brief Deterministic digest of one closed semantic compilation graph.
class ContextFingerprint final {
public:
  /// \brief Reconstructs a fingerprint from an independently verified canonical digest.
  ZC_NODISCARD static ContextFingerprint fromCanonicalDigest(
      const Sha256Digest& digest) noexcept;

  /// \brief Computes the RFC 0011 domain-separated fingerprint.
  /// \return None when any supposedly unique sequence contains duplicate encodings.
  ZC_NODISCARD static zc::Maybe<ContextFingerprint> compute(
      zc::ArrayPtr<const CompilationUnitIdentity> compilationUnits,
      zc::ArrayPtr<const ToolchainSemanticContextInput> toolchainInputs,
      zc::ArrayPtr<const PackageDependencyEdgeKey> packageEdges,
      zc::ArrayPtr<const CrateKey> crates, zc::ArrayPtr<const CrateDependencyEdgeKey> crateEdges,
      zc::ArrayPtr<const SourceContentIdentity> sourceContents,
      zc::ArrayPtr<const ModuleKey> modules);

  ZC_NODISCARD ContextFingerprint clone() const noexcept;
  ZC_NODISCARD const Sha256Digest& digest() const noexcept;

private:
  explicit ContextFingerprint(const Sha256Digest& digest) noexcept;

  Sha256Digest value;
};

}  // namespace zomlang::compiler::identity
