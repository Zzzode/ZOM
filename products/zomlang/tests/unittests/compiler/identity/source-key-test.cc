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

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/source-snapshot.h"

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

CanonicalTargetSpecificationKey targetSpec() {
  auto value = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc), requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc), requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), 64, Endianness::Little, emptyTargetFeatures());
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

CrateKey crate(zc::StringPtr packageName) {
  auto value = CrateKey::from(CompilationUnitIdentity::userPackage(localPackage(packageName)),
                              CrateTargetKind::Library, requireScalar<TargetName>("lib"_zc),
                              targetCompilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

CrateKey coreCrate() {
  zc::Maybe<BuildScriptProducerKey> noProducer;
  auto compilation = CompilationConfigKey::from(
      CompilationDomain::Target, targetSpec(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noProducer));
  ZC_REQUIRE(compilation != zc::none);
  ZC_IF_SOME(compilationValue, compilation) {
    auto value = CrateKey::from(CompilationUnitIdentity::toolchain(ToolchainUnitKey::core()),
                                CrateTargetKind::Library, requireScalar<TargetName>("core"_zc),
                                zc::mv(compilationValue));
    ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  }
  ZC_FAIL_REQUIRE("invalid core crate test input");
}

CanonicalRelativePath logicalPath() {
  zc::Vector<CanonicalPathSegment> segments;
  segments.add(requireScalar<CanonicalPathSegment>("g.zom"_zc));
  return CanonicalRelativePath::from(zc::mv(segments));
}

SourceFileKey source(zc::StringPtr packageName = "a"_zc, uint8_t producerByte = 0x11) {
  auto origin = SourceOriginKey::generatedFile(
      BuildScriptProducerKey::from(repeatedDigest(producerByte)), logicalPath());
  return SourceFileKey::from(crate(packageName), zc::mv(origin));
}

SourceFileKey localSource() {
  zc::Vector<CanonicalPathSegment> segments;
  segments.add(requireScalar<CanonicalPathSegment>("main.zom"_zc));
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return SourceFileKey::from(crate("a"_zc), SourceOriginKey::localFile(zc::mv(path)));
}

SourceFileKey registrySource() {
  return SourceFileKey::from(
      crate("a"_zc), SourceOriginKey::registryFile(localPackage("registry"_zc), logicalPath()));
}

SourceFileKey vcsSource() {
  return SourceFileKey::from(crate("a"_zc),
                             SourceOriginKey::vcsFile(localPackage("checkout"_zc), logicalPath()));
}

SourceFileKey coreSource() {
  return SourceFileKey::from(coreCrate(),
                             SourceOriginKey::coreFile(ToolchainUnitKey::core(), logicalPath()));
}

void expectSourceRoundTrip(SourceFileKey&& key) {
  auto encoded = key.encode();
  CanonicalDecoder decoder(encoded);
  auto decoded = SourceFileKey::decodeCanonical(decoder);
  ZC_REQUIRE(decoded != zc::none);
  ZC_IF_SOME(value, decoded) { ZC_EXPECT(value.encode().asPtr() == encoded.asPtr()); }
  ZC_EXPECT(decoder.finished());
}

ModuleKey module() {
  zc::Vector<ModulePathSegment> path;
  path.add(requireScalar<ModulePathSegment>("m"_zc));
  auto value = ModuleKey::from(crate("a"_zc), zc::mv(path));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module test input");
}

ModuleKey coreModule() {
  zc::Vector<ModulePathSegment> path;
  path.add(requireScalar<ModulePathSegment>("prelude"_zc));
  auto value = ModuleKey::from(coreCrate(), zc::mv(path));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid core module test input");
}

ImmutableSourceSnapshot snapshot(SourceFileKey&& key, size_t byteCount = 1) {
  auto value =
      ImmutableSourceSnapshot::from(zc::mv(key), zc::heapArray<uint8_t>(byteCount, uint8_t{0}));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid immutable source snapshot was rejected");
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

ZC_TEST("SourceFileKey passes the fixed generated-source codec vector") {
  auto key = source();
  auto encoded = key.encode();
  ZC_EXPECT(encoded.size() == 209);
  expectDigest(encoded.asPtr(),
               "889b71e47414fb642b8af459e87a64fe99afa5d6c44ef16a92091c4f7cd5b762"_zc);
}

ZC_TEST("SourceFileKey passes the fixed toolchain core source codec vector") {
  auto key = coreSource();
  auto encoded = key.encode();
  auto crateBytes = key.crate().encode();
  ZC_EXPECT(encoded.size() == 105);
  ZC_REQUIRE(encoded.size() >= crateBytes.size() + 2);
  ZC_EXPECT(encoded.asPtr().first(crateBytes.size()) == crateBytes.asPtr());
  ZC_EXPECT(encoded[0] == static_cast<uint8_t>(CompilationUnitKind::Toolchain));
  ZC_EXPECT(encoded[1] == static_cast<uint8_t>(ToolchainComponent::Core));
  ZC_EXPECT(encoded[crateBytes.size()] == static_cast<uint8_t>(SourceOriginKind::CoreFile));
  ZC_EXPECT(encoded[crateBytes.size() + 1] == static_cast<uint8_t>(ToolchainComponent::Core));
  expectDigest(encoded.asPtr(),
               "0e8fba52f2348cedd83d7a15d58bd1878334e8f54a34bfc0c276e1e60700f9d4"_zc);
}

ZC_TEST("SourceFileKey origin and core path substitutions change the complete identity") {
  auto canonicalCore = coreSource();

  zc::Vector<CanonicalPathSegment> otherPathSegments;
  otherPathSegments.add(requireScalar<CanonicalPathSegment>("other.zom"_zc));
  auto otherCore = SourceFileKey::from(
      coreCrate(),
      SourceOriginKey::coreFile(ToolchainUnitKey::core(),
                                CanonicalRelativePath::from(zc::mv(otherPathSegments))));
  ZC_EXPECT(!canonicalCore.sameAs(otherCore));

  auto generatedCore = SourceFileKey::from(
      coreCrate(), SourceOriginKey::generatedFile(
                       BuildScriptProducerKey::from(repeatedDigest(0x11)), logicalPath()));
  ZC_EXPECT(!canonicalCore.sameAs(generatedCore));

  auto userWithCoreOrigin = SourceFileKey::from(
      crate("a"_zc), SourceOriginKey::coreFile(ToolchainUnitKey::core(), logicalPath()));
  ZC_EXPECT(!canonicalCore.sameAs(userWithCoreOrigin));
  ZC_EXPECT(!source().sameAs(userWithCoreOrigin));
}

ZC_TEST("SourceFileKey remains stable when generated contents change") {
  auto key = source();
  auto first = snapshot(key.clone(), 1);
  auto second = snapshot(key.clone(), 2);
  ZC_EXPECT(first.source().encode().asPtr() == second.source().encode().asPtr());
  ZC_EXPECT(first.contentDigest() != second.contentDigest());
}

ZC_TEST("SourceFileKey canonical decoder round trips every source-origin variant") {
  expectSourceRoundTrip(localSource());
  expectSourceRoundTrip(registrySource());
  expectSourceRoundTrip(vcsSource());
  expectSourceRoundTrip(source());
  expectSourceRoundTrip(coreSource());
}

ZC_TEST("SourceFileKey reports the canonical logical file name for every origin") {
  ZC_EXPECT(ZC_REQUIRE_NONNULL(localSource().logicalFileName()) == "main.zom"_zc);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(registrySource().logicalFileName()) == "g.zom"_zc);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(vcsSource().logicalFileName()) == "g.zom"_zc);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(source().logicalFileName()) == "g.zom"_zc);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(coreSource().logicalFileName()) == "g.zom"_zc);
}

ZC_TEST("SourceFileKey canonical decoder rejects unknown truncation and leaves outer framing") {
  CanonicalEncoder unknownOriginEncoder;
  crate("a"_zc).encode(unknownOriginEncoder);
  unknownOriginEncoder.encodeUint8(0xff);
  auto unknownOriginBytes = unknownOriginEncoder.finish();
  CanonicalDecoder unknownOriginDecoder(unknownOriginBytes);
  ZC_EXPECT(SourceFileKey::decodeCanonical(unknownOriginDecoder) == zc::none);

  auto encoded = source().encode();
  CanonicalDecoder truncatedDecoder(encoded.asPtr().first(encoded.size() - 1));
  ZC_EXPECT(SourceFileKey::decodeCanonical(truncatedDecoder) == zc::none);

  zc::Vector<uint8_t> framed(encoded.size() + 1);
  framed.addAll(encoded);
  framed.add(0xa5);
  CanonicalDecoder framedDecoder(framed.asPtr());
  ZC_EXPECT(SourceFileKey::decodeCanonical(framedDecoder) != zc::none);
  ZC_EXPECT(!framedDecoder.finished());
  ZC_EXPECT(framedDecoder.remaining() == 1);
  ZC_EXPECT(framedDecoder.decodeUint8() == 0xa5);
  ZC_EXPECT(framedDecoder.finished());

  CanonicalEncoder invalidCoreToolchainEncoder;
  coreCrate().encode(invalidCoreToolchainEncoder);
  invalidCoreToolchainEncoder.encodeUint8(static_cast<uint8_t>(SourceOriginKind::CoreFile));
  invalidCoreToolchainEncoder.encodeUint8(0xff);
  auto invalidCoreToolchainBytes = invalidCoreToolchainEncoder.finish();
  CanonicalDecoder invalidCoreToolchainDecoder(invalidCoreToolchainBytes);
  ZC_EXPECT(SourceFileKey::decodeCanonical(invalidCoreToolchainDecoder) == zc::none);
}

ZC_TEST("ModuleKey passes the fixed stable module codec vector") {
  const uint8_t expected[] = {
      0x01, 0x03, 0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
      0,    0,    0,    0,    0,    0,    1,    'a',  0,    0,    0,    0,    0,    0,    0,
      5,    '0',  '.',  '0',  '.',  '0',  0,    0,    0,    0,    0,    0,    0,    0,    0x01,
      0,    0,    0,    0,    0,    0,    0,    3,    'l',  'i',  'b',  0x02, 0,    0,    0,
      0,    0,    0,    0,    1,    'x',  0,    0,    0,    0,    0,    0,    0,    1,    'v',
      0,    0,    0,    0,    0,    0,    0,    1,    'o',  0,    0,    0,    0,    0,    0,
      0,    1,    'e',  0,    0,    0,    0,    0,    0,    0,    1,    'a',  0,    0,    0,
      64,   0x01, 0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0x07, 0xea, 0x01,
      0x00, 0x00, 0x01, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
      0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
      0x11, 0x11, 0x11, 0x11, 0x11, 0,    0,    0,    0,    0,    0,    0,    1,    0,    0,
      0,    0,    0,    0,    0,    1,    'm',
  };
  auto key = module();
  auto encoded = key.encode();
  ZC_EXPECT(encoded.size() == 172);
  ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));
  expectDigest(encoded.asPtr(),
               "be102589e44f91f4fe750453ca986e0855d38d3d32e5e5dbaacc4f5b19161804"_zc);

  CanonicalDecoder decoder(zc::arrayPtr(expected));
  auto decoded = ModuleKey::decodeCanonical(decoder);
  ZC_REQUIRE(decoded != zc::none);
  ZC_IF_SOME(value, decoded) { ZC_EXPECT(value.encode().asPtr() == zc::arrayPtr(expected)); }
  ZC_EXPECT(decoder.finished());
}

ZC_TEST("ModuleKey preserves the toolchain core compilation unit") {
  auto key = coreModule();
  auto encoded = key.encode();
  ZC_EXPECT(encoded.size() == 105);
  ZC_EXPECT(encoded[0] == static_cast<uint8_t>(CompilationUnitKind::Toolchain));
  ZC_EXPECT(encoded[1] == static_cast<uint8_t>(ToolchainComponent::Core));
  expectDigest(encoded.asPtr(),
               "e6e3ce3f73df10036990fe5df6443d621deaf475d29e6c4a44f169f1bc7c69fb"_zc);

  CanonicalDecoder decoder(encoded);
  auto decoded = ModuleKey::decodeCanonical(decoder);
  ZC_REQUIRE(decoded != zc::none);
  ZC_IF_SOME(moduleKey, decoded) {
    ZC_EXPECT(moduleKey.crate().unit().kind() == CompilationUnitKind::Toolchain);
    ZC_EXPECT(moduleKey.path().size() == 1);
    ZC_EXPECT(moduleKey.path()[0].text() == "prelude"_zc);
  }
  ZC_EXPECT(decoder.finished());
}

ZC_TEST("ModuleKey canonical decoder enforces path bounds canonical text and exact consumption") {
  auto key = module();
  auto encoded = key.encode();

  CanonicalDecoder truncatedDecoder(encoded.asPtr().first(encoded.size() - 1));
  ZC_EXPECT(ModuleKey::decodeCanonical(truncatedDecoder) == zc::none);

  CanonicalEncoder emptyPathEncoder;
  key.crate().encode(emptyPathEncoder);
  emptyPathEncoder.encodeSequenceSize(0);
  auto emptyPathBytes = emptyPathEncoder.finish();
  CanonicalDecoder emptyPathDecoder(emptyPathBytes);
  ZC_EXPECT(ModuleKey::decodeCanonical(emptyPathDecoder) == zc::none);

  CanonicalEncoder excessivePathEncoder;
  key.crate().encode(excessivePathEncoder);
  excessivePathEncoder.encodeSequenceSize(UINT64_MAX);
  auto excessivePathBytes = excessivePathEncoder.finish();
  CanonicalDecoder excessivePathDecoder(excessivePathBytes);
  ZC_EXPECT(ModuleKey::decodeCanonical(excessivePathDecoder) == zc::none);

  zc::Vector<ModulePathSegment> excessiveProducerPath;
  for (size_t index = 0; index < 257; ++index) {
    excessiveProducerPath.add(requireScalar<ModulePathSegment>("m"_zc));
  }
  ZC_EXPECT(ModuleKey::from(crate("a"_zc), zc::mv(excessiveProducerPath)) == zc::none);

  CanonicalEncoder nonCanonicalPathEncoder;
  key.crate().encode(nonCanonicalPathEncoder);
  nonCanonicalPathEncoder.encodeSequenceSize(1);
  nonCanonicalPathEncoder.encodeByteString("caf\x65\xCC\x81"_zc.asBytes());
  auto nonCanonicalPathBytes = nonCanonicalPathEncoder.finish();
  CanonicalDecoder nonCanonicalPathDecoder(nonCanonicalPathBytes);
  ZC_EXPECT(ModuleKey::decodeCanonical(nonCanonicalPathDecoder) == zc::none);

  auto maximumSegmentText = zc::heapString(4096);
  for (size_t index = 0; index < maximumSegmentText.size(); ++index) {
    maximumSegmentText[index] = 'a';
  }
  CanonicalEncoder oversizedKeyEncoder;
  key.crate().encode(oversizedKeyEncoder);
  oversizedKeyEncoder.encodeSequenceSize(4);
  for (size_t index = 0; index < 4; ++index) {
    requireScalar<ModulePathSegment>(maximumSegmentText).encode(oversizedKeyEncoder);
  }
  auto oversizedKeyBytes = oversizedKeyEncoder.finish();
  CanonicalDecoder oversizedKeyDecoder(oversizedKeyBytes);
  ZC_EXPECT(ModuleKey::decodeCanonical(oversizedKeyDecoder) == zc::none);

  zc::Vector<ModulePathSegment> oversizedProducerKeyPath;
  for (size_t index = 0; index < 4; ++index) {
    oversizedProducerKeyPath.add(requireScalar<ModulePathSegment>(maximumSegmentText));
  }
  ZC_EXPECT(ModuleKey::from(crate("a"_zc), zc::mv(oversizedProducerKeyPath)) == zc::none);

  zc::Vector<uint8_t> framed(encoded.size() + 1);
  framed.addAll(encoded);
  framed.add(0xa5);
  CanonicalDecoder framedDecoder(framed.asPtr());
  ZC_EXPECT(ModuleKey::decodeCanonical(framedDecoder) != zc::none);
  ZC_EXPECT(!framedDecoder.finished());
  ZC_EXPECT(framedDecoder.remaining() == 1);
  ZC_EXPECT(framedDecoder.decodeUint8() == 0xa5);
  ZC_EXPECT(framedDecoder.finished());
}

ZC_TEST("ModuleKey exposes its immutable canonical path") {
  zc::Vector<ModulePathSegment> path;
  path.add(requireScalar<ModulePathSegment>("graphics"_zc));
  path.add(requireScalar<ModulePathSegment>("shapes"_zc));
  auto value = ModuleKey::from(crate("a"_zc), zc::mv(path));
  ZC_REQUIRE(value != zc::none);
  ZC_IF_SOME(key, value) {
    const auto canonicalPath = key.path();
    ZC_REQUIRE(canonicalPath.size() == 2);
    ZC_EXPECT(canonicalPath[0].text() == "graphics"_zc);
    ZC_EXPECT(canonicalPath[1].text() == "shapes"_zc);
  }
}

ZC_TEST("SourceSpan and ModuleKey reject malformed values") {
  auto validSnapshot = snapshot(source());
  ZC_EXPECT(validSnapshot.span(2, 1) == zc::none);
  ZC_EXPECT(validSnapshot.span(0, 2) == zc::none);
  ZC_EXPECT(validSnapshot.unbrandedRange(0, 2) == zc::none);
  ZC_EXPECT(validSnapshot.span(0, 1) != zc::none);
  ZC_EXPECT(validSnapshot.unbrandedRange(0, 1) != zc::none);

  zc::Vector<ModulePathSegment> emptyPath;
  ZC_EXPECT(ModuleKey::from(crate("a"_zc), zc::mv(emptyPath)) == zc::none);
}

}  // namespace zomlang::compiler::identity
