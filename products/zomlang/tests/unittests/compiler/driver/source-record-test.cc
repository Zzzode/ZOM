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

#include "sodium/crypto_sign_ed25519.h"
#include "source-archive-test-data.h"
#include "zc/core/encoding.h"
#include "zc/core/filesystem.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/driver/package/registry-record.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

identity::CanonicalUrl url() {
  auto value = identity::CanonicalUrl::fromCanonical("https://example.com/index"_zc);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical URL fixture");
}

uint8_t hexNibble(char value) {
  if (value >= '0' && value <= '9') { return static_cast<uint8_t>(value - '0'); }
  if (value >= 'a' && value <= 'f') { return static_cast<uint8_t>(value - 'a' + 10); }
  ZC_FAIL_REQUIRE("invalid hexadecimal fixture");
}

zc::Array<zc::byte> decodeHex(zc::StringPtr text) {
  ZC_REQUIRE(text.size() % 2 == 0);
  auto bytes = zc::heapArray<zc::byte>(text.size() / 2);
  for (size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] =
        static_cast<zc::byte>((hexNibble(text[index * 2]) << 4) | hexNibble(text[index * 2 + 1]));
  }
  return bytes;
}

Ed25519PublicKey publicKey(zc::byte fill) {
  auto bytes = zc::heapArray<zc::byte>(32, fill);
  auto value = Ed25519PublicKey::fromBytes(bytes.asPtr());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid public-key fixture");
}

RegistryTrustConfiguration trust(bool reverse) {
  zc::Vector<RegistryTrustedKey> keys;
  if (reverse) {
    keys.add(RegistryTrustedKey::from(publicKey(1)));
    keys.add(RegistryTrustedKey::from(publicKey(0)));
  } else {
    keys.add(RegistryTrustedKey::from(publicKey(0)));
    keys.add(RegistryTrustedKey::from(publicKey(1)));
  }
  auto value = RegistryTrustConfiguration::from(url(), zc::mv(keys));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid registry trust fixture was rejected");
}

RegistryTrustConfiguration trustWithKeys(zc::Vector<RegistryTrustedKey>&& keys) {
  auto value = RegistryTrustConfiguration::from(url(), zc::mv(keys));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid registry trust keys were rejected");
}

identity::Sha256Digest requireDigest(zc::ArrayPtr<const uint8_t> bytes) {
  auto value = identity::sha256(bytes);
  ZC_IF_SOME(admitted, value) { return admitted; }
  ZC_FAIL_REQUIRE("digest fixture length was rejected");
}

Ed25519Signature requireSignature(zc::ArrayPtr<const zc::byte> bytes) {
  auto value = Ed25519Signature::fromBytes(bytes);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("signature fixture length was rejected");
}

class TestFreshDirectory final : public FreshSourceDirectory {
public:
  explicit TestFreshDirectory(zc::Own<zc::Directory>&& root) : rootValue(zc::mv(root)) {}
  ~TestFreshDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<zc::Directory> rootValue;
};

class TestFreshDirectoryFactory final : public FreshSourceDirectoryFactory {
public:
  FreshSourceDirectoryResult create() override {
    return zc::Own<FreshSourceDirectory>(
        zc::heap<TestFreshDirectory>(zc::newInMemoryDirectory(zc::nullClock())));
  }
};

constexpr zc::StringPtr kManifest = R"toml([package]
name = "geometry"
version = "1.2.3"
edition = "2026"
)toml"_zc;

DigestVerifiedSourceSnapshot snapshot() {
  auto source = zc::newInMemoryDirectory(zc::nullClock());
  source->openFile(zc::Path("Zom.toml"_zc), zc::WriteMode::CREATE)->writeAll(kManifest);
  source
      ->openFile(zc::Path({"src"_zc, "lib.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("library"_zc);
  TestFreshDirectoryFactory factory;
  SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*source, factory);
  if (result.is<DigestVerifiedSourceSnapshot>()) {
    return zc::mv(result.get<DigestVerifiedSourceSnapshot>());
  }
  ZC_FAIL_REQUIRE("valid source-record snapshot fixture was rejected");
}

identity::CanonicalWorkspaceRelativePath manifestPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto segment = identity::CanonicalPathSegment::fromCanonical("Zom.toml"_zc);
  ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
}

NormalizedManifest manifest(const DigestVerifiedSourceSnapshot& sourceSnapshot) {
  zc::Vector<identity::CanonicalRelativePath> paths;
  for (const auto& file : sourceSnapshot.record().files()) { paths.add(file.path().clone()); }
  auto inventory = PackageSourceInventory::from(zc::mv(paths));
  ZC_IF_SOME(inventoryValue, inventory) {
    ManifestParser parser;
    auto result = parser.parseWorkspaceManifest(manifestPath(), kManifest, inventoryValue);
    if (result.is<NormalizedManifest>()) { return zc::mv(result.get<NormalizedManifest>()); }
  }
  ZC_FAIL_REQUIRE("valid source-record manifest fixture was rejected");
}

identity::PackageName packageName() {
  auto value = identity::PackageName::fromCanonical("geometry"_zc);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid package-name fixture");
}

identity::ResolvedVersion version() {
  auto value = identity::ResolvedVersion::fromCanonical("1.2.3"_zc);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid version fixture");
}

identity::PackageBaseKey localBase() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto path = identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return identity::PackageBaseKey::from(identity::CanonicalPackageSource::localPath(zc::mv(path)),
                                        packageName(), version());
}

identity::PackageBaseKey vcsBase() {
  uint8_t revisionBytes[20] = {};
  auto revision = identity::VcsRevision::from(identity::VcsRevisionAlgorithm::Sha1,
                                              zc::arrayPtr(revisionBytes));
  zc::Vector<identity::CanonicalPathSegment> segments;
  ZC_IF_SOME(revisionValue, revision) {
    return identity::PackageBaseKey::from(
        identity::CanonicalPackageSource::vcs(
            url(), zc::mv(revisionValue), identity::CanonicalRelativePath::from(zc::mv(segments))),
        packageName(), version());
  }
  ZC_FAIL_REQUIRE("invalid VCS base fixture");
}

zc::Array<uint8_t> encodeRegistry(const identity::RegistryIdentity& registry) {
  identity::CanonicalEncoder encoder;
  registry.encode(encoder);
  return encoder.finish();
}

}  // namespace

ZC_TEST("SourceRecordTest.VcsSelectorDigestDoesNotRetainRawSelector") {
  auto selector = VcsSelectorIdentity::from(url(), VcsSelectorKind::Tag, "release-1"_zc.asBytes());
  ZC_IF_SOME(value, selector) {
    ZC_EXPECT(value.kind() == VcsSelectorKind::Tag);
    ZC_EXPECT(zc::encodeHex(value.selectorDigest().bytes()) ==
              "7d09318699d16db389bcd69426b4cad2b8c4a63d09f2537cd5fca8d0de0ff8ee"_zc);
  }
  else { ZC_FAIL_EXPECT("valid VCS selector was rejected"); }
  ZC_EXPECT(VcsSelectorIdentity::from(url(), VcsSelectorKind::Revision, "immutable"_zc.asBytes()) ==
            zc::none);
  ZC_EXPECT(VcsSelectorIdentity::from(url(), VcsSelectorKind::Branch,
                                      zc::ArrayPtr<const zc::byte>()) == zc::none);
}

ZC_TEST("SourceRecordTest.RegistryTrustMapIsSortedAndPermutationInvariant") {
  auto forward = trust(false);
  auto reverse = trust(true);
  ZC_EXPECT(encodeRegistry(forward.identity()).asPtr() ==
            encodeRegistry(reverse.identity()).asPtr());
  ZC_REQUIRE(forward.trustedKeys().size() == 2);
  ZC_EXPECT(forward.trustedKeys()[0].id().digest().bytes() <
            forward.trustedKeys()[1].id().digest().bytes());
}

ZC_TEST("SourceRecordTest.RegistryTrustRejectsDuplicateSigningKey") {
  zc::Vector<RegistryTrustedKey> keys;
  keys.add(RegistryTrustedKey::from(publicKey(7)));
  keys.add(RegistryTrustedKey::from(publicKey(7)));
  ZC_EXPECT(RegistryTrustConfiguration::from(url(), zc::mv(keys)) == zc::none);

  auto key = publicKey(0);
  ZC_EXPECT(zc::encodeHex(SigningKeyId::from(key).digest().bytes()) ==
            "534ad34a0e0643f0f44f35e62f8d2b57e956b73cbf962e16aa8d190a7e5f64ec"_zc);
}

ZC_TEST("SourceRecordTest.BindsLocalAndVcsRecordsToVerifiedSnapshot") {
  auto sourceSnapshot = snapshot();
  auto local = LocalPackageRecord::from(localBase(), manifest(sourceSnapshot), sourceSnapshot);
  ZC_REQUIRE(local != zc::none);
  auto vcs = VerifiedVcsPackageRecord::from(vcsBase(), manifest(sourceSnapshot), sourceSnapshot);
  ZC_REQUIRE(vcs != zc::none);
  ZC_IF_SOME(localValue, local) {
    ZC_IF_SOME(vcsValue, vcs) {
      ZC_EXPECT(localValue.manifestDigest() == vcsValue.manifestDigest());
      ZC_EXPECT(localValue.sourceTreeDigest() == sourceSnapshot.record().digest());
      ZC_EXPECT(vcsValue.sourceTreeDigest() == sourceSnapshot.record().digest());
    }
  }
}

ZC_TEST("SourceRecordTest.VerifiesCompleteRegistryReleaseSignature") {
  auto publicBytes = decodeHex(
      "d75a980182b10ab7d54bfed3c964073a"
      "0ee172f3daa62325af021a68f707511a"_zc);
  auto secretBytes = decodeHex(
      "9d61b19deffd5a60ba844af492ec2cc4"
      "4449c5697b326919703bac031cae7f60"
      "d75a980182b10ab7d54bfed3c964073a"
      "0ee172f3daa62325af021a68f707511a"_zc);
  auto publicValue = Ed25519PublicKey::fromBytes(publicBytes.asPtr());
  zc::Vector<RegistryTrustedKey> keys;
  ZC_IF_SOME(value, publicValue) { keys.add(RegistryTrustedKey::from(zc::mv(value))); }
  ZC_REQUIRE(keys.size() == 1);
  auto trustValue = trustWithKeys(zc::mv(keys));

  auto sourceSnapshot = snapshot();
  auto normalized = manifest(sourceSnapshot);
  auto signingKey = SigningKeyId::from(trustValue.trustedKeys()[0].publicKey());
  auto archiveDigest = requireDigest(zc::arrayPtr(test::kCompressedUstar));
  auto candidate = RegistryReleaseCandidate::from(trustValue, packageName(), version(),
                                                  CanonicalManifestRecord::from(normalized),
                                                  archiveDigest, sourceSnapshot, false, signingKey);
  bool verifiedRelease = false;
  ZC_IF_SOME(candidateValue, candidate) {
    auto message = candidateValue.signedMessage();
    zc::byte signatureBytes[crypto_sign_ed25519_BYTES];
    ZC_REQUIRE(crypto_sign_ed25519_detached(signatureBytes, nullptr, message.begin(),
                                            message.size(), secretBytes.begin()) == 0);
    auto signature = requireSignature(zc::arrayPtr(signatureBytes));
    SodiumRuntime sodium;
    auto verified = zc::mv(candidateValue).verify(trustValue, zc::mv(signature), sodium);
    ZC_IF_SOME(value, verified) {
      ZC_EXPECT(value.archiveDigest() == archiveDigest);
      ZC_EXPECT(value.sourceTreeDigest() == sourceSnapshot.record().digest());
      identity::CanonicalEncoder encoder;
      value.encode(encoder);
      auto oracleDigest = identity::sha256(encoder.finish().asPtr());
      ZC_REQUIRE(oracleDigest != zc::none);
      ZC_IF_SOME(digestValue, oracleDigest) {
        ZC_EXPECT(zc::encodeHex(digestValue.bytes()) ==
                  "ed5b5ab7ea48e60b2f236a3010a967c441eccce9327b9c003a974b57a62b9103"_zc);
      }
      verifiedRelease = true;
    }
  }
  ZC_EXPECT(verifiedRelease);
}

}  // namespace zomlang::compiler::driver::package
