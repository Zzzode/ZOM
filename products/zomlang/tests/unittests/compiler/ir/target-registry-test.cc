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

#include "zomlang/compiler/ir/target-registry.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::ir {
namespace {

// Production target registry fixtures use only canonical package and target facts.

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid target registry scalar fixture");
}

driver::package::RegisteredTargetProfileName profileName(zc::StringPtr text) {
  auto result = driver::package::RegisteredTargetProfileName::from(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid target registry profile fixture");
}

identity::CanonicalTargetSpecificationKey projection(zc::StringPtr architecture = "x86_64"_zc) {
  zc::Vector<identity::TargetFeatureName> features;
  features.add(scalar<identity::TargetFeatureName>("sse2"_zc));
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(featureValues, sorted) {
    auto result = identity::CanonicalTargetSpecificationKey::from(
        scalar<identity::TargetComponentName>(architecture),
        scalar<identity::TargetComponentName>("zom"_zc),
        scalar<identity::TargetComponentName>("none"_zc),
        scalar<identity::TargetComponentName>("unknown"_zc),
        scalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
        zc::mv(featureValues));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid target registry projection fixture");
}

CanonicalTargetSpec oracleSpec(BackendPanicStrategy panic = BackendPanicStrategy::Unwind) {
  zc::Vector<CanonicalTargetFeature> features;
  auto feature = CanonicalTargetFeature::from("sse2"_zc, TargetFeatureState::Enabled);
  ZC_IF_SOME(value, feature) { features.add(zc::mv(value)); }
  auto result = CanonicalTargetSpec::from("x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc,
                                          zc::mv(features), "zom"_zc, panic, ObjectFormat::Elf);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid target specification fixture");
}

RegisteredTargetProfileRecord profile(bool includeAbort = false) {
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  semanticFeatures.add(scalar<identity::TargetFeatureName>("sse2"_zc));
  zc::Vector<CanonicalTargetSpec> specifications;
  specifications.add(oracleSpec());
  if (includeAbort) { specifications.add(oracleSpec(BackendPanicStrategy::Abort)); }
  auto result = RegisteredTargetProfileRecord::from(
      profileName("host"_zc), projection(), zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid target profile fixture");
}

TargetRegistrySnapshot snapshot(bool includeAbort = false) {
  zc::Vector<RegisteredTargetProfileRecord> profiles;
  profiles.add(profile(includeAbort));
  auto result = TargetRegistrySnapshot::from(profileName("host"_zc), zc::mv(profiles));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid target registry snapshot fixture");
}

identity::CanonicalTargetSpecificationKey targetProjection(zc::StringPtr architecture,
                                                           zc::StringPtr vendor,
                                                           zc::StringPtr operatingSystem,
                                                           zc::StringPtr environment,
                                                           uint32_t pointerWidth) {
  zc::Vector<identity::TargetFeatureName> features;
  auto featureSet = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(values, featureSet) {
    auto result = identity::CanonicalTargetSpecificationKey::from(
        scalar<identity::TargetComponentName>(architecture),
        scalar<identity::TargetComponentName>(vendor),
        scalar<identity::TargetComponentName>(operatingSystem),
        scalar<identity::TargetComponentName>(environment),
        scalar<identity::TargetComponentName>("zom"_zc), pointerWidth, identity::Endianness::Little,
        zc::mv(values));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid cross-platform target projection fixture");
}

TargetRegistrySnapshot registryFor(CanonicalTargetSpec&& specification,
                                   identity::CanonicalTargetSpecificationKey&& projection) {
  zc::Vector<CanonicalTargetSpec> specifications;
  specifications.add(zc::mv(specification));
  zc::Vector<identity::TargetFeatureName> features;
  auto profile = RegisteredTargetProfileRecord::from(profileName("host"_zc), zc::mv(projection),
                                                     zc::mv(features), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<RegisteredTargetProfileRecord> profiles;
  profiles.add(zc::mv(ZC_REQUIRE_NONNULL(profile)));
  auto registry = TargetRegistrySnapshot::from(profileName("host"_zc), zc::mv(profiles));
  ZC_REQUIRE(registry != zc::none);
  return zc::mv(ZC_REQUIRE_NONNULL(registry));
}

zc::Maybe<RegisteredTargetProfileRecord> profileFor(
    CanonicalTargetSpec&& specification, identity::CanonicalTargetSpecificationKey&& projection) {
  zc::Vector<CanonicalTargetSpec> specifications;
  specifications.add(zc::mv(specification));
  zc::Vector<identity::TargetFeatureName> features;
  return RegisteredTargetProfileRecord::from(profileName("host"_zc), zc::mv(projection),
                                             zc::mv(features), zc::mv(specifications));
}

}  // namespace

ZC_TEST("Canonical target specification matches the RFC 0010 fixed oracle") {
  auto spec = oracleSpec();
  ZC_EXPECT(zc::encodeHex(spec.targetSpecId().bytes()) ==
            "b5171e0d457c8ddac8eec7df5625c5edcec1b4b20d1f42945053ce95300c4c0b"_zc);
}

ZC_TEST("Canonical target specification preserves a Mach-O target without host defaults") {
  zc::Vector<CanonicalTargetFeature> features;
  auto feature = CanonicalTargetFeature::from("neon"_zc, TargetFeatureState::Enabled);
  ZC_REQUIRE(feature != zc::none);
  ZC_IF_SOME(value, feature) { features.add(zc::mv(value)); }
  auto specification = CanonicalTargetSpec::from(
      "aarch64-apple-darwin"_zc, "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"_zc, "apple-m1"_zc,
      zc::mv(features), "zom"_zc, BackendPanicStrategy::Abort, ObjectFormat::MachO);
  ZC_REQUIRE(specification != zc::none);
  ZC_IF_SOME(value, specification) {
    ZC_EXPECT(value.triple() == "aarch64-apple-darwin"_zc);
    ZC_EXPECT(value.llvmDataLayout() == "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"_zc);
    ZC_EXPECT(value.cpu() == "apple-m1"_zc);
    ZC_EXPECT(value.panicStrategy() == BackendPanicStrategy::Abort);
    ZC_EXPECT(value.objectFormat() == ObjectFormat::MachO);
  }
}

ZC_TEST("Target registry verifies a Mach-O host profile selection") {
  zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
  auto specification = CanonicalTargetSpec::from(
      "aarch64-apple-darwin"_zc, "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"_zc, "apple-m1"_zc,
      zc::mv(backendFeatures), "zom"_zc, BackendPanicStrategy::Abort, ObjectFormat::MachO);
  ZC_REQUIRE(specification != zc::none);
  auto registry =
      registryFor(zc::mv(ZC_REQUIRE_NONNULL(specification)),
                  targetProjection("aarch64"_zc, "apple"_zc, "darwin"_zc, "unknown"_zc, 64));

  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  auto selection =
      ZC_REQUIRE_NONNULL(service).select(zc::none, driver::package::PackagePanicStrategy::Abort);
  ZC_REQUIRE(selection != zc::none);
  auto verified = registry.verify(ZC_REQUIRE_NONNULL(selection));
  ZC_REQUIRE(verified.is<VerifiedTargetSelection>());
  ZC_EXPECT(verified.get<VerifiedTargetSelection>().targetSpec().triple() ==
            "aarch64-apple-darwin"_zc);
  ZC_EXPECT(verified.get<VerifiedTargetSelection>().targetSpec().objectFormat() ==
            ObjectFormat::MachO);
}

ZC_TEST("Target registry verifies an ELF host profile selection") {
  zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
  auto specification = CanonicalTargetSpec::from(
      "x86_64-unknown-linux-gnu"_zc,
      "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"_zc,
      "generic"_zc, zc::mv(backendFeatures), "zom"_zc, BackendPanicStrategy::Unwind,
      ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);
  auto registry =
      registryFor(zc::mv(ZC_REQUIRE_NONNULL(specification)),
                  targetProjection("x86_64"_zc, "unknown"_zc, "linux"_zc, "gnu"_zc, 64));

  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  auto selection =
      ZC_REQUIRE_NONNULL(service).select(zc::none, driver::package::PackagePanicStrategy::Unwind);
  ZC_REQUIRE(selection != zc::none);
  auto verified = registry.verify(ZC_REQUIRE_NONNULL(selection));
  ZC_REQUIRE(verified.is<VerifiedTargetSelection>());
  ZC_EXPECT(verified.get<VerifiedTargetSelection>().targetSpec().triple() ==
            "x86_64-unknown-linux-gnu"_zc);
  ZC_EXPECT(verified.get<VerifiedTargetSelection>().targetSpec().objectFormat() ==
            ObjectFormat::Elf);
}

ZC_TEST("Target registry verifies a 32-bit ELF host profile selection") {
  zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
  auto specification = CanonicalTargetSpec::from(
      "i686-unknown-linux-gnu"_zc,
      "e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-i128:128-f64:32:64-f80:32-n8:16:32-S128"_zc,
      "pentium4"_zc, zc::mv(backendFeatures), "zom"_zc, BackendPanicStrategy::Unwind,
      ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);
  auto registry = registryFor(zc::mv(ZC_REQUIRE_NONNULL(specification)),
                              targetProjection("i686"_zc, "unknown"_zc, "linux"_zc, "gnu"_zc, 32));

  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  auto selection =
      ZC_REQUIRE_NONNULL(service).select(zc::none, driver::package::PackagePanicStrategy::Unwind);
  ZC_REQUIRE(selection != zc::none);
  auto verified = registry.verify(ZC_REQUIRE_NONNULL(selection));
  ZC_REQUIRE(verified.is<VerifiedTargetSelection>());
  ZC_EXPECT(verified.get<VerifiedTargetSelection>().targetSpec().triple() ==
            "i686-unknown-linux-gnu"_zc);
}

ZC_TEST("Target registry issues and verifies one revision-bound package selection") {
  auto registry = snapshot();
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto selected = targets.select(zc::none, driver::package::PackagePanicStrategy::Unwind);
    ZC_REQUIRE(selected != zc::none);
    ZC_IF_SOME(selection, selected) {
      identity::CanonicalEncoder selectionEncoder;
      selection.encode(selectionEncoder);
      auto oracleDigest = identity::sha256(selectionEncoder.finish().asPtr());
      ZC_REQUIRE(oracleDigest != zc::none);
      ZC_IF_SOME(value, oracleDigest) {
        ZC_EXPECT(zc::encodeHex(value.bytes()) ==
                  "c469c35f6f5a8258d48e965149c189c9a859201f6e4db6d80e230b47a8891f28"_zc);
      }
      auto verified = registry.verify(selection);
      ZC_REQUIRE(verified.is<VerifiedTargetSelection>());
      ZC_EXPECT(verified.get<VerifiedTargetSelection>().targetSpecId() ==
                oracleSpec().targetSpecId());
    }
  }
}

ZC_TEST("Target registry rejects unavailable panic and foreign registry revisions") {
  auto unwindOnly = snapshot();
  auto service = unwindOnly.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto abort = targets.select(zc::none, driver::package::PackagePanicStrategy::Abort);
    ZC_REQUIRE(abort != zc::none);
    ZC_IF_SOME(selection, abort) {
      auto result = unwindOnly.verify(selection);
      ZC_REQUIRE(result.is<TargetSelectionVerificationIssue>());
      ZC_EXPECT(result.get<TargetSelectionVerificationIssue>() ==
                TargetSelectionVerificationIssue::CapabilityUnavailable);
    }

    auto otherRegistry = snapshot(true);
    auto unwind = targets.select(zc::none, driver::package::PackagePanicStrategy::Unwind);
    ZC_REQUIRE(unwind != zc::none);
    ZC_IF_SOME(selection, unwind) {
      auto result = otherRegistry.verify(selection);
      ZC_REQUIRE(result.is<TargetSelectionVerificationIssue>());
      ZC_EXPECT(result.get<TargetSelectionVerificationIssue>() ==
                TargetSelectionVerificationIssue::RegistryRevisionMismatch);
    }
  }
}

ZC_TEST("Target registry rejects a semantic projection mismatch before publication") {
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  semanticFeatures.add(scalar<identity::TargetFeatureName>("sse2"_zc));
  zc::Vector<CanonicalTargetSpec> specifications;
  specifications.add(oracleSpec());
  auto invalid =
      RegisteredTargetProfileRecord::from(profileName("host"_zc), projection("aarch64"_zc),
                                          zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_EXPECT(invalid == zc::none);
}

ZC_TEST("Target registry rejects conflicting default pointer layouts") {
  zc::Vector<CanonicalTargetFeature> backendFeatures;
  auto specification = CanonicalTargetSpec::from("x86_64-unknown-linux"_zc, "e-p:64:64-p:32:32"_zc,
                                                 "generic"_zc, zc::mv(backendFeatures), "zom"_zc,
                                                 BackendPanicStrategy::Unwind, ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);

  auto profile =
      profileFor(zc::mv(ZC_REQUIRE_NONNULL(specification)),
                 targetProjection("x86_64"_zc, "unknown"_zc, "linux"_zc, "unknown"_zc, 64));
  ZC_EXPECT(profile == zc::none);
}

ZC_TEST("Target registry rejects a malformed default pointer layout") {
  zc::Vector<CanonicalTargetFeature> backendFeatures;
  auto specification = CanonicalTargetSpec::from("x86_64-unknown-linux"_zc, "e-p:64:"_zc,
                                                 "generic"_zc, zc::mv(backendFeatures), "zom"_zc,
                                                 BackendPanicStrategy::Unwind, ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);

  auto profile =
      profileFor(zc::mv(ZC_REQUIRE_NONNULL(specification)),
                 targetProjection("x86_64"_zc, "unknown"_zc, "linux"_zc, "unknown"_zc, 64));
  ZC_EXPECT(profile == zc::none);
}

ZC_TEST("Target registry rejects an overflowing default pointer layout") {
  zc::Vector<CanonicalTargetFeature> backendFeatures;
  auto specification = CanonicalTargetSpec::from("x86_64-unknown-linux"_zc, "e-p:42949672960:64"_zc,
                                                 "generic"_zc, zc::mv(backendFeatures), "zom"_zc,
                                                 BackendPanicStrategy::Unwind, ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);

  auto profile =
      profileFor(zc::mv(ZC_REQUIRE_NONNULL(specification)),
                 targetProjection("x86_64"_zc, "unknown"_zc, "linux"_zc, "unknown"_zc, 64));
  ZC_EXPECT(profile == zc::none);
}

ZC_TEST("Target registry rejects empty data-layout components") {
  zc::Vector<CanonicalTargetFeature> backendFeatures;
  auto specification = CanonicalTargetSpec::from("x86_64-unknown-linux"_zc, "e-p:64:64-"_zc,
                                                 "generic"_zc, zc::mv(backendFeatures), "zom"_zc,
                                                 BackendPanicStrategy::Unwind, ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);

  auto profile =
      profileFor(zc::mv(ZC_REQUIRE_NONNULL(specification)),
                 targetProjection("x86_64"_zc, "unknown"_zc, "linux"_zc, "unknown"_zc, 64));
  ZC_EXPECT(profile == zc::none);
}

ZC_TEST("Target registry rejects a non-canonical endianness component") {
  zc::Vector<CanonicalTargetFeature> backendFeatures;
  auto specification = CanonicalTargetSpec::from("x86_64-unknown-linux"_zc, "e64-p:64:64"_zc,
                                                 "generic"_zc, zc::mv(backendFeatures), "zom"_zc,
                                                 BackendPanicStrategy::Unwind, ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);

  auto profile =
      profileFor(zc::mv(ZC_REQUIRE_NONNULL(specification)),
                 targetProjection("x86_64"_zc, "unknown"_zc, "linux"_zc, "unknown"_zc, 64));
  ZC_EXPECT(profile == zc::none);
}

}  // namespace zomlang::compiler::ir
