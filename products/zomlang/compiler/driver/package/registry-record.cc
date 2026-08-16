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

#include "zomlang/compiler/driver/package/registry-record.h"

#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

zc::Maybe<identity::Sha256Digest> domainDigest(zc::StringPtr domain,
                                               zc::ArrayPtr<const uint8_t> encoded) {
  identity::Sha256Hasher hasher;
  const uint8_t separator = 0;
  if (!hasher.update(domain.asBytes()) || !hasher.update(zc::arrayPtr(&separator, 1)) ||
      !hasher.update(encoded)) {
    return zc::none;
  }
  return hasher.finish();
}

bool idLess(const RegistryTrustedKey& left, const RegistryTrustedKey& right) {
  return left.id().digest().bytes() < right.id().digest().bytes();
}

bool sameRegistry(const identity::RegistryIdentity& left, const identity::RegistryIdentity& right) {
  identity::CanonicalEncoder leftEncoder;
  identity::CanonicalEncoder rightEncoder;
  left.encode(leftEncoder);
  right.encode(rightEncoder);
  return leftEncoder.finish().asPtr() == rightEncoder.finish().asPtr();
}

}  // namespace

SigningKeyId::SigningKeyId(const identity::Sha256Digest& digest) noexcept : digestValue(digest) {}

SigningKeyId SigningKeyId::from(const Ed25519PublicKey& publicKey) {
  ZC_IF_SOME(digest, domainDigest("zom.ed25519-key"_zc, publicKey.bytes())) {
    return SigningKeyId(digest);
  }
  ZC_UNREACHABLE
}
SigningKeyId SigningKeyId::fromDigest(const identity::Sha256Digest& digest) {
  return SigningKeyId(digest);
}
const identity::Sha256Digest& SigningKeyId::digest() const noexcept { return digestValue; }
void SigningKeyId::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeDigest(digestValue);
}

RegistryTrustedKey::RegistryTrustedKey(SigningKeyId&& id, Ed25519PublicKey&& publicKey) noexcept
    : idValue(zc::mv(id)), publicKeyValue(zc::mv(publicKey)) {}
RegistryTrustedKey RegistryTrustedKey::from(Ed25519PublicKey&& publicKey) {
  auto id = SigningKeyId::from(publicKey);
  return RegistryTrustedKey(zc::mv(id), zc::mv(publicKey));
}
RegistryTrustedKey RegistryTrustedKey::clone() const {
  return RegistryTrustedKey(SigningKeyId::from(publicKeyValue), publicKeyValue.clone());
}
const SigningKeyId& RegistryTrustedKey::id() const noexcept { return idValue; }
const Ed25519PublicKey& RegistryTrustedKey::publicKey() const noexcept { return publicKeyValue; }
void RegistryTrustedKey::encode(identity::CanonicalEncoder& encoder) const {
  idValue.encode(encoder);
  encoder.encodeByteString(publicKeyValue.bytes());
}

RegistryTrustConfiguration::RegistryTrustConfiguration(
    identity::RegistryIdentity&& identity, zc::Vector<RegistryTrustedKey>&& trustedKeys) noexcept
    : identityValue(zc::mv(identity)), keyValues(zc::mv(trustedKeys)) {}

zc::Maybe<RegistryTrustConfiguration> RegistryTrustConfiguration::from(
    identity::CanonicalUrl&& indexUrl, zc::Vector<RegistryTrustedKey>&& trustedKeys) {
  for (size_t index = 1; index < trustedKeys.size(); ++index) {
    auto current = zc::mv(trustedKeys[index]);
    size_t insertion = index;
    while (insertion != 0 && idLess(current, trustedKeys[insertion - 1])) {
      trustedKeys[insertion] = zc::mv(trustedKeys[insertion - 1]);
      --insertion;
    }
    trustedKeys[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < trustedKeys.size(); ++index) {
    if (trustedKeys[index - 1].id().digest() == trustedKeys[index].id().digest()) {
      return zc::none;
    }
  }
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(trustedKeys.size());
  for (const auto& key : trustedKeys) { key.encode(encoder); }
  ZC_IF_SOME(trustDomain, domainDigest("zom.registry-trust"_zc, encoder.finish().asPtr())) {
    return RegistryTrustConfiguration(
        identity::RegistryIdentity::from(zc::mv(indexUrl), trustDomain), zc::mv(trustedKeys));
  }
  return zc::none;
}
const identity::RegistryIdentity& RegistryTrustConfiguration::identity() const noexcept {
  return identityValue;
}
zc::ArrayPtr<const RegistryTrustedKey> RegistryTrustConfiguration::trustedKeys() const noexcept {
  return keyValues;
}
zc::Maybe<const Ed25519PublicKey&> RegistryTrustConfiguration::find(const SigningKeyId& id) const {
  for (const auto& key : keyValues) {
    if (key.id().digest() == id.digest()) { return key.publicKey(); }
  }
  return zc::none;
}

RegistryReleaseCandidate::RegistryReleaseCandidate(
    identity::RegistryIdentity&& registry, identity::PackageName&& package,
    identity::ResolvedVersion&& version, CanonicalManifestRecord&& manifest,
    const identity::Sha256Digest& manifestDigest, const identity::Sha256Digest& archiveDigest,
    const identity::Sha256Digest& sourceTreeDigest, bool yanked,
    const SigningKeyId& signingKey) noexcept
    : registryValue(zc::mv(registry)),
      packageValue(zc::mv(package)),
      versionValue(zc::mv(version)),
      manifestValue(zc::mv(manifest)),
      manifestDigestValue(manifestDigest),
      archiveDigestValue(archiveDigest),
      sourceTreeDigestValue(sourceTreeDigest),
      yankedValue(yanked),
      signingKeyValue(signingKey) {}

zc::Maybe<RegistryReleaseCandidate> RegistryReleaseCandidate::from(
    const RegistryTrustConfiguration& trust, identity::PackageName&& package,
    identity::ResolvedVersion&& version, CanonicalManifestRecord&& manifest,
    const identity::Sha256Digest& archiveDigest, const DigestVerifiedSourceSnapshot& sourceSnapshot,
    bool yanked, const SigningKeyId& signingKey) {
  ZC_IF_SOME(digest, domainDigest("zom.normalized-manifest"_zc, manifest.encode().asPtr())) {
    return RegistryReleaseCandidate(trust.identity().clone(), zc::mv(package), zc::mv(version),
                                    zc::mv(manifest), digest, archiveDigest,
                                    sourceSnapshot.record().digest(), yanked, signingKey);
  }
  return zc::none;
}

zc::Array<uint8_t> RegistryReleaseCandidate::signedMessage() const {
  identity::CanonicalEncoder fields;
  encodeFields(fields);
  auto encoded = fields.finish();
  zc::Vector<uint8_t> message;
  message.addAll("zom.registry-release"_zc.asBytes());
  message.add(0);
  message.addAll(encoded);
  return message.releaseAsArray();
}

void RegistryReleaseCandidate::encodeFields(identity::CanonicalEncoder& fields) const {
  registryValue.encode(fields);
  packageValue.encode(fields);
  versionValue.encode(fields);
  manifestValue.encode(fields);
  fields.encodeDigest(manifestDigestValue);
  fields.encodeUint8(static_cast<uint8_t>(ArchiveFormat::TarZstd));
  fields.encodeDigest(archiveDigestValue);
  fields.encodeDigest(sourceTreeDigestValue);
  fields.encodeBool(yankedValue);
  signingKeyValue.encode(fields);
}

zc::Maybe<VerifiedRegistryReleaseRecord> RegistryReleaseCandidate::verify(
    const RegistryTrustConfiguration& trust, Ed25519Signature&& signature,
    const SodiumRuntime& sodium) && {
  if (!sameRegistry(registryValue, trust.identity())) { return zc::none; }
  ZC_IF_SOME(publicKey, trust.find(signingKeyValue)) {
    const auto message = signedMessage();
    if (sodium.verifyEd25519(publicKey, signature, message.asPtr())) {
      return VerifiedRegistryReleaseRecord(zc::mv(*this), zc::mv(signature));
    }
  }
  return zc::none;
}

VerifiedRegistryReleaseRecord::VerifiedRegistryReleaseRecord(RegistryReleaseCandidate&& candidate,
                                                             Ed25519Signature&& signature) noexcept
    : candidateValue(zc::mv(candidate)), signatureValue(zc::mv(signature)) {}
const identity::RegistryIdentity& VerifiedRegistryReleaseRecord::registry() const noexcept {
  return candidateValue.registryValue;
}
zc::StringPtr VerifiedRegistryReleaseRecord::package() const noexcept {
  return candidateValue.packageValue.text();
}
zc::StringPtr VerifiedRegistryReleaseRecord::version() const noexcept {
  return candidateValue.versionValue.text();
}
const CanonicalManifestRecord& VerifiedRegistryReleaseRecord::manifest() const noexcept {
  return candidateValue.manifestValue;
}
bool VerifiedRegistryReleaseRecord::yanked() const noexcept { return candidateValue.yankedValue; }
const identity::Sha256Digest& VerifiedRegistryReleaseRecord::manifestDigest() const noexcept {
  return candidateValue.manifestDigestValue;
}
const identity::Sha256Digest& VerifiedRegistryReleaseRecord::archiveDigest() const noexcept {
  return candidateValue.archiveDigestValue;
}
const identity::Sha256Digest& VerifiedRegistryReleaseRecord::sourceTreeDigest() const noexcept {
  return candidateValue.sourceTreeDigestValue;
}
const SigningKeyId& VerifiedRegistryReleaseRecord::signingKey() const noexcept {
  return candidateValue.signingKeyValue;
}
void VerifiedRegistryReleaseRecord::encode(identity::CanonicalEncoder& encoder) const {
  candidateValue.encodeFields(encoder);
  encoder.encodeByteString(signatureValue.bytes());
}

}  // namespace zomlang::compiler::driver::package
