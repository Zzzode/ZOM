// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/checked-facts.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/type/semantic-type-data.h"
#include "zomlang/tests/unittests/compiler/checker/checker-authority-test-fixture.h"

namespace zomlang::compiler::checker::checked {
namespace {

using namespace tests::checker_fixture;

identity::Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) value = byte;
  ZC_IF_SOME(digest, identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes))) { return digest; }
  ZC_FAIL_REQUIRE("invalid checked-facts digest fixture");
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) return false;
  }
  return true;
}

struct SingleRecordGroup final {
  explicit SingleRecordGroup(uint8_t value) : bytes{value}, recordValues{zc::arrayPtr(bytes)} {}
  uint8_t bytes[1];
  zc::ArrayPtr<const uint8_t> recordValues[1];
  ZC_NODISCARD zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> records() const noexcept {
    return zc::arrayPtr(recordValues);
  }
};

template <typename Map>
Map emptyMap() {
  zc::Vector<typename Map::Entry> entries;
  return Map::fromEntries(zc::mv(entries));
}

class CheckedFactsCodecFixture final {
public:
  CheckedFactsCodecFixture()
      : session(
            "interface Behavior { fun act(); }\n"
            "class RecoveryOwner { fun act() {} }\n"
            "impl Behavior for RecoveryOwner {}\n"_zc),
        storeBrands(session.brands()),
        semanticTypes(session.semanticTypes()) {
    context = session.semanticContext();
    moduleId = session.module();
    definitionId = session.owner();
    auto canonical = semanticTypes.canonicalizeClosed(type::semantic::TypeData(
        type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::I32}));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto interned =
        semanticTypes.intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
    ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
    i32 = interned.get<type::SemanticTypeInterned>().id;
    zc::Vector<identity::SemanticTypeId> ownerArguments;
    auto ownerCanonical = semanticTypes.canonicalizeClosed(type::semantic::TypeData(
        type::semantic::NominalTypeData{definitionId, zc::mv(ownerArguments)}));
    ZC_REQUIRE(ownerCanonical.is<type::semantic::CanonicalTypeData>());
    auto ownerInterned =
        semanticTypes.intern(zc::mv(ownerCanonical).get<type::semantic::CanonicalTypeData>());
    ZC_REQUIRE(ownerInterned.is<type::SemanticTypeInterned>());
    ownerType = ownerInterned.get<type::SemanticTypeInterned>().id;

    const auto moduleBytes = moduleKey().encode();
    const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
    auto signature = signature::SignatureFactsRevision::computeFramed(
        session.identityAuthority().fingerprint().digest(), moduleBytes.asPtr(),
        boundModule().parsedModule().contentDigest(), repeatedDigest(0x21), repeatedDigest(0x22),
        emptyRecords, emptyRecords, emptyRecords);
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
    auto coherence = cross_module::CoherenceViewRevision::computeFramed(
        session.identityAuthority().fingerprint().digest(), repeatedDigest(0x23), emptyRecords,
        emptyRecords, emptyRecords);
    ZC_REQUIRE(coherence != zc::none);
    ZC_IF_SOME(value, coherence) {
      coherenceRevision = zc::heap<cross_module::CoherenceViewRevision>(value);
    }
  }

  CheckedNodeKey checkedNode(uint32_t preorder) const {
    return CheckedNodeKey{0x31, preorder, session.span(0, 1)};
  }

  CheckedFactsVerificationInput input(
      zc::ArrayPtr<const NodeFactRequirement> nodeRequirements,
      zc::ArrayPtr<const DefinitionFactRequirement> definitionRequirements = {},
      zc::ArrayPtr<const CheckerFailureRef> registeredFailures = {},
      zc::ArrayPtr<const CaptureFactRequirement> captureRequirements = {},
      zc::ArrayPtr<const binder::MaterializedOwnerLocalBindingInventoryEntry> ownerLocalBindings =
          {},
      zc::ArrayPtr<const binder::MaterializedAnonymousEntityEntry> anonymousEntities = {},
      zc::ArrayPtr<const identity::ImplId> coherentImpls = {}) const {
    const zc::ArrayPtr<const identity::DefId> importedDefinitions;
    return CheckedFactsVerificationInput{context,
                                         session.identityAuthority().fingerprint(),
                                         moduleId,
                                         session.source(),
                                         boundModule().parsedModule().contentDigest(),
                                         boundModule().parsedModule().receipt(),
                                         *signatureRevision,
                                         *importedRevision,
                                         *coherenceRevision,
                                         options,
                                         nodeRequirements,
                                         definitionRequirements,
                                         captureRequirements,
                                         importedDefinitions,
                                         coherentImpls,
                                         registeredFailures,
                                         ownerLocalBindings,
                                         anonymousEntities,
                                         session.identityAuthority(),
                                         semanticTypes};
  }

  identity::DefId definitionNamed(zc::StringPtr name) const {
    const auto definitions = boundModule().definitions().definitions();
    for (const auto& definition : definitions) {
      if (definition.record.name() == name) return definition.definition;
    }
    ZC_FAIL_REQUIRE("missing checked-facts fixture definition");
  }

  identity::ImplId implementation() const {
    const auto implementations = boundModule().definitions().identities().implementations();
    ZC_REQUIRE(implementations.size() == 1);
    return implementations[0].handle();
  }

  FrozenRecoveryLedger recoveryLedger() {
    auto brand = storeBrands.issue();
    ZC_REQUIRE(brand != zc::none);
    zc::Maybe<FrozenRecoveryLedger> result;
    ZC_IF_SOME(value, brand) {
      result =
          FrozenRecoveryLedger::from(context, value, 1, zc::heapArray<uint8_t>(1, uint8_t{0xa1}));
    }
    ZC_REQUIRE(result != zc::none);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
    ZC_UNREACHABLE
  }

  CheckedFactsCandidate nodeTypeCandidate(ast::NodeId node, zc::Array<uint8_t>&& record) {
    zc::Vector<NodeTypeMap::Entry> nodeTypes;
    nodeTypes.add(NodeTypeMap::Entry{node, i32, zc::mv(record)});
    return candidate(emptySubstitutions(), emptyWitnesses(),
                     NodeTypeMap::fromEntries(zc::mv(nodeTypes)));
  }

  CheckedFactsCandidate twoNodeTypesCandidate(ast::NodeId first, ast::NodeId second) {
    zc::Vector<NodeTypeMap::Entry> nodeTypes;
    nodeTypes.add(NodeTypeMap::Entry{first, i32, zc::heapArray<uint8_t>(1, uint8_t{0xf1})});
    nodeTypes.add(NodeTypeMap::Entry{second, i32, zc::heapArray<uint8_t>(1, uint8_t{0xf2})});
    return candidate(emptySubstitutions(), emptyWitnesses(),
                     NodeTypeMap::fromEntries(zc::mv(nodeTypes)));
  }

  CheckedFactsCandidate constantCandidate(ast::NodeId expression,
                                          const identity::Sha256Digest& evaluationRevision,
                                          zc::Array<uint8_t>&& nodeRecord,
                                          zc::Array<uint8_t>&& constantRecord) {
    zc::Vector<NodeTypeMap::Entry> nodeTypes;
    nodeTypes.add(NodeTypeMap::Entry{expression, i32, zc::mv(nodeRecord)});
    zc::Vector<ConstantFactMap::Entry> constants;
    using DependencyMap = ImmutableFactMap<identity::DefId, identity::Sha256Digest>;
    constants.add(ConstantFactMap::Entry{
        definitionId,
        ConstantEvaluationFact{definitionId, expression,
                               CanonicalConstValue::integer(signature::CanonicalInteger{
                                   signature::IntegerSign::NonNegative, zc::heapArray<uint8_t>(0)}),
                               i32, emptyMap<DependencyMap>(), evaluationRevision},
        zc::mv(constantRecord)});
    return candidate(emptySubstitutions(), emptyWitnesses(),
                     NodeTypeMap::fromEntries(zc::mv(nodeTypes)),
                     ConstantFactMap::fromEntries(zc::mv(constants)));
  }

  CheckedFactsCandidate storeCandidate(zc::Array<uint8_t>&& substitutionRecord,
                                       zc::Array<uint8_t>&& witnessRecord) {
    auto substitutionBrand = storeBrands.issue();
    auto witnessBrand = storeBrands.issue();
    ZC_REQUIRE(substitutionBrand != zc::none);
    ZC_REQUIRE(witnessBrand != zc::none);
    zc::Maybe<FrozenSubstitutionStore> substitutions;
    zc::Maybe<FrozenWitnessStore> witnesses;
    ZC_IF_SOME(brand, substitutionBrand) {
      zc::Vector<FrozenSubstitutionStore::Record> records;
      records.add(FrozenSubstitutionStore::Record{
          SubstitutionData{zc::Vector<identity::DefId>(), zc::Vector<identity::SemanticTypeId>()},
          zc::mv(substitutionRecord)});
      substitutions = FrozenSubstitutionStore::from(context, brand, zc::mv(records));
    }
    ZC_IF_SOME(brand, witnessBrand) {
      zc::Vector<FrozenWitnessStore::Record> records;
      records.add(FrozenWitnessStore::Record{WitnessArgumentsData{zc::Vector<WitnessEntry>()},
                                             zc::mv(witnessRecord)});
      witnesses = FrozenWitnessStore::from(context, brand, zc::mv(records));
    }
    ZC_REQUIRE(substitutions != zc::none);
    ZC_REQUIRE(witnesses != zc::none);
    ZC_IF_SOME(substitutionStore, substitutions) {
      ZC_IF_SOME(witnessStore, witnesses) {
        return candidate(zc::mv(substitutionStore), zc::mv(witnessStore), emptyMap<NodeTypeMap>());
      }
    }
    ZC_UNREACHABLE
  }

  CheckedFactsCandidate projectionCandidate(identity::DefId interface,
                                            identity::ImplId implementation) {
    auto substitutionBrand = storeBrands.issue();
    auto witnessBrand = storeBrands.issue();
    ZC_REQUIRE(substitutionBrand != zc::none);
    ZC_REQUIRE(witnessBrand != zc::none);
    zc::Maybe<FrozenSubstitutionStore> substitutions;
    zc::Maybe<FrozenWitnessStore> witnesses;
    ZC_IF_SOME(brand, substitutionBrand) {
      zc::Vector<identity::DefId> parameters;
      parameters.add(interface);
      zc::Vector<identity::SemanticTypeId> arguments;
      arguments.add(ownerType);
      zc::Vector<FrozenSubstitutionStore::Record> records;
      records.add(
          FrozenSubstitutionStore::Record{SubstitutionData{zc::mv(parameters), zc::mv(arguments)},
                                          zc::heapArray<uint8_t>(1, uint8_t{0xa1})});
      substitutions = FrozenSubstitutionStore::from(context, brand, zc::mv(records));
    }
    ZC_IF_SOME(brand, witnessBrand) {
      zc::Vector<identity::SemanticTypeId> interfaceArguments;
      zc::Vector<WitnessEntry> entries;
      entries.add(WitnessEntry{
          ownerType, InterfaceInstantiation{interface, zc::mv(interfaceArguments)}, implementation,
          zc::Vector<AssociatedTypeBindingData>(), zc::Vector<WitnessArgumentsId>()});
      zc::Vector<FrozenWitnessStore::Record> records;
      records.add(FrozenWitnessStore::Record{WitnessArgumentsData{zc::mv(entries)},
                                             zc::heapArray<uint8_t>(1, uint8_t{0xa1})});
      witnesses = FrozenWitnessStore::from(context, brand, zc::mv(records));
    }
    ZC_REQUIRE(substitutions != zc::none);
    ZC_REQUIRE(witnesses != zc::none);

    ZC_IF_SOME(substitutionStore, substitutions) {
      ZC_IF_SOME(witnessStore, witnesses) {
        auto substitution = substitutionStore.idAt(0);
        auto witness = witnessStore.idAt(0);
        ZC_REQUIRE(substitution != zc::none);
        ZC_REQUIRE(witness != zc::none);
        ZC_IF_SOME(substitutionId, substitution) {
          ZC_IF_SOME(witnessId, witness) {
            zc::Vector<identity::SemanticTypeId> interfaceArguments;
            zc::Vector<ProjectionFactMap::Entry> projections;
            projections.add(ProjectionFactMap::Entry{
                ast::NodeId(33),
                ProjectionFact{
                    ast::NodeId(33),
                    ProjectionKey{ownerType,
                                  InterfaceInstantiation{interface, zc::mv(interfaceArguments)},
                                  interface},
                    ownerType, implementation, witnessId},
                zc::heapArray<uint8_t>(1, uint8_t{0xa2})});
            zc::Vector<identity::SemanticTypeId> obligationArguments;
            zc::Vector<ObligationFactMap::Entry> obligations;
            obligations.add(ObligationFactMap::Entry{
                ast::NodeId(34),
                ObligationFact{ast::NodeId(34), ownerType,
                               InterfaceInstantiation{interface, zc::mv(obligationArguments)},
                               ImplResolution(UniqueImplResolution{implementation, substitutionId,
                                                                   witnessId})},
                zc::heapArray<uint8_t>(1, uint8_t{0xa3})});
            zc::Vector<identity::SemanticTypeId> eraseArguments;
            zc::Vector<identity::DefId> upcastPath;
            upcastPath.add(interface);
            zc::Vector<CoercionStep> coercionSteps;
            coercionSteps.add(
                CoercionStep(DynEraseStep{InterfaceInstantiation{interface, zc::mv(eraseArguments)},
                                          implementation, witnessId}));
            coercionSteps.add(CoercionStep(DynUpcastStep{zc::mv(upcastPath)}));
            zc::Vector<CoercionFactMap::Entry> coercions;
            coercions.add(CoercionFactMap::Entry{
                ast::NodeId(35),
                CoercionAdjustment{CoercionSite::Argument, ownerType, ownerType,
                                   zc::mv(coercionSteps), checkedNode(35).sourceSpan},
                zc::heapArray<uint8_t>(1, uint8_t{0xa4})});
            zc::Maybe<identity::ImplId> castImplementation = implementation;
            zc::Maybe<WitnessArgumentsId> castWitnesses = witnessId;
            zc::Vector<identity::DefId> castPath;
            castPath.add(interface);
            zc::Vector<CastFactMap::Entry> casts;
            casts.add(CastFactMap::Entry{
                ast::NodeId(36),
                CheckedCastFact{ast::NodeId(36), CastMode::Guaranteed, CastKind::DynErase,
                                ownerType, ownerType, ownerType, zc::mv(castImplementation),
                                zc::mv(castWitnesses), zc::mv(castPath), UnsafeRequirement::None,
                                checkedNode(36).sourceSpan},
                zc::heapArray<uint8_t>(1, uint8_t{0xa5})});
            zc::Vector<CoercionStep> receiverCoercionSteps;
            receiverCoercionSteps.add(CoercionStep(NeverToStep{}));
            zc::Maybe<CoercionAdjustment> receiverCoercion =
                CoercionAdjustment{CoercionSite::Argument, ownerType, ownerType,
                                   zc::mv(receiverCoercionSteps), checkedNode(37).sourceSpan};
            zc::Maybe<CheckedArgumentFact> receiver = CheckedArgumentFact{
                ast::NodeId(37), ownerType, ownerType, zc::mv(receiverCoercion)};
            zc::Maybe<ReceiverMode> receiverMode = ReceiverMode::Shared;
            zc::Vector<ReceiverAdjustmentStep> receiverSteps;
            receiverSteps.add(ReceiverAdjustmentStep::DereferenceShared);
            zc::Maybe<ReceiverAdjustment> receiverAdjustment = ReceiverAdjustment{
                ownerType, ownerType, zc::mv(receiverSteps), checkedNode(37).sourceSpan};
            zc::Vector<CheckedArgumentFact> arguments;
            zc::Maybe<CoercionAdjustment> argumentCoercion;
            arguments.add(CheckedArgumentFact{ast::NodeId(37), ownerType, ownerType,
                                              zc::mv(argumentCoercion)});
            zc::Maybe<CanonicalSubstitutionId> callSubstitution = substitutionId;
            zc::Maybe<WitnessArgumentsId> callWitnesses = witnessId;
            zc::Maybe<identity::SemanticTypeId> raises = ownerType;
            zc::Vector<CallFactMap::Entry> calls;
            calls.add(CallFactMap::Entry{
                ast::NodeId(37),
                TypedCallFact{ast::NodeId(37),
                              CheckedCallEnvelope{SelectedCallable(DirectCallable{definitionId}),
                                                  ownerType, zc::mv(receiver), zc::mv(receiverMode),
                                                  zc::mv(receiverAdjustment), zc::mv(arguments),
                                                  ownerType, ownerType, zc::mv(callSubstitution),
                                                  zc::mv(callWitnesses), zc::mv(raises)},
                              checkedNode(37).sourceSpan},
                zc::heapArray<uint8_t>(1, uint8_t{0xa6})});
            zc::Vector<ErrorUnionShapeFactMap::Entry> errorUnionShapes;
            errorUnionShapes.add(ErrorUnionShapeFactMap::Entry{
                ast::NodeId(37),
                ErrorUnionShapeFact{ast::NodeId(37), ownerType, ownerType, ownerType,
                                    ErrorUnionShapeOrigin::RaisingCall, checkedNode(37).sourceSpan},
                zc::heapArray<uint8_t>(1, uint8_t{0xa7})});
            auto result =
                candidate(zc::mv(substitutionStore), zc::mv(witnessStore), emptyMap<NodeTypeMap>());
            result.projections = ProjectionFactMap::fromEntries(zc::mv(projections));
            result.obligations = ObligationFactMap::fromEntries(zc::mv(obligations));
            result.coercions = CoercionFactMap::fromEntries(zc::mv(coercions));
            result.casts = CastFactMap::fromEntries(zc::mv(casts));
            result.calls = CallFactMap::fromEntries(zc::mv(calls));
            result.errorUnionShapes = ErrorUnionShapeFactMap::fromEntries(zc::mv(errorUnionShapes));
            return result;
          }
        }
      }
    }
    ZC_UNREACHABLE
  }

  CheckedFactsCandidate captureCandidate(const binder::AnonymousOwnerLocalKey& closure,
                                         binder::BindingTarget&& target, ast::NodeId node) {
    zc::Vector<PlaceProjection> projections;
    auto sourceSpan = checkedNode(0).sourceSpan;
    zc::Vector<CaptureFactMap::Entry> captures;
    captures.add(CaptureFactMap::Entry{
        CaptureKey{closure.clone(), target.clone()},
        CheckedCaptureFact{closure.clone(), target.clone(),
                           CheckedPlaceFact{node, PlaceRoot(TemporaryPlaceRoot{node}),
                                            zc::mv(projections), i32, false, true},
                           CaptureMode::SharedReference, CaptureOrigin::Inferred, i32,
                           zc::mv(sourceSpan)},
        zc::heapArray<uint8_t>(1, uint8_t{0xee})});
    return candidate(emptySubstitutions(), emptyWitnesses(), emptyMap<NodeTypeMap>(),
                     emptyMap<ConstantFactMap>(), CaptureFactMap::fromEntries(zc::mv(captures)));
  }

  CheckedFactsCandidate surfaceFactsCandidate(bool includeExtendedFacts = false) {
    const auto span = checkedNode(0).sourceSpan;
    zc::Vector<LiteralFactMap::Entry> literals;
    literals.add(LiteralFactMap::Entry{
        ast::NodeId(1),
        CheckedLiteralFact{ast::NodeId(1),
                           CanonicalLiteral::integer(signature::CanonicalInteger{
                               signature::IntegerSign::NonNegative, zc::heapArray<uint8_t>(0)}),
                           i32, span.clone()},
        zc::heapArray<uint8_t>(1, uint8_t{0xa1})});

    zc::Vector<AggregateFactMap::Entry> aggregates;
    aggregates.add(AggregateFactMap::Entry{
        ast::NodeId(2),
        CheckedAggregateFact{ast::NodeId(2), AggregateKind(TupleAggregate{}), i32,
                             zc::Vector<AggregateElementFact>(), span.clone()},
        zc::heapArray<uint8_t>(1, uint8_t{0xa2})});
    aggregates.add(AggregateFactMap::Entry{
        ast::NodeId(13),
        CheckedAggregateFact{ast::NodeId(13), AggregateKind(ArrayAggregate{}), i32,
                             zc::Vector<AggregateElementFact>(), span.clone()},
        zc::heapArray<uint8_t>(1, uint8_t{0xd1})});
    aggregates.add(AggregateFactMap::Entry{
        ast::NodeId(14),
        CheckedAggregateFact{ast::NodeId(14), AggregateKind(ObjectAggregate{}), i32,
                             zc::Vector<AggregateElementFact>(), span.clone()},
        zc::heapArray<uint8_t>(1, uint8_t{0xd2})});
    aggregates.add(AggregateFactMap::Entry{
        ast::NodeId(15),
        CheckedAggregateFact{ast::NodeId(15), AggregateKind(NominalAggregate{definitionId}), i32,
                             zc::Vector<AggregateElementFact>(), span.clone()},
        zc::heapArray<uint8_t>(1, uint8_t{0xd3})});

    zc::Vector<PlaceFactMap::Entry> places;
    places.add(PlaceFactMap::Entry{
        ast::NodeId(3),
        CheckedPlaceFact{ast::NodeId(3), PlaceRoot(TemporaryPlaceRoot{ast::NodeId(3)}),
                         zc::Vector<PlaceProjection>(), i32, false, true},
        zc::heapArray<uint8_t>(1, uint8_t{0xa3})});
    zc::Vector<PlaceProjection> projections;
    projections.add(PlaceProjection(FieldProjection{definitionId}));
    projections.add(PlaceProjection(TupleIndexProjection{1}));
    projections.add(PlaceProjection(IndexProjection{ast::NodeId(16)}));
    places.add(PlaceFactMap::Entry{
        ast::NodeId(16),
        CheckedPlaceFact{ast::NodeId(16), PlaceRoot(DefinitionPlaceRoot{definitionId}),
                         zc::mv(projections), i32, true, false},
        zc::heapArray<uint8_t>(1, uint8_t{0xd4})});

    zc::Vector<CoercionStep> coercionSteps;
    coercionSteps.add(CoercionStep(NeverToStep{}));
    coercionSteps.add(CoercionStep(ToAnyStep{}));
    coercionSteps.add(CoercionStep(ReborrowSharedStep{}));
    coercionSteps.add(CoercionStep(ReferenceToRawConstStep{}));
    coercionSteps.add(CoercionStep(ReferenceToRawMutableStep{}));
    coercionSteps.add(CoercionStep(RawMutToConstStep{}));
    coercionSteps.add(CoercionStep(UnionInjectStep{0, i32}));
    zc::Vector<CoercionFactMap::Entry> coercions;
    coercions.add(CoercionFactMap::Entry{
        ast::NodeId(4),
        CoercionAdjustment{CoercionSite::Argument, i32, i32, zc::mv(coercionSteps), span.clone()},
        zc::heapArray<uint8_t>(1, uint8_t{0xa4})});

    zc::Vector<CastFactMap::Entry> casts;
    casts.add(CastFactMap::Entry{
        ast::NodeId(5),
        CheckedCastFact{ast::NodeId(5), CastMode::Guaranteed, CastKind::IntegerWiden, i32, i32, i32,
                        zc::none, zc::none, zc::Vector<identity::DefId>(), UnsafeRequirement::None,
                        span.clone()},
        zc::heapArray<uint8_t>(1, uint8_t{0xa5})});

    zc::Vector<DefinitionTypeMap::Entry> definitionTypes;
    definitionTypes.add(
        DefinitionTypeMap::Entry{definitionId, i32, zc::heapArray<uint8_t>(1, uint8_t{0xb0})});

    zc::Vector<MemberFactMap::Entry> members;
    members.add(MemberFactMap::Entry{
        ast::NodeId(10), CheckedMemberFact{ast::NodeId(10), i32, definitionId, i32, zc::none},
        zc::heapArray<uint8_t>(1, uint8_t{0xb1})});

    zc::Vector<CallFactMap::Entry> calls;
    calls.add(CallFactMap::Entry{
        ast::NodeId(11),
        TypedCallFact{
            ast::NodeId(11),
            CheckedCallEnvelope{SelectedCallable(DirectCallable{definitionId}), i32, zc::none,
                                zc::none, zc::none, zc::Vector<CheckedArgumentFact>(), i32, i32,
                                zc::none, zc::none, zc::none},
            span.clone()},
        zc::heapArray<uint8_t>(1, uint8_t{0xb2})});

    zc::Vector<CompoundAssignmentFactMap::Entry> compoundAssignments;
    compoundAssignments.add(CompoundAssignmentFactMap::Entry{
        ast::NodeId(17),
        CompoundAssignmentFact{
            ast::NodeId(17), ast::NodeId(3), CompoundAssignmentOperation::AddAssign,
            CheckedCallEnvelope{SelectedCallable(DirectCallable{definitionId}), i32, zc::none,
                                zc::none, zc::none, zc::Vector<CheckedArgumentFact>(), i32, i32,
                                zc::none, zc::none, zc::none},
            zc::none, span.clone()},
        zc::heapArray<uint8_t>(1, uint8_t{0xd5})});

    zc::Vector<IndexFactMap::Entry> indexes;
    indexes.add(IndexFactMap::Entry{
        ast::NodeId(11),
        CheckedIndexFact{ast::NodeId(11), i32, i32, i32, IndexAccessMode::Read, i32},
        zc::heapArray<uint8_t>(1, uint8_t{0xb3})});

    zc::Vector<PatternFactMap::Entry> patterns;
    patterns.add(PatternFactMap::Entry{
        ast::NodeId(6),
        CheckedPatternFact{ast::NodeId(6), i32, PatternConstructor(WildcardPattern{}),
                           zc::Vector<PatternBindingFact>(), zc::Vector<PatternRefinementFact>(),
                           true, zc::none},
        zc::heapArray<uint8_t>(1, uint8_t{0xa6})});
    if (includeExtendedFacts) {
      patterns.add(PatternFactMap::Entry{
          ast::NodeId(20),
          CheckedPatternFact{ast::NodeId(20), i32,
                             PatternConstructor(LiteralPattern{CanonicalLiteral::integer(
                                 signature::CanonicalInteger{signature::IntegerSign::NonNegative,
                                                             zc::heapArray<uint8_t>(0)})}),
                             zc::Vector<PatternBindingFact>(), zc::Vector<PatternRefinementFact>(),
                             true, zc::none},
          zc::heapArray<uint8_t>(1, uint8_t{0xd8})});
      patterns.add(PatternFactMap::Entry{
          ast::NodeId(21),
          CheckedPatternFact{ast::NodeId(21), i32, PatternConstructor(TuplePattern{2}),
                             zc::Vector<PatternBindingFact>(), zc::Vector<PatternRefinementFact>(),
                             true, zc::none},
          zc::heapArray<uint8_t>(1, uint8_t{0xd9})});
      zc::Vector<identity::SemanticIdentifier> objectPatternFields;
      objectPatternFields.add(scalar<identity::SemanticIdentifier>("field"_zc));
      patterns.add(PatternFactMap::Entry{
          ast::NodeId(22),
          CheckedPatternFact{ast::NodeId(22), i32,
                             PatternConstructor(ObjectPattern{zc::mv(objectPatternFields)}),
                             zc::Vector<PatternBindingFact>(), zc::Vector<PatternRefinementFact>(),
                             true, zc::none},
          zc::heapArray<uint8_t>(1, uint8_t{0xda})});
      patterns.add(PatternFactMap::Entry{
          ast::NodeId(23),
          CheckedPatternFact{ast::NodeId(23), i32,
                             PatternConstructor(UnionAlternativePattern{0, i32}),
                             zc::Vector<PatternBindingFact>(), zc::Vector<PatternRefinementFact>(),
                             true, zc::none},
          zc::heapArray<uint8_t>(1, uint8_t{0xdb})});
      patterns.add(PatternFactMap::Entry{
          ast::NodeId(24),
          CheckedPatternFact{ast::NodeId(24), i32,
                             PatternConstructor(EnumVariantPattern{definitionId}),
                             zc::Vector<PatternBindingFact>(), zc::Vector<PatternRefinementFact>(),
                             true, zc::none},
          zc::heapArray<uint8_t>(1, uint8_t{0xdc})});
      patterns.add(PatternFactMap::Entry{
          ast::NodeId(25),
          CheckedPatternFact{ast::NodeId(25), i32, PatternConstructor(NominalPattern{definitionId}),
                             zc::Vector<PatternBindingFact>(), zc::Vector<PatternRefinementFact>(),
                             true, zc::none},
          zc::heapArray<uint8_t>(1, uint8_t{0xdd})});
    }

    zc::Vector<ObservedOperationFactMap::Entry> observedOperations;
    observedOperations.add(ObservedOperationFactMap::Entry{
        ast::NodeId(7),
        ObservedOperationFact{ast::NodeId(7), ObservedOperation::Raise, zc::none, span.clone()},
        zc::heapArray<uint8_t>(1, uint8_t{0xa7})});
    if (includeExtendedFacts) {
      observedOperations.add(ObservedOperationFactMap::Entry{
          ast::NodeId(26),
          ObservedOperationFact{ast::NodeId(26), ObservedOperation::MutateReceiver, zc::none,
                                span.clone()},
          zc::heapArray<uint8_t>(1, uint8_t{0xde})});
      observedOperations.add(ObservedOperationFactMap::Entry{
          ast::NodeId(27),
          ObservedOperationFact{ast::NodeId(27), ObservedOperation::UnsafeBoundary, zc::none,
                                span.clone()},
          zc::heapArray<uint8_t>(1, uint8_t{0xdf})});
      observedOperations.add(ObservedOperationFactMap::Entry{
          ast::NodeId(28),
          ObservedOperationFact{ast::NodeId(28), ObservedOperation::Suspend, zc::none,
                                span.clone()},
          zc::heapArray<uint8_t>(1, uint8_t{0xe0})});
    }

    zc::Vector<UnsafeOperationFactMap::Entry> unsafeOperations;
    unsafeOperations.add(UnsafeOperationFactMap::Entry{
        ast::NodeId(8), UnsafeScopeFact{ast::NodeId(8), UnsafeOperation::RawCast, zc::none, true},
        zc::heapArray<uint8_t>(1, uint8_t{0xa8})});
    if (includeExtendedFacts) {
      unsafeOperations.add(UnsafeOperationFactMap::Entry{
          ast::NodeId(29),
          UnsafeScopeFact{ast::NodeId(29), UnsafeOperation::RawDereference, ast::NodeId(1), false},
          zc::heapArray<uint8_t>(1, uint8_t{0xe1})});
      unsafeOperations.add(UnsafeOperationFactMap::Entry{
          ast::NodeId(30),
          UnsafeScopeFact{ast::NodeId(30), UnsafeOperation::ExternCall, ast::NodeId(1), true},
          zc::heapArray<uint8_t>(1, uint8_t{0xe2})});
      unsafeOperations.add(UnsafeOperationFactMap::Entry{
          ast::NodeId(31),
          UnsafeScopeFact{ast::NodeId(31), UnsafeOperation::Transmute, ast::NodeId(1), true},
          zc::heapArray<uint8_t>(1, uint8_t{0xe3})});
      unsafeOperations.add(UnsafeOperationFactMap::Entry{
          ast::NodeId(32),
          UnsafeScopeFact{ast::NodeId(32), UnsafeOperation::PackedFieldAccess, ast::NodeId(1),
                          true},
          zc::heapArray<uint8_t>(1, uint8_t{0xe4})});
    }

    zc::Vector<MarkerObligationFactMap::Entry> markerObligations;
    markerObligations.add(MarkerObligationFactMap::Entry{
        ast::NodeId(18),
        MarkerObligationFact{
            ast::NodeId(18), i32, definitionId, Polarity::Positive,
            MarkerEvidence(signature::BuiltinMarkerEvidence{signature::PrimitiveKind::I32})},
        zc::heapArray<uint8_t>(1, uint8_t{0xd6})});

    zc::Vector<ObligationFactMap::Entry> obligations;
    zc::Vector<identity::SemanticTypeId> interfaceArguments;
    interfaceArguments.add(i32);
    obligations.add(ObligationFactMap::Entry{
        ast::NodeId(19),
        ObligationFact{ast::NodeId(19), i32,
                       InterfaceInstantiation{definitionId, zc::mv(interfaceArguments)},
                       ImplResolution(NoImplResolution{})},
        zc::heapArray<uint8_t>(1, uint8_t{0xd7})});

    zc::Vector<ExhaustivenessFactMap::Entry> exhaustiveness;
    exhaustiveness.add(ExhaustivenessFactMap::Entry{
        ast::NodeId(12),
        ExhaustivenessFact{ast::NodeId(12), i32, ExhaustivenessDomain::Closed,
                           zc::Vector<PatternConstructor>(), zc::Vector<PatternConstructor>(),
                           zc::Vector<ast::NodeId>()},
        zc::heapArray<uint8_t>(1, uint8_t{0xb4})});

    zc::Vector<ErrorUnionShapeFactMap::Entry> errorUnionShapes;
    errorUnionShapes.add(ErrorUnionShapeFactMap::Entry{
        ast::NodeId(9),
        ErrorUnionShapeFact{ast::NodeId(9), i32, i32, i32, ErrorUnionShapeOrigin::Coercion,
                            span.clone()},
        zc::heapArray<uint8_t>(1, uint8_t{0xa9})});

    zc::Vector<ErrorOperatorFactMap::Entry> errorOperators;
    errorOperators.add(
        ErrorOperatorFactMap::Entry{ast::NodeId(9),
                                    ErrorOperatorFact{ast::NodeId(9), ErrorOperatorKind::Propagate,
                                                      i32, i32, i32, zc::none, span.clone()},
                                    zc::heapArray<uint8_t>(1, uint8_t{0xaa})});

    return CheckedFactsCandidate{
        context,
        session.identityAuthority().fingerprint().clone(),
        moduleId,
        boundModule().parsedModule().contentDigest(),
        boundModule().parsedModule().receipt(),
        *signatureRevision,
        *importedRevision,
        *coherenceRevision,
        options,
        emptySubstitutions(),
        emptyWitnesses(),
        emptyMap<NodeTypeMap>(),
        DefinitionTypeMap::fromEntries(zc::mv(definitionTypes)),
        LiteralFactMap::fromEntries(zc::mv(literals)),
        emptyMap<ConstantFactMap>(),
        AggregateFactMap::fromEntries(zc::mv(aggregates)),
        PlaceFactMap::fromEntries(zc::mv(places)),
        CoercionFactMap::fromEntries(zc::mv(coercions)),
        CastFactMap::fromEntries(zc::mv(casts)),
        CallFactMap::fromEntries(zc::mv(calls)),
        includeExtendedFacts ? CompoundAssignmentFactMap::fromEntries(zc::mv(compoundAssignments))
                             : emptyMap<CompoundAssignmentFactMap>(),
        MemberFactMap::fromEntries(zc::mv(members)),
        IndexFactMap::fromEntries(zc::mv(indexes)),
        PatternFactMap::fromEntries(zc::mv(patterns)),
        ObservedOperationFactMap::fromEntries(zc::mv(observedOperations)),
        emptyMap<CaptureFactMap>(),
        includeExtendedFacts ? MarkerObligationFactMap::fromEntries(zc::mv(markerObligations))
                             : emptyMap<MarkerObligationFactMap>(),
        ExhaustivenessFactMap::fromEntries(zc::mv(exhaustiveness)),
        UnsafeOperationFactMap::fromEntries(zc::mv(unsafeOperations)),
        emptyMap<ProjectionFactMap>(),
        includeExtendedFacts ? ObligationFactMap::fromEntries(zc::mv(obligations))
                             : emptyMap<ObligationFactMap>(),
        ErrorUnionShapeFactMap::fromEntries(zc::mv(errorUnionShapes)),
        ErrorOperatorFactMap::fromEntries(zc::mv(errorOperators)),
        zc::Vector<FrozenRecoveryLedger>(),
        zc::Vector<CheckerFailureRef>(),
        zc::Vector<CheckerAdvisoryRef>()};
  }

  identity::SemanticTypeId semanticType() const noexcept { return i32; }
  identity::DefId definition() const noexcept { return definitionId; }
  identity::SemanticContextBrand semanticContext() const noexcept { return context; }
  identity::ModuleId moduleIdentity() const noexcept { return moduleId; }
  const identity::DefinitionKey& definitionKey() const {
    auto definition = session.identityAuthority().definition(definitionId);
    ZC_REQUIRE(definition != zc::none);
    ZC_IF_SOME(entry, definition) { return entry.key(); }
    ZC_UNREACHABLE
  }

private:
  FrozenSubstitutionStore emptySubstitutions() {
    auto brand = storeBrands.issue();
    ZC_REQUIRE(brand != zc::none);
    zc::Maybe<FrozenSubstitutionStore> result;
    ZC_IF_SOME(value, brand) {
      result = FrozenSubstitutionStore::from(context, value,
                                             zc::Vector<FrozenSubstitutionStore::Record>());
    }
    ZC_IF_SOME(value, result) { return zc::mv(value); }
    ZC_UNREACHABLE
  }

  FrozenWitnessStore emptyWitnesses() {
    auto brand = storeBrands.issue();
    ZC_REQUIRE(brand != zc::none);
    zc::Maybe<FrozenWitnessStore> result;
    ZC_IF_SOME(value, brand) {
      result = FrozenWitnessStore::from(context, value, zc::Vector<FrozenWitnessStore::Record>());
    }
    ZC_IF_SOME(value, result) { return zc::mv(value); }
    ZC_UNREACHABLE
  }

  CheckedFactsCandidate candidate(FrozenSubstitutionStore&& substitutions,
                                  FrozenWitnessStore&& witnesses, NodeTypeMap&& nodeTypes) {
    return candidate(zc::mv(substitutions), zc::mv(witnesses), zc::mv(nodeTypes),
                     emptyMap<ConstantFactMap>());
  }

  CheckedFactsCandidate candidate(FrozenSubstitutionStore&& substitutions,
                                  FrozenWitnessStore&& witnesses, NodeTypeMap&& nodeTypes,
                                  ConstantFactMap&& constants) {
    return candidate(zc::mv(substitutions), zc::mv(witnesses), zc::mv(nodeTypes), zc::mv(constants),
                     emptyMap<CaptureFactMap>());
  }

  CheckedFactsCandidate candidate(FrozenSubstitutionStore&& substitutions,
                                  FrozenWitnessStore&& witnesses, NodeTypeMap&& nodeTypes,
                                  ConstantFactMap&& constants, CaptureFactMap&& captures) {
    return CheckedFactsCandidate{context,
                                 session.identityAuthority().fingerprint().clone(),
                                 moduleId,
                                 boundModule().parsedModule().contentDigest(),
                                 boundModule().parsedModule().receipt(),
                                 *signatureRevision,
                                 *importedRevision,
                                 *coherenceRevision,
                                 options,
                                 zc::mv(substitutions),
                                 zc::mv(witnesses),
                                 zc::mv(nodeTypes),
                                 emptyMap<DefinitionTypeMap>(),
                                 emptyMap<LiteralFactMap>(),
                                 zc::mv(constants),
                                 emptyMap<AggregateFactMap>(),
                                 emptyMap<PlaceFactMap>(),
                                 emptyMap<CoercionFactMap>(),
                                 emptyMap<CastFactMap>(),
                                 emptyMap<CallFactMap>(),
                                 emptyMap<CompoundAssignmentFactMap>(),
                                 emptyMap<MemberFactMap>(),
                                 emptyMap<IndexFactMap>(),
                                 emptyMap<PatternFactMap>(),
                                 emptyMap<ObservedOperationFactMap>(),
                                 zc::mv(captures),
                                 emptyMap<MarkerObligationFactMap>(),
                                 emptyMap<ExhaustivenessFactMap>(),
                                 emptyMap<UnsafeOperationFactMap>(),
                                 emptyMap<ProjectionFactMap>(),
                                 emptyMap<ObligationFactMap>(),
                                 emptyMap<ErrorUnionShapeFactMap>(),
                                 emptyMap<ErrorOperatorFactMap>(),
                                 zc::Vector<FrozenRecoveryLedger>(),
                                 zc::Vector<CheckerFailureRef>(),
                                 zc::Vector<CheckerAdvisoryRef>()};
  }

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

  CheckerAuthoritySession session;
  const identity::RegistryBrandIssuer& storeBrands;
  type::SemanticTypeStore& semanticTypes;
  identity::SemanticContextBrand context;
  identity::ModuleId moduleId;
  identity::DefId definitionId;
  identity::SemanticTypeId i32;
  identity::SemanticTypeId ownerType;
  zc::Own<signature::SignatureFactsRevision> signatureRevision;
  zc::Own<cross_module::ImportedSignatureViewRevision> importedRevision;
  zc::Own<cross_module::CoherenceViewRevision> coherenceRevision;
  identity::SemanticCompilerOptionsKey options =
      identity::SemanticCompilerOptionsKey::from(2026, true, false, true);
};

binder::LocalSyntaxPath localPath(uint32_t component) {
  zc::Vector<uint32_t> components;
  components.add(component);
  auto path = binder::LocalSyntaxPath::from(zc::mv(components));
  ZC_REQUIRE(path != zc::none);
  ZC_IF_SOME(value, path) { return zc::mv(value); }
  ZC_UNREACHABLE
}

binder::AnonymousOwnerLocalKey closureKey(
    const CheckedFactsCodecFixture& fixture,
    binder::AnonymousOwnerLocalRole role = binder::AnonymousOwnerLocalRole::Closure,
    uint32_t pathComponent = 1) {
  auto key = binder::AnonymousOwnerLocalKey::from(
      binder::StableBodyOwnerKey::definition(fixture.definitionKey().clone()),
      localPath(pathComponent), role);
  ZC_REQUIRE(key != zc::none);
  ZC_IF_SOME(value, key) { return zc::mv(value); }
  ZC_UNREACHABLE
}

binder::OwnerLocalBindingKey ownerLocalKey(const CheckedFactsCodecFixture& fixture) {
  auto key = binder::OwnerLocalBindingKey::from(
      binder::StableBodyOwnerKey::definition(fixture.definitionKey().clone()), localPath(2),
      binder::OwnerLocalBindingNamespace::Value, binder::OwnerLocalBindingKind::Local,
      scalar<identity::DeclaredDefinitionName>("captured"_zc));
  ZC_REQUIRE(key != zc::none);
  ZC_IF_SOME(value, key) { return zc::mv(value); }
  ZC_UNREACHABLE
}

binder::OwnerLocalBindingId ownerLocalId(const CheckedFactsCodecFixture& fixture, uint32_t slot) {
  auto allocator = binder::ModuleLocalIdentityAllocator::create(fixture.semanticContext(),
                                                                fixture.moduleIdentity());
  ZC_REQUIRE(allocator != zc::none);
  binder::OwnerLocalBindingId result;
  ZC_IF_SOME(value, allocator) {
    for (uint32_t index = 0; index <= slot; ++index) {
      auto allocated = value.allocateOwnerLocalBinding();
      ZC_REQUIRE(allocated != zc::none);
      ZC_IF_SOME(binding, allocated) { result = binding; }
    }
  }
  return result;
}

binder::AnonymousOwnerLocalId anonymousOwnerLocalId(const CheckedFactsCodecFixture& fixture,
                                                    uint32_t slot) {
  auto allocator = binder::ModuleLocalIdentityAllocator::create(fixture.semanticContext(),
                                                                fixture.moduleIdentity());
  ZC_REQUIRE(allocator != zc::none);
  binder::AnonymousOwnerLocalId result;
  ZC_IF_SOME(value, allocator) {
    for (uint32_t index = 0; index <= slot; ++index) {
      auto allocated = value.allocateAnonymousOwnerLocal();
      ZC_REQUIRE(allocated != zc::none);
      ZC_IF_SOME(entity, allocated) { result = entity; }
    }
  }
  return result;
}

bool canonicalMismatch(const CheckedFactsVerificationResult& result) {
  if (!result.is<CheckedFactsInvariantRejected>()) return false;
  const auto& failures = result.get<CheckedFactsInvariantRejected>().failures;
  if (failures.size() != 1) return false;
  const auto& value = failures[0].variant();
  return value.is<signature::CheckerInvariantFact>() &&
         value.get<signature::CheckerInvariantFact>().kind ==
             signature::CheckerInvariantKind::CanonicalCodecMismatch;
}

CheckerFailureRef literalOutOfRangeFailure(const CheckedFactsCodecFixture& fixture,
                                           type::semantic::PrimitiveKind target,
                                           TypeErrorId recovery) {
  zc::Vector<CheckerDisplayArgument> arguments;
  arguments.add(CheckerDisplayArgument(
      LiteralDisplayArg{CanonicalLiteral::integer(signature::CanonicalInteger{
          signature::IntegerSign::NonNegative, zc::heapArray<uint8_t>(0)})}));
  arguments.add(CheckerDisplayArgument(PrimitiveTypeDisplayArg{target}));
  zc::Vector<CheckerNoteRef> notes;
  zc::Maybe<TypeErrorId> recoveryId(recovery);
  return CheckerFailureRef{
      CheckerErrorId::BodyLiteralOutOfRange(),
      CheckerDiagnosticStage::Body,
      ast::NodeId(1),
      fixture.checkedNode(0).sourceSpan.clone(),
      zc::mv(arguments),
      zc::mv(notes),
      CheckerDiagnosticProducer::Constant,
      CheckerRecoveryPolicy(CreateRootRecoveryPolicy{CheckerRecoveryClass::FailedInference, true}),
      CheckerEmitterOrdinal{static_cast<uint8_t>(CheckerDiagnosticStage::Body), 0, 0, 0},
      zc::mv(recoveryId)};
}

}  // namespace

ZC_TEST("CheckedFactsRevision.ReproducesIndependentOracle") {
  const uint8_t module[] = {0xa1};
  SingleRecordGroup b0(0xb0);
  SingleRecordGroup b1(0xb1);
  SingleRecordGroup b2(0xb2);
  SingleRecordGroup b3(0xb3);
  SingleRecordGroup b4(0xb4);
  SingleRecordGroup b5(0xb5);
  SingleRecordGroup b6(0xb6);
  SingleRecordGroup b7(0xb7);
  SingleRecordGroup b8(0xb8);
  SingleRecordGroup b9(0xb9);
  SingleRecordGroup ba(0xba);
  SingleRecordGroup bb(0xbb);
  SingleRecordGroup bc(0xbc);
  SingleRecordGroup bd(0xbd);
  SingleRecordGroup be(0xbe);
  SingleRecordGroup bf(0xbf);
  SingleRecordGroup c0(0xc0);
  SingleRecordGroup c1(0xc1);
  SingleRecordGroup c2(0xc2);
  SingleRecordGroup c3(0xc3);
  SingleRecordGroup c4(0xc4);
  SingleRecordGroup c5(0xc5);
  SingleRecordGroup c6(0xc6);
  SingleRecordGroup c7(0xc7);
  const CheckedFactsCanonicalGroups groups{
      b0.records(), b1.records(), b2.records(), b3.records(), b4.records(), b5.records(),
      b6.records(), b7.records(), b8.records(), b9.records(), ba.records(), bb.records(),
      bc.records(), bd.records(), be.records(), bf.records(), c0.records(), c1.records(),
      c2.records(), c3.records(), c4.records(), c5.records(), c6.records(), c7.records()};
  const auto options = identity::SemanticCompilerOptionsKey::from(2026, true, false, true);
  auto revision = CheckedFactsRevision::computeFramed(
      repeatedDigest(0x00), module, repeatedDigest(0x22), repeatedDigest(0x33),
      repeatedDigest(0x44), repeatedDigest(0x55), repeatedDigest(0x66), options, groups);
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "09e8335be64649f47e44e18672852ec1e9a1669f9d142a806d6a58fceb7c1b62"_zc);
  }
}

ZC_TEST("CheckedFactsRevision.RejectsDuplicateCanonicalRecords") {
  const uint8_t module[] = {0xa1};
  const uint8_t record[] = {0xb0};
  const zc::ArrayPtr<const uint8_t> duplicateRecords[] = {record, record};
  const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> empty;
  const CheckedFactsCanonicalGroups groups{duplicateRecords,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty,
                                           empty};
  const auto options = identity::SemanticCompilerOptionsKey::from(2026, true, false, true);
  ZC_EXPECT(CheckedFactsRevision::computeFramed(repeatedDigest(0x00), module, repeatedDigest(0x22),
                                                repeatedDigest(0x33), repeatedDigest(0x44),
                                                repeatedDigest(0x55), repeatedDigest(0x66), options,
                                                groups) == zc::none);
}

ZC_TEST("CheckedFactsCanonicalCodec.ExpandsCheckedNodeKeyWithoutNodeHandle") {
  CheckedFactsCodecFixture fixture;
  zc::Vector<NodeFactRequirement> firstRequirements;
  firstRequirements.add(
      NodeFactRequirement{CheckedFactGroup::NodeType, ast::NodeId(1), fixture.checkedNode(7)});
  const auto firstInput = fixture.input(firstRequirements.asPtr());
  auto firstCandidate =
      fixture.nodeTypeCandidate(ast::NodeId(1), zc::heapArray<uint8_t>(1, uint8_t{0xff}));
  auto firstRecord = CheckedFactsCanonicalCodec::encodeNodeFact(
      CheckedFactGroup::NodeType, ast::NodeId(1), firstCandidate, firstInput);
  ZC_REQUIRE(firstRecord != zc::none);

  zc::Vector<NodeFactRequirement> secondRequirements;
  secondRequirements.add(
      NodeFactRequirement{CheckedFactGroup::NodeType, ast::NodeId(99), fixture.checkedNode(7)});
  const auto secondInput = fixture.input(secondRequirements.asPtr());
  auto secondCandidate =
      fixture.nodeTypeCandidate(ast::NodeId(99), zc::heapArray<uint8_t>(1, uint8_t{0xfe}));
  auto secondRecord = CheckedFactsCanonicalCodec::encodeNodeFact(
      CheckedFactGroup::NodeType, ast::NodeId(99), secondCandidate, secondInput);
  ZC_REQUIRE(secondRecord != zc::none);
  ZC_IF_SOME(first, firstRecord) {
    ZC_IF_SOME(second, secondRecord) { ZC_EXPECT(sameBytes(first.asPtr(), second.asPtr())); }
  }
}

ZC_TEST("CheckedFactsCanonicalCodec.CanonicallySortsProducerMapRecords") {
  CheckedFactsCodecFixture fixture;
  zc::Vector<NodeFactRequirement> requirements;
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::NodeType, ast::NodeId(1), fixture.checkedNode(9)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::NodeType, ast::NodeId(2), fixture.checkedNode(0)});
  const auto input = fixture.input(requirements.asPtr());
  auto candidate = fixture.twoNodeTypesCandidate(ast::NodeId(1), ast::NodeId(2));
  ZC_EXPECT(candidate.nodeTypes.entries()[0].key == ast::NodeId(1));
  ZC_EXPECT(CheckedFactsCanonicalCodec::writeCanonicalRecords(candidate, input));
  ZC_EXPECT(candidate.nodeTypes.entries()[0].key == ast::NodeId(2));
  ZC_EXPECT(candidate.nodeTypes.entries()[1].key == ast::NodeId(1));
  auto accepted = CheckedFactsVerifier::verify(zc::mv(candidate), input);
  ZC_EXPECT(accepted.is<VerifiedCheckedFacts>());
}

ZC_TEST("CheckedFactsCanonicalCodec.ExpandsCaptureKeysWithoutRevisionLocalSlots") {
  CheckedFactsCodecFixture fixture;
  const auto firstBinding = ownerLocalId(fixture, 0);
  const auto secondBinding = ownerLocalId(fixture, 1);
  ZC_REQUIRE(firstBinding != secondBinding);
  auto closure = closureKey(fixture);
  auto bindingKey = ownerLocalKey(fixture);
  auto sourceSpan = fixture.checkedNode(0).sourceSpan;

  zc::Vector<binder::MaterializedAnonymousEntityEntry> anonymousEntities;
  anonymousEntities.add(binder::MaterializedAnonymousEntityEntry{
      ast::NodeId(2), binder::DefinitionSite::declaration(ast::NodeId(2)),
      anonymousOwnerLocalId(fixture, 0), closure.clone(), sourceSpan.clone()});
  zc::Vector<NodeFactRequirement> nodeRequirements;
  nodeRequirements.add(
      NodeFactRequirement{CheckedFactGroup::NodeType, ast::NodeId(7), fixture.checkedNode(0)});

  zc::Vector<binder::MaterializedOwnerLocalBindingInventoryEntry> firstBindings;
  firstBindings.add(binder::MaterializedOwnerLocalBindingInventoryEntry{
      ast::NodeId(1), binder::DefinitionSite::declaration(ast::NodeId(1)), firstBinding,
      bindingKey.clone(), sourceSpan.clone()});
  zc::Vector<CaptureFactRequirement> firstRequirements;
  firstRequirements.add(CaptureFactRequirement{
      CaptureKey{closure.clone(), binder::BindingTarget::ownerLocal(firstBinding)}});
  const auto firstInput = fixture.input(nodeRequirements.asPtr(), {}, {}, firstRequirements.asPtr(),
                                        firstBindings.asPtr(), anonymousEntities.asPtr());
  auto firstCandidate = fixture.captureCandidate(
      closure, binder::BindingTarget::ownerLocal(firstBinding), ast::NodeId(7));
  auto firstRecord = CheckedFactsCanonicalCodec::encodeCaptureFact(
      firstCandidate.captures.entries()[0].key, firstCandidate, firstInput);
  ZC_REQUIRE(firstRecord != zc::none);

  zc::Vector<binder::MaterializedOwnerLocalBindingInventoryEntry> secondBindings;
  secondBindings.add(binder::MaterializedOwnerLocalBindingInventoryEntry{
      ast::NodeId(1), binder::DefinitionSite::declaration(ast::NodeId(1)), secondBinding,
      bindingKey.clone(), sourceSpan.clone()});
  zc::Vector<CaptureFactRequirement> secondRequirements;
  secondRequirements.add(CaptureFactRequirement{
      CaptureKey{closure.clone(), binder::BindingTarget::ownerLocal(secondBinding)}});
  const auto secondInput =
      fixture.input(nodeRequirements.asPtr(), {}, {}, secondRequirements.asPtr(),
                    secondBindings.asPtr(), anonymousEntities.asPtr());
  auto secondCandidate = fixture.captureCandidate(
      closure, binder::BindingTarget::ownerLocal(secondBinding), ast::NodeId(7));
  auto secondRecord = CheckedFactsCanonicalCodec::encodeCaptureFact(
      secondCandidate.captures.entries()[0].key, secondCandidate, secondInput);
  ZC_REQUIRE(secondRecord != zc::none);
  ZC_IF_SOME(first, firstRecord) {
    ZC_IF_SOME(second, secondRecord) { ZC_EXPECT(sameBytes(first.asPtr(), second.asPtr())); }
  }

  auto functionExpression =
      closureKey(fixture, binder::AnonymousOwnerLocalRole::FunctionExpression, 3);
  zc::Vector<binder::MaterializedAnonymousEntityEntry> functionExpressions;
  functionExpressions.add(binder::MaterializedAnonymousEntityEntry{
      ast::NodeId(3), binder::DefinitionSite::declaration(ast::NodeId(3)),
      anonymousOwnerLocalId(fixture, 0), functionExpression.clone(), sourceSpan.clone()});
  zc::Vector<CaptureFactRequirement> functionRequirements;
  functionRequirements.add(CaptureFactRequirement{
      CaptureKey{functionExpression.clone(), binder::BindingTarget::ownerLocal(firstBinding)}});
  const auto functionInput =
      fixture.input(nodeRequirements.asPtr(), {}, {}, functionRequirements.asPtr(),
                    firstBindings.asPtr(), functionExpressions.asPtr());
  auto functionCandidate = fixture.captureCandidate(
      functionExpression, binder::BindingTarget::ownerLocal(firstBinding), ast::NodeId(7));
  ZC_EXPECT(CheckedFactsCanonicalCodec::encodeCaptureFact(
                functionCandidate.captures.entries()[0].key, functionCandidate, functionInput) !=
            zc::none);

  zc::Vector<CaptureFactRequirement> moduleRequirements;
  moduleRequirements.add(CaptureFactRequirement{
      CaptureKey{closure.clone(), binder::BindingTarget::module(fixture.moduleIdentity())}});
  const auto moduleInput = fixture.input(nodeRequirements.asPtr(), {}, {},
                                         moduleRequirements.asPtr(), {}, anonymousEntities.asPtr());
  auto moduleCandidate = fixture.captureCandidate(
      closure, binder::BindingTarget::module(fixture.moduleIdentity()), ast::NodeId(7));
  ZC_EXPECT(CheckedFactsCanonicalCodec::encodeCaptureFact(moduleCandidate.captures.entries()[0].key,
                                                          moduleCandidate,
                                                          moduleInput) == zc::none);

  zc::Vector<CaptureFactRequirement> definitionRequirements;
  definitionRequirements.add(CaptureFactRequirement{
      CaptureKey{closure.clone(), binder::BindingTarget::definition(fixture.definition())}});
  const auto definitionInput =
      fixture.input(nodeRequirements.asPtr(), {}, {}, definitionRequirements.asPtr(), {},
                    anonymousEntities.asPtr());
  auto definitionCandidate = fixture.captureCandidate(
      closure, binder::BindingTarget::definition(fixture.definition()), ast::NodeId(7));
  ZC_EXPECT(CheckedFactsCanonicalCodec::encodeCaptureFact(
                definitionCandidate.captures.entries()[0].key, definitionCandidate,
                definitionInput) == zc::none);
}

ZC_TEST("CheckedFactsCanonicalCodec.VerifierRejectsTamperedNodeRecord") {
  CheckedFactsCodecFixture fixture;
  zc::Vector<NodeFactRequirement> requirements;
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::NodeType, ast::NodeId(1), fixture.checkedNode(0)});
  const auto input = fixture.input(requirements.asPtr());
  auto probe = fixture.nodeTypeCandidate(ast::NodeId(1), zc::heapArray<uint8_t>(1, uint8_t{0xff}));
  auto encoded = CheckedFactsCanonicalCodec::encodeNodeFact(CheckedFactGroup::NodeType,
                                                            ast::NodeId(1), probe, input);
  ZC_REQUIRE(encoded != zc::none);
  ZC_IF_SOME(record, encoded) {
    auto valid = fixture.nodeTypeCandidate(ast::NodeId(1), zc::heapArray<uint8_t>(record.asPtr()));
    auto accepted = CheckedFactsVerifier::verify(zc::mv(valid), input);
    ZC_EXPECT(accepted.is<VerifiedCheckedFacts>());

    auto tamperedBytes = zc::heapArray<uint8_t>(record.asPtr());
    tamperedBytes[0] ^= 0x01;
    auto tampered = fixture.nodeTypeCandidate(ast::NodeId(1), zc::mv(tamperedBytes));
    auto rejected = CheckedFactsVerifier::verify(zc::mv(tampered), input);
    ZC_EXPECT(canonicalMismatch(rejected));
  }
}

ZC_TEST("CheckedFactsSourceRejectionVerifier.ReencodesPrimitiveTypeDisplayArgument") {
  CheckedFactsCodecFixture fixture;
  zc::Vector<NodeFactRequirement> nodeRequirements;

  auto acceptedLedger = fixture.recoveryLedger();
  auto acceptedError = acceptedLedger.idAt(0);
  ZC_REQUIRE(acceptedError != zc::none);
  zc::Vector<CheckerFailureRef> acceptedRegistered;
  zc::Vector<CheckerFailureRef> acceptedFailures;
  ZC_IF_SOME(error, acceptedError) {
    acceptedRegistered.add(
        literalOutOfRangeFailure(fixture, type::semantic::PrimitiveKind::U64, error));
    acceptedFailures.add(
        literalOutOfRangeFailure(fixture, type::semantic::PrimitiveKind::U64, error));
  }
  const auto acceptedInput =
      fixture.input(nodeRequirements.asPtr(), {}, acceptedRegistered.asPtr());
  zc::Vector<FrozenRecoveryLedger> acceptedLedgers;
  acceptedLedgers.add(zc::mv(acceptedLedger));
  auto accepted = CheckedFactsSourceRejectionVerifier::verify(
      CheckedFactsSourceRejected{zc::mv(acceptedFailures), zc::Vector<CheckerAdvisoryRef>(),
                                 zc::mv(acceptedLedgers)},
      acceptedInput);
  ZC_EXPECT(accepted.is<CheckedFactsSourceRejected>());

  auto mutatedLedger = fixture.recoveryLedger();
  auto mutatedError = mutatedLedger.idAt(0);
  ZC_REQUIRE(mutatedError != zc::none);
  zc::Vector<CheckerFailureRef> mutatedRegistered;
  zc::Vector<CheckerFailureRef> mutatedFailures;
  ZC_IF_SOME(error, mutatedError) {
    mutatedRegistered.add(
        literalOutOfRangeFailure(fixture, type::semantic::PrimitiveKind::U64, error));
    mutatedFailures.add(
        literalOutOfRangeFailure(fixture, type::semantic::PrimitiveKind::F64, error));
  }
  const auto mutatedInput = fixture.input(nodeRequirements.asPtr(), {}, mutatedRegistered.asPtr());
  zc::Vector<FrozenRecoveryLedger> mutatedLedgers;
  mutatedLedgers.add(zc::mv(mutatedLedger));
  auto mutated = CheckedFactsSourceRejectionVerifier::verify(
      CheckedFactsSourceRejected{zc::mv(mutatedFailures), zc::Vector<CheckerAdvisoryRef>(),
                                 zc::mv(mutatedLedgers)},
      mutatedInput);
  ZC_REQUIRE(mutated.is<CheckedFactsInvariantRejected>());
  const auto& failures = mutated.get<CheckedFactsInvariantRejected>().failures;
  ZC_REQUIRE(failures.size() == 1);
  const auto& failure = failures[0].variant();
  ZC_REQUIRE(failure.is<signature::CheckerInvariantFact>());
  ZC_EXPECT(failure.get<signature::CheckerInvariantFact>().kind ==
            signature::CheckerInvariantKind::InvalidEmitterOrdinal);
}

ZC_TEST("CheckedFactsCanonicalCodec.ExpandsStoreValuesIndependentlyOfIssuerSlots") {
  CheckedFactsCodecFixture fixture;
  zc::Vector<NodeFactRequirement> requirements;
  const auto input = fixture.input(requirements.asPtr());
  auto first = fixture.storeCandidate(zc::heapArray<uint8_t>(1, uint8_t{0xa1}),
                                      zc::heapArray<uint8_t>(1, uint8_t{0xa2}));
  auto substitution = CheckedFactsCanonicalCodec::encodeSubstitution(0, first, input);
  auto witness = CheckedFactsCanonicalCodec::encodeWitness(0, first, input);
  ZC_REQUIRE(substitution != zc::none);
  ZC_REQUIRE(witness != zc::none);
  ZC_IF_SOME(substitutionRecord, substitution) {
    ZC_IF_SOME(witnessRecord, witness) {
      ZC_EXPECT(substitutionRecord.size() == 16);
      ZC_EXPECT(witnessRecord.size() == 8);
      auto canonical = fixture.storeCandidate(zc::heapArray<uint8_t>(substitutionRecord.asPtr()),
                                              zc::heapArray<uint8_t>(witnessRecord.asPtr()));
      ZC_EXPECT(CheckedFactsCanonicalCodec::recordsMatch(canonical, input));
      ZC_REQUIRE(CheckedFactsCanonicalCodec::writeCanonicalRecords(canonical, input));
      auto accepted = CheckedFactsVerifier::verify(zc::mv(canonical), input);
      ZC_REQUIRE(accepted.is<VerifiedCheckedFacts>());
      const auto& verified = accepted.get<VerifiedCheckedFacts>();
      ZC_EXPECT(verified.coherenceViewRevision().digest() == input.coherenceViewRevision.digest());
      ZC_EXPECT(verified.advisories().size() == 0);
      ZC_IF_SOME(witnessId, verified.witnessStore().idAt(0)) {
        ZC_EXPECT(verified.witnessStore().contains(witnessId));
      }

      auto second = fixture.storeCandidate(zc::heapArray<uint8_t>(1, uint8_t{0xb1}),
                                           zc::heapArray<uint8_t>(1, uint8_t{0xb2}));
      auto secondSubstitution = CheckedFactsCanonicalCodec::encodeSubstitution(0, second, input);
      auto secondWitness = CheckedFactsCanonicalCodec::encodeWitness(0, second, input);
      ZC_REQUIRE(secondSubstitution != zc::none);
      ZC_REQUIRE(secondWitness != zc::none);
      ZC_IF_SOME(secondSubstitutionRecord, secondSubstitution) {
        ZC_EXPECT(sameBytes(substitutionRecord.asPtr(), secondSubstitutionRecord.asPtr()));
      }
      ZC_IF_SOME(secondWitnessRecord, secondWitness) {
        ZC_EXPECT(sameBytes(witnessRecord.asPtr(), secondWitnessRecord.asPtr()));
      }

      auto tamperedSubstitution = zc::heapArray<uint8_t>(substitutionRecord.asPtr());
      tamperedSubstitution[15] = 1;
      auto tampered = fixture.storeCandidate(zc::mv(tamperedSubstitution),
                                             zc::heapArray<uint8_t>(witnessRecord.asPtr()));
      ZC_EXPECT(!CheckedFactsCanonicalCodec::recordsMatch(tampered, input));
      auto rejected = CheckedFactsVerifier::verify(zc::mv(tampered), input);
      ZC_EXPECT(canonicalMismatch(rejected));
    }
  }
}

ZC_TEST("CheckedFactsCanonicalCodec.RecomputesConstantEvaluationRevision") {
  CheckedFactsCodecFixture fixture;
  zc::Vector<NodeFactRequirement> nodeRequirements;
  nodeRequirements.add(
      NodeFactRequirement{CheckedFactGroup::NodeType, ast::NodeId(7), fixture.checkedNode(0)});
  zc::Vector<DefinitionFactRequirement> definitionRequirements;
  definitionRequirements.add(
      DefinitionFactRequirement{CheckedFactGroup::Constant, fixture.definition()});
  const auto input = fixture.input(nodeRequirements.asPtr(), definitionRequirements.asPtr());

  auto candidate = fixture.constantCandidate(ast::NodeId(7), repeatedDigest(0xee),
                                             zc::heapArray<uint8_t>(1, uint8_t{0xa1}),
                                             zc::heapArray<uint8_t>(1, uint8_t{0xa2}));
  const auto revision = CheckedFactsCanonicalCodec::computeConstantEvaluationRevision(
      candidate.constants.entries()[0].value, candidate, input);
  ZC_REQUIRE(revision != zc::none);
  ZC_EXPECT(CheckedFactsCanonicalCodec::writeCanonicalRecords(candidate, input));
  auto nodeRecord =
      zc::heapArray<uint8_t>(candidate.nodeTypes.entries()[0].canonicalRecord.asPtr());
  auto constantRecord =
      zc::heapArray<uint8_t>(candidate.constants.entries()[0].canonicalRecord.asPtr());
  const auto acceptedRevision = candidate.constants.entries()[0].value.evaluationRevision;
  ZC_IF_SOME(value, revision) { ZC_EXPECT(value == acceptedRevision); }
  auto accepted = CheckedFactsVerifier::verify(zc::mv(candidate), input);
  ZC_EXPECT(accepted.is<VerifiedCheckedFacts>());

  auto tampered = fixture.constantCandidate(ast::NodeId(7), repeatedDigest(0xef),
                                            zc::mv(nodeRecord), zc::mv(constantRecord));
  auto rejected = CheckedFactsVerifier::verify(zc::mv(tampered), input);
  ZC_EXPECT(canonicalMismatch(rejected));
}

ZC_TEST("CheckedFactsCanonicalCodec.VerifiesSurfaceFactFamilies") {
  CheckedFactsCodecFixture fixture;
  zc::Vector<NodeFactRequirement> requirements;
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Literal, ast::NodeId(1), fixture.checkedNode(1)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Aggregate, ast::NodeId(2), fixture.checkedNode(2)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Aggregate, ast::NodeId(13), fixture.checkedNode(13)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Aggregate, ast::NodeId(14), fixture.checkedNode(14)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Aggregate, ast::NodeId(15), fixture.checkedNode(15)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Place, ast::NodeId(3), fixture.checkedNode(3)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Place, ast::NodeId(16), fixture.checkedNode(16)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Coercion, ast::NodeId(4), fixture.checkedNode(4)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Cast, ast::NodeId(5), fixture.checkedNode(5)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Pattern, ast::NodeId(6), fixture.checkedNode(6)});
  requirements.add(NodeFactRequirement{CheckedFactGroup::ObservedOperation, ast::NodeId(7),
                                       fixture.checkedNode(7)});
  requirements.add(NodeFactRequirement{CheckedFactGroup::UnsafeOperation, ast::NodeId(8),
                                       fixture.checkedNode(8)});
  requirements.add(NodeFactRequirement{CheckedFactGroup::ErrorUnionShape, ast::NodeId(9),
                                       fixture.checkedNode(9)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::ErrorOperator, ast::NodeId(9), fixture.checkedNode(9)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Member, ast::NodeId(10), fixture.checkedNode(10)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Call, ast::NodeId(11), fixture.checkedNode(11)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Index, ast::NodeId(11), fixture.checkedNode(11)});
  requirements.add(NodeFactRequirement{CheckedFactGroup::Exhaustiveness, ast::NodeId(12),
                                       fixture.checkedNode(12)});
  zc::Vector<DefinitionFactRequirement> definitionRequirements;
  definitionRequirements.add(
      DefinitionFactRequirement{CheckedFactGroup::DefinitionType, fixture.definition()});
  const auto input = fixture.input(requirements.asPtr(), definitionRequirements.asPtr());
  auto candidate = fixture.surfaceFactsCandidate();

  ZC_REQUIRE(CheckedFactsCanonicalCodec::writeCanonicalRecords(candidate, input));
  ZC_EXPECT(CheckedFactsCanonicalCodec::recordsMatch(candidate, input));
  auto verified = CheckedFactsVerifier::verify(zc::mv(candidate), input);
  ZC_EXPECT(verified.is<VerifiedCheckedFacts>());

  requirements.add(NodeFactRequirement{CheckedFactGroup::CompoundAssignment, ast::NodeId(17),
                                       fixture.checkedNode(17)});
  requirements.add(NodeFactRequirement{CheckedFactGroup::MarkerObligation, ast::NodeId(18),
                                       fixture.checkedNode(18)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Obligation, ast::NodeId(19), fixture.checkedNode(19)});
  for (uint32_t node = 20; node <= 25; ++node)
    requirements.add(NodeFactRequirement{CheckedFactGroup::Pattern, ast::NodeId(node),
                                         fixture.checkedNode(node)});
  for (uint32_t node = 26; node <= 28; ++node)
    requirements.add(NodeFactRequirement{CheckedFactGroup::ObservedOperation, ast::NodeId(node),
                                         fixture.checkedNode(node)});
  for (uint32_t node = 29; node <= 32; ++node)
    requirements.add(NodeFactRequirement{CheckedFactGroup::UnsafeOperation, ast::NodeId(node),
                                         fixture.checkedNode(node)});
  const auto extendedInput = fixture.input(requirements.asPtr(), definitionRequirements.asPtr());
  auto extendedCandidate = fixture.surfaceFactsCandidate(true);
  ZC_REQUIRE(CheckedFactsCanonicalCodec::writeCanonicalRecords(extendedCandidate, extendedInput));
  ZC_EXPECT(CheckedFactsCanonicalCodec::recordsMatch(extendedCandidate, extendedInput));
  auto rejected = CheckedFactsVerifier::verify(zc::mv(extendedCandidate), extendedInput);
  ZC_EXPECT(rejected.is<CheckedFactsInvariantRejected>());
}

ZC_TEST("CheckedFactsVerifier.AcceptsCoherentProjectionWithWitnessLease") {
  CheckedFactsCodecFixture fixture;
  const auto behavior = fixture.definitionNamed("Behavior"_zc);
  const auto implementation = fixture.implementation();
  zc::Vector<identity::ImplId> coherentImplementations;
  coherentImplementations.add(implementation);
  zc::Vector<NodeFactRequirement> requirements;
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Projection, ast::NodeId(33), fixture.checkedNode(33)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Obligation, ast::NodeId(34), fixture.checkedNode(34)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Coercion, ast::NodeId(35), fixture.checkedNode(35)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Cast, ast::NodeId(36), fixture.checkedNode(36)});
  requirements.add(
      NodeFactRequirement{CheckedFactGroup::Call, ast::NodeId(37), fixture.checkedNode(37)});
  requirements.add(NodeFactRequirement{CheckedFactGroup::ErrorUnionShape, ast::NodeId(37),
                                       fixture.checkedNode(37)});
  const auto input =
      fixture.input(requirements.asPtr(), {}, {}, {}, {}, {}, coherentImplementations.asPtr());
  auto candidate = fixture.projectionCandidate(behavior, implementation);

  ZC_REQUIRE(CheckedFactsCanonicalCodec::writeCanonicalRecords(candidate, input));
  ZC_EXPECT(CheckedFactsCanonicalCodec::recordsMatch(candidate, input));
  auto verified = CheckedFactsVerifier::verify(zc::mv(candidate), input);
  ZC_EXPECT(verified.is<VerifiedCheckedFacts>());
}

ZC_TEST("CheckedFactsAlgebra.UsesNormativeClosedTags") {
  ZC_EXPECT(static_cast<uint8_t>(PrimitiveOperation::NullCoalesce) == 0x25);
  ZC_EXPECT(static_cast<uint8_t>(CompoundAssignmentOperation::NullCoalesceAssign) == 0x0f);
  ZC_EXPECT(static_cast<uint8_t>(CastKind::RawPointerReinterpret) == 0x0d);
  ZC_EXPECT(static_cast<uint8_t>(UnsafeOperation::PackedFieldAccess) == 0x05);
  ZC_EXPECT(static_cast<uint8_t>(ErrorOperatorKind::ForcedUnwrap) == 0x02);
}

ZC_TEST("CheckerAuthoritySession.RepeatedSkeletonMaterializationIsDeterministic") {
  for (size_t iteration = 0; iteration < 4; ++iteration) {
    CheckerAuthoritySession session("class RecoveryOwner {}\n"_zc);
    const auto& authority = session.identityAuthority();
    ZC_REQUIRE(authority.modules().size() != 0);
    ZC_EXPECT(authority.boundModule(session.module()) != zc::none);
  }
}

}  // namespace zomlang::compiler::checker::checked
