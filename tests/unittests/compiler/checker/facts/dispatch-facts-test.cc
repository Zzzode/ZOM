// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/checker/facts/dispatch-facts.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "compiler/type/semantic-type-data.h"
#include "tests/unittests/compiler/checker/checker-authority-test-fixture.h"
#include "tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::checker::dispatch {
namespace {

using namespace tests::test_identity_detail;
using namespace tests::checker_fixture;

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
    ForeignStore,
    AdditionalThird,
    TamperedCanonical
  };

  DispatchFactsFixture()
      : session(
            "interface Behavior { fun act(); }\n"
            "class RecoveryOwner { fun act() {} }\n"
            "impl Behavior for RecoveryOwner {}\n"_zc),
        factStoreBrands(session.brands()),
        semanticTypes(session.semanticTypes()) {
    context = session.semanticContext();
    moduleId = session.module();
    auto foreign = foreignFactory.issue();
    ZC_REQUIRE(foreign != zc::none);
    ZC_IF_SOME(value, foreign) { foreignContext = value; }
    auto canonical = semanticTypes.canonicalizeClosed(type::semantic::TypeData(
        type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::I32}));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto interned =
        semanticTypes.intern(zc::mv(canonical.get<type::semantic::CanonicalTypeData>()));
    ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
    i32 = interned.get<type::SemanticTypeInterned>().id;

    contextFingerprint = zc::heap<identity::ContextFingerprint>(
        session.identityAuthority().fingerprint().clone());
    const auto moduleBytes = moduleKey().encode();
    const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
    auto signature = signature::SignatureFactsRevision::computeFramed(
        contextFingerprint->digest(), moduleBytes.asPtr(),
        boundModule().parsedModule().contentDigest(), digest(0x21), digest(0x22), emptyRecords,
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
                                          session.source(),
                                          requirements.asPtr(),
                                          projections.asPtr(),
                                          lease,
                                          facts,
                                          session.identityAuthority(),
                                          semanticTypes};
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
    } else if (shape == CandidateShape::AdditionalThird) {
      entries.add(entry(ast::NodeId(1), 0, PrimitiveOperation::Add));
      entries.add(entry(ast::NodeId(2), 1, PrimitiveOperation::Sub));
      entries.add(entry(ast::NodeId(3), 2, PrimitiveOperation::Mul));
    } else if (shape == CandidateShape::TamperedCanonical) {
      auto first = entry(ast::NodeId(1), 0, PrimitiveOperation::Add);
      first.canonicalRecord[0] ^= 0xff;
      entries.add(zc::mv(first));
      entries.add(entry(ast::NodeId(2), 1, PrimitiveOperation::Sub));
    }
    ZC_IF_SOME(revision, selectedRevision) {
      return DispatchFactsCandidate(ownedContext, contextFingerprint->clone(), moduleId, revision,
                                    zc::mv(entries));
    }
    ZC_UNREACHABLE
  }

  zc::Maybe<zc::Array<uint8_t>> encodeStandalone(DispatchTarget&& target,
                                                 DispatchResultTransform&& transform) const {
    zc::Maybe<DispatchReceiverPlan> noReceiver;
    zc::Vector<DispatchArgumentPlan> noArguments;
    zc::Maybe<checked::CanonicalSubstitutionId> noSubstitutions;
    zc::Maybe<checked::WitnessArgumentsId> noWitnesses;
    zc::Maybe<identity::SemanticTypeId> noRaises;
    DispatchFact value{zc::mv(target),
                       zc::mv(transform),
                       zc::mv(noReceiver),
                       zc::mv(noArguments),
                       i32,
                       i32,
                       zc::mv(noSubstitutions),
                       zc::mv(noWitnesses),
                       zc::mv(noRaises),
                       checkedNode(0).sourceSpan};
    return DispatchFactCanonicalCodec::encode(checkedNode(0), value, session.identityAuthority(),
                                              semanticTypes, firstFacts());
  }

  zc::Maybe<zc::Array<uint8_t>> encodeWithMismatchedSpan() const {
    zc::Maybe<DispatchReceiverPlan> noReceiver;
    zc::Vector<DispatchArgumentPlan> noArguments;
    zc::Maybe<checked::CanonicalSubstitutionId> noSubstitutions;
    zc::Maybe<checked::WitnessArgumentsId> noWitnesses;
    zc::Maybe<identity::SemanticTypeId> noRaises;
    DispatchFact value{DispatchTarget(PrimitiveTarget{PrimitiveOperation::Add}),
                       DispatchResultTransform(IdentityResultTransform{}),
                       zc::mv(noReceiver),
                       zc::mv(noArguments),
                       i32,
                       i32,
                       zc::mv(noSubstitutions),
                       zc::mv(noWitnesses),
                       zc::mv(noRaises),
                       session.span(1, 2)};
    return DispatchFactCanonicalCodec::encode(checkedNode(0), value, session.identityAuthority(),
                                              semanticTypes, firstFacts());
  }

  identity::ImplId implementation() const {
    const auto implementations = boundModule().definitions().identities().implementations();
    ZC_REQUIRE(implementations.size() == 1);
    return implementations[0].handle();
  }

  DispatchFactsCandidate completeCandidate() const {
    auto substitution = firstFacts().substitutionStore().idAt(0);
    auto witnesses = firstFacts().witnessStore().idAt(0);
    ZC_REQUIRE(substitution != zc::none);
    ZC_REQUIRE(witnesses != zc::none);
    ZC_IF_SOME(substitutionId, substitution) {
      ZC_IF_SOME(witnessId, witnesses) {
        zc::Vector<DispatchFactCandidateEntry> entries;
        entries.add(entry(ast::NodeId(1), 0, PrimitiveOperation::Add));
        entries.add(entry(ast::NodeId(2), 1, PrimitiveOperation::Sub));
        auto value = completeFact(substitutionId, witnessId);
        auto key = checkedNode(2);
        auto encoded = DispatchFactCanonicalCodec::encode(key, value, session.identityAuthority(),
                                                          semanticTypes, firstFacts());
        ZC_REQUIRE(encoded != zc::none);
        ZC_IF_SOME(record, encoded) {
          entries.add(DispatchFactCandidateEntry{ast::NodeId(3), zc::mv(key), zc::none,
                                                 zc::mv(value), zc::mv(record)});
        }
        return DispatchFactsCandidate(context, contextFingerprint->clone(), moduleId,
                                      firstFacts().revision(), zc::mv(entries));
      }
    }
    ZC_FAIL_REQUIRE("dispatch fixture stores did not issue canonical identifiers");
  }

  DispatchFactsCandidate candidateWithFirstTarget(DispatchTarget&& target) const {
    auto key = checkedNode(0);
    zc::Maybe<DispatchReceiverPlan> noReceiver;
    zc::Vector<DispatchArgumentPlan> noArguments;
    zc::Maybe<checked::CanonicalSubstitutionId> noSubstitutions;
    zc::Maybe<checked::WitnessArgumentsId> noWitnesses;
    zc::Maybe<identity::SemanticTypeId> noRaises;
    DispatchFact first{zc::mv(target),
                       DispatchResultTransform(IdentityResultTransform{}),
                       zc::mv(noReceiver),
                       zc::mv(noArguments),
                       i32,
                       i32,
                       zc::mv(noSubstitutions),
                       zc::mv(noWitnesses),
                       zc::mv(noRaises),
                       key.sourceSpan.clone()};
    auto encoded = DispatchFactCanonicalCodec::encode(key, first, session.identityAuthority(),
                                                      semanticTypes, firstFacts());
    ZC_REQUIRE(encoded != zc::none);
    ZC_IF_SOME(record, encoded) {
      zc::Vector<DispatchFactCandidateEntry> entries;
      entries.add(DispatchFactCandidateEntry{ast::NodeId(1), zc::mv(key), zc::none, zc::mv(first),
                                             zc::mv(record)});
      entries.add(entry(ast::NodeId(2), 1, PrimitiveOperation::Sub));
      return DispatchFactsCandidate(context, contextFingerprint->clone(), moduleId,
                                    firstFacts().revision(), zc::mv(entries));
    }
    ZC_UNREACHABLE
  }

  CheckerAuthoritySession session;
  identity::SemanticContextFactory foreignFactory;
  const identity::RegistryBrandIssuer& factStoreBrands;
  type::SemanticTypeStore& semanticTypes;
  identity::SemanticContextBrand context;
  identity::SemanticContextBrand foreignContext;
  identity::ModuleId moduleId;
  identity::SemanticTypeId i32;
  zc::Own<identity::ContextFingerprint> contextFingerprint;
  zc::Own<signature::SignatureFactsRevision> signatureRevision;
  zc::Own<cross_module::ImportedSignatureViewRevision> importedRevision;
  zc::Own<cross_module::CoherenceViewRevision> coherenceRevision;
  zc::Own<checked::CheckedFactsRepository> repository;
  zc::Own<checked::CheckedEvidenceLease> firstLease;
  zc::Own<checked::CheckedEvidenceLease> secondLease;
  zc::Vector<DispatchNodeProjection> projections;
  zc::Vector<DispatchSiteRequirement> requirements;

private:
  const driver::module_graph_query::CheckerBoundModuleView& boundModule() const {
    auto view = session.identityAuthority().boundModule(moduleId);
    ZC_REQUIRE(view != zc::none);
    ZC_IF_SOME(value, view) { return value; }
    ZC_UNREACHABLE
  }

  const identity::ModuleKey& moduleKey() const {
    auto module = session.identityAuthority().module(moduleId);
    ZC_REQUIRE(module != zc::none);
    ZC_IF_SOME(entry, module) { return entry.key(); }
    ZC_UNREACHABLE
  }

  checked::CheckedNodeKey checkedNode(uint32_t preorder) const {
    return checked::CheckedNodeKey{0x31, preorder, session.span(0, 1)};
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

  checked::CoercionAdjustment completeCoercion(checked::WitnessArgumentsId witnesses) const {
    zc::Vector<identity::SemanticTypeId> interfaceArguments;
    interfaceArguments.add(i32);
    zc::Vector<identity::DefId> upcastPath;
    upcastPath.add(session.owner());
    zc::Vector<checked::CoercionStep> steps;
    steps.add(checked::CoercionStep(checked::NeverToStep{}));
    steps.add(checked::CoercionStep(checked::ToAnyStep{}));
    steps.add(checked::CoercionStep(checked::ReborrowSharedStep{}));
    steps.add(checked::CoercionStep(checked::ReferenceToRawConstStep{}));
    steps.add(checked::CoercionStep(checked::ReferenceToRawMutableStep{}));
    steps.add(checked::CoercionStep(checked::RawMutToConstStep{}));
    steps.add(checked::CoercionStep(checked::UnionInjectStep{7, i32}));
    steps.add(checked::CoercionStep(checked::DynEraseStep{
        checked::InterfaceInstantiation{session.owner(), zc::mv(interfaceArguments)},
        implementation(), witnesses}));
    steps.add(checked::CoercionStep(checked::DynUpcastStep{zc::mv(upcastPath)}));
    return checked::CoercionAdjustment{checked::CoercionSite::Argument, i32, i32, zc::mv(steps),
                                       session.span(0, 1)};
  }

  checked::ReceiverAdjustment completeReceiverAdjustment() const {
    zc::Vector<checked::ReceiverAdjustmentStep> steps;
    steps.add(checked::ReceiverAdjustmentStep::DereferenceShared);
    steps.add(checked::ReceiverAdjustmentStep::DereferenceMutable);
    steps.add(checked::ReceiverAdjustmentStep::BorrowShared);
    steps.add(checked::ReceiverAdjustmentStep::ReborrowShared);
    steps.add(checked::ReceiverAdjustmentStep::BorrowMutable);
    steps.add(checked::ReceiverAdjustmentStep::ReborrowMutable);
    steps.add(checked::ReceiverAdjustmentStep::MoveValue);
    steps.add(checked::ReceiverAdjustmentStep::CopyValue);
    return checked::ReceiverAdjustment{i32, i32, zc::mv(steps), session.span(0, 1)};
  }

  DispatchFact completeFact(checked::CanonicalSubstitutionId substitution,
                            checked::WitnessArgumentsId witnesses) const {
    zc::Maybe<checked::CoercionAdjustment> noReceiverCoercion;
    DispatchArgumentPlan receiverArgument{checkedNode(3), i32, i32, zc::mv(noReceiverCoercion)};
    zc::Maybe<DispatchReceiverPlan> receiver;
    receiver = DispatchReceiverPlan{DispatchReceiverRole::OperatorLeftHandSide,
                                    checked::ReceiverMode::Shared, zc::mv(receiverArgument),
                                    completeReceiverAdjustment()};
    zc::Maybe<checked::CoercionAdjustment> argumentCoercion = completeCoercion(witnesses);
    zc::Vector<DispatchArgumentPlan> arguments;
    arguments.add(DispatchArgumentPlan{checkedNode(4), i32, i32, zc::mv(argumentCoercion)});
    zc::Maybe<checked::CanonicalSubstitutionId> selectedSubstitution = substitution;
    zc::Maybe<checked::WitnessArgumentsId> selectedWitnesses = witnesses;
    zc::Maybe<identity::SemanticTypeId> raises = i32;
    return DispatchFact{DispatchTarget(PrimitiveTarget{PrimitiveOperation::Add}),
                        DispatchResultTransform(IdentityResultTransform{}),
                        zc::mv(receiver),
                        zc::mv(arguments),
                        i32,
                        i32,
                        zc::mv(selectedSubstitution),
                        zc::mv(selectedWitnesses),
                        zc::mv(raises),
                        session.span(0, 1)};
  }

  checked::VerifiedCheckedFacts buildCheckedFacts(uint8_t canonicalBase) {
    auto substitutionBrand = factStoreBrands.issue();
    auto witnessBrand = factStoreBrands.issue();
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
                                                 boundModule().parsedModule().contentDigest(),
                                                 boundModule().parsedModule().receipt(),
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
            session.source(),
            boundModule().parsedModule().contentDigest(),
            boundModule().parsedModule().receipt(),
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
            session.identityAuthority(),
            semanticTypes};
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
    auto encoded = DispatchFactCanonicalCodec::encode(key, value, session.identityAuthority(),
                                                      semanticTypes, firstFacts());
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
              "b50df7fb9f580a517707e385c6a90d91860031cd018bf9a2057a8a5be3f038bf"_zc);
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

ZC_TEST("DispatchFactsVerifier.RejectsInventoryAndCanonicalMutations") {
  {
    DispatchFactsFixture fixture;
    fixture.projections.add(DispatchNodeProjection{
        fixture.projections[0].sourceNode,
        checked::CheckedNodeKey{fixture.projections[0].checkedNode.syntaxKind,
                                fixture.projections[0].checkedNode.schemaPreorder,
                                fixture.projections[0].checkedNode.sourceSpan.clone()}});
    auto duplicated = DispatchFactsVerifier::verify(
        fixture.candidate(), fixture.input(*fixture.firstLease, fixture.firstFacts()));
    ZC_EXPECT(rejectionKind(duplicated) == DispatchInvariantKind::InputMismatch);
  }

  {
    DispatchFactsFixture fixture;
    fixture.requirements.add(DispatchSiteRequirement{
        fixture.requirements[0].sourceNode,
        checked::CheckedNodeKey{fixture.requirements[0].checkedNode.syntaxKind,
                                fixture.requirements[0].checkedNode.schemaPreorder,
                                fixture.requirements[0].checkedNode.sourceSpan.clone()},
        zc::none, fixture.requirements[0].siteKind, fixture.requirements[0].receiverRole,
        fixture.requirements[0].operation, fixture.requirements[0].compoundOperation});
    auto duplicated = DispatchFactsVerifier::verify(
        fixture.candidate(), fixture.input(*fixture.firstLease, fixture.firstFacts()));
    ZC_EXPECT(rejectionKind(duplicated) == DispatchInvariantKind::AdditionalFact);
  }

  {
    DispatchFactsFixture fixture;
    auto additional = DispatchFactsVerifier::verify(
        fixture.candidate(DispatchFactsFixture::CandidateShape::AdditionalThird),
        fixture.input(*fixture.firstLease, fixture.firstFacts()));
    ZC_EXPECT(rejectionKind(additional) == DispatchInvariantKind::AdditionalFact);
  }

  {
    DispatchFactsFixture fixture;
    auto tampered = DispatchFactsVerifier::verify(
        fixture.candidate(DispatchFactsFixture::CandidateShape::TamperedCanonical),
        fixture.input(*fixture.firstLease, fixture.firstFacts()));
    ZC_EXPECT(rejectionKind(tampered) == DispatchInvariantKind::CanonicalCodecMismatch);
  }
}

ZC_TEST("DispatchFactCanonicalCodec.EncodesDirectAndConcreteMethodTargets") {
  DispatchFactsFixture fixture;
  auto direct = fixture.encodeStandalone(DispatchTarget(DirectTarget{fixture.session.owner()}),
                                         DispatchResultTransform(BooleanNotResultTransform{}));
  auto concrete = fixture.encodeStandalone(
      DispatchTarget(ConcreteMethodTarget{fixture.session.owner()}),
      DispatchResultTransform(CompareOrderingResultTransform{OrderingRelation::GreaterEqual}));
  ZC_REQUIRE(direct != zc::none);
  ZC_REQUIRE(concrete != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(direct).asPtr() != ZC_REQUIRE_NONNULL(concrete).asPtr());
}

ZC_TEST("DispatchFactCanonicalCodec.EncodesImplementationAndDynamicTargets") {
  DispatchFactsFixture fixture;
  auto implementation = fixture.implementation();
  auto implMethod = fixture.encodeStandalone(
      DispatchTarget(ImplMethodTarget{implementation, fixture.session.owner()}),
      DispatchResultTransform(IdentityResultTransform{}));
  auto witnessMethod = fixture.encodeStandalone(
      DispatchTarget(WitnessMethodTarget{fixture.session.owner(), fixture.session.owner(),
                                         fixture.session.owner()}),
      DispatchResultTransform(IdentityResultTransform{}));
  auto dynMethod = fixture.encodeStandalone(
      DispatchTarget(DynMethodTarget{fixture.session.owner(), fixture.session.owner()}),
      DispatchResultTransform(IdentityResultTransform{}));
  ZC_REQUIRE(implMethod != zc::none);
  ZC_REQUIRE(witnessMethod != zc::none);
  ZC_REQUIRE(dynMethod != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(implMethod).asPtr() != ZC_REQUIRE_NONNULL(witnessMethod).asPtr());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(witnessMethod).asPtr() != ZC_REQUIRE_NONNULL(dynMethod).asPtr());
}

ZC_TEST("DispatchFactCanonicalCodec.RejectsInvalidTagsAndMismatchedSpan") {
  DispatchFactsFixture fixture;
  auto invalidTarget = fixture.encodeStandalone(
      DispatchTarget(PrimitiveTarget{static_cast<PrimitiveOperation>(0xff)}),
      DispatchResultTransform(IdentityResultTransform{}));
  auto invalidTransform = fixture.encodeStandalone(
      DispatchTarget(PrimitiveTarget{PrimitiveOperation::Add}),
      DispatchResultTransform(CompareOrderingResultTransform{static_cast<OrderingRelation>(0xff)}));
  auto mismatchedSpan = fixture.encodeWithMismatchedSpan();
  ZC_EXPECT(invalidTarget == zc::none);
  ZC_EXPECT(invalidTransform == zc::none);
  ZC_EXPECT(mismatchedSpan == zc::none);
}

ZC_TEST("DispatchFactCanonicalCodec.EncodesCompleteReceiverAndArgumentPlans") {
  DispatchFactsFixture fixture;
  auto substitution = fixture.firstFacts().substitutionStore().idAt(0);
  auto witnesses = fixture.firstFacts().witnessStore().idAt(0);
  ZC_REQUIRE(substitution != zc::none);
  ZC_REQUIRE(witnesses != zc::none);

  ZC_IF_SOME(substitutionId, substitution) {
    ZC_IF_SOME(witnessId, witnesses) {
      zc::Vector<identity::SemanticTypeId> interfaceArguments;
      interfaceArguments.add(fixture.i32);
      zc::Vector<identity::DefId> upcastPath;
      upcastPath.add(fixture.session.owner());
      zc::Vector<checked::CoercionStep> coercionSteps;
      coercionSteps.add(checked::CoercionStep(checked::NeverToStep{}));
      coercionSteps.add(checked::CoercionStep(checked::ToAnyStep{}));
      coercionSteps.add(checked::CoercionStep(checked::ReborrowSharedStep{}));
      coercionSteps.add(checked::CoercionStep(checked::ReferenceToRawConstStep{}));
      coercionSteps.add(checked::CoercionStep(checked::ReferenceToRawMutableStep{}));
      coercionSteps.add(checked::CoercionStep(checked::RawMutToConstStep{}));
      coercionSteps.add(checked::CoercionStep(checked::UnionInjectStep{7, fixture.i32}));
      coercionSteps.add(checked::CoercionStep(checked::DynEraseStep{
          checked::InterfaceInstantiation{fixture.session.owner(), zc::mv(interfaceArguments)},
          fixture.implementation(), witnessId}));
      coercionSteps.add(checked::CoercionStep(checked::DynUpcastStep{zc::mv(upcastPath)}));

      zc::Maybe<checked::CoercionAdjustment> argumentCoercion;
      argumentCoercion =
          checked::CoercionAdjustment{checked::CoercionSite::Argument, fixture.i32, fixture.i32,
                                      zc::mv(coercionSteps), fixture.session.span(0, 1)};
      DispatchArgumentPlan argument{checked::CheckedNodeKey{0x31, 2, fixture.session.span(0, 1)},
                                    fixture.i32, fixture.i32, zc::mv(argumentCoercion)};

      zc::Maybe<checked::CoercionAdjustment> noReceiverCoercion;
      DispatchArgumentPlan receiverArgument{
          checked::CheckedNodeKey{0x31, 3, fixture.session.span(0, 1)}, fixture.i32, fixture.i32,
          zc::mv(noReceiverCoercion)};
      zc::Vector<checked::ReceiverAdjustmentStep> receiverSteps;
      receiverSteps.add(checked::ReceiverAdjustmentStep::DereferenceShared);
      receiverSteps.add(checked::ReceiverAdjustmentStep::DereferenceMutable);
      receiverSteps.add(checked::ReceiverAdjustmentStep::BorrowShared);
      receiverSteps.add(checked::ReceiverAdjustmentStep::ReborrowShared);
      receiverSteps.add(checked::ReceiverAdjustmentStep::BorrowMutable);
      receiverSteps.add(checked::ReceiverAdjustmentStep::ReborrowMutable);
      receiverSteps.add(checked::ReceiverAdjustmentStep::MoveValue);
      receiverSteps.add(checked::ReceiverAdjustmentStep::CopyValue);
      zc::Maybe<DispatchReceiverPlan> receiver;
      receiver = DispatchReceiverPlan{
          DispatchReceiverRole::ImplicitSelf, checked::ReceiverMode::Shared,
          zc::mv(receiverArgument),
          checked::ReceiverAdjustment{fixture.i32, fixture.i32, zc::mv(receiverSteps),
                                      fixture.session.span(0, 1)}};

      zc::Vector<DispatchArgumentPlan> arguments;
      arguments.add(zc::mv(argument));
      zc::Maybe<checked::CanonicalSubstitutionId> selectedSubstitution = substitutionId;
      zc::Maybe<checked::WitnessArgumentsId> selectedWitnesses = witnessId;
      zc::Maybe<identity::SemanticTypeId> raises = fixture.i32;
      DispatchFact fact{DispatchTarget(PrimitiveTarget{PrimitiveOperation::Add}),
                        DispatchResultTransform(IdentityResultTransform{}),
                        zc::mv(receiver),
                        zc::mv(arguments),
                        fixture.i32,
                        fixture.i32,
                        zc::mv(selectedSubstitution),
                        zc::mv(selectedWitnesses),
                        zc::mv(raises),
                        fixture.session.span(0, 1)};
      auto encoded = DispatchFactCanonicalCodec::encode(
          checked::CheckedNodeKey{0x31, 0, fixture.session.span(0, 1)}, fact,
          fixture.session.identityAuthority(), fixture.semanticTypes, fixture.firstFacts());
      ZC_EXPECT(encoded != zc::none);
      return;
    }
  }
  ZC_FAIL_REQUIRE("dispatch fixture stores did not issue canonical identifiers");
}

ZC_TEST("DispatchFactsVerifier.ValidatesCompletePlansBeforeRejectingAdditionalFact") {
  DispatchFactsFixture fixture;
  auto result = DispatchFactsVerifier::verify(
      fixture.completeCandidate(), fixture.input(*fixture.firstLease, fixture.firstFacts()));
  ZC_EXPECT(rejectionKind(result) == DispatchInvariantKind::AdditionalFact);
}

ZC_TEST("DispatchFactsVerifier.RejectsNonPrimitiveTargetAgainstPrimitiveEnvelope") {
  DispatchFactsFixture fixture;
  const auto input = fixture.input(*fixture.firstLease, fixture.firstFacts());
  auto direct = DispatchFactsVerifier::verify(
      fixture.candidateWithFirstTarget(DispatchTarget(DirectTarget{fixture.session.owner()})),
      input);
  auto concrete = DispatchFactsVerifier::verify(fixture.candidateWithFirstTarget(DispatchTarget(
                                                    ConcreteMethodTarget{fixture.session.owner()})),
                                                input);
  auto implementation = DispatchFactsVerifier::verify(
      fixture.candidateWithFirstTarget(
          DispatchTarget(ImplMethodTarget{fixture.implementation(), fixture.session.owner()})),
      input);
  auto witness = DispatchFactsVerifier::verify(
      fixture.candidateWithFirstTarget(DispatchTarget(WitnessMethodTarget{
          fixture.session.owner(), fixture.session.owner(), fixture.session.owner()})),
      input);
  auto dynamic = DispatchFactsVerifier::verify(
      fixture.candidateWithFirstTarget(
          DispatchTarget(DynMethodTarget{fixture.session.owner(), fixture.session.owner()})),
      input);
  ZC_EXPECT(rejectionKind(direct) == DispatchInvariantKind::InvalidFact);
  ZC_EXPECT(rejectionKind(concrete) == DispatchInvariantKind::InvalidFact);
  ZC_EXPECT(rejectionKind(implementation) == DispatchInvariantKind::InvalidFact);
  ZC_EXPECT(rejectionKind(witness) == DispatchInvariantKind::InvalidFact);
  ZC_EXPECT(rejectionKind(dynamic) == DispatchInvariantKind::InvalidFact);
}

ZC_TEST("DispatchSiteInventoryBuilder.ProjectsCallAndOperatorRequirements") {
  CheckerAuthoritySession session(
      "class RecoveryOwner {}\n"
      "class Holder { fun act() {} }\n"
      "fun helper() {}\n"
      "fun calculate() { helper(); let holder = Holder(); holder.act(); let value = 1 + 2; let negated = -value; let indexed = value[0]; let fallback = value ?? 4; value += 3; }\n"_zc);
  auto requirements = body::BodyFactRequirementInventoryBuilder::build(session.boundModule());
  ZC_REQUIRE(requirements.is<body::VerifiedBodyFactRequirementInventory>());
  auto inventory = DispatchSiteInventoryBuilder::build(
      session.boundModule(), requirements.get<body::VerifiedBodyFactRequirementInventory>());
  ZC_REQUIRE(inventory.is<VerifiedDispatchSiteInventory>());
  const auto& verified = inventory.get<VerifiedDispatchSiteInventory>();
  ZC_REQUIRE(verified.requirements().size() == 8);
  ZC_REQUIRE(verified.nodeProjections().size() >= 8);
  bool call = false;
  bool memberCall = false;
  bool binary = false;
  bool compound = false;
  bool index = false;
  bool nullCoalesce = false;
  bool unary = false;
  for (const auto& requirement : verified.requirements()) {
    if (requirement.siteKind == DispatchSiteKind::Call) {
      call = call || requirement.receiverRole == DispatchReceiverRole::ExplicitFirstArgument;
      memberCall = memberCall || requirement.receiverRole == DispatchReceiverRole::ImplicitSelf;
    } else if (requirement.siteKind == DispatchSiteKind::BinaryOperator) {
      binary = requirement.operation == PrimitiveOperation::Add &&
               requirement.receiverRole == DispatchReceiverRole::OperatorLeftHandSide;
    } else if (requirement.siteKind == DispatchSiteKind::CompoundAssignment) {
      compound = requirement.operation == PrimitiveOperation::Add &&
                 requirement.compoundOperation == CompoundAssignmentOperation::AddAssign &&
                 requirement.receiverRole == DispatchReceiverRole::OperatorLeftHandSide;
    } else if (requirement.siteKind == DispatchSiteKind::UnaryOperator) {
      unary = requirement.operation == PrimitiveOperation::Neg &&
              requirement.receiverRole == DispatchReceiverRole::OperatorOperand;
    } else if (requirement.siteKind == DispatchSiteKind::Index) {
      index = requirement.receiverRole == DispatchReceiverRole::IndexBase;
    } else if (requirement.siteKind == DispatchSiteKind::NullCoalescing) {
      nullCoalesce = requirement.operation == PrimitiveOperation::NullCoalesce &&
                     requirement.receiverRole == DispatchReceiverRole::OperatorLeftHandSide;
    }
  }
  ZC_EXPECT(call);
  ZC_EXPECT(memberCall);
  ZC_EXPECT(binary);
  ZC_EXPECT(compound);
  ZC_EXPECT(unary);
  ZC_EXPECT(index);
  ZC_EXPECT(nullCoalesce);
}

}  // namespace zomlang::compiler::checker::dispatch
