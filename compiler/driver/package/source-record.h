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
#include "compiler/driver/package/manifest-parser.h"
#include "compiler/driver/package/source-snapshot.h"
#include "compiler/identity/canonical/canonical-url.h"
#include "compiler/identity/key/package-key.h"

namespace zomlang::compiler::driver::package {

/// \brief Credential-free identity of one mutable VCS selector lookup.
class VcsSelectorIdentity final {
public:
  ZC_NODISCARD static zc::Maybe<VcsSelectorIdentity> from(identity::CanonicalUrl&& repository,
                                                          VcsSelectorKind kind,
                                                          zc::ArrayPtr<const zc::byte> selector);

  VcsSelectorIdentity(VcsSelectorIdentity&&) noexcept = default;
  VcsSelectorIdentity& operator=(VcsSelectorIdentity&&) noexcept = default;
  ZC_DISALLOW_COPY(VcsSelectorIdentity);

  ZC_NODISCARD VcsSelectorIdentity clone() const;
  ZC_NODISCARD VcsSelectorKind kind() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& selectorDigest() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  VcsSelectorIdentity(identity::CanonicalUrl&& repository, VcsSelectorKind kind,
                      const identity::Sha256Digest& selectorDigest) noexcept;

  identity::CanonicalUrl repositoryValue;
  VcsSelectorKind kindValue;
  identity::Sha256Digest digestValue;
};

/// \brief Immutable revision observed for one VCS selector identity.
class ResolvedVcsSelectorRecord final {
public:
  ZC_NODISCARD static ResolvedVcsSelectorRecord from(VcsSelectorIdentity&& identity,
                                                     identity::VcsRevision&& revision);

  ResolvedVcsSelectorRecord(ResolvedVcsSelectorRecord&&) noexcept = default;
  ResolvedVcsSelectorRecord& operator=(ResolvedVcsSelectorRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedVcsSelectorRecord);

  ZC_NODISCARD ResolvedVcsSelectorRecord clone() const;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  ResolvedVcsSelectorRecord(VcsSelectorIdentity&& identity,
                            identity::VcsRevision&& revision) noexcept;

  VcsSelectorIdentity identityValue;
  identity::VcsRevision revisionValue;
};

/// \brief Digest-verified VCS package admitted to resolver input.
class VerifiedVcsPackageRecord final {
public:
  ZC_NODISCARD static zc::Maybe<VerifiedVcsPackageRecord> from(
      identity::PackageBaseKey&& base, NormalizedManifest&& manifest,
      const DigestVerifiedSourceSnapshot& snapshot);

  VerifiedVcsPackageRecord(VerifiedVcsPackageRecord&&) noexcept = default;
  VerifiedVcsPackageRecord& operator=(VerifiedVcsPackageRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedVcsPackageRecord);

  ZC_NODISCARD const identity::PackageBaseKey& base() const noexcept;
  ZC_NODISCARD const NormalizedManifest& manifest() const noexcept;
  ZC_NODISCARD const CanonicalManifestRecord& canonicalManifest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& manifestDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceTreeDigest() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  VerifiedVcsPackageRecord(identity::PackageBaseKey&& base, NormalizedManifest&& manifest,
                           CanonicalManifestRecord&& canonicalManifest,
                           const identity::Sha256Digest& manifestDigest,
                           const identity::Sha256Digest& sourceTreeDigest) noexcept;

  identity::PackageBaseKey baseValue;
  NormalizedManifest manifestValue;
  CanonicalManifestRecord canonicalManifestValue;
  identity::Sha256Digest manifestDigestValue;
  identity::Sha256Digest sourceTreeDigestValue;
};

/// \brief Digest-verified local package admitted to resolver input.
class LocalPackageRecord final {
public:
  ZC_NODISCARD static zc::Maybe<LocalPackageRecord> from(
      identity::PackageBaseKey&& base, NormalizedManifest&& manifest,
      const DigestVerifiedSourceSnapshot& snapshot);

  LocalPackageRecord(LocalPackageRecord&&) noexcept = default;
  LocalPackageRecord& operator=(LocalPackageRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(LocalPackageRecord);

  ZC_NODISCARD const identity::PackageBaseKey& base() const noexcept;
  ZC_NODISCARD const NormalizedManifest& manifest() const noexcept;
  ZC_NODISCARD const CanonicalManifestRecord& canonicalManifest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& manifestDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceTreeDigest() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  LocalPackageRecord(identity::PackageBaseKey&& base, NormalizedManifest&& manifest,
                     CanonicalManifestRecord&& canonicalManifest,
                     const identity::Sha256Digest& manifestDigest,
                     const identity::Sha256Digest& sourceTreeDigest) noexcept;

  identity::PackageBaseKey baseValue;
  NormalizedManifest manifestValue;
  CanonicalManifestRecord canonicalManifestValue;
  identity::Sha256Digest manifestDigestValue;
  identity::Sha256Digest sourceTreeDigestValue;
};

}  // namespace zomlang::compiler::driver::package
