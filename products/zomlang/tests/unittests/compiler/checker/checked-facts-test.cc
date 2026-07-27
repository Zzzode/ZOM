// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/checked-facts.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/type/semantic-type-data.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::checker::checked {
namespace {

using namespace tests::test_identity_detail;

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

identity::SemanticContextFingerprint checkedFactsFingerprint(
    const identity::SemanticIdentityRegistrySet& registries) {
  zc::Vector<identity::ToolchainSemanticContextInput> toolchainInputs;
  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  zc::Vector<identity::CrateDependencyEdgeKey> crateEdges;
  auto result = identity::SemanticContextFingerprint::compute(
      registries, toolchainInputs.asPtr(), packageEdges.asPtr(), crateEdges.asPtr());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid checked-facts fingerprint fixture");
}

binder::ParsedModuleReceipt checkedFactsParsedReceipt(
    const identity::SemanticIdentityRegistrySet& registries) {
  const auto sourceBytes = tests::test_identity_detail::source().encode();
  const auto& snapshot = registries.sourceSnapshots()[0];
  const uint8_t astDump[] = {0x01};
  auto result =
      binder::ParsedModuleReceipt::compute(sourceBytes.asPtr(), snapshot.contentDigest(),
                                           snapshot.bytes().size(), digest(0x44), astDump);
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("invalid checked-facts parsed receipt fixture");
}

class CheckedFactsCodecFixture final {
public:
  CheckedFactsCodecFixture() {
    auto issued = factory.issue();
    ZC_REQUIRE(issued != zc::none);
    ZC_IF_SOME(value, issued) { context = value; }
    auto created = identity::SemanticIdentityRegistrySet::create(factory, context);
    ZC_REQUIRE(created != zc::none);
    ZC_IF_SOME(value, created) {
      registries = zc::heap<identity::SemanticIdentityRegistrySet>(zc::mv(value));
    }
    auto brands = factory.issueRegistryBrandIssuer(context);
    ZC_REQUIRE(brands != zc::none);
    ZC_IF_SOME(value, brands) {
      storeBrands = zc::heap<identity::RegistryBrandIssuer>(zc::mv(value));
    }

    ZC_REQUIRE(registries->collectCompilationUnit(userUnit()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
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
    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Maybe<identity::OverloadHeaderDigest> noOverloadDigest;
    auto definitionRecord = identity::DefinitionIdentityRecord::from(
        module(), zc::mv(owners), identity::DefinitionKind::Constant,
        identity::DefinitionNamespace::Value,
        scalar<identity::DeclaredDefinitionName>("constant"_zc), zc::mv(noOverloadDigest));
    ZC_REQUIRE(definitionRecord != zc::none);
    ZC_IF_SOME(value, definitionRecord) {
      retainedDefinition =
          zc::heap<identity::DefinitionKey>(identity::DefinitionKey::compute(value));
      zc::Maybe<identity::OverloadHeaderAuthority> noOverloadAuthority;
      ZC_REQUIRE(registries->collectDefinition(zc::mv(value), zc::mv(noOverloadAuthority)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries->freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCallableParameters() == identity::FrozenRegistryFailure::None);
    auto foundModule = registries->modules().find(module());
    ZC_REQUIRE(foundModule != zc::none);
    ZC_IF_SOME(value, foundModule) { moduleId = value; }
    auto foundDefinition = registries->definitions().find(*retainedDefinition);
    ZC_REQUIRE(foundDefinition != zc::none);
    ZC_IF_SOME(value, foundDefinition) { definitionId = value; }

    auto typeToken = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_REQUIRE(typeToken != zc::none);
    ZC_IF_SOME(value, typeToken) {
      semanticTypes = zc::heap<type::SemanticTypeStore>(zc::mv(value), *registries);
    }
    auto canonical = semanticTypes->canonicalizeClosed(type::semantic::TypeData(
        type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::I32}));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto interned =
        semanticTypes->intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
    ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
    i32 = interned.get<type::SemanticTypeInterned>().id;

    fingerprintValue =
        zc::heap<identity::SemanticContextFingerprint>(checkedFactsFingerprint(*registries));
    parsedReceipt = zc::heap<binder::ParsedModuleReceipt>(checkedFactsParsedReceipt(*registries));
    const auto moduleBytes = module().encode();
    const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
    auto signature = signature::SignatureFactsRevision::computeFramed(
        fingerprintValue->digest(), moduleBytes.asPtr(),
        registries->sourceSnapshots()[0].contentDigest(), digest(0x21), digest(0x22), emptyRecords,
        emptyRecords, emptyRecords);
    ZC_REQUIRE(signature != zc::none);
    ZC_IF_SOME(value, signature) {
      signatureRevision = zc::heap<signature::SignatureFactsRevision>(value);
    }
    auto imported = cross_module::ImportedSignatureViewRevision::computeFramed(
        fingerprintValue->digest(), moduleBytes.asPtr(), emptyRecords);
    ZC_REQUIRE(imported != zc::none);
    ZC_IF_SOME(value, imported) {
      importedRevision = zc::heap<cross_module::ImportedSignatureViewRevision>(value);
    }
    auto coherence = cross_module::CoherenceViewRevision::computeFramed(
        fingerprintValue->digest(), digest(0x23), emptyRecords, emptyRecords, emptyRecords);
    ZC_REQUIRE(coherence != zc::none);
    ZC_IF_SOME(value, coherence) {
      coherenceRevision = zc::heap<cross_module::CoherenceViewRevision>(value);
    }
  }

  CheckedNodeKey checkedNode(uint32_t preorder) const {
    auto span = registries->sourceSnapshots()[0].span(0, 1);
    ZC_IF_SOME(value, span) { return CheckedNodeKey{0x31, preorder, zc::mv(value)}; }
    ZC_FAIL_REQUIRE("invalid checked node fixture");
  }

  CheckedFactsVerificationInput input(
      zc::ArrayPtr<const NodeFactRequirement> nodeRequirements,
      zc::ArrayPtr<const DefinitionFactRequirement> definitionRequirements = {},
      zc::ArrayPtr<const CheckerFailureRef> registeredFailures = {},
      zc::ArrayPtr<const CaptureFactRequirement> captureRequirements = {},
      zc::ArrayPtr<const binder::FrozenOwnerLocalBindingEntry> ownerLocalBindings = {},
      zc::ArrayPtr<const binder::FrozenAnonymousEntityEntry> anonymousEntities = {}) const {
    const zc::ArrayPtr<const identity::DefId> importedDefinitions;
    const zc::ArrayPtr<const identity::ImplId> coherentImpls;
    return CheckedFactsVerificationInput{context,
                                         *fingerprintValue,
                                         moduleId,
                                         registries->sourceSnapshots()[0].source(),
                                         registries->sourceSnapshots()[0].contentDigest(),
                                         *parsedReceipt,
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
                                         *registries,
                                         *semanticTypes};
  }

  FrozenRecoveryLedger recoveryLedger() {
    auto brand = storeBrands->issue();
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
    auto substitutionBrand = storeBrands->issue();
    auto witnessBrand = storeBrands->issue();
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

  identity::SemanticTypeId semanticType() const noexcept { return i32; }
  identity::DefId definition() const noexcept { return definitionId; }
  identity::SemanticContextBrand semanticContext() const noexcept { return context; }
  identity::ModuleId moduleIdentity() const noexcept { return moduleId; }
  const identity::DefinitionKey& definitionKey() const noexcept { return *retainedDefinition; }

private:
  FrozenSubstitutionStore emptySubstitutions() {
    auto brand = storeBrands->issue();
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
    auto brand = storeBrands->issue();
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
                                 fingerprintValue->clone(),
                                 moduleId,
                                 registries->sourceSnapshots()[0].contentDigest(),
                                 *parsedReceipt,
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

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  zc::Own<identity::SemanticIdentityRegistrySet> registries;
  zc::Own<identity::RegistryBrandIssuer> storeBrands;
  zc::Own<type::SemanticTypeStore> semanticTypes;
  identity::ModuleId moduleId;
  identity::DefId definitionId;
  identity::SemanticTypeId i32;
  zc::Own<identity::DefinitionKey> retainedDefinition;
  zc::Own<identity::SemanticContextFingerprint> fingerprintValue;
  zc::Own<binder::ParsedModuleReceipt> parsedReceipt;
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

  zc::Vector<binder::FrozenAnonymousEntityEntry> anonymousEntities;
  anonymousEntities.add(binder::FrozenAnonymousEntityEntry{
      ast::NodeId(2), binder::DefinitionSite::declaration(ast::NodeId(2)), closure.clone(),
      sourceSpan.clone()});
  zc::Vector<NodeFactRequirement> nodeRequirements;
  nodeRequirements.add(
      NodeFactRequirement{CheckedFactGroup::NodeType, ast::NodeId(7), fixture.checkedNode(0)});

  zc::Vector<binder::FrozenOwnerLocalBindingEntry> firstBindings;
  firstBindings.add(binder::FrozenOwnerLocalBindingEntry{
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

  zc::Vector<binder::FrozenOwnerLocalBindingEntry> secondBindings;
  secondBindings.add(binder::FrozenOwnerLocalBindingEntry{
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
  zc::Vector<binder::FrozenAnonymousEntityEntry> functionExpressions;
  functionExpressions.add(binder::FrozenAnonymousEntityEntry{
      ast::NodeId(3), binder::DefinitionSite::declaration(ast::NodeId(3)),
      functionExpression.clone(), sourceSpan.clone()});
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

ZC_TEST("CheckedFactsAlgebra.UsesNormativeClosedTags") {
  ZC_EXPECT(static_cast<uint8_t>(PrimitiveOperation::NullCoalesce) == 0x25);
  ZC_EXPECT(static_cast<uint8_t>(CompoundAssignmentOperation::NullCoalesceAssign) == 0x0f);
  ZC_EXPECT(static_cast<uint8_t>(CastKind::RawPointerReinterpret) == 0x0d);
  ZC_EXPECT(static_cast<uint8_t>(UnsafeOperation::PackedFieldAccess) == 0x05);
  ZC_EXPECT(static_cast<uint8_t>(ErrorOperatorKind::ForcedUnwrap) == 0x02);
}

}  // namespace zomlang::compiler::checker::checked
