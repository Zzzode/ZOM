// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/checker/borrow/borrow-interface.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "tests/unittests/compiler/checker/checker-authority-test-fixture.h"

namespace zomlang::compiler::checker::borrow {
namespace {

identity::Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  ZC_IF_SOME(digest, identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes))) { return digest; }
  ZC_FAIL_REQUIRE("invalid borrow-interface digest fixture");
}

class BorrowInterfaceFixture final {
public:
  BorrowInterfaceFixture()
      : session(
            "fun borrow0(first: i32, second: i32) -> i32 { return 0; }\n"
            "fun borrow1() -> i32 { return 0; }\n"
            "fun borrow2(first: i32) -> i32 { return 0; }\n"
            "class RecoveryOwner {\n"
            "  fun borrow3(this, first: i32, second: i32) -> i32 { return 0; }\n"
            "}\n"_zc),
        context(session.semanticContext()),
        moduleId(session.module()) {
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

    definitions.add(definitionNamed("borrow0"_zc));
    definitions.add(definitionNamed("borrow1"_zc));
    definitions.add(definitionNamed("borrow2"_zc));
    definitions.add(definitionNamed("borrow3"_zc));
    definitions.add(session.owner());
    auto module = session.identityAuthority().module(moduleId);
    ZC_REQUIRE(module != zc::none);
    const auto moduleBytes = ZC_REQUIRE_NONNULL(module).key().encode();
    const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
    auto signature = signature::SignatureFactsRevision::computeFramed(
        session.identityAuthority().fingerprint().digest(), moduleBytes.asPtr(),
        repeatedDigest(0x11), repeatedDigest(0x22), repeatedDigest(0x33), emptyRecords,
        emptyRecords, emptyRecords);
    ZC_REQUIRE(signature != zc::none);
    ZC_IF_SOME(value, signature) {
      signatureRevision = zc::heap<signature::SignatureFactsRevision>(value);
    }
    auto imported = cross_module::ImportedSignatureViewRevision::computeFramed(
        session.identityAuthority().fingerprint().digest(), moduleBytes.asPtr(), emptyRecords);
    ZC_REQUIRE(imported != zc::none);
    ZC_IF_SOME(value, imported) {
      importedRevision = zc::heap<cross_module::ImportedSignatureViewRevision>(value);
    }
  }

  identity::DefId definition(size_t index) const { return definitions[index]; }

  identity::SourceSpan sourceSpan() const { return session.span(0, 1); }

  signature::ParameterSignature parameter(size_t callableIndex, uint32_t ordinal,
                                          identity::SemanticTypeId type,
                                          signature::ParameterMode mode) const {
    auto owner = session.identityAuthority().definition(definition(callableIndex));
    ZC_REQUIRE(owner != zc::none);
    for (const auto& module : session.identityAuthority().modules()) {
      for (const auto& value : module.definitions().identities().callableParameters()) {
        const auto position = value.record().position();
        ZC_IF_SOME(index, position.ordinal()) {
          if (value.record().owner() == ZC_REQUIRE_NONNULL(owner).key() && index == ordinal) {
            return signature::ParameterSignature{
                value.key().clone(),
                tests::checker_fixture::scalar<identity::SemanticIdentifier>(
                    ordinal == 0 ? "first"_zc : "second"_zc),
                type, mode, false};
          }
        }
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
      auto owner = session.identityAuthority().definition(definition(definitionIndex));
      ZC_REQUIRE(owner != zc::none);
      for (const auto& module : session.identityAuthority().modules()) {
        for (const auto& value : module.definitions().identities().callableParameters()) {
          if (value.record().owner() == ZC_REQUIRE_NONNULL(owner).key() &&
              value.record().position().kind() ==
                  identity::CallableParameterPositionKind::Receiver) {
            receiverSignature = signature::ReceiverSignature{value.key().clone(), mode};
            break;
          }
        }
        if (receiverSignature != zc::none) { break; }
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
    return BorrowInterfaceBuilder::build(BorrowInterfaceBuildInput{
        context, session.identityAuthority().fingerprint(), moduleId, *signatureRevision,
        *importedRevision, local, support, session.identityAuthority(), session.semanticTypes()});
  }

  tests::checker_fixture::CheckerAuthoritySession session;
  identity::SemanticContextBrand context;
  identity::ModuleId moduleId;
  identity::SemanticTypeId unit;
  identity::SemanticTypeId i32;
  identity::SemanticTypeId sharedI32;
  identity::SemanticTypeId mutableI32;
  identity::SemanticTypeId nestedSharedI32;
  zc::Own<signature::SignatureFactsRevision> signatureRevision;
  zc::Own<cross_module::ImportedSignatureViewRevision> importedRevision;

private:
  identity::SemanticTypeId intern(type::semantic::TypeData&& data) {
    auto canonical = session.semanticTypes().canonicalizeClosed(zc::mv(data));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto result =
        session.semanticTypes().intern(zc::mv(canonical.get<type::semantic::CanonicalTypeData>()));
    ZC_REQUIRE(result.is<type::SemanticTypeInterned>());
    return result.get<type::SemanticTypeInterned>().id;
  }

  identity::DefId definitionNamed(zc::StringPtr name) const {
    for (const auto& module : session.identityAuthority().modules()) {
      for (const auto& definition : module.definitions().definitions()) {
        if (definition.record.name() == name) { return definition.definition; }
      }
    }
    ZC_FAIL_REQUIRE("missing borrow-interface definition fixture");
  }

  zc::Vector<identity::DefId> definitions;
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
    ZC_EXPECT(bytes.size() == 58);
    ZC_IF_SOME(digest, identity::sha256(bytes.asPtr())) {
      ZC_EXPECT(zc::encodeHex(digest.bytes()) ==
                "fcaee879534108f89aa47013f72b6f17f6dd783def9ccd46029d76eb752ce603"_zc);
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
              "799e2fed5be5220c268a5413afd2713520add15a0505105d61c9850c4256737a"_zc);
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

ZC_TEST("BorrowInterfaceBuilder.RejectsDuplicateCallableSummary") {
  BorrowInterfaceFixture fixture;
  zc::Vector<signature::SemanticSignature> definitions;
  for (size_t index = 0; index < 2; ++index) {
    zc::Vector<signature::ParameterSignature> parameters;
    zc::Maybe<signature::ReceiverMode> noReceiver;
    zc::Maybe<signature::ExternAbi> noAbi;
    definitions.add(
        fixture.callable(0, signature::SignatureScope(signature::ModuleDefinitionSignatureScope{}),
                         zc::mv(noReceiver), zc::mv(parameters), fixture.unit, zc::mv(noAbi)));
  }

  auto result = fixture.build(definitions.asPtr());
  ZC_REQUIRE(result.is<BorrowInterfaceInvariantRejected>());
  const auto& failures = result.get<BorrowInterfaceInvariantRejected>().failures;
  ZC_REQUIRE(failures.size() == 1);
  ZC_REQUIRE(failures[0].variant().is<signature::CheckerInvariantFact>());
  ZC_EXPECT(failures[0].variant().get<signature::CheckerInvariantFact>().kind ==
            signature::CheckerInvariantKind::AdditionalFact);
}

}  // namespace zomlang::compiler::checker::borrow
