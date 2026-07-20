// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/dispatch-facts.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/type/semantic-type-data.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::checker::dispatch {
namespace {

using namespace tests::test_identity_detail;

identity::SemanticContextFingerprint fingerprint(
    const identity::SemanticIdentityRegistrySet& registries) {
  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  zc::Vector<identity::CrateDependencyEdgeKey> crateEdges;
  auto result = identity::SemanticContextFingerprint::compute(registries, packageEdges.asPtr(),
                                                              crateEdges.asPtr());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid dispatch-facts fingerprint fixture");
}

binder::ParsedModuleReceipt parsedReceipt(const identity::SemanticIdentityRegistrySet& registries) {
  const auto sourceBytes = tests::test_identity_detail::source().encode();
  const auto& snapshot = registries.sourceSnapshots()[0];
  const uint8_t astDump[] = {0x01};
  auto result =
      binder::ParsedModuleReceipt::compute(sourceBytes.asPtr(), snapshot.contentDigest(),
                                           snapshot.bytes().size(), digest(0x44), astDump);
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("invalid dispatch-facts parsed receipt fixture");
}

template <typename Map>
Map emptyMap() {
  zc::Vector<typename Map::Entry> entries;
  return Map::fromEntries(zc::mv(entries));
}

class DispatchFactsFixture final {
public:
  enum class CandidateShape : uint8_t {
    Ordered,
    Reversed,
    MissingSecond,
    DuplicateFirst,
    ForeignStore
  };

  DispatchFactsFixture() {
    auto issued = factory.issue();
    ZC_REQUIRE(issued != zc::none);
    ZC_IF_SOME(value, issued) { context = value; }
    auto foreign = foreignFactory.issue();
    ZC_REQUIRE(foreign != zc::none);
    ZC_IF_SOME(value, foreign) { foreignContext = value; }

    auto created = identity::SemanticIdentityRegistrySet::create(factory, context);
    ZC_REQUIRE(created != zc::none);
    ZC_IF_SOME(value, created) {
      registries = zc::heap<identity::SemanticIdentityRegistrySet>(zc::mv(value));
    }
    auto brands = factory.issueRegistryBrandIssuer(context);
    ZC_REQUIRE(brands != zc::none);
    ZC_IF_SOME(value, brands) {
      factStoreBrands = zc::heap<identity::RegistryBrandIssuer>(zc::mv(value));
    }

    ZC_REQUIRE(registries->collectPackage(package()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezePackages() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCrates() == identity::FrozenRegistryFailure::None);
    auto snapshot = identity::ImmutableSourceSnapshot::from(tests::test_identity_detail::source(),
                                                            zc::heapArray<uint8_t>(1, uint8_t{0}));
    ZC_REQUIRE(snapshot != zc::none);
    ZC_IF_SOME(value, snapshot) {
      ZC_REQUIRE(registries->collectSourceFile(zc::mv(value)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries->freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->collectModule(module()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeModules() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCallableParameters() == identity::FrozenRegistryFailure::None);
    auto foundModule = registries->modules().find(module());
    ZC_REQUIRE(foundModule != zc::none);
    ZC_IF_SOME(value, foundModule) { moduleId = value; }

    auto typeToken = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_REQUIRE(typeToken != zc::none);
    ZC_IF_SOME(value, typeToken) {
      semanticTypes = zc::heap<type::SemanticTypeStore>(zc::mv(value), *registries);
    }
    auto canonical = semanticTypes->canonicalizeClosed(type::semantic::TypeData(
        type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::I32}));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto interned =
        semanticTypes->intern(zc::mv(canonical.get<type::semantic::CanonicalTypeData>()));
    ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
    i32 = interned.get<type::SemanticTypeInterned>().id;

    contextFingerprint = zc::heap<identity::SemanticContextFingerprint>(fingerprint(*registries));
    parsed = zc::heap<binder::ParsedModuleReceipt>(parsedReceipt(*registries));
    const auto moduleBytes = module().encode();
    const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
    auto signature = signature::SignatureFactsRevision::computeFramed(
        contextFingerprint->digest(), moduleBytes.asPtr(),
        registries->sourceSnapshots()[0].contentDigest(), digest(0x21), digest(0x22), emptyRecords,
        emptyRecords, emptyRecords);
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
    auto coherence = cross_module::CoherenceViewRevision::computeFramed(
        contextFingerprint->digest(), digest(0x23), emptyRecords, emptyRecords, emptyRecords);
    ZC_REQUIRE(coherence != zc::none);
    ZC_IF_SOME(value, coherence) {
      coherenceRevision = zc::heap<cross_module::CoherenceViewRevision>(value);
    }

    repository = zc::heap<checked::CheckedFactsRepository>(context);
    auto first = repository->adopt(buildCheckedFacts(0xb3));
    ZC_REQUIRE(first.is<checked::CheckedEvidenceLease>());
    firstLease =
        zc::heap<checked::CheckedEvidenceLease>(zc::mv(first.get<checked::CheckedEvidenceLease>()));
    auto second = repository->adopt(buildCheckedFacts(0xc3));
    ZC_REQUIRE(second.is<checked::CheckedEvidenceLease>());
    secondLease = zc::heap<checked::CheckedEvidenceLease>(
        zc::mv(second.get<checked::CheckedEvidenceLease>()));

    projections.add(DispatchNodeProjection{ast::NodeId(1), checkedNode(0)});
    projections.add(DispatchNodeProjection{ast::NodeId(2), checkedNode(1)});
    requirements.add(DispatchSiteRequirement{
        ast::NodeId(1), checkedNode(0), zc::none, DispatchSiteKind::BinaryOperator,
        DispatchReceiverRole::OperatorLeftHandSide, PrimitiveOperation::Add, zc::none});
    requirements.add(DispatchSiteRequirement{
        ast::NodeId(2), checkedNode(1), zc::none, DispatchSiteKind::BinaryOperator,
        DispatchReceiverRole::OperatorLeftHandSide, PrimitiveOperation::Sub, zc::none});
  }

  const checked::VerifiedCheckedFacts& firstFacts() const {
    auto result = repository->lookup(*firstLease);
    ZC_IF_SOME(value, result) { return value; }
    ZC_FAIL_REQUIRE("first checked evidence lookup failed");
  }

  const checked::VerifiedCheckedFacts& secondFacts() const {
    auto result = repository->lookup(*secondLease);
    ZC_IF_SOME(value, result) { return value; }
    ZC_FAIL_REQUIRE("second checked evidence lookup failed");
  }

  DispatchFactsVerificationInput input(const checked::CheckedEvidenceLease& lease,
                                       const checked::VerifiedCheckedFacts& facts) const {
    return DispatchFactsVerificationInput{*contextFingerprint,
                                          moduleId,
                                          registries->sourceSnapshots()[0].source(),
                                          requirements.asPtr(),
                                          projections.asPtr(),
                                          lease,
                                          facts,
                                          *registries,
                                          *semanticTypes};
  }

  DispatchFactsCandidate candidate(
      CandidateShape shape = CandidateShape::Ordered,
      zc::Maybe<const checked::CheckedFactsRevision&> checkedRevision = zc::none,
      identity::SemanticContextBrand candidateContext = identity::SemanticContextBrand()) const {
    const auto& facts = firstFacts();
    zc::Maybe<const checked::CheckedFactsRevision&> selectedRevision = facts.revision();
    ZC_IF_SOME(value, checkedRevision) { selectedRevision = value; }
    const auto ownedContext = candidateContext.isValid() ? candidateContext : context;
    zc::Vector<DispatchFactCandidateEntry> entries;
    if (shape == CandidateShape::Ordered) {
      entries.add(entry(ast::NodeId(1), 0, PrimitiveOperation::Add));
      entries.add(entry(ast::NodeId(2), 1, PrimitiveOperation::Sub));
    } else if (shape == CandidateShape::Reversed) {
      entries.add(entry(ast::NodeId(2), 1, PrimitiveOperation::Sub));
      entries.add(entry(ast::NodeId(1), 0, PrimitiveOperation::Add));
    } else if (shape == CandidateShape::MissingSecond) {
      entries.add(entry(ast::NodeId(1), 0, PrimitiveOperation::Add));
    } else if (shape == CandidateShape::DuplicateFirst) {
      entries.add(entry(ast::NodeId(1), 0, PrimitiveOperation::Add));
      entries.add(entry(ast::NodeId(2), 1, PrimitiveOperation::Sub));
      entries.add(entry(ast::NodeId(1), 0, PrimitiveOperation::Add));
    } else if (shape == CandidateShape::ForeignStore) {
      auto foreignSubstitution = secondFacts().substitutionStore().idAt(0);
      ZC_REQUIRE(foreignSubstitution != zc::none);
      ZC_IF_SOME(value, foreignSubstitution) { entries.add(foreignStoreEntry(value)); }
      entries.add(entry(ast::NodeId(2), 1, PrimitiveOperation::Sub));
    }
    ZC_IF_SOME(revision, selectedRevision) {
      return DispatchFactsCandidate(ownedContext, contextFingerprint->clone(), moduleId, revision,
                                    zc::mv(entries));
    }
    ZC_UNREACHABLE
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextFactory foreignFactory;
  identity::SemanticContextBrand context;
  identity::SemanticContextBrand foreignContext;
  zc::Own<identity::SemanticIdentityRegistrySet> registries;
  zc::Own<identity::RegistryBrandIssuer> factStoreBrands;
  zc::Own<type::SemanticTypeStore> semanticTypes;
  identity::ModuleId moduleId;
  identity::SemanticTypeId i32;
  zc::Own<identity::SemanticContextFingerprint> contextFingerprint;
  zc::Own<binder::ParsedModuleReceipt> parsed;
  zc::Own<signature::SignatureFactsRevision> signatureRevision;
  zc::Own<cross_module::ImportedSignatureViewRevision> importedRevision;
  zc::Own<cross_module::CoherenceViewRevision> coherenceRevision;
  zc::Own<checked::CheckedFactsRepository> repository;
  zc::Own<checked::CheckedEvidenceLease> firstLease;
  zc::Own<checked::CheckedEvidenceLease> secondLease;
  zc::Vector<DispatchNodeProjection> projections;
  zc::Vector<DispatchSiteRequirement> requirements;

private:
  checked::CheckedNodeKey checkedNode(uint32_t preorder) const {
    auto span = registries->sourceSnapshots()[0].span(0, 1);
    ZC_IF_SOME(value, span) { return checked::CheckedNodeKey{0x31, preorder, zc::mv(value)}; }
    ZC_FAIL_REQUIRE("invalid dispatch checked-node span");
  }

  checked::TypedCallFact call(ast::NodeId node, PrimitiveOperation operation) const {
    zc::Maybe<checked::CheckedArgumentFact> noReceiver;
    zc::Maybe<checked::ReceiverMode> noReceiverMode;
    zc::Maybe<checked::ReceiverAdjustment> noReceiverAdjustment;
    zc::Vector<checked::CheckedArgumentFact> noArguments;
    zc::Maybe<checked::CanonicalSubstitutionId> noSubstitutions;
    zc::Maybe<checked::WitnessArgumentsId> noWitnesses;
    zc::Maybe<identity::SemanticTypeId> noRaises;
    return checked::TypedCallFact{
        node,
        checked::CheckedCallEnvelope{
            checked::SelectedCallable(checked::PrimitiveCallable{operation}), i32,
            zc::mv(noReceiver), zc::mv(noReceiverMode), zc::mv(noReceiverAdjustment),
            zc::mv(noArguments), i32, i32, zc::mv(noSubstitutions), zc::mv(noWitnesses),
            zc::mv(noRaises)},
        checkedNode(node.value - 1).sourceSpan};
  }

  checked::VerifiedCheckedFacts buildCheckedFacts(uint8_t canonicalBase) {
    auto substitutionBrand = factStoreBrands->issue();
    auto witnessBrand = factStoreBrands->issue();
    ZC_REQUIRE(substitutionBrand != zc::none);
    ZC_REQUIRE(witnessBrand != zc::none);
    zc::Maybe<checked::FrozenSubstitutionStore> substitutions;
    zc::Maybe<checked::FrozenWitnessStore> witnesses;
    ZC_IF_SOME(brand, substitutionBrand) {
      zc::Vector<identity::DefId> noParameters;
      zc::Vector<identity::SemanticTypeId> noArguments;
      zc::Vector<checked::FrozenSubstitutionStore::Record> records;
      records.add(checked::FrozenSubstitutionStore::Record{
          checked::SubstitutionData{zc::mv(noParameters), zc::mv(noArguments)},
          zc::heapArray<uint8_t>(1, uint8_t{0xa1})});
      substitutions = checked::FrozenSubstitutionStore::from(context, brand, zc::mv(records));
    }
    ZC_IF_SOME(brand, witnessBrand) {
      zc::Vector<checked::FrozenWitnessStore::Record> records;
      records.add(checked::FrozenWitnessStore::Record{
          checked::WitnessArgumentsData{zc::Vector<checked::WitnessEntry>()},
          zc::heapArray<uint8_t>(1, uint8_t{0xa2})});
      witnesses = checked::FrozenWitnessStore::from(context, brand, zc::mv(records));
    }
    ZC_REQUIRE(substitutions != zc::none);
    ZC_REQUIRE(witnesses != zc::none);

    const auto firstOperation =
        canonicalBase == 0xc3 ? PrimitiveOperation::Mul : PrimitiveOperation::Add;
    const auto secondOperation =
        canonicalBase == 0xc3 ? PrimitiveOperation::Div : PrimitiveOperation::Sub;
    zc::Vector<checked::CallFactMap::Entry> calls;
    calls.add(checked::CallFactMap::Entry{ast::NodeId(1), call(ast::NodeId(1), firstOperation),
                                          zc::heapArray<uint8_t>(1, canonicalBase)});
    calls.add(checked::CallFactMap::Entry{ast::NodeId(2), call(ast::NodeId(2), secondOperation),
                                          zc::heapArray<uint8_t>(1, uint8_t(canonicalBase + 1))});
    zc::Vector<checked::NodeFactRequirement> nodeRequirements;
    nodeRequirements.add(checked::NodeFactRequirement{checked::CheckedFactGroup::Call,
                                                      ast::NodeId(1), checkedNode(0)});
    nodeRequirements.add(checked::NodeFactRequirement{checked::CheckedFactGroup::Call,
                                                      ast::NodeId(2), checkedNode(1)});
    zc::Vector<checked::DefinitionFactRequirement> definitionRequirements;
    zc::Vector<checked::CaptureFactRequirement> captureRequirements;
    zc::Vector<identity::DefId> importedDefinitions;
    zc::Vector<identity::ImplId> coherentImpls;
    zc::Vector<checked::CheckerFailureRef> registeredFailures;
    const auto options = identity::SemanticCompilerOptionsKey::from(2026, true, false, true);

    zc::Maybe<checked::VerifiedCheckedFacts> verified;
    ZC_IF_SOME(substitutionStore, substitutions) {
      ZC_IF_SOME(witnessStore, witnesses) {
        checked::CheckedFactsCandidate candidate{context,
                                                 contextFingerprint->clone(),
                                                 moduleId,
                                                 registries->sourceSnapshots()[0].contentDigest(),
                                                 *parsed,
                                                 *signatureRevision,
                                                 *importedRevision,
                                                 *coherenceRevision,
                                                 options,
                                                 zc::mv(substitutionStore),
                                                 zc::mv(witnessStore),
                                                 emptyMap<checked::NodeTypeMap>(),
                                                 emptyMap<checked::DefinitionTypeMap>(),
                                                 emptyMap<checked::LiteralFactMap>(),
                                                 emptyMap<checked::ConstantFactMap>(),
                                                 emptyMap<checked::AggregateFactMap>(),
                                                 emptyMap<checked::PlaceFactMap>(),
                                                 emptyMap<checked::CoercionFactMap>(),
                                                 emptyMap<checked::CastFactMap>(),
                                                 checked::CallFactMap::fromEntries(zc::mv(calls)),
                                                 emptyMap<checked::CompoundAssignmentFactMap>(),
                                                 emptyMap<checked::MemberFactMap>(),
                                                 emptyMap<checked::IndexFactMap>(),
                                                 emptyMap<checked::PatternFactMap>(),
                                                 emptyMap<checked::ObservedOperationFactMap>(),
                                                 emptyMap<checked::CaptureFactMap>(),
                                                 emptyMap<checked::MarkerObligationFactMap>(),
                                                 emptyMap<checked::ExhaustivenessFactMap>(),
                                                 emptyMap<checked::UnsafeOperationFactMap>(),
                                                 emptyMap<checked::ProjectionFactMap>(),
                                                 emptyMap<checked::ObligationFactMap>(),
                                                 emptyMap<checked::ErrorUnionShapeFactMap>(),
                                                 emptyMap<checked::ErrorOperatorFactMap>(),
                                                 zc::Vector<checked::FrozenRecoveryLedger>(),
                                                 zc::Vector<checked::CheckerFailureRef>(),
                                                 zc::Vector<checked::CheckerAdvisoryRef>()};
        const checked::CheckedFactsVerificationInput verificationInput{
            context,
            *contextFingerprint,
            moduleId,
            registries->sourceSnapshots()[0].source(),
            registries->sourceSnapshots()[0].contentDigest(),
            *parsed,
            *signatureRevision,
            *importedRevision,
            *coherenceRevision,
            options,
            nodeRequirements.asPtr(),
            definitionRequirements.asPtr(),
            captureRequirements.asPtr(),
            importedDefinitions.asPtr(),
            coherentImpls.asPtr(),
            registeredFailures.asPtr(),
            {},
            {},
            *registries,
            *semanticTypes};
        ZC_REQUIRE(checked::CheckedFactsCanonicalCodec::writeCanonicalRecords(candidate,
                                                                              verificationInput));
        auto result = checked::CheckedFactsVerifier::verify(zc::mv(candidate), verificationInput);
        ZC_REQUIRE(result.is<checked::VerifiedCheckedFacts>());
        verified = zc::mv(result.get<checked::VerifiedCheckedFacts>());
      }
    }
    ZC_IF_SOME(value, verified) { return zc::mv(value); }
    ZC_FAIL_REQUIRE("checked dispatch evidence publication failed");
  }

  DispatchFact fact(PrimitiveOperation operation,
                    zc::Maybe<checked::CanonicalSubstitutionId>&& substitution = zc::none) const {
    zc::Maybe<DispatchReceiverPlan> noReceiver;
    zc::Vector<DispatchArgumentPlan> noArguments;
    zc::Maybe<checked::WitnessArgumentsId> noWitnesses;
    zc::Maybe<identity::SemanticTypeId> noRaises;
    return DispatchFact{DispatchTarget(PrimitiveTarget{operation}),
                        DispatchResultTransform(IdentityResultTransform{}),
                        zc::mv(noReceiver),
                        zc::mv(noArguments),
                        i32,
                        i32,
                        zc::mv(substitution),
                        zc::mv(noWitnesses),
                        zc::mv(noRaises),
                        checkedNode(0).sourceSpan};
  }

  DispatchFactCandidateEntry entry(
      ast::NodeId node, uint32_t preorder, PrimitiveOperation operation,
      zc::Maybe<checked::CanonicalSubstitutionId>&& substitution = zc::none) const {
    auto value = fact(operation, zc::mv(substitution));
    auto key = checkedNode(preorder);
    auto encoded =
        DispatchFactCanonicalCodec::encode(key, value, *registries, *semanticTypes, firstFacts());
    ZC_REQUIRE(encoded != zc::none);
    ZC_IF_SOME(record, encoded) {
      return DispatchFactCandidateEntry{node, zc::mv(key), zc::none, zc::mv(value), zc::mv(record)};
    }
    ZC_UNREACHABLE
  }

  DispatchFactCandidateEntry foreignStoreEntry(
      checked::CanonicalSubstitutionId substitution) const {
    auto value = fact(PrimitiveOperation::Add, substitution);
    return DispatchFactCandidateEntry{ast::NodeId(1), checkedNode(0), zc::none, zc::mv(value),
                                      zc::heapArray<uint8_t>(1, uint8_t{0xff})};
  }
};

DispatchInvariantKind rejectionKind(const DispatchVerificationResult& result) {
  ZC_REQUIRE(result.is<DispatchFactsInvariantRejected>());
  const auto& rejected = result.get<DispatchFactsInvariantRejected>();
  ZC_REQUIRE(rejected.failures.size() == 1);
  const auto& failure = rejected.failures[0].variant();
  ZC_REQUIRE(failure.is<DispatchInvariantFact>());
  return failure.get<DispatchInvariantFact>().kind;
}

}  // namespace

ZC_TEST("DispatchFactsRevision.ReproducesIndependentNonEmptyOracle") {
  const uint8_t moduleBytes[] = {0xa1};
  const uint8_t recordBytes[] = {0xb3};
  const zc::ArrayPtr<const uint8_t> records[] = {recordBytes};
  auto revision =
      DispatchFactsRevision::computeFramedDigest(digest(0x00), moduleBytes, digest(0x22), records);
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "25ca384dcdb9cd5225d8ec8abfb25c68865c2c6ca4518c8f30ffdc359a51835c"_zc);
  }
}

ZC_TEST("DispatchFactsAlgebra.UsesNormativeClosedTags") {
  ZC_EXPECT(static_cast<uint8_t>(DispatchReceiverRole::IndexBase) == 0x05);
  ZC_EXPECT(static_cast<uint8_t>(OrderingRelation::GreaterEqual) == 0x04);
  ZC_EXPECT(static_cast<uint8_t>(DispatchInvariantKind::CanonicalCodecMismatch) == 0x05);
  ZC_EXPECT(static_cast<uint8_t>(DispatchInvariantStage::Encoding) == 0x04);
}

ZC_TEST("DispatchFactsVerifier.PublishesNodeIdFreeFactsAndNormalizesPermutations") {
  DispatchFactsFixture fixture;
  auto ordered = DispatchFactsVerifier::verify(
      fixture.candidate(), fixture.input(*fixture.firstLease, fixture.firstFacts()));
  auto reversed = DispatchFactsVerifier::verify(
      fixture.candidate(DispatchFactsFixture::CandidateShape::Reversed),
      fixture.input(*fixture.firstLease, fixture.firstFacts()));
  ZC_REQUIRE(ordered.is<VerifiedDispatchFacts>());
  ZC_REQUIRE(reversed.is<VerifiedDispatchFacts>());
  const auto& left = ordered.get<VerifiedDispatchFacts>();
  const auto& right = reversed.get<VerifiedDispatchFacts>();
  ZC_EXPECT(left.revision().digest() == right.revision().digest());
  ZC_EXPECT(left.facts().size() == 2);
  ZC_EXPECT(left.facts()[0].checkedNode.schemaPreorder == 0);
  ZC_EXPECT(left.facts()[1].checkedNode.schemaPreorder == 1);
}

ZC_TEST("DispatchFactsVerifier.RejectsMissingAndDuplicateFactsExactly") {
  DispatchFactsFixture fixture;
  auto missing = DispatchFactsVerifier::verify(
      fixture.candidate(DispatchFactsFixture::CandidateShape::MissingSecond),
      fixture.input(*fixture.firstLease, fixture.firstFacts()));
  ZC_EXPECT(rejectionKind(missing) == DispatchInvariantKind::MissingFact);

  auto duplicate = DispatchFactsVerifier::verify(
      fixture.candidate(DispatchFactsFixture::CandidateShape::DuplicateFirst),
      fixture.input(*fixture.firstLease, fixture.firstFacts()));
  ZC_EXPECT(rejectionKind(duplicate) == DispatchInvariantKind::AdditionalFact);
}

ZC_TEST("DispatchFactsVerifier.RejectsStaleCheckedRevisionAndLease") {
  DispatchFactsFixture fixture;
  auto staleRevision =
      DispatchFactsVerifier::verify(fixture.candidate(DispatchFactsFixture::CandidateShape::Ordered,
                                                      fixture.secondFacts().revision()),
                                    fixture.input(*fixture.firstLease, fixture.firstFacts()));
  ZC_EXPECT(rejectionKind(staleRevision) == DispatchInvariantKind::InputMismatch);

  auto staleLease = DispatchFactsVerifier::verify(
      fixture.candidate(), fixture.input(*fixture.secondLease, fixture.firstFacts()));
  ZC_EXPECT(rejectionKind(staleLease) == DispatchInvariantKind::InputMismatch);
}

ZC_TEST("DispatchFactsVerifier.RejectsForeignSemanticContext") {
  DispatchFactsFixture fixture;
  auto foreign =
      DispatchFactsVerifier::verify(fixture.candidate(DispatchFactsFixture::CandidateShape::Ordered,
                                                      zc::none, fixture.foreignContext),
                                    fixture.input(*fixture.firstLease, fixture.firstFacts()));
  ZC_EXPECT(rejectionKind(foreign) == DispatchInvariantKind::InputMismatch);
}

ZC_TEST("DispatchFactsVerifier.RejectsForeignCheckedStoreHandle") {
  DispatchFactsFixture fixture;
  auto foreign = DispatchFactsVerifier::verify(
      fixture.candidate(DispatchFactsFixture::CandidateShape::ForeignStore),
      fixture.input(*fixture.firstLease, fixture.firstFacts()));
  ZC_EXPECT(rejectionKind(foreign) == DispatchInvariantKind::InputMismatch);
}

ZC_TEST("DispatchFactsVerifier.RejectsSyntaxOperationMismatch") {
  DispatchFactsFixture fixture;
  fixture.requirements[0].operation = PrimitiveOperation::Sub;
  auto mismatched = DispatchFactsVerifier::verify(
      fixture.candidate(), fixture.input(*fixture.firstLease, fixture.firstFacts()));
  ZC_EXPECT(rejectionKind(mismatched) == DispatchInvariantKind::InvalidFact);
}

}  // namespace zomlang::compiler::checker::dispatch
