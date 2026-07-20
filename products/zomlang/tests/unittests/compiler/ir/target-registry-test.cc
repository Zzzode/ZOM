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
#include "zomlang/compiler/identity/canonical-encoder.h"

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
        scalar<identity::TargetComponentName>("zom-v1"_zc), 64, identity::Endianness::Little,
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
                                          zc::mv(features), "zom-v1"_zc, panic, ObjectFormat::Elf);
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

}  // namespace

ZC_TEST("Canonical target specification matches the RFC 0010 fixed oracle") {
  auto spec = oracleSpec();
  ZC_EXPECT(zc::encodeHex(spec.targetSpecId().bytes()) ==
            "6d72a26055117cb6e84c3cc3a72fd4c1e42caf861138d8f84f5bf34f2f244d37"_zc);
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
                  "ee53bebededb1c6020619cc95979fe960814b4ef732afcba82cb96157546febc"_zc);
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

}  // namespace zomlang::compiler::ir
