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

#include "zomlang/compiler/checker/signature-facts.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::checker::signature {
namespace {

using namespace tests::test_identity_detail;

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

uint8_t hexNibble(char value) {
  if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f') return static_cast<uint8_t>(value - 'a' + 10);
  ZC_FAIL_REQUIRE("invalid signature-facts hexadecimal fixture");
}

identity::Sha256Digest digestFromHex(zc::StringPtr text) {
  ZC_REQUIRE(text.size() == 64);
  uint8_t bytes[32];
  for (size_t index = 0; index < 32; ++index) {
    bytes[index] =
        static_cast<uint8_t>((hexNibble(text[index * 2]) << 4) | hexNibble(text[index * 2 + 1]));
  }
  auto result = identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("invalid signature-facts digest fixture");
}

zc::Array<uint8_t> bytesFromHex(zc::StringPtr text) {
  ZC_REQUIRE(text.size() % 2 == 0);
  auto bytes = zc::heapArray<uint8_t>(text.size() / 2);
  for (size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] =
        static_cast<uint8_t>((hexNibble(text[index * 2]) << 4) | hexNibble(text[index * 2 + 1]));
  }
  return bytes;
}

void expectTypeKeyPatternOracle(TypeKeyPattern&& pattern,
                                const identity::SemanticIdentityRegistrySet& registries,
                                zc::StringPtr expectedHex, zc::StringPtr expectedDigest) {
  auto key = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(pattern, registries);
  ZC_REQUIRE(key != zc::none);
  ZC_IF_SOME(value, key) {
    ZC_EXPECT(zc::encodeHex(value.bytes()) == expectedHex);
    auto digest = identity::sha256(value.bytes());
    ZC_REQUIRE(digest != zc::none);
    ZC_IF_SOME(actual, digest) { ZC_EXPECT(zc::encodeHex(actual.bytes()) == expectedDigest); }
    ZC_EXPECT(SignatureFactsCanonicalCodec::typeKeyPatternKeyIsCanonical(value, registries));
    const auto clone = value.clone();
    ZC_EXPECT(clone.bytes() == value.bytes());
    auto decoded = SignatureFactsCanonicalCodec::decodeTypeKeyPatternKey(value.bytes(), registries);
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(decodedValue, decoded) { ZC_EXPECT(decodedValue.bytes() == value.bytes()); }
  }
}

void expectRawOracle(zc::StringPtr expectedHex, zc::StringPtr expectedDigest) {
  auto bytes = bytesFromHex(expectedHex);
  auto digest = identity::sha256(bytes.asPtr());
  ZC_REQUIRE(digest != zc::none);
  ZC_IF_SOME(actual, digest) { ZC_EXPECT(zc::encodeHex(actual.bytes()) == expectedDigest); }
}

void expectTypeKeyDecodeRejected(zc::Array<uint8_t>&& bytes,
                                 const identity::SemanticIdentityRegistrySet& registries) {
  ZC_EXPECT(SignatureFactsCanonicalCodec::decodeTypeKeyPatternKey(bytes.asPtr(), registries) ==
            zc::none);
}

void expectImplPatternRoundTrip(const ImplPattern& pattern,
                                const identity::SemanticIdentityRegistrySet& registries) {
  auto key = SignatureFactsCanonicalCodec::makeImplPatternKey(pattern, registries);
  ZC_REQUIRE(key != zc::none);
  ZC_IF_SOME(value, key) {
    ZC_EXPECT(SignatureFactsCanonicalCodec::implPatternKeyIsCanonical(value, registries));
    auto decoded = SignatureFactsCanonicalCodec::decodeImplPatternKey(value.bytes(), registries);
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(decodedValue, decoded) { ZC_EXPECT(decodedValue.bytes() == value.bytes()); }
  }
}

void expectImplKeyDecodeRejected(zc::Array<uint8_t>&& bytes,
                                 const identity::SemanticIdentityRegistrySet& registries) {
  ZC_EXPECT(SignatureFactsCanonicalCodec::decodeImplPatternKey(bytes.asPtr(), registries) ==
            zc::none);
}

identity::SemanticContextFingerprint fingerprint(
    const identity::SemanticIdentityRegistrySet& registries) {
  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  zc::Vector<identity::CrateDependencyEdgeKey> crateEdges;
  auto result = identity::SemanticContextFingerprint::compute(registries, packageEdges.asPtr(),
                                                              crateEdges.asPtr());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid signature-facts fingerprint fixture");
}

binder::ParsedModuleReceipt parsedReceipt(const identity::SemanticIdentityRegistrySet& registries) {
  const auto sourceBytes = tests::test_identity_detail::source().encode();
  const auto& snapshot = registries.sourceSnapshots()[0];
  const auto parserSchema = digest(0x44);
  const uint8_t astDump[] = {0x01};
  auto result =
      binder::ParsedModuleReceipt::compute(sourceBytes.asPtr(), snapshot.contentDigest(),
                                           snapshot.bytes().size(), parserSchema, astDump);
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("invalid signature-facts parsed receipt fixture");
}

binder::ExportSurfaceRevision surfaceRevision(const identity::SemanticContextFingerprint& context) {
  const auto moduleBytes = module().encode();
  const auto packageBytes = package().encode();
  const uint8_t emptyMap[] = {0, 0, 0, 0, 0, 0, 0, 0};
  auto result = binder::ExportSurfaceRevision::computeFramed(
      context.digest(), moduleBytes.asPtr(), packageBytes.asPtr(), emptyMap, emptyMap);
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("invalid signature-facts surface revision fixture");
}

template <typename Handle>
Handle requireHandle(zc::Maybe<Handle>&& candidate) {
  ZC_IF_SOME(value, candidate) { return value; }
  ZC_FAIL_REQUIRE("signature-facts production identity lookup failed");
}

zc::StringPtr definitionName(uint32_t ordinal) {
  switch (ordinal) {
    case 0:
      return "signature0"_zc;
    case 1:
      return "Signature1"_zc;
    case 2:
      return "Signature2"_zc;
    case 3:
      return "Signature3"_zc;
    case 4:
      return "Signature4"_zc;
    case 5:
      return "Signature5"_zc;
    case 6:
      return "SIGNATURE6"_zc;
    default:
      ZC_FAIL_REQUIRE("invalid signature-facts definition ordinal");
  }
}

identity::CanonicalHeaderTypeSyntax unitHeaderType() {
  auto value = identity::CanonicalHeaderTypeSyntax::predefined(identity::PredefinedTypeKind::Unit);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid signature-facts unit header type");
}

identity::OverloadHeaderAuthority functionAuthority() {
  zc::Maybe<identity::ReceiverShape> receiver;
  zc::Vector<identity::CanonicalGenericParameter> generics;
  zc::Maybe<identity::CanonicalHeaderTypeSyntax> noDefault;
  generics.add(identity::CanonicalGenericParameter::from(zc::mv(noDefault)));
  zc::Vector<identity::CanonicalBoundObligation> obligations;
  zc::Vector<identity::CanonicalCallableParameter> parameters;
  parameters.add(identity::CanonicalCallableParameter::from(
      scalar<identity::SemanticIdentifier>("value"_zc), unitHeaderType(), false));
  zc::Maybe<zc::Vector<identity::CanonicalHeaderTypeSyntax>> raises;
  zc::Maybe<identity::ExternalAbi> abi;
  auto header = identity::CanonicalOverloadHeader::from(
      identity::CallableHeaderKind::Function,
      scalar<identity::DeclaredDefinitionName>(definitionName(0)), zc::mv(receiver),
      zc::mv(generics), zc::mv(obligations), zc::mv(parameters),
      identity::CanonicalCallableResult::unit(), zc::mv(raises), zc::mv(abi));
  ZC_IF_SOME(admitted, header) { return identity::OverloadHeaderAuthority::from(zc::mv(admitted)); }
  ZC_FAIL_REQUIRE("invalid signature-facts function header");
}

struct DefinitionFixture final {
  identity::DefinitionKind kind;
  identity::DefinitionKey key;
  identity::DefId id;
};

class SignatureFixture final {
public:
  SignatureFixture() {
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    ZC_IF_SOME(value, issuedContext) { context = value; }

    auto createdRegistries = identity::SemanticIdentityRegistrySet::create(factory, context);
    ZC_REQUIRE(createdRegistries != zc::none);
    ZC_IF_SOME(value, createdRegistries) {
      registries = zc::heap<identity::SemanticIdentityRegistrySet>(zc::mv(value));
    }
    auto token = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(value, token) {
      semanticTypes = zc::heap<type::SemanticTypeStore>(zc::mv(value), *registries);
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

    addDefinition(identity::DefinitionKind::Function, 0);
    addDefinition(identity::DefinitionKind::Class, 1);
    addDefinition(identity::DefinitionKind::Interface, 2);
    addDefinition(identity::DefinitionKind::TypeAlias, 3);
    addDefinition(identity::DefinitionKind::AssociatedType, 4);
    addDefinition(identity::DefinitionKind::EnumVariant, 5);
    addDefinition(identity::DefinitionKind::Constant, 6);
    ZC_REQUIRE(registries->freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    for (auto& definition : definitions) {
      auto handle = registries->definitions().find(definition.key);
      ZC_REQUIRE(handle != zc::none);
      ZC_IF_SOME(value, handle) { definition.id = value; }
    }
    auto genericRecord = identity::GenericParameterIdentityRecord::type(
        identity::StableGenericParameterOwnerKey::definition(definitions[0].key.clone()), 0);
    ZC_REQUIRE(registries->collectGenericParameter(zc::mv(genericRecord)) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    auto parameterRecord = identity::CallableParameterIdentityRecord::from(
        definitions[0].key.clone(), identity::CallableParameterPosition::ordinary(0));
    ZC_REQUIRE(registries->collectCallableParameter(zc::mv(parameterRecord)) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCallableParameters() == identity::FrozenRegistryFailure::None);

    auto moduleHandle = registries->modules().find(module());
    ZC_REQUIRE(moduleHandle != zc::none);
    ZC_IF_SOME(value, moduleHandle) { moduleId = value; }

    auto canonical = semanticTypes->canonicalizeClosed(type::semantic::TypeData(
        type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::Unit}));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto interned =
        semanticTypes->intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
    ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
    unitType = interned.get<type::SemanticTypeInterned>().id;

    contextFingerprint = zc::heap<identity::SemanticContextFingerprint>(fingerprint(*registries));
    receipt = zc::heap<binder::ParsedModuleReceipt>(parsedReceipt(*registries));
    surface = zc::heap<binder::ExportSurfaceRevision>(surfaceRevision(*contextFingerprint));
    zc::Vector<zc::ArrayPtr<const uint8_t>> noShapeRecords;
    auto shapeRevisionValue = MarkerShapeInventoryRevision::computeFramed(
        contextFingerprint->digest(), noShapeRecords.asPtr());
    ZC_REQUIRE(shapeRevisionValue != zc::none);
    ZC_IF_SOME(value, shapeRevisionValue) {
      shapeRevision = zc::heap<MarkerShapeInventoryRevision>(zc::mv(value));
    }
    const auto configuration = MarkerPolicyConfiguration::explicitOnly();
    auto policyRevisionValue = MarkerPolicyRegistryRevision::computeFramed(
        contextFingerprint->digest(), configuration.revision(), *shapeRevision,
        noShapeRecords.asPtr());
    ZC_REQUIRE(policyRevisionValue != zc::none);
    ZC_IF_SOME(value, policyRevisionValue) {
      policyRevision = zc::heap<MarkerPolicyRegistryRevision>(zc::mv(value));
    }
  }

  identity::DefId definition(identity::DefinitionKind kind) const {
    for (const auto& value : definitions) {
      if (value.kind == kind) return value.id;
    }
    ZC_FAIL_REQUIRE("missing signature-facts definition fixture");
  }

  identity::SourceSpan span() const {
    auto result = registries->sourceSnapshots()[0].span(0, 1);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
    ZC_FAIL_REQUIRE("invalid signature-facts source span fixture");
  }

  const identity::GenericParameterKey& genericParameter() const {
    ZC_IF_SOME(value, registries->genericParameters().keyAt(0)) { return value; }
    ZC_UNREACHABLE
  }

  const identity::CallableParameterKey& callableParameter() const {
    ZC_IF_SOME(value, registries->callableParameters().keyAt(0)) { return value; }
    ZC_UNREACHABLE
  }

  SemanticSignature functionSignature(zc::StringPtr parameterLabel, bool hasDefault,
                                      identity::SemanticTypeId success) const {
    zc::Vector<GenericParameterSignature> functionGenerics;
    zc::Maybe<identity::SemanticTypeId> noGenericDefault;
    functionGenerics.add(GenericParameterSignature{
        genericParameter().clone(), 0, zc::Vector<InterfaceInstantiation>(),
        zc::Vector<identity::DefId>(), zc::mv(noGenericDefault)});
    zc::Maybe<ReceiverSignature> noReceiver;
    zc::Vector<ParameterSignature> functionParameters;
    functionParameters.add(ParameterSignature{callableParameter().clone(),
                                              scalar<identity::SemanticIdentifier>(parameterLabel),
                                              unitType, ParameterMode::Value, hasDefault});
    zc::Maybe<identity::SemanticTypeId> noRaises;
    zc::Maybe<ExternAbi> noAbi;
    return SemanticSignature{
        definition(identity::DefinitionKind::Function),
        identity::DefinitionKind::Function,
        SignatureScope(ModuleDefinitionSignatureScope{}),
        zc::Vector<SignatureModifier>(),
        zc::Vector<NormalizedAttributeFact>(),
        SemanticSignaturePayload(CallableSignature{zc::mv(functionGenerics), zc::mv(noReceiver),
                                                   zc::mv(functionParameters), success,
                                                   zc::mv(noRaises), zc::mv(noAbi)}),
        span()};
  }

  SemanticSignature interfaceSignature(bool markerOnly) const {
    return SemanticSignature{
        definition(identity::DefinitionKind::Interface),
        identity::DefinitionKind::Interface,
        SignatureScope(ModuleDefinitionSignatureScope{}),
        zc::Vector<SignatureModifier>(),
        zc::Vector<NormalizedAttributeFact>(),
        SemanticSignaturePayload(InterfaceSignature{
            zc::Vector<GenericParameterSignature>(), zc::Vector<InterfaceInstantiation>(),
            zc::Vector<identity::DefId>(), zc::Vector<identity::DefId>(), markerOnly,
            zc::Vector<ObjectSafetyCause>()}),
        span()};
  }

  zc::Vector<SemanticSignature> completeSignatures() const {
    zc::Vector<SemanticSignature> result;
    result.add(functionSignature("value"_zc, false, unitType));

    zc::Maybe<identity::SemanticTypeId> noBase;
    result.add(SemanticSignature{
        definition(identity::DefinitionKind::Class), identity::DefinitionKind::Class,
        SignatureScope(ModuleDefinitionSignatureScope{}), zc::Vector<SignatureModifier>(),
        zc::Vector<NormalizedAttributeFact>(),
        SemanticSignaturePayload(
            NominalSignature{zc::Vector<GenericParameterSignature>(), zc::mv(noBase),
                             zc::Vector<InterfaceInstantiation>(), zc::Vector<identity::DefId>(),
                             zc::Vector<identity::DefId>(), zc::Vector<identity::DefId>()}),
        span()});

    result.add(interfaceSignature(false));

    result.add(SemanticSignature{
        definition(identity::DefinitionKind::TypeAlias), identity::DefinitionKind::TypeAlias,
        SignatureScope(ModuleDefinitionSignatureScope{}), zc::Vector<SignatureModifier>(),
        zc::Vector<NormalizedAttributeFact>(),
        SemanticSignaturePayload(
            TypeAliasSignature{zc::Vector<GenericParameterSignature>(), unitType}),
        span()});

    zc::Maybe<identity::SemanticTypeId> noDefault;
    result.add(SemanticSignature{
        definition(identity::DefinitionKind::AssociatedType),
        identity::DefinitionKind::AssociatedType,
        SignatureScope(EnclosedSignatureScope{definition(identity::DefinitionKind::Interface)}),
        zc::Vector<SignatureModifier>(), zc::Vector<NormalizedAttributeFact>(),
        SemanticSignaturePayload(AssociatedTypeSignature{
            zc::Vector<GenericParameterSignature>(), zc::Vector<InterfaceInstantiation>(),
            zc::Vector<identity::DefId>(), zc::mv(noDefault)}),
        span()});

    zc::Maybe<CanonicalInteger> noDiscriminant;
    result.add(SemanticSignature{
        definition(identity::DefinitionKind::EnumVariant), identity::DefinitionKind::EnumVariant,
        SignatureScope(EnclosedSignatureScope{definition(identity::DefinitionKind::Class)}),
        zc::Vector<SignatureModifier>(), zc::Vector<NormalizedAttributeFact>(),
        SemanticSignaturePayload(
            EnumVariantSignature{zc::Vector<identity::SemanticTypeId>(), zc::mv(noDiscriminant)}),
        span()});

    zc::Maybe<CanonicalConstValue> constantValue = CanonicalConstValue::integer(
        CanonicalInteger{IntegerSign::NonNegative, zc::heapArray<uint8_t>(0)});
    zc::Maybe<ExternAbi> noValueAbi;
    result.add(SemanticSignature{
        definition(identity::DefinitionKind::Constant), identity::DefinitionKind::Constant,
        SignatureScope(ModuleDefinitionSignatureScope{}), zc::Vector<SignatureModifier>(),
        zc::Vector<NormalizedAttributeFact>(),
        SemanticSignaturePayload(ValueSignature{unitType, Mutability::Const, true,
                                                zc::mv(constantValue), zc::mv(noValueAbi)}),
        span()});
    for (size_t index = 1; index < result.size(); ++index) {
      auto current = zc::mv(result[index]);
      size_t insertion = index;
      while (insertion > 0) {
        auto currentKey = registries->definitions().lookup(current.definition);
        auto previousKey = registries->definitions().lookup(result[insertion - 1].definition);
        ZC_REQUIRE(currentKey != zc::none);
        ZC_REQUIRE(previousKey != zc::none);
        bool currentBeforePrevious = false;
        ZC_IF_SOME(left, currentKey) {
          ZC_IF_SOME(right, previousKey) {
            currentBeforePrevious = lessBytes(left.bytes(), right.bytes());
          }
        }
        if (!currentBeforePrevious) break;
        result[insertion] = zc::mv(result[insertion - 1]);
        --insertion;
      }
      result[insertion] = zc::mv(current);
    }
    return result;
  }

  zc::Vector<SignatureDefinitionRequirement> requirements(
      zc::ArrayPtr<const SemanticSignature> signatures) const {
    zc::Vector<SignatureDefinitionRequirement> result;
    for (const auto& signature : signatures) {
      auto record = SignatureFactsCanonicalCodec::encodeSignature(signature, moduleId, *registries,
                                                                  *semanticTypes);
      ZC_REQUIRE(record != zc::none);
      ZC_IF_SOME(bytes, record) {
        result.add(SignatureDefinitionRequirement{signature.definition, signature.definitionKind,
                                                  zc::mv(bytes)});
      }
    }
    return result;
  }

  SignatureFactsCandidate candidate(zc::Vector<SemanticSignature>&& signatures) const {
    return SignatureFactsCandidate{context,
                                   fingerprint(*registries),
                                   moduleId,
                                   registries->sourceSnapshots()[0].contentDigest(),
                                   parsedReceipt(*registries),
                                   surfaceRevision(*contextFingerprint),
                                   *policyRevision,
                                   zc::mv(signatures),
                                   zc::Vector<ImplHead>(),
                                   zc::Vector<MarkerFact>()};
  }

  SignatureFactsVerificationResult verifyAgainst(
      SignatureFactsCandidate&& candidate, zc::ArrayPtr<const SemanticSignature> recordSignatures,
      zc::ArrayPtr<const SemanticSignature> censusSignatures) const {
    auto expected = requirements(recordSignatures);
    zc::Vector<SignatureDefinitionCensusEntry> sourceSignatureCensus;
    for (const auto& signature : censusSignatures) {
      sourceSignatureCensus.add(
          SignatureDefinitionCensusEntry{signature.definition, signature.definitionKind});
    }
    zc::Vector<ImplAuthorityCensusEntry> sourceImplCensus;
    zc::Vector<ImplHeadRequirement> noImpls;
    zc::Vector<MarkerFactRequirement> noMarkers;
    return SignatureFactsVerifier::verify(
        zc::mv(candidate),
        SignatureFactsVerificationInput{
            context, *contextFingerprint, moduleId, registries->sourceSnapshots()[0].source(),
            registries->sourceSnapshots()[0].contentDigest(), *receipt, *surface, *policyRevision,
            sourceSignatureCensus.asPtr(), sourceImplCensus.asPtr(), expected.asPtr(),
            noImpls.asPtr(), noMarkers.asPtr(), *registries, *semanticTypes});
  }

  SignatureFactsVerificationResult verify(SignatureFactsCandidate&& candidate) const {
    auto expectedSignatures = completeSignatures();
    return verifyAgainst(zc::mv(candidate), expectedSignatures.asPtr(), expectedSignatures.asPtr());
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  zc::Own<identity::SemanticIdentityRegistrySet> registries;
  zc::Own<type::SemanticTypeStore> semanticTypes;
  identity::ModuleId moduleId;
  identity::SemanticTypeId unitType;
  zc::Vector<DefinitionFixture> definitions;
  zc::Own<identity::SemanticContextFingerprint> contextFingerprint;
  zc::Own<binder::ParsedModuleReceipt> receipt;
  zc::Own<binder::ExportSurfaceRevision> surface;
  zc::Own<MarkerShapeInventoryRevision> shapeRevision;
  zc::Own<MarkerPolicyRegistryRevision> policyRevision;

private:
  void addDefinition(identity::DefinitionKind kind, uint32_t ordinal) {
    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Maybe<identity::OverloadHeaderDigest> overloadDigest;
    zc::Maybe<identity::OverloadHeaderAuthority> overloadAuthority;
    if (kind == identity::DefinitionKind::Function) {
      auto authority = functionAuthority();
      overloadDigest = authority.digest().clone();
      overloadAuthority = zc::mv(authority);
    }
    auto nameSpace = identity::definitionNamespaceFor(kind);
    ZC_REQUIRE(nameSpace != zc::none);
    zc::Maybe<identity::DefinitionIdentityRecord> record;
    ZC_IF_SOME(value, nameSpace) {
      record = identity::DefinitionIdentityRecord::from(
          module(), zc::mv(owners), kind, value,
          scalar<identity::DeclaredDefinitionName>(definitionName(ordinal)),
          zc::mv(overloadDigest));
    }
    ZC_REQUIRE(record != zc::none);
    ZC_IF_SOME(value, record) {
      auto key = identity::DefinitionKey::compute(value);
      definitions.add(DefinitionFixture{kind, key.clone(), identity::DefId()});
      ZC_REQUIRE(registries->collectDefinition(zc::mv(value), zc::mv(overloadAuthority), ordinal) ==
                 identity::FrozenRegistryFailure::None);
    }
  }
};

const CheckerInvariantFact& checkerFailure(const SignatureFactsVerificationResult& result) {
  ZC_REQUIRE(result.is<SignatureFactsInvariantRejected>());
  const auto& rejected = result.get<SignatureFactsInvariantRejected>();
  ZC_REQUIRE(rejected.failures.size() == 1);
  ZC_REQUIRE(rejected.failures[0].variant().is<CheckerInvariantFact>());
  return rejected.failures[0].variant().get<CheckerInvariantFact>();
}

const identity::IdentityInvariant& identityFailure(const SignatureFactsVerificationResult& result) {
  ZC_REQUIRE(result.is<SignatureFactsInvariantRejected>());
  const auto& rejected = result.get<SignatureFactsInvariantRejected>();
  ZC_REQUIRE(rejected.failures.size() == 1);
  ZC_REQUIRE(rejected.failures[0].variant().is<identity::IdentityInvariant>());
  return rejected.failures[0].variant().get<identity::IdentityInvariant>();
}

}  // namespace

ZC_TEST("SignatureFactsRevision.MatchesNormativeNonEmptyFramingOracle") {
  const auto zero = digest(0x00);
  const auto sourceDigest = digest(0x22);
  const auto surfaceDigest = digest(0x33);
  const auto policyDigest = digest(0x77);
  const uint8_t moduleBytes[] = {0xa1};
  const uint8_t signatureBytes[] = {0xb2, 0x01, 0x03};
  const zc::ArrayPtr<const uint8_t> signatures[] = {zc::arrayPtr(signatureBytes)};
  zc::Vector<zc::ArrayPtr<const uint8_t>> noRecords;

  auto revision = SignatureFactsRevision::computeFramed(zero, moduleBytes, sourceDigest,
                                                        surfaceDigest, policyDigest, signatures,
                                                        noRecords.asPtr(), noRecords.asPtr());

  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "dac5b3c2ce95be20cf3c42028d5a05a042c504dc8d37a4d45f7ec97b7955b4a4"_zc);
  }
}

ZC_TEST("MarkerShapeInventoryRevision.MatchesNormativeNonEmptyFramingOracle") {
  const auto zero = digest(0x00);
  const uint8_t shapeBytes[] = {0xa1, 0x03};
  const zc::ArrayPtr<const uint8_t> shapeRecords[] = {zc::arrayPtr(shapeBytes)};

  auto revision = MarkerShapeInventoryRevision::computeFramed(zero, shapeRecords);

  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "1594af0c3d3f1cd1c3d5e58ce672673855b6924ceced83e78bf4306c77dc7e7b"_zc);
  }
}

ZC_TEST("MarkerPolicyRegistryRevision.MatchesNormativeNonEmptyFramingOracle") {
  const auto zero = digest(0x00);
  const uint8_t shapeBytes[] = {0xa1, 0x03};
  const zc::ArrayPtr<const uint8_t> shapeRecords[] = {zc::arrayPtr(shapeBytes)};
  auto shapeRevision = MarkerShapeInventoryRevision::computeFramed(zero, shapeRecords);
  ZC_REQUIRE(shapeRevision != zc::none);
  const auto configurationRevision =
      digestFromHex("7b17b923e4931f81d8fc06e17db18786d8e665623e83c87093aeba9493fc1dba"_zc);
  const auto entry =
      bytesFromHex("a100000000000000020104000000000000000103000000000000000101b2"_zc);
  const zc::ArrayPtr<const uint8_t> entryRecords[] = {entry.asPtr()};

  ZC_IF_SOME(shape, shapeRevision) {
    auto revision = MarkerPolicyRegistryRevision::computeFramed(zero, configurationRevision, shape,
                                                                entryRecords);
    ZC_REQUIRE(revision != zc::none);
    ZC_IF_SOME(value, revision) {
      ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
                "15329853e2faae147a2f5ca73c85a58c4084c70faa3d1faef278c856fd75067b"_zc);
    }
  }
}

ZC_TEST("SignatureFactsVerifier.VerifiesAllSevenStableSemanticSignatureBranches") {
  SignatureFixture fixture;
  auto result = fixture.verify(fixture.candidate(fixture.completeSignatures()));

  ZC_REQUIRE(result.is<VerifiedSignatureFacts>());
  const auto& facts = result.get<VerifiedSignatureFacts>();
  ZC_EXPECT(facts.semanticContext() == fixture.context);
  ZC_EXPECT(facts.module() == fixture.moduleId);
  ZC_EXPECT(facts.signatures().size() == 7);
  ZC_EXPECT(facts.revision().digest() != identity::Sha256Digest());
}

ZC_TEST("SignatureFactsVerifier.RejectsMissingAndKindMismatchedSignatures") {
  SignatureFixture fixture;
  auto missing = fixture.completeSignatures();
  missing.removeLast();
  auto missingResult = fixture.verify(fixture.candidate(zc::mv(missing)));
  ZC_EXPECT(checkerFailure(missingResult).kind == CheckerInvariantKind::MissingRequiredFact);

  auto mismatched = fixture.completeSignatures();
  mismatched[0].definitionKind = identity::DefinitionKind::Class;
  auto mismatchResult = fixture.verify(fixture.candidate(zc::mv(mismatched)));
  ZC_EXPECT(checkerFailure(mismatchResult).kind == CheckerInvariantKind::InvalidFact);
}

ZC_TEST("SignatureFactsVerifier.RejectsProducerAndRequirementSharedOmission") {
  SignatureFixture fixture;
  auto candidateSignatures = fixture.completeSignatures();
  candidateSignatures.removeLast();
  auto recordSignatures = fixture.completeSignatures();
  recordSignatures.removeLast();
  auto censusSignatures = fixture.completeSignatures();

  auto result = fixture.verifyAgainst(fixture.candidate(zc::mv(candidateSignatures)),
                                      recordSignatures.asPtr(), censusSignatures.asPtr());

  ZC_EXPECT(checkerFailure(result).kind == CheckerInvariantKind::MissingRequiredFact);
}

ZC_TEST("SignatureFactsVerifier.RejectsNonCanonicalMapOrder") {
  SignatureFixture fixture;
  auto signatures = fixture.completeSignatures();
  auto first = zc::mv(signatures[0]);
  signatures[0] = zc::mv(signatures[1]);
  signatures[1] = zc::mv(first);

  auto result = fixture.verify(fixture.candidate(zc::mv(signatures)));

  ZC_EXPECT(checkerFailure(result).kind == CheckerInvariantKind::CanonicalCodecMismatch);
}

ZC_TEST("SignatureFactsVerifier.RejectsCallableAuthorityDrift") {
  SignatureFixture fixture;
  auto labelMismatch = fixture.completeSignatures();
  for (size_t index = 0; index < labelMismatch.size(); ++index) {
    if (labelMismatch[index].definitionKind == identity::DefinitionKind::Function) {
      labelMismatch[index] = fixture.functionSignature("renamed"_zc, false, fixture.unitType);
      break;
    }
  }
  auto labelResult = fixture.verify(fixture.candidate(zc::mv(labelMismatch)));
  ZC_EXPECT(checkerFailure(labelResult).kind == CheckerInvariantKind::CanonicalCodecMismatch);

  auto defaultMismatch = fixture.completeSignatures();
  for (size_t index = 0; index < defaultMismatch.size(); ++index) {
    if (defaultMismatch[index].definitionKind == identity::DefinitionKind::Function) {
      defaultMismatch[index] = fixture.functionSignature("value"_zc, true, fixture.unitType);
      break;
    }
  }
  auto defaultResult = fixture.verify(fixture.candidate(zc::mv(defaultMismatch)));
  ZC_EXPECT(checkerFailure(defaultResult).kind == CheckerInvariantKind::CanonicalCodecMismatch);
}

ZC_TEST("SignatureFactsVerifier.RejectsCanonicalWholeRecordReplacement") {
  SignatureFixture fixture;
  auto signatures = fixture.completeSignatures();
  for (size_t index = 0; index < signatures.size(); ++index) {
    if (signatures[index].definitionKind == identity::DefinitionKind::Interface) {
      signatures[index] = fixture.interfaceSignature(true);
      break;
    }
  }

  auto result = fixture.verify(fixture.candidate(zc::mv(signatures)));

  ZC_EXPECT(checkerFailure(result).kind == CheckerInvariantKind::CanonicalCodecMismatch);
}

ZC_TEST("SignatureFactsVerifier.PreservesExactSemanticTypeIdentityInvariant") {
  SignatureFixture fixture;
  auto signatures = fixture.completeSignatures();
  size_t functionIndex = signatures.size();
  for (size_t index = 0; index < signatures.size(); ++index) {
    if (signatures[index].definitionKind == identity::DefinitionKind::Function) {
      functionIndex = index;
      break;
    }
  }
  ZC_REQUIRE(functionIndex < signatures.size());
  signatures[functionIndex] =
      fixture.functionSignature("value"_zc, false, identity::SemanticTypeId());

  auto result = fixture.verify(fixture.candidate(zc::mv(signatures)));

  ZC_EXPECT(identityFailure(result).kind() == identity::IdentityInvariantKind::InvalidHandle);
  ZC_EXPECT(identityFailure(result).phase() == identity::IdentityAllocationPhase::SemanticType);
}

ZC_TEST("SignatureFactsCanonicalCodec.MatchesIdentityFreePatternGoldenVectors") {
  SignatureFixture fixture;
  expectTypeKeyPatternOracle(TypeKeyPattern::parameter(0), *fixture.registries,
                             "7a6f6d2e747970652d6b65792d7061747465726e2e7631001100000000"_zc,
                             "0c9d8c3a3d5ccff890dbed8dad7b7270cf580bee6a024e610107aa99dfb5a022"_zc);
  expectTypeKeyPatternOracle(
      TypeKeyPattern::reference(Mutability::Const, TypeKeyPattern::parameter(0)),
      *fixture.registries, "7a6f6d2e747970652d6b65792d7061747465726e2e7631000c011100000000"_zc,
      "71cd89aa34ed4343c7f092700dba2b313f817348b542acbd33db33c4c880fdcd"_zc);
}

ZC_TEST("SignatureFactsCanonicalCodec.MatchesRemainingNormativeRawPatternVectors") {
  expectRawOracle("7a6f6d2e747970652d6b65792d7061747465726e2e76310010a1"_zc,
                  "2386a3fdf91952907a4e8a486bca5763fda467aa4d4fd52bd50fe2c20ffc4fc5"_zc);
  expectRawOracle(
      "7a6f6d2e747970652d6b65792d7061747465726e2e76310008a100000000000000011100000000"_zc,
      "c4951246c3831b62b19f245d604801351fc2545a6abb4a0136a8551ee1963e18"_zc);
  expectRawOracle("7a6f6d2e696d706c2d7061747465726e2e763100a100000000000000001100000000"_zc,
                  "f81b1b7dbbb67d1c9bb9209864d4cf36166ffdf2ce3e0b83fb4033d92dae4703"_zc);
  expectRawOracle(
      "7a6f6d2e696d706c2d7061747465726e2e763100a10000000000000001110000000008b200000000000000011100000000"_zc,
      "89cd999fa5df60dc6bd7a811213b5c2f954fef780624ec82e0e94059a138107e"_zc);
}

ZC_TEST("SignatureFactsCanonicalCodec.EncodesNonEmptyRecursivePatternFamilies") {
  SignatureFixture fixture;
  const auto interface = fixture.definition(identity::DefinitionKind::Interface);
  const auto nominal = fixture.definition(identity::DefinitionKind::Class);
  const auto associated = fixture.definition(identity::DefinitionKind::AssociatedType);
  const auto marker = fixture.definition(identity::DefinitionKind::TypeAlias);

  zc::Vector<TypeKeyPattern> patterns;

  zc::Vector<PatternObjectField> fields;
  fields.add(PatternObjectField{scalar<identity::SemanticIdentifier>("field"_zc),
                                TypeKeyPattern::primitive(PrimitiveKind::Unit), Mutability::Const,
                                FieldPresence::Required});
  patterns.add(TypeKeyPattern::object(zc::mv(fields)));

  zc::Vector<TypeKeyPattern> parameters;
  parameters.add(TypeKeyPattern::parameter(0));
  zc::Maybe<TypeKeyPattern> raises;
  raises = TypeKeyPattern::primitive(PrimitiveKind::Null);
  patterns.add(TypeKeyPattern::function(PatternFunctionType{
      zc::mv(parameters), TypeKeyPattern::primitive(PrimitiveKind::Bool), zc::mv(raises)}));

  zc::Vector<TypeKeyPattern> principalArguments;
  principalArguments.add(TypeKeyPattern::parameter(0));
  zc::Vector<PatternExistentialInterface> additionalInterfaces;
  zc::Vector<TypeKeyPattern> additionalArguments;
  additionalArguments.add(TypeKeyPattern::primitive(PrimitiveKind::Unit));
  additionalInterfaces.add(PatternExistentialInterface{nominal, zc::mv(additionalArguments)});
  zc::Vector<identity::DefId> markers;
  markers.add(marker);
  zc::Vector<PatternAssociatedTypeBinding> associatedBindings;
  associatedBindings.add(
      PatternAssociatedTypeBinding{associated, TypeKeyPattern::primitive(PrimitiveKind::Bool)});
  patterns.add(TypeKeyPattern::existential(PatternExistentialType{
      PatternExistentialInterface{interface, zc::mv(principalArguments)},
      zc::mv(additionalInterfaces), zc::mv(markers), zc::mv(associatedBindings)}));

  zc::Vector<TypeKeyPattern> boundArguments;
  boundArguments.add(TypeKeyPattern::parameter(0));
  patterns.add(TypeKeyPattern::interfaceBound(
      PatternInterfaceInstantiation{interface, zc::mv(boundArguments)}));

  zc::Vector<TypeKeyPattern> alternatives;
  alternatives.add(TypeKeyPattern::primitive(PrimitiveKind::Unit));
  alternatives.add(TypeKeyPattern::parameter(0));
  patterns.add(TypeKeyPattern::unionOf(zc::mv(alternatives)));

  zc::Vector<TypeKeyPattern> conjuncts;
  conjuncts.add(TypeKeyPattern::primitive(PrimitiveKind::Bool));
  conjuncts.add(TypeKeyPattern::parameter(1));
  patterns.add(TypeKeyPattern::intersection(zc::mv(conjuncts)));

  for (const auto& pattern : patterns) {
    auto key = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(pattern, *fixture.registries);
    ZC_REQUIRE(key != zc::none);
    ZC_IF_SOME(value, key) {
      ZC_EXPECT(
          SignatureFactsCanonicalCodec::typeKeyPatternKeyIsCanonical(value, *fixture.registries));
      auto decoded =
          SignatureFactsCanonicalCodec::decodeTypeKeyPatternKey(value.bytes(), *fixture.registries);
      ZC_REQUIRE(decoded != zc::none);
      ZC_IF_SOME(decodedValue, decoded) { ZC_EXPECT(decodedValue.bytes() == value.bytes()); }
    }
  }
}

ZC_TEST("SignatureFactsCanonicalCodec.RejectsMalformedEncodedPatternKeys") {
  SignatureFixture fixture;
  const auto payloadOffset = zc::StringPtr("zom.type-key-pattern.v1").size() + 1;

  auto parameter = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(TypeKeyPattern::parameter(0),
                                                                       *fixture.registries);
  ZC_REQUIRE(parameter != zc::none);
  ZC_IF_SOME(value, parameter) {
    auto wrongDomain = zc::heapArray<uint8_t>(value.bytes());
    wrongDomain[0] ^= 0x01;
    expectTypeKeyDecodeRejected(zc::mv(wrongDomain), *fixture.registries);

    auto wrongDelimiter = zc::heapArray<uint8_t>(value.bytes());
    wrongDelimiter[payloadOffset - 1] = 0x01;
    expectTypeKeyDecodeRejected(zc::mv(wrongDelimiter), *fixture.registries);

    auto unknownTag = zc::heapArray<uint8_t>(value.bytes());
    unknownTag[payloadOffset] = 0xff;
    expectTypeKeyDecodeRejected(zc::mv(unknownTag), *fixture.registries);

    auto truncated = zc::heapArray<uint8_t>(value.bytes().slice(0, value.bytes().size() - 1));
    expectTypeKeyDecodeRejected(zc::mv(truncated), *fixture.registries);

    zc::Vector<uint8_t> trailing(value.bytes().size() + 1);
    trailing.addAll(value.bytes());
    trailing.add(0x00);
    expectTypeKeyDecodeRejected(trailing.releaseAsArray(), *fixture.registries);
  }

  zc::Vector<TypeKeyPattern> tupleElements;
  tupleElements.add(TypeKeyPattern::parameter(0));
  auto tuple = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(
      TypeKeyPattern::tuple(zc::mv(tupleElements)), *fixture.registries);
  ZC_REQUIRE(tuple != zc::none);
  ZC_IF_SOME(value, tuple) {
    auto invalidCount = zc::heapArray<uint8_t>(value.bytes());
    invalidCount[payloadOffset + 8] = 0x02;
    expectTypeKeyDecodeRejected(zc::mv(invalidCount), *fixture.registries);
  }

  auto function = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(
      TypeKeyPattern::function(PatternFunctionType{zc::Vector<TypeKeyPattern>(),
                                                   TypeKeyPattern::primitive(PrimitiveKind::Bool),
                                                   zc::Maybe<TypeKeyPattern>()}),
      *fixture.registries);
  ZC_REQUIRE(function != zc::none);
  ZC_IF_SOME(value, function) {
    auto invalidOptional = zc::heapArray<uint8_t>(value.bytes());
    invalidOptional[invalidOptional.size() - 1] = 0x02;
    expectTypeKeyDecodeRejected(zc::mv(invalidOptional), *fixture.registries);
  }

  const auto interface = fixture.definition(identity::DefinitionKind::Interface);
  auto interfaceSelf = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(
      TypeKeyPattern::interfaceSelf(interface), *fixture.registries);
  ZC_REQUIRE(interfaceSelf != zc::none);
  ZC_IF_SOME(value, interfaceSelf) {
    auto unknownIdentity = zc::heapArray<uint8_t>(value.bytes());
    unknownIdentity[payloadOffset + 1] ^= 0xff;
    expectTypeKeyDecodeRejected(zc::mv(unknownIdentity), *fixture.registries);
  }

  zc::Vector<TypeKeyPattern> alternatives;
  alternatives.add(TypeKeyPattern::primitive(PrimitiveKind::I8));
  alternatives.add(TypeKeyPattern::primitive(PrimitiveKind::I16));
  auto unionKey = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(
      TypeKeyPattern::unionOf(zc::mv(alternatives)), *fixture.registries);
  ZC_REQUIRE(unionKey != zc::none);
  ZC_IF_SOME(value, unionKey) {
    const size_t recordsOffset = payloadOffset + 1 + 8;
    auto duplicate = zc::heapArray<uint8_t>(value.bytes());
    duplicate[recordsOffset + 3] = duplicate[recordsOffset + 1];
    expectTypeKeyDecodeRejected(zc::mv(duplicate), *fixture.registries);

    auto reversed = zc::heapArray<uint8_t>(value.bytes());
    const auto firstTag = reversed[recordsOffset];
    const auto firstKind = reversed[recordsOffset + 1];
    reversed[recordsOffset] = reversed[recordsOffset + 2];
    reversed[recordsOffset + 1] = reversed[recordsOffset + 3];
    reversed[recordsOffset + 2] = firstTag;
    reversed[recordsOffset + 3] = firstKind;
    expectTypeKeyDecodeRejected(zc::mv(reversed), *fixture.registries);
  }

  zc::Vector<PatternObjectField> fields;
  fields.add(PatternObjectField{scalar<identity::SemanticIdentifier>("a"_zc),
                                TypeKeyPattern::primitive(PrimitiveKind::Bool), Mutability::Const,
                                FieldPresence::Required});
  fields.add(PatternObjectField{scalar<identity::SemanticIdentifier>("b"_zc),
                                TypeKeyPattern::primitive(PrimitiveKind::Bool), Mutability::Const,
                                FieldPresence::Required});
  auto object = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(
      TypeKeyPattern::object(zc::mv(fields)), *fixture.registries);
  ZC_REQUIRE(object != zc::none);
  ZC_IF_SOME(value, object) {
    const size_t recordsOffset = payloadOffset + 1 + 8;
    constexpr size_t recordSize = 13;
    auto invalidLength = zc::heapArray<uint8_t>(value.bytes());
    invalidLength[recordsOffset + 7] = 0x02;
    expectTypeKeyDecodeRejected(zc::mv(invalidLength), *fixture.registries);

    auto reversed = zc::heapArray<uint8_t>(value.bytes());
    for (size_t index = 0; index < recordSize; ++index) {
      const auto first = reversed[recordsOffset + index];
      reversed[recordsOffset + index] = reversed[recordsOffset + recordSize + index];
      reversed[recordsOffset + recordSize + index] = first;
    }
    expectTypeKeyDecodeRejected(zc::mv(reversed), *fixture.registries);
  }
}

ZC_TEST("SignatureFactsCanonicalCodec.RejectsNonCanonicalPatternCandidates") {
  SignatureFixture fixture;

  auto invalidPrimitive = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(
      TypeKeyPattern::primitive(static_cast<PrimitiveKind>(0xff)), *fixture.registries);
  ZC_EXPECT(invalidPrimitive == zc::none);

  auto invalidIdentity = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(
      TypeKeyPattern::interfaceSelf(identity::DefId()), *fixture.registries);
  ZC_EXPECT(invalidIdentity == zc::none);

  zc::Vector<TypeKeyPattern> duplicateAlternatives;
  duplicateAlternatives.add(TypeKeyPattern::primitive(PrimitiveKind::Unit));
  duplicateAlternatives.add(TypeKeyPattern::primitive(PrimitiveKind::Unit));
  auto duplicateUnion = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(
      TypeKeyPattern::unionOf(zc::mv(duplicateAlternatives)), *fixture.registries);
  ZC_EXPECT(duplicateUnion == zc::none);

  zc::Vector<TypeKeyPattern> reorderedConjuncts;
  reorderedConjuncts.add(TypeKeyPattern::parameter(0));
  reorderedConjuncts.add(TypeKeyPattern::primitive(PrimitiveKind::Bool));
  auto reorderedIntersection = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(
      TypeKeyPattern::intersection(zc::mv(reorderedConjuncts)), *fixture.registries);
  ZC_EXPECT(reorderedIntersection == zc::none);

  zc::Vector<PatternObjectField> duplicateFields;
  duplicateFields.add(PatternObjectField{scalar<identity::SemanticIdentifier>("field"_zc),
                                         TypeKeyPattern::primitive(PrimitiveKind::Bool),
                                         Mutability::Const, FieldPresence::Required});
  duplicateFields.add(PatternObjectField{scalar<identity::SemanticIdentifier>("field"_zc),
                                         TypeKeyPattern::primitive(PrimitiveKind::Unit),
                                         Mutability::Const, FieldPresence::Required});
  auto duplicateObject = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(
      TypeKeyPattern::object(zc::mv(duplicateFields)), *fixture.registries);
  ZC_EXPECT(duplicateObject == zc::none);

  const auto interface = fixture.definition(identity::DefinitionKind::Interface);
  const auto associated = fixture.definition(identity::DefinitionKind::AssociatedType);
  zc::Vector<PatternAssociatedTypeBinding> duplicateBindings;
  duplicateBindings.add(
      PatternAssociatedTypeBinding{associated, TypeKeyPattern::primitive(PrimitiveKind::Bool)});
  duplicateBindings.add(
      PatternAssociatedTypeBinding{associated, TypeKeyPattern::primitive(PrimitiveKind::Unit)});
  auto duplicateAssociatedBinding = SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(
      TypeKeyPattern::existential(PatternExistentialType{
          PatternExistentialInterface{interface, zc::Vector<TypeKeyPattern>()},
          zc::Vector<PatternExistentialInterface>(), zc::Vector<identity::DefId>(),
          zc::mv(duplicateBindings)}),
      *fixture.registries);
  ZC_EXPECT(duplicateAssociatedBinding == zc::none);
}

ZC_TEST("SignatureFactsCanonicalCodec.RejectsMalformedEncodedImplPatternKeys") {
  SignatureFixture fixture;
  const auto interface = fixture.definition(identity::DefinitionKind::Interface);
  const ImplPattern pattern{PatternInterfaceInstantiation{interface, zc::Vector<TypeKeyPattern>()},
                            TypeKeyPattern::parameter(0)};
  auto key = SignatureFactsCanonicalCodec::makeImplPatternKey(pattern, *fixture.registries);
  ZC_REQUIRE(key != zc::none);
  ZC_IF_SOME(value, key) {
    const auto payloadOffset = zc::StringPtr("zom.impl-pattern.v1").size() + 1;

    auto wrongDomain = zc::heapArray<uint8_t>(value.bytes());
    wrongDomain[0] ^= 0x01;
    expectImplKeyDecodeRejected(zc::mv(wrongDomain), *fixture.registries);

    auto wrongDelimiter = zc::heapArray<uint8_t>(value.bytes());
    wrongDelimiter[payloadOffset - 1] = 0x01;
    expectImplKeyDecodeRejected(zc::mv(wrongDelimiter), *fixture.registries);

    auto unknownInterface = zc::heapArray<uint8_t>(value.bytes());
    unknownInterface[payloadOffset] ^= 0xff;
    expectImplKeyDecodeRejected(zc::mv(unknownInterface), *fixture.registries);

    auto invalidArgumentCount = zc::heapArray<uint8_t>(value.bytes());
    invalidArgumentCount[payloadOffset + 39] = 0x01;
    expectImplKeyDecodeRejected(zc::mv(invalidArgumentCount), *fixture.registries);

    auto invalidSelfTag = zc::heapArray<uint8_t>(value.bytes());
    invalidSelfTag[payloadOffset + 40] = 0xff;
    expectImplKeyDecodeRejected(zc::mv(invalidSelfTag), *fixture.registries);

    auto truncated = zc::heapArray<uint8_t>(value.bytes().slice(0, value.bytes().size() - 1));
    expectImplKeyDecodeRejected(zc::mv(truncated), *fixture.registries);

    zc::Vector<uint8_t> trailing(value.bytes().size() + 1);
    trailing.addAll(value.bytes());
    trailing.add(0x00);
    expectImplKeyDecodeRejected(trailing.releaseAsArray(), *fixture.registries);

    auto differentParameter = zc::heapArray<uint8_t>(value.bytes());
    differentParameter[differentParameter.size() - 1] = 0x01;
    auto decoded = SignatureFactsCanonicalCodec::decodeImplPatternKey(differentParameter.asPtr(),
                                                                      *fixture.registries);
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(decodedValue, decoded) {
      ZC_EXPECT(decodedValue.bytes() == differentParameter.asPtr());
      ZC_EXPECT(decodedValue.bytes() != value.bytes());
    }
  }
}

ZC_TEST("SignatureFactsCanonicalCodec.UnifiesCompleteImplPatternsWithDisjointParameters") {
  SignatureFixture fixture;
  const auto interface = fixture.definition(identity::DefinitionKind::Interface);
  const auto nominal = fixture.definition(identity::DefinitionKind::Class);
  const auto makeInterface = [&]() {
    return PatternInterfaceInstantiation{interface, zc::Vector<TypeKeyPattern>()};
  };

  zc::Vector<TypeKeyPattern> genericArguments;
  genericArguments.add(TypeKeyPattern::parameter(0));
  ImplPattern generic{makeInterface(), TypeKeyPattern::nominal(nominal, zc::mv(genericArguments))};
  zc::Vector<TypeKeyPattern> concreteArguments;
  concreteArguments.add(TypeKeyPattern::primitive(PrimitiveKind::Unit));
  ImplPattern concrete{makeInterface(),
                       TypeKeyPattern::nominal(nominal, zc::mv(concreteArguments))};
  expectImplPatternRoundTrip(generic, *fixture.registries);
  expectImplPatternRoundTrip(concrete, *fixture.registries);

  auto genericKey = SignatureFactsCanonicalCodec::makeImplPatternKey(generic, *fixture.registries);
  auto concreteKey =
      SignatureFactsCanonicalCodec::makeImplPatternKey(concrete, *fixture.registries);
  ZC_REQUIRE(genericKey != zc::none);
  ZC_REQUIRE(concreteKey != zc::none);
  ZC_IF_SOME(genericValue, genericKey) {
    ZC_IF_SOME(concreteValue, concreteKey) {
      auto overlap = SignatureFactsCanonicalCodec::implPatternsOverlap(genericValue, concreteValue,
                                                                       *fixture.registries);
      ZC_REQUIRE(overlap != zc::none);
      ZC_IF_SOME(value, overlap) { ZC_EXPECT(value); }
      ZC_EXPECT(SignatureFactsCanonicalCodec::implPatternIsPublishable(genericValue, 1));
      ZC_EXPECT(!SignatureFactsCanonicalCodec::implPatternIsPublishable(genericValue, 0));
    }
  }

  ImplPattern integer{makeInterface(), TypeKeyPattern::primitive(PrimitiveKind::I32)};
  ImplPattern boolean{makeInterface(), TypeKeyPattern::primitive(PrimitiveKind::Bool)};
  auto integerKey = SignatureFactsCanonicalCodec::makeImplPatternKey(integer, *fixture.registries);
  auto booleanKey = SignatureFactsCanonicalCodec::makeImplPatternKey(boolean, *fixture.registries);
  ZC_REQUIRE(integerKey != zc::none);
  ZC_REQUIRE(booleanKey != zc::none);
  ZC_IF_SOME(integerValue, integerKey) {
    ZC_IF_SOME(booleanValue, booleanKey) {
      auto disjoint = SignatureFactsCanonicalCodec::implPatternsOverlap(integerValue, booleanValue,
                                                                        *fixture.registries);
      ZC_REQUIRE(disjoint != zc::none);
      ZC_IF_SOME(value, disjoint) { ZC_EXPECT(!value); }
    }
  }
}

ZC_TEST("SignatureFactsCanonicalCodec.EnforcesImplPatternPublicationRestrictions") {
  SignatureFixture fixture;
  const auto interface = fixture.definition(identity::DefinitionKind::Interface);
  const auto nominal = fixture.definition(identity::DefinitionKind::Class);
  const auto makeInterface = [&]() {
    return PatternInterfaceInstantiation{interface, zc::Vector<TypeKeyPattern>()};
  };
  const auto requireKey = [&](const ImplPattern& pattern) {
    auto key = SignatureFactsCanonicalCodec::makeImplPatternKey(pattern, *fixture.registries);
    ZC_REQUIRE(key != zc::none);
    ZC_IF_SOME(value, key) { return zc::mv(value); }
    ZC_UNREACHABLE
  };

  zc::Vector<TypeKeyPattern> nominalArguments;
  nominalArguments.add(TypeKeyPattern::parameter(0));
  const ImplPattern valid{makeInterface(),
                          TypeKeyPattern::nominal(nominal, zc::mv(nominalArguments))};
  auto validKey = requireKey(valid);
  ZC_EXPECT(SignatureFactsCanonicalCodec::implPatternIsPublishable(validKey, 1));
  ZC_EXPECT(!SignatureFactsCanonicalCodec::implPatternIsPublishable(validKey, 2));
  auto nominalHead = SignatureFactsCanonicalCodec::implPatternHead(validKey);
  ZC_REQUIRE(nominalHead != zc::none);
  ZC_IF_SOME(value, nominalHead) {
    ZC_EXPECT(value.variant().is<NominalTypeHead>());
    ZC_EXPECT(value.variant().get<NominalTypeHead>().definition == nominal);
  }

  const ImplPattern unused{makeInterface(), TypeKeyPattern::primitive(PrimitiveKind::Bool)};
  auto unusedKey = requireKey(unused);
  ZC_EXPECT(!SignatureFactsCanonicalCodec::implPatternIsPublishable(unusedKey, 1));

  const ImplPattern outOfRange{makeInterface(), TypeKeyPattern::parameter(0)};
  auto outOfRangeKey = requireKey(outOfRange);
  ZC_EXPECT(!SignatureFactsCanonicalCodec::implPatternIsPublishable(outOfRangeKey, 0));
  ZC_EXPECT(SignatureFactsCanonicalCodec::implPatternIsPublishable(outOfRangeKey, 1));
  auto blanketHead = SignatureFactsCanonicalCodec::implPatternHead(outOfRangeKey);
  ZC_REQUIRE(blanketHead != zc::none);
  ZC_IF_SOME(value, blanketHead) { ZC_EXPECT(value.variant().is<BlanketTypeHead>()); }

  zc::Vector<TypeKeyPattern> unionMembers;
  unionMembers.add(TypeKeyPattern::primitive(PrimitiveKind::Bool));
  unionMembers.add(TypeKeyPattern::parameter(0));
  const ImplPattern normalizedCollection{makeInterface(),
                                         TypeKeyPattern::unionOf(zc::mv(unionMembers))};
  auto normalizedCollectionKey = requireKey(normalizedCollection);
  ZC_EXPECT(!SignatureFactsCanonicalCodec::implPatternIsPublishable(normalizedCollectionKey, 1));

  const ImplPattern unresolvedSelf{makeInterface(), TypeKeyPattern::interfaceSelf(interface)};
  auto unresolvedSelfKey = requireKey(unresolvedSelf);
  ZC_EXPECT(!SignatureFactsCanonicalCodec::implPatternIsPublishable(unresolvedSelfKey, 0));
  ZC_EXPECT(SignatureFactsCanonicalCodec::implPatternHead(unresolvedSelfKey) == zc::none);

  const ImplPattern rigidParameter{
      makeInterface(), TypeKeyPattern::typeParameter(fixture.genericParameter().clone())};
  auto rigidParameterKey = requireKey(rigidParameter);
  ZC_EXPECT(!SignatureFactsCanonicalCodec::implPatternIsPublishable(rigidParameterKey, 0));

  zc::Vector<TypeKeyPattern> principalArguments;
  principalArguments.add(TypeKeyPattern::parameter(0));
  zc::Vector<PatternExistentialInterface> repeatedAdditional;
  zc::Vector<TypeKeyPattern> repeatedArguments;
  repeatedArguments.add(TypeKeyPattern::primitive(PrimitiveKind::Bool));
  repeatedAdditional.add(PatternExistentialInterface{interface, zc::mv(repeatedArguments)});
  const ImplPattern repeatedPrincipal{
      makeInterface(), TypeKeyPattern::existential(PatternExistentialType{
                           PatternExistentialInterface{interface, zc::mv(principalArguments)},
                           zc::mv(repeatedAdditional), zc::Vector<identity::DefId>(),
                           zc::Vector<PatternAssociatedTypeBinding>()})};
  auto repeatedPrincipalKey = requireKey(repeatedPrincipal);
  ZC_EXPECT(!SignatureFactsCanonicalCodec::implPatternIsPublishable(repeatedPrincipalKey, 1));

  zc::Vector<TypeKeyPattern> distinctPrincipalArguments;
  distinctPrincipalArguments.add(TypeKeyPattern::parameter(0));
  zc::Vector<PatternExistentialInterface> distinctAdditional;
  zc::Vector<TypeKeyPattern> distinctArguments;
  distinctArguments.add(TypeKeyPattern::primitive(PrimitiveKind::Bool));
  distinctAdditional.add(PatternExistentialInterface{nominal, zc::mv(distinctArguments)});
  const ImplPattern distinctPrincipal{
      makeInterface(),
      TypeKeyPattern::existential(PatternExistentialType{
          PatternExistentialInterface{interface, zc::mv(distinctPrincipalArguments)},
          zc::mv(distinctAdditional), zc::Vector<identity::DefId>(),
          zc::Vector<PatternAssociatedTypeBinding>()})};
  auto distinctPrincipalKey = requireKey(distinctPrincipal);
  ZC_EXPECT(SignatureFactsCanonicalCodec::implPatternIsPublishable(distinctPrincipalKey, 1));

  zc::Vector<PatternExistentialInterface> parameterizedAdditional;
  zc::Vector<TypeKeyPattern> parameterizedArguments;
  parameterizedArguments.add(TypeKeyPattern::parameter(0));
  parameterizedAdditional.add(PatternExistentialInterface{nominal, zc::mv(parameterizedArguments)});
  const ImplPattern parameterizedAdditionalPattern{
      makeInterface(), TypeKeyPattern::existential(PatternExistentialType{
                           PatternExistentialInterface{interface, zc::Vector<TypeKeyPattern>()},
                           zc::mv(parameterizedAdditional), zc::Vector<identity::DefId>(),
                           zc::Vector<PatternAssociatedTypeBinding>()})};
  auto parameterizedAdditionalKey = requireKey(parameterizedAdditionalPattern);
  ZC_EXPECT(!SignatureFactsCanonicalCodec::implPatternIsPublishable(parameterizedAdditionalKey, 1));
}

ZC_TEST("SignatureFactsCanonicalCodec.RejectsCyclicFirstOrderUnification") {
  SignatureFixture fixture;
  const auto interface = fixture.definition(identity::DefinitionKind::Interface);

  zc::Vector<TypeKeyPattern> leftInterfaceArguments;
  leftInterfaceArguments.add(TypeKeyPattern::parameter(0));
  zc::Vector<TypeKeyPattern> leftTupleElements;
  leftTupleElements.add(TypeKeyPattern::parameter(0));
  ImplPattern left{PatternInterfaceInstantiation{interface, zc::mv(leftInterfaceArguments)},
                   TypeKeyPattern::tuple(zc::mv(leftTupleElements))};

  zc::Vector<TypeKeyPattern> nestedRightParameter;
  nestedRightParameter.add(TypeKeyPattern::parameter(0));
  zc::Vector<TypeKeyPattern> rightInterfaceArguments;
  rightInterfaceArguments.add(TypeKeyPattern::tuple(zc::mv(nestedRightParameter)));
  ImplPattern right{PatternInterfaceInstantiation{interface, zc::mv(rightInterfaceArguments)},
                    TypeKeyPattern::parameter(0)};

  auto leftKey = SignatureFactsCanonicalCodec::makeImplPatternKey(left, *fixture.registries);
  auto rightKey = SignatureFactsCanonicalCodec::makeImplPatternKey(right, *fixture.registries);
  ZC_REQUIRE(leftKey != zc::none);
  ZC_REQUIRE(rightKey != zc::none);
  ZC_IF_SOME(leftValue, leftKey) {
    ZC_IF_SOME(rightValue, rightKey) {
      auto overlap = SignatureFactsCanonicalCodec::implPatternsOverlap(leftValue, rightValue,
                                                                       *fixture.registries);
      ZC_REQUIRE(overlap != zc::none);
      ZC_IF_SOME(value, overlap) { ZC_EXPECT(!value); }
    }
  }
}

}  // namespace zomlang::compiler::checker::signature
