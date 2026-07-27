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

#include "zomlang/compiler/identity/definition-key.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

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

Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto digest = Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(value, digest) { return value; }
  ZC_FAIL_REQUIRE("invalid digest test input");
}

PackageKey package() {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return PackageKey::from(CanonicalPackageSource::localPath(zc::mv(path)),
                          requireScalar<PackageName>("a"_zc), requireVersion(),
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

CompilationConfigKey compilation() {
  zc::Maybe<BuildScriptProducerKey> output = BuildScriptProducerKey::from(repeatedDigest(0x11));
  auto value = CompilationConfigKey::from(
      CompilationDomain::Target, targetSpec(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(output));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid compilation configuration test input");
}

CrateKey crate() {
  auto value =
      CrateKey::from(CompilationUnitIdentity::userPackage(package()), CrateTargetKind::Library,
                     requireScalar<TargetName>("lib"_zc), compilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

ModuleKey module() {
  zc::Vector<ModulePathSegment> path;
  path.add(requireScalar<ModulePathSegment>("m"_zc));
  auto value = ModuleKey::from(crate(), zc::mv(path));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module test input");
}

DeclaredDefinitionName declaredName(zc::StringPtr text) {
  return requireScalar<DeclaredDefinitionName>(text);
}

DefinitionKey rawDefinitionKey(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto key = DefinitionKey::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(admitted, key) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid raw definition key was rejected");
}

ImplKey rawImplKey(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto key = ImplKey::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(admitted, key) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid raw implementation key was rejected");
}

SemanticIdentifier identifier(zc::StringPtr text) {
  return requireScalar<SemanticIdentifier>(text);
}

CanonicalNameReference name(CanonicalNameRoot&& root, zc::StringPtr text) {
  zc::Vector<SemanticIdentifier> suffix;
  suffix.add(identifier(text));
  auto value = CanonicalNameReference::from(zc::mv(root), zc::mv(suffix));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid canonical name was rejected");
}

CanonicalHeaderTypeSyntax predefined(PredefinedTypeKind kind) {
  auto value = CanonicalHeaderTypeSyntax::predefined(kind);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid predefined type was rejected");
}

CanonicalTraitReference trait(zc::StringPtr text) {
  zc::Vector<CanonicalHeaderTypeSyntax> arguments;
  auto value =
      CanonicalTraitReference::from(name(CanonicalNameRoot::relative(), text), zc::mv(arguments));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid canonical trait was rejected");
}

CanonicalImplHeader implHeader(zc::StringPtr traitName = "Trait"_zc) {
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  auto value = CanonicalImplHeader::from(zc::mv(generics), ImplPolarity::Positive, ImplSafety::Safe,
                                         trait(traitName), predefined(PredefinedTypeKind::I32),
                                         zc::mv(obligations));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid canonical implementation header was rejected");
}

CanonicalOverloadHeader overloadHeader(CallableHeaderKind kind, zc::StringPtr text) {
  zc::Maybe<ReceiverShape> receiver;
  if (kind == CallableHeaderKind::Method) { receiver = ReceiverShape::Shared; }
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  zc::Vector<CanonicalCallableParameter> parameters;
  auto result = kind == CallableHeaderKind::Constructor ? CanonicalCallableResult::constructorSelf()
                                                        : CanonicalCallableResult::unit();
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises;
  zc::Maybe<ExternalAbi> externalAbi;
  auto value = CanonicalOverloadHeader::from(
      kind, declaredName(text), zc::mv(receiver), zc::mv(generics), zc::mv(obligations),
      zc::mv(parameters), zc::mv(result), zc::mv(raises), zc::mv(externalAbi));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid canonical overload header was rejected");
}

DefinitionIdentityRecord definitionRecord(
    DefinitionKind kind, zc::StringPtr text,
    zc::Maybe<OverloadHeaderDigest>&& overloadHeader = zc::none,
    zc::Vector<EnclosingStableOwnerKey>&& owners = {}) {
  auto nameSpace = definitionNamespaceFor(kind);
  ZC_IF_SOME(admittedNamespace, nameSpace) {
    auto value = DefinitionIdentityRecord::from(module(), zc::mv(owners), kind, admittedNamespace,
                                                declaredName(text), zc::mv(overloadHeader));
    ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  }
  ZC_FAIL_REQUIRE("valid definition identity record was rejected");
}

DefinitionIdentityRecord callableRecord(DefinitionKind kind, zc::StringPtr text,
                                        const OverloadHeaderDigest& digest) {
  zc::Maybe<OverloadHeaderDigest> overloadHeader = digest.clone();
  return definitionRecord(kind, text, zc::mv(overloadHeader));
}

DefinitionIdentityAuthority definitionAuthority(DefinitionKind kind, CallableHeaderKind headerKind,
                                                zc::StringPtr text) {
  auto overload = OverloadHeaderAuthority::from(overloadHeader(headerKind, text));
  auto record = callableRecord(kind, text, overload.digest());
  zc::Maybe<OverloadHeaderAuthority> retained = zc::mv(overload);
  auto value = DefinitionIdentityAuthority::from(zc::mv(record), zc::mv(retained));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid definition identity authority was rejected");
}

template <typename Key>
void expectRawDigestLength() {
  uint8_t bytes[33] = {};
  for (size_t index = 0; index < 32; ++index) { bytes[index] = static_cast<uint8_t>(index); }
  bytes[32] = 0xff;
  ZC_EXPECT(Key::fromBytes(zc::arrayPtr(bytes, 31)) == zc::none);
  ZC_EXPECT(Key::fromBytes(zc::arrayPtr(bytes, 33)) == zc::none);
  auto admitted = Key::fromBytes(zc::arrayPtr(bytes, 32));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) {
    ZC_EXPECT(value.bytes().size() == 32);
    ZC_EXPECT(value.encode().asPtr() == value.bytes());
    ZC_EXPECT(value.clone() == value);
  }
}

}  // namespace

ZC_TEST("Definition kinds retain the closed stable namespace partition") {
  struct StableKind final {
    DefinitionKind kind;
    DefinitionNamespace nameSpace;
  };
  StableKind stable[] = {
      {DefinitionKind::ModuleAlias, DefinitionNamespace::Module},
      {DefinitionKind::Function, DefinitionNamespace::Value},
      {DefinitionKind::Method, DefinitionNamespace::Value},
      {DefinitionKind::Constructor, DefinitionNamespace::Value},
      {DefinitionKind::Destructor, DefinitionNamespace::Value},
      {DefinitionKind::Class, DefinitionNamespace::Type},
      {DefinitionKind::Struct, DefinitionNamespace::Type},
      {DefinitionKind::Interface, DefinitionNamespace::Type},
      {DefinitionKind::Enum, DefinitionNamespace::Type},
      {DefinitionKind::Error, DefinitionNamespace::Type},
      {DefinitionKind::TypeAlias, DefinitionNamespace::Type},
      {DefinitionKind::AssociatedType, DefinitionNamespace::Type},
      {DefinitionKind::Field, DefinitionNamespace::Value},
      {DefinitionKind::EnumVariant, DefinitionNamespace::Value},
      {DefinitionKind::Constant, DefinitionNamespace::Value},
      {DefinitionKind::Static, DefinitionNamespace::Value},
  };
  for (const auto& entry : stable) {
    ZC_EXPECT(isDefinitionKindValue(entry.kind));
    ZC_EXPECT(isStableDefinitionKind(entry.kind));
    ZC_EXPECT(definitionNamespaceFor(entry.kind) == entry.nameSpace);
  }

  DefinitionKind subordinateOrLocal[] = {
      DefinitionKind::Parameter,      DefinitionKind::TypeParameter, DefinitionKind::Local,
      DefinitionKind::PatternBinding, DefinitionKind::Closure,
  };
  for (auto kind : subordinateOrLocal) {
    ZC_EXPECT(isDefinitionKindValue(kind));
    ZC_EXPECT(!isStableDefinitionKind(kind));
    ZC_EXPECT(definitionNamespaceFor(kind) == zc::none);
  }
  ZC_EXPECT(!isDefinitionKindValue(static_cast<DefinitionKind>(0x00)));
  ZC_EXPECT(!isDefinitionKindValue(static_cast<DefinitionKind>(0xff)));
  ZC_EXPECT(!isStableDefinitionKind(static_cast<DefinitionKind>(0xff)));
}

ZC_TEST("DefinitionIdentityRecord passes the complete owner and named-item vectors") {
  zc::Vector<EnclosingStableOwnerKey> owners;
  owners.add(EnclosingStableOwnerKey::definition(rawDefinitionKey(0x11)));
  owners.add(EnclosingStableOwnerKey::implementation(rawImplKey(0x22)));
  auto record = definitionRecord(DefinitionKind::Class, "C"_zc, zc::none, zc::mv(owners));
  auto encoded = record.encode();
  ZC_REQUIRE(module().encode().size() == 172);
  ZC_EXPECT(encoded.size() == 258);
  ZC_EXPECT(zc::encodeHex(encoded.asPtr().slice(172)) ==
            "0000000000000002011111111111111111111111111111111111111111111111111111111111111111"
            "0222222222222222222222222222222222222222222222222222222222222222220602"
            "00000000000000014300"_zc);
  auto key = DefinitionKey::compute(record);
  ZC_EXPECT(zc::encodeHex(key.bytes()) ==
            "399b67c684a90539cf5e5d2779db2208f036209d9f9f9b98c32f68a9d1b7a1ff"_zc);
  auto decoded = DefinitionIdentityRecord::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).encode().asPtr() == encoded.asPtr());
  ZC_EXPECT(DefinitionKey::compute(ZC_REQUIRE_NONNULL(decoded)) == key);
}

ZC_TEST("DefinitionIdentityRecord decoder is exact bounded and closed") {
  zc::Vector<EnclosingStableOwnerKey> owners;
  owners.add(EnclosingStableOwnerKey::definition(rawDefinitionKey(0x11)));
  owners.add(EnclosingStableOwnerKey::implementation(rawImplKey(0x22)));
  auto record = definitionRecord(DefinitionKind::Class, "C"_zc, zc::none, zc::mv(owners));
  auto encoded = record.encode();

  for (size_t size = 0; size < encoded.size(); ++size) {
    ZC_EXPECT(DefinitionIdentityRecord::decodeCanonical(encoded.asPtr().first(size)) == zc::none);
  }

  auto trailing = zc::heapArray<uint8_t>(encoded.size() + 1);
  trailing.first(encoded.size()).copyFrom(encoded.asPtr());
  trailing.back() = 0;
  ZC_EXPECT(DefinitionIdentityRecord::decodeCanonical(trailing.asPtr()) == zc::none);

  const size_t ownerCountOffset = module().encode().size();
  auto oversizedOwnerCount = zc::heapArray<uint8_t>(encoded.asPtr());
  for (size_t index = 0; index < 8; ++index) {
    oversizedOwnerCount[ownerCountOffset + index] = 0xff;
  }
  ZC_EXPECT(DefinitionIdentityRecord::decodeCanonical(oversizedOwnerCount.asPtr()) == zc::none);

  auto unknownOwner = zc::heapArray<uint8_t>(encoded.asPtr());
  unknownOwner[ownerCountOffset + 8] = 0xff;
  ZC_EXPECT(DefinitionIdentityRecord::decodeCanonical(unknownOwner.asPtr()) == zc::none);

  const size_t kindOffset = ownerCountOffset + 8 + 2 * 33;
  auto unknownKind = zc::heapArray<uint8_t>(encoded.asPtr());
  unknownKind[kindOffset] = 0xff;
  ZC_EXPECT(DefinitionIdentityRecord::decodeCanonical(unknownKind.asPtr()) == zc::none);

  auto unknownNamespace = zc::heapArray<uint8_t>(encoded.asPtr());
  unknownNamespace[kindOffset + 1] = 0xff;
  ZC_EXPECT(DefinitionIdentityRecord::decodeCanonical(unknownNamespace.asPtr()) == zc::none);

  auto unknownPresence = zc::heapArray<uint8_t>(encoded.asPtr());
  unknownPresence.back() = 0xff;
  ZC_EXPECT(DefinitionIdentityRecord::decodeCanonical(unknownPresence.asPtr()) == zc::none);

  auto oversized = zc::heapArray<uint8_t>(4 * 1024 * 1024 + 1);
  ZC_EXPECT(DefinitionIdentityRecord::decodeCanonical(oversized.asPtr()) == zc::none);
}

ZC_TEST("ImplIdentityRecord passes the complete owner and implementation-header vectors") {
  zc::Vector<EnclosingStableOwnerKey> owners;
  owners.add(EnclosingStableOwnerKey::definition(rawDefinitionKey(0x33)));
  auto record = ImplIdentityRecord::from(module(), zc::mv(owners), implHeader());
  auto encoded = record.encode();
  ZC_REQUIRE(module().encode().size() == 172);
  ZC_EXPECT(encoded.size() == 263);
  ZC_EXPECT(zc::encodeHex(encoded.asPtr().slice(172)) ==
            "0000000000000001013333333333333333333333333333333333333333333333333333333333333333"
            "0000000000000000010102000000000000000100000000000000055472616974"
            "000000000000000002030000000000000000"_zc);
  auto key = ImplKey::compute(record);
  ZC_EXPECT(zc::encodeHex(key.bytes()) ==
            "250c2ac7bdbf2982970d2ad2bbcb7df02bf9eb30d9a55bb5411fdb5fcb9bf69b"_zc);
}

ZC_TEST("Stable owner keys encode one closed tag followed by one raw digest") {
  auto definition = EnclosingStableOwnerKey::definition(rawDefinitionKey(0x44));
  auto implementation = EnclosingStableOwnerKey::implementation(rawImplKey(0x55));
  ZC_EXPECT(definition.encode().size() == 33);
  ZC_EXPECT(implementation.encode().size() == 33);
  ZC_EXPECT(zc::encodeHex(definition.encode().asPtr()) ==
            "014444444444444444444444444444444444444444444444444444444444444444"_zc);
  ZC_EXPECT(zc::encodeHex(implementation.encode().asPtr()) ==
            "025555555555555555555555555555555555555555555555555555555555555555"_zc);
  ZC_EXPECT(definition.kind() == EnclosingStableOwnerKind::Definition);
  ZC_EXPECT(implementation.kind() == EnclosingStableOwnerKind::Implementation);
  ZC_EXPECT(definition.definitionKey() != zc::none);
  ZC_EXPECT(definition.implKey() == zc::none);
  ZC_EXPECT(implementation.definitionKey() == zc::none);
  ZC_EXPECT(implementation.implKey() != zc::none);
}

ZC_TEST("DefinitionIdentityRecord rejects unstable kinds namespace drift and overload drift") {
  zc::Vector<EnclosingStableOwnerKey> noOwners;
  zc::Maybe<OverloadHeaderDigest> noOverload;
  ZC_EXPECT(DefinitionIdentityRecord::from(module(), zc::mv(noOwners), DefinitionKind::Class,
                                           DefinitionNamespace::Value, declaredName("C"_zc),
                                           zc::mv(noOverload)) == zc::none);

  DefinitionKind unstable[] = {
      DefinitionKind::Parameter,      DefinitionKind::TypeParameter, DefinitionKind::Local,
      DefinitionKind::PatternBinding, DefinitionKind::Closure,
  };
  for (auto kind : unstable) {
    zc::Vector<EnclosingStableOwnerKey> owners;
    zc::Maybe<OverloadHeaderDigest> overload;
    ZC_EXPECT(DefinitionIdentityRecord::from(module(), zc::mv(owners), kind,
                                             DefinitionNamespace::Value, declaredName("x"_zc),
                                             zc::mv(overload)) == zc::none);
  }

  auto overload =
      OverloadHeaderAuthority::from(overloadHeader(CallableHeaderKind::Function, "f"_zc));
  zc::Vector<EnclosingStableOwnerKey> callableOwners;
  zc::Maybe<OverloadHeaderDigest> missingOverload;
  ZC_EXPECT(DefinitionIdentityRecord::from(module(), zc::mv(callableOwners),
                                           DefinitionKind::Function, DefinitionNamespace::Value,
                                           declaredName("f"_zc),
                                           zc::mv(missingOverload)) == zc::none);
  zc::Vector<EnclosingStableOwnerKey> classOwners;
  zc::Maybe<OverloadHeaderDigest> unexpectedOverload = overload.digest().clone();
  ZC_EXPECT(DefinitionIdentityRecord::from(module(), zc::mv(classOwners), DefinitionKind::Class,
                                           DefinitionNamespace::Type, declaredName("C"_zc),
                                           zc::mv(unexpectedOverload)) == zc::none);
}

ZC_TEST("Definition identity authority retains the complete overload equality authority") {
  auto authority =
      definitionAuthority(DefinitionKind::Function, CallableHeaderKind::Function, "f"_zc);
  auto cloned = authority.clone();
  auto different =
      definitionAuthority(DefinitionKind::Function, CallableHeaderKind::Function, "g"_zc);

  ZC_EXPECT(authority.verify());
  ZC_EXPECT(cloned.verify());
  ZC_EXPECT(different.verify());
  ZC_EXPECT(authority.key() == cloned.key());
  ZC_EXPECT(authority.sameRecordAs(cloned));
  ZC_EXPECT(!authority.sameRecordAs(different));
  ZC_EXPECT(authority.overloadHeaderAuthority() != zc::none);
  ZC_IF_SOME(overload, authority.overloadHeaderAuthority()) {
    ZC_EXPECT(overload.header().name() == "f"_zc);
    ZC_EXPECT(overload.header().callableKind() == CallableHeaderKind::Function);
  }

  auto nonCallableRecord = definitionRecord(DefinitionKind::Class, "C"_zc);
  zc::Maybe<OverloadHeaderAuthority> noOverload;
  auto nonCallable =
      DefinitionIdentityAuthority::from(zc::mv(nonCallableRecord), zc::mv(noOverload));
  ZC_REQUIRE(nonCallable != zc::none);
  ZC_IF_SOME(value, nonCallable) {
    ZC_EXPECT(value.verify());
    ZC_EXPECT(value.overloadHeaderAuthority() == zc::none);
  }
}

ZC_TEST("Definition identity authority rejects missing and mismatched overload authorities") {
  auto function =
      OverloadHeaderAuthority::from(overloadHeader(CallableHeaderKind::Function, "f"_zc));
  auto functionDigest = function.digest().clone();

  auto missingRecord = callableRecord(DefinitionKind::Function, "f"_zc, functionDigest);
  zc::Maybe<OverloadHeaderAuthority> missing;
  ZC_EXPECT(DefinitionIdentityAuthority::from(zc::mv(missingRecord), zc::mv(missing)) == zc::none);

  auto nonCallableRecord = definitionRecord(DefinitionKind::Class, "C"_zc);
  zc::Maybe<OverloadHeaderAuthority> unexpected = function.clone();
  ZC_EXPECT(DefinitionIdentityAuthority::from(zc::mv(nonCallableRecord), zc::mv(unexpected)) ==
            zc::none);

  auto wrongDigestRecord = callableRecord(DefinitionKind::Function, "f"_zc, functionDigest);
  zc::Maybe<OverloadHeaderAuthority> wrongDigest =
      OverloadHeaderAuthority::from(overloadHeader(CallableHeaderKind::Function, "g"_zc));
  ZC_EXPECT(DefinitionIdentityAuthority::from(zc::mv(wrongDigestRecord), zc::mv(wrongDigest)) ==
            zc::none);

  auto wrongNameRecord = callableRecord(DefinitionKind::Function, "g"_zc, functionDigest);
  zc::Maybe<OverloadHeaderAuthority> wrongName = function.clone();
  ZC_EXPECT(DefinitionIdentityAuthority::from(zc::mv(wrongNameRecord), zc::mv(wrongName)) ==
            zc::none);

  auto wrongKindRecord = callableRecord(DefinitionKind::Method, "f"_zc, functionDigest);
  zc::Maybe<OverloadHeaderAuthority> wrongKind = function.clone();
  ZC_EXPECT(DefinitionIdentityAuthority::from(zc::mv(wrongKindRecord), zc::mv(wrongKind)) ==
            zc::none);
}

ZC_TEST("Generic parameter identities pass the exact subordinate record and digest vectors") {
  auto record = GenericParameterIdentityRecord::type(
      StableGenericParameterOwnerKey::definition(rawDefinitionKey(0x44)), 7);
  ZC_EXPECT(record.kind() == GenericParameterKind::Type);
  ZC_EXPECT(record.ordinal() == 7);
  ZC_EXPECT(record.owner().kind() == StableGenericParameterOwnerKind::Definition);
  ZC_EXPECT(record.encode().size() == 38);
  ZC_EXPECT(zc::encodeHex(record.encode().asPtr()) ==
            "0144444444444444444444444444444444444444444444444444444444444444440100000007"_zc);
  auto key = GenericParameterKey::compute(record);
  ZC_EXPECT(zc::encodeHex(key.bytes()) ==
            "491f5a3aa3da6f5e3643975fc1dc85aa775a5cfc0d645cd34b9b2bffae982afc"_zc);

  auto authority = GenericParameterAuthority::from(zc::mv(record));
  auto cloned = authority.clone();
  auto different = GenericParameterAuthority::from(GenericParameterIdentityRecord::type(
      StableGenericParameterOwnerKey::definition(rawDefinitionKey(0x44)), 8));
  ZC_EXPECT(authority.verify());
  ZC_EXPECT(cloned.verify());
  ZC_EXPECT(authority.sameRecordAs(cloned));
  ZC_EXPECT(!authority.sameRecordAs(different));
}

ZC_TEST("Callable parameter identities distinguish receiver from ordinary position vectors") {
  auto receiver = CallableParameterIdentityRecord::from(rawDefinitionKey(0x66),
                                                        CallableParameterPosition::receiver());
  auto ordinary = CallableParameterIdentityRecord::from(rawDefinitionKey(0x66),
                                                        CallableParameterPosition::ordinary(7));
  ZC_EXPECT(receiver.position().kind() == CallableParameterPositionKind::Receiver);
  ZC_EXPECT(receiver.position().ordinal() == zc::none);
  ZC_EXPECT(ordinary.position().kind() == CallableParameterPositionKind::Ordinary);
  ZC_EXPECT(ordinary.position().ordinal() == 7);
  ZC_EXPECT(receiver.encode().size() == 33);
  ZC_EXPECT(ordinary.encode().size() == 37);
  ZC_EXPECT(zc::encodeHex(receiver.encode().asPtr()) ==
            "666666666666666666666666666666666666666666666666666666666666666601"_zc);
  ZC_EXPECT(zc::encodeHex(ordinary.encode().asPtr()) ==
            "66666666666666666666666666666666666666666666666666666666666666660200000007"_zc);
  auto receiverKey = CallableParameterKey::compute(receiver);
  auto ordinaryKey = CallableParameterKey::compute(ordinary);
  ZC_EXPECT(zc::encodeHex(receiverKey.bytes()) ==
            "1bb0709d06cbff633cc0985471dfa19605a3e823f5ac4eeb39e7778f751c9fdf"_zc);
  ZC_EXPECT(zc::encodeHex(ordinaryKey.bytes()) ==
            "456ff0c1e10b707c7801d6f40054be5e29188aa27d8bef70a51e8883f9cdbe35"_zc);
  ZC_EXPECT(receiverKey != ordinaryKey);

  auto authority = CallableParameterAuthority::from(zc::mv(receiver));
  auto cloned = authority.clone();
  auto different = CallableParameterAuthority::from(zc::mv(ordinary));
  ZC_EXPECT(authority.verify());
  ZC_EXPECT(cloned.verify());
  ZC_EXPECT(authority.sameRecordAs(cloned));
  ZC_EXPECT(!authority.sameRecordAs(different));
}

ZC_TEST("All RFC 0018 raw digest key families admit exactly thirty-two bytes") {
  expectRawDigestLength<DefinitionKey>();
  expectRawDigestLength<ImplKey>();
  expectRawDigestLength<GenericParameterKey>();
  expectRawDigestLength<CallableParameterKey>();
}

ZC_TEST("Implementation identity authority verifies clones and compares complete records") {
  zc::Vector<EnclosingStableOwnerKey> owners;
  auto authority = ImplIdentityAuthority::from(
      ImplIdentityRecord::from(module(), zc::mv(owners), implHeader("Trait"_zc)));
  auto cloned = authority.clone();
  zc::Vector<EnclosingStableOwnerKey> differentOwners;
  auto different = ImplIdentityAuthority::from(
      ImplIdentityRecord::from(module(), zc::mv(differentOwners), implHeader("Other"_zc)));

  ZC_EXPECT(authority.verify());
  ZC_EXPECT(cloned.verify());
  ZC_EXPECT(different.verify());
  ZC_EXPECT(authority.key() == cloned.key());
  ZC_EXPECT(authority.sameRecordAs(cloned));
  ZC_EXPECT(!authority.sameRecordAs(different));
}

}  // namespace zomlang::compiler::identity
