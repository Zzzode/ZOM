// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/local-identity.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/semantic-identity-registry-set.h"

namespace zomlang::compiler::binder {
namespace {

zc::Array<uint8_t> copyBytes(zc::ArrayPtr<const uint8_t> input) {
  return zc::heapArray<uint8_t>(input);
}

zc::Array<uint8_t> withTrailingByte(zc::ArrayPtr<const uint8_t> input, uint8_t trailing) {
  zc::Vector<uint8_t> result;
  result.addAll(input);
  result.add(trailing);
  return result.releaseAsArray();
}

zc::Array<uint8_t> withoutLastByte(zc::ArrayPtr<const uint8_t> input) {
  ZC_REQUIRE(input.size() != 0);
  return zc::heapArray<uint8_t>(input.slice(0, input.size() - 1));
}

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid local-identity test scalar");
}

identity::DefinitionKey rawDefinitionKey(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto key = identity::DefinitionKey::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(admitted, key) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid raw definition key was rejected");
}

StableBodyOwnerKey definitionOwner(uint8_t byte) {
  return StableBodyOwnerKey::definition(rawDefinitionKey(byte));
}

LocalSyntaxPath path(uint32_t first = 0, uint32_t second = 0x01020304u) {
  zc::Vector<uint32_t> components;
  components.add(first);
  components.add(second);
  auto value = LocalSyntaxPath::from(zc::mv(components));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid local syntax path was rejected");
}

identity::ResolvedVersion version() {
  auto value = identity::ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid local-identity test version");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> features;
  auto value = identity::SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty feature set was rejected");
}

identity::SortedTargetFeatureSet emptyTargetFeatures() {
  zc::Vector<identity::TargetFeatureName> features;
  auto value = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty target feature set was rejected");
}

identity::PackageKey package() {
  zc::Vector<identity::CanonicalPathSegment> path;
  return identity::PackageKey::from(
      identity::CanonicalPackageSource::localPath(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))),
      scalar<identity::PackageName>("local_identity"_zc), version(), emptyFeatures());
}

identity::CanonicalTargetSpecificationKey target() {
  auto value = identity::CanonicalTargetSpecificationKey::from(
      scalar<identity::TargetComponentName>("x"_zc), scalar<identity::TargetComponentName>("v"_zc),
      scalar<identity::TargetComponentName>("o"_zc), scalar<identity::TargetComponentName>("e"_zc),
      scalar<identity::TargetComponentName>("a"_zc), 64, identity::Endianness::Little,
      emptyTargetFeatures());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid local-identity test target was rejected");
}

identity::CompilationConfigKey compilation() {
  zc::Maybe<identity::BuildScriptProducerKey> noBuildScript;
  auto value = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, target(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noBuildScript));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid local-identity test compilation was rejected");
}

identity::CrateKey crate() {
  auto value =
      identity::CrateKey::from(package(), identity::CrateTargetKind::Library,
                               scalar<identity::TargetName>("local_identity"_zc), compilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid local-identity test crate was rejected");
}

identity::SourceFileKey source() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>("local-identity.zom"_zc));
  return identity::SourceFileKey::from(
      crate(), identity::SourceOriginKey::localFile(
                   identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments))));
}

identity::ImmutableSourceSnapshot snapshot() {
  auto value =
      identity::ImmutableSourceSnapshot::from(source(), zc::heapArray<uint8_t>(1, uint8_t{0}));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid local-identity test source snapshot was rejected");
}

identity::ModuleKey module(zc::StringPtr name) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>(name));
  auto value = identity::ModuleKey::from(crate(), zc::mv(path));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid local-identity test module was rejected");
}

identity::SemanticContextBrand requireContext(identity::SemanticContextFactory& factory) {
  auto value = factory.issue();
  ZC_IF_SOME(admitted, value) { return admitted; }
  ZC_FAIL_REQUIRE("semantic context brand space exhausted during local-identity test");
}

zc::Array<identity::ModuleId> issueModules(identity::SemanticContextFactory& factory,
                                           identity::SemanticContextBrand context) {
  auto created = identity::SemanticIdentityRegistrySet::create(factory, context);
  ZC_REQUIRE(created != zc::none);
  ZC_IF_SOME(registries, created) {
    ZC_REQUIRE(registries.collectPackage(package()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezePackages() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(snapshot()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);

    auto first = module("first"_zc);
    auto second = module("second"_zc);
    auto retainedFirst = first.clone();
    auto retainedSecond = second.clone();
    ZC_REQUIRE(registries.collectModule(zc::mv(first)) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(zc::mv(second)) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);

    auto firstId = registries.modules().find(retainedFirst);
    auto secondId = registries.modules().find(retainedSecond);
    ZC_REQUIRE(firstId != zc::none);
    ZC_REQUIRE(secondId != zc::none);
    ZC_IF_SOME(firstValue, firstId) {
      ZC_IF_SOME(secondValue, secondId) {
        zc::Vector<identity::ModuleId> result;
        result.add(firstValue);
        result.add(secondValue);
        return result.releaseAsArray();
      }
    }
  }
  ZC_UNREACHABLE
}

ModuleLocalIdentityAllocator requireAllocator(identity::SemanticContextBrand context,
                                              identity::ModuleId module) {
  auto value = ModuleLocalIdentityAllocator::create(context, module);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid module-local identity allocator was rejected");
}

OwnerLocalBindingId requireOwnerLocalBindingId(ModuleLocalIdentityAllocator& allocator) {
  auto value = allocator.allocateOwnerLocalBinding();
  ZC_IF_SOME(admitted, value) { return admitted; }
  ZC_FAIL_REQUIRE("owner-local binding slot space exhausted during test");
}

ImplOccurrenceId requireImplOccurrenceId(ModuleLocalIdentityAllocator& allocator) {
  auto value = allocator.allocateImplOccurrence();
  ZC_IF_SOME(admitted, value) { return admitted; }
  ZC_FAIL_REQUIRE("implementation occurrence slot space exhausted during test");
}

}  // namespace

ZC_TEST("Local syntax paths reject empty input and use exact structural encoding") {
  zc::Vector<uint32_t> empty;
  ZC_EXPECT(LocalSyntaxPath::from(zc::mv(empty)) == zc::none);
  zc::Vector<uint32_t> oversized;
  for (size_t index = 0; index < 4097; ++index) { oversized.add(0); }
  ZC_EXPECT(LocalSyntaxPath::from(zc::mv(oversized)) == zc::none);

  auto value = path();
  const uint8_t expected[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
                              0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04};
  ZC_EXPECT(value.encode().asPtr() == zc::arrayPtr(expected));
  ZC_EXPECT(value == value.clone());
  ZC_EXPECT(value != path(1, 0x01020304u));
}

ZC_TEST("Stable body owner keys form one exact module-or-definition canonical sum") {
  auto definition = definitionOwner(0x44);
  zc::Vector<uint8_t> expectedDefinition;
  expectedDefinition.add(0x02);
  for (size_t index = 0; index < 32; ++index) { expectedDefinition.add(0x44); }
  ZC_EXPECT(definition.encode().asPtr() == expectedDefinition.asPtr());
  ZC_EXPECT(definition.kind() == StableBodyOwnerKind::Definition);
  ZC_EXPECT(definition.moduleKey() == zc::none);
  ZC_REQUIRE(definition.definitionKey() != zc::none);
  ZC_IF_SOME(key, definition.definitionKey()) { ZC_EXPECT(key == rawDefinitionKey(0x44)); }
  ZC_EXPECT(definition == definition.clone());
  ZC_EXPECT(definition != definitionOwner(0x45));
  auto decodedDefinition = StableBodyOwnerKey::decodeCanonical(expectedDefinition.asPtr());
  ZC_REQUIRE(decodedDefinition != zc::none);
  ZC_IF_SOME(decoded, decodedDefinition) { ZC_EXPECT(decoded == definition); }

  auto moduleKey = module("root"_zc);
  auto encodedModule = moduleKey.encode();
  auto moduleOwner = StableBodyOwnerKey::module(moduleKey.clone());
  auto expectedModule = zc::decodeHex(
      "0103000000000000000000000000000000000000000e6c6f63616c5f6964656e746974790000000000"
      "000005302e302e30000000000000000001000000000000000e6c6f63616c5f6964656e746974790200"
      "000000000000017800000000000000017600000000000000016f000000000000000165000000000000"
      "00016100000040010000000000000000000007ea010000000000000000000001000000000000000472"
      "6f6f74");
  ZC_REQUIRE(expectedModule != zc::none);
  ZC_EXPECT(moduleOwner.encode().asPtr() == expectedModule.asPtr());
  ZC_EXPECT(moduleOwner.kind() == StableBodyOwnerKind::Module);
  ZC_EXPECT(moduleOwner.definitionKey() == zc::none);
  ZC_REQUIRE(moduleOwner.moduleKey() != zc::none);
  ZC_IF_SOME(key, moduleOwner.moduleKey()) {
    ZC_EXPECT(key.encode().asPtr() == encodedModule.asPtr());
  }
  ZC_EXPECT(moduleOwner == moduleOwner.clone());
  ZC_EXPECT(moduleOwner != StableBodyOwnerKey::module(module("other"_zc)));
  ZC_EXPECT(moduleOwner != definition);
  auto decodedModule = StableBodyOwnerKey::decodeCanonical(expectedModule.asPtr());
  ZC_REQUIRE(decodedModule != zc::none);
  ZC_IF_SOME(decoded, decodedModule) { ZC_EXPECT(decoded == moduleOwner); }

  auto invalidTag = copyBytes(expectedDefinition.asPtr());
  invalidTag[0] = 0xff;
  ZC_EXPECT(StableBodyOwnerKey::decodeCanonical(invalidTag.asPtr()) == zc::none);
  auto truncated = withoutLastByte(expectedDefinition.asPtr());
  ZC_EXPECT(StableBodyOwnerKey::decodeCanonical(truncated.asPtr()) == zc::none);
  auto trailing = withTrailingByte(expectedDefinition.asPtr(), 0x00);
  ZC_EXPECT(StableBodyOwnerKey::decodeCanonical(trailing.asPtr()) == zc::none);
}

ZC_TEST("Owner-local binding keys retain the exact body-value record") {
  auto value = OwnerLocalBindingKey::from(
      definitionOwner(0x11), path(), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, scalar<identity::DeclaredDefinitionName>("value"_zc));
  ZC_REQUIRE(value != zc::none);
  ZC_IF_SOME(admitted, value) {
    zc::Vector<uint8_t> expected;
    const uint8_t prefix[] = {0x7a, 0x6f, 0x6d, 0x2e, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x2d,
                              0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x2d, 0x62, 0x69, 0x6e, 0x64,
                              0x69, 0x6e, 0x67, 0x2e, 0x76, 0x31, 0x00, 0x02};
    expected.addAll(zc::arrayPtr(prefix));
    for (size_t index = 0; index < 32; ++index) { expected.add(0x11); }
    const uint8_t suffix[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
                              0x00, 0x01, 0x02, 0x03, 0x04, 0x01, 0x13, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x05, 0x76, 0x61, 0x6c, 0x75, 0x65};
    expected.addAll(zc::arrayPtr(suffix));

    ZC_EXPECT(admitted.encode().asPtr() == expected.asPtr());
    ZC_EXPECT(admitted == admitted.clone());
    ZC_EXPECT(admitted.owner().kind() == StableBodyOwnerKind::Definition);
    ZC_REQUIRE(admitted.owner().definitionKey() != zc::none);
    ZC_IF_SOME(owner, admitted.owner().definitionKey()) {
      ZC_EXPECT(owner == rawDefinitionKey(0x11));
    }
    ZC_EXPECT(admitted.path() == path());
    ZC_EXPECT(admitted.nameSpace() == OwnerLocalBindingNamespace::Value);
    ZC_EXPECT(admitted.kind() == OwnerLocalBindingKind::Local);
    ZC_EXPECT(admitted.name().text() == "value"_zc);

    auto decoded = OwnerLocalBindingKey::decodeCanonical(expected.asPtr());
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(decodedValue, decoded) { ZC_EXPECT(decodedValue == admitted); }
  }

  auto invalidKind =
      OwnerLocalBindingKey::from(definitionOwner(0x11), path(), OwnerLocalBindingNamespace::Value,
                                 static_cast<OwnerLocalBindingKind>(0xff),
                                 scalar<identity::DeclaredDefinitionName>("value"_zc));
  ZC_EXPECT(invalidKind == zc::none);
  auto invalidNamespace = OwnerLocalBindingKey::from(
      definitionOwner(0x11), path(), static_cast<OwnerLocalBindingNamespace>(0xff),
      OwnerLocalBindingKind::PatternBinding, scalar<identity::DeclaredDefinitionName>("value"_zc));
  ZC_EXPECT(invalidNamespace == zc::none);

  auto moduleKey = module("root"_zc);
  auto moduleBinding = OwnerLocalBindingKey::from(
      StableBodyOwnerKey::module(moduleKey.clone()), path(), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, scalar<identity::DeclaredDefinitionName>("value"_zc));
  ZC_REQUIRE(moduleBinding != zc::none);
  ZC_IF_SOME(admitted, moduleBinding) {
    auto expected = zc::decodeHex(
        "7a6f6d2e6f776e65722d6c6f63616c2d62696e64696e672e763100010300000000000000000000000000"
        "0000000000000e6c6f63616c5f6964656e746974790000000000000005302e302e300000000000000000"
        "01000000000000000e6c6f63616c5f6964656e7469747902000000000000000178000000000000000176"
        "00000000000000016f000000000000000165000000000000000161000000400100000000000000000000"
        "07ea0100000000000000000000010000000000000004726f6f7400000000000000020000000001020304"
        "0113000000000000000576616c7565");
    ZC_REQUIRE(expected != zc::none);
    ZC_EXPECT(admitted.encode().asPtr() == expected.asPtr());
    ZC_EXPECT(admitted.owner().kind() == StableBodyOwnerKind::Module);
    ZC_REQUIRE(admitted.owner().moduleKey() != zc::none);
    ZC_IF_SOME(owner, admitted.owner().moduleKey()) {
      ZC_EXPECT(owner.encode().asPtr() == moduleKey.encode().asPtr());
    }
    auto decoded = OwnerLocalBindingKey::decodeCanonical(expected.asPtr());
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(decodedValue, decoded) { ZC_EXPECT(decodedValue == admitted); }
  }
}

ZC_TEST("Owner-local binding kinds admit only their semantic namespace") {
  ZC_EXPECT(static_cast<uint8_t>(OwnerLocalBindingKind::CallableParameter) == 0x0f);
  ZC_EXPECT(static_cast<uint8_t>(OwnerLocalBindingKind::GenericParameter) == 0x10);
  ZC_EXPECT(static_cast<uint8_t>(OwnerLocalBindingKind::Local) == 0x13);
  ZC_EXPECT(static_cast<uint8_t>(OwnerLocalBindingKind::PatternBinding) == 0x14);

  const OwnerLocalBindingNamespace namespaces[] = {
      OwnerLocalBindingNamespace::Value, OwnerLocalBindingNamespace::Type,
      OwnerLocalBindingNamespace::Module, OwnerLocalBindingNamespace::Label,
      OwnerLocalBindingNamespace::Attribute};
  const OwnerLocalBindingKind kinds[] = {
      OwnerLocalBindingKind::CallableParameter, OwnerLocalBindingKind::GenericParameter,
      OwnerLocalBindingKind::Local, OwnerLocalBindingKind::PatternBinding};

  for (const auto nameSpace : namespaces) {
    for (const auto kind : kinds) {
      const bool expected = (nameSpace == OwnerLocalBindingNamespace::Value &&
                             (kind == OwnerLocalBindingKind::CallableParameter ||
                              kind == OwnerLocalBindingKind::Local ||
                              kind == OwnerLocalBindingKind::PatternBinding)) ||
                            (nameSpace == OwnerLocalBindingNamespace::Type &&
                             kind == OwnerLocalBindingKind::GenericParameter);
      auto value =
          OwnerLocalBindingKey::from(definitionOwner(0x11), path(), nameSpace, kind,
                                     scalar<identity::DeclaredDefinitionName>("parameter"_zc));
      ZC_EXPECT((value != zc::none) == expected);
    }
  }
}

ZC_TEST("Anonymous owner-local keys retain role without admitting stable identity") {
  auto value = AnonymousOwnerLocalKey::from(definitionOwner(0x22), path(),
                                            AnonymousOwnerLocalRole::FunctionExpression);
  ZC_REQUIRE(value != zc::none);
  ZC_IF_SOME(admitted, value) {
    zc::Vector<uint8_t> expected;
    const uint8_t prefix[] = {0x7a, 0x6f, 0x6d, 0x2e, 0x61, 0x6e, 0x6f, 0x6e, 0x79, 0x6d,
                              0x6f, 0x75, 0x73, 0x2d, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x2d,
                              0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x2e, 0x76, 0x31, 0x00, 0x02};
    expected.addAll(zc::arrayPtr(prefix));
    for (size_t index = 0; index < 32; ++index) { expected.add(0x22); }
    const uint8_t suffix[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
                              0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x02};
    expected.addAll(zc::arrayPtr(suffix));

    ZC_EXPECT(admitted.encode().asPtr() == expected.asPtr());
    ZC_EXPECT(admitted == admitted.clone());
    ZC_EXPECT(admitted.owner().kind() == StableBodyOwnerKind::Definition);
    ZC_EXPECT(admitted.role() == AnonymousOwnerLocalRole::FunctionExpression);

    auto decoded = AnonymousOwnerLocalKey::decodeCanonical(expected.asPtr());
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(decodedValue, decoded) { ZC_EXPECT(decodedValue == admitted); }
  }

  auto invalid = AnonymousOwnerLocalKey::from(definitionOwner(0x22), path(),
                                              static_cast<AnonymousOwnerLocalRole>(0xff));
  ZC_EXPECT(invalid == zc::none);

  auto moduleValue = AnonymousOwnerLocalKey::from(StableBodyOwnerKey::module(module("root"_zc)),
                                                  path(), AnonymousOwnerLocalRole::Closure);
  ZC_REQUIRE(moduleValue != zc::none);
  ZC_IF_SOME(admitted, moduleValue) {
    auto expected = zc::decodeHex(
        "7a6f6d2e616e6f6e796d6f75732d6f776e65722d6c6f63616c2e7631000103000000000000000000000000"
        "000000000000000e6c6f63616c5f6964656e746974790000000000000005302e302e300000000000000000"
        "01000000000000000e6c6f63616c5f6964656e746974790200000000000000017800000000000000017600"
        "000000000000016f00000000000000016500000000000000016100000040010000000000000000000007ea"
        "0100000000000000000000010000000000000004726f6f740000000000000002000000000102030401");
    ZC_REQUIRE(expected != zc::none);
    ZC_EXPECT(admitted.encode().asPtr() == expected.asPtr());
    auto decoded = AnonymousOwnerLocalKey::decodeCanonical(expected.asPtr());
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(decodedValue, decoded) { ZC_EXPECT(decodedValue == admitted); }
  }
}

ZC_TEST("Owner-local binding decoder rejects every non-canonical record boundary") {
  auto value = OwnerLocalBindingKey::from(
      definitionOwner(0x11), path(), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, scalar<identity::DeclaredDefinitionName>("value"_zc));
  ZC_REQUIRE(value != zc::none);
  ZC_IF_SOME(admitted, value) {
    auto canonical = admitted.encode();

    auto wrongDomain = copyBytes(canonical.asPtr());
    wrongDomain[0] = static_cast<uint8_t>('x');
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(wrongDomain.asPtr()) == zc::none);

    auto wrongVersion = copyBytes(canonical.asPtr());
    wrongVersion[25] = static_cast<uint8_t>('2');
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(wrongVersion.asPtr()) == zc::none);

    auto missingDomainTerminator = copyBytes(canonical.asPtr());
    missingDomainTerminator[26] = 0x01;
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(missingDomainTerminator.asPtr()) == zc::none);

    auto unknownOwnerTag = copyBytes(canonical.asPtr());
    unknownOwnerTag[27] = 0xff;
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(unknownOwnerTag.asPtr()) == zc::none);

    auto emptyPath = copyBytes(canonical.asPtr());
    emptyPath[67] = 0x00;
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(emptyPath.asPtr()) == zc::none);

    auto oversizedPath = copyBytes(canonical.asPtr());
    oversizedPath[66] = 0x10;
    oversizedPath[67] = 0x01;
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(oversizedPath.asPtr()) == zc::none);

    auto truncatedPathComponent = zc::heapArray<uint8_t>(canonical.slice(0, 71));
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(truncatedPathComponent.asPtr()) == zc::none);

    auto unknownNamespace = copyBytes(canonical.asPtr());
    unknownNamespace[76] = 0xff;
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(unknownNamespace.asPtr()) == zc::none);

    auto unknownKind = copyBytes(canonical.asPtr());
    unknownKind[77] = 0xff;
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(unknownKind.asPtr()) == zc::none);

    auto invalidMatrix = copyBytes(canonical.asPtr());
    invalidMatrix[76] = static_cast<uint8_t>(OwnerLocalBindingNamespace::Type);
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(invalidMatrix.asPtr()) == zc::none);

    zc::Vector<uint8_t> nonCanonicalName;
    nonCanonicalName.addAll(canonical.slice(0, 78));
    const uint8_t decomposedName[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x03, 0x65, 0xcc, 0x81};
    nonCanonicalName.addAll(zc::arrayPtr(decomposedName));
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(nonCanonicalName.asPtr()) == zc::none);

    auto truncated = withoutLastByte(canonical.asPtr());
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(truncated.asPtr()) == zc::none);
    auto trailing = withTrailingByte(canonical.asPtr(), 0x00);
    ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(trailing.asPtr()) == zc::none);
  }

  auto oversized = zc::heapArray<uint8_t>(64 * 1024 + 1, uint8_t{0});
  ZC_EXPECT(OwnerLocalBindingKey::decodeCanonical(oversized.asPtr()) == zc::none);
}

ZC_TEST("Anonymous owner-local decoder rejects invalid domains roles and framing") {
  auto value = AnonymousOwnerLocalKey::from(definitionOwner(0x22), path(),
                                            AnonymousOwnerLocalRole::FunctionExpression);
  ZC_REQUIRE(value != zc::none);
  ZC_IF_SOME(admitted, value) {
    auto canonical = admitted.encode();

    auto wrongDomain = copyBytes(canonical.asPtr());
    wrongDomain[0] = static_cast<uint8_t>('x');
    ZC_EXPECT(AnonymousOwnerLocalKey::decodeCanonical(wrongDomain.asPtr()) == zc::none);

    auto wrongVersion = copyBytes(canonical.asPtr());
    wrongVersion[27] = static_cast<uint8_t>('2');
    ZC_EXPECT(AnonymousOwnerLocalKey::decodeCanonical(wrongVersion.asPtr()) == zc::none);

    auto missingDomainTerminator = copyBytes(canonical.asPtr());
    missingDomainTerminator[28] = 0x01;
    ZC_EXPECT(AnonymousOwnerLocalKey::decodeCanonical(missingDomainTerminator.asPtr()) == zc::none);

    auto invalidRole = copyBytes(canonical.asPtr());
    invalidRole[78] = 0xff;
    ZC_EXPECT(AnonymousOwnerLocalKey::decodeCanonical(invalidRole.asPtr()) == zc::none);

    auto truncated = withoutLastByte(canonical.asPtr());
    ZC_EXPECT(AnonymousOwnerLocalKey::decodeCanonical(truncated.asPtr()) == zc::none);
    auto trailing = withTrailingByte(canonical.asPtr(), 0x00);
    ZC_EXPECT(AnonymousOwnerLocalKey::decodeCanonical(trailing.asPtr()) == zc::none);
  }
}

ZC_TEST("Module-local handles enforce context module and dense position") {
  identity::SemanticContextFactory factory;
  const auto firstContext = requireContext(factory);
  const auto secondContext = requireContext(factory);
  const auto firstModules = issueModules(factory, firstContext);
  const auto secondModules = issueModules(factory, secondContext);

  auto firstAllocator = requireAllocator(firstContext, firstModules[0]);
  auto siblingAllocator = requireAllocator(firstContext, firstModules[1]);
  auto foreignAllocator = requireAllocator(secondContext, secondModules[0]);

  const auto local0 = requireOwnerLocalBindingId(firstAllocator);
  const auto local1 = requireOwnerLocalBindingId(firstAllocator);
  const auto siblingLocal = requireOwnerLocalBindingId(siblingAllocator);
  const auto foreignLocal = requireOwnerLocalBindingId(foreignAllocator);
  ZC_EXPECT(firstAllocator.ownerLocalBindingCount() == 2);
  ZC_EXPECT(firstAllocator.validate(local0, 0) == ModuleLocalIdentityFailure::None);
  ZC_EXPECT(firstAllocator.validate(local1, 1) == ModuleLocalIdentityFailure::None);
  ZC_EXPECT(firstAllocator.validate(local0, 1) == ModuleLocalIdentityFailure::NonDenseSlot);
  ZC_EXPECT(firstAllocator.validate(siblingLocal, 0) == ModuleLocalIdentityFailure::ForeignModule);
  ZC_EXPECT(firstAllocator.validate(foreignLocal, 0) == ModuleLocalIdentityFailure::ForeignContext);
  ZC_EXPECT(firstAllocator.validate(OwnerLocalBindingId(), 0) ==
            ModuleLocalIdentityFailure::InvalidHandle);
  ZC_EXPECT(local0 != local1);
  ZC_EXPECT(local0.belongsTo(firstContext));
  ZC_EXPECT(local0.belongsTo(firstModules[0]));
  ZC_EXPECT(!local0.belongsTo(firstModules[1]));

  const auto occurrence0 = requireImplOccurrenceId(firstAllocator);
  const auto occurrence1 = requireImplOccurrenceId(firstAllocator);
  const auto siblingOccurrence = requireImplOccurrenceId(siblingAllocator);
  const auto foreignOccurrence = requireImplOccurrenceId(foreignAllocator);
  ZC_EXPECT(firstAllocator.implOccurrenceCount() == 2);
  ZC_EXPECT(firstAllocator.validate(occurrence0, 0) == ModuleLocalIdentityFailure::None);
  ZC_EXPECT(firstAllocator.validate(occurrence1, 1) == ModuleLocalIdentityFailure::None);
  ZC_EXPECT(firstAllocator.validate(occurrence1, 0) == ModuleLocalIdentityFailure::NonDenseSlot);
  ZC_EXPECT(firstAllocator.validate(siblingOccurrence, 0) ==
            ModuleLocalIdentityFailure::ForeignModule);
  ZC_EXPECT(firstAllocator.validate(foreignOccurrence, 0) ==
            ModuleLocalIdentityFailure::ForeignContext);
  ZC_EXPECT(firstAllocator.validate(ImplOccurrenceId(), 0) ==
            ModuleLocalIdentityFailure::InvalidHandle);
  ZC_EXPECT(occurrence0 != occurrence1);
  ZC_EXPECT(occurrence0.belongsTo(firstContext));
  ZC_EXPECT(occurrence0.belongsTo(firstModules[0]));
  ZC_EXPECT(!occurrence0.belongsTo(firstModules[1]));

  ZC_EXPECT(ModuleLocalIdentityAllocator::create(firstContext, secondModules[0]) == zc::none);
  ZC_EXPECT(ModuleLocalIdentityAllocator::create(identity::SemanticContextBrand(),
                                                 firstModules[0]) == zc::none);
}

}  // namespace zomlang::compiler::binder
