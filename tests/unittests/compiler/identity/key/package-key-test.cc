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

#include "compiler/identity/key/package-key.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/key/compilation-unit-key.h"

namespace zomlang::compiler::identity {
namespace {

PackageName requirePackageName(zc::StringPtr text) {
  auto value = PackageName::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid package-name test input");
}

DependencyAlias requireDependencyAlias(zc::StringPtr text) {
  auto value = DependencyAlias::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid dependency-alias test input");
}

ResolvedVersion requireVersion(zc::StringPtr text) {
  auto value = ResolvedVersion::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid version test input");
}

template <typename Scalar>
Scalar requireScalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical scalar test input");
}

CanonicalUrl requireUrl(zc::StringPtr text) {
  auto value = CanonicalUrl::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical URL test input");
}

SortedFeatureSet emptyFeatures() {
  zc::Vector<FeatureName> features;
  auto value = SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty feature set was rejected");
}

PackageKey localPackage(zc::StringPtr name) {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  auto source = CanonicalPackageSource::localPath(zc::mv(path));
  return PackageKey::from(zc::mv(source), requirePackageName(name), requireVersion("0.0.0"_zc),
                          emptyFeatures());
}

Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto digest = Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(value, digest) { return value; }
  ZC_FAIL_REQUIRE("invalid digest test input");
}

void encodeLocalPackagePrefix(CanonicalEncoder& encoder) {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  CanonicalPackageSource::localPath(zc::mv(path)).encode(encoder);
  requirePackageName("a"_zc).encode(encoder);
  requireVersion("0.0.0"_zc).encode(encoder);
}

void expectDigest(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr expected) {
  auto digest = sha256(bytes);
  bool matched = false;
  ZC_IF_SOME(value, digest) {
    ZC_EXPECT(zc::encodeHex(value.bytes()) == expected);
    matched = true;
  }
  ZC_EXPECT(matched);
}

void expectDependencyEdgeRoundTrip(DependencyDomain domain) {
  auto admitted = PackageDependencyEdgeKey::from(
      localPackage("a"_zc), requireDependencyAlias("dep"_zc), domain, localPackage("b"_zc));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(edge, admitted) {
    auto encoded = edge.encode();
    CanonicalDecoder decoder(encoded);
    auto decoded = PackageDependencyEdgeKey::decodeCanonical(decoder);
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(value, decoded) { ZC_EXPECT(value.encode().asPtr() == encoded.asPtr()); }
    ZC_EXPECT(decoder.finished());
  }
}

}  // namespace

ZC_TEST("PackageKey passes the fixed local package codec vector") {
  const uint8_t expected[] = {
      0x03, 0, 0, 0, 0, 0, 0, 0, 0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 1, 'a',
      0,    0, 0, 0, 0, 0, 0, 5, '0', '.', '0', '.', '0', 0, 0, 0, 0, 0, 0, 0, 0,
  };
  auto package = localPackage("a"_zc);
  auto encoded = package.encode();
  ZC_EXPECT(encoded.size() == 43);
  ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));
  expectDigest(encoded.asPtr(),
               "b0c7b4f55c7faf6d4522b3a6f81e979347436c782d29ad2eeaa09985479d40a6"_zc);
}

ZC_TEST("CompilationUnitIdentity passes fixed user and toolchain codec vectors") {
  const uint8_t expectedUser[] = {
      0x01, 0x03, 0, 0, 0, 0, 0, 0, 0, 0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 1,
      'a',  0,    0, 0, 0, 0, 0, 0, 5, '0', '.', '0', '.', '0', 0, 0, 0, 0, 0, 0, 0, 0,
  };
  const uint8_t expectedToolchain[] = {0x02, 0x01};

  auto user = CompilationUnitIdentity::userPackage(localPackage("a"_zc));
  auto userBytes = user.encode();
  ZC_EXPECT(user.kind() == CompilationUnitKind::UserPackage);
  ZC_EXPECT(user.userPackage().name() == "a"_zc);
  ZC_EXPECT(userBytes.asPtr() == zc::arrayPtr(expectedUser));

  auto toolchain = CompilationUnitIdentity::toolchain(ToolchainUnitKey::core());
  auto toolchainBytes = toolchain.encode();
  ZC_EXPECT(toolchain.kind() == CompilationUnitKind::Toolchain);
  ZC_EXPECT(toolchain.toolchain().component() == ToolchainComponent::Core);
  ZC_EXPECT(toolchainBytes.asPtr() == zc::arrayPtr(expectedToolchain));
}

ZC_TEST("ToolchainUnitKey uses an exact domain-separated standalone codec") {
  const uint8_t expected[] = {'z', 'o', 'm', '.', 't', 'o', 'o', 'l', 'c', 'h', 'a',  'i',
                              'n', '-', 'c', 'o', 'r', 'e', '-', 'k', 'e', 'y', 0x00, 0x01};
  auto encoded = ToolchainUnitKey::core().encode();
  ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));

  auto decoded = ToolchainUnitKey::decode(encoded);
  ZC_REQUIRE(decoded != zc::none);
  ZC_IF_SOME(key, decoded) { ZC_EXPECT(key.component() == ToolchainComponent::Core); }

  const uint8_t missingComponent[] = {'z', 'o', 'm', '.', 't', 'o', 'o', 'l', 'c', 'h', 'a', 'i',
                                      'n', '-', 'c', 'o', 'r', 'e', '-', 'k', 'e', 'y', 0x00};
  const uint8_t unknownComponent[] = {'z', 'o', 'm', '.', 't', 'o', 'o', 'l', 'c', 'h', 'a',  'i',
                                      'n', '-', 'c', 'o', 'r', 'e', '-', 'k', 'e', 'y', 0x00, 0xff};
  const uint8_t wrongDomain[] = {'z', 'o', 'm', '.', 'u', 's', 'e', 'r', 0x00, 0x01};
  const uint8_t trailing[] = {'z', 'o', 'm', '.', 't', 'o', 'o', 'l', 'c', 'h',  'a',  'i', 'n',
                              '-', 'c', 'o', 'r', 'e', '-', 'k', 'e', 'y', 0x00, 0x01, 0xa5};
  ZC_EXPECT(ToolchainUnitKey::decode(zc::arrayPtr(missingComponent)) == zc::none);
  ZC_EXPECT(ToolchainUnitKey::decode(zc::arrayPtr(unknownComponent)) == zc::none);
  ZC_EXPECT(ToolchainUnitKey::decode(zc::arrayPtr(wrongDomain)) == zc::none);
  ZC_EXPECT(ToolchainUnitKey::decode(zc::arrayPtr(trailing)) == zc::none);
}

ZC_TEST("CompilationUnitIdentity canonical decoder is exhaustive and compositional") {
  auto userBytes = CompilationUnitIdentity::userPackage(localPackage("a"_zc)).encode();
  CanonicalDecoder userDecoder(userBytes);
  auto user = CompilationUnitIdentity::decodeCanonical(userDecoder);
  ZC_REQUIRE(user != zc::none);
  ZC_IF_SOME(unit, user) {
    ZC_EXPECT(unit.kind() == CompilationUnitKind::UserPackage);
    ZC_EXPECT(unit.userPackage().name() == "a"_zc);
  }
  ZC_EXPECT(userDecoder.finished());

  const uint8_t coreRecord[] = {0x02, 0x01};
  CanonicalDecoder coreDecoder(zc::arrayPtr(coreRecord));
  auto core = CompilationUnitIdentity::decodeCanonical(coreDecoder);
  ZC_REQUIRE(core != zc::none);
  ZC_IF_SOME(unit, core) {
    ZC_EXPECT(unit.kind() == CompilationUnitKind::Toolchain);
    ZC_EXPECT(unit.toolchain().component() == ToolchainComponent::Core);
  }
  ZC_EXPECT(coreDecoder.finished());

  const uint8_t unknownKind[] = {0xff};
  const uint8_t missingUser[] = {0x01};
  const uint8_t missingToolchain[] = {0x02};
  const uint8_t unknownToolchain[] = {0x02, 0xff};
  CanonicalDecoder unknownKindDecoder(zc::arrayPtr(unknownKind));
  CanonicalDecoder missingUserDecoder(zc::arrayPtr(missingUser));
  CanonicalDecoder missingToolchainDecoder(zc::arrayPtr(missingToolchain));
  CanonicalDecoder unknownToolchainDecoder(zc::arrayPtr(unknownToolchain));
  ZC_EXPECT(CompilationUnitIdentity::decodeCanonical(unknownKindDecoder) == zc::none);
  ZC_EXPECT(CompilationUnitIdentity::decodeCanonical(missingUserDecoder) == zc::none);
  ZC_EXPECT(CompilationUnitIdentity::decodeCanonical(missingToolchainDecoder) == zc::none);
  ZC_EXPECT(CompilationUnitIdentity::decodeCanonical(unknownToolchainDecoder) == zc::none);
}

ZC_TEST("A user package named core is distinct from the toolchain core") {
  auto user = CompilationUnitIdentity::userPackage(localPackage("core"_zc));
  auto toolchain = CompilationUnitIdentity::toolchain(ToolchainUnitKey::core());
  ZC_EXPECT(user.kind() == CompilationUnitKind::UserPackage);
  ZC_EXPECT(user.userPackage().name() == "core"_zc);
  ZC_EXPECT(user.encode().asPtr() != toolchain.encode().asPtr());
}

ZC_TEST("PackageKey canonical decoder composes all package source variants") {
  auto local = localPackage("a"_zc);
  auto localBytes = local.encode();
  CanonicalDecoder localDecoder(localBytes);
  auto decodedLocal = PackageKey::decodeCanonical(localDecoder);
  ZC_REQUIRE(decodedLocal != zc::none);
  ZC_IF_SOME(value, decodedLocal) { ZC_EXPECT(value.encode().asPtr() == localBytes.asPtr()); }
  ZC_EXPECT(localDecoder.finished());

  auto registrySource = CanonicalPackageSource::registry(
      RegistryIdentity::from(requireUrl("https://example.com/index"_zc), repeatedDigest(0x22)));
  auto registry = PackageKey::from(zc::mv(registrySource), requirePackageName("registry"_zc),
                                   requireVersion("1.2.3"_zc), emptyFeatures());
  auto registryBytes = registry.encode();
  CanonicalDecoder registryDecoder(registryBytes);
  auto decodedRegistry = PackageKey::decodeCanonical(registryDecoder);
  ZC_REQUIRE(decodedRegistry != zc::none);
  ZC_IF_SOME(value, decodedRegistry) { ZC_EXPECT(value.encode().asPtr() == registryBytes.asPtr()); }
  ZC_EXPECT(registryDecoder.finished());

  uint8_t revisionBytes[20] = {};
  auto revision = VcsRevision::from(VcsRevisionAlgorithm::Sha1, zc::arrayPtr(revisionBytes));
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(revisionValue, revision) {
    zc::Vector<CanonicalPathSegment> segments;
    segments.add(requireScalar<CanonicalPathSegment>("compiler"_zc));
    auto source = CanonicalPackageSource::vcs(requireUrl("ssh://example.com/repository"_zc),
                                              zc::mv(revisionValue),
                                              CanonicalRelativePath::from(zc::mv(segments)));
    auto vcs = PackageKey::from(zc::mv(source), requirePackageName("checkout"_zc),
                                requireVersion("2.0.0"_zc), emptyFeatures());
    auto vcsBytes = vcs.encode();
    CanonicalDecoder vcsDecoder(vcsBytes);
    auto decodedVcs = PackageKey::decodeCanonical(vcsDecoder);
    ZC_REQUIRE(decodedVcs != zc::none);
    ZC_IF_SOME(value, decodedVcs) { ZC_EXPECT(value.encode().asPtr() == vcsBytes.asPtr()); }
    ZC_EXPECT(vcsDecoder.finished());
  }
}

ZC_TEST("PackageKey canonical decoder rejects malformed unions paths and feature sets") {
  const uint8_t invalidSource[] = {0xff};
  CanonicalDecoder invalidSourceDecoder(zc::arrayPtr(invalidSource));
  ZC_EXPECT(PackageKey::decodeCanonical(invalidSourceDecoder) == zc::none);

  CanonicalEncoder invalidRevisionEncoder;
  invalidRevisionEncoder.encodeUint8(0xff);
  auto invalidRevisionBytes = invalidRevisionEncoder.finish();
  CanonicalDecoder invalidRevisionDecoder(invalidRevisionBytes);
  ZC_EXPECT(VcsRevision::decodeCanonical(invalidRevisionDecoder) == zc::none);

  CanonicalEncoder excessivePathEncoder;
  excessivePathEncoder.encodeSequenceSize(UINT64_MAX);
  auto excessivePathBytes = excessivePathEncoder.finish();
  CanonicalDecoder excessivePathDecoder(excessivePathBytes);
  ZC_EXPECT(CanonicalRelativePath::decodeCanonical(excessivePathDecoder) == zc::none);

  CanonicalEncoder unsortedEncoder;
  encodeLocalPackagePrefix(unsortedEncoder);
  unsortedEncoder.encodeSequenceSize(2);
  requireScalar<FeatureName>("alpha"_zc).encode(unsortedEncoder);
  requireScalar<FeatureName>("beta"_zc).encode(unsortedEncoder);
  auto unsortedBytes = unsortedEncoder.finish();
  CanonicalDecoder unsortedDecoder(unsortedBytes);
  ZC_EXPECT(PackageKey::decodeCanonical(unsortedDecoder) == zc::none);

  CanonicalEncoder duplicateEncoder;
  encodeLocalPackagePrefix(duplicateEncoder);
  duplicateEncoder.encodeSequenceSize(2);
  requireScalar<FeatureName>("same"_zc).encode(duplicateEncoder);
  requireScalar<FeatureName>("same"_zc).encode(duplicateEncoder);
  auto duplicateBytes = duplicateEncoder.finish();
  CanonicalDecoder duplicateDecoder(duplicateBytes);
  ZC_EXPECT(PackageKey::decodeCanonical(duplicateDecoder) == zc::none);

  CanonicalEncoder excessiveFeaturesEncoder;
  encodeLocalPackagePrefix(excessiveFeaturesEncoder);
  excessiveFeaturesEncoder.encodeSequenceSize(UINT64_MAX);
  auto excessiveFeaturesBytes = excessiveFeaturesEncoder.finish();
  CanonicalDecoder excessiveFeaturesDecoder(excessiveFeaturesBytes);
  ZC_EXPECT(PackageKey::decodeCanonical(excessiveFeaturesDecoder) == zc::none);
}

ZC_TEST("PackageBaseKey omits feature activation from the coordinate codec") {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  auto base = PackageBaseKey::from(CanonicalPackageSource::localPath(zc::mv(path)),
                                   requirePackageName("a"_zc), requireVersion("0.0.0"_zc));
  const auto encoded = base.encode();
  ZC_EXPECT(encoded.size() == 35);
  expectDigest(encoded.asPtr(),
               "b5f5a6cf5c9bd24c96447bd81e14907905aa66142bd73df0d73da9bc8db223ee"_zc);
}

ZC_TEST("PackageDependencyEdgeKey passes the fixed target edge codec vector") {
  auto admitted =
      PackageDependencyEdgeKey::from(localPackage("a"_zc), requireDependencyAlias("dep"_zc),
                                     DependencyDomain::Target, localPackage("b"_zc));
  bool matched = false;
  ZC_IF_SOME(edge, admitted) {
    auto encoded = edge.encode();
    ZC_EXPECT(encoded.size() == 98);
    expectDigest(encoded.asPtr(),
                 "b4a6fdda29af9e3c0b0d6a21b062aa94be3315bc47bde3f432d46e85766b2751"_zc);
    matched = true;
  }
  ZC_EXPECT(matched);

  ZC_EXPECT(PackageDependencyEdgeKey::from(localPackage("a"_zc), requireDependencyAlias("dep"_zc),
                                           static_cast<DependencyDomain>(0xff),
                                           localPackage("b"_zc)) == zc::none);
}

ZC_TEST("PackageDependencyEdgeKey canonical decoder is compositional and fail closed") {
  expectDependencyEdgeRoundTrip(DependencyDomain::Target);
  expectDependencyEdgeRoundTrip(DependencyDomain::Development);
  expectDependencyEdgeRoundTrip(DependencyDomain::Build);

  auto admitted =
      PackageDependencyEdgeKey::from(localPackage("a"_zc), requireDependencyAlias("dep"_zc),
                                     DependencyDomain::Target, localPackage("b"_zc));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(edge, admitted) {
    auto encoded = edge.encode();
    CanonicalDecoder truncatedDecoder(encoded.asPtr().first(encoded.size() - 1));
    ZC_EXPECT(PackageDependencyEdgeKey::decodeCanonical(truncatedDecoder) == zc::none);
  }

  CanonicalEncoder invalidDomainEncoder;
  localPackage("a"_zc).encode(invalidDomainEncoder);
  requireDependencyAlias("dep"_zc).encode(invalidDomainEncoder);
  invalidDomainEncoder.encodeUint8(0xff);
  localPackage("b"_zc).encode(invalidDomainEncoder);
  auto invalidDomainBytes = invalidDomainEncoder.finish();
  CanonicalDecoder invalidDomainDecoder(invalidDomainBytes);
  ZC_EXPECT(PackageDependencyEdgeKey::decodeCanonical(invalidDomainDecoder) == zc::none);
}

ZC_TEST("Canonical package paths preserve strong normalized segments") {
  auto first = CanonicalPathSegment::fromSource("caf\x65\xCC\x81"_zc);
  bool admitted = false;
  ZC_IF_SOME(segment, first) {
    zc::Vector<CanonicalPathSegment> segments;
    segments.add(zc::mv(segment));
    auto path = CanonicalWorkspaceRelativePath::from(2, zc::mv(segments));
    auto duplicate = path.clone();
    ZC_EXPECT(duplicate.leadingParents() == 2);
    ZC_REQUIRE(duplicate.segments().size() == 1);
    ZC_EXPECT(duplicate.segments()[0].text() == "caf\xC3\xA9"_zc);
    admitted = true;
  }
  ZC_EXPECT(admitted);
}

ZC_TEST("VcsRevision enforces closed digest widths") {
  uint8_t sha1Bytes[20] = {};
  uint8_t sha256Bytes[32] = {};
  ZC_EXPECT(VcsRevision::from(VcsRevisionAlgorithm::Sha1, zc::arrayPtr(sha1Bytes)) != zc::none);
  ZC_EXPECT(VcsRevision::from(VcsRevisionAlgorithm::Sha256, zc::arrayPtr(sha256Bytes)) != zc::none);
  ZC_EXPECT(VcsRevision::from(VcsRevisionAlgorithm::Sha1, zc::arrayPtr(sha256Bytes)) == zc::none);
  ZC_EXPECT(VcsRevision::from(VcsRevisionAlgorithm::Sha256, zc::arrayPtr(sha1Bytes)) == zc::none);
  ZC_EXPECT(VcsRevision::from(static_cast<VcsRevisionAlgorithm>(0xff), zc::arrayPtr(sha256Bytes)) ==
            zc::none);
}

}  // namespace zomlang::compiler::identity
