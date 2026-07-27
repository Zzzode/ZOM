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

#include "zomlang/compiler/type/semantic-type-key.h"

#include "zc/core/encoding.h"
#include "zc/core/memory.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang::compiler::type::semantic {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto admitted = Scalar::fromCanonical(text);
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

identity::PackageKey package() {
  zc::Vector<identity::CanonicalPathSegment> path;
  auto version = identity::ResolvedVersion::fromCanonical("0.0.0"_zc);
  zc::Vector<identity::FeatureName> features;
  auto sortedFeatures = identity::SortedFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(version != zc::none);
  ZC_REQUIRE(sortedFeatures != zc::none);
  ZC_IF_SOME(versionValue, version) {
    ZC_IF_SOME(featureValues, sortedFeatures) {
      return identity::PackageKey::from(
          identity::CanonicalPackageSource::localPath(
              identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))),
          scalar<identity::PackageName>("test"_zc), zc::mv(versionValue), zc::mv(featureValues));
    }
  }
  ZC_UNREACHABLE;
}

identity::CanonicalTargetSpecificationKey target() {
  zc::Vector<identity::TargetFeatureName> features;
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(values, sorted) {
    auto admitted = identity::CanonicalTargetSpecificationKey::from(
        scalar<identity::TargetComponentName>("x"_zc),
        scalar<identity::TargetComponentName>("v"_zc),
        scalar<identity::TargetComponentName>("o"_zc),
        scalar<identity::TargetComponentName>("e"_zc),
        scalar<identity::TargetComponentName>("a"_zc), 64, identity::Endianness::Little,
        zc::mv(values));
    ZC_REQUIRE(admitted != zc::none);
    ZC_IF_SOME(value, admitted) { return zc::mv(value); }
  }
  ZC_UNREACHABLE;
}

identity::CrateKey crate() {
  zc::Maybe<identity::BuildScriptProducerKey> noOutput;
  auto compilation = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, target(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, true), zc::mv(noOutput));
  ZC_REQUIRE(compilation != zc::none);
  ZC_IF_SOME(value, compilation) {
    auto admitted = identity::CrateKey::from(
        identity::CompilationUnitIdentity::userPackage(package()),
        identity::CrateTargetKind::Library, scalar<identity::TargetName>("test"_zc), zc::mv(value));
    ZC_REQUIRE(admitted != zc::none);
    ZC_IF_SOME(result, admitted) { return zc::mv(result); }
  }
  ZC_UNREACHABLE;
}

identity::ModuleKey module() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("test"_zc));
  auto admitted = identity::ModuleKey::from(crate(), zc::mv(path));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

class RegistryStoreFixture final {
public:
  RegistryStoreFixture() {
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    ZC_IF_SOME(value, issuedContext) { contextValue = value; }

    auto created = identity::SemanticIdentityRegistrySet::create(factory, contextValue);
    ZC_REQUIRE(created != zc::none);
    ZC_IF_SOME(value, created) {
      registriesValue = zc::heap<identity::SemanticIdentityRegistrySet>(zc::mv(value));
    }
    buildRegistry();

    auto token = factory.issueSemanticTypeStoreConstructionToken(contextValue);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(value, token) {
      storeValue = zc::heap<type::SemanticTypeStore>(zc::mv(value), registries());
    }
  }

  type::SemanticTypeStore& store() { return *storeValue; }
  const identity::GenericParameterKey& parameter() const {
    ZC_IF_SOME(value, registries().genericParameters().keyAt(0)) { return value; }
    ZC_UNREACHABLE;
  }

private:
  void buildRegistry() {
    ZC_REQUIRE(registries().collectCompilationUnit(identity::CompilationUnitIdentity::userPackage(
                   package())) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().collectModule(module()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().freezeModules() == identity::FrozenRegistryFailure::None);

    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Maybe<identity::OverloadHeaderDigest> noOverload;
    auto definition = identity::DefinitionIdentityRecord::from(
        module(), zc::mv(owners), identity::DefinitionKind::Class,
        identity::DefinitionNamespace::Type, scalar<identity::DeclaredDefinitionName>("Owner"_zc),
        zc::mv(noOverload));
    ZC_REQUIRE(definition != zc::none);
    zc::Maybe<identity::DefinitionKey> ownerKey;
    ZC_IF_SOME(value, definition) {
      ownerKey = identity::DefinitionKey::compute(value);
      zc::Maybe<identity::OverloadHeaderAuthority> noOverloadAuthority;
      ZC_REQUIRE(registries().collectDefinition(zc::mv(value), zc::mv(noOverloadAuthority)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries().freezeStableIdentities() == identity::FrozenRegistryFailure::None);

    ZC_REQUIRE(ownerKey != zc::none);
    ZC_IF_SOME(value, ownerKey) {
      auto parameter = identity::GenericParameterIdentityRecord::type(
          identity::StableGenericParameterOwnerKey::definition(zc::mv(value)), 0);
      ZC_REQUIRE(registries().collectGenericParameter(zc::mv(parameter)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries().freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().freezeCallableParameters() == identity::FrozenRegistryFailure::None);
  }

  identity::SemanticIdentityRegistrySet& registries() { return *registriesValue; }
  const identity::SemanticIdentityRegistrySet& registries() const { return *registriesValue; }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand contextValue;
  zc::Own<identity::SemanticIdentityRegistrySet> registriesValue;
  zc::Own<type::SemanticTypeStore> storeValue;
};

}  // namespace

ZC_TEST("SemanticTypeKey.EncodesGenericParameterKeyAsRawDigest") {
  RegistryStoreFixture fixture;
  const auto& parameter = fixture.parameter();
  auto canonical =
      fixture.store().canonicalizeClosed(TypeData(TypeParameterTypeData{parameter.clone()}));
  ZC_REQUIRE(canonical.is<CanonicalTypeData>());

  auto interned = fixture.store().intern(zc::mv(canonical.get<CanonicalTypeData>()));
  ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
  auto lookup = fixture.store().get(interned.get<type::SemanticTypeInterned>().id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());

  const auto& stored = lookup.get<type::SemanticTypeLookup>();
  ZC_EXPECT(stored.data().tag() == TypeDataTag::TypeParameter);
  ZC_EXPECT(stored.data().get<TypeParameterTypeData>().parameter == parameter);
  ZC_REQUIRE(stored.key().bytes().size() == 55);
  ZC_EXPECT(zc::encodeHex(stored.key().bytes().slice(0, 23)) ==
            "7a6f6d2e73656d616e7469632d747970652d6b65790009"_zc);
  ZC_EXPECT(stored.key().bytes().slice(23) == parameter.bytes());
}

ZC_TEST("SemanticTypeData.PreservesGenericParameterKeyCloneAndEquality") {
  RegistryStoreFixture fixture;
  const auto& parameter = fixture.parameter();
  TypeData data(TypeParameterTypeData{parameter.clone()});

  ZC_EXPECT(data.get<TypeParameterTypeData>().parameter == parameter);
  ZC_EXPECT(data.get<TypeParameterTypeData>().parameter.bytes().size() == 32);
  ZC_EXPECT(data.get<TypeParameterTypeData>().parameter.bytes() == parameter.bytes());
}

ZC_TEST("SemanticTypeKey.RejectsUnretainedGenericParameterKey") {
  RegistryStoreFixture fixture;
  auto unretained =
      identity::GenericParameterKey::fromBytes(zc::heapArray<uint8_t>(32, uint8_t{0xff}));
  ZC_REQUIRE(unretained != zc::none);
  ZC_IF_SOME(value, unretained) {
    auto result =
        fixture.store().canonicalizeClosed(TypeData(TypeParameterTypeData{zc::mv(value)}));
    ZC_REQUIRE(result.is<identity::IdentityInvariant>());
    const auto& invariant = result.get<identity::IdentityInvariant>();
    ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidHandle);
    ZC_EXPECT(invariant.phase() == identity::IdentityAllocationPhase::GenericParameter);
  }
}

}  // namespace zomlang::compiler::type::semantic
