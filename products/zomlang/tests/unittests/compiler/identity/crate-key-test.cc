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

#include "zomlang/compiler/identity/crate-key.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

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

CompilationConfigKey targetCompilation() {
  zc::Maybe<BuildScriptProducerKey> output = BuildScriptProducerKey::from(repeatedDigest(0x11));
  auto value = CompilationConfigKey::from(
      CompilationDomain::Target, targetSpec(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(output));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid compilation configuration test input");
}

CrateKey crate(PackageKey&& package) {
  auto value = CrateKey::from(zc::mv(package), CrateTargetKind::Library,
                              requireScalar<TargetName>("lib"_zc), targetCompilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

CrateKey crate(zc::StringPtr packageName) { return crate(localPackage(packageName)); }

PackageDependencyEdgeKey packageEdge() {
  auto value =
      PackageDependencyEdgeKey::from(localPackage("a"_zc), requireScalar<DependencyAlias>("dep"_zc),
                                     DependencyDomain::Target, localPackage("b"_zc));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid package edge test input");
}

CrateDependencyEdgeKey crateEdge() {
  auto value = CrateDependencyEdgeKey::from(packageEdge(), crate("a"_zc), crate("b"_zc));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate edge test input");
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
  ZC_EXPECT(encoded.size() == 154);
  expectDigest(encoded.asPtr(),
               "136b0e54d7750bc21ab3e1b5f7cd1f6046fa8f5bafab919c391444a869a6c537"_zc);
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
  localPackage("a"_zc).encode(invalidKindEncoder);
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
  ZC_EXPECT(encoded.size() == 406);
  expectDigest(encoded.asPtr(),
               "64fcca3d969d5d52c170d40a8a8db32005853856b61087719d003799c2c387a5"_zc);
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

  ZC_EXPECT(CrateDependencyEdgeKey::from(packageEdge(), crate("wrong"_zc), crate("b"_zc)) ==
            zc::none);
  ZC_EXPECT(CrateDependencyEdgeKey::from(packageEdge(), crate("a"_zc), crate("wrong"_zc)) ==
            zc::none);
  ZC_EXPECT(CrateDependencyEdgeKey::from(packageEdge(),
                                         crate(localPackageWithFeature("a"_zc, "different"_zc)),
                                         crate("b"_zc)) == zc::none);

  CanonicalEncoder mismatchedConsumerEncoder;
  packageEdge().encode(mismatchedConsumerEncoder);
  crate("wrong"_zc).encode(mismatchedConsumerEncoder);
  crate("b"_zc).encode(mismatchedConsumerEncoder);
  auto mismatchedConsumerBytes = mismatchedConsumerEncoder.finish();
  CanonicalDecoder mismatchedConsumerDecoder(mismatchedConsumerBytes);
  ZC_EXPECT(CrateDependencyEdgeKey::decodeCanonical(mismatchedConsumerDecoder) == zc::none);

  CanonicalEncoder mismatchedProviderEncoder;
  packageEdge().encode(mismatchedProviderEncoder);
  crate("a"_zc).encode(mismatchedProviderEncoder);
  crate("wrong"_zc).encode(mismatchedProviderEncoder);
  auto mismatchedProviderBytes = mismatchedProviderEncoder.finish();
  CanonicalDecoder mismatchedProviderDecoder(mismatchedProviderBytes);
  ZC_EXPECT(CrateDependencyEdgeKey::decodeCanonical(mismatchedProviderDecoder) == zc::none);
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

  auto invalidKind = CrateKey::from(localPackage("a"_zc), static_cast<CrateTargetKind>(0xff),
                                    requireScalar<TargetName>("lib"_zc), targetCompilation());
  ZC_EXPECT(invalidKind == zc::none);
}

}  // namespace zomlang::compiler::identity
