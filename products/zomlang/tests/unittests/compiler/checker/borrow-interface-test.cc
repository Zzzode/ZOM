// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/borrow-interface.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::checker::borrow {
namespace {

identity::Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  ZC_IF_SOME(digest, identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes))) { return digest; }
  ZC_FAIL_REQUIRE("invalid borrow-interface digest fixture");
}

using namespace tests::test_identity_detail;

identity::SemanticContextFingerprint fingerprint(
    const identity::SemanticIdentityRegistrySet& registries) {
  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  zc::Vector<identity::CrateDependencyEdgeKey> crateEdges;
  auto result = identity::SemanticContextFingerprint::compute(registries, packageEdges.asPtr(),
                                                              crateEdges.asPtr());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid borrow-interface fingerprint fixture");
}

zc::StringPtr definitionName(uint32_t ordinal) {
  switch (ordinal) {
    case 0:
      return "borrow0"_zc;
    case 1:
      return "borrow1"_zc;
    case 2:
      return "borrow2"_zc;
    case 3:
      return "borrow3"_zc;
    case 4:
      return "BorrowOwner"_zc;
    default:
      ZC_FAIL_REQUIRE("invalid borrow-interface definition ordinal");
  }
}

identity::CanonicalHeaderTypeSyntax unitHeaderType() {
  auto value = identity::CanonicalHeaderTypeSyntax::predefined(identity::PredefinedTypeKind::Unit);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid borrow-interface unit header type");
}

identity::OverloadHeaderAuthority callableAuthority(identity::DefinitionKind kind, uint32_t ordinal,
                                                    uint32_t parameterCount) {
  zc::Maybe<identity::ReceiverShape> receiver;
  if (kind == identity::DefinitionKind::Method) { receiver = identity::ReceiverShape::Shared; }
  zc::Vector<identity::CanonicalGenericParameter> generics;
  zc::Vector<identity::CanonicalBoundObligation> obligations;
  zc::Vector<identity::CanonicalCallableParameter> parameters(parameterCount);
  for (uint32_t index = 0; index < parameterCount; ++index) {
    parameters.add(identity::CanonicalCallableParameter::from(
        scalar<identity::SemanticIdentifier>(index == 0 ? "first"_zc : "second"_zc),
        unitHeaderType(), false));
  }
  zc::Maybe<zc::Vector<identity::CanonicalHeaderTypeSyntax>> raises;
  zc::Maybe<identity::ExternalAbi> abi;
  auto header = identity::CanonicalOverloadHeader::from(
      kind == identity::DefinitionKind::Method ? identity::CallableHeaderKind::Method
                                               : identity::CallableHeaderKind::Function,
      scalar<identity::DeclaredDefinitionName>(definitionName(ordinal)), zc::mv(receiver),
      zc::mv(generics), zc::mv(obligations), zc::mv(parameters),
      identity::CanonicalCallableResult::unit(), zc::mv(raises), zc::mv(abi));
  ZC_IF_SOME(admitted, header) { return identity::OverloadHeaderAuthority::from(zc::mv(admitted)); }
  ZC_FAIL_REQUIRE("invalid borrow-interface callable header");
}

class BorrowInterfaceFixture final {
public:
  BorrowInterfaceFixture() {
    auto issued = factory.issue();
    ZC_REQUIRE(issued != zc::none);
    ZC_IF_SOME(value, issued) { context = value; }

    auto created = identity::SemanticIdentityRegistrySet::create(factory, context);
    ZC_REQUIRE(created != zc::none);
    ZC_IF_SOME(value, created) {
      registries = zc::heap<identity::SemanticIdentityRegistrySet>(zc::mv(value));
    }
    ZC_REQUIRE(registries->collectPackage(package()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezePackages() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCrates() == identity::FrozenRegistryFailure::None);
    auto sourceSnapshot = identity::ImmutableSourceSnapshot::from(
        tests::test_identity_detail::source(), zc::heapArray<uint8_t>(1, uint8_t{0}));
    ZC_REQUIRE(sourceSnapshot != zc::none);
    ZC_IF_SOME(value, sourceSnapshot) {
      ZC_REQUIRE(registries->collectSourceFile(zc::mv(value)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries->freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->collectModule(module()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeModules() == identity::FrozenRegistryFailure::None);

    addCallable(identity::DefinitionKind::Function, 0, 2);
    addCallable(identity::DefinitionKind::Function, 1, 0);
    addCallable(identity::DefinitionKind::Function, 2, 1);
    addCallable(identity::DefinitionKind::Method, 3, 2);
    addDefinition(identity::DefinitionKind::Class, 4);
    ZC_REQUIRE(registries->freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    for (const auto& key : definitionKeys) {
      auto handle = registries->definitions().find(key);
      ZC_REQUIRE(handle != zc::none);
      ZC_IF_SOME(value, handle) { definitions.add(value); }
    }
    collectCallableParameters(0, 2, false);
    collectCallableParameters(2, 1, false);
    collectCallableParameters(3, 2, true);
    ZC_REQUIRE(registries->freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCallableParameters() == identity::FrozenRegistryFailure::None);
    auto moduleHandle = registries->modules().find(module());
    ZC_REQUIRE(moduleHandle != zc::none);
    ZC_IF_SOME(value, moduleHandle) { moduleId = value; }

    auto token = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(value, token) {
      semanticTypes = zc::heap<type::SemanticTypeStore>(zc::mv(value), *registries);
    }
    unit = intern(type::semantic::TypeData(
        type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::Unit}));
    i32 = intern(type::semantic::TypeData(
        type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::I32}));
    sharedI32 = intern(type::semantic::TypeData(
        type::semantic::ReferenceTypeData{type::semantic::Mutability::Const, i32}));
    mutableI32 = intern(type::semantic::TypeData(
        type::semantic::ReferenceTypeData{type::semantic::Mutability::Mutable, i32}));
    zc::Vector<identity::SemanticTypeId> tupleElements;
    tupleElements.add(sharedI32);
    tupleElements.add(i32);
    nestedSharedI32 =
        intern(type::semantic::TypeData(type::semantic::TupleTypeData{zc::mv(tupleElements)}));

    contextFingerprint = zc::heap<identity::SemanticContextFingerprint>(fingerprint(*registries));
    const auto moduleBytes = module().encode();
    const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
    auto signature = signature::SignatureFactsRevision::computeFramed(
        contextFingerprint->digest(), moduleBytes.asPtr(), repeatedDigest(0x11),
        repeatedDigest(0x22), repeatedDigest(0x33), emptyRecords, emptyRecords, emptyRecords);
    ZC_REQUIRE(signature != zc::none);
    ZC_IF_SOME(value, signature) {
      signatureRevision = zc::heap<signature::SignatureFactsRevision>(value);
    }
    auto imported = cross_module::ImportedSignatureViewRevision::computeFramed(
        contextFingerprint->digest(), moduleBytes.asPtr(), emptyRecords);
    ZC_REQUIRE(imported != zc::none);
    ZC_IF_SOME(value, imported) {
      importedRevision = zc::heap<cross_module::ImportedSignatureViewRevision>(value);
    }
  }

  identity::DefId definition(size_t index) const { return definitions[index]; }

  identity::SourceSpan sourceSpan() const {
    auto result = registries->sourceSnapshots()[0].span(0, 1);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
    ZC_FAIL_REQUIRE("invalid borrow-interface source span fixture");
  }

  signature::ParameterSignature parameter(size_t callableIndex, uint32_t ordinal,
                                          identity::SemanticTypeId type,
                                          signature::ParameterMode mode) const {
    for (const auto& value : callableParameters) {
      if (value.owner == callableIndex && value.ordinal == ordinal) {
        return signature::ParameterSignature{
            value.key.clone(),
            scalar<identity::SemanticIdentifier>(ordinal == 0 ? "first"_zc : "second"_zc), type,
            mode, false};
      }
    }
    ZC_FAIL_REQUIRE("missing borrow-interface callable parameter fixture");
  }

  signature::SemanticSignature callable(size_t definitionIndex, signature::SignatureScope&& scope,
                                        zc::Maybe<signature::ReceiverMode>&& receiver,
                                        zc::Vector<signature::ParameterSignature>&& parameters,
                                        identity::SemanticTypeId result,
                                        zc::Maybe<signature::ExternAbi>&& abi) const {
    zc::Vector<signature::GenericParameterSignature> noGenerics;
    zc::Maybe<identity::SemanticTypeId> noRaises;
    zc::Maybe<signature::ReceiverSignature> receiverSignature;
    ZC_IF_SOME(mode, receiver) {
      for (const auto& value : callableParameters) {
        if (value.owner == definitionIndex && value.receiver) {
          receiverSignature = signature::ReceiverSignature{value.key.clone(), mode};
          break;
        }
      }
      ZC_REQUIRE(receiverSignature != zc::none);
    }
    return signature::SemanticSignature{
        definition(definitionIndex),
        definitionIndex == 3 ? identity::DefinitionKind::Method
                             : identity::DefinitionKind::Function,
        zc::mv(scope),
        zc::Vector<signature::SignatureModifier>(),
        zc::Vector<signature::NormalizedAttributeFact>(),
        signature::SemanticSignaturePayload(signature::CallableSignature{
            zc::mv(noGenerics), zc::mv(receiverSignature), zc::mv(parameters), result,
            zc::mv(noRaises), zc::mv(abi)}),
        sourceSpan()};
  }

  signature::SemanticSignature owner() const {
    zc::Maybe<identity::SemanticTypeId> noBase;
    return signature::SemanticSignature{
        definition(4),
        identity::DefinitionKind::Class,
        signature::SignatureScope(signature::ModuleDefinitionSignatureScope{}),
        zc::Vector<signature::SignatureModifier>(),
        zc::Vector<signature::NormalizedAttributeFact>(),
        signature::SemanticSignaturePayload(signature::NominalSignature{
            zc::Vector<signature::GenericParameterSignature>(), zc::mv(noBase),
            zc::Vector<signature::InterfaceInstantiation>(), zc::Vector<identity::DefId>(),
            zc::Vector<identity::DefId>(), zc::Vector<identity::DefId>()}),
        sourceSpan()};
  }

  BorrowInterfaceBuildResult build(zc::ArrayPtr<const signature::SemanticSignature> local,
                                   zc::ArrayPtr<const signature::SemanticSignature> support = {}) {
    return BorrowInterfaceBuilder::build(
        BorrowInterfaceBuildInput{context, *contextFingerprint, moduleId, *signatureRevision,
                                  *importedRevision, local, support, *registries, *semanticTypes});
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  zc::Own<identity::SemanticIdentityRegistrySet> registries;
  zc::Own<type::SemanticTypeStore> semanticTypes;
  identity::ModuleId moduleId;
  identity::SemanticTypeId unit;
  identity::SemanticTypeId i32;
  identity::SemanticTypeId sharedI32;
  identity::SemanticTypeId mutableI32;
  identity::SemanticTypeId nestedSharedI32;
  zc::Own<identity::SemanticContextFingerprint> contextFingerprint;
  zc::Own<signature::SignatureFactsRevision> signatureRevision;
  zc::Own<cross_module::ImportedSignatureViewRevision> importedRevision;

private:
  identity::SemanticTypeId intern(type::semantic::TypeData&& data) {
    auto canonical = semanticTypes->canonicalizeClosed(zc::mv(data));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto result = semanticTypes->intern(zc::mv(canonical.get<type::semantic::CanonicalTypeData>()));
    ZC_REQUIRE(result.is<type::SemanticTypeInterned>());
    return result.get<type::SemanticTypeInterned>().id;
  }

  void addDefinition(identity::DefinitionKind kind, uint32_t ordinal) {
    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Maybe<identity::OverloadHeaderDigest> noOverload;
    auto record = identity::DefinitionIdentityRecord::from(
        module(), zc::mv(owners), kind, identity::DefinitionNamespace::Type,
        scalar<identity::DeclaredDefinitionName>(definitionName(ordinal)), zc::mv(noOverload));
    ZC_REQUIRE(record != zc::none);
    ZC_IF_SOME(value, record) {
      definitionKeys.add(identity::DefinitionKey::compute(value));
      zc::Maybe<identity::OverloadHeaderAuthority> noAuthority;
      ZC_REQUIRE(registries->collectDefinition(zc::mv(value), zc::mv(noAuthority), ordinal) ==
                 identity::FrozenRegistryFailure::None);
    }
  }

  void addCallable(identity::DefinitionKind kind, uint32_t ordinal, uint32_t parameterCount) {
    auto authority = callableAuthority(kind, ordinal, parameterCount);
    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Maybe<identity::OverloadHeaderDigest> digest = authority.digest().clone();
    auto record = identity::DefinitionIdentityRecord::from(
        module(), zc::mv(owners), kind, identity::DefinitionNamespace::Value,
        scalar<identity::DeclaredDefinitionName>(definitionName(ordinal)), zc::mv(digest));
    ZC_REQUIRE(record != zc::none);
    ZC_IF_SOME(value, record) {
      definitionKeys.add(identity::DefinitionKey::compute(value));
      zc::Maybe<identity::OverloadHeaderAuthority> retained = zc::mv(authority);
      ZC_REQUIRE(registries->collectDefinition(zc::mv(value), zc::mv(retained), ordinal) ==
                 identity::FrozenRegistryFailure::None);
    }
  }

  void collectCallableParameters(size_t owner, uint32_t parameterCount, bool receiver) {
    if (receiver) {
      auto record = identity::CallableParameterIdentityRecord::from(
          definitionKeys[owner].clone(), identity::CallableParameterPosition::receiver());
      auto key = identity::CallableParameterKey::compute(record);
      callableParameters.add(CallableParameterFixture{owner, 0, true, key.clone()});
      ZC_REQUIRE(registries->collectCallableParameter(zc::mv(record)) ==
                 identity::FrozenRegistryFailure::None);
    }
    for (uint32_t ordinal = 0; ordinal < parameterCount; ++ordinal) {
      auto record = identity::CallableParameterIdentityRecord::from(
          definitionKeys[owner].clone(), identity::CallableParameterPosition::ordinary(ordinal));
      auto key = identity::CallableParameterKey::compute(record);
      callableParameters.add(CallableParameterFixture{owner, ordinal, false, key.clone()});
      ZC_REQUIRE(registries->collectCallableParameter(zc::mv(record)) ==
                 identity::FrozenRegistryFailure::None);
    }
  }

  struct CallableParameterFixture final {
    size_t owner;
    uint32_t ordinal;
    bool receiver;
    identity::CallableParameterKey key;
  };

  zc::Vector<identity::DefinitionKey> definitionKeys;
  zc::Vector<identity::DefId> definitions;
  zc::Vector<CallableParameterFixture> callableParameters;
};

}  // namespace

ZC_TEST("BorrowSignatureCanonicalCodec.ReproducesNonEmptyOracle") {
  zc::Vector<BorrowInputRegion> inputs;
  inputs.add(BorrowInputRegion::receiver());
  inputs.add(BorrowInputRegion::parameter(2));
  const auto summary =
      BorrowSignatureSummary{identity::DefId(), zc::mv(inputs),
                             BorrowReturnRelation::directRoot(BorrowInputRegion::parameter(2))};
  const uint8_t callable[] = {0xa1};
  auto encoded = BorrowSignatureCanonicalCodec::encodeFramed(summary, callable);
  ZC_REQUIRE(encoded != zc::none);
  ZC_IF_SOME(bytes, encoded) {
    ZC_EXPECT(bytes.size() == 61);
    ZC_IF_SOME(digest, identity::sha256(bytes.asPtr())) {
      ZC_EXPECT(zc::encodeHex(digest.bytes()) ==
                "bda5523f367e5abb71aaabf0e3a8b5dbe920c3dbe73d5d1051fbc6739db7dc30"_zc);
    }
  }
}

ZC_TEST("BorrowInterfaceRevision.ReproducesEmptySurfaceOracle") {
  const uint8_t module[] = {0xa1};
  const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
  auto revision = BorrowInterfaceRevision::computeFramed(
      repeatedDigest(0x00), module, repeatedDigest(0x22), repeatedDigest(0x33), emptyRecords);
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "409eb28bdc0218f92eebcf9411687d24fbe7eb70ee527910110804a3f23fe81c"_zc);
  }
}

ZC_TEST("BorrowSignatureCanonicalCodec.RejectsUnsortedAndUnboundRelations") {
  const uint8_t callable[] = {0xa1};
  zc::Vector<BorrowInputRegion> reversed;
  reversed.add(BorrowInputRegion::parameter(1));
  reversed.add(BorrowInputRegion::receiver());
  const auto unsorted =
      BorrowSignatureSummary{identity::DefId(), zc::mv(reversed), BorrowReturnRelation::none()};
  ZC_EXPECT(BorrowSignatureCanonicalCodec::encodeFramed(unsorted, callable) == zc::none);

  zc::Vector<BorrowInputRegion> inputs;
  inputs.add(BorrowInputRegion::receiver());
  const auto unbound =
      BorrowSignatureSummary{identity::DefId(), zc::mv(inputs),
                             BorrowReturnRelation::directRoot(BorrowInputRegion::parameter(0))};
  ZC_EXPECT(BorrowSignatureCanonicalCodec::encodeFramed(unbound, callable) == zc::none);
}

ZC_TEST("BorrowInterfaceBuilder.PublishesSingleInputAndReceiverRelations") {
  BorrowInterfaceFixture fixture;
  zc::Vector<signature::SemanticSignature> singleInput;
  zc::Vector<signature::ParameterSignature> parameters;
  parameters.add(
      fixture.parameter(0, 0, fixture.sharedI32, signature::ParameterMode::SharedReference));
  zc::Maybe<signature::ReceiverMode> noReceiver;
  zc::Maybe<signature::ExternAbi> noAbi;
  singleInput.add(
      fixture.callable(0, signature::SignatureScope(signature::ModuleDefinitionSignatureScope{}),
                       zc::mv(noReceiver), zc::mv(parameters), fixture.sharedI32, zc::mv(noAbi)));
  auto result = fixture.build(singleInput.asPtr());
  ZC_REQUIRE(result.is<VerifiedBorrowInterfaceSurface>());
  const auto& surface = result.get<VerifiedBorrowInterfaceSurface>();
  ZC_REQUIRE(surface.summaries().size() == 1);
  ZC_EXPECT(surface.summaries()[0].directInputs.size() == 1);
  ZC_EXPECT(surface.summaries()[0].directInputs[0] == BorrowInputRegion::parameter(0));
  ZC_EXPECT(surface.summaries()[0].returnRelation.tag() == BorrowReturnRelationTag::DirectRoot);
  ZC_EXPECT(surface.summaries()[0].returnRelation.source() == BorrowInputRegion::parameter(0));

  zc::Vector<signature::SemanticSignature> receiverDefinitions;
  receiverDefinitions.add(fixture.owner());
  zc::Vector<signature::ParameterSignature> receiverParameters;
  receiverParameters.add(
      fixture.parameter(3, 0, fixture.sharedI32, signature::ParameterMode::SharedReference));
  receiverParameters.add(
      fixture.parameter(3, 1, fixture.mutableI32, signature::ParameterMode::MutableReference));
  zc::Maybe<signature::ReceiverMode> receiver = signature::ReceiverMode::Shared;
  zc::Maybe<signature::ExternAbi> memberNoAbi;
  receiverDefinitions.add(fixture.callable(
      3,
      signature::SignatureScope(signature::MemberSignatureScope{
          fixture.definition(4), signature::MemberVisibility::Public}),
      zc::mv(receiver), zc::mv(receiverParameters), fixture.sharedI32, zc::mv(memberNoAbi)));
  auto receiverResult = fixture.build(receiverDefinitions.asPtr());
  ZC_REQUIRE(receiverResult.is<VerifiedBorrowInterfaceSurface>());
  const auto& receiverSurface = receiverResult.get<VerifiedBorrowInterfaceSurface>();
  ZC_REQUIRE(receiverSurface.summaries().size() == 1);
  ZC_EXPECT(receiverSurface.summaries()[0].directInputs.size() == 3);
  ZC_EXPECT(receiverSurface.summaries()[0].returnRelation.source() ==
            BorrowInputRegion::receiver());
}

ZC_TEST("BorrowInterfaceBuilder.SelectsClosedSourceFailurePrecedence") {
  BorrowInterfaceFixture fixture;
  zc::Vector<signature::SemanticSignature> definitions;

  zc::Vector<signature::ParameterSignature> ambiguousParameters;
  ambiguousParameters.add(
      fixture.parameter(0, 0, fixture.sharedI32, signature::ParameterMode::SharedReference));
  ambiguousParameters.add(
      fixture.parameter(0, 1, fixture.mutableI32, signature::ParameterMode::MutableReference));
  zc::Maybe<signature::ReceiverMode> noReceiver0;
  zc::Maybe<signature::ExternAbi> noAbi0;
  definitions.add(fixture.callable(
      0, signature::SignatureScope(signature::ModuleDefinitionSignatureScope{}),
      zc::mv(noReceiver0), zc::mv(ambiguousParameters), fixture.sharedI32, zc::mv(noAbi0)));

  zc::Vector<signature::ParameterSignature> noParameters;
  zc::Maybe<signature::ReceiverMode> noReceiver1;
  zc::Maybe<signature::ExternAbi> noAbi1;
  definitions.add(fixture.callable(
      1, signature::SignatureScope(signature::ModuleDefinitionSignatureScope{}),
      zc::mv(noReceiver1), zc::mv(noParameters), fixture.nestedSharedI32, zc::mv(noAbi1)));

  zc::Vector<signature::ParameterSignature> externParameters;
  externParameters.add(fixture.parameter(2, 0, fixture.sharedI32, signature::ParameterMode::Value));
  zc::Maybe<signature::ReceiverMode> noReceiver2;
  zc::Maybe<signature::ExternAbi> externAbi = signature::ExternAbi::Cdecl;
  definitions.add(fixture.callable(
      2, signature::SignatureScope(signature::ModuleDefinitionSignatureScope{}),
      zc::mv(noReceiver2), zc::mv(externParameters), fixture.unit, zc::mv(externAbi)));

  auto result = fixture.build(definitions.asPtr());
  ZC_REQUIRE(result.is<BorrowInterfaceSourceRejected>());
  const auto& failures = result.get<BorrowInterfaceSourceRejected>().failures;
  ZC_REQUIRE(failures.size() == 3);
  ZC_EXPECT(failures[0].kind == BorrowSignatureFailureKind::AmbiguousDirectResult);
  ZC_EXPECT(failures[1].kind == BorrowSignatureFailureKind::UnexpressibleResult);
  ZC_EXPECT(failures[2].kind == BorrowSignatureFailureKind::UnverifiedExternContract);
  ZC_EXPECT(failures[0].traversalOrdinal == 0);
  ZC_EXPECT(failures[1].traversalOrdinal == 1);
  ZC_EXPECT(failures[2].traversalOrdinal == 2);
}

ZC_TEST("BorrowInterfaceBuilder.RejectsModeTypeAndReceiverProjectionMismatch") {
  BorrowInterfaceFixture fixture;
  zc::Vector<signature::SemanticSignature> modeMismatch;
  zc::Vector<signature::ParameterSignature> parameters;
  parameters.add(
      fixture.parameter(0, 0, fixture.mutableI32, signature::ParameterMode::SharedReference));
  zc::Maybe<signature::ReceiverMode> noReceiver;
  zc::Maybe<signature::ExternAbi> noAbi;
  modeMismatch.add(
      fixture.callable(0, signature::SignatureScope(signature::ModuleDefinitionSignatureScope{}),
                       zc::mv(noReceiver), zc::mv(parameters), fixture.unit, zc::mv(noAbi)));
  auto modeResult = fixture.build(modeMismatch.asPtr());
  ZC_REQUIRE(modeResult.is<BorrowInterfaceInvariantRejected>());
  const auto& modeFailures = modeResult.get<BorrowInterfaceInvariantRejected>().failures;
  ZC_REQUIRE(modeFailures.size() == 1);
  ZC_REQUIRE(modeFailures[0].variant().is<signature::CheckerInvariantFact>());
  ZC_EXPECT(modeFailures[0].variant().get<signature::CheckerInvariantFact>().kind ==
            signature::CheckerInvariantKind::InvalidFact);

  zc::Vector<signature::SemanticSignature> invalidReceiver;
  zc::Vector<signature::ParameterSignature> noParameters;
  zc::Maybe<signature::ReceiverMode> receiver = signature::ReceiverMode::Shared;
  zc::Maybe<signature::ExternAbi> memberNoAbi;
  invalidReceiver.add(
      fixture.callable(3, signature::SignatureScope(signature::ModuleDefinitionSignatureScope{}),
                       zc::mv(receiver), zc::mv(noParameters), fixture.unit, zc::mv(memberNoAbi)));
  auto receiverResult = fixture.build(invalidReceiver.asPtr());
  ZC_REQUIRE(receiverResult.is<BorrowInterfaceInvariantRejected>());
  const auto& receiverFailures = receiverResult.get<BorrowInterfaceInvariantRejected>().failures;
  ZC_REQUIRE(receiverFailures.size() == 1);
  ZC_REQUIRE(receiverFailures[0].variant().is<signature::CheckerInvariantFact>());
  ZC_EXPECT(receiverFailures[0].variant().get<signature::CheckerInvariantFact>().kind ==
            signature::CheckerInvariantKind::InvalidFact);
}

}  // namespace zomlang::compiler::checker::borrow
