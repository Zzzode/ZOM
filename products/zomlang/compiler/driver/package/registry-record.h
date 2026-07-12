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
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/sodium-runtime.h"
#include "zomlang/compiler/driver/package/source-snapshot.h"
#include "zomlang/compiler/identity/package-key.h"

namespace zomlang::compiler::driver::package {

enum class ArchiveFormat : uint8_t { TarZstdV1 = 0x01 };

/// \brief Domain-separated identity of one Ed25519 public key.
class SigningKeyId final {
public:
  ZC_NODISCARD static SigningKeyId from(const Ed25519PublicKey& publicKey);
  ZC_NODISCARD static SigningKeyId fromDigest(const identity::Sha256Digest& digest);

  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  explicit SigningKeyId(const identity::Sha256Digest& digest) noexcept;

  identity::Sha256Digest digestValue;
};

/// \brief One key in a canonical registry trust map.
class RegistryTrustedKey final {
public:
  ZC_NODISCARD static RegistryTrustedKey from(Ed25519PublicKey&& publicKey);

  RegistryTrustedKey(RegistryTrustedKey&&) noexcept = default;
  RegistryTrustedKey& operator=(RegistryTrustedKey&&) noexcept = default;
  ZC_DISALLOW_COPY(RegistryTrustedKey);

  ZC_NODISCARD RegistryTrustedKey clone() const;
  ZC_NODISCARD const SigningKeyId& id() const noexcept;
  ZC_NODISCARD const Ed25519PublicKey& publicKey() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  RegistryTrustedKey(SigningKeyId&& id, Ed25519PublicKey&& publicKey) noexcept;

  SigningKeyId idValue;
  Ed25519PublicKey publicKeyValue;
};

/// \brief Sorted unique registry key map and its trust-domain identity.
class RegistryTrustConfiguration final {
public:
  ZC_NODISCARD static zc::Maybe<RegistryTrustConfiguration> from(
      identity::CanonicalUrl&& indexUrl, zc::Vector<RegistryTrustedKey>&& trustedKeys);

  RegistryTrustConfiguration(RegistryTrustConfiguration&&) noexcept = default;
  RegistryTrustConfiguration& operator=(RegistryTrustConfiguration&&) noexcept = default;
  ZC_DISALLOW_COPY(RegistryTrustConfiguration);

  ZC_NODISCARD const identity::RegistryIdentity& identity() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const RegistryTrustedKey> trustedKeys() const noexcept;
  ZC_NODISCARD zc::Maybe<const Ed25519PublicKey&> find(const SigningKeyId& id) const;

private:
  RegistryTrustConfiguration(identity::RegistryIdentity&& identity,
                             zc::Vector<RegistryTrustedKey>&& trustedKeys) noexcept;

  identity::RegistryIdentity identityValue;
  zc::Vector<RegistryTrustedKey> keyValues;
};

class VerifiedRegistryReleaseRecord;

/// \brief Complete signed registry release fields before signature admission.
class RegistryReleaseCandidate final {
public:
  ZC_NODISCARD static zc::Maybe<RegistryReleaseCandidate> from(
      const RegistryTrustConfiguration& trust, identity::PackageName&& package,
      identity::ResolvedVersion&& version, CanonicalManifestRecord&& manifest,
      const identity::Sha256Digest& archiveDigest,
      const DigestVerifiedSourceSnapshot& sourceSnapshot, bool yanked,
      const SigningKeyId& signingKey);

  RegistryReleaseCandidate(RegistryReleaseCandidate&&) noexcept = default;
  RegistryReleaseCandidate& operator=(RegistryReleaseCandidate&&) noexcept = default;
  ZC_DISALLOW_COPY(RegistryReleaseCandidate);

  ZC_NODISCARD zc::Array<uint8_t> signedMessage() const;
  ZC_NODISCARD zc::Maybe<VerifiedRegistryReleaseRecord> verify(
      const RegistryTrustConfiguration& trust, Ed25519Signature&& signature,
      const SodiumRuntime& sodium) &&;

private:
  friend class VerifiedRegistryReleaseRecord;

  void encodeFields(identity::CanonicalEncoder& encoder) const;

  RegistryReleaseCandidate(identity::RegistryIdentity&& registry, identity::PackageName&& package,
                           identity::ResolvedVersion&& version, CanonicalManifestRecord&& manifest,
                           const identity::Sha256Digest& manifestDigest,
                           const identity::Sha256Digest& archiveDigest,
                           const identity::Sha256Digest& sourceTreeDigest, bool yanked,
                           const SigningKeyId& signingKey) noexcept;

  identity::RegistryIdentity registryValue;
  identity::PackageName packageValue;
  identity::ResolvedVersion versionValue;
  CanonicalManifestRecord manifestValue;
  identity::Sha256Digest manifestDigestValue;
  identity::Sha256Digest archiveDigestValue;
  identity::Sha256Digest sourceTreeDigestValue;
  bool yankedValue;
  SigningKeyId signingKeyValue;
};

/// \brief Signature-verified registry release admitted to resolver input.
class VerifiedRegistryReleaseRecord final {
public:
  VerifiedRegistryReleaseRecord(VerifiedRegistryReleaseRecord&&) noexcept = default;
  VerifiedRegistryReleaseRecord& operator=(VerifiedRegistryReleaseRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedRegistryReleaseRecord);

  ZC_NODISCARD const identity::RegistryIdentity& registry() const noexcept;
  ZC_NODISCARD zc::StringPtr package() const noexcept;
  ZC_NODISCARD zc::StringPtr version() const noexcept;
  ZC_NODISCARD const CanonicalManifestRecord& manifest() const noexcept;
  ZC_NODISCARD bool yanked() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& manifestDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& archiveDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceTreeDigest() const noexcept;
  ZC_NODISCARD const SigningKeyId& signingKey() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  friend class RegistryReleaseCandidate;
  VerifiedRegistryReleaseRecord(RegistryReleaseCandidate&& candidate,
                                Ed25519Signature&& signature) noexcept;

  RegistryReleaseCandidate candidateValue;
  Ed25519Signature signatureValue;
};

}  // namespace zomlang::compiler::driver::package
