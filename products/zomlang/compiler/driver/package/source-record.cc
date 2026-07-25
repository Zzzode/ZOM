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

#include "zomlang/compiler/driver/package/source-record.h"

#include "zomlang/compiler/identity/canonical-encoder.h"

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

zc::Maybe<identity::Sha256Digest> computeManifestDigest(const CanonicalManifestRecord& manifest) {
  return domainDigest("zom.normalized-manifest"_zc, manifest.encode().asPtr());
}

}  // namespace

VcsSelectorIdentity::VcsSelectorIdentity(identity::CanonicalUrl&& repository, VcsSelectorKind kind,
                                         const identity::Sha256Digest& selectorDigest) noexcept
    : repositoryValue(zc::mv(repository)), kindValue(kind), digestValue(selectorDigest) {}

zc::Maybe<VcsSelectorIdentity> VcsSelectorIdentity::from(identity::CanonicalUrl&& repository,
                                                         VcsSelectorKind kind,
                                                         zc::ArrayPtr<const zc::byte> selector) {
  if ((kind != VcsSelectorKind::Tag && kind != VcsSelectorKind::Branch) || selector.size() == 0) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(kind));
  encoder.encodeByteString(selector);
  ZC_IF_SOME(digest, domainDigest("zom.vcs-selector"_zc, encoder.finish().asPtr())) {
    return VcsSelectorIdentity(zc::mv(repository), kind, digest);
  }
  return zc::none;
}

VcsSelectorIdentity VcsSelectorIdentity::clone() const {
  return VcsSelectorIdentity(repositoryValue.clone(), kindValue, digestValue);
}
VcsSelectorKind VcsSelectorIdentity::kind() const noexcept { return kindValue; }
const identity::Sha256Digest& VcsSelectorIdentity::selectorDigest() const noexcept {
  return digestValue;
}
void VcsSelectorIdentity::encode(identity::CanonicalEncoder& encoder) const {
  repositoryValue.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(kindValue));
  encoder.encodeDigest(digestValue);
}
zc::Array<uint8_t> VcsSelectorIdentity::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

ResolvedVcsSelectorRecord::ResolvedVcsSelectorRecord(VcsSelectorIdentity&& identity,
                                                     identity::VcsRevision&& revision) noexcept
    : identityValue(zc::mv(identity)), revisionValue(zc::mv(revision)) {}
ResolvedVcsSelectorRecord ResolvedVcsSelectorRecord::from(VcsSelectorIdentity&& identity,
                                                          identity::VcsRevision&& revision) {
  return ResolvedVcsSelectorRecord(zc::mv(identity), zc::mv(revision));
}
ResolvedVcsSelectorRecord ResolvedVcsSelectorRecord::clone() const {
  return ResolvedVcsSelectorRecord(identityValue.clone(), revisionValue.clone());
}
void ResolvedVcsSelectorRecord::encode(identity::CanonicalEncoder& encoder) const {
  identityValue.encode(encoder);
  revisionValue.encode(encoder);
}
zc::Array<uint8_t> ResolvedVcsSelectorRecord::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

VerifiedVcsPackageRecord::VerifiedVcsPackageRecord(
    identity::PackageBaseKey&& base, NormalizedManifest&& manifest,
    CanonicalManifestRecord&& canonicalManifest, const identity::Sha256Digest& manifestDigest,
    const identity::Sha256Digest& sourceTreeDigest) noexcept
    : baseValue(zc::mv(base)),
      manifestValue(zc::mv(manifest)),
      canonicalManifestValue(zc::mv(canonicalManifest)),
      manifestDigestValue(manifestDigest),
      sourceTreeDigestValue(sourceTreeDigest) {}

zc::Maybe<VerifiedVcsPackageRecord> VerifiedVcsPackageRecord::from(
    identity::PackageBaseKey&& base, NormalizedManifest&& manifest,
    const DigestVerifiedSourceSnapshot& snapshot) {
  if (base.source().kind() != identity::PackageSourceKind::Vcs) { return zc::none; }
  auto canonical = CanonicalManifestRecord::from(manifest);
  ZC_IF_SOME(digest, computeManifestDigest(canonical)) {
    return VerifiedVcsPackageRecord(zc::mv(base), zc::mv(manifest), zc::mv(canonical), digest,
                                    snapshot.record().digest());
  }
  return zc::none;
}
const identity::PackageBaseKey& VerifiedVcsPackageRecord::base() const noexcept {
  return baseValue;
}
const NormalizedManifest& VerifiedVcsPackageRecord::manifest() const noexcept {
  return manifestValue;
}
const CanonicalManifestRecord& VerifiedVcsPackageRecord::canonicalManifest() const noexcept {
  return canonicalManifestValue;
}
const identity::Sha256Digest& VerifiedVcsPackageRecord::manifestDigest() const noexcept {
  return manifestDigestValue;
}
const identity::Sha256Digest& VerifiedVcsPackageRecord::sourceTreeDigest() const noexcept {
  return sourceTreeDigestValue;
}
void VerifiedVcsPackageRecord::encode(identity::CanonicalEncoder& encoder) const {
  baseValue.encode(encoder);
  canonicalManifestValue.encode(encoder);
  encoder.encodeDigest(manifestDigestValue);
  encoder.encodeDigest(sourceTreeDigestValue);
}

LocalPackageRecord::LocalPackageRecord(identity::PackageBaseKey&& base,
                                       NormalizedManifest&& manifest,
                                       CanonicalManifestRecord&& canonicalManifest,
                                       const identity::Sha256Digest& manifestDigest,
                                       const identity::Sha256Digest& sourceTreeDigest) noexcept
    : baseValue(zc::mv(base)),
      manifestValue(zc::mv(manifest)),
      canonicalManifestValue(zc::mv(canonicalManifest)),
      manifestDigestValue(manifestDigest),
      sourceTreeDigestValue(sourceTreeDigest) {}

zc::Maybe<LocalPackageRecord> LocalPackageRecord::from(
    identity::PackageBaseKey&& base, NormalizedManifest&& manifest,
    const DigestVerifiedSourceSnapshot& snapshot) {
  if (base.source().kind() != identity::PackageSourceKind::LocalPath) { return zc::none; }
  auto canonical = CanonicalManifestRecord::from(manifest);
  ZC_IF_SOME(digest, computeManifestDigest(canonical)) {
    return LocalPackageRecord(zc::mv(base), zc::mv(manifest), zc::mv(canonical), digest,
                              snapshot.record().digest());
  }
  return zc::none;
}
const identity::PackageBaseKey& LocalPackageRecord::base() const noexcept { return baseValue; }
const NormalizedManifest& LocalPackageRecord::manifest() const noexcept { return manifestValue; }
const CanonicalManifestRecord& LocalPackageRecord::canonicalManifest() const noexcept {
  return canonicalManifestValue;
}
const identity::Sha256Digest& LocalPackageRecord::manifestDigest() const noexcept {
  return manifestDigestValue;
}
const identity::Sha256Digest& LocalPackageRecord::sourceTreeDigest() const noexcept {
  return sourceTreeDigestValue;
}
void LocalPackageRecord::encode(identity::CanonicalEncoder& encoder) const {
  baseValue.encode(encoder);
  canonicalManifestValue.encode(encoder);
  encoder.encodeDigest(manifestDigestValue);
  encoder.encodeDigest(sourceTreeDigestValue);
}

}  // namespace zomlang::compiler::driver::package
