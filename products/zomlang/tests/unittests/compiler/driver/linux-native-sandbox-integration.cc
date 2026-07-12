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

#include <unistd.h>

#include <cstdlib>

#include "zc/core/filesystem.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/driver/package/linux-native-sandbox.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/irgen/target-registry.h"

namespace zomlang::compiler::driver::package {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid Linux sandbox integration scalar");
}

zc::StringPtr hostArchitectureName() {
#if defined(__x86_64__)
  return "x86_64"_zc;
#else
  return "aarch64"_zc;
#endif
}

RegisteredTargetProfileName profileName() {
  auto result = RegisteredTargetProfileName::from("linux-sandbox-host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid Linux sandbox integration profile");
}

identity::CanonicalTargetSpecificationKey semanticProjection() {
  zc::Vector<identity::TargetFeatureName> features;
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(featureSet, sorted) {
    auto result = identity::CanonicalTargetSpecificationKey::from(
        scalar<identity::TargetComponentName>(hostArchitectureName()),
        scalar<identity::TargetComponentName>("zom"_zc),
        scalar<identity::TargetComponentName>("none"_zc),
        scalar<identity::TargetComponentName>("unknown"_zc),
        scalar<identity::TargetComponentName>("zom-v1"_zc), 64, identity::Endianness::Little,
        zc::mv(featureSet));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid Linux sandbox integration projection");
}

RegisteredTargetSelection targetSelection() {
  zc::Vector<irgen::CanonicalTargetFeature> targetFeatures;
  auto targetSpec = irgen::CanonicalTargetSpec::from(
      zc::str(hostArchitectureName(), "-zom-none"_zc), "e-p:64:64"_zc, "generic"_zc,
      zc::mv(targetFeatures), "zom-v1"_zc, irgen::BackendPanicStrategy::Unwind,
      irgen::ObjectFormat::Elf);
  ZC_REQUIRE(targetSpec != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<irgen::CanonicalTargetSpec> specifications;
  ZC_IF_SOME(value, targetSpec) { specifications.add(zc::mv(value)); }
  auto profile = irgen::RegisteredTargetProfileRecord::from(
      profileName(), semanticProjection(), zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<irgen::RegisteredTargetProfileRecord> profiles;
  ZC_IF_SOME(value, profile) { profiles.add(zc::mv(value)); }
  auto registry = irgen::TargetRegistrySnapshot::from(profileName(), zc::mv(profiles));
  ZC_REQUIRE(registry != zc::none);
  ZC_IF_SOME(snapshot, registry) {
    auto service = snapshot.packageTargetService();
    ZC_REQUIRE(service != zc::none);
    ZC_IF_SOME(targets, service) {
      auto selection = targets.select(zc::none, PackagePanicStrategy::Unwind);
      ZC_IF_SOME(value, selection) { return zc::mv(value); }
    }
  }
  ZC_FAIL_REQUIRE("Linux sandbox integration target selection failed");
}

uint16_t readUint16(zc::ArrayPtr<const uint8_t> bytes, size_t offset) {
  ZC_REQUIRE(offset + 2 <= bytes.size());
  return static_cast<uint16_t>(bytes[offset]) | static_cast<uint16_t>(bytes[offset + 1] << 8U);
}

uint64_t readUint64(zc::ArrayPtr<const uint8_t> bytes, size_t offset) {
  ZC_REQUIRE(offset + 8 <= bytes.size());
  uint64_t result = 0;
  for (uint32_t index = 0; index < 8; ++index) {
    result |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8U);
  }
  return result;
}

void putUint32(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint32_t value) {
  ZC_REQUIRE(offset + 4 <= bytes.size());
  for (uint32_t index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8U));
  }
}

void putUint64(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint64_t value) {
  ZC_REQUIRE(offset + 8 <= bytes.size());
  for (uint32_t index = 0; index < 8; ++index) {
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8U));
  }
}

zc::Array<uint8_t> fixtureImage(const RegisteredTargetSelection& target) {
  auto filesystem = zc::newDiskFilesystem();
  const zc::StringPtr absolutePath = ZOM_LINUX_SANDBOX_RUNTIME_FIXTURE;
  ZC_REQUIRE(absolutePath.startsWith("/"_zc));
  auto bytes =
      filesystem->getRoot().openFile(zc::Path::parse(absolutePath.slice(1)))->readAllBytes();
  const uint8_t marker[] = {4, 0, 0, 0, 0xf0, 0x0f, 0, 0, 1, 0x4d, 0x4f, 0x5a, 'Z', 'O', 'M', 0};
  size_t noteOffset = bytes.size();
  for (size_t offset = 0; offset + zc::size(marker) <= bytes.size(); ++offset) {
    bool equal = true;
    for (size_t index = 0; index < zc::size(marker); ++index) {
      if (bytes[offset + index] != marker[index]) { equal = false; }
    }
    if (equal) {
      ZC_REQUIRE(noteOffset == bytes.size());
      noteOffset = offset;
    }
  }
  ZC_REQUIRE(noteOffset != bytes.size());

  identity::CanonicalEncoder encoder;
  target.encode(encoder);
  auto descriptor = encoder.finish();
  ZC_REQUIRE(descriptor.size() <= 4080);
  putUint32(bytes, noteOffset + 4, static_cast<uint32_t>(descriptor.size()));
  for (size_t index = 0; index < descriptor.size(); ++index) {
    bytes[noteOffset + 16 + index] = descriptor[index];
  }
  const uint64_t noteSize = 16 + ((descriptor.size() + 3) & ~size_t{3});
  const uint64_t sectionOffset = readUint64(bytes, 40);
  const uint16_t sectionCount = readUint16(bytes, 60);
  bool sectionPatched = false;
  for (uint16_t index = 0; index < sectionCount; ++index) {
    const size_t header = sectionOffset + static_cast<uint64_t>(index) * 64;
    if (readUint64(bytes, header + 24) == noteOffset) {
      ZC_REQUIRE(!sectionPatched);
      putUint64(bytes, header + 32, noteSize);
      sectionPatched = true;
    }
  }
  ZC_REQUIRE(sectionPatched);
  return bytes;
}

VerifiedBuildScriptExecutable executable() {
  auto target = targetSelection();
  auto image = fixtureImage(target);
  auto digest = identity::sha256(image);
  ZC_REQUIRE(digest != zc::none);
  ZC_IF_SOME(value, digest) {
    auto key = BuildScriptExecutableKey::from(zc::mv(target), value);
    auto verified = VerifiedBuildScriptExecutable::verify(zc::mv(key), zc::mv(image));
    ZC_REQUIRE(verified.is<VerifiedBuildScriptExecutable>());
    return zc::mv(verified.get<VerifiedBuildScriptExecutable>());
  }
  ZC_FAIL_REQUIRE("Linux sandbox fixture digest failed");
}

class MemoryFreshDirectory final : public FreshSourceDirectory {
public:
  MemoryFreshDirectory() : rootValue(zc::newInMemoryDirectory(zc::nullClock())) {}
  ~MemoryFreshDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<zc::Directory> rootValue;
};

class MemoryFreshDirectoryFactory final : public FreshSourceDirectoryFactory {
public:
  FreshSourceDirectoryResult create() override {
    zc::Own<FreshSourceDirectory> result = zc::heap<MemoryFreshDirectory>();
    return zc::mv(result);
  }
};

class DiskFreshDirectory final : public FreshSourceDirectory {
public:
  DiskFreshDirectory(zc::Own<const zc::Directory>&& parent, zc::Path&& path,
                     zc::Own<const zc::Directory>&& root) noexcept
      : parent(zc::mv(parent)), path(zc::mv(path)), rootValue(zc::mv(root)) {}
  ~DiskFreshDirectory() noexcept override { (void)finish(); }
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<MaterializationIssue> finish() override {
    if (finished) { return zc::none; }
    rootValue = nullptr;
    try {
      parent->remove(path);
      finished = true;
      return zc::none;
    } catch (const zc::Exception&) { return MaterializationIssue::SnapshotCleanupFailed; }
  }

private:
  zc::Own<const zc::Directory> parent;
  zc::Path path;
  zc::Own<const zc::Directory> rootValue;
  bool finished = false;
};

class DiskFreshDirectoryFactory final : public FreshSourceDirectoryFactory {
public:
  DiskFreshDirectoryFactory(const zc::Directory& parent, zc::StringPtr name)
      : parent(parent.clone()), path(name) {}
  FreshSourceDirectoryResult create() override {
    if (used) { return MaterializationIssue::FreshDirectoryCreateFailed; }
    used = true;
    try {
      auto owner = parent->clone();
      auto root = owner->openSubdir(path, zc::WriteMode::CREATE | zc::WriteMode::PRIVATE);
      zc::Own<FreshSourceDirectory> result =
          zc::heap<DiskFreshDirectory>(zc::mv(owner), path.clone(), zc::mv(root));
      return zc::mv(result);
    } catch (const zc::Exception&) { return MaterializationIssue::FreshDirectoryCreateFailed; }
  }

private:
  zc::Own<const zc::Directory> parent;
  zc::Path path;
  bool used = false;
};

DigestVerifiedSourceSnapshot emptyInputSnapshot() {
  auto source = zc::newInMemoryDirectory(zc::nullClock());
  MemoryFreshDirectoryFactory factory;
  SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*source, factory);
  ZC_REQUIRE(result.is<DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<DigestVerifiedSourceSnapshot>());
}

BuildScriptLimitKey limits() {
  auto result = BuildScriptLimitKey::verify(BuildScriptLimitKey::defaults());
  ZC_REQUIRE(result.is<BuildScriptLimitKey>());
  return zc::mv(result.get<BuildScriptLimitKey>());
}

BuildScriptRequestFrame request() {
  zc::Vector<identity::CanonicalRelativePath> inputs;
  zc::Vector<BuildScriptEnvironmentValue> environment;
  zc::Vector<identity::CanonicalRelativePath> outputs;
  auto buildLimits = limits();
  auto result = BuildScriptRequestFrame::encode(zc::mv(inputs), zc::mv(environment),
                                                zc::mv(outputs), buildLimits);
  ZC_REQUIRE(result.is<BuildScriptRequestFrame>());
  return zc::mv(result.get<BuildScriptRequestFrame>());
}

}  // namespace

ZC_TEST("Production LinuxNativeSandboxV1 executes an admitted static PIE in kernel isolation") {
  const char* cgroupParent = getenv("ZOM_LINUX_SANDBOX_CGROUP_PARENT");
  ZC_REQUIRE(cgroupParent != nullptr && cgroupParent[0] == '/');
  auto filesystem = zc::newDiskFilesystem();
  const auto rootName = zc::str("zom-linux-sandbox-integration-"_zc, getpid());
  const auto rootAbsolutePath = zc::str("/tmp/"_zc, rootName);
  const auto rootPath = zc::Path({"tmp"_zc, rootName});
  auto testRoot = filesystem->getRoot().openSubdir(
      rootPath, zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT | zc::WriteMode::PRIVATE);
  {
    auto sandboxParent = testRoot->openSubdir(zc::Path("sandboxes"_zc),
                                              zc::WriteMode::CREATE | zc::WriteMode::PRIVATE);
    auto outputFactory = zc::Own<FreshSourceDirectoryFactory>(
        zc::heap<DiskFreshDirectoryFactory>(*testRoot, "captured-output"_zc));
    auto sandboxName = scalar<identity::CanonicalPathSegment>("isolated-run"_zc);
    auto input = emptyInputSnapshot();
    auto platform = createProductionLinuxNativeSandboxPlatform(
        executable(), input, *sandboxParent, zc::str(rootAbsolutePath, "/sandboxes"_zc),
        zc::mv(sandboxName), cgroupParent, zc::mv(outputFactory));
    ZC_REQUIRE(platform.is<zc::Own<LinuxNativeSandboxPlatform>>());
    auto buildLimits = limits();
    auto sandbox = LinuxNativeSandboxV1::create(
        zc::mv(platform.get<zc::Own<LinuxNativeSandboxPlatform>>()), buildLimits);
    ZC_REQUIRE(sandbox.is<zc::Own<BuildScriptSandboxAdapter>>());
    auto adapter = zc::mv(sandbox.get<zc::Own<BuildScriptSandboxAdapter>>());
    auto result = adapter->execute(request());
    if (result.is<BuildScriptIssue>()) {
      ZC_FAIL_REQUIRE("production Linux sandbox returned issue ",
                      static_cast<uint32_t>(result.get<BuildScriptIssue>()));
    }
    ZC_REQUIRE(result.is<VerifiedBuildScriptRun>());
    ZC_EXPECT(result.get<VerifiedBuildScriptRun>().outputs().files().size() == 0);
    ZC_EXPECT(result.get<VerifiedBuildScriptRun>().response().exportedEnvironment().size() == 0);
    ZC_EXPECT(adapter->finish() == zc::none);
  }
  testRoot = nullptr;
  filesystem->getRoot().remove(rootPath);
}

}  // namespace zomlang::compiler::driver::package
