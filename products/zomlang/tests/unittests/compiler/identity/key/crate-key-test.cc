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

#include "zomlang/compiler/identity/key/crate-key.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

template <typename T>
concept HasPackageAccessor = requires(const T& value) { value.package(); };

template <typename T>
concept HasCompilationUnitAccessor = requires(const T& value) { value.unit(); };

template <typename T>
concept AcceptsPackageParent = requires(T&& package, TargetName&& target,
                                        CompilationConfigKey&& compilation) {
  CrateKey::from(zc::mv(package), CrateTargetKind::Library, zc::mv(target), zc::mv(compilation));
};

static_assert(!HasPackageAccessor<CrateKey>);
static_assert(HasCompilationUnitAccessor<CrateKey>);
static_assert(!AcceptsPackageParent<PackageKey>);

template <typename Scalar>
Scalar requireScalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical scalar test input");
}

ResolvedVersion requireVersion() {
  auto value = ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid version test input");
}

SortedFeatureSet emptyPackageFeatures() {
  zc::Vector<FeatureName> features;
  auto value = SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty package feature set was rejected");
}

SortedTargetFeatureSet emptyTargetFeatures() {
  zc::Vector<TargetFeatureName> features;
  auto value = SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty target feature set was rejected");
}

PackageKey localPackage(zc::StringPtr name) {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return PackageKey::from(CanonicalPackageSource::localPath(zc::mv(path)),
                          requireScalar<PackageName>(name), requireVersion(),
                          emptyPackageFeatures());
}

PackageKey localPackageWithFeature(zc::StringPtr name, zc::StringPtr feature) {
  zc::Vector<FeatureName> features;
  features.add(requireScalar<FeatureName>(feature));
  auto admittedFeatures = SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(featureSet, admittedFeatures) {
    zc::Vector<CanonicalPathSegment> segments;
    auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
    return PackageKey::from(CanonicalPackageSource::localPath(zc::mv(path)),
                            requireScalar<PackageName>(name), requireVersion(), zc::mv(featureSet));
  }
  ZC_FAIL_REQUIRE("invalid package feature test input");
}

CanonicalTargetSpecificationKey targetSpec(uint32_t pointerWidth = 64,
                                           Endianness endianness = Endianness::Little) {
  auto value = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc), requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc), requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), pointerWidth, endianness, emptyTargetFeatures());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid target specification test input");
}

Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto digest = Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(value, digest) { return value; }
  ZC_FAIL_REQUIRE("invalid digest test input");
}

CompilationConfigKey compilation(CompilationDomain domain = CompilationDomain::Target,
                                 uint32_t pointerWidth = 64, uint32_t editionYear = 2026,
                                 bool useUnicode = true, bool hasBuildScriptProducer = true) {
  zc::Maybe<BuildScriptProducerKey> output;
  if (hasBuildScriptProducer) { output = BuildScriptProducerKey::from(repeatedDigest(0x11)); }
  auto value = CompilationConfigKey::from(
      domain, targetSpec(pointerWidth),
      SemanticCompilerOptionsKey::from(editionYear, useUnicode, false, false), zc::mv(output));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid compilation configuration test input");
}

CompilationConfigKey targetCompilation() { return compilation(); }

CrateKey crate(PackageKey&& package) {
  auto value = CrateKey::from(CompilationUnitIdentity::userPackage(zc::mv(package)),
                              CrateTargetKind::Library, requireScalar<TargetName>("lib"_zc),
                              targetCompilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

CrateKey crate(zc::StringPtr packageName) { return crate(localPackage(packageName)); }

CrateKey crateWith(zc::StringPtr packageName, CompilationDomain domain, CrateTargetKind kind) {
  auto value = CrateKey::from(CompilationUnitIdentity::userPackage(localPackage(packageName)), kind,
                              requireScalar<TargetName>("consumer"_zc), compilation(domain));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid configured crate test input");
}

CrateKey coreCrate(CompilationDomain domain = CompilationDomain::Target, uint32_t pointerWidth = 64,
                   bool useUnicode = true) {
  auto value = CrateKey::from(CompilationUnitIdentity::toolchain(ToolchainUnitKey::core()),
                              CrateTargetKind::Library, requireScalar<TargetName>("core"_zc),
                              compilation(domain, pointerWidth, 2026, useUnicode, false));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid core crate test input");
}

PackageDependencyEdgeKey packageEdge() {
  auto value =
      PackageDependencyEdgeKey::from(localPackage("a"_zc), requireScalar<DependencyAlias>("dep"_zc),
                                     DependencyDomain::Target, localPackage("b"_zc));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid package edge test input");
}

CrateDependencyEdgeKey crateEdge() {
  auto value = CrateDependencyEdgeKey::from(CrateDependencyOrigin::userPackage(packageEdge()),
                                            crate("a"_zc), crate("b"_zc));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate edge test input");
}

CrateDependencyEdgeKey coreCrateEdge() {
  auto value = CrateDependencyEdgeKey::from(CrateDependencyOrigin::toolchainCore(), crate("a"_zc),
                                            coreCrate());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid core crate edge test input");
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

}  // namespace

ZC_TEST("CrateKey passes the fixed composite codec vector") {
  auto key = crate("a"_zc);
  auto encoded = key.encode();
  ZC_EXPECT(encoded.size() == 155);
  ZC_EXPECT(encoded[0] == static_cast<uint8_t>(CompilationUnitKind::UserPackage));
  expectDigest(encoded.asPtr(),
               "48ea005fa923ecc5684b0468487080220ad7efb3ebc2024cec844f2300b721d8"_zc);
}

ZC_TEST("CrateKey admits only the canonical toolchain core shape") {
  auto key = coreCrate();
  auto encoded = key.encode();
  ZC_EXPECT(encoded.size() == 82);
  ZC_EXPECT(encoded[0] == static_cast<uint8_t>(CompilationUnitKind::Toolchain));
  ZC_EXPECT(encoded[1] == static_cast<uint8_t>(ToolchainComponent::Core));
  ZC_EXPECT(key.unit().kind() == CompilationUnitKind::Toolchain);
  ZC_EXPECT(key.targetKind() == CrateTargetKind::Library);
  ZC_EXPECT(key.targetName() == "core"_zc);
  ZC_EXPECT(key.semanticOptions().editionYear() == 2026);
  ZC_EXPECT(!key.compilation().hasBuildScriptProducer());
  expectDigest(encoded.asPtr(),
               "07b4c079e87d9e12ea989a1e4fcaf35f3cba416e7d863467a3195cd4276256c1"_zc);

  ZC_EXPECT(CrateKey::from(CompilationUnitIdentity::toolchain(ToolchainUnitKey::core()),
                           CrateTargetKind::Binary, requireScalar<TargetName>("core"_zc),
                           compilation(CompilationDomain::Target, 64, 2026, true, false)) ==
            zc::none);
  ZC_EXPECT(CrateKey::from(CompilationUnitIdentity::toolchain(ToolchainUnitKey::core()),
                           CrateTargetKind::Library, requireScalar<TargetName>("other"_zc),
                           compilation(CompilationDomain::Target, 64, 2026, true, false)) ==
            zc::none);
  ZC_EXPECT(CrateKey::from(CompilationUnitIdentity::toolchain(ToolchainUnitKey::core()),
                           CrateTargetKind::Library, requireScalar<TargetName>("core"_zc),
                           compilation(CompilationDomain::Target, 64, 2025, true, false)) ==
            zc::none);
  ZC_EXPECT(CrateKey::from(CompilationUnitIdentity::toolchain(ToolchainUnitKey::core()),
                           CrateTargetKind::Library, requireScalar<TargetName>("core"_zc),
                           compilation()) == zc::none);
}

ZC_TEST("CrateKey derives the exact toolchain core projection for every accepted consumer") {
  const auto targetConsumer =
      crateWith("target"_zc, CompilationDomain::Target, CrateTargetKind::Binary);
  auto targetCore = projectToolchainCoreCrate(targetConsumer);
  ZC_REQUIRE(targetCore != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(targetCore).compilation().domain() == CompilationDomain::Target);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(targetCore).semanticOptions().editionYear() == 2026);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(targetCore).semanticOptions().useUnicode() ==
            targetConsumer.semanticOptions().useUnicode());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(targetCore).semanticOptions().allowDollarIdentifiers() ==
            targetConsumer.semanticOptions().allowDollarIdentifiers());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(targetCore).semanticOptions().supportRegexLiterals() ==
            targetConsumer.semanticOptions().supportRegexLiterals());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(targetCore).compilation().target().architecture() ==
            targetConsumer.compilation().target().architecture());

  const auto hostConsumer =
      crateWith("host"_zc, CompilationDomain::Host, CrateTargetKind::BuildScript);
  auto hostCore = projectToolchainCoreCrate(hostConsumer);
  ZC_REQUIRE(hostCore != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(hostCore).compilation().domain() == CompilationDomain::Host);

  ZC_EXPECT(projectToolchainCoreCrate(coreCrate()) == zc::none);
}

ZC_TEST("CrateKey canonical decoder round trips the complete compilation key") {
  auto key = crate("a"_zc);
  auto encoded = key.encode();
  CanonicalDecoder decoder(encoded);
  auto decoded = CrateKey::decodeCanonical(decoder);
  ZC_REQUIRE(decoded != zc::none);
  ZC_IF_SOME(value, decoded) { ZC_EXPECT(value.encode().asPtr() == encoded.asPtr()); }
  ZC_EXPECT(decoder.finished());
}

ZC_TEST("CrateKey canonical decoder rejects closed-value and optional-tag mutations") {
  CanonicalEncoder invalidKindEncoder;
  CompilationUnitIdentity::userPackage(localPackage("a"_zc)).encode(invalidKindEncoder);
  invalidKindEncoder.encodeUint8(0xff);
  auto invalidKindBytes = invalidKindEncoder.finish();
  CanonicalDecoder invalidKindDecoder(invalidKindBytes);
  ZC_EXPECT(CrateKey::decodeCanonical(invalidKindDecoder) == zc::none);

  CanonicalEncoder invalidDomainEncoder;
  invalidDomainEncoder.encodeUint8(0xff);
  auto invalidDomainBytes = invalidDomainEncoder.finish();
  CanonicalDecoder invalidDomainDecoder(invalidDomainBytes);
  ZC_EXPECT(CompilationConfigKey::decodeCanonical(invalidDomainDecoder) == zc::none);

  CanonicalEncoder invalidEndianEncoder;
  requireScalar<TargetComponentName>("x"_zc).encode(invalidEndianEncoder);
  requireScalar<TargetComponentName>("v"_zc).encode(invalidEndianEncoder);
  requireScalar<TargetComponentName>("o"_zc).encode(invalidEndianEncoder);
  requireScalar<TargetComponentName>("e"_zc).encode(invalidEndianEncoder);
  requireScalar<TargetComponentName>("a"_zc).encode(invalidEndianEncoder);
  invalidEndianEncoder.encodeUint32(64);
  invalidEndianEncoder.encodeUint8(0xff);
  auto invalidEndianBytes = invalidEndianEncoder.finish();
  CanonicalDecoder invalidEndianDecoder(invalidEndianBytes);
  ZC_EXPECT(CanonicalTargetSpecificationKey::decodeCanonical(invalidEndianDecoder) == zc::none);

  CanonicalEncoder invalidWidthEncoder;
  requireScalar<TargetComponentName>("x"_zc).encode(invalidWidthEncoder);
  requireScalar<TargetComponentName>("v"_zc).encode(invalidWidthEncoder);
  requireScalar<TargetComponentName>("o"_zc).encode(invalidWidthEncoder);
  requireScalar<TargetComponentName>("e"_zc).encode(invalidWidthEncoder);
  requireScalar<TargetComponentName>("a"_zc).encode(invalidWidthEncoder);
  invalidWidthEncoder.encodeUint32(7);
  invalidWidthEncoder.encodeUint8(static_cast<uint8_t>(Endianness::Little));
  invalidWidthEncoder.encodeSequenceSize(0);
  auto invalidWidthBytes = invalidWidthEncoder.finish();
  CanonicalDecoder invalidWidthDecoder(invalidWidthBytes);
  ZC_EXPECT(CanonicalTargetSpecificationKey::decodeCanonical(invalidWidthDecoder) == zc::none);

  CanonicalEncoder invalidBooleanEncoder;
  invalidBooleanEncoder.encodeUint32(2026);
  invalidBooleanEncoder.encodeUint8(0x02);
  auto invalidBooleanBytes = invalidBooleanEncoder.finish();
  CanonicalDecoder invalidBooleanDecoder(invalidBooleanBytes);
  ZC_EXPECT(SemanticCompilerOptionsKey::decodeCanonical(invalidBooleanDecoder) == zc::none);

  CanonicalEncoder invalidOptionalEncoder;
  invalidOptionalEncoder.encodeUint8(static_cast<uint8_t>(CompilationDomain::Target));
  targetSpec().encode(invalidOptionalEncoder);
  SemanticCompilerOptionsKey::from(2026, true, false, false).encode(invalidOptionalEncoder);
  invalidOptionalEncoder.encodeUint8(0x02);
  auto invalidOptionalBytes = invalidOptionalEncoder.finish();
  CanonicalDecoder invalidOptionalDecoder(invalidOptionalBytes);
  ZC_EXPECT(CompilationConfigKey::decodeCanonical(invalidOptionalDecoder) == zc::none);
}

ZC_TEST("Target canonical decoder rejects unsorted duplicate and excessive features") {
  auto encodeTargetPrefix = [](CanonicalEncoder& encoder) {
    requireScalar<TargetComponentName>("x"_zc).encode(encoder);
    requireScalar<TargetComponentName>("v"_zc).encode(encoder);
    requireScalar<TargetComponentName>("o"_zc).encode(encoder);
    requireScalar<TargetComponentName>("e"_zc).encode(encoder);
    requireScalar<TargetComponentName>("a"_zc).encode(encoder);
    encoder.encodeUint32(64);
    encoder.encodeUint8(static_cast<uint8_t>(Endianness::Little));
  };

  CanonicalEncoder unsortedEncoder;
  encodeTargetPrefix(unsortedEncoder);
  unsortedEncoder.encodeSequenceSize(2);
  requireScalar<TargetFeatureName>("sse4"_zc).encode(unsortedEncoder);
  requireScalar<TargetFeatureName>("avx2"_zc).encode(unsortedEncoder);
  auto unsortedBytes = unsortedEncoder.finish();
  CanonicalDecoder unsortedDecoder(unsortedBytes);
  ZC_EXPECT(CanonicalTargetSpecificationKey::decodeCanonical(unsortedDecoder) == zc::none);

  CanonicalEncoder duplicateEncoder;
  encodeTargetPrefix(duplicateEncoder);
  duplicateEncoder.encodeSequenceSize(2);
  requireScalar<TargetFeatureName>("avx2"_zc).encode(duplicateEncoder);
  requireScalar<TargetFeatureName>("avx2"_zc).encode(duplicateEncoder);
  auto duplicateBytes = duplicateEncoder.finish();
  CanonicalDecoder duplicateDecoder(duplicateBytes);
  ZC_EXPECT(CanonicalTargetSpecificationKey::decodeCanonical(duplicateDecoder) == zc::none);

  CanonicalEncoder excessiveEncoder;
  encodeTargetPrefix(excessiveEncoder);
  excessiveEncoder.encodeSequenceSize(UINT64_MAX);
  auto excessiveBytes = excessiveEncoder.finish();
  CanonicalDecoder excessiveDecoder(excessiveBytes);
  ZC_EXPECT(CanonicalTargetSpecificationKey::decodeCanonical(excessiveDecoder) == zc::none);
}

ZC_TEST("CrateDependencyEdgeKey preserves the complete package edge") {
  auto key = crateEdge();
  auto encoded = key.encode();
  ZC_EXPECT(encoded.size() == 409);
  ZC_EXPECT(encoded[0] == static_cast<uint8_t>(CrateDependencyOriginKind::UserPackage));
  expectDigest(encoded.asPtr(),
               "2305626539d957d62f37cc6fd9dcfa7e4326b0d503bfb29021d36472e7fff67f"_zc);
}

ZC_TEST("CrateDependencyEdgeKey canonical decoder enforces package endpoint coherence") {
  auto key = crateEdge();
  auto encoded = key.encode();
  CanonicalDecoder decoder(encoded);
  auto decoded = CrateDependencyEdgeKey::decodeCanonical(decoder);
  ZC_REQUIRE(decoded != zc::none);
  ZC_IF_SOME(value, decoded) { ZC_EXPECT(value.encode().asPtr() == encoded.asPtr()); }
  ZC_EXPECT(decoder.finished());

  CanonicalDecoder truncatedDecoder(encoded.asPtr().first(encoded.size() - 1));
  ZC_EXPECT(CrateDependencyEdgeKey::decodeCanonical(truncatedDecoder) == zc::none);

  ZC_EXPECT(CrateDependencyEdgeKey::from(CrateDependencyOrigin::userPackage(packageEdge()),
                                         crate("wrong"_zc), crate("b"_zc)) == zc::none);
  ZC_EXPECT(CrateDependencyEdgeKey::from(CrateDependencyOrigin::userPackage(packageEdge()),
                                         crate("a"_zc), crate("wrong"_zc)) == zc::none);
  ZC_EXPECT(CrateDependencyEdgeKey::from(CrateDependencyOrigin::userPackage(packageEdge()),
                                         crate(localPackageWithFeature("a"_zc, "different"_zc)),
                                         crate("b"_zc)) == zc::none);

  CanonicalEncoder mismatchedConsumerEncoder;
  CrateDependencyOrigin::userPackage(packageEdge()).encode(mismatchedConsumerEncoder);
  crate("wrong"_zc).encode(mismatchedConsumerEncoder);
  crate("b"_zc).encode(mismatchedConsumerEncoder);
  auto mismatchedConsumerBytes = mismatchedConsumerEncoder.finish();
  CanonicalDecoder mismatchedConsumerDecoder(mismatchedConsumerBytes);
  ZC_EXPECT(CrateDependencyEdgeKey::decodeCanonical(mismatchedConsumerDecoder) == zc::none);

  CanonicalEncoder mismatchedProviderEncoder;
  CrateDependencyOrigin::userPackage(packageEdge()).encode(mismatchedProviderEncoder);
  crate("a"_zc).encode(mismatchedProviderEncoder);
  crate("wrong"_zc).encode(mismatchedProviderEncoder);
  auto mismatchedProviderBytes = mismatchedProviderEncoder.finish();
  CanonicalDecoder mismatchedProviderDecoder(mismatchedProviderBytes);
  ZC_EXPECT(CrateDependencyEdgeKey::decodeCanonical(mismatchedProviderDecoder) == zc::none);
}

ZC_TEST("CrateDependencyOrigin is exhaustive and preserves complete user package edges") {
  auto userOrigin = CrateDependencyOrigin::userPackage(packageEdge());
  CanonicalEncoder userEncoder;
  userOrigin.encode(userEncoder);
  auto userBytes = userEncoder.finish();
  ZC_EXPECT(userBytes.size() == 99);
  ZC_EXPECT(userBytes[0] == static_cast<uint8_t>(CrateDependencyOriginKind::UserPackage));
  ZC_EXPECT(userOrigin.kind() == CrateDependencyOriginKind::UserPackage);
  ZC_EXPECT(userOrigin.userPackageEdge().encode().size() == 98);

  const uint8_t coreBytes[] = {0x02};
  CanonicalEncoder coreEncoder;
  CrateDependencyOrigin::toolchainCore().encode(coreEncoder);
  ZC_EXPECT(coreEncoder.finish().asPtr() == zc::arrayPtr(coreBytes));

  CanonicalDecoder userDecoder(userBytes);
  auto decodedUser = CrateDependencyOrigin::decodeCanonical(userDecoder);
  ZC_REQUIRE(decodedUser != zc::none);
  ZC_IF_SOME(origin, decodedUser) {
    ZC_EXPECT(origin.kind() == CrateDependencyOriginKind::UserPackage);
    ZC_EXPECT(origin.userPackageEdge().encode().asPtr() ==
              userOrigin.userPackageEdge().encode().asPtr());
  }
  ZC_EXPECT(userDecoder.finished());

  CanonicalDecoder coreDecoder(zc::arrayPtr(coreBytes));
  auto decodedCore = CrateDependencyOrigin::decodeCanonical(coreDecoder);
  ZC_REQUIRE(decodedCore != zc::none);
  ZC_IF_SOME(origin, decodedCore) {
    ZC_EXPECT(origin.kind() == CrateDependencyOriginKind::ToolchainCore);
  }
  ZC_EXPECT(coreDecoder.finished());

  const uint8_t unknown[] = {0xff};
  const uint8_t missingUserPayload[] = {0x01};
  CanonicalDecoder unknownDecoder(zc::arrayPtr(unknown));
  CanonicalDecoder missingUserPayloadDecoder(zc::arrayPtr(missingUserPayload));
  ZC_EXPECT(CrateDependencyOrigin::decodeCanonical(unknownDecoder) == zc::none);
  ZC_EXPECT(CrateDependencyOrigin::decodeCanonical(missingUserPayloadDecoder) == zc::none);
}

ZC_TEST("Toolchain core crate edges enforce provenance endpoints and semantic projection") {
  auto key = coreCrateEdge();
  auto encoded = key.encode();
  ZC_EXPECT(encoded.size() == 238);
  ZC_EXPECT(encoded[0] == static_cast<uint8_t>(CrateDependencyOriginKind::ToolchainCore));
  expectDigest(encoded.asPtr(),
               "5b2031ee1f8a9e801aff7d6f2dd0c408b5ee1fa00266cbabeebf6b44e69ebc44"_zc);

  CanonicalDecoder decoder(encoded);
  auto decoded = CrateDependencyEdgeKey::decodeCanonical(decoder);
  ZC_REQUIRE(decoded != zc::none);
  ZC_IF_SOME(edge, decoded) {
    ZC_EXPECT(edge.origin().kind() == CrateDependencyOriginKind::ToolchainCore);
    ZC_EXPECT(edge.consumer().unit().kind() == CompilationUnitKind::UserPackage);
    ZC_EXPECT(edge.provider().unit().kind() == CompilationUnitKind::Toolchain);
  }
  ZC_EXPECT(decoder.finished());

  ZC_EXPECT(CrateDependencyEdgeKey::from(CrateDependencyOrigin::toolchainCore(), coreCrate(),
                                         coreCrate()) == zc::none);
  ZC_EXPECT(CrateDependencyEdgeKey::from(CrateDependencyOrigin::toolchainCore(), crate("a"_zc),
                                         crate("b"_zc)) == zc::none);
  ZC_EXPECT(CrateDependencyEdgeKey::from(CrateDependencyOrigin::toolchainCore(), crate("a"_zc),
                                         coreCrate(CompilationDomain::Host)) == zc::none);
  ZC_EXPECT(CrateDependencyEdgeKey::from(CrateDependencyOrigin::toolchainCore(), crate("a"_zc),
                                         coreCrate(CompilationDomain::Target, 32)) == zc::none);
  ZC_EXPECT(CrateDependencyEdgeKey::from(CrateDependencyOrigin::toolchainCore(), crate("a"_zc),
                                         coreCrate(CompilationDomain::Target, 64, false)) ==
            zc::none);
  ZC_EXPECT(CrateDependencyEdgeKey::from(
                CrateDependencyOrigin::toolchainCore(),
                crateWith("a"_zc, CompilationDomain::Target, CrateTargetKind::BuildScript),
                coreCrate()) == zc::none);
  ZC_EXPECT(CrateDependencyEdgeKey::from(CrateDependencyOrigin::userPackage(packageEdge()),
                                         coreCrate(), crate("b"_zc)) == zc::none);
  ZC_EXPECT(CrateDependencyEdgeKey::from(CrateDependencyOrigin::userPackage(packageEdge()),
                                         crate("a"_zc), coreCrate()) == zc::none);

  CanonicalEncoder reversedEncoder;
  CrateDependencyOrigin::toolchainCore().encode(reversedEncoder);
  coreCrate().encode(reversedEncoder);
  crate("a"_zc).encode(reversedEncoder);
  auto reversedBytes = reversedEncoder.finish();
  CanonicalDecoder reversedDecoder(reversedBytes);
  ZC_EXPECT(CrateDependencyEdgeKey::decodeCanonical(reversedDecoder) == zc::none);

  CanonicalDecoder truncatedDecoder(encoded.asPtr().first(encoded.size() - 1));
  ZC_EXPECT(CrateDependencyEdgeKey::decodeCanonical(truncatedDecoder) == zc::none);

  zc::Vector<uint8_t> framed(encoded.size() + 1);
  framed.addAll(encoded);
  framed.add(0xa5);
  CanonicalDecoder framedDecoder(framed.asPtr());
  ZC_EXPECT(CrateDependencyEdgeKey::decodeCanonical(framedDecoder) != zc::none);
  ZC_EXPECT(framedDecoder.remaining() == 1);
}

ZC_TEST("Crate target keys reject unknown closed values and invalid pointer widths") {
  auto invalidWidth = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc), requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc), requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), 7, Endianness::Little, emptyTargetFeatures());
  ZC_EXPECT(invalidWidth == zc::none);

  auto invalidEndian = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc), requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc), requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), 64, static_cast<Endianness>(0xff),
      emptyTargetFeatures());
  ZC_EXPECT(invalidEndian == zc::none);

  zc::Maybe<BuildScriptProducerKey> noOutput;
  auto invalidDomain = CompilationConfigKey::from(
      static_cast<CompilationDomain>(0xff), targetSpec(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noOutput));
  ZC_EXPECT(invalidDomain == zc::none);

  auto invalidKind = CrateKey::from(CompilationUnitIdentity::userPackage(localPackage("a"_zc)),
                                    static_cast<CrateTargetKind>(0xff),
                                    requireScalar<TargetName>("lib"_zc), targetCompilation());
  ZC_EXPECT(invalidKind == zc::none);
}

}  // namespace zomlang::compiler::identity
