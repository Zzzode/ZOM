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

#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/semantic-identity-registry-set.h"

namespace zomlang::compiler::identity {
namespace {

SemanticContextBrand requireContext(SemanticContextFactory& factory) {
  auto issued = factory.issue();
  ZC_IF_SOME(context, issued) { return context; }
  ZC_FAIL_REQUIRE("semantic context brand space exhausted during registry test");
}

SemanticIdentityRegistrySet requireRegistrySet(SemanticContextFactory& factory,
                                               SemanticContextBrand context) {
  auto created = SemanticIdentityRegistrySet::create(factory, context);
  ZC_IF_SOME(registries, created) { return zc::mv(registries); }
  ZC_FAIL_REQUIRE("semantic context registry set was already claimed during registry test");
}

PackageName requirePackageName(zc::StringPtr text) {
  auto value = PackageName::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid package-name test input");
}

template <typename Scalar>
Scalar requireScalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical scalar test input");
}

ResolvedVersion requireVersion() {
  auto value = ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid semantic-version test input");
}

SortedFeatureSet emptyFeatures() {
  zc::Vector<FeatureName> features;
  auto value = SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty feature set was rejected");
}

SortedTargetFeatureSet emptyTargetFeatures() {
  zc::Vector<TargetFeatureName> features;
  auto value = SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty target feature set was rejected");
}

Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto digest = Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(value, digest) { return value; }
  ZC_FAIL_REQUIRE("invalid digest test input");
}

PackageKey localPackage(zc::StringPtr name) {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return PackageKey::from(CanonicalPackageSource::localPath(zc::mv(path)), requirePackageName(name),
                          requireVersion(), emptyFeatures());
}

CanonicalTargetSpecificationKey targetSpec() {
  auto value = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc), requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc), requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), 64, Endianness::Little, emptyTargetFeatures());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid target specification test input");
}

CompilationConfigKey targetCompilation() {
  zc::Maybe<BuildScriptProducerKey> output = BuildScriptProducerKey::from(repeatedDigest(0x11));
  auto value = CompilationConfigKey::from(
      CompilationDomain::Target, targetSpec(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(output));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid compilation configuration test input");
}

CrateKey crate(zc::StringPtr packageName = "a"_zc) {
  auto value = CrateKey::from(localPackage(packageName), CrateTargetKind::Library,
                              requireScalar<TargetName>("lib"_zc), targetCompilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

SourceFileKey source(zc::StringPtr packageName = "a"_zc) {
  zc::Vector<CanonicalPathSegment> segments;
  segments.add(requireScalar<CanonicalPathSegment>("g.zom"_zc));
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  auto origin = SourceOriginKey::localFile(zc::mv(path));
  return SourceFileKey::from(crate(packageName), zc::mv(origin));
}

SourceFileKey generatedSource() {
  zc::Vector<CanonicalPathSegment> segments;
  segments.add(requireScalar<CanonicalPathSegment>("g.zom"_zc));
  auto path = CanonicalRelativePath::from(zc::mv(segments));
  auto origin = SourceOriginKey::generatedFile(BuildScriptProducerKey::from(repeatedDigest(0x11)),
                                               zc::mv(path));
  return SourceFileKey::from(crate("a"_zc), zc::mv(origin));
}

ImmutableSourceSnapshot snapshot(zc::StringPtr packageName, uint8_t contentByte) {
  auto value =
      ImmutableSourceSnapshot::from(source(packageName), zc::heapArray<uint8_t>(2, contentByte));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid immutable source snapshot was rejected");
}

ModuleKey module(zc::StringPtr packageName) {
  zc::Vector<ModulePathSegment> path;
  path.add(requireScalar<ModulePathSegment>("m"_zc));
  auto value = ModuleKey::from(crate(packageName), zc::mv(path));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid module test input was rejected");
}

DeclaredDefinitionName definitionName(zc::StringPtr text) {
  return requireScalar<DeclaredDefinitionName>(text);
}

SemanticIdentifier identifier(zc::StringPtr text) {
  return requireScalar<SemanticIdentifier>(text);
}

CanonicalNameReference traitName() {
  zc::Vector<SemanticIdentifier> suffix;
  suffix.add(identifier("Trait"_zc));
  auto value = CanonicalNameReference::from(CanonicalNameRoot::relative(), zc::mv(suffix));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid trait name was rejected");
}

CanonicalImplHeader implHeader() {
  zc::Vector<CanonicalHeaderTypeSyntax> arguments;
  auto trait = CanonicalTraitReference::from(traitName(), zc::mv(arguments));
  ZC_REQUIRE(trait != zc::none);
  auto selfType = CanonicalHeaderTypeSyntax::predefined(PredefinedTypeKind::I32);
  ZC_REQUIRE(selfType != zc::none);
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  ZC_IF_SOME(traitValue, trait) {
    ZC_IF_SOME(selfTypeValue, selfType) {
      auto value =
          CanonicalImplHeader::from(zc::mv(generics), ImplPolarity::Positive, ImplSafety::Safe,
                                    zc::mv(traitValue), zc::mv(selfTypeValue), zc::mv(obligations));
      ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
    }
  }
  ZC_FAIL_REQUIRE("valid implementation header was rejected");
}

DefinitionIdentityRecord definitionRecord(zc::StringPtr packageName, zc::StringPtr name = "f"_zc,
                                          zc::Vector<EnclosingStableOwnerKey>&& owners = {}) {
  zc::Maybe<OverloadHeaderDigest> noOverload;
  auto value = DefinitionIdentityRecord::from(module(packageName), zc::mv(owners),
                                              DefinitionKind::Class, DefinitionNamespace::Type,
                                              definitionName(name), zc::mv(noOverload));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid definition identity record was rejected");
}

DefinitionKey definition(zc::StringPtr packageName) {
  auto record = definitionRecord(packageName);
  return DefinitionKey::compute(record);
}

ImplIdentityRecord implRecord(zc::StringPtr packageName,
                              zc::Vector<EnclosingStableOwnerKey>&& owners = {}) {
  return ImplIdentityRecord::from(module(packageName), zc::mv(owners), implHeader());
}

ImplKey impl(zc::StringPtr packageName) {
  auto record = implRecord(packageName);
  return ImplKey::compute(record);
}

DefinitionKey rawDefinitionKey(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto key = DefinitionKey::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(admitted, key) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid raw definition key was rejected");
}

FrozenRegistryFailure admitDefinition(SemanticIdentityRegistrySet& registries,
                                      zc::StringPtr packageName, zc::StringPtr name = "f"_zc) {
  zc::Maybe<OverloadHeaderAuthority> noOverload;
  return registries.collectDefinition(definitionRecord(packageName, name), zc::mv(noOverload));
}

FrozenRegistryFailure admitImpl(SemanticIdentityRegistrySet& registries,
                                zc::StringPtr packageName) {
  return registries.collectImpl(implRecord(packageName));
}

SemanticIdentityRegistrySet populatedRegistrySet(SemanticContextFactory& factory) {
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_REQUIRE(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(crate("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCrates() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(snapshot("a"_zc, 0x41)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeSourceFiles() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(module("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeModules() == FrozenRegistryFailure::None);
  ZC_REQUIRE(admitDefinition(registries, "a"_zc) == FrozenRegistryFailure::None);
  ZC_REQUIRE(admitImpl(registries, "a"_zc) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeStableIdentities() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeGenericParameters() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCallableParameters() == FrozenRegistryFailure::None);
  return registries;
}

void expectPackage(const PackageRegistry& registry, size_t slot, zc::StringPtr name) {
  auto expected = localPackage(name).encode();
  bool matched = false;
  ZC_IF_SOME(key, registry.keyAt(slot)) {
    ZC_EXPECT(key.encode().asPtr() == expected.asPtr());
    matched = true;
  }
  ZC_EXPECT(matched);
}

void expectSingleAncestorMismatch(SemanticIdentityRegistrySet& registries,
                                  IdentityAllocationPhase phase) {
  registries.sortIdentityInvariants();
  ZC_REQUIRE(registries.identityInvariants().size() == 1);
  const auto& invariant = registries.identityInvariants()[0];
  ZC_EXPECT(invariant.kind() == IdentityInvariantKind::AncestorMismatch);
  ZC_EXPECT(invariant.phase() == phase);
  ZC_EXPECT(invariant.apiSite() == IdentityApiSite::RegistryMutation);
  ZC_EXPECT(invariant.structuralInputKey() != zc::none);
}

}  // namespace

ZC_TEST("Frozen registry assigns slots in canonical key order") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));

  ZC_EXPECT(registries.collectPackage(localPackage("b"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.packages().isFrozen());
  ZC_EXPECT(registries.packages().size() == 2);
  expectPackage(registries.packages(), 0, "a"_zc);
  expectPackage(registries.packages(), 1, "b"_zc);

  auto firstKey = localPackage("a"_zc);
  auto secondKey = localPackage("b"_zc);
  auto first = registries.packages().find(firstKey);
  auto second = registries.packages().find(secondKey);
  ZC_EXPECT(first != zc::none);
  ZC_EXPECT(second != zc::none);
  ZC_IF_SOME(handle, first) { ZC_EXPECT(registries.packages().lookup(handle) != zc::none); }
  ZC_IF_SOME(handle, second) { ZC_EXPECT(registries.packages().lookup(handle) != zc::none); }
}

ZC_TEST("Frozen key index owns canonical lookup after registry move") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_EXPECT(registries.packages().snapshotKeys() == zc::none);
  ZC_REQUIRE(registries.collectPackage(localPackage("b"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezePackages() == FrozenRegistryFailure::None);

  auto firstKey = localPackage("a"_zc);
  auto secondKey = localPackage("b"_zc);
  auto first = registries.packages().find(firstKey);
  auto second = registries.packages().find(secondKey);
  auto index = registries.packages().snapshotKeys();
  ZC_REQUIRE(first != zc::none);
  ZC_REQUIRE(second != zc::none);
  ZC_REQUIRE(index != zc::none);

  auto displacedRegistries = zc::mv(registries);
  ZC_IF_SOME(keys, index) {
    ZC_IF_SOME(firstHandle, first) {
      ZC_IF_SOME(key, keys.lookup(firstHandle)) {
        ZC_EXPECT(key.encode().asPtr() == firstKey.encode().asPtr());
      } else {
        ZC_EXPECT(false);
      }
    }
    ZC_IF_SOME(secondHandle, second) {
      ZC_IF_SOME(key, keys.lookup(secondHandle)) {
        ZC_EXPECT(key.encode().asPtr() == secondKey.encode().asPtr());
      } else {
        ZC_EXPECT(false);
      }
    }
    const PackageId invalid;
    ZC_EXPECT(keys.lookup(invalid) == zc::none);

    auto foreignRegistries = requireRegistrySet(factory, requireContext(factory));
    ZC_REQUIRE(foreignRegistries.collectPackage(localPackage("a"_zc)) ==
               FrozenRegistryFailure::None);
    ZC_REQUIRE(foreignRegistries.freezePackages() == FrozenRegistryFailure::None);
    ZC_IF_SOME(foreign, foreignRegistries.packages().find(firstKey)) {
      ZC_EXPECT(keys.lookup(foreign) == zc::none);
    }
  }
  ZC_EXPECT(displacedRegistries.packages().size() == 2);
}

ZC_TEST("Frozen registry rejects duplicate canonical keys without issuing handles") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));

  ZC_EXPECT(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezePackages() == FrozenRegistryFailure::DuplicateCanonicalKey);
  ZC_EXPECT(!registries.packages().isFrozen());
  ZC_EXPECT(registries.packages().terminalFailure() ==
            FrozenRegistryFailure::DuplicateCanonicalKey);
  auto key = localPackage("a"_zc);
  ZC_EXPECT(registries.packages().find(key) == zc::none);
  registries.sortIdentityInvariants();
  ZC_REQUIRE(registries.identityInvariants().size() == 1);
  ZC_EXPECT(registries.identityInvariants()[0].kind() ==
            IdentityInvariantKind::DuplicateCanonicalKey);
  ZC_EXPECT(registries.identityInvariants()[0].phase() == IdentityAllocationPhase::Package);
  ZC_EXPECT(registries.identityInvariants()[0].structuralInputKey() != zc::none);
}

ZC_TEST("Frozen registry invalidates post-freeze mutation") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));

  ZC_EXPECT(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezePackages() == FrozenRegistryFailure::None);
  auto key = localPackage("a"_zc);
  ZC_EXPECT(registries.packages().find(key) != zc::none);
  ZC_EXPECT(registries.collectPackage(localPackage("b"_zc)) ==
            FrozenRegistryFailure::PostFreezeMutation);
  ZC_EXPECT(!registries.packages().isFrozen());
  ZC_EXPECT(registries.packages().terminalFailure() == FrozenRegistryFailure::PostFreezeMutation);
  ZC_EXPECT(registries.packages().find(key) == zc::none);
  ZC_REQUIRE(registries.identityInvariants().size() == 1);
  ZC_EXPECT(registries.identityInvariants()[0].kind() == IdentityInvariantKind::PostFreezeMutation);
}

ZC_TEST("Frozen registry rejects invalid and foreign-context handles") {
  SemanticContextFactory factory;
  auto first = requireRegistrySet(factory, requireContext(factory));
  auto second = requireRegistrySet(factory, requireContext(factory));
  ZC_EXPECT(first.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(second.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(first.freezePackages() == FrozenRegistryFailure::None);
  ZC_EXPECT(second.freezePackages() == FrozenRegistryFailure::None);

  const PackageId invalid;
  ZC_EXPECT(first.packages().validate(invalid) == FrozenRegistryFailure::InvalidHandle);
  auto key = localPackage("a"_zc);
  ZC_IF_SOME(foreign, second.packages().find(key)) {
    ZC_EXPECT(first.packages().validate(foreign) == FrozenRegistryFailure::ForeignContext);
    ZC_EXPECT(first.packages().lookup(foreign) == zc::none);
  }
}

ZC_TEST("Semantic context permits exactly one registry set and enforces freeze order") {
  SemanticContextFactory factory;
  const auto context = requireContext(factory);
  auto registries = requireRegistrySet(factory, context);
  ZC_EXPECT(SemanticIdentityRegistrySet::create(factory, context) == zc::none);
  ZC_EXPECT(registries.freezeCrates() == FrozenRegistryFailure::RegistryNotFrozen);
  ZC_EXPECT(registries.freezeSourceFiles() == FrozenRegistryFailure::RegistryNotFrozen);
  ZC_EXPECT(registries.freezeModules() == FrozenRegistryFailure::RegistryNotFrozen);
  ZC_EXPECT(registries.freezeStableIdentities() == FrozenRegistryFailure::RegistryNotFrozen);
  ZC_EXPECT(registries.freezeGenericParameters() == FrozenRegistryFailure::RegistryNotFrozen);
  ZC_EXPECT(registries.freezeCallableParameters() == FrozenRegistryFailure::RegistryNotFrozen);
  registries.sortIdentityInvariants();
  ZC_REQUIRE(registries.identityInvariants().size() == 6);
  for (const auto& invariant : registries.identityInvariants()) {
    ZC_EXPECT(invariant.kind() == IdentityInvariantKind::AncestorMismatch);
  }
}

ZC_TEST("Crate registry rejects a package key outside the frozen package registry") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_REQUIRE(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezePackages() == FrozenRegistryFailure::None);

  ZC_EXPECT(registries.collectCrate(crate("b"_zc)) == FrozenRegistryFailure::AncestorMismatch);
  ZC_EXPECT(registries.crates().size() == 0);
  expectSingleAncestorMismatch(registries, IdentityAllocationPhase::Crate);

  ZC_EXPECT(registries.collectCrate(crate("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeCrates() == FrozenRegistryFailure::None);
}

ZC_TEST("Source registry rejects a crate key outside the frozen crate registry") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_REQUIRE(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectPackage(localPackage("b"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(crate("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCrates() == FrozenRegistryFailure::None);

  ZC_EXPECT(registries.collectSourceFile(snapshot("b"_zc, 0x42)) ==
            FrozenRegistryFailure::AncestorMismatch);
  ZC_EXPECT(registries.sourceSnapshots().size() == 0);
  expectSingleAncestorMismatch(registries, IdentityAllocationPhase::Source);

  ZC_EXPECT(registries.collectSourceFile(snapshot("a"_zc, 0x41)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeSourceFiles() == FrozenRegistryFailure::None);
}

ZC_TEST("Module registry rejects a crate outside the frozen crate registry") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_REQUIRE(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectPackage(localPackage("b"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(crate("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCrates() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(snapshot("a"_zc, 0x41)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeSourceFiles() == FrozenRegistryFailure::None);

  ZC_EXPECT(registries.collectModule(module("b"_zc)) == FrozenRegistryFailure::AncestorMismatch);
  ZC_EXPECT(registries.modules().size() == 0);
  expectSingleAncestorMismatch(registries, IdentityAllocationPhase::Module);

  ZC_EXPECT(registries.collectModule(module("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeModules() == FrozenRegistryFailure::None);
}

ZC_TEST("Definition registry rejects a module outside the frozen module registry") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_REQUIRE(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectPackage(localPackage("b"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(crate("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(crate("b"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCrates() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(snapshot("a"_zc, 0x41)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(snapshot("b"_zc, 0x42)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeSourceFiles() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(module("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeModules() == FrozenRegistryFailure::None);

  ZC_EXPECT(admitDefinition(registries, "b"_zc) == FrozenRegistryFailure::AncestorMismatch);
  ZC_EXPECT(registries.definitions().size() == 0);
  expectSingleAncestorMismatch(registries, IdentityAllocationPhase::Definition);

  ZC_EXPECT(admitDefinition(registries, "a"_zc) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeStableIdentities() == FrozenRegistryFailure::None);
}

ZC_TEST("Impl registry rejects a module outside the frozen module registry") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_REQUIRE(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectPackage(localPackage("b"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(crate("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(crate("b"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCrates() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(snapshot("a"_zc, 0x41)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(snapshot("b"_zc, 0x42)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeSourceFiles() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(module("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeModules() == FrozenRegistryFailure::None);
  ZC_REQUIRE(admitDefinition(registries, "a"_zc) == FrozenRegistryFailure::None);

  ZC_EXPECT(admitImpl(registries, "b"_zc) == FrozenRegistryFailure::AncestorMismatch);
  ZC_EXPECT(registries.impls().size() == 0);
  expectSingleAncestorMismatch(registries, IdentityAllocationPhase::Impl);

  ZC_EXPECT(admitImpl(registries, "a"_zc) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeStableIdentities() == FrozenRegistryFailure::None);
}

ZC_TEST("Source registry retains immutable contents and bounds source spans") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_EXPECT(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectCrate(crate()) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeCrates() == FrozenRegistryFailure::None);

  auto sourceKey = source();
  auto sourceSnapshot = snapshot("a"_zc, 0x41);
  ZC_EXPECT(registries.collectSourceFile(zc::mv(sourceSnapshot)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeSourceFiles() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.sourceSnapshots().size() == 1);

  auto sourceId = registries.sourceFiles().find(sourceKey);
  ZC_REQUIRE(sourceId != zc::none);
  ZC_IF_SOME(id, sourceId) {
    ZC_EXPECT(registries.sourceSnapshot(id) != zc::none);
    ZC_EXPECT(registries.sourceSpan(id, 0, 2) != zc::none);
    ZC_EXPECT(registries.sourceSpan(id, 0, 3) == zc::none);
    ZC_EXPECT(registries.sourceSpan(id, 2, 1) == zc::none);
  }
}

ZC_TEST("Source registry keeps generated contents outside stable source identity") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_EXPECT(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectCrate(crate()) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeCrates() == FrozenRegistryFailure::None);

  auto snapshotValue =
      ImmutableSourceSnapshot::from(generatedSource(), zc::heapArray<uint8_t>(1, uint8_t{0x41}));
  ZC_IF_SOME(admitted, snapshotValue) {
    ZC_EXPECT(registries.collectSourceFile(zc::mv(admitted)) == FrozenRegistryFailure::None);
  }
  ZC_EXPECT(registries.sourceSnapshots().size() == 1);
  ZC_EXPECT(registries.identityInvariants().size() == 0);
}

ZC_TEST("Module registry assigns one global order across crates") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_EXPECT(registries.collectPackage(localPackage("b"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectCrate(crate("b"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectCrate(crate("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeCrates() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectSourceFile(snapshot("b"_zc, 0x42)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectSourceFile(snapshot("a"_zc, 0x41)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeSourceFiles() == FrozenRegistryFailure::None);

  ZC_EXPECT(registries.collectModule(module("b"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectModule(module("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeModules() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.modules().size() == 2);

  auto firstKey = module("a"_zc);
  auto secondKey = module("b"_zc);
  auto first = registries.modules().find(firstKey);
  auto second = registries.modules().find(secondKey);
  ZC_REQUIRE(first != zc::none);
  ZC_REQUIRE(second != zc::none);
  ZC_IF_SOME(firstId, first) {
    ZC_IF_SOME(secondId, second) { ZC_EXPECT(firstId != secondId); }
  }
}

ZC_TEST("Definition and impl registries freeze after the global module inventory") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_EXPECT(registries.collectPackage(localPackage("b"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectCrate(crate("b"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectCrate(crate("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeCrates() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectSourceFile(snapshot("b"_zc, 0x42)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectSourceFile(snapshot("a"_zc, 0x41)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeSourceFiles() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectModule(module("b"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectModule(module("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeModules() == FrozenRegistryFailure::None);

  ZC_EXPECT(admitDefinition(registries, "b"_zc) == FrozenRegistryFailure::None);
  ZC_EXPECT(admitDefinition(registries, "a"_zc) == FrozenRegistryFailure::None);
  ZC_EXPECT(admitImpl(registries, "b"_zc) == FrozenRegistryFailure::None);
  ZC_EXPECT(admitImpl(registries, "a"_zc) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeStableIdentities() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.definitions().size() == 2);
  ZC_EXPECT(registries.impls().size() == 2);

  auto firstDefinition = definition("a"_zc);
  auto secondDefinition = definition("b"_zc);
  ZC_EXPECT(registries.definitions().find(firstDefinition) != zc::none);
  ZC_EXPECT(registries.definitions().find(secondDefinition) != zc::none);
  auto firstImpl = impl("a"_zc);
  auto secondImpl = impl("b"_zc);
  ZC_EXPECT(registries.impls().find(firstImpl) != zc::none);
  ZC_EXPECT(registries.impls().find(secondImpl) != zc::none);
}

ZC_TEST("Stable authority catalog coalesces equal complete records") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_REQUIRE(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(crate("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCrates() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(snapshot("a"_zc, 0x41)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeSourceFiles() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(module("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeModules() == FrozenRegistryFailure::None);

  ZC_EXPECT(admitDefinition(registries, "a"_zc, "C"_zc) == FrozenRegistryFailure::None);
  ZC_EXPECT(admitDefinition(registries, "a"_zc, "C"_zc) == FrozenRegistryFailure::None);
  ZC_EXPECT(admitImpl(registries, "a"_zc) == FrozenRegistryFailure::None);
  ZC_EXPECT(admitImpl(registries, "a"_zc) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.definitions().size() == 1);
  ZC_EXPECT(registries.impls().size() == 1);
  ZC_EXPECT(registries.freezeStableIdentities() == FrozenRegistryFailure::None);
}

ZC_TEST("Stable authority catalog admits mixed owners outermost first") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_REQUIRE(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(crate("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCrates() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(snapshot("a"_zc, 0x41)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeSourceFiles() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(module("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeModules() == FrozenRegistryFailure::None);

  auto outerRecord = definitionRecord("a"_zc, "Outer"_zc);
  auto outerKey = DefinitionKey::compute(outerRecord);
  zc::Maybe<OverloadHeaderAuthority> noOverload;
  ZC_REQUIRE(registries.collectDefinition(zc::mv(outerRecord), zc::mv(noOverload)) ==
             FrozenRegistryFailure::None);

  zc::Vector<EnclosingStableOwnerKey> implOwners;
  implOwners.add(EnclosingStableOwnerKey::definition(outerKey.clone()));
  auto implementationRecord = implRecord("a"_zc, zc::mv(implOwners));
  auto implementationKey = ImplKey::compute(implementationRecord);
  ZC_REQUIRE(registries.collectImpl(zc::mv(implementationRecord)) == FrozenRegistryFailure::None);

  zc::Vector<EnclosingStableOwnerKey> innerOwners;
  innerOwners.add(EnclosingStableOwnerKey::definition(outerKey.clone()));
  innerOwners.add(EnclosingStableOwnerKey::implementation(implementationKey.clone()));
  auto innerRecord = definitionRecord("a"_zc, "Inner"_zc, zc::mv(innerOwners));
  zc::Maybe<OverloadHeaderAuthority> innerNoOverload;
  ZC_EXPECT(registries.collectDefinition(zc::mv(innerRecord), zc::mv(innerNoOverload)) ==
            FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeStableIdentities() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.definitions().size() == 2);
  ZC_EXPECT(registries.impls().size() == 1);
}

ZC_TEST("Stable authority catalog rejects unknown and skipped owner prefixes") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  ZC_REQUIRE(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(crate("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCrates() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(snapshot("a"_zc, 0x41)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeSourceFiles() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(module("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeModules() == FrozenRegistryFailure::None);

  zc::Vector<EnclosingStableOwnerKey> unknownOwners;
  unknownOwners.add(EnclosingStableOwnerKey::definition(rawDefinitionKey(0x7f)));
  auto unknown = definitionRecord("a"_zc, "Unknown"_zc, zc::mv(unknownOwners));
  zc::Maybe<OverloadHeaderAuthority> unknownNoOverload;
  ZC_EXPECT(registries.collectDefinition(zc::mv(unknown), zc::mv(unknownNoOverload)) ==
            FrozenRegistryFailure::UnknownOwner);

  auto outerRecord = definitionRecord("a"_zc, "Outer"_zc);
  auto outerKey = DefinitionKey::compute(outerRecord);
  zc::Maybe<OverloadHeaderAuthority> outerNoOverload;
  ZC_REQUIRE(registries.collectDefinition(zc::mv(outerRecord), zc::mv(outerNoOverload)) ==
             FrozenRegistryFailure::None);
  zc::Vector<EnclosingStableOwnerKey> implOwners;
  implOwners.add(EnclosingStableOwnerKey::definition(outerKey.clone()));
  auto implementationRecord = implRecord("a"_zc, zc::mv(implOwners));
  auto implementationKey = ImplKey::compute(implementationRecord);
  ZC_REQUIRE(registries.collectImpl(zc::mv(implementationRecord)) == FrozenRegistryFailure::None);

  zc::Vector<EnclosingStableOwnerKey> skippedOwners;
  skippedOwners.add(EnclosingStableOwnerKey::implementation(zc::mv(implementationKey)));
  auto skipped = definitionRecord("a"_zc, "Skipped"_zc, zc::mv(skippedOwners));
  zc::Maybe<OverloadHeaderAuthority> skippedNoOverload;
  ZC_EXPECT(registries.collectDefinition(zc::mv(skipped), zc::mv(skippedNoOverload)) ==
            FrozenRegistryFailure::OwnerPrefixMismatch);
}

ZC_TEST("Every context identity tag rejects a same-slot foreign context handle") {
  SemanticContextFactory factory;
  auto first = populatedRegistrySet(factory);
  auto second = populatedRegistrySet(factory);

  auto packageKey = localPackage("a"_zc);
  auto crateKey = crate("a"_zc);
  auto sourceKey = source("a"_zc);
  auto moduleKey = module("a"_zc);
  auto definitionKey = definition("a"_zc);
  auto implKey = impl("a"_zc);

  ZC_IF_SOME(handle, second.packages().find(packageKey)) {
    ZC_EXPECT(first.packages().validate(handle) == FrozenRegistryFailure::ForeignContext);
  }
  ZC_IF_SOME(handle, second.crates().find(crateKey)) {
    ZC_EXPECT(first.crates().validate(handle) == FrozenRegistryFailure::ForeignContext);
  }
  ZC_IF_SOME(handle, second.sourceFiles().find(sourceKey)) {
    ZC_EXPECT(first.sourceFiles().validate(handle) == FrozenRegistryFailure::ForeignContext);
  }
  ZC_IF_SOME(handle, second.modules().find(moduleKey)) {
    ZC_EXPECT(first.modules().validate(handle) == FrozenRegistryFailure::ForeignContext);
  }
  ZC_IF_SOME(handle, second.definitions().find(definitionKey)) {
    ZC_EXPECT(first.definitions().validate(handle) == FrozenRegistryFailure::ForeignContext);
  }
  ZC_IF_SOME(handle, second.impls().find(implKey)) {
    ZC_EXPECT(first.impls().validate(handle) == FrozenRegistryFailure::ForeignContext);
  }
}

}  // namespace zomlang::compiler::identity
