// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/semantic-identity-registry-set.h"

namespace zomlang::compiler::tests {
namespace test_identity_detail {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid semantic identity test scalar");
}

inline identity::Sha256Digest digest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto result = identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("invalid semantic identity test digest");
}

inline identity::PackageKey package() {
  zc::Vector<identity::CanonicalPathSegment> path;
  auto version = identity::ResolvedVersion::fromCanonical("0.0.0"_zc);
  zc::Vector<identity::FeatureName> features;
  auto sortedFeatures = identity::SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(versionValue, version) {
    ZC_IF_SOME(featureValues, sortedFeatures) {
      return identity::PackageKey::from(
          identity::CanonicalPackageSource::localPath(
              identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))),
          scalar<identity::PackageName>("test"_zc), zc::mv(versionValue), zc::mv(featureValues));
    }
  }
  ZC_FAIL_REQUIRE("invalid semantic identity test package");
}

inline identity::CanonicalTargetSpecificationKey target() {
  zc::Vector<identity::TargetFeatureName> features;
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(values, sorted) {
    auto result = identity::CanonicalTargetSpecificationKey::from(
        scalar<identity::TargetComponentName>("x"_zc),
        scalar<identity::TargetComponentName>("v"_zc),
        scalar<identity::TargetComponentName>("o"_zc),
        scalar<identity::TargetComponentName>("e"_zc),
        scalar<identity::TargetComponentName>("a"_zc), 64, identity::Endianness::Little,
        zc::mv(values));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid semantic identity test target");
}

inline identity::CrateKey crate() {
  zc::Maybe<identity::BuildScriptProducerKey> noOutput;
  auto compilation = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, target(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, true), zc::mv(noOutput));
  ZC_IF_SOME(value, compilation) {
    auto result = identity::CrateKey::from(package(), identity::CrateTargetKind::Library,
                                           scalar<identity::TargetName>("test"_zc), zc::mv(value));
    ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  }
  ZC_FAIL_REQUIRE("invalid semantic identity test crate");
}

inline identity::SourceFileKey source() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>("test.zom"_zc));
  return identity::SourceFileKey::from(
      crate(), identity::SourceOriginKey::localFile(
                   identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments))));
}

inline identity::ModuleKey module() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("test"_zc));
  auto result = identity::ModuleKey::from(crate(), zc::mv(path));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid semantic identity test module");
}

}  // namespace test_identity_detail

inline zc::Vector<identity::DefId> makeTestDefinitionIds(size_t count) {
  using namespace test_identity_detail;
  identity::SemanticContextFactory factory;
  auto context = factory.issue();
  ZC_REQUIRE(context != zc::none);
  zc::Maybe<identity::SemanticIdentityRegistrySet> registries;
  ZC_IF_SOME(value, context) {
    registries = identity::SemanticIdentityRegistrySet::create(factory, value);
  }
  ZC_REQUIRE(registries != zc::none);

  zc::Vector<identity::DefinitionKey> retained(count);
  ZC_IF_SOME(values, registries) {
    ZC_REQUIRE(values.collectPackage(package()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.freezePackages() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.freezeCrates() == identity::FrozenRegistryFailure::None);
    auto snapshot =
        identity::ImmutableSourceSnapshot::from(source(), zc::heapArray<uint8_t>(1, uint8_t{0}));
    ZC_REQUIRE(snapshot != zc::none);
    ZC_IF_SOME(sourceValue, snapshot) {
      ZC_REQUIRE(values.collectSourceFile(zc::mv(sourceValue)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(values.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.collectModule(module()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.freezeModules() == identity::FrozenRegistryFailure::None);
    for (size_t index = 0; index < count; ++index) {
      auto text = zc::str("definition", static_cast<uint64_t>(index));
      auto name = identity::DeclaredDefinitionName::fromCanonical(text);
      ZC_REQUIRE(name != zc::none);
      zc::Vector<identity::EnclosingStableOwnerKey> owners;
      zc::Maybe<identity::OverloadHeaderDigest> noOverloadDigest;
      ZC_IF_SOME(nameValue, name) {
        auto record = identity::DefinitionIdentityRecord::from(
            module(), zc::mv(owners), identity::DefinitionKind::Class,
            identity::DefinitionNamespace::Type, zc::mv(nameValue), zc::mv(noOverloadDigest));
        ZC_REQUIRE(record != zc::none);
        ZC_IF_SOME(recordValue, record) {
          retained.add(identity::DefinitionKey::compute(recordValue));
          zc::Maybe<identity::OverloadHeaderAuthority> noOverloadAuthority;
          ZC_REQUIRE(values.collectDefinition(recordValue.clone(), zc::mv(noOverloadAuthority)) ==
                     identity::FrozenRegistryFailure::None);
        }
      }
    }
    ZC_REQUIRE(values.freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.freezeCallableParameters() == identity::FrozenRegistryFailure::None);
    zc::Vector<identity::DefId> result(count);
    for (const auto& key : retained) {
      auto handle = values.definitions().find(key);
      ZC_REQUIRE(handle != zc::none);
      ZC_IF_SOME(value, handle) { result.add(value); }
    }
    return result;
  }
  ZC_UNREACHABLE;
}

inline identity::DefId testDefinition(uint32_t ordinal) {
  auto identities = makeTestDefinitionIds(static_cast<size_t>(ordinal) + 1);
  return identities[ordinal];
}

}  // namespace zomlang::compiler::tests
