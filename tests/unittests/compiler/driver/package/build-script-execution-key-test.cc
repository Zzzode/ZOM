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
#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "compiler/driver/package/build-script-runtime.h"
#include "compiler/driver/package/manifest-parser.h"
#include "compiler/driver/package/trusted-runtime-elf.h"
#include "compiler/driver/package/trusted-runtime-manifest.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/ir/target-registry.h"

namespace zomlang::compiler::driver::package {
namespace {

void putElf16(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
}

void putElf32(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint32_t value) {
  for (uint32_t index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8U));
  }
}

void putElf64(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint64_t value) {
  for (uint32_t index = 0; index < 8; ++index) {
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8U));
  }
}

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid execution-key scalar fixture");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("empty feature set was rejected");
}

identity::PackageKey package(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto source = identity::CanonicalPackageSource::localPath(
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments)));
  return identity::PackageKey::from(zc::mv(source), scalar<identity::PackageName>(name),
                                    scalar<identity::ResolvedVersion>("1.0.0"_zc), emptyFeatures());
}

identity::CanonicalTargetSpecificationKey projection() {
  zc::Vector<identity::TargetFeatureName> features;
  features.add(scalar<identity::TargetFeatureName>("sse2"_zc));
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(featureSet, sorted) {
    auto result = identity::CanonicalTargetSpecificationKey::from(
        scalar<identity::TargetComponentName>("x86_64"_zc),
        scalar<identity::TargetComponentName>("zom"_zc),
        scalar<identity::TargetComponentName>("none"_zc),
        scalar<identity::TargetComponentName>("unknown"_zc),
        scalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
        zc::mv(featureSet));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("target projection fixture was rejected");
}

RegisteredTargetProfileName profileName() {
  auto result = RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("profile name fixture was rejected");
}

ir::CanonicalTargetSpec targetSpec() {
  zc::Vector<ir::CanonicalTargetFeature> features;
  auto feature = ir::CanonicalTargetFeature::from("sse2"_zc, ir::TargetFeatureState::Enabled);
  ZC_IF_SOME(value, feature) { features.add(zc::mv(value)); }
  auto result = ir::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(features), "zom"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("target specification fixture was rejected");
}

RegisteredTargetSelection targetSelection() {
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  semanticFeatures.add(scalar<identity::TargetFeatureName>("sse2"_zc));
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  specifications.add(targetSpec());
  auto profile = ir::RegisteredTargetProfileRecord::from(
      profileName(), projection(), zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  ZC_IF_SOME(value, profile) { profiles.add(zc::mv(value)); }
  auto registry = ir::TargetRegistrySnapshot::from(profileName(), zc::mv(profiles));
  ZC_REQUIRE(registry != zc::none);
  ZC_IF_SOME(snapshot, registry) {
    auto service = snapshot.packageTargetService();
    ZC_REQUIRE(service != zc::none);
    ZC_IF_SOME(targets, service) {
      auto selection = targets.select(zc::none, PackagePanicStrategy::Unwind);
      ZC_IF_SOME(value, selection) { return zc::mv(value); }
    }
  }
  ZC_FAIL_REQUIRE("target selection fixture was rejected");
}

zc::Array<uint8_t> staticPie(const RegisteredTargetSelection& target) {
  identity::CanonicalEncoder targetEncoder;
  target.encode(targetEncoder);
  auto descriptor = targetEncoder.finish();
  const size_t paddedDescriptor = (descriptor.size() + 3) & ~size_t{3};
  constexpr size_t programOffset = 64;
  constexpr size_t sectionOffset = 120;
  constexpr size_t noteOffset = 248;
  const size_t imageSize = noteOffset + 16 + paddedDescriptor;
  zc::Vector<uint8_t> bytes(imageSize);
  for (size_t index = 0; index < imageSize; ++index) { bytes.add(0); }
  bytes[0] = 0x7f;
  bytes[1] = 'E';
  bytes[2] = 'L';
  bytes[3] = 'F';
  bytes[4] = 2;
  bytes[5] = 1;
  bytes[6] = 1;
  putElf16(bytes, 16, 3);
  putElf16(bytes, 18, 62);
  putElf32(bytes, 20, 1);
  putElf64(bytes, 24, 0x1080);
  putElf64(bytes, 32, programOffset);
  putElf64(bytes, 40, sectionOffset);
  putElf16(bytes, 52, 64);
  putElf16(bytes, 54, 56);
  putElf16(bytes, 56, 1);
  putElf16(bytes, 58, 64);
  putElf16(bytes, 60, 2);

  putElf32(bytes, programOffset, 1);
  putElf32(bytes, programOffset + 4, 5);
  putElf64(bytes, programOffset + 8, 0);
  putElf64(bytes, programOffset + 16, 0x1000);
  putElf64(bytes, programOffset + 32, imageSize);
  putElf64(bytes, programOffset + 40, imageSize);

  const size_t noteSection = sectionOffset + 64;
  putElf32(bytes, noteSection + 4, 7);
  putElf64(bytes, noteSection + 24, noteOffset);
  putElf64(bytes, noteSection + 32, 16 + paddedDescriptor);
  putElf32(bytes, noteOffset, 4);
  putElf32(bytes, noteOffset + 4, static_cast<uint32_t>(descriptor.size()));
  putElf32(bytes, noteOffset + 8, 0x5a4f4d01);
  bytes[noteOffset + 12] = 'Z';
  bytes[noteOffset + 13] = 'O';
  bytes[noteOffset + 14] = 'M';
  for (size_t index = 0; index < descriptor.size(); ++index) {
    bytes[noteOffset + 16 + index] = descriptor[index];
  }
  return bytes.releaseAsArray();
}

identity::CrateKey crate(zc::StringPtr packageName) {
  zc::Maybe<identity::BuildScriptProducerKey> noOutput;
  auto compilation = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Host, projection(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, true), zc::mv(noOutput));
  ZC_REQUIRE(compilation != zc::none);
  ZC_IF_SOME(config, compilation) {
    auto result = identity::CrateKey::from(
        identity::CompilationUnitIdentity::userPackage(package(packageName)),
        identity::CrateTargetKind::BuildScript, scalar<identity::TargetName>("build"_zc),
        zc::mv(config));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("crate fixture was rejected");
}

identity::PreparatoryBuildScriptKey preparatory() {
  zc::Vector<identity::PackageKey> dependencies;
  dependencies.add(package("dep"_zc));
  auto result = identity::PreparatoryBuildScriptKey::from(
      package("app"_zc), scalar<identity::TargetName>("build"_zc), projection(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, true), zc::mv(dependencies));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("preparatory key fixture was rejected");
}

identity::ContextFingerprint fingerprint() {
  zc::Vector<identity::CompilationUnitIdentity> compilationUnits;
  zc::Vector<identity::ToolchainSemanticContextInput> toolchainInputs;
  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  zc::Vector<identity::CrateKey> crates;
  zc::Vector<identity::CrateDependencyEdgeKey> crateEdges;
  zc::Vector<identity::SourceContentIdentity> sources;
  zc::Vector<identity::ModuleKey> modules;
  auto result = identity::ContextFingerprint::compute(
      compilationUnits, toolchainInputs, packageEdges, crates, crateEdges, sources, modules);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("semantic context fingerprint fixture was rejected");
}

identity::CanonicalRelativePath path(zc::StringPtr first, zc::StringPtr second) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>(first));
  segments.add(scalar<identity::CanonicalPathSegment>(second));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

CanonicalBuildScriptManifest contract() {
  zc::Vector<identity::CanonicalRelativePath> files;
  files.add(path("tools"_zc, "build.zom"_zc));
  files.add(path("data"_zc, "input.txt"_zc));
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  ZC_IF_SOME(sourceInventory, inventory) {
    zc::Vector<identity::CanonicalPathSegment> documentSegments;
    documentSegments.add(scalar<identity::CanonicalPathSegment>("Zom.toml"_zc));
    ManifestParser parser;
    auto parsed = parser.parseWorkspaceManifest(
        identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(documentSegments)),
        R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[build]
path = "tools/build.zom"
inputs = ["data/input.txt"]
outputs = ["generated/out.zom"]
environment = ["HOME", "ZOM_TARGET"]
exported-environment = ["MODE"]
)toml"_zc,
        sourceInventory);
    if (parsed.is<NormalizedManifest>()) {
      return CanonicalBuildScriptManifest::from(parsed.get<NormalizedManifest>().buildScript());
    }
  }
  ZC_FAIL_REQUIRE("build contract fixture was rejected");
}

identity::Sha256Digest digest(zc::StringPtr text) {
  auto result = identity::sha256(text.asBytes());
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("digest fixture failed");
}

identity::Sha256Digest digestBytes(zc::ArrayPtr<const uint8_t> bytes) {
  auto result = identity::sha256(bytes);
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("binary digest fixture failed");
}

zc::Array<uint8_t> bytes(zc::StringPtr text) {
  zc::Vector<uint8_t> result(text.size());
  result.addAll(text.asBytes());
  return result.releaseAsArray();
}

TrustedBuildRuntimeKey runtime() {
  zc::Vector<zc::Array<uint8_t>> objects;
  objects.add(bytes("runtime-object"_zc));
  zc::Vector<TrustedRuntimeSymbolRecord> declaredSymbols;
  zc::Vector<TrustedRuntimeRelocationRecord> declaredRelocations;
  zc::Vector<TrustedRuntimeOperationRecord> declaredOperations;
  zc::Vector<TrustedRuntimeSymbolId> requiredOperations;
  uint32_t sectionCount[] = {1};
  auto declared = TrustedRuntimeManifestSet::verify(
      zc::arrayPtr(sectionCount), zc::mv(declaredSymbols), zc::mv(declaredRelocations),
      zc::mv(declaredOperations), requiredOperations);
  ZC_REQUIRE(declared.is<TrustedRuntimeManifestSet>());
  zc::Vector<TrustedRuntimeSymbolRecord> observedSymbols;
  zc::Vector<TrustedRuntimeRelocationRecord> observedRelocations;
  zc::Vector<TrustedRuntimeOperationRecord> observedOperations;
  zc::Vector<TrustedRuntimeSymbolId> observedRequiredOperations;
  auto observed = TrustedRuntimeManifestSet::verify(
      zc::arrayPtr(sectionCount), zc::mv(observedSymbols), zc::mv(observedRelocations),
      zc::mv(observedOperations), observedRequiredOperations);
  ZC_REQUIRE(observed.is<TrustedRuntimeManifestSet>());
  zc::Vector<identity::Sha256Digest> objectDigests;
  objectDigests.add(digest("runtime-object"_zc));
  auto evidence = TrustedRuntimeVerificationEvidence::verify(
      zc::mv(objectDigests), zc::mv(declared.get<TrustedRuntimeManifestSet>()),
      observed.get<TrustedRuntimeManifestSet>());
  ZC_REQUIRE(evidence.is<TrustedRuntimeVerificationEvidence>());
  auto result = TrustedBuildRuntimeKey::verifyEvidence(
      "zom"_zc, "zom"_zc, zc::mv(objects),
      zc::mv(evidence.get<TrustedRuntimeVerificationEvidence>()));
  if (result.is<TrustedBuildRuntimeKey>()) { return zc::mv(result.get<TrustedBuildRuntimeKey>()); }
  ZC_FAIL_REQUIRE("trusted runtime fixture was rejected");
}

BuildScriptLimitKey limits() {
  auto result = BuildScriptLimitKey::verify(BuildScriptLimitKey::defaults());
  if (result.is<BuildScriptLimitKey>()) { return zc::mv(result.get<BuildScriptLimitKey>()); }
  ZC_FAIL_REQUIRE("default limits were rejected");
}

BuildScriptExecutionKey executionKey(bool reverse) {
  zc::Vector<identity::CrateKey> crates;
  if (reverse) {
    crates.add(crate("z"_zc));
    crates.add(crate("a"_zc));
  } else {
    crates.add(crate("a"_zc));
    crates.add(crate("z"_zc));
  }
  zc::Vector<identity::CrateDependencyEdgeKey> edges;
  zc::Vector<identity::BuildScriptDigestEntry> inputs;
  if (reverse) {
    inputs.add(identity::BuildScriptDigestEntry::from(path("tools"_zc, "build.zom"_zc),
                                                      digest("build"_zc)));
    inputs.add(identity::BuildScriptDigestEntry::from(path("data"_zc, "input.txt"_zc),
                                                      digest("input"_zc)));
  } else {
    inputs.add(identity::BuildScriptDigestEntry::from(path("data"_zc, "input.txt"_zc),
                                                      digest("input"_zc)));
    inputs.add(identity::BuildScriptDigestEntry::from(path("tools"_zc, "build.zom"_zc),
                                                      digest("build"_zc)));
  }
  zc::Vector<identity::BuildScriptEnvironmentEntry> environment;
  environment.add(identity::BuildScriptEnvironmentEntry::from(
      scalar<identity::SemanticEnvironmentName>("ZOM_TARGET"_zc), bytes("host"_zc)));
  environment.add(identity::BuildScriptEnvironmentEntry::from(
      scalar<identity::SemanticEnvironmentName>("HOME"_zc), bytes("home"_zc)));
  auto result = BuildScriptExecutionKey::from(
      preparatory(), fingerprint(),
      BuildScriptExecutableKey::from(targetSelection(), digest("image"_zc)), runtime(), contract(),
      crate("app"_zc), zc::mv(crates), zc::mv(edges), zc::mv(inputs), zc::mv(environment),
      limits());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("execution key fixture was rejected");
}

class MemoryFreshDirectory final : public FreshSourceDirectory {
public:
  explicit MemoryFreshDirectory(zc::Own<zc::Directory>&& directory) noexcept
      : directory(zc::mv(directory)) {}
  ~MemoryFreshDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *directory; }
  zc::Maybe<MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<zc::Directory> directory;
};

class MemoryFreshDirectoryFactory final : public FreshSourceDirectoryFactory {
public:
  FreshSourceDirectoryResult create() override {
    zc::Own<FreshSourceDirectory> directory =
        zc::heap<MemoryFreshDirectory>(zc::newInMemoryDirectory(zc::nullClock()));
    return zc::mv(directory);
  }
};

DigestVerifiedSourceSnapshot outputSnapshot(zc::StringPtr content, bool includeOutput = true) {
  auto source = zc::newInMemoryDirectory(zc::nullClock());
  if (includeOutput) {
    source
        ->openFile(zc::Path({"generated"_zc, "out.zom"_zc}),
                   zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
        ->writeAll(content);
  }
  MemoryFreshDirectoryFactory factory;
  SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*source, factory);
  if (result.is<DigestVerifiedSourceSnapshot>()) {
    return zc::mv(result.get<DigestVerifiedSourceSnapshot>());
  }
  ZC_FAIL_REQUIRE("output snapshot fixture was rejected");
}

struct SandboxRunSpec final {
  zc::String output;
  zc::String exported;
  bool includeOutput;
  bool teardownFails;

  SandboxRunSpec(zc::String&& output, zc::String&& exported, bool includeOutput = true,
                 bool teardownFails = false)
      : output(zc::mv(output)),
        exported(zc::mv(exported)),
        includeOutput(includeOutput),
        teardownFails(teardownFails) {}
  SandboxRunSpec(SandboxRunSpec&&) noexcept = default;
  SandboxRunSpec& operator=(SandboxRunSpec&&) noexcept = default;
  ZC_DISALLOW_COPY(SandboxRunSpec);
};

class FakeSandbox final : public BuildScriptSandboxAdapter {
public:
  explicit FakeSandbox(SandboxRunSpec&& spec) noexcept : spec(zc::mv(spec)) {}
  ~FakeSandbox() noexcept override = default;

  BuildScriptRunResult execute(const BuildScriptRequestFrame&) override {
    zc::Vector<BuildScriptEnvironmentValue> exported;
    exported.add(BuildScriptEnvironmentValue::from(
        scalar<identity::SemanticEnvironmentName>("MODE"_zc), bytes(spec.exported)));
    return VerifiedBuildScriptRun::from(outputSnapshot(spec.output, spec.includeOutput),
                                        BuildScriptResponse::success(zc::mv(exported)));
  }

  zc::Maybe<BuildScriptIssue> finish() override {
    if (spec.teardownFails) {
      spec.teardownFails = false;
      return BuildScriptIssue::SandboxTeardownFailed;
    }
    return zc::none;
  }

private:
  SandboxRunSpec spec;
};

class FakeSandboxFactory final : public BuildScriptSandboxFactory {
public:
  explicit FakeSandboxFactory(zc::Vector<SandboxRunSpec>&& runs) : runs(zc::mv(runs)) {}
  ~FakeSandboxFactory() noexcept override = default;

  BuildScriptSandboxCreateResult create() override {
    if (next == runs.size()) { return BuildScriptIssue::SandboxSetupFailed; }
    zc::Own<BuildScriptSandboxAdapter> sandbox = zc::heap<FakeSandbox>(zc::mv(runs[next++]));
    return zc::mv(sandbox);
  }

  size_t createCount() const noexcept { return next; }

private:
  zc::Vector<SandboxRunSpec> runs;
  size_t next = 0;
};

BuildScriptRequestFrame request(const BuildScriptExecutionKey& key);

class FakeBuildScriptCache final : public BuildScriptCache {
public:
  FakeBuildScriptCache() = default;
  explicit FakeBuildScriptCache(BuildScriptCacheCandidate&& candidate)
      : candidate(zc::mv(candidate)) {}
  ~FakeBuildScriptCache() noexcept override = default;

  BuildScriptCacheLookupResult lookup(zc::ArrayPtr<const uint8_t> executionKeyBytes) override {
    ++lookups;
    observedLookupKey = copy(executionKeyBytes);
    ZC_IF_SOME(value, candidate) { return zc::mv(value); }
    return BuildScriptCacheMiss{};
  }

  zc::Maybe<BuildScriptIssue> publishAtomically(
      zc::ArrayPtr<const uint8_t> executionKeyBytes,
      const identity::BuildScriptOutputRecord& output,
      const DigestVerifiedSourceSnapshot& generatedSnapshot) override {
    ++publications;
    observedPublishedKey = copy(executionKeyBytes);
    observedOutputRecord = output.encode();
    for (const auto& file : generatedSnapshot.record().files()) {
      auto read = generatedSnapshot.readVerifiedFile(file.path());
      if (!read.is<zc::Array<zc::byte>>()) {
        return BuildScriptIssue::BuildResultIntegrityViolation;
      }
    }
    return publicationIssue;
  }

  static zc::Array<uint8_t> copy(zc::ArrayPtr<const uint8_t> source) {
    zc::Vector<uint8_t> result(source.size());
    result.addAll(source);
    return result.releaseAsArray();
  }

  zc::Maybe<BuildScriptCacheCandidate> candidate;
  zc::Maybe<BuildScriptIssue> publicationIssue;
  zc::Array<uint8_t> observedLookupKey;
  zc::Array<uint8_t> observedPublishedKey;
  zc::Array<uint8_t> observedOutputRecord;
  size_t lookups = 0;
  size_t publications = 0;
};

enum class CacheCandidateMutation : uint8_t { None, ExecutionKey, OutputRecord, GeneratedBytes };

BuildScriptCacheCandidate cacheCandidate(const BuildScriptExecutionKey& key,
                                         CacheCandidateMutation mutation) {
  auto frame = request(key);
  zc::Vector<SandboxRunSpec> runs;
  runs.add(SandboxRunSpec(zc::str("generated"), zc::str("fast")));
  runs.add(SandboxRunSpec(zc::str("generated"), zc::str("fast")));
  FakeSandboxFactory factory(zc::mv(runs));
  auto executed = BuildScriptDeterminismExecutor::executeCacheMiss(factory, key, frame);
  ZC_REQUIRE(executed.is<VerifiedBuildScriptResult>());
  auto executionBytes = key.encode();
  auto output = executed.get<VerifiedBuildScriptResult>().output().clone();
  auto outputBytes = output.encode();
  if (mutation == CacheCandidateMutation::ExecutionKey) {
    executionBytes[0] ^= 1;
  } else if (mutation == CacheCandidateMutation::OutputRecord) {
    outputBytes[0] ^= 1;
  }
  return BuildScriptCacheCandidate::from(
      zc::mv(executionBytes), zc::mv(outputBytes), zc::mv(output),
      outputSnapshot(mutation == CacheCandidateMutation::GeneratedBytes ? "changed"_zc
                                                                        : "generated"_zc));
}

VerifiedBuildScriptResult executedResult(const BuildScriptExecutionKey& key) {
  auto frame = request(key);
  zc::Vector<SandboxRunSpec> runs;
  runs.add(SandboxRunSpec(zc::str("generated"), zc::str("fast")));
  runs.add(SandboxRunSpec(zc::str("generated"), zc::str("fast")));
  FakeSandboxFactory factory(zc::mv(runs));
  auto result = BuildScriptDeterminismExecutor::executeCacheMiss(factory, key, frame);
  ZC_REQUIRE(result.is<VerifiedBuildScriptResult>());
  return zc::mv(result.get<VerifiedBuildScriptResult>());
}

enum class OutputRecordMutation : uint8_t {
  SourceDigest,
  DeclaredEnvironment,
  ExportedEnvironment
};

identity::BuildScriptOutputRecord mutateOutputRecord(
    const identity::BuildScriptOutputRecord& source, OutputRecordMutation mutation) {
  zc::Vector<identity::BuildScriptDigestEntry> sourceDigests(source.sourceDigests().size());
  for (size_t index = 0; index < source.sourceDigests().size(); ++index) {
    const auto& value = source.sourceDigests()[index];
    sourceDigests.add(mutation == OutputRecordMutation::SourceDigest && index == 0
                          ? identity::BuildScriptDigestEntry::from(value.path().clone(),
                                                                   digest("stale-source"_zc))
                          : value.clone());
  }
  zc::Vector<identity::BuildScriptEnvironmentEntry> declared(source.declaredEnvironment().size());
  for (size_t index = 0; index < source.declaredEnvironment().size(); ++index) {
    const auto& value = source.declaredEnvironment()[index];
    declared.add(mutation == OutputRecordMutation::DeclaredEnvironment && index == 0
                     ? identity::BuildScriptEnvironmentEntry::from(
                           scalar<identity::SemanticEnvironmentName>(value.name()),
                           bytes("stale-declared"_zc))
                     : value.clone());
  }
  zc::Vector<identity::BuildScriptDigestEntry> generated(source.generatedSources().size());
  for (const auto& value : source.generatedSources()) { generated.add(value.clone()); }
  zc::Vector<identity::BuildScriptEnvironmentEntry> exported(source.exportedEnvironment().size());
  for (size_t index = 0; index < source.exportedEnvironment().size(); ++index) {
    const auto& value = source.exportedEnvironment()[index];
    exported.add(mutation == OutputRecordMutation::ExportedEnvironment && index == 0
                     ? identity::BuildScriptEnvironmentEntry::from(
                           scalar<identity::SemanticEnvironmentName>(value.name()),
                           bytes("stale-exported"_zc))
                     : value.clone());
  }
  auto output = identity::BuildScriptOutputRecord::from(source.producerKey(), zc::mv(sourceDigests),
                                                        zc::mv(declared), zc::mv(generated),
                                                        zc::mv(exported));
  ZC_REQUIRE(output != zc::none);
  ZC_IF_SOME(value, output) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

zc::String invalidUtf8() {
  const char raw[] = {static_cast<char>(0xff)};
  return zc::heapString(zc::arrayPtr(raw, zc::size(raw)));
}

BuildScriptRequestFrame request(const BuildScriptExecutionKey& key) {
  zc::Vector<BuildScriptEnvironmentValue> environment;
  environment.add(BuildScriptEnvironmentValue::from(
      scalar<identity::SemanticEnvironmentName>("HOME"_zc), bytes("home"_zc)));
  environment.add(BuildScriptEnvironmentValue::from(
      scalar<identity::SemanticEnvironmentName>("ZOM_TARGET"_zc), bytes("host"_zc)));
  auto result =
      BuildScriptContractVerifier::createRequest(key.contract(), zc::mv(environment), key.limits());
  if (result.is<BuildScriptRequestFrame>()) {
    return zc::mv(result.get<BuildScriptRequestFrame>());
  }
  ZC_FAIL_REQUIRE("execution request fixture was rejected");
}

}  // namespace

ZC_TEST("BuildScriptExecutionKey canonicalizes the complete host closure") {
  auto encoded = executionKey(false).encode();
  ZC_EXPECT(encoded.asPtr() == executionKey(true).encode().asPtr());
  auto oracleDigest = identity::sha256(encoded.asPtr());
  ZC_REQUIRE(oracleDigest != zc::none);
  ZC_IF_SOME(value, oracleDigest) {
    ZC_EXPECT(zc::encodeHex(value.bytes()) ==
              "be3bd2d50289ca57e89c6cef56378ee97595261fd63101fe2f87c9dba2722a36"_zc);
  }
}

ZC_TEST("Build script executable admission requires exact static PIE identity") {
  {
    auto target = targetSelection();
    auto image = staticPie(target);
    auto key = BuildScriptExecutableKey::from(zc::mv(target), digestBytes(image));
    auto result = VerifiedBuildScriptExecutable::verify(zc::mv(key), zc::mv(image));
    ZC_EXPECT(result.is<VerifiedBuildScriptExecutable>());
  }
  {
    auto target = targetSelection();
    auto image = staticPie(target);
    auto key = BuildScriptExecutableKey::from(zc::mv(target), digest("different"_zc));
    auto result = VerifiedBuildScriptExecutable::verify(zc::mv(key), zc::mv(image));
    ZC_REQUIRE(result.is<BuildScriptIssue>());
    ZC_EXPECT(result.get<BuildScriptIssue>() == BuildScriptIssue::ExecutableIdentityMismatch);
  }
  constexpr size_t programOffset = 64;
  constexpr size_t sectionOffset = 120;
  constexpr size_t noteOffset = 248;
  for (uint32_t mutation = 0; mutation < 6; ++mutation) {
    auto target = targetSelection();
    auto image = staticPie(target);
    if (mutation == 0) {
      putElf16(image, 16, 2);
    } else if (mutation == 1) {
      putElf32(image, programOffset, 3);
    } else if (mutation == 2) {
      putElf32(image, programOffset + 4, 7);
    } else if (mutation == 3) {
      putElf32(image, sectionOffset + 64 + 4, 14);
    } else if (mutation == 4) {
      image[noteOffset + 16] ^= 1;
    } else {
      putElf16(image, 18, 183);
    }
    auto imageDigest = identity::sha256(image);
    ZC_REQUIRE(imageDigest != zc::none);
    ZC_IF_SOME(value, imageDigest) {
      auto key = BuildScriptExecutableKey::from(zc::mv(target), value);
      auto result = VerifiedBuildScriptExecutable::verify(zc::mv(key), zc::mv(image));
      ZC_REQUIRE(result.is<BuildScriptIssue>());
      ZC_EXPECT(result.get<BuildScriptIssue>() == BuildScriptIssue::ExecutableIdentityMismatch);
    }
  }
}

ZC_TEST("BuildScriptExecutionKey rejects contract input key mismatch") {
  zc::Vector<identity::CrateKey> crates;
  zc::Vector<identity::CrateDependencyEdgeKey> edges;
  zc::Vector<identity::BuildScriptDigestEntry> inputs;
  inputs.add(
      identity::BuildScriptDigestEntry::from(path("wrong"_zc, "input.zom"_zc), digest("wrong"_zc)));
  zc::Vector<identity::BuildScriptEnvironmentEntry> environment;
  auto result = BuildScriptExecutionKey::from(
      preparatory(), fingerprint(),
      BuildScriptExecutableKey::from(targetSelection(), digest("image"_zc)), runtime(), contract(),
      crate("app"_zc), zc::mv(crates), zc::mv(edges), zc::mv(inputs), zc::mv(environment),
      limits());
  ZC_EXPECT(result == zc::none);
}

ZC_TEST("Build-script cache miss publishes one complete byte-identical output record") {
  auto key = executionKey(false);
  auto frame = request(key);
  zc::Vector<SandboxRunSpec> runs;
  runs.add(SandboxRunSpec(zc::str("generated"), zc::str("fast")));
  runs.add(SandboxRunSpec(zc::str("generated"), zc::str("fast")));
  FakeSandboxFactory factory(zc::mv(runs));
  auto result = BuildScriptDeterminismExecutor::executeCacheMiss(factory, key, frame);
  ZC_REQUIRE(result.is<VerifiedBuildScriptResult>());
  ZC_EXPECT(factory.createCount() == 2);
  ZC_EXPECT(result.get<VerifiedBuildScriptResult>().run().outputs().files().size() == 1);
  ZC_EXPECT(result.get<VerifiedBuildScriptResult>().output().generatedSources().size() == 1);
  auto outputBytes = result.get<VerifiedBuildScriptResult>().output().encode();
  auto outputDigest = identity::sha256(outputBytes.asPtr());
  ZC_REQUIRE(outputDigest != zc::none);
  ZC_IF_SOME(value, outputDigest) {
    ZC_EXPECT(zc::encodeHex(value.bytes()) ==
              "07bb991bb4c5d57e573c2de43ea6612ffe5003f247420a9239cb8e454c614f96"_zc);
  }
}

ZC_TEST("Build-result publication rejects stale execution environment facts with provenance") {
  const OutputRecordMutation mutations[] = {
      OutputRecordMutation::SourceDigest,
      OutputRecordMutation::DeclaredEnvironment,
      OutputRecordMutation::ExportedEnvironment,
  };
  const BuildResultIntegrityFact expectedFacts[] = {
      BuildResultIntegrityFact::SourceDigests,
      BuildResultIntegrityFact::DeclaredEnvironment,
      BuildResultIntegrityFact::ExportedEnvironment,
  };
  for (size_t index = 0; index < zc::size(mutations); ++index) {
    auto key = executionKey(false);
    auto original = executedResult(key);
    auto stale = mutateOutputRecord(original.output(), mutations[index]);
    zc::Vector<BuildScriptEnvironmentValue> exported;
    exported.add(BuildScriptEnvironmentValue::from(
        scalar<identity::SemanticEnvironmentName>("MODE"_zc), bytes("fast"_zc)));
    auto publication = VerifiedBuildScriptResult::publishDeterministicExecution(
        key,
        VerifiedBuildScriptRun::from(outputSnapshot("generated"_zc),
                                     BuildScriptResponse::success(zc::mv(exported))),
        zc::mv(stale));
    ZC_REQUIRE(publication.is<BuildResultIntegrityViolation>());
    const auto& violation = publication.get<BuildResultIntegrityViolation>();
    ZC_EXPECT(violation.producer() == BuildResultIntegrityProducer::DeterministicExecution);
    ZC_EXPECT(violation.fact() == expectedFacts[index]);
  }
}

ZC_TEST("Build-result cache replay reports the cache publication producer") {
  auto key = executionKey(false);
  auto original = executedResult(key);
  auto stale = mutateOutputRecord(original.output(), OutputRecordMutation::ExportedEnvironment);
  zc::Vector<BuildScriptEnvironmentValue> exported;
  exported.add(BuildScriptEnvironmentValue::from(
      scalar<identity::SemanticEnvironmentName>("MODE"_zc), bytes("fast"_zc)));
  auto publication = VerifiedBuildScriptResult::publishCacheReplay(
      key,
      VerifiedBuildScriptRun::from(outputSnapshot("generated"_zc),
                                   BuildScriptResponse::success(zc::mv(exported))),
      zc::mv(stale));
  ZC_REQUIRE(publication.is<BuildResultIntegrityViolation>());
  const auto& violation = publication.get<BuildResultIntegrityViolation>();
  ZC_EXPECT(violation.producer() == BuildResultIntegrityProducer::CacheReplay);
  ZC_EXPECT(violation.fact() == BuildResultIntegrityFact::ExportedEnvironment);
}

ZC_TEST("Build-script cache miss rejects record bytes outputs and teardown divergence") {
  {
    auto key = executionKey(false);
    auto frame = request(key);
    zc::Vector<SandboxRunSpec> runs;
    runs.add(SandboxRunSpec(zc::str("a"), zc::str("fast")));
    runs.add(SandboxRunSpec(zc::str("changed"), zc::str("fast")));
    FakeSandboxFactory factory(zc::mv(runs));
    auto result = BuildScriptDeterminismExecutor::executeCacheMiss(factory, key, frame);
    ZC_REQUIRE(result.is<BuildScriptIssue>());
    ZC_EXPECT(result.get<BuildScriptIssue>() == BuildScriptIssue::NondeterministicOutput);
  }
  {
    auto key = executionKey(false);
    auto frame = request(key);
    zc::Vector<SandboxRunSpec> runs;
    runs.add(SandboxRunSpec(zc::str("a"), zc::str("fast")));
    runs.add(SandboxRunSpec(zc::str("a"), zc::str("slow")));
    FakeSandboxFactory factory(zc::mv(runs));
    auto result = BuildScriptDeterminismExecutor::executeCacheMiss(factory, key, frame);
    ZC_REQUIRE(result.is<BuildScriptIssue>());
    ZC_EXPECT(result.get<BuildScriptIssue>() == BuildScriptIssue::NondeterministicOutput);
  }
  {
    auto key = executionKey(false);
    auto frame = request(key);
    zc::Vector<SandboxRunSpec> runs;
    runs.add(SandboxRunSpec(zc::str("a"), zc::str("fast"), false));
    FakeSandboxFactory factory(zc::mv(runs));
    auto result = BuildScriptDeterminismExecutor::executeCacheMiss(factory, key, frame);
    ZC_REQUIRE(result.is<BuildScriptIssue>());
    ZC_EXPECT(result.get<BuildScriptIssue>() == BuildScriptIssue::MissingOutput);
  }
  {
    auto key = executionKey(false);
    auto frame = request(key);
    zc::Vector<SandboxRunSpec> runs;
    runs.add(SandboxRunSpec(zc::str("a"), zc::str("fast"), true, true));
    FakeSandboxFactory factory(zc::mv(runs));
    auto result = BuildScriptDeterminismExecutor::executeCacheMiss(factory, key, frame);
    ZC_REQUIRE(result.is<BuildScriptIssue>());
    ZC_EXPECT(result.get<BuildScriptIssue>() == BuildScriptIssue::SandboxTeardownFailed);
  }
}

ZC_TEST("Build-script cache executor publishes only one verified double execution") {
  auto key = executionKey(false);
  auto frame = request(key);
  zc::Vector<SandboxRunSpec> runs;
  runs.add(SandboxRunSpec(zc::str("generated"), zc::str("fast")));
  runs.add(SandboxRunSpec(zc::str("generated"), zc::str("fast")));
  FakeSandboxFactory factory(zc::mv(runs));
  FakeBuildScriptCache cache;
  auto result = BuildScriptCacheExecutor::execute(cache, factory, key, frame);
  ZC_REQUIRE(result.is<VerifiedBuildScriptResult>());
  ZC_EXPECT(factory.createCount() == 2);
  ZC_EXPECT(cache.lookups == 1);
  ZC_EXPECT(cache.publications == 1);
  ZC_EXPECT(cache.observedLookupKey.asPtr() == key.encode().asPtr());
  ZC_EXPECT(cache.observedPublishedKey.asPtr() == key.encode().asPtr());
  ZC_EXPECT(cache.observedOutputRecord.asPtr() ==
            result.get<VerifiedBuildScriptResult>().output().encode().asPtr());
}

ZC_TEST("Build-script cache executor revalidates a complete hit without execution") {
  auto key = executionKey(false);
  auto frame = request(key);
  zc::Vector<SandboxRunSpec> noRuns;
  FakeSandboxFactory factory(zc::mv(noRuns));
  FakeBuildScriptCache cache(cacheCandidate(key, CacheCandidateMutation::None));
  auto result = BuildScriptCacheExecutor::execute(cache, factory, key, frame);
  ZC_REQUIRE(result.is<VerifiedBuildScriptResult>());
  ZC_EXPECT(factory.createCount() == 0);
  ZC_EXPECT(cache.lookups == 1);
  ZC_EXPECT(cache.publications == 0);
  ZC_EXPECT(result.get<VerifiedBuildScriptResult>().run().outputs().files().size() == 1);
}

ZC_TEST("Build-script cache executor rejects every stale replay fact") {
  for (const auto mutation :
       {CacheCandidateMutation::ExecutionKey, CacheCandidateMutation::OutputRecord,
        CacheCandidateMutation::GeneratedBytes}) {
    auto key = executionKey(false);
    auto frame = request(key);
    zc::Vector<SandboxRunSpec> noRuns;
    FakeSandboxFactory factory(zc::mv(noRuns));
    FakeBuildScriptCache cache(cacheCandidate(key, mutation));
    auto result = BuildScriptCacheExecutor::execute(cache, factory, key, frame);
    ZC_REQUIRE(result.is<BuildScriptIssue>());
    ZC_EXPECT(result.get<BuildScriptIssue>() == BuildScriptIssue::BuildResultIntegrityViolation);
    ZC_EXPECT(factory.createCount() == 0);
    ZC_EXPECT(cache.publications == 0);
  }
}

ZC_TEST("Build-script cache executor rejects invalid generated source before publication") {
  auto key = executionKey(false);
  auto frame = request(key);
  zc::Vector<SandboxRunSpec> runs;
  runs.add(SandboxRunSpec(invalidUtf8(), zc::str("fast")));
  FakeSandboxFactory factory(zc::mv(runs));
  FakeBuildScriptCache cache;
  auto result = BuildScriptCacheExecutor::execute(cache, factory, key, frame);
  ZC_REQUIRE(result.is<BuildScriptIssue>());
  ZC_EXPECT(result.get<BuildScriptIssue>() == BuildScriptIssue::InvalidGeneratedSource);
  ZC_EXPECT(factory.createCount() == 1);
  ZC_EXPECT(cache.publications == 0);
}

ZC_TEST("Build-script cache publication failure suppresses the verified result") {
  auto key = executionKey(false);
  auto frame = request(key);
  zc::Vector<SandboxRunSpec> runs;
  runs.add(SandboxRunSpec(zc::str("generated"), zc::str("fast")));
  runs.add(SandboxRunSpec(zc::str("generated"), zc::str("fast")));
  FakeSandboxFactory factory(zc::mv(runs));
  FakeBuildScriptCache cache;
  cache.publicationIssue = BuildScriptIssue::BuildResultIntegrityViolation;
  auto result = BuildScriptCacheExecutor::execute(cache, factory, key, frame);
  ZC_REQUIRE(result.is<BuildScriptIssue>());
  ZC_EXPECT(result.get<BuildScriptIssue>() == BuildScriptIssue::BuildResultIntegrityViolation);
  ZC_EXPECT(cache.publications == 1);
}

ZC_TEST("Build-script final result set freezes exact plan keys and generated views") {
  auto key = executionKey(false);
  zc::Vector<identity::PreparatoryBuildScriptKey> planKeys;
  planKeys.add(key.preparatory().clone());
  zc::Vector<VerifiedBuildScriptResult> results;
  results.add(executedResult(key));
  auto verified = VerifiedBuildScriptResultSet::from(zc::mv(planKeys), zc::mv(results));
  ZC_REQUIRE(verified.is<VerifiedBuildScriptResultSet>());
  auto& value = verified.get<VerifiedBuildScriptResultSet>();
  ZC_EXPECT(value.planKeys().size() == 1);
  ZC_EXPECT(value.results().size() == 1);
  ZC_EXPECT(value.results()[0].output().generatedSources()[0].digest() ==
            value.results()[0].run().outputs().files()[0].contentDigest());

  zc::Vector<identity::PreparatoryBuildScriptKey> emptyPlan;
  zc::Vector<VerifiedBuildScriptResult> emptyResults;
  auto empty = VerifiedBuildScriptResultSet::from(zc::mv(emptyPlan), zc::mv(emptyResults));
  ZC_REQUIRE(empty.is<VerifiedBuildScriptResultSet>());
}

ZC_TEST("Build-script final result set rejects missing duplicate and changed generated facts") {
  {
    auto key = executionKey(false);
    zc::Vector<identity::PreparatoryBuildScriptKey> planKeys;
    planKeys.add(key.preparatory().clone());
    zc::Vector<VerifiedBuildScriptResult> noResults;
    auto rejected = VerifiedBuildScriptResultSet::from(zc::mv(planKeys), zc::mv(noResults));
    ZC_REQUIRE(rejected.is<BuildResultIntegrityViolation>());
    ZC_EXPECT(rejected.get<BuildResultIntegrityViolation>().producer() ==
              BuildResultIntegrityProducer::FinalSessionPublication);
    ZC_EXPECT(rejected.get<BuildResultIntegrityViolation>().fact() ==
              BuildResultIntegrityFact::PlanAssociation);
  }
  {
    auto key = executionKey(false);
    zc::Vector<identity::PreparatoryBuildScriptKey> planKeys;
    planKeys.add(key.preparatory().clone());
    planKeys.add(key.preparatory().clone());
    zc::Vector<VerifiedBuildScriptResult> results;
    results.add(executedResult(key));
    results.add(executedResult(key));
    auto rejected = VerifiedBuildScriptResultSet::from(zc::mv(planKeys), zc::mv(results));
    ZC_REQUIRE(rejected.is<BuildResultIntegrityViolation>());
    ZC_EXPECT(rejected.get<BuildResultIntegrityViolation>().producer() ==
              BuildResultIntegrityProducer::FinalSessionPublication);
    ZC_EXPECT(rejected.get<BuildResultIntegrityViolation>().fact() ==
              BuildResultIntegrityFact::PlanAssociation);
  }
  {
    auto key = executionKey(false);
    auto original = executedResult(key);
    auto output = original.output().clone();
    zc::Vector<BuildScriptEnvironmentValue> exported;
    exported.add(BuildScriptEnvironmentValue::from(
        scalar<identity::SemanticEnvironmentName>("MODE"_zc), bytes("fast"_zc)));
    auto changed = VerifiedBuildScriptResult::publishDeterministicExecution(
        key,
        VerifiedBuildScriptRun::from(outputSnapshot("changed"_zc),
                                     BuildScriptResponse::success(zc::mv(exported))),
        zc::mv(output));
    ZC_REQUIRE(changed.is<BuildResultIntegrityViolation>());
    ZC_EXPECT(changed.get<BuildResultIntegrityViolation>().producer() ==
              BuildResultIntegrityProducer::DeterministicExecution);
    ZC_EXPECT(changed.get<BuildResultIntegrityViolation>().fact() ==
              BuildResultIntegrityFact::GeneratedInventory);
  }
}

}  // namespace zomlang::compiler::driver::package
