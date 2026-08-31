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

#include "compiler/checker/facts/signature-facts.h"

#include "compiler/identity/crypto/sha256.h"
#include "compiler/ownership/surface-admission.h"
#include "tests/unittests/compiler/checker/checker-authority-test-fixture.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

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

class PatternAuthorityFixture final {
public:
  PatternAuthorityFixture()
      : session(R"zom(class RecoveryOwner {}
interface PatternInterface { type Associated; }
class PatternNominal {}
interface PatternMarker {}
fun PatternGeneric<T>(value: T) {}
)zom"_zc) {
    const auto& inventory = userBoundModule().definitions();
    interfaceDefinition = findDefinition(inventory, "PatternInterface"_zc);
    nominalDefinition = findDefinition(inventory, "PatternNominal"_zc);
    markerDefinition = findDefinition(inventory, "PatternMarker"_zc);
    genericDefinition = findDefinition(inventory, "PatternGeneric"_zc);
    associatedDefinition = findDefinition(inventory, "Associated"_zc);
    genericParameterKey =
        zc::heap<identity::GenericParameterKey>(findGenericParameter(inventory, genericDefinition));
    registries = zc::heap<CheckerIdentityAuthority>(session.identityAuthority().clone());
  }

  const CheckerIdentityAuthority& identities() const { return *registries; }
  identity::DefId definition(identity::DefinitionKind kind) const {
    switch (kind) {
      case identity::DefinitionKind::Class:
        return nominalDefinition;
      case identity::DefinitionKind::Interface:
        return interfaceDefinition;
      case identity::DefinitionKind::AssociatedType:
        return associatedDefinition;
      case identity::DefinitionKind::TypeAlias:
        return markerDefinition;
      default:
        ZC_FAIL_REQUIRE("missing pattern authority definition kind");
    }
  }
  identity::DefId interface() const noexcept { return interfaceDefinition; }
  identity::DefId nominal() const noexcept { return nominalDefinition; }
  identity::DefId marker() const noexcept { return markerDefinition; }
  identity::DefId associated() const noexcept { return associatedDefinition; }
  const identity::GenericParameterKey& genericParameter() const noexcept {
    return *genericParameterKey;
  }

  zc::Own<CheckerIdentityAuthority> registries;

private:
  const driver::module_graph_query::CheckerBoundModuleView& userBoundModule() const {
    auto module = session.identityAuthority().boundModule(session.module());
    ZC_REQUIRE(module != zc::none);
    return ZC_REQUIRE_NONNULL(module);
  }

  static identity::DefId findDefinition(const binder::ImmutableDefinitionInventory& inventory,
                                        zc::StringPtr name) {
    for (const auto& definition : inventory.definitions()) {
      if (definition.record.name() == name) { return definition.definition; }
    }
    ZC_FAIL_REQUIRE("missing pattern authority definition");
  }

  static identity::GenericParameterKey findGenericParameter(
      const binder::ImmutableDefinitionInventory& inventory, identity::DefId owner) {
    auto ownerEntry = inventory.definition(owner);
    ZC_REQUIRE(ownerEntry != zc::none);
    ZC_IF_SOME(definition, ownerEntry) {
      for (const auto& parameter : inventory.genericParameters()) {
        auto parameterOwner = parameter.record.owner().definitionKey();
        ZC_IF_SOME(expected, parameterOwner) {
          if (expected == definition.key()) { return parameter.key.clone(); }
        }
      }
    }
    ZC_FAIL_REQUIRE("missing pattern authority generic parameter");
  }

  tests::checker_fixture::CheckerAuthoritySession session;
  identity::DefId interfaceDefinition;
  identity::DefId nominalDefinition;
  identity::DefId markerDefinition;
  identity::DefId genericDefinition;
  identity::DefId associatedDefinition;
  zc::Own<identity::GenericParameterKey> genericParameterKey;
};

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
                                const CheckerIdentityAuthority& registries,
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
                                 const CheckerIdentityAuthority& registries) {
  ZC_EXPECT(SignatureFactsCanonicalCodec::decodeTypeKeyPatternKey(bytes.asPtr(), registries) ==
            zc::none);
}

void expectImplPatternRoundTrip(const ImplPattern& pattern,
                                const CheckerIdentityAuthority& registries) {
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
                                 const CheckerIdentityAuthority& registries) {
  ZC_EXPECT(SignatureFactsCanonicalCodec::decodeImplPatternKey(bytes.asPtr(), registries) ==
            zc::none);
}

struct DefinitionFixture final {
  identity::DefinitionKind kind;
  identity::DefinitionKey key;
  identity::DefId id;
};

class SignatureFixture final {
public:
  SignatureFixture()
      : session(R"zom(class RecoveryOwner {}
fun signature0(value: unit) -> unit {}
class Signature1 {}
interface Signature2 { type Signature4; }
alias Signature3 = unit;
enum SignatureEnum { Signature5 }
const SIGNATURE6: unit = ();
)zom"_zc) {
    const auto& inventory = boundModule().definitions();
    definitions.add(findDefinition(inventory, identity::DefinitionKind::Function, "signature0"_zc));
    definitions.add(findDefinition(inventory, identity::DefinitionKind::Class, "Signature1"_zc));
    definitions.add(
        findDefinition(inventory, identity::DefinitionKind::Interface, "Signature2"_zc));
    definitions.add(
        findDefinition(inventory, identity::DefinitionKind::TypeAlias, "Signature3"_zc));
    definitions.add(
        findDefinition(inventory, identity::DefinitionKind::AssociatedType, "Signature4"_zc));
    definitions.add(
        findDefinition(inventory, identity::DefinitionKind::EnumVariant, "Signature5"_zc));
    definitions.add(findDefinition(inventory, identity::DefinitionKind::Constant, "SIGNATURE6"_zc));
    callableParameterKey = zc::heap<identity::CallableParameterKey>(
        findCallableParameter(inventory, definition(identity::DefinitionKind::Function)));

    auto canonical = session.semanticTypes().canonicalizeClosed(type::semantic::TypeData(
        type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::Unit}));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto interned =
        session.semanticTypes().intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
    ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
    unitType = interned.get<type::SemanticTypeInterned>().id;

    zc::Vector<zc::ArrayPtr<const uint8_t>> noShapeRecords;
    auto shapeRevisionValue = MarkerShapeInventoryRevision::computeFramed(
        boundModule().semanticFingerprint().digest(), noShapeRecords.asPtr());
    ZC_REQUIRE(shapeRevisionValue != zc::none);
    ZC_IF_SOME(value, shapeRevisionValue) {
      shapeRevision = zc::heap<MarkerShapeInventoryRevision>(zc::mv(value));
    }
    const auto configuration = MarkerPolicyConfiguration::explicitOnly();
    auto policyRevisionValue = MarkerPolicyRegistryRevision::computeFramed(
        boundModule().semanticFingerprint().digest(), configuration.revision(), *shapeRevision,
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

  identity::SourceSpan span() const { return session.span(0, 1); }

  const identity::CallableParameterKey& callableParameter() const { return *callableParameterKey; }

  SemanticSignature functionSignature(zc::StringPtr parameterLabel, bool hasDefault,
                                      identity::SemanticTypeId success) const {
    zc::Vector<GenericParameterSignature> functionGenerics;
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
                             zc::Vector<identity::DefId>(), zc::Vector<identity::DefId>(),
                             zc::Vector<identity::DefId>()}),
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
        auto currentKey = session.identityAuthority().definition(current.definition);
        auto previousKey = session.identityAuthority().definition(result[insertion - 1].definition);
        ZC_REQUIRE(currentKey != zc::none);
        ZC_REQUIRE(previousKey != zc::none);
        bool currentBeforePrevious = false;
        ZC_IF_SOME(left, currentKey) {
          ZC_IF_SOME(right, previousKey) {
            currentBeforePrevious = lessBytes(left.key().bytes(), right.key().bytes());
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
      auto record = SignatureFactsCanonicalCodec::encodeSignature(
          signature, boundModule().module(), session.identityAuthority(), session.semanticTypes());
      ZC_REQUIRE(record != zc::none);
      ZC_IF_SOME(bytes, record) {
        result.add(SignatureDefinitionRequirement{signature.definition, signature.definitionKind,
                                                  zc::mv(bytes)});
      }
    }
    return result;
  }

  SignatureFactsCandidate candidate(zc::Vector<SemanticSignature>&& signatures) const {
    return SignatureFactsCandidate{session.semanticContext(),
                                   boundModule().semanticFingerprint(),
                                   boundModule().module(),
                                   boundModule().parsedModule().contentDigest(),
                                   boundModule().parsedModule().receipt(),
                                   boundModule().bindingSurface().revision(),
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
            session.semanticContext(), boundModule().semanticFingerprint(), boundModule().module(),
            boundModule().parsedModule().source(), boundModule().parsedModule().contentDigest(),
            boundModule().parsedModule().receipt(), boundModule().bindingSurface().revision(),
            *policyRevision, sourceSignatureCensus.asPtr(), sourceImplCensus.asPtr(),
            expected.asPtr(), noImpls.asPtr(), noMarkers.asPtr(), session.semanticTypes(),
            session.identityAuthority()});
  }

  SignatureFactsVerificationResult verify(SignatureFactsCandidate&& candidate) const {
    auto expectedSignatures = completeSignatures();
    return verifyAgainst(zc::mv(candidate), expectedSignatures.asPtr(), expectedSignatures.asPtr());
  }

  tests::checker_fixture::CheckerAuthoritySession session;
  identity::SemanticTypeId unitType;
  zc::Vector<DefinitionFixture> definitions;
  zc::Own<identity::CallableParameterKey> callableParameterKey;
  zc::Own<MarkerShapeInventoryRevision> shapeRevision;
  zc::Own<MarkerPolicyRegistryRevision> policyRevision;

private:
  const driver::module_graph_query::CheckerBoundModuleView& boundModule() const {
    return session.boundModule();
  }

  static DefinitionFixture findDefinition(const binder::ImmutableDefinitionInventory& inventory,
                                          identity::DefinitionKind kind, zc::StringPtr name) {
    for (const auto& entry : inventory.definitions()) {
      if (entry.record.kind() == kind && entry.record.name() == name) {
        return DefinitionFixture{kind, entry.key.clone(), entry.definition};
      }
    }
    ZC_FAIL_REQUIRE("missing signature-facts definition fixture");
  }

  static identity::CallableParameterKey findCallableParameter(
      const binder::ImmutableDefinitionInventory& inventory, identity::DefId owner) {
    auto ownerDefinition = inventory.definition(owner);
    ZC_REQUIRE(ownerDefinition != zc::none);
    ZC_IF_SOME(definition, ownerDefinition) {
      for (const auto& parameter : inventory.callableParameters()) {
        if (parameter.record.owner() == definition.key()) { return parameter.key.clone(); }
      }
    }
    ZC_FAIL_REQUIRE("missing signature-facts callable parameter fixture");
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

// Drives the production SignatureFactsBuilder over a real source module so the
// non-canonical `declaredFields` metadata can be observed against the canonical
// digest-sorted `fields`. The struct's stored fields are named so that the
// canonical digest order does not coincide with source order; the tests refuse
// to pass unless that reordering actually happens, so they stay falsifiable
// rather than trivially green.
class DeclaredFieldOrderFixture final {
public:
  explicit DeclaredFieldOrderFixture(zc::StringPtr sourceText) : session(sourceText) {
    const auto& identities = session.identityAuthority();
    zc::Vector<ownership::OwnershipAdmittedBoundModule> admitted(identities.modules().size());
    zc::Vector<MarkerShapeModuleInput> shapeInputs(identities.modules().size());
    zc::Maybe<size_t> userIndex;
    for (size_t index = 0; index < identities.modules().size(); ++index) {
      const auto& candidate = identities.modules()[index];
      auto admission = ownership::OwnershipSurfaceAdmissionBuilder::admit(candidate.retain());
      ZC_REQUIRE(admission.is<ownership::OwnershipAdmittedBoundModule>());
      admitted.add(zc::mv(admission).get<ownership::OwnershipAdmittedBoundModule>());
      shapeInputs.add(MarkerShapeModuleInput{admitted.back()});
      if (candidate.module() == session.module()) { userIndex = index; }
    }
    ZC_REQUIRE(userIndex != zc::none);

    auto shapeResult =
        MarkerShapeInventoryBuilder::build(session.semanticContext(), identities.fingerprint(),
                                           session.module(), shapeInputs.asPtr(), identities);
    ZC_REQUIRE(shapeResult.is<VerifiedMarkerShapeInventory>());
    shapes = zc::heap<VerifiedMarkerShapeInventory>(
        zc::mv(shapeResult).get<VerifiedMarkerShapeInventory>());

    const auto configuration = MarkerPolicyConfiguration::explicitOnly();
    zc::Vector<identity::ModuleId> noPreludeModules;
    auto policyResult = MarkerPolicyRegistryBuilder::build(session.module(), configuration, *shapes,
                                                           noPreludeModules.asPtr(), identities);
    ZC_REQUIRE(policyResult.is<VerifiedMarkerPolicyRegistry>());
    policies = zc::heap<VerifiedMarkerPolicyRegistry>(
        zc::mv(policyResult).get<VerifiedMarkerPolicyRegistry>());

    ZC_IF_SOME(value, userIndex) {
      auto factsResult = SignatureFactsBuilder::build(SignatureFactsBuildInput{
          admitted[value], session.semanticTypes(), *shapes, *policies, identities});
      ZC_REQUIRE(factsResult.is<VerifiedSignatureFacts>());
      facts = zc::heap<VerifiedSignatureFacts>(zc::mv(factsResult).get<VerifiedSignatureFacts>());
    }
  }

  const VerifiedSignatureFacts& signatureFacts() const {
    ZC_REQUIRE(facts.get() != nullptr);
    return *facts;
  }

  // Resolves the source name of a stored field DefId through the bound-module
  // definition inventory.
  zc::StringPtr fieldName(identity::DefId field) const {
    for (const auto& entry : session.boundModule().definitions().definitions()) {
      if (entry.definition == field) { return entry.record.name(); }
    }
    ZC_FAIL_REQUIRE("missing declared-field-order definition fixture");
  }

  const NominalSignature& nominal() const {
    const auto& payload = nominalSignature().payload.variant();
    ZC_REQUIRE(payload.is<NominalSignature>());
    return payload.get<NominalSignature>();
  }

  const SemanticSignature& nominalSignature() const {
    for (const auto& signature : signatureFacts().signatures()) {
      if (signature.definitionKind == identity::DefinitionKind::Struct) { return signature; }
    }
    ZC_FAIL_REQUIRE("missing declared-field-order struct signature");
  }

  // The declared-order field-name sequence, used to assert two modules really do
  // differ in declaration order before comparing their canonical encodings.
  zc::Vector<zc::String> declaredFieldNames() const {
    zc::Vector<zc::String> names;
    for (const auto declared : nominal().declaredFields) {
      names.add(zc::str(fieldName(declared)));
    }
    return names;
  }

  zc::Maybe<zc::Array<uint8_t>> encodeNominal() const {
    return SignatureFactsCanonicalCodec::encodeSignature(
        nominalSignature(), session.module(), session.identityAuthority(), session.semanticTypes());
  }

  // Re-runs the signature-facts verifier over the produced signatures, optionally
  // replacing the struct signature's `declaredFields` with a caller-supplied set.
  // The canonical requirements and census are always built from the unmodified
  // signatures, so a rejection can only come from the substituted `declaredFields`.
  // The fixture struct is plain (no generics, base, interfaces, variants, or
  // members), so the rebuilt nominal payload carries only fields + declaredFields.
  SignatureFactsVerificationResult verifyWithDeclaredFields(
      zc::Vector<identity::DefId>&& replacementDeclaredFields, bool replace) const {
    zc::Vector<SemanticSignature> candidateSignatures;
    for (const auto& signature : signatureFacts().signatures()) {
      auto cloned = signature.clone();
      if (replace && signature.definitionKind == identity::DefinitionKind::Struct) {
        const auto& payload = cloned.payload.variant();
        ZC_REQUIRE(payload.is<NominalSignature>());
        const auto& original = payload.get<NominalSignature>();
        ZC_REQUIRE(original.genericParameters.size() == 0);
        ZC_REQUIRE(original.base == zc::none);
        ZC_REQUIRE(original.interfaces.size() == 0);
        ZC_REQUIRE(original.variants.size() == 0);
        ZC_REQUIRE(original.members.size() == 0);
        zc::Vector<identity::DefId> fields(original.fields.size());
        fields.addAll(original.fields.asPtr());
        zc::Maybe<identity::SemanticTypeId> noBase;
        cloned =
            SemanticSignature{signature.definition,
                              signature.definitionKind,
                              signature.scope.clone(),
                              zc::Vector<SignatureModifier>(),
                              zc::Vector<NormalizedAttributeFact>(),
                              SemanticSignaturePayload(NominalSignature{
                                  zc::Vector<GenericParameterSignature>(), zc::mv(noBase),
                                  zc::Vector<InterfaceInstantiation>(), zc::mv(fields),
                                  zc::mv(replacementDeclaredFields), zc::Vector<identity::DefId>(),
                                  zc::Vector<identity::DefId>()}),
                              signature.declarationSpan.clone()};
      }
      candidateSignatures.add(zc::mv(cloned));
    }

    zc::Vector<SignatureDefinitionRequirement> requirements;
    zc::Vector<SignatureDefinitionCensusEntry> census;
    for (const auto& signature : signatureFacts().signatures()) {
      auto record = SignatureFactsCanonicalCodec::encodeSignature(
          signature, session.module(), session.identityAuthority(), session.semanticTypes());
      ZC_REQUIRE(record != zc::none);
      ZC_IF_SOME(bytes, record) {
        requirements.add(SignatureDefinitionRequirement{signature.definition,
                                                        signature.definitionKind, zc::mv(bytes)});
      }
      census.add(SignatureDefinitionCensusEntry{signature.definition, signature.definitionKind});
    }

    const auto& bound = session.boundModule();
    SignatureFactsCandidate candidate{session.semanticContext(),
                                      session.identityAuthority().fingerprint(),
                                      session.module(),
                                      bound.parsedModule().contentDigest(),
                                      bound.parsedModule().receipt(),
                                      bound.bindingSurface().revision(),
                                      policies->revision(),
                                      zc::mv(candidateSignatures),
                                      zc::Vector<ImplHead>(),
                                      zc::Vector<MarkerFact>()};
    zc::Vector<ImplAuthorityCensusEntry> noImplCensus;
    zc::Vector<ImplHeadRequirement> noImpls;
    zc::Vector<MarkerFactRequirement> noMarkers;
    return SignatureFactsVerifier::verify(
        zc::mv(candidate),
        SignatureFactsVerificationInput{
            session.semanticContext(), session.identityAuthority().fingerprint(), session.module(),
            bound.parsedModule().source(), bound.parsedModule().contentDigest(),
            bound.parsedModule().receipt(), bound.bindingSurface().revision(), policies->revision(),
            census.asPtr(), noImplCensus.asPtr(), requirements.asPtr(), noImpls.asPtr(),
            noMarkers.asPtr(), session.semanticTypes(), session.identityAuthority()});
  }

  tests::checker_fixture::CheckerAuthoritySession session;

private:
  zc::Own<VerifiedMarkerShapeInventory> shapes;
  zc::Own<VerifiedMarkerPolicyRegistry> policies;
  zc::Own<VerifiedSignatureFacts> facts;
};

// Source-order field list chosen so the canonical digest sort reorders it.
constexpr zc::StringPtr kDeclaredOrderSource = R"zom(class RecoveryOwner {}
struct DeclaredOrder {
  zulu: unit;
  alpha: unit;
  mike: unit;
  bravo: unit;
  quebec: unit;
  delta: unit;
}
)zom"_zc;

// Same field set as kDeclaredOrderSource, declared in a different source order.
constexpr zc::StringPtr kReorderedDeclaredOrderSource = R"zom(class RecoveryOwner {}
struct DeclaredOrder {
  delta: unit;
  quebec: unit;
  bravo: unit;
  mike: unit;
  alpha: unit;
  zulu: unit;
}
)zom"_zc;

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
              "df372823d5f51543118268c3ebf4345fcfefee34104389bb269d0ba17771d39d"_zc);
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
              "1e1aa7ddd2c702f9febc3d07f3353849a697b377c9589fe68536d30fe465d7f9"_zc);
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
                "5cb8da449247154b7dee5d3b6269f5a30dab11f1b1f032561590d590e24c2475"_zc);
    }
  }
}

ZC_TEST("SignatureFactsVerifier.VerifiesAndRejectsAuthorityBackedSignatures") {
  SignatureFixture fixture;
  auto accepted = fixture.verify(fixture.candidate(fixture.completeSignatures()));

  ZC_REQUIRE(accepted.is<VerifiedSignatureFacts>());
  const auto& facts = accepted.get<VerifiedSignatureFacts>();
  ZC_EXPECT(facts.semanticContext() == fixture.session.semanticContext());
  ZC_EXPECT(facts.module() == fixture.session.module());
  ZC_EXPECT(facts.signatures().size() == 7);
  ZC_EXPECT(facts.revision().digest() != identity::Sha256Digest());

  auto missing = fixture.completeSignatures();
  missing.removeLast();
  auto missingResult = fixture.verify(fixture.candidate(zc::mv(missing)));
  ZC_EXPECT(checkerFailure(missingResult).kind == CheckerInvariantKind::MissingRequiredFact);

  auto mismatched = fixture.completeSignatures();
  mismatched[0].definitionKind = identity::DefinitionKind::Class;
  auto mismatchResult = fixture.verify(fixture.candidate(zc::mv(mismatched)));
  ZC_EXPECT(checkerFailure(mismatchResult).kind == CheckerInvariantKind::InvalidFact);

  auto candidateSignatures = fixture.completeSignatures();
  candidateSignatures.removeLast();
  auto recordSignatures = fixture.completeSignatures();
  recordSignatures.removeLast();
  auto censusSignatures = fixture.completeSignatures();

  auto omissionResult = fixture.verifyAgainst(fixture.candidate(zc::mv(candidateSignatures)),
                                              recordSignatures.asPtr(), censusSignatures.asPtr());

  ZC_EXPECT(checkerFailure(omissionResult).kind == CheckerInvariantKind::MissingRequiredFact);

  auto reorderedSignatures = fixture.completeSignatures();
  auto first = zc::mv(reorderedSignatures[0]);
  reorderedSignatures[0] = zc::mv(reorderedSignatures[1]);
  reorderedSignatures[1] = zc::mv(first);

  auto reorderedResult = fixture.verify(fixture.candidate(zc::mv(reorderedSignatures)));

  ZC_EXPECT(checkerFailure(reorderedResult).kind == CheckerInvariantKind::CanonicalCodecMismatch);

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

  auto interfaceReplacement = fixture.completeSignatures();
  for (size_t index = 0; index < interfaceReplacement.size(); ++index) {
    if (interfaceReplacement[index].definitionKind == identity::DefinitionKind::Interface) {
      interfaceReplacement[index] = fixture.interfaceSignature(true);
      break;
    }
  }

  auto interfaceReplacementResult = fixture.verify(fixture.candidate(zc::mv(interfaceReplacement)));

  ZC_EXPECT(checkerFailure(interfaceReplacementResult).kind ==
            CheckerInvariantKind::CanonicalCodecMismatch);

  auto invalidTypeSignatures = fixture.completeSignatures();
  size_t functionIndex = invalidTypeSignatures.size();
  for (size_t index = 0; index < invalidTypeSignatures.size(); ++index) {
    if (invalidTypeSignatures[index].definitionKind == identity::DefinitionKind::Function) {
      functionIndex = index;
      break;
    }
  }
  ZC_REQUIRE(functionIndex < invalidTypeSignatures.size());
  invalidTypeSignatures[functionIndex] =
      fixture.functionSignature("value"_zc, false, identity::SemanticTypeId());

  auto invalidTypeResult = fixture.verify(fixture.candidate(zc::mv(invalidTypeSignatures)));

  ZC_EXPECT(identityFailure(invalidTypeResult).kind() ==
            identity::IdentityInvariantKind::InvalidHandle);
  ZC_EXPECT(identityFailure(invalidTypeResult).phase() ==
            identity::IdentityAllocationPhase::SemanticType);
}

ZC_TEST("SignatureFactsCanonicalCodec.EncodesClosedMarkerInterfacesWithoutTypeStore") {
  SignatureFixture fixture;
  auto marker = fixture.interfaceSignature(true);

  auto typeFree = SignatureFactsCanonicalCodec::encodeTypeFreeInterfaceSignature(
      marker, fixture.session.module(), fixture.session.identityAuthority());
  auto standard = SignatureFactsCanonicalCodec::encodeSignature(marker, fixture.session.module(),
                                                                fixture.session.identityAuthority(),
                                                                fixture.session.semanticTypes());

  ZC_REQUIRE(typeFree != zc::none);
  ZC_REQUIRE(standard != zc::none);
  ZC_IF_SOME(typeFreeBytes, typeFree) {
    ZC_IF_SOME(standardBytes, standard) { ZC_EXPECT(typeFreeBytes == standardBytes); }
  }

  auto nonMarker = fixture.interfaceSignature(false);
  ZC_EXPECT(SignatureFactsCanonicalCodec::encodeTypeFreeInterfaceSignature(
                nonMarker, fixture.session.module(), fixture.session.identityAuthority()) ==
            zc::none);
}

ZC_TEST("SignatureFactsCanonicalCodec.PatternFixtureRetainsMaterializedIdentityAuthority") {
  PatternAuthorityFixture fixture;
  ZC_EXPECT(fixture.identities().definition(fixture.interface()) != zc::none);
  ZC_EXPECT(fixture.identities().definition(fixture.nominal()) != zc::none);
  ZC_EXPECT(fixture.identities().definition(fixture.marker()) != zc::none);
  ZC_EXPECT(fixture.identities().definition(fixture.associated()) != zc::none);
  ZC_EXPECT(fixture.identities().genericParameter(fixture.genericParameter()) != zc::none);
}

ZC_TEST("SignatureFactsCanonicalCodec.MatchesIdentityFreePatternGoldenVectors") {
  PatternAuthorityFixture fixture;
  expectTypeKeyPatternOracle(TypeKeyPattern::parameter(0), *fixture.registries,
                             "7a6f6d2e747970652d6b65792d7061747465726e001100000000"_zc,
                             "96bd577588d6b8551392c23a61a2135ddf7f29b564d37c2b7e5ede52fafe8581"_zc);
  expectTypeKeyPatternOracle(
      TypeKeyPattern::reference(Mutability::Const, TypeKeyPattern::parameter(0)),
      *fixture.registries, "7a6f6d2e747970652d6b65792d7061747465726e000c011100000000"_zc,
      "bbd2d487bb1eef06c4a0177a71fbccb6a296d22a002e3fa8b296d121f36e0b19"_zc);
}

ZC_TEST("SignatureFactsCanonicalCodec.MatchesRemainingNormativeRawPatternVectors") {
  expectRawOracle("7a6f6d2e747970652d6b65792d7061747465726e0010a1"_zc,
                  "0f5e0bc5f6db8f233e6c5e9428718ac9538513fc5e9bc22338b1bfaed0a92f1c"_zc);
  expectRawOracle("7a6f6d2e747970652d6b65792d7061747465726e0008a100000000000000011100000000"_zc,
                  "9f347b5d6fa3d7faa247f06754e7992c7cc4fb2af8a475cb20c53152f3377a26"_zc);
  expectRawOracle("7a6f6d2e696d706c2d7061747465726e00a100000000000000001100000000"_zc,
                  "b203dbd1a3fae2885b4eef6963584363e134690f50b3f8f396100a8e7f319443"_zc);
  expectRawOracle(
      "7a6f6d2e696d706c2d7061747465726e00a10000000000000001110000000008b200000000000000011100000000"_zc,
      "4801c959691fe5a8ef7f7bf667205df514df9d792fa2cebf4c940447af11f9b2"_zc);
}

ZC_TEST("SignatureFactsCanonicalCodec.EncodesNonEmptyRecursivePatternFamilies") {
  PatternAuthorityFixture fixture;
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

  patterns.add(TypeKeyPattern::dynamicArray(TypeKeyPattern::primitive(PrimitiveKind::Bool)));
  patterns.add(TypeKeyPattern::slice(TypeKeyPattern::primitive(PrimitiveKind::Unit)));
  patterns.add(TypeKeyPattern::fixedArray(TypeKeyPattern::primitive(PrimitiveKind::Null), 4));
  patterns.add(TypeKeyPattern::typeParameter(fixture.genericParameter().clone()));

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
  PatternAuthorityFixture fixture;
  const auto payloadOffset = zc::StringPtr("zom.type-key-pattern").size() + 1;

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
  PatternAuthorityFixture fixture;

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
  PatternAuthorityFixture fixture;
  const auto interface = fixture.definition(identity::DefinitionKind::Interface);
  const ImplPattern pattern{PatternInterfaceInstantiation{interface, zc::Vector<TypeKeyPattern>()},
                            TypeKeyPattern::parameter(0)};
  auto key = SignatureFactsCanonicalCodec::makeImplPatternKey(pattern, *fixture.registries);
  ZC_REQUIRE(key != zc::none);
  ZC_IF_SOME(value, key) {
    const auto payloadOffset = zc::StringPtr("zom.impl-pattern").size() + 1;

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
  PatternAuthorityFixture fixture;
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

ZC_TEST("SignatureFactsCanonicalCodec.ComparesEquivalentConcreteTypeHeads") {
  PatternAuthorityFixture fixture;
  const auto interface = fixture.definition(identity::DefinitionKind::Interface);
  const auto nominal = fixture.definition(identity::DefinitionKind::Class);
  const auto makeInterface = [&]() {
    return PatternInterfaceInstantiation{interface, zc::Vector<TypeKeyPattern>()};
  };
  const auto expectOverlap = [&](TypeKeyPattern&& left, TypeKeyPattern&& right) {
    auto leftKey = SignatureFactsCanonicalCodec::makeImplPatternKey(
        ImplPattern{makeInterface(), zc::mv(left)}, *fixture.registries);
    auto rightKey = SignatureFactsCanonicalCodec::makeImplPatternKey(
        ImplPattern{makeInterface(), zc::mv(right)}, *fixture.registries);
    ZC_REQUIRE(leftKey != zc::none);
    ZC_REQUIRE(rightKey != zc::none);
    ZC_IF_SOME(leftValue, leftKey) {
      ZC_IF_SOME(rightValue, rightKey) {
        auto overlap = SignatureFactsCanonicalCodec::implPatternsOverlap(leftValue, rightValue,
                                                                         *fixture.registries);
        ZC_REQUIRE(overlap != zc::none);
        ZC_IF_SOME(value, overlap) { ZC_EXPECT(value); }
      }
    }
  };

  expectOverlap(TypeKeyPattern::primitive(PrimitiveKind::I32),
                TypeKeyPattern::primitive(PrimitiveKind::I32));

  zc::Vector<TypeKeyPattern> leftTuple;
  leftTuple.add(TypeKeyPattern::primitive(PrimitiveKind::I32));
  leftTuple.add(TypeKeyPattern::primitive(PrimitiveKind::Bool));
  zc::Vector<TypeKeyPattern> rightTuple;
  rightTuple.add(TypeKeyPattern::primitive(PrimitiveKind::I32));
  rightTuple.add(TypeKeyPattern::primitive(PrimitiveKind::Bool));
  expectOverlap(TypeKeyPattern::tuple(zc::mv(leftTuple)),
                TypeKeyPattern::tuple(zc::mv(rightTuple)));

  zc::Vector<PatternObjectField> leftFields;
  leftFields.add(PatternObjectField{scalar<identity::SemanticIdentifier>("field"_zc),
                                    TypeKeyPattern::primitive(PrimitiveKind::I32),
                                    Mutability::Const, FieldPresence::Required});
  zc::Vector<PatternObjectField> rightFields;
  rightFields.add(PatternObjectField{scalar<identity::SemanticIdentifier>("field"_zc),
                                     TypeKeyPattern::primitive(PrimitiveKind::I32),
                                     Mutability::Const, FieldPresence::Required});
  expectOverlap(TypeKeyPattern::object(zc::mv(leftFields)),
                TypeKeyPattern::object(zc::mv(rightFields)));

  zc::Vector<TypeKeyPattern> leftUnion;
  leftUnion.add(TypeKeyPattern::primitive(PrimitiveKind::I32));
  leftUnion.add(TypeKeyPattern::primitive(PrimitiveKind::Bool));
  zc::Vector<TypeKeyPattern> rightUnion;
  rightUnion.add(TypeKeyPattern::primitive(PrimitiveKind::I32));
  rightUnion.add(TypeKeyPattern::primitive(PrimitiveKind::Bool));
  expectOverlap(TypeKeyPattern::unionOf(zc::mv(leftUnion)),
                TypeKeyPattern::unionOf(zc::mv(rightUnion)));

  zc::Vector<TypeKeyPattern> leftIntersection;
  leftIntersection.add(TypeKeyPattern::primitive(PrimitiveKind::I32));
  leftIntersection.add(TypeKeyPattern::primitive(PrimitiveKind::Bool));
  zc::Vector<TypeKeyPattern> rightIntersection;
  rightIntersection.add(TypeKeyPattern::primitive(PrimitiveKind::I32));
  rightIntersection.add(TypeKeyPattern::primitive(PrimitiveKind::Bool));
  expectOverlap(TypeKeyPattern::intersection(zc::mv(leftIntersection)),
                TypeKeyPattern::intersection(zc::mv(rightIntersection)));

  expectOverlap(TypeKeyPattern::function(PatternFunctionType{
                    zc::Vector<TypeKeyPattern>(), TypeKeyPattern::primitive(PrimitiveKind::Unit),
                    zc::Maybe<TypeKeyPattern>()}),
                TypeKeyPattern::function(PatternFunctionType{
                    zc::Vector<TypeKeyPattern>(), TypeKeyPattern::primitive(PrimitiveKind::Unit),
                    zc::Maybe<TypeKeyPattern>()}));
  expectOverlap(TypeKeyPattern::nominal(nominal, zc::Vector<TypeKeyPattern>()),
                TypeKeyPattern::nominal(nominal, zc::Vector<TypeKeyPattern>()));
  expectOverlap(
      TypeKeyPattern::reference(Mutability::Const, TypeKeyPattern::primitive(PrimitiveKind::Bool)),
      TypeKeyPattern::reference(Mutability::Const, TypeKeyPattern::primitive(PrimitiveKind::Bool)));
  expectOverlap(TypeKeyPattern::rawPointer(Mutability::Mutable,
                                           TypeKeyPattern::primitive(PrimitiveKind::I32)),
                TypeKeyPattern::rawPointer(Mutability::Mutable,
                                           TypeKeyPattern::primitive(PrimitiveKind::I32)));
  expectOverlap(TypeKeyPattern::dynamicArray(TypeKeyPattern::primitive(PrimitiveKind::I32)),
                TypeKeyPattern::dynamicArray(TypeKeyPattern::primitive(PrimitiveKind::I32)));
  expectOverlap(TypeKeyPattern::slice(TypeKeyPattern::primitive(PrimitiveKind::I32)),
                TypeKeyPattern::slice(TypeKeyPattern::primitive(PrimitiveKind::I32)));
  expectOverlap(TypeKeyPattern::fixedArray(TypeKeyPattern::primitive(PrimitiveKind::I32), 4),
                TypeKeyPattern::fixedArray(TypeKeyPattern::primitive(PrimitiveKind::I32), 4));

  expectOverlap(TypeKeyPattern::existential(PatternExistentialType{
                    PatternExistentialInterface{interface, zc::Vector<TypeKeyPattern>()},
                    zc::Vector<PatternExistentialInterface>(), zc::Vector<identity::DefId>(),
                    zc::Vector<PatternAssociatedTypeBinding>()}),
                TypeKeyPattern::existential(PatternExistentialType{
                    PatternExistentialInterface{interface, zc::Vector<TypeKeyPattern>()},
                    zc::Vector<PatternExistentialInterface>(), zc::Vector<identity::DefId>(),
                    zc::Vector<PatternAssociatedTypeBinding>()}));
}

ZC_TEST("SignatureFactsCanonicalCodec.EnforcesImplPatternPublicationRestrictions") {
  PatternAuthorityFixture fixture;
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
  PatternAuthorityFixture fixture;
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

ZC_TEST("SignatureFactsBuilder retains declared field order as a permutation of canonical fields") {
  DeclaredFieldOrderFixture fixture(kDeclaredOrderSource);
  const auto& nominal = fixture.nominal();

  // Canonical `fields` and non-canonical `declaredFields` describe the same set.
  ZC_REQUIRE(nominal.fields.size() == 6);
  ZC_REQUIRE(nominal.declaredFields.size() == nominal.fields.size());

  // `declaredFields` must reproduce the exact source declaration order.
  const zc::StringPtr expectedOrder[] = {"zulu"_zc,  "alpha"_zc,  "mike"_zc,
                                         "bravo"_zc, "quebec"_zc, "delta"_zc};
  for (size_t index = 0; index < nominal.declaredFields.size(); ++index) {
    ZC_EXPECT(fixture.fieldName(nominal.declaredFields[index]) == expectedOrder[index]);
  }

  // `declaredFields` is a permutation of `fields`: same set, each present once.
  for (const auto declared : nominal.declaredFields) {
    size_t occurrences = 0;
    for (const auto field : nominal.fields) {
      if (field == declared) { ++occurrences; }
    }
    ZC_EXPECT(occurrences == 1);
  }
  for (const auto field : nominal.fields) {
    size_t occurrences = 0;
    for (const auto declared : nominal.declaredFields) {
      if (declared == field) { ++occurrences; }
    }
    ZC_EXPECT(occurrences == 1);
  }

  // Falsifiability guard: the canonical digest sort must actually reorder the
  // stored fields relative to source order. If `fields` happened to equal
  // `declaredFields`, this fixture would prove nothing, so it fails loudly.
  bool reordered = false;
  for (size_t index = 0; index < nominal.fields.size(); ++index) {
    if (fixture.fieldName(nominal.fields[index]) !=
        fixture.fieldName(nominal.declaredFields[index])) {
      reordered = true;
      break;
    }
  }
  ZC_EXPECT(reordered);
}

ZC_TEST("SignatureFactsCanonicalCodec ignores declared field order in the canonical encoding") {
  DeclaredFieldOrderFixture declared(kDeclaredOrderSource);
  DeclaredFieldOrderFixture reordered(kReorderedDeclaredOrderSource);

  // Precondition: the two modules genuinely differ in declaration order, so a
  // matching canonical encoding proves the encoder ignores `declaredFields`
  // rather than the two orders coinciding by accident.
  auto declaredNames = declared.declaredFieldNames();
  auto reorderedNames = reordered.declaredFieldNames();
  ZC_REQUIRE(declaredNames.size() == reorderedNames.size());
  bool orderDiffers = false;
  for (size_t index = 0; index < declaredNames.size(); ++index) {
    if (declaredNames[index] != reorderedNames[index]) {
      orderDiffers = true;
      break;
    }
  }
  ZC_EXPECT(orderDiffers);

  // Same field set + same containing struct => identical canonical bytes despite
  // the differing declaration order, because the encoder reads only `fields`.
  auto declaredBytes = declared.encodeNominal();
  auto reorderedBytes = reordered.encodeNominal();
  ZC_REQUIRE(declaredBytes != zc::none);
  ZC_REQUIRE(reorderedBytes != zc::none);
  ZC_IF_SOME(left, declaredBytes) {
    ZC_IF_SOME(right, reorderedBytes) { ZC_EXPECT(left.asPtr() == right.asPtr()); }
  }
}

ZC_TEST("SignatureFactsVerifier accepts a faithful declared field permutation") {
  DeclaredFieldOrderFixture fixture(kDeclaredOrderSource);
  auto accepted = fixture.verifyWithDeclaredFields(zc::Vector<identity::DefId>(), false);
  ZC_EXPECT(accepted.is<VerifiedSignatureFacts>());
}

ZC_TEST("SignatureFactsVerifier fail-closes on a declared field set that is not a permutation") {
  DeclaredFieldOrderFixture fixture(kDeclaredOrderSource);
  const auto& fields = fixture.nominal().fields;
  ZC_REQUIRE(fields.size() >= 2);

  // Missing: drop the last field so `declaredFields` is a strict subset of `fields`.
  {
    zc::Vector<identity::DefId> missing(fields.size() - 1);
    for (size_t index = 0; index + 1 < fields.size(); ++index) { missing.add(fields[index]); }
    auto result = fixture.verifyWithDeclaredFields(zc::mv(missing), true);
    ZC_EXPECT(checkerFailure(result).kind == CheckerInvariantKind::InvalidFact);
  }

  // Duplicate: repeat the first field so the set has the right size but a duplicate
  // and a missing member.
  {
    zc::Vector<identity::DefId> duplicated(fields.size());
    duplicated.add(fields[0]);
    for (size_t index = 1; index < fields.size(); ++index) { duplicated.add(fields[0]); }
    auto result = fixture.verifyWithDeclaredFields(zc::mv(duplicated), true);
    ZC_EXPECT(checkerFailure(result).kind == CheckerInvariantKind::InvalidFact);
  }
}

}  // namespace zomlang::compiler::checker::signature
