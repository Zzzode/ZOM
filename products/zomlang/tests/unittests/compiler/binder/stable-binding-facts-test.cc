// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/core/map.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/binder/module-binding-allocation-plan.h"
#include "zomlang/compiler/binder/stable-binding-codec.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::binder {
namespace {

template <typename T>
constexpr bool isMoveOnly() {
  return __is_constructible(T, T&&) && !__is_constructible(T, const T&);
}

static_assert(isMoveOnly<StableDefinitionQueryKey>());
static_assert(isMoveOnly<StableImplementationQueryKey>());
static_assert(isMoveOnly<StableImplementationOccurrenceQueryKey>());
static_assert(isMoveOnly<StableGenericParameterQueryKey>());
static_assert(isMoveOnly<StableCallableParameterQueryKey>());
static_assert(isMoveOnly<StableSemanticImportQueryKey>());
static_assert(isMoveOnly<StableOwnerBodyQueryKey>());
static_assert(isMoveOnly<StableExportedBindingQueryKey>());
static_assert(isMoveOnly<StableScopeNameBucketQueryKey>());
static_assert(isMoveOnly<StableHeaderSite>());
static_assert(isMoveOnly<StableHeaderGenericParameter>());
static_assert(isMoveOnly<StableHeaderCallableParameter>());
static_assert(isMoveOnly<StableDefinitionHeader>());
static_assert(isMoveOnly<StableImplementationOccurrenceHeader>());
static_assert(isMoveOnly<StableScopeOwnerKey>());
static_assert(isMoveOnly<StableNodeSyntaxRoot>());
static_assert(isMoveOnly<StableScopeFact>());
static_assert(isMoveOnly<StableNodeScopeFact>());
static_assert(isMoveOnly<StableBodyScopeFact>());
static_assert(isMoveOnly<StableBodyNodeScopeFact>());
static_assert(isMoveOnly<StableOwnerLocalBindingFact>());
static_assert(isMoveOnly<StableResolutionFact>());
static_assert(isMoveOnly<StableDeferredMemberFact>());
static_assert(isMoveOnly<StableSelfOwner>());
static_assert(isMoveOnly<StableSelfTypeFact>());
static_assert(isMoveOnly<StableThisBindingFact>());
static_assert(isMoveOnly<StableShadowTargetFact>());
static_assert(isMoveOnly<StableLabelKey>());
static_assert(isMoveOnly<StableLabelTarget>());
static_assert(isMoveOnly<StableLabelFact>());
static_assert(isMoveOnly<StableControlTarget>());
static_assert(isMoveOnly<StableControlTransferFact>());
static_assert(isMoveOnly<StableClosureFact>());
static_assert(isMoveOnly<StableClosureFreeVariable>());
static_assert(isMoveOnly<StableClosureFreeVariableFact>());
static_assert(isMoveOnly<StableExplicitCaptureBindingFact>());
static_assert(isMoveOnly<StableExplicitClosureCaptureFact>());
static_assert(isMoveOnly<StableDeclarationFact>());
static_assert(isMoveOnly<StableImplementationOccurrenceFact>());
static_assert(isMoveOnly<StableGenericParameterDeclarationFact>());
static_assert(isMoveOnly<StableCallableParameterDeclarationFact>());
static_assert(isMoveOnly<StableBindingTargetKey>());
static_assert(isMoveOnly<StableExportedBinding>());
static_assert(isMoveOnly<StableImportFact>());
static_assert(isMoveOnly<StableModuleAliasFact>());
static_assert(isMoveOnly<StableReexportStep>());
static_assert(isMoveOnly<StableLocalExportFact>());
static_assert(isMoveOnly<BinderQueryOwner>());
static_assert(isMoveOnly<StableFailedLookupOutcome>());
static_assert(isMoveOnly<StableFailedLookupFact>());
static_assert(isMoveOnly<BoundOwnerBody>());
static_assert(isMoveOnly<OwnerAllocationRange>());
static_assert(isMoveOnly<ModuleBindingAllocationPlan>());
static_assert(isMoveOnly<BoundModuleSkeleton>());
static_assert(isMoveOnly<BinderKeyFailure>());
static_assert(isMoveOnly<BinderSourceRejected>());
static_assert(isMoveOnly<BinderQueryResult<uint32_t>>());
static_assert(isMoveOnly<CanonicalSequence<uint32_t>>());
static_assert(isMoveOnly<CanonicalNonEmptySequence<uint32_t>>());
static_assert(isMoveOnly<diagnostics::DiagnosticFact>());

template <typename T>
T require(zc::Maybe<T>&& result) {
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid stable binding fixture");
}

identity::ModuleKey module(zc::StringPtr segment) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::ModulePathSegment>(segment));
  return require(identity::ModuleKey::from(tests::test_identity_detail::crate(), zc::mv(path)));
}

template <typename T>
T digestKey(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  return require(T::fromBytes(zc::arrayPtr(bytes)));
}

identity::ModuleResolutionPolicyKey policy() {
  return require(identity::ModuleResolutionPolicyKey::from(
      identity::UnicodeNormalizationPolicy::Nfc, identity::CaseComparisonPolicy::CaseSensitive,
      identity::SymlinkHandlingPolicy::ResolveThenConfine,
      identity::ModuleContainmentPolicy::DeclaredRootsOnly,
      identity::LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
      identity::DependencyAliasLookupPolicy::ExactFirstSegment,
      identity::PreludeLookupPolicy::ConfiguredCratePrelude,
      identity::ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence));
}

identity::SemanticImportBindingKey importBinding(zc::StringPtr requester) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::ModulePathSegment>("dep"_zc));
  zc::Maybe<zc::Vector<identity::ModulePathSegment>> retainedPath(zc::mv(path));
  zc::Maybe<identity::DependencyAlias> noAlias;
  auto resolution = require(
      identity::ModuleResolutionKey::from(module(requester), identity::ModuleDependencyKind::Import,
                                          zc::mv(retainedPath), zc::mv(noAlias), policy()));
  using identity::DeclaredDefinitionName;
  return require(identity::SemanticImportBindingKey::from(
      module(requester), zc::mv(resolution), identity::SemanticImportOperation::Import,
      identity::DefinitionNamespace::Value,
      tests::test_identity_detail::scalar<DeclaredDefinitionName>("source"_zc),
      identity::DefinitionNamespace::Value,
      tests::test_identity_detail::scalar<DeclaredDefinitionName>("local"_zc)));
}

ImplSourceOccurrenceKey occurrence(zc::StringPtr owner) {
  zc::Vector<uint32_t> path;
  auto site = require(IdentitySyntaxSiteKey::from(
      module(owner), tests::test_identity_detail::source(), zc::mv(path)));
  return ImplSourceOccurrenceKey::from(digestKey<identity::ImplKey>(0x22), zc::mv(site));
}

IdentitySyntaxSiteKey headerSite(zc::StringPtr owner) {
  zc::Vector<uint32_t> path;
  return require(IdentitySyntaxSiteKey::from(module(owner), tests::test_identity_detail::source(),
                                             zc::mv(path)));
}

identity::DeclaredDefinitionName declaredName(zc::StringPtr text) {
  return tests::test_identity_detail::scalar<identity::DeclaredDefinitionName>(text);
}

zc::Maybe<identity::DeclaredDefinitionName> optionalName(zc::StringPtr text) {
  return declaredName(text);
}

identity::DefinitionIdentityRecord definitionRecord(zc::StringPtr owner) {
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  zc::Maybe<identity::OverloadHeaderDigest> overload;
  return require(identity::DefinitionIdentityRecord::from(
      module(owner), zc::mv(owners), identity::DefinitionKind::Class,
      identity::DefinitionNamespace::Type, declaredName("Owner"_zc), zc::mv(overload)));
}

identity::DefinitionIdentityRecord functionRecord(zc::StringPtr owner) {
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  zc::Maybe<identity::OverloadHeaderDigest> overload =
      digestKey<identity::OverloadHeaderDigest>(0x55);
  return require(identity::DefinitionIdentityRecord::from(
      module(owner), zc::mv(owners), identity::DefinitionKind::Function,
      identity::DefinitionNamespace::Value, declaredName("run"_zc), zc::mv(overload)));
}

identity::CanonicalNameReference canonicalName(zc::StringPtr text) {
  zc::Vector<identity::SemanticIdentifier> suffix;
  suffix.add(tests::test_identity_detail::scalar<identity::SemanticIdentifier>(text));
  return require(identity::CanonicalNameReference::from(identity::CanonicalNameRoot::relative(),
                                                        zc::mv(suffix)));
}

identity::CanonicalHeaderTypeSyntax namedHeaderType(zc::StringPtr text) {
  zc::Vector<identity::CanonicalHeaderTypeSyntax> arguments;
  return identity::CanonicalHeaderTypeSyntax::named(
      identity::CanonicalNamedHeaderType::from(canonicalName(text), zc::mv(arguments)));
}

identity::ImplIdentityRecord implementationRecord(zc::StringPtr owner) {
  zc::Vector<identity::CanonicalHeaderTypeSyntax> arguments;
  auto trait = require(
      identity::CanonicalTraitReference::from(canonicalName("Trait"_zc), zc::mv(arguments)));
  zc::Vector<identity::CanonicalGenericParameter> generics;
  zc::Vector<identity::CanonicalBoundObligation> obligations;
  auto header = require(identity::CanonicalImplHeader::from(
      zc::mv(generics), identity::ImplPolarity::Positive, identity::ImplSafety::Safe, zc::mv(trait),
      namedHeaderType("T"_zc), zc::mv(obligations)));
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  return identity::ImplIdentityRecord::from(module(owner), zc::mv(owners), zc::mv(header));
}

ImplSourceOccurrenceKey implementationOccurrence(const identity::ImplKey& implementation,
                                                 uint32_t pathComponent = 0) {
  zc::Vector<uint32_t> path;
  if (pathComponent != 0) { path.add(pathComponent); }
  auto site = require(IdentitySyntaxSiteKey::from(
      module("owner"_zc), tests::test_identity_detail::source(), zc::mv(path)));
  return ImplSourceOccurrenceKey::from(implementation.clone(), zc::mv(site));
}

LocalSyntaxPath localPath(uint32_t component = 1) {
  zc::Vector<uint32_t> components;
  components.add(component);
  return require(LocalSyntaxPath::from(zc::mv(components)));
}

StableOwnerBodyQueryKey ownerBody(zc::StringPtr owner = "owner"_zc) {
  return require(
      StableOwnerBodyQueryKey::from(module(owner), StableBodyOwnerKey::module(module(owner))));
}

StableOwnerBodyQueryKey foreignOwnerBody(const StableOwnerBodyQueryKey& owner) {
  if (owner.owner().kind() == StableBodyOwnerKind::Module) { return ownerBody("other"_zc); }
  return require(StableOwnerBodyQueryKey::from(
      module("other"_zc),
      StableBodyOwnerKey::definition(ZC_ASSERT_NONNULL(owner.owner().definitionKey()).clone())));
}

void expectSumWire(const StableScopeOwnerKey& value);
void expectSumWire(const StableNodeSyntaxRoot& value);
void expectSumWire(const StableBindingTargetKey& value);
void expectSumWire(const StableSelfOwner& value);
void expectSumWire(const StableLabelTarget& value);
void expectSumWire(const StableControlTarget& value);
zc::Array<uint8_t> ownerLocalTargetWire(const StableOwnerBodyQueryKey& owner,
                                        const OwnerLocalBindingKey& binding);
zc::Array<uint8_t> anonymousTargetWire(const StableOwnerBodyQueryKey& owner,
                                       const AnonymousOwnerLocalKey& binding);
void encodeHeaderSiteOracle(identity::CanonicalEncoder& encoder, const StableHeaderSite& site);
template <typename... T>
void expectSumWires(const T&... values) {
  (expectSumWire(values), ...);
}
enum class DefinitionHeaderMutation {
  None,
  WithoutVisibility,
  QueryIdentity,
  QueryOwner,
  AuthoritySite,
  Kind,
  Namespace,
  Name,
  Activation,
  Visibility,
  BodyDisposition,
  GenericOwner,
  GenericSite,
  GenericOrdinal,
  GenericDuplicate,
  CallableOwner,
  CallableSite,
  CallablePosition,
  CallableDuplicate,
  ScopeRole
};

template <typename T>
void addCanonicalPair(zc::Vector<T>& values, T&& left, T&& right) {
  const auto leftBytes = StableBindingCodec<T>::encode(left);
  const auto rightBytes = StableBindingCodec<T>::encode(right);
  if (stable_binding_codec_detail::compareBytes(leftBytes.asPtr(), rightBytes.asPtr()) < 0) {
    values.add(zc::mv(left));
    values.add(zc::mv(right));
  } else {
    values.add(zc::mv(right));
    values.add(zc::mv(left));
  }
}

zc::Maybe<StableDefinitionHeader> definitionHeader(DefinitionHeaderMutation mutation) {
  auto record = functionRecord("owner"_zc);
  auto definition = identity::DefinitionKey::compute(record);
  auto queryDefinition = mutation == DefinitionHeaderMutation::QueryIdentity
                             ? digestKey<identity::DefinitionKey>(0x77)
                             : definition.clone();
  auto queryKey = StableDefinitionQueryKey::from(
      module(mutation == DefinitionHeaderMutation::QueryOwner ? "foreign"_zc : "owner"_zc),
      zc::mv(queryDefinition));
  auto authoritySite =
      headerSite(mutation == DefinitionHeaderMutation::AuthoritySite ? "foreign"_zc : "owner"_zc);

  auto genericOwner = mutation == DefinitionHeaderMutation::GenericOwner
                          ? identity::DefinitionKey::compute(functionRecord("foreign"_zc))
                          : definition.clone();
  const uint32_t genericOrdinal = mutation == DefinitionHeaderMutation::GenericOrdinal ? 1 : 0;
  auto genericRecord = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::definition(zc::mv(genericOwner)), genericOrdinal);
  auto generic = require(StableHeaderGenericParameter::from(
      identity::GenericParameterKey::compute(genericRecord), genericRecord.clone(),
      StableHeaderSite::definition(headerSite(
          mutation == DefinitionHeaderMutation::GenericSite ? "foreign"_zc : "owner"_zc)),
      declaredName("T"_zc), genericOrdinal));
  zc::Vector<StableHeaderGenericParameter> genericValues;
  if (mutation == DefinitionHeaderMutation::GenericDuplicate) {
    auto duplicateRecord = identity::GenericParameterIdentityRecord::type(
        identity::StableGenericParameterOwnerKey::definition(definition.clone()), 0);
    auto duplicate = require(StableHeaderGenericParameter::from(
        identity::GenericParameterKey::compute(duplicateRecord), duplicateRecord.clone(),
        StableHeaderSite::definition(headerSite("owner"_zc)), declaredName("U"_zc), 0));
    addCanonicalPair(genericValues, zc::mv(generic), zc::mv(duplicate));
  } else {
    genericValues.add(zc::mv(generic));
  }
  auto generics = require(
      StableBindingSequenceBuilder<StableHeaderGenericParameter>::from(zc::mv(genericValues)));

  auto callableOwner = mutation == DefinitionHeaderMutation::CallableOwner
                           ? identity::DefinitionKey::compute(functionRecord("foreign"_zc))
                           : definition.clone();
  auto callablePosition = identity::CallableParameterPosition::ordinary(
      mutation == DefinitionHeaderMutation::CallablePosition ? 1 : 0);
  auto callableRecord =
      identity::CallableParameterIdentityRecord::from(zc::mv(callableOwner), callablePosition);
  auto callableName = optionalName("value"_zc);
  auto callable = require(StableHeaderCallableParameter::from(
      identity::CallableParameterKey::compute(callableRecord), callableRecord.clone(),
      StableHeaderSite::definition(headerSite(
          mutation == DefinitionHeaderMutation::CallableSite ? "foreign"_zc : "owner"_zc)),
      zc::mv(callableName), callablePosition));
  zc::Vector<StableHeaderCallableParameter> callableValues;
  if (mutation == DefinitionHeaderMutation::CallableDuplicate) {
    auto duplicateRecord = identity::CallableParameterIdentityRecord::from(
        definition.clone(), identity::CallableParameterPosition::ordinary(0));
    auto duplicateName = optionalName("other"_zc);
    auto duplicate = require(StableHeaderCallableParameter::from(
        identity::CallableParameterKey::compute(duplicateRecord), duplicateRecord.clone(),
        StableHeaderSite::definition(headerSite("owner"_zc)), zc::mv(duplicateName),
        identity::CallableParameterPosition::ordinary(0)));
    addCanonicalPair(callableValues, zc::mv(callable), zc::mv(duplicate));
  } else {
    callableValues.add(zc::mv(callable));
  }
  auto callables = require(
      StableBindingSequenceBuilder<StableHeaderCallableParameter>::from(zc::mv(callableValues)));

  zc::Vector<ScopeRole> roleValues;
  roleValues.add(mutation == DefinitionHeaderMutation::ScopeRole ? static_cast<ScopeRole>(0xff)
                                                                 : ScopeRole::Declaration);
  auto roles = require(StableBindingSequenceBuilder<ScopeRole>::from(zc::mv(roleValues)));
  zc::Maybe<MemberVisibility> visibility;
  if (mutation != DefinitionHeaderMutation::WithoutVisibility) {
    visibility = mutation == DefinitionHeaderMutation::Visibility
                     ? static_cast<MemberVisibility>(0xff)
                     : MemberVisibility::Public;
  }
  return StableDefinitionHeader::from(
      zc::mv(queryKey), zc::mv(record), zc::mv(authoritySite),
      mutation == DefinitionHeaderMutation::Kind ? identity::DefinitionKind::Class
                                                 : identity::DefinitionKind::Function,
      mutation == DefinitionHeaderMutation::Namespace ? Namespace::Type : Namespace::Value,
      declaredName(mutation == DefinitionHeaderMutation::Name ? "other"_zc : "run"_zc),
      mutation == DefinitionHeaderMutation::Activation ? static_cast<DefinitionActivation>(0xff)
                                                       : DefinitionActivation::ModuleSkeleton,
      zc::mv(visibility),
      mutation == DefinitionHeaderMutation::BodyDisposition
          ? static_cast<DefinitionBodyDisposition>(0xff)
          : DefinitionBodyDisposition::ExecutableBody,
      zc::mv(generics), zc::mv(callables), zc::mv(roles));
}

enum class ImplementationHeaderMutation {
  None,
  Bodyless,
  QueryImplementation,
  OccurrenceSite,
  AuthorityModule,
  AuthorityImplementation,
  Record,
  GenericOwner,
  GenericSite,
  GenericOrdinal,
  GenericDuplicate,
  ScopeRole,
  SourceForm
};

zc::Maybe<StableImplementationOccurrenceHeader> implementationHeader(
    ImplementationHeaderMutation mutation) {
  auto record = implementationRecord(mutation == ImplementationHeaderMutation::Record ? "foreign"_zc
                                                                                      : "owner"_zc);
  auto implementation = mutation == ImplementationHeaderMutation::Record
                            ? identity::ImplKey::compute(implementationRecord("owner"_zc))
                            : identity::ImplKey::compute(record);
  auto queryImplementation = mutation == ImplementationHeaderMutation::QueryImplementation
                                 ? digestKey<identity::ImplKey>(0x77)
                                 : implementation.clone();
  auto queryOccurrence = implementationOccurrence(
      queryImplementation, mutation == ImplementationHeaderMutation::OccurrenceSite ? 1 : 0);
  auto queryKey = require(
      StableImplementationOccurrenceQueryKey::from(module("owner"_zc), zc::mv(queryOccurrence)));
  auto authority = StableImplementationQueryKey::from(
      module(mutation == ImplementationHeaderMutation::AuthorityModule ? "foreign"_zc : "owner"_zc),
      mutation == ImplementationHeaderMutation::AuthorityImplementation
          ? digestKey<identity::ImplKey>(0x66)
          : implementation.clone());

  auto genericOwner = mutation == ImplementationHeaderMutation::GenericOwner
                          ? digestKey<identity::ImplKey>(0x55)
                          : implementation.clone();
  const uint32_t ordinal = mutation == ImplementationHeaderMutation::GenericOrdinal ? 1 : 0;
  auto genericRecord = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::implementation(genericOwner.clone()), ordinal);
  auto generic = require(StableHeaderGenericParameter::from(
      identity::GenericParameterKey::compute(genericRecord), genericRecord.clone(),
      StableHeaderSite::implementation(implementationOccurrence(
          genericOwner, mutation == ImplementationHeaderMutation::GenericSite ? 1 : 0)),
      declaredName("T"_zc), ordinal));
  zc::Vector<StableHeaderGenericParameter> genericValues;
  if (mutation == ImplementationHeaderMutation::GenericDuplicate) {
    auto duplicateRecord = identity::GenericParameterIdentityRecord::type(
        identity::StableGenericParameterOwnerKey::implementation(implementation.clone()), 0);
    auto duplicate = require(StableHeaderGenericParameter::from(
        identity::GenericParameterKey::compute(duplicateRecord), duplicateRecord.clone(),
        StableHeaderSite::implementation(implementationOccurrence(implementation)),
        declaredName("U"_zc), 0));
    addCanonicalPair(genericValues, zc::mv(generic), zc::mv(duplicate));
  } else {
    genericValues.add(zc::mv(generic));
  }
  auto generics = require(
      StableBindingSequenceBuilder<StableHeaderGenericParameter>::from(zc::mv(genericValues)));
  zc::Vector<ScopeRole> roleValues;
  roleValues.add(mutation == ImplementationHeaderMutation::ScopeRole ? static_cast<ScopeRole>(0xff)
                                                                     : ScopeRole::Implementation);
  auto roles = require(StableBindingSequenceBuilder<ScopeRole>::from(zc::mv(roleValues)));
  return StableImplementationOccurrenceHeader::from(
      zc::mv(queryKey), zc::mv(authority), zc::mv(record), zc::mv(generics), zc::mv(roles),
      mutation == ImplementationHeaderMutation::SourceForm
          ? static_cast<ImplementationSourceForm>(0xff)
      : mutation == ImplementationHeaderMutation::Bodyless
          ? ImplementationSourceForm::BodylessMarker
          : ImplementationSourceForm::Ordinary);
}

identity::DefinitionIdentityRecord nestedFunctionRecord() {
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  owners.add(
      identity::EnclosingStableOwnerKey::definition(digestKey<identity::DefinitionKey>(0x55)));
  zc::Maybe<identity::OverloadHeaderDigest> overload =
      digestKey<identity::OverloadHeaderDigest>(0x55);
  return require(identity::DefinitionIdentityRecord::from(
      module("owner"_zc), zc::mv(owners), identity::DefinitionKind::Function,
      identity::DefinitionNamespace::Value, declaredName("run"_zc), zc::mv(overload)));
}

identity::ImplIdentityRecord alternativeImplementationRecord() {
  zc::Vector<identity::CanonicalHeaderTypeSyntax> arguments;
  auto trait = require(
      identity::CanonicalTraitReference::from(canonicalName("OtherTrait"_zc), zc::mv(arguments)));
  zc::Vector<identity::CanonicalGenericParameter> generics;
  zc::Vector<identity::CanonicalBoundObligation> obligations;
  auto header = require(identity::CanonicalImplHeader::from(
      zc::mv(generics), identity::ImplPolarity::Positive, identity::ImplSafety::Safe, zc::mv(trait),
      namedHeaderType("T"_zc), zc::mv(obligations)));
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  return identity::ImplIdentityRecord::from(module("owner"_zc), zc::mv(owners), zc::mv(header));
}

enum class DeclarationFactMutation {
  None,
  OtherIdentity,
  OtherSameShapeIdentity,
  NestedScope,
  ImportActivation,
  PrivateVisibility,
  WithoutVisibility,
  QueryIdentity,
  QueryModule,
  Record,
  Scope,
  Kind,
  Namespace,
  Name,
  Activation,
  Visibility
};

zc::Maybe<StableDeclarationFact> declarationFact(DeclarationFactMutation mutation) {
  const bool otherIdentity = mutation == DeclarationFactMutation::OtherIdentity;
  auto identityRecord = otherIdentity ? definitionRecord("owner"_zc)
                        : mutation == DeclarationFactMutation::OtherSameShapeIdentity
                            ? nestedFunctionRecord()
                            : functionRecord("owner"_zc);
  auto record = mutation == DeclarationFactMutation::Record ? functionRecord("foreign"_zc)
                                                            : identityRecord.clone();
  auto definition = mutation == DeclarationFactMutation::QueryIdentity
                        ? digestKey<identity::DefinitionKey>(0x77)
                        : identity::DefinitionKey::compute(identityRecord);
  auto queryKey = StableDefinitionQueryKey::from(
      module(mutation == DeclarationFactMutation::QueryModule ? "foreign"_zc : "owner"_zc),
      zc::mv(definition));
  auto declaringScope = StableScopeOwnerKey::module(
      module(mutation == DeclarationFactMutation::Scope ? "foreign"_zc : "owner"_zc));
  if (mutation == DeclarationFactMutation::NestedScope) {
    declaringScope = require(StableScopeOwnerKey::definition(
        StableDefinitionQueryKey::from(module("owner"_zc),
                                       digestKey<identity::DefinitionKey>(0x44)),
        ScopeRole::Declaration));
  }
  zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
  if (mutation == DeclarationFactMutation::PrivateVisibility) {
    visibility = MemberVisibility::Private;
  } else if (mutation == DeclarationFactMutation::WithoutVisibility) {
    visibility = zc::none;
  } else if (mutation == DeclarationFactMutation::Visibility) {
    visibility = static_cast<MemberVisibility>(0xff);
  }
  return StableDeclarationFact::from(
      zc::mv(queryKey), zc::mv(record), zc::mv(declaringScope),
      otherIdentity || mutation == DeclarationFactMutation::Kind
          ? identity::DefinitionKind::Class
          : identity::DefinitionKind::Function,
      otherIdentity || mutation == DeclarationFactMutation::Namespace ? Namespace::Type
                                                                      : Namespace::Value,
      declaredName(otherIdentity                               ? "Owner"_zc
                   : mutation == DeclarationFactMutation::Name ? "other"_zc
                                                               : "run"_zc),
      mutation == DeclarationFactMutation::Activation ? static_cast<DefinitionActivation>(0xff)
      : mutation == DeclarationFactMutation::ImportActivation
          ? DefinitionActivation::ImportSurface
          : DefinitionActivation::ModuleSkeleton,
      zc::mv(visibility));
}

IdentitySyntaxSiteKey parameterHeaderSite(zc::StringPtr owner = "owner"_zc,
                                          uint32_t component = 0) {
  zc::Vector<uint32_t> path;
  if (component != 0) { path.add(component); }
  return require(IdentitySyntaxSiteKey::from(module(owner), tests::test_identity_detail::source(),
                                             zc::mv(path)));
}

enum class GenericDeclarationMutation {
  None,
  OtherIdentity,
  Implementation,
  HeaderSite,
  Scope,
  Name,
  QueryKey,
  QueryModule,
  HeaderSiteModule,
  SiteKind,
  ImplementationOwner,
  ScopeModule
};

zc::Maybe<StableGenericParameterDeclarationFact> genericDeclaration(
    GenericDeclarationMutation mutation) {
  const bool implementation = mutation == GenericDeclarationMutation::Implementation ||
                              mutation == GenericDeclarationMutation::SiteKind ||
                              mutation == GenericDeclarationMutation::ImplementationOwner;
  auto ownerKey = digestKey<identity::ImplKey>(
      mutation == GenericDeclarationMutation::ImplementationOwner ? 0x77 : 0x66);
  auto owner =
      implementation
          ? identity::StableGenericParameterOwnerKey::implementation(ownerKey.clone())
          : identity::StableGenericParameterOwnerKey::definition(digestKey<identity::DefinitionKey>(
                mutation == GenericDeclarationMutation::OtherIdentity ? 0x22 : 0x11));
  auto record = identity::GenericParameterIdentityRecord::type(zc::mv(owner), 0);
  auto queryKey = StableGenericParameterQueryKey::from(
      module(mutation == GenericDeclarationMutation::QueryModule ? "foreign"_zc : "owner"_zc),
      mutation == GenericDeclarationMutation::QueryKey
          ? digestKey<identity::GenericParameterKey>(0x55)
          : identity::GenericParameterKey::compute(record));
  auto site = implementation && mutation != GenericDeclarationMutation::SiteKind
                  ? StableHeaderSite::implementation(implementationOccurrence(
                        mutation == GenericDeclarationMutation::ImplementationOwner
                            ? digestKey<identity::ImplKey>(0x66)
                            : ownerKey.clone()))
                  : StableHeaderSite::definition(parameterHeaderSite(
                        mutation == GenericDeclarationMutation::HeaderSiteModule ? "foreign"_zc
                                                                                 : "owner"_zc,
                        mutation == GenericDeclarationMutation::HeaderSite ? 1 : 0));
  auto scope =
      mutation == GenericDeclarationMutation::Scope
          ? require(StableScopeOwnerKey::definition(
                StableDefinitionQueryKey::from(module("owner"_zc),
                                               digestKey<identity::DefinitionKey>(0x44)),
                ScopeRole::Generic))
          : StableScopeOwnerKey::module(module(
                mutation == GenericDeclarationMutation::ScopeModule ? "foreign"_zc : "owner"_zc));
  return StableGenericParameterDeclarationFact::from(
      zc::mv(queryKey), zc::mv(record), zc::mv(site), zc::mv(scope),
      declaredName(mutation == GenericDeclarationMutation::Name ? "U"_zc : "T"_zc));
}

enum class CallableDeclarationMutation {
  None,
  OtherIdentity,
  Receiver,
  HeaderSite,
  Scope,
  Name,
  QueryKey,
  QueryModule,
  HeaderSiteModule,
  ImplementationSite,
  ScopeModule,
  ReceiverName,
  OrdinaryWithoutName
};

zc::Maybe<StableCallableParameterDeclarationFact> callableDeclaration(
    CallableDeclarationMutation mutation) {
  const bool receiver = mutation == CallableDeclarationMutation::Receiver ||
                        mutation == CallableDeclarationMutation::ReceiverName;
  auto position = receiver ? identity::CallableParameterPosition::receiver()
                           : identity::CallableParameterPosition::ordinary(0);
  auto record = identity::CallableParameterIdentityRecord::from(
      digestKey<identity::DefinitionKey>(
          mutation == CallableDeclarationMutation::OtherIdentity ? 0x22 : 0x11),
      position);
  auto queryKey = StableCallableParameterQueryKey::from(
      module(mutation == CallableDeclarationMutation::QueryModule ? "foreign"_zc : "owner"_zc),
      mutation == CallableDeclarationMutation::QueryKey
          ? digestKey<identity::CallableParameterKey>(0x55)
          : identity::CallableParameterKey::compute(record));
  auto site = mutation == CallableDeclarationMutation::ImplementationSite
                  ? StableHeaderSite::implementation(occurrence("owner"_zc))
                  : StableHeaderSite::definition(parameterHeaderSite(
                        mutation == CallableDeclarationMutation::HeaderSiteModule ? "foreign"_zc
                                                                                  : "owner"_zc,
                        mutation == CallableDeclarationMutation::HeaderSite ? 1 : 0));
  auto scope =
      mutation == CallableDeclarationMutation::Scope
          ? require(StableScopeOwnerKey::definition(
                StableDefinitionQueryKey::from(module("owner"_zc),
                                               digestKey<identity::DefinitionKey>(0x44)),
                ScopeRole::Parameters))
          : StableScopeOwnerKey::module(module(
                mutation == CallableDeclarationMutation::ScopeModule ? "foreign"_zc : "owner"_zc));
  zc::Maybe<identity::DeclaredDefinitionName> name;
  if (!receiver && mutation != CallableDeclarationMutation::OrdinaryWithoutName) {
    name = declaredName(mutation == CallableDeclarationMutation::Name ? "other"_zc : "value"_zc);
  } else if (mutation == CallableDeclarationMutation::ReceiverName) {
    name = declaredName("self"_zc);
  }
  return StableCallableParameterDeclarationFact::from(zc::mv(queryKey), zc::mv(record),
                                                      zc::mv(site), zc::mv(scope), zc::mv(name));
}

identity::SemanticImportBindingKey semanticBinding(identity::SemanticImportOperation operation,
                                                   identity::DefinitionNamespace nameSpace,
                                                   zc::StringPtr localName = "local"_zc) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::ModulePathSegment>("dep"_zc));
  zc::Maybe<zc::Vector<identity::ModulePathSegment>> retainedPath(zc::mv(path));
  zc::Maybe<identity::DependencyAlias> noAlias;
  identity::ModuleDependencyKind dependencyKind;
  switch (operation) {
    case identity::SemanticImportOperation::Import:
      dependencyKind = identity::ModuleDependencyKind::Import;
      break;
    case identity::SemanticImportOperation::ForeignReexport:
      dependencyKind = identity::ModuleDependencyKind::ForeignReexport;
      break;
    case identity::SemanticImportOperation::ModuleAlias:
      dependencyKind = identity::ModuleDependencyKind::ModuleAlias;
      break;
  }
  auto resolution = require(identity::ModuleResolutionKey::from(
      module("owner"_zc), dependencyKind, zc::mv(retainedPath), zc::mv(noAlias), policy()));
  return require(identity::SemanticImportBindingKey::from(
      module("owner"_zc), zc::mv(resolution), operation, nameSpace, declaredName("source"_zc),
      nameSpace, declaredName(localName)));
}

ModuleAliasExportNamesRevision moduleAliasExportNamesRevision(uint8_t seed) {
  return ModuleAliasExportNamesRevision::fromDigest(digestKey<identity::Sha256Digest>(seed));
}

enum class ImportFactMutation {
  None,
  Query,
  Scope,
  Target,
  CanonicalTarget,
  TypeNamespace,
  Prelude,
  NoVisibility,
  PrivateVisibility,
  Reexport,
  NamespaceMismatch,
  ScopeModule,
  Origin,
  Visibility,
  ImportedExported,
  ReexportNotExported
};

zc::Maybe<StableImportFact> importFact(ImportFactMutation mutation) {
  const bool reexport = mutation == ImportFactMutation::Reexport ||
                        mutation == ImportFactMutation::ReexportNotExported;
  auto binding = semanticBinding(reexport ? identity::SemanticImportOperation::ForeignReexport
                                          : identity::SemanticImportOperation::Import,
                                 mutation == ImportFactMutation::TypeNamespace
                                     ? identity::DefinitionNamespace::Type
                                     : identity::DefinitionNamespace::Value,
                                 mutation == ImportFactMutation::Query ? "other"_zc : "local"_zc);
  auto queryKey = require(StableSemanticImportQueryKey::from(module("owner"_zc), zc::mv(binding)));
  auto scope = mutation == ImportFactMutation::Scope
                   ? require(StableScopeOwnerKey::definition(
                         StableDefinitionQueryKey::from(module("owner"_zc),
                                                        digestKey<identity::DefinitionKey>(0x44)),
                         ScopeRole::Declaration))
                   : StableScopeOwnerKey::module(module(
                         mutation == ImportFactMutation::ScopeModule ? "foreign"_zc : "owner"_zc));
  auto target = mutation == ImportFactMutation::Target
                    ? StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
                          module("target"_zc), digestKey<identity::DefinitionKey>(0x22)))
                    : StableBindingTargetKey::module(module("target"_zc));
  auto canonicalTarget = mutation == ImportFactMutation::CanonicalTarget
                             ? StableBindingTargetKey::module(module("canonical"_zc))
                             : StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
                                   module("target"_zc), digestKey<identity::DefinitionKey>(0x11)));
  zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
  if (mutation == ImportFactMutation::NoVisibility) {
    visibility = zc::none;
  } else if (mutation == ImportFactMutation::PrivateVisibility) {
    visibility = MemberVisibility::Private;
  } else if (mutation == ImportFactMutation::Visibility) {
    visibility = static_cast<MemberVisibility>(0xff);
  }
  const auto origin = mutation == ImportFactMutation::Origin    ? BindingOrigin::LocalDeclaration
                      : mutation == ImportFactMutation::Prelude ? BindingOrigin::Prelude
                      : reexport                                ? BindingOrigin::ReexportAlias
                                                                : BindingOrigin::ImportAlias;
  const bool exported = mutation == ImportFactMutation::ImportedExported ||
                        (reexport && mutation != ImportFactMutation::ReexportNotExported);
  return StableImportFact::from(zc::mv(queryKey), zc::mv(scope), zc::mv(target),
                                zc::mv(canonicalTarget),
                                mutation == ImportFactMutation::NamespaceMismatch ||
                                        mutation == ImportFactMutation::TypeNamespace
                                    ? Namespace::Type
                                    : Namespace::Value,
                                origin, zc::mv(visibility), exported);
}

enum class ModuleAliasMutation {
  None,
  Query,
  Scope,
  Alias,
  CanonicalModule,
  Revision,
  Namespace,
  ScopeModule,
  AliasModule
};

zc::Maybe<StableModuleAliasFact> moduleAliasFact(ModuleAliasMutation mutation) {
  auto binding = semanticBinding(identity::SemanticImportOperation::ModuleAlias,
                                 mutation == ModuleAliasMutation::Namespace
                                     ? identity::DefinitionNamespace::Value
                                     : identity::DefinitionNamespace::Module,
                                 mutation == ModuleAliasMutation::Query ? "other"_zc : "local"_zc);
  auto queryKey = require(StableSemanticImportQueryKey::from(module("owner"_zc), zc::mv(binding)));
  auto scope = mutation == ModuleAliasMutation::Scope
                   ? require(StableScopeOwnerKey::definition(
                         StableDefinitionQueryKey::from(module("owner"_zc),
                                                        digestKey<identity::DefinitionKey>(0x44)),
                         ScopeRole::Declaration))
                   : StableScopeOwnerKey::module(module(
                         mutation == ModuleAliasMutation::ScopeModule ? "foreign"_zc : "owner"_zc));
  auto alias = StableDefinitionQueryKey::from(
      module(mutation == ModuleAliasMutation::AliasModule ? "foreign"_zc : "owner"_zc),
      digestKey<identity::DefinitionKey>(mutation == ModuleAliasMutation::Alias ? 0x22 : 0x11));
  return StableModuleAliasFact::from(
      zc::mv(queryKey), zc::mv(scope), zc::mv(alias),
      module(mutation == ModuleAliasMutation::CanonicalModule ? "other"_zc : "target"_zc),
      moduleAliasExportNamesRevision(mutation == ModuleAliasMutation::Revision ? 0x22 : 0x11));
}

enum class ReexportStepMutation { None, Module, ExportPath, Binding, CanonicalTarget };

StableReexportStep reexportStep(ReexportStepMutation mutation) {
  return StableReexportStep::from(
      module(mutation == ReexportStepMutation::Module ? "other"_zc : "owner"_zc),
      localPath(mutation == ReexportStepMutation::ExportPath ? 2 : 1),
      StableBindingTargetKey::module(
          module(mutation == ReexportStepMutation::Binding ? "otherbinding"_zc : "binding"_zc)),
      StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
          module("target"_zc),
          digestKey<identity::DefinitionKey>(
              mutation == ReexportStepMutation::CanonicalTarget ? 0x22 : 0x11))));
}

enum class LocalExportMutation {
  None,
  DeclaringModule,
  ExportPath,
  Namespace,
  Name,
  Binding,
  CanonicalTarget,
  NoVisibility,
  PrivateVisibility,
  Visibility,
  ReexportChain
};

zc::Maybe<StableLocalExportFact> localExportFact(LocalExportMutation mutation) {
  auto name = require(BindingNameKey::from(
      mutation == LocalExportMutation::Namespace ? Namespace::Type : Namespace::Value,
      declaredName(mutation == LocalExportMutation::Name ? "other"_zc : "name"_zc)));
  auto binding = StableBindingTargetKey::module(
      module(mutation == LocalExportMutation::Binding ? "otherbinding"_zc : "binding"_zc));
  auto canonicalTarget = StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
      module("target"_zc), digestKey<identity::DefinitionKey>(
                               mutation == LocalExportMutation::CanonicalTarget ? 0x22 : 0x11)));
  zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
  if (mutation == LocalExportMutation::NoVisibility) {
    visibility = zc::none;
  } else if (mutation == LocalExportMutation::PrivateVisibility) {
    visibility = MemberVisibility::Private;
  } else if (mutation == LocalExportMutation::Visibility) {
    visibility = static_cast<MemberVisibility>(0xff);
  }
  auto reexportChain = CanonicalSequence<StableReexportStep>::empty();
  if (mutation == LocalExportMutation::ReexportChain) {
    zc::Vector<StableReexportStep> steps;
    steps.add(reexportStep(ReexportStepMutation::None));
    reexportChain = require(StableBindingSequenceBuilder<StableReexportStep>::from(zc::mv(steps)));
  }
  return StableLocalExportFact::from(
      module(mutation == LocalExportMutation::DeclaringModule ? "other"_zc : "owner"_zc),
      localPath(mutation == LocalExportMutation::ExportPath ? 2 : 1), zc::mv(name), zc::mv(binding),
      zc::mv(canonicalTarget), zc::mv(visibility), zc::mv(reexportChain));
}

StableFailedLookupOutcome ambiguousLookupOutcome() {
  auto left = StableBindingTargetKey::module(module("left"_zc));
  auto right = StableBindingTargetKey::definition(
      StableDefinitionQueryKey::from(module("right"_zc), digestKey<identity::DefinitionKey>(0x11)));
  zc::Vector<StableBindingTargetKey> candidates;
  addCanonicalPair(candidates, zc::mv(left), zc::mv(right));
  auto admitted = require(
      StableBindingSequenceBuilder<StableBindingTargetKey>::fromNonEmpty(zc::mv(candidates)));
  return require(StableFailedLookupOutcome::ambiguous(zc::mv(admitted)));
}

StableFailedLookupOutcome namespaceMismatchLookupOutcome(Namespace available = Namespace::Type) {
  zc::Vector<Namespace> namespaces;
  namespaces.add(available);
  auto admitted =
      require(StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(namespaces)));
  return StableFailedLookupOutcome::namespaceMismatch(zc::mv(admitted));
}

enum class FailedLookupMutation {
  None,
  Owner,
  Path,
  Namespace,
  Name,
  Outcome,
  NamespaceMismatch,
  InvalidNamespace
};

zc::Maybe<StableFailedLookupFact> failedLookupFact(FailedLookupMutation mutation) {
  return StableFailedLookupFact::from(
      BinderQueryOwner::module(
          module(mutation == FailedLookupMutation::Owner ? "other"_zc : "owner"_zc)),
      localPath(mutation == FailedLookupMutation::Path ? 2 : 1),
      mutation == FailedLookupMutation::InvalidNamespace ? static_cast<Namespace>(0xff)
      : mutation == FailedLookupMutation::Namespace      ? Namespace::Type
                                                         : Namespace::Value,
      declaredName(mutation == FailedLookupMutation::Name ? "other"_zc : "name"_zc),
      mutation == FailedLookupMutation::Outcome             ? ambiguousLookupOutcome()
      : mutation == FailedLookupMutation::NamespaceMismatch ? namespaceMismatchLookupOutcome()
                                                            : StableFailedLookupOutcome::missing());
}

template <typename T>
CanonicalSequence<T> singletonSequence(T&& value) {
  zc::Vector<T> values;
  values.add(zc::mv(value));
  return require(StableBindingSequenceBuilder<T>::from(zc::mv(values)));
}

template <typename T>
CanonicalSequence<T> pairSequence(T&& left, T&& right) {
  zc::Vector<T> values;
  addCanonicalPair(values, zc::mv(left), zc::mv(right));
  return require(StableBindingSequenceBuilder<T>::from(zc::mv(values)));
}

zc::Maybe<BoundModuleSkeleton> moduleSkeleton(LocalExportMutation exportMutation,
                                              bool includeModuleOwner = true) {
  auto declaration = require(declarationFact(DeclarationFactMutation::None));
  auto alias = require(StableModuleAliasFact::from(
      require(StableSemanticImportQueryKey::from(
          module("owner"_zc), semanticBinding(identity::SemanticImportOperation::ModuleAlias,
                                              identity::DefinitionNamespace::Module))),
      StableScopeOwnerKey::module(module("owner"_zc)), declaration.queryKey().clone(),
      module("target"_zc), moduleAliasExportNamesRevision(0x11)));
  auto definition = declaration.queryKey().definition().clone();
  auto implementationRecordValue = implementationRecord("owner"_zc);
  auto implementation = identity::ImplKey::compute(implementationRecordValue);
  auto implementationOccurrenceKey = require(StableImplementationOccurrenceQueryKey::from(
      module("owner"_zc), implementationOccurrence(implementation, 1)));
  auto implementationFact = require(StableImplementationOccurrenceFact::from(
      implementationOccurrenceKey.clone(),
      StableImplementationQueryKey::from(module("owner"_zc), implementation.clone()),
      zc::mv(implementationRecordValue), StableScopeOwnerKey::module(module("owner"_zc))));
  auto genericRecord = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::definition(definition.clone()), 0);
  auto genericFact = require(StableGenericParameterDeclarationFact::from(
      StableGenericParameterQueryKey::from(module("owner"_zc),
                                           identity::GenericParameterKey::compute(genericRecord)),
      genericRecord.clone(), StableHeaderSite::definition(parameterHeaderSite()),
      StableScopeOwnerKey::module(module("owner"_zc)), declaredName("T"_zc)));
  auto callableRecord = identity::CallableParameterIdentityRecord::from(
      definition.clone(), identity::CallableParameterPosition::ordinary(0));
  auto callableFact = require(StableCallableParameterDeclarationFact::from(
      StableCallableParameterQueryKey::from(
          module("owner"_zc), identity::CallableParameterKey::compute(callableRecord)),
      callableRecord.clone(), StableHeaderSite::definition(parameterHeaderSite()),
      StableScopeOwnerKey::module(module("owner"_zc)), optionalName("value"_zc)));
  zc::Maybe<StableScopeOwnerKey> noParent;
  auto scope = require(StableScopeFact::from(StableScopeOwnerKey::module(module("owner"_zc)),
                                             zc::mv(noParent), ScopeKind::Module));
  auto nodeScope = require(
      StableNodeScopeFact::from(StableNodeSyntaxRoot::moduleBody(module("owner"_zc)), localPath(),
                                StableScopeOwnerKey::module(module("owner"_zc))));
  zc::Vector<StableOwnerBodyQueryKey> bodyOwnerValues;
  auto definitionBody = require(StableOwnerBodyQueryKey::from(
      module("owner"_zc), StableBodyOwnerKey::definition(zc::mv(definition))));
  if (includeModuleOwner) {
    addCanonicalPair(bodyOwnerValues, ownerBody(), zc::mv(definitionBody));
  } else {
    bodyOwnerValues.add(zc::mv(definitionBody));
  }
  auto bodyOwners = require(
      StableBindingSequenceBuilder<StableOwnerBodyQueryKey>::fromNonEmpty(zc::mv(bodyOwnerValues)));
  return BoundModuleSkeleton::from(
      module("owner"_zc), singletonSequence(zc::mv(scope)), singletonSequence(zc::mv(nodeScope)),
      singletonSequence(zc::mv(declaration)), singletonSequence(zc::mv(implementationFact)),
      singletonSequence(zc::mv(genericFact)), singletonSequence(zc::mv(callableFact)),
      singletonSequence(zc::mv(alias)),
      singletonSequence(require(importFact(ImportFactMutation::None))),
      singletonSequence(require(localExportFact(exportMutation))), zc::mv(bodyOwners),
      singletonSequence(require(failedLookupFact(FailedLookupMutation::None))));
}

enum class SkeletonRelationMutation {
  MissingParent,
  ParentCycle,
  DuplicateScopeOwner,
  BodyScope,
  MissingModuleScope,
  MissingDefinitionScopeOwner,
  MissingImplementationScopeOwner,
  DuplicateDeclaration,
  DuplicateOccurrence,
  DuplicateNodePath,
  DuplicateGeneric,
  DuplicateCallable,
  DuplicateAliasImport,
  DuplicateLocalExportName,
  DuplicateFailedLookupPath,
  MissingDeclarationScope,
  MissingOccurrenceScope,
  MissingNodeScope,
  MissingGenericScope,
  MissingCallableScope,
  MissingAliasScope,
  MissingImportScope,
  MissingGenericDefinition,
  MissingCallableDefinition,
  MissingAliasDeclaration,
  MissingDefinitionBodyDeclaration,
  MissingDefinitionLookupOwner,
  MissingImplementationLookupOwner,
  BodyLookupOwner
};

StableScopeOwnerKey definitionScopeOwner(uint8_t key, ScopeRole role) {
  return require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(key)),
      role));
}

StableScopeFact childScope(StableScopeOwnerKey&& owner, StableScopeOwnerKey&& parent,
                           ScopeKind kind) {
  zc::Maybe<StableScopeOwnerKey> retainedParent(zc::mv(parent));
  return require(StableScopeFact::from(zc::mv(owner), zc::mv(retainedParent), kind));
}

zc::Maybe<BoundModuleSkeleton> hostileModuleSkeleton(SkeletonRelationMutation mutation) {
  auto base = require(moduleSkeleton(LocalExportMutation::None));
  auto scopes = base.scopes().clone();
  auto nodeScopes = base.nodeScopes().clone();
  auto declarations = base.declarations().clone();
  auto occurrences = base.implementationOccurrences().clone();
  auto generics = base.genericParameterDeclarations().clone();
  auto callables = base.callableParameterDeclarations().clone();
  auto aliases = base.moduleAliases().clone();
  auto imports = base.imports().clone();
  auto exports = base.localExports().clone();
  auto bodyOwners = base.bodyOwners().clone();
  auto failures = base.failedLookups().clone();

  switch (mutation) {
    case SkeletonRelationMutation::MissingParent: {
      scopes = singletonSequence(childScope(definitionScopeOwner(0x11, ScopeRole::Declaration),
                                            definitionScopeOwner(0x44, ScopeRole::Declaration),
                                            ScopeKind::Function));
      break;
    }
    case SkeletonRelationMutation::ParentCycle: {
      auto leftOwner = definitionScopeOwner(0x44, ScopeRole::Declaration);
      auto rightOwner = definitionScopeOwner(0x55, ScopeRole::Declaration);
      zc::Maybe<StableScopeOwnerKey> leftParent = rightOwner.clone();
      zc::Maybe<StableScopeOwnerKey> rightParent = leftOwner.clone();
      scopes = pairSequence(require(StableScopeFact::from(zc::mv(leftOwner), zc::mv(leftParent),
                                                          ScopeKind::Function)),
                            require(StableScopeFact::from(zc::mv(rightOwner), zc::mv(rightParent),
                                                          ScopeKind::Function)));
      break;
    }
    case SkeletonRelationMutation::DuplicateScopeOwner: {
      auto owner = definitionScopeOwner(0x44, ScopeRole::Declaration);
      scopes =
          pairSequence(childScope(owner.clone(), StableScopeOwnerKey::module(module("owner"_zc)),
                                  ScopeKind::Function),
                       childScope(zc::mv(owner), definitionScopeOwner(0x55, ScopeRole::Declaration),
                                  ScopeKind::Block));
      break;
    }
    case SkeletonRelationMutation::BodyScope:
      scopes = singletonSequence(childScope(StableScopeOwnerKey::body(ownerBody(), localPath()),
                                            StableScopeOwnerKey::module(module("owner"_zc)),
                                            ScopeKind::Block));
      break;
    case SkeletonRelationMutation::MissingModuleScope:
      scopes = CanonicalSequence<StableScopeFact>::empty();
      break;
    case SkeletonRelationMutation::MissingDefinitionScopeOwner:
      scopes = pairSequence(
          base.scopes().values()[0].clone(),
          childScope(definitionScopeOwner(0x44, ScopeRole::Declaration),
                     StableScopeOwnerKey::module(module("owner"_zc)), ScopeKind::Function));
      break;
    case SkeletonRelationMutation::MissingImplementationScopeOwner: {
      auto missingOccurrence = require(
          StableImplementationOccurrenceQueryKey::from(module("owner"_zc), occurrence("owner"_zc)));
      auto missingOwner = require(StableScopeOwnerKey::implementationOccurrence(
          zc::mv(missingOccurrence), ScopeRole::Implementation));
      scopes = pairSequence(
          base.scopes().values()[0].clone(),
          childScope(zc::mv(missingOwner), StableScopeOwnerKey::module(module("owner"_zc)),
                     ScopeKind::ImplBody));
      break;
    }
    case SkeletonRelationMutation::DuplicateDeclaration:
      declarations =
          pairSequence(base.declarations().values()[0].clone(),
                       require(declarationFact(DeclarationFactMutation::PrivateVisibility)));
      break;
    case SkeletonRelationMutation::DuplicateOccurrence: {
      auto scopeOwner = require(StableScopeOwnerKey::definition(
          base.declarations().values()[0].queryKey().clone(), ScopeRole::Declaration));
      scopes = pairSequence(
          base.scopes().values()[0].clone(),
          childScope(scopeOwner.clone(), StableScopeOwnerKey::module(module("owner"_zc)),
                     ScopeKind::Function));
      const auto& value = base.implementationOccurrences().values()[0];
      occurrences =
          pairSequence(value.clone(), require(StableImplementationOccurrenceFact::from(
                                          value.occurrence().clone(), value.authority().clone(),
                                          value.record().clone(), zc::mv(scopeOwner))));
      break;
    }
    case SkeletonRelationMutation::DuplicateNodePath: {
      auto declarationScope = require(StableScopeOwnerKey::definition(
          base.declarations().values()[0].queryKey().clone(), ScopeRole::Declaration));
      auto genericScope = require(StableScopeOwnerKey::definition(
          base.declarations().values()[0].queryKey().clone(), ScopeRole::Generic));
      zc::Vector<StableScopeFact> values;
      values.add(base.scopes().values()[0].clone());
      values.add(childScope(declarationScope.clone(),
                            StableScopeOwnerKey::module(module("owner"_zc)), ScopeKind::Function));
      values.add(childScope(genericScope.clone(), StableScopeOwnerKey::module(module("owner"_zc)),
                            ScopeKind::Function));
      scopes = require(StableBindingSequenceBuilder<StableScopeFact>::from(zc::mv(values)));
      auto root = StableNodeSyntaxRoot::definitionHeader(
          base.declarations().values()[0].queryKey().clone());
      nodeScopes = pairSequence(
          require(StableNodeScopeFact::from(root.clone(), localPath(), zc::mv(declarationScope))),
          require(StableNodeScopeFact::from(zc::mv(root), localPath(), zc::mv(genericScope))));
      break;
    }
    case SkeletonRelationMutation::DuplicateGeneric: {
      const auto& value = base.genericParameterDeclarations().values()[0];
      generics = pairSequence(
          value.clone(),
          require(StableGenericParameterDeclarationFact::from(
              value.queryKey().clone(), value.record().clone(), value.headerSite().clone(),
              value.declaringScope().clone(), declaredName("U"_zc))));
      break;
    }
    case SkeletonRelationMutation::DuplicateCallable: {
      const auto& value = base.callableParameterDeclarations().values()[0];
      auto otherName = optionalName("other"_zc);
      callables = pairSequence(
          value.clone(),
          require(StableCallableParameterDeclarationFact::from(
              value.queryKey().clone(), value.record().clone(), value.headerSite().clone(),
              value.declaringScope().clone(), zc::mv(otherName))));
      break;
    }
    case SkeletonRelationMutation::DuplicateAliasImport: {
      const auto& alias = base.moduleAliases().values()[0];
      zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
      imports = singletonSequence(require(StableImportFact::from(
          alias.queryKey().clone(), alias.declaringScope().clone(),
          StableBindingTargetKey::module(module("target"_zc)),
          StableBindingTargetKey::definition(alias.alias().clone()), Namespace::Module,
          BindingOrigin::ImportAlias, zc::mv(visibility), false)));
      break;
    }
    case SkeletonRelationMutation::DuplicateLocalExportName:
      exports = pairSequence(base.localExports().values()[0].clone(),
                             require(localExportFact(LocalExportMutation::Binding)));
      break;
    case SkeletonRelationMutation::DuplicateFailedLookupPath:
      failures = pairSequence(base.failedLookups().values()[0].clone(),
                              require(failedLookupFact(FailedLookupMutation::Name)));
      break;
    case SkeletonRelationMutation::MissingDeclarationScope:
      declarations =
          singletonSequence(require(declarationFact(DeclarationFactMutation::NestedScope)));
      break;
    case SkeletonRelationMutation::MissingOccurrenceScope: {
      const auto& value = base.implementationOccurrences().values()[0];
      occurrences = singletonSequence(require(StableImplementationOccurrenceFact::from(
          value.occurrence().clone(), value.authority().clone(), value.record().clone(),
          definitionScopeOwner(0x44, ScopeRole::Declaration))));
      break;
    }
    case SkeletonRelationMutation::MissingNodeScope:
      nodeScopes = singletonSequence(require(StableNodeScopeFact::from(
          StableNodeSyntaxRoot::definitionHeader(StableDefinitionQueryKey::from(
              module("owner"_zc), digestKey<identity::DefinitionKey>(0x44))),
          localPath(), definitionScopeOwner(0x44, ScopeRole::Declaration))));
      break;
    case SkeletonRelationMutation::MissingGenericScope:
      generics = singletonSequence(require(genericDeclaration(GenericDeclarationMutation::Scope)));
      break;
    case SkeletonRelationMutation::MissingCallableScope:
      callables =
          singletonSequence(require(callableDeclaration(CallableDeclarationMutation::Scope)));
      break;
    case SkeletonRelationMutation::MissingAliasScope:
      aliases = singletonSequence(require(moduleAliasFact(ModuleAliasMutation::Scope)));
      break;
    case SkeletonRelationMutation::MissingImportScope:
      imports = singletonSequence(require(importFact(ImportFactMutation::Scope)));
      break;
    case SkeletonRelationMutation::MissingGenericDefinition:
      generics = singletonSequence(require(genericDeclaration(GenericDeclarationMutation::None)));
      break;
    case SkeletonRelationMutation::MissingCallableDefinition:
      callables =
          singletonSequence(require(callableDeclaration(CallableDeclarationMutation::None)));
      break;
    case SkeletonRelationMutation::MissingAliasDeclaration:
      aliases = singletonSequence(require(moduleAliasFact(ModuleAliasMutation::None)));
      break;
    case SkeletonRelationMutation::MissingDefinitionBodyDeclaration: {
      zc::Vector<StableOwnerBodyQueryKey> values;
      addCanonicalPair(values, ownerBody(),
                       require(StableOwnerBodyQueryKey::from(
                           module("owner"_zc), StableBodyOwnerKey::definition(
                                                   digestKey<identity::DefinitionKey>(0x44)))));
      bodyOwners = require(
          StableBindingSequenceBuilder<StableOwnerBodyQueryKey>::fromNonEmpty(zc::mv(values)));
      break;
    }
    case SkeletonRelationMutation::MissingDefinitionLookupOwner:
      failures = singletonSequence(require(StableFailedLookupFact::from(
          BinderQueryOwner::definitionHeader(StableDefinitionQueryKey::from(
              module("owner"_zc), digestKey<identity::DefinitionKey>(0x44))),
          localPath(), Namespace::Value, declaredName("name"_zc),
          StableFailedLookupOutcome::missing())));
      break;
    case SkeletonRelationMutation::MissingImplementationLookupOwner:
      failures = singletonSequence(require(
          StableFailedLookupFact::from(BinderQueryOwner::implementationHeader(
                                           require(StableImplementationOccurrenceQueryKey::from(
                                               module("owner"_zc), occurrence("owner"_zc)))),
                                       localPath(), Namespace::Value, declaredName("name"_zc),
                                       StableFailedLookupOutcome::missing())));
      break;
    case SkeletonRelationMutation::BodyLookupOwner:
      failures = singletonSequence(require(StableFailedLookupFact::from(
          BinderQueryOwner::body(ownerBody()), localPath(), Namespace::Value,
          declaredName("name"_zc), StableFailedLookupOutcome::missing())));
      break;
  }
  return BoundModuleSkeleton::from(base.module().clone(), zc::mv(scopes), zc::mv(nodeScopes),
                                   zc::mv(declarations), zc::mv(occurrences), zc::mv(generics),
                                   zc::mv(callables), zc::mv(aliases), zc::mv(imports),
                                   zc::mv(exports), zc::mv(bodyOwners), zc::mv(failures));
}

ZC_TEST("StableBindingFacts.ModuleSkeletonAdmitsCompleteCanonicalComponents") {
  auto skeleton = require(moduleSkeleton(LocalExportMutation::None));
  auto clone = skeleton.clone();
  auto different = require(moduleSkeleton(LocalExportMutation::ExportPath));
  ZC_EXPECT(skeleton == clone && skeleton != different &&
            skeleton.module().encode().asPtr() == module("owner"_zc).encode().asPtr());
  ZC_EXPECT(moduleSkeleton(LocalExportMutation::DeclaringModule) == zc::none);
  ZC_EXPECT(moduleSkeleton(LocalExportMutation::None, false) == zc::none);
}

ZC_TEST("StableBindingFacts.ModuleSkeletonRejectsReachableRelationFailures") {
  SkeletonRelationMutation mutations[] = {
      SkeletonRelationMutation::MissingParent,
      SkeletonRelationMutation::ParentCycle,
      SkeletonRelationMutation::DuplicateScopeOwner,
      SkeletonRelationMutation::BodyScope,
      SkeletonRelationMutation::MissingModuleScope,
      SkeletonRelationMutation::MissingDefinitionScopeOwner,
      SkeletonRelationMutation::MissingImplementationScopeOwner,
      SkeletonRelationMutation::DuplicateDeclaration,
      SkeletonRelationMutation::DuplicateOccurrence,
      SkeletonRelationMutation::DuplicateNodePath,
      SkeletonRelationMutation::DuplicateGeneric,
      SkeletonRelationMutation::DuplicateCallable,
      SkeletonRelationMutation::DuplicateAliasImport,
      SkeletonRelationMutation::DuplicateLocalExportName,
      SkeletonRelationMutation::DuplicateFailedLookupPath,
      SkeletonRelationMutation::MissingDeclarationScope,
      SkeletonRelationMutation::MissingOccurrenceScope,
      SkeletonRelationMutation::MissingNodeScope,
      SkeletonRelationMutation::MissingGenericScope,
      SkeletonRelationMutation::MissingCallableScope,
      SkeletonRelationMutation::MissingAliasScope,
      SkeletonRelationMutation::MissingImportScope,
      SkeletonRelationMutation::MissingGenericDefinition,
      SkeletonRelationMutation::MissingCallableDefinition,
      SkeletonRelationMutation::MissingAliasDeclaration,
      SkeletonRelationMutation::MissingDefinitionBodyDeclaration,
      SkeletonRelationMutation::MissingDefinitionLookupOwner,
      SkeletonRelationMutation::MissingImplementationLookupOwner,
      SkeletonRelationMutation::BodyLookupOwner};
  for (const auto mutation : mutations) { ZC_EXPECT(hostileModuleSkeleton(mutation) == zc::none); }
}

ZC_TEST("StableBindingFacts.CanonicalAdmissionOwnsImpliedSkeletonMultiplicity") {
  auto skeleton = require(moduleSkeleton(LocalExportMutation::None));
  zc::Vector<StableScopeFact> scopes;
  scopes.add(skeleton.scopes().values()[0].clone());
  scopes.add(skeleton.scopes().values()[0].clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableScopeFact>::from(zc::mv(scopes)) == zc::none);
  zc::Vector<StableOwnerBodyQueryKey> owners;
  owners.add(ownerBody());
  owners.add(ownerBody());
  ZC_EXPECT(StableBindingSequenceBuilder<StableOwnerBodyQueryKey>::fromNonEmpty(zc::mv(owners)) ==
            zc::none);
}

enum class SkeletonOwnershipMutation {
  Scope,
  Node,
  Declaration,
  Occurrence,
  Generic,
  Callable,
  Alias,
  Import,
  BodyOwner,
  FailedLookup
};

ImplSourceOccurrenceKey implementationOccurrenceFor(const identity::ImplKey& implementation,
                                                    zc::StringPtr owner) {
  zc::Vector<uint32_t> path;
  auto site = require(IdentitySyntaxSiteKey::from(
      module(owner), tests::test_identity_detail::source(), zc::mv(path)));
  return ImplSourceOccurrenceKey::from(implementation.clone(), zc::mv(site));
}

identity::SemanticImportBindingKey semanticBindingFor(
    zc::StringPtr owner, identity::DefinitionNamespace nameSpace,
    identity::SemanticImportOperation operation = identity::SemanticImportOperation::Import) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::ModulePathSegment>("dep"_zc));
  zc::Maybe<zc::Vector<identity::ModulePathSegment>> retainedPath(zc::mv(path));
  zc::Maybe<identity::DependencyAlias> noAlias;
  identity::ModuleDependencyKind dependencyKind;
  switch (operation) {
    case identity::SemanticImportOperation::Import:
      dependencyKind = identity::ModuleDependencyKind::Import;
      break;
    case identity::SemanticImportOperation::ForeignReexport:
      dependencyKind = identity::ModuleDependencyKind::ForeignReexport;
      break;
    case identity::SemanticImportOperation::ModuleAlias:
      dependencyKind = identity::ModuleDependencyKind::ModuleAlias;
      break;
  }
  auto resolution = require(identity::ModuleResolutionKey::from(
      module(owner), dependencyKind, zc::mv(retainedPath), zc::mv(noAlias), policy()));
  return require(identity::SemanticImportBindingKey::from(
      module(owner), zc::mv(resolution), operation, nameSpace, declaredName("source"_zc), nameSpace,
      declaredName("local"_zc)));
}

zc::Maybe<BoundModuleSkeleton> foreignOwnershipSkeleton(SkeletonOwnershipMutation mutation) {
  auto base = require(moduleSkeleton(LocalExportMutation::None));
  auto scopes = base.scopes().clone();
  auto nodes = base.nodeScopes().clone();
  auto declarations = base.declarations().clone();
  auto occurrences = base.implementationOccurrences().clone();
  auto generics = base.genericParameterDeclarations().clone();
  auto callables = base.callableParameterDeclarations().clone();
  auto aliases = base.moduleAliases().clone();
  auto imports = base.imports().clone();
  auto bodyOwners = base.bodyOwners().clone();
  auto failures = base.failedLookups().clone();
  switch (mutation) {
    case SkeletonOwnershipMutation::Scope: {
      zc::Maybe<StableScopeOwnerKey> noParent;
      scopes = singletonSequence(require(StableScopeFact::from(
          StableScopeOwnerKey::module(module("foreign"_zc)), zc::mv(noParent), ScopeKind::Module)));
      break;
    }
    case SkeletonOwnershipMutation::Node:
      nodes = singletonSequence(require(StableNodeScopeFact::from(
          StableNodeSyntaxRoot::moduleBody(module("foreign"_zc)), localPath(),
          StableScopeOwnerKey::module(module("foreign"_zc)))));
      break;
    case SkeletonOwnershipMutation::Declaration: {
      auto record = functionRecord("foreign"_zc);
      auto query = StableDefinitionQueryKey::from(module("foreign"_zc),
                                                  identity::DefinitionKey::compute(record));
      zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
      declarations = singletonSequence(require(StableDeclarationFact::from(
          zc::mv(query), zc::mv(record), StableScopeOwnerKey::module(module("foreign"_zc)),
          identity::DefinitionKind::Function, Namespace::Value, declaredName("run"_zc),
          DefinitionActivation::ModuleSkeleton, zc::mv(visibility))));
      break;
    }
    case SkeletonOwnershipMutation::Occurrence: {
      auto record = implementationRecord("foreign"_zc);
      auto implementation = identity::ImplKey::compute(record);
      auto occurrence = require(StableImplementationOccurrenceQueryKey::from(
          module("foreign"_zc), implementationOccurrenceFor(implementation, "foreign"_zc)));
      occurrences = singletonSequence(require(StableImplementationOccurrenceFact::from(
          zc::mv(occurrence),
          StableImplementationQueryKey::from(module("foreign"_zc), implementation.clone()),
          zc::mv(record), StableScopeOwnerKey::module(module("foreign"_zc)))));
      break;
    }
    case SkeletonOwnershipMutation::Generic: {
      auto ownerRecord = functionRecord("foreign"_zc);
      auto owner = identity::DefinitionKey::compute(ownerRecord);
      auto record = identity::GenericParameterIdentityRecord::type(
          identity::StableGenericParameterOwnerKey::definition(owner.clone()), 0);
      generics = singletonSequence(require(StableGenericParameterDeclarationFact::from(
          StableGenericParameterQueryKey::from(module("foreign"_zc),
                                               identity::GenericParameterKey::compute(record)),
          record.clone(), StableHeaderSite::definition(parameterHeaderSite("foreign"_zc)),
          StableScopeOwnerKey::module(module("foreign"_zc)), declaredName("T"_zc))));
      break;
    }
    case SkeletonOwnershipMutation::Callable: {
      auto ownerRecord = functionRecord("foreign"_zc);
      auto owner = identity::DefinitionKey::compute(ownerRecord);
      auto position = identity::CallableParameterPosition::ordinary(0);
      auto record = identity::CallableParameterIdentityRecord::from(owner.clone(), position);
      callables = singletonSequence(require(StableCallableParameterDeclarationFact::from(
          StableCallableParameterQueryKey::from(module("foreign"_zc),
                                                identity::CallableParameterKey::compute(record)),
          record.clone(), StableHeaderSite::definition(parameterHeaderSite("foreign"_zc)),
          StableScopeOwnerKey::module(module("foreign"_zc)), optionalName("value"_zc))));
      break;
    }
    case SkeletonOwnershipMutation::Alias: {
      auto query = require(StableSemanticImportQueryKey::from(
          module("foreign"_zc),
          semanticBindingFor("foreign"_zc, identity::DefinitionNamespace::Module,
                             identity::SemanticImportOperation::ModuleAlias)));
      aliases = singletonSequence(require(StableModuleAliasFact::from(
          zc::mv(query), StableScopeOwnerKey::module(module("foreign"_zc)),
          StableDefinitionQueryKey::from(module("foreign"_zc),
                                         digestKey<identity::DefinitionKey>(0x11)),
          module("target"_zc), moduleAliasExportNamesRevision(0x11))));
      break;
    }
    case SkeletonOwnershipMutation::Import: {
      auto query = require(StableSemanticImportQueryKey::from(
          module("foreign"_zc),
          semanticBindingFor("foreign"_zc, identity::DefinitionNamespace::Value)));
      zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
      imports = singletonSequence(require(StableImportFact::from(
          zc::mv(query), StableScopeOwnerKey::module(module("foreign"_zc)),
          StableBindingTargetKey::module(module("target"_zc)),
          StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
              module("target"_zc), digestKey<identity::DefinitionKey>(0x11))),
          Namespace::Value, BindingOrigin::ImportAlias, zc::mv(visibility), false)));
      break;
    }
    case SkeletonOwnershipMutation::BodyOwner: {
      zc::Vector<StableOwnerBodyQueryKey> values;
      values.add(ownerBody("foreign"_zc));
      bodyOwners = require(
          StableBindingSequenceBuilder<StableOwnerBodyQueryKey>::fromNonEmpty(zc::mv(values)));
      break;
    }
    case SkeletonOwnershipMutation::FailedLookup:
      failures = singletonSequence(require(StableFailedLookupFact::from(
          BinderQueryOwner::module(module("foreign"_zc)), localPath(), Namespace::Value,
          declaredName("name"_zc), StableFailedLookupOutcome::missing())));
      break;
  }
  return BoundModuleSkeleton::from(
      base.module().clone(), zc::mv(scopes), zc::mv(nodes), zc::mv(declarations),
      zc::mv(occurrences), zc::mv(generics), zc::mv(callables), zc::mv(aliases), zc::mv(imports),
      base.localExports().clone(), zc::mv(bodyOwners), zc::mv(failures));
}

zc::Maybe<BoundModuleSkeleton> implementationGenericSkeleton(bool retainOccurrence) {
  auto base = require(moduleSkeleton(LocalExportMutation::None));
  const auto& retained = base.implementationOccurrences().values()[0].occurrence();
  auto implementation = retainOccurrence ? retained.occurrence().implementation().clone()
                                         : digestKey<identity::ImplKey>(0x77);
  auto occurrence = retainOccurrence ? retained.occurrence().clone()
                                     : implementationOccurrence(implementation, 9);
  auto record = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::implementation(implementation.clone()), 0);
  auto generic = require(StableGenericParameterDeclarationFact::from(
      StableGenericParameterQueryKey::from(module("owner"_zc),
                                           identity::GenericParameterKey::compute(record)),
      record.clone(), StableHeaderSite::implementation(zc::mv(occurrence)),
      StableScopeOwnerKey::module(module("owner"_zc)), declaredName("I"_zc)));
  auto generics =
      pairSequence(base.genericParameterDeclarations().values()[0].clone(), zc::mv(generic));
  return BoundModuleSkeleton::from(
      base.module().clone(), base.scopes().clone(), base.nodeScopes().clone(),
      base.declarations().clone(), base.implementationOccurrences().clone(), zc::mv(generics),
      base.callableParameterDeclarations().clone(), base.moduleAliases().clone(),
      base.imports().clone(), base.localExports().clone(), base.bodyOwners().clone(),
      base.failedLookups().clone());
}

struct FixtureOrderKey final {
  zc::Array<uint8_t> bytes;
  bool operator<(const FixtureOrderKey& other) const {
    return stable_binding_codec_detail::compareBytes(bytes.asPtr(), other.bytes.asPtr()) < 0;
  }
  bool operator==(const FixtureOrderKey& other) const {
    return bytes.asPtr() == other.bytes.asPtr();
  }
};

template <typename T>
void orderFixture(zc::TreeMap<FixtureOrderKey, T>& values, T&& value) {
  auto bytes = StableBindingCodec<T>::encode(value);
  values.insert(FixtureOrderKey{zc::mv(bytes)}, zc::mv(value));
}

template <typename T>
CanonicalSequence<T> admitOrderedFixtures(zc::TreeMap<FixtureOrderKey, T>& values) {
  zc::Vector<T> ordered(values.size());
  for (auto& entry : values) { ordered.add(zc::mv(entry.value)); }
  return require(StableBindingSequenceBuilder<T>::from(zc::mv(ordered)));
}

template <typename T>
T indexedDigest(uint32_t index) {
  uint8_t bytes[32] = {};
  bytes[28] = static_cast<uint8_t>(index >> 24);
  bytes[29] = static_cast<uint8_t>(index >> 16);
  bytes[30] = static_cast<uint8_t>(index >> 8);
  bytes[31] = static_cast<uint8_t>(index);
  return require(T::fromBytes(zc::arrayPtr(bytes)));
}

identity::DefinitionIdentityRecord chainDefinitionRecord(uint32_t index) {
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  owners.add(identity::EnclosingStableOwnerKey::definition(
      indexedDigest<identity::DefinitionKey>(index + 1)));
  zc::Maybe<identity::OverloadHeaderDigest> overload =
      digestKey<identity::OverloadHeaderDigest>(0x55);
  return require(identity::DefinitionIdentityRecord::from(
      module("owner"_zc), zc::mv(owners), identity::DefinitionKind::Function,
      identity::DefinitionNamespace::Value, declaredName("run"_zc), zc::mv(overload)));
}

zc::Maybe<BoundModuleSkeleton> deepModuleSkeleton(uint32_t depth) {
  auto base = require(moduleSkeleton(LocalExportMutation::None));
  zc::TreeMap<FixtureOrderKey, StableScopeFact> orderedScopes;
  zc::TreeMap<FixtureOrderKey, StableDeclarationFact> orderedDeclarations;
  orderFixture(orderedScopes, base.scopes().values()[0].clone());
  orderFixture(orderedDeclarations, base.declarations().values()[0].clone());
  auto parent = StableScopeOwnerKey::module(module("owner"_zc));
  for (uint32_t index = 0; index < depth; ++index) {
    auto record = chainDefinitionRecord(index);
    auto query = StableDefinitionQueryKey::from(module("owner"_zc),
                                                identity::DefinitionKey::compute(record));
    auto scopeOwner =
        require(StableScopeOwnerKey::definition(query.clone(), ScopeRole::Declaration));
    auto nextParent = scopeOwner.clone();
    orderFixture(orderedScopes,
                 childScope(zc::mv(scopeOwner), zc::mv(parent), ScopeKind::Function));
    parent = zc::mv(nextParent);
    zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
    orderFixture(orderedDeclarations,
                 require(StableDeclarationFact::from(
                     zc::mv(query), zc::mv(record), StableScopeOwnerKey::module(module("owner"_zc)),
                     identity::DefinitionKind::Function, Namespace::Value, declaredName("run"_zc),
                     DefinitionActivation::ModuleSkeleton, zc::mv(visibility))));
  }
  auto scopes = admitOrderedFixtures(orderedScopes);
  auto declarations = admitOrderedFixtures(orderedDeclarations);
  return BoundModuleSkeleton::from(
      base.module().clone(), zc::mv(scopes), base.nodeScopes().clone(), zc::mv(declarations),
      base.implementationOccurrences().clone(), base.genericParameterDeclarations().clone(),
      base.callableParameterDeclarations().clone(), base.moduleAliases().clone(),
      base.imports().clone(), base.localExports().clone(), base.bodyOwners().clone(),
      base.failedLookups().clone());
}

ZC_TEST("StableBindingFacts.ModuleSkeletonRejectsEveryRemainingForeignFamily") {
  SkeletonOwnershipMutation mutations[] = {
      SkeletonOwnershipMutation::Scope,       SkeletonOwnershipMutation::Node,
      SkeletonOwnershipMutation::Declaration, SkeletonOwnershipMutation::Occurrence,
      SkeletonOwnershipMutation::Generic,     SkeletonOwnershipMutation::Callable,
      SkeletonOwnershipMutation::Alias,       SkeletonOwnershipMutation::Import,
      SkeletonOwnershipMutation::BodyOwner,   SkeletonOwnershipMutation::FailedLookup};
  for (const auto mutation : mutations) {
    ZC_EXPECT(foreignOwnershipSkeleton(mutation) == zc::none);
  }
}

ZC_TEST("StableBindingFacts.ModuleSkeletonRetainsEverySequenceAccessor") {
  auto skeleton = require(moduleSkeleton(LocalExportMutation::None));
  ZC_EXPECT(
      skeleton.scopes().values().size() == 1 && skeleton.nodeScopes().values().size() == 1 &&
      skeleton.declarations().values().size() == 1 &&
      skeleton.implementationOccurrences().values().size() == 1 &&
      skeleton.genericParameterDeclarations().values().size() == 1 &&
      skeleton.callableParameterDeclarations().values().size() == 1 &&
      skeleton.moduleAliases().values().size() == 1 && skeleton.imports().values().size() == 1 &&
      skeleton.localExports().values().size() == 1 && skeleton.bodyOwners().values().size() == 2 &&
      skeleton.failedLookups().values().size() == 1);
}

ZC_TEST("StableBindingFacts.ModuleSkeletonValidatesImplementationGenericOccurrences") {
  ZC_EXPECT(implementationGenericSkeleton(true) != zc::none);
  ZC_EXPECT(implementationGenericSkeleton(false) == zc::none);
}

ZC_TEST("StableBindingFacts.ModuleSkeletonAdmitsReferenceCompleteDeepChain") {
  constexpr uint32_t kDepth = 8192;
  auto skeleton = require(deepModuleSkeleton(kDepth));
  ZC_EXPECT(skeleton.scopes().values().size() == kDepth + 1);
  ZC_EXPECT(skeleton.declarations().values().size() == kDepth + 1);
}

ZC_TEST("StableBindingFacts.RoutingKeysRetainExplicitOwnersAndClone") {
  auto key =
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11));
  auto clone = key.clone();
  ZC_EXPECT(key == clone);
  ZC_EXPECT(key.module().encode().asPtr() == module("owner"_zc).encode().asPtr());
  ZC_EXPECT(key.definition() == digestKey<identity::DefinitionKey>(0x11));
}

ZC_TEST("StableBindingFacts.ScopeOwnersAndSyntaxRootsAreClosedMoveOnlySums") {
  auto moduleScope = StableScopeOwnerKey::module(module("owner"_zc));
  auto definitionScope = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11)),
      ScopeRole::Declaration));
  auto implementationScope = require(StableScopeOwnerKey::implementationOccurrence(
      require(
          StableImplementationOccurrenceQueryKey::from(module("owner"_zc), occurrence("owner"_zc))),
      ScopeRole::Implementation));
  auto bodyScope = StableScopeOwnerKey::body(ownerBody(), localPath());
  ZC_EXPECT(moduleScope != definitionScope);
  ZC_EXPECT(moduleScope.value().is<StableModuleScope>() && moduleScope == moduleScope.clone() &&
            definitionScope.value().is<StableDefinitionScope>() &&
            definitionScope == definitionScope.clone() &&
            implementationScope.value().is<StableImplementationOccurrenceScope>() &&
            implementationScope == implementationScope.clone() &&
            bodyScope.value().is<StableBodyScope>() && bodyScope == bodyScope.clone());
  ZC_EXPECT(definitionScope.value().get<StableDefinitionScope>().role == ScopeRole::Declaration);
  ZC_EXPECT(implementationScope.value().get<StableImplementationOccurrenceScope>().role ==
            ScopeRole::Implementation);
  ZC_EXPECT(StableScopeOwnerKey::definition(
                StableDefinitionQueryKey::from(module("owner"_zc),
                                               digestKey<identity::DefinitionKey>(0x11)),
                static_cast<ScopeRole>(0xff)) == zc::none);
  ZC_EXPECT(StableScopeOwnerKey::implementationOccurrence(
                require(StableImplementationOccurrenceQueryKey::from(module("owner"_zc),
                                                                     occurrence("owner"_zc))),
                static_cast<ScopeRole>(0xff)) == zc::none);
  expectSumWires(moduleScope, definitionScope, implementationScope, bodyScope);
  auto invalidDefinitionRole = StableBindingCodec<StableScopeOwnerKey>::encode(definitionScope);
  invalidDefinitionRole[invalidDefinitionRole.size() - 1] = 0xff;
  ZC_EXPECT(StableBindingCodec<StableScopeOwnerKey>::decode(invalidDefinitionRole.asPtr()) ==
            zc::none);
  auto invalidImplementationRole =
      StableBindingCodec<StableScopeOwnerKey>::encode(implementationScope);
  invalidImplementationRole[invalidImplementationRole.size() - 1] = 0xff;
  ZC_EXPECT(StableBindingCodec<StableScopeOwnerKey>::decode(invalidImplementationRole.asPtr()) ==
            zc::none);

  auto moduleRoot = StableNodeSyntaxRoot::moduleBody(module("owner"_zc));
  auto definitionRoot = StableNodeSyntaxRoot::definitionHeader(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11)));
  auto implementationRoot = StableNodeSyntaxRoot::implementationHeader(require(
      StableImplementationOccurrenceQueryKey::from(module("owner"_zc), occurrence("owner"_zc))));
  ZC_EXPECT(moduleRoot != definitionRoot);
  ZC_EXPECT(moduleRoot.value().is<StableModuleBodySyntaxRoot>() &&
            moduleRoot == moduleRoot.clone() &&
            definitionRoot.value().is<StableDefinitionHeaderSyntaxRoot>() &&
            definitionRoot == definitionRoot.clone() &&
            implementationRoot.value().is<StableImplementationHeaderSyntaxRoot>() &&
            implementationRoot == implementationRoot.clone());
  expectSumWires(moduleRoot, definitionRoot, implementationRoot);
}

ZC_TEST("StableBindingFacts.ScopeFactsEnforceLocalOwnershipAndValueSemantics") {
  zc::Maybe<StableScopeOwnerKey> noParent;
  auto moduleFact = require(StableScopeFact::from(StableScopeOwnerKey::module(module("owner"_zc)),
                                                  zc::mv(noParent), ScopeKind::Module));
  ZC_EXPECT(moduleFact == moduleFact.clone() && moduleFact.parent() == zc::none &&
            moduleFact.kind() == ScopeKind::Module &&
            moduleFact.owner().value().is<StableModuleScope>());

  auto definitionOwner = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11)),
      ScopeRole::Declaration));
  auto definitionFact = require(StableScopeFact::from(
      definitionOwner.clone(), moduleFact.owner().clone(), ScopeKind::Function));
  ZC_EXPECT(definitionFact == definitionFact.clone() && definitionFact.owner() == definitionOwner &&
            definitionFact.parent() != zc::none && definitionFact.kind() == ScopeKind::Function);

  auto differentKind = require(StableScopeFact::from(
      definitionOwner.clone(), moduleFact.owner().clone(), ScopeKind::TypeBody));
  auto otherOwner = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x12)),
      ScopeRole::Declaration));
  auto differentOwner = require(
      StableScopeFact::from(otherOwner.clone(), moduleFact.owner().clone(), ScopeKind::Function));
  auto alternateParent = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x13)),
      ScopeRole::Generic));
  auto differentParent = require(
      StableScopeFact::from(definitionOwner.clone(), zc::mv(alternateParent), ScopeKind::Function));
  ZC_EXPECT(definitionFact != differentKind && definitionFact != differentOwner &&
            definitionFact != differentParent);

  zc::Maybe<StableScopeOwnerKey> missingParent;
  ZC_EXPECT(StableScopeFact::from(definitionOwner.clone(), zc::mv(missingParent),
                                  ScopeKind::Function) == zc::none);
  ZC_EXPECT(StableScopeFact::from(definitionOwner.clone(), moduleFact.owner().clone(),
                                  ScopeKind::Module) == zc::none);
  ZC_EXPECT(StableScopeFact::from(definitionOwner.clone(), definitionOwner.clone(),
                                  ScopeKind::Function) == zc::none);
  ZC_EXPECT(StableScopeFact::from(definitionOwner.clone(),
                                  StableScopeOwnerKey::module(module("foreign"_zc)),
                                  ScopeKind::Function) == zc::none);
  ZC_EXPECT(StableScopeFact::from(StableScopeOwnerKey::module(module("owner"_zc)),
                                  moduleFact.owner().clone(), ScopeKind::Module) == zc::none);

  zc::Maybe<StableScopeOwnerKey> invalidKindParent;
  ZC_EXPECT(StableScopeFact::from(StableScopeOwnerKey::module(module("owner"_zc)),
                                  zc::mv(invalidKindParent),
                                  static_cast<ScopeKind>(0xff)) == zc::none);
}

ZC_TEST("StableBindingFacts.NodeScopeFactsRejectForeignScopeOwners") {
  auto definitionKey =
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11));
  auto definitionScope =
      require(StableScopeOwnerKey::definition(definitionKey.clone(), ScopeRole::Declaration));
  auto fact = require(
      StableNodeScopeFact::from(StableNodeSyntaxRoot::definitionHeader(zc::mv(definitionKey)),
                                localPath(), definitionScope.clone()));
  ZC_EXPECT(fact == fact.clone() && fact.nodePath() == localPath() &&
            fact.scope() == definitionScope &&
            fact.root().value().is<StableDefinitionHeaderSyntaxRoot>());

  auto differentPath =
      require(StableNodeScopeFact::from(fact.root().clone(), localPath(2), fact.scope().clone()));
  auto differentScopeOwner = require(StableScopeOwnerKey::definition(
      fact.root().value().get<StableDefinitionHeaderSyntaxRoot>().definition.clone(),
      ScopeRole::Generic));
  auto differentScope = require(
      StableNodeScopeFact::from(fact.root().clone(), localPath(), zc::mv(differentScopeOwner)));
  auto moduleRootFact = require(
      StableNodeScopeFact::from(StableNodeSyntaxRoot::moduleBody(module("owner"_zc)), localPath(),
                                StableScopeOwnerKey::module(module("owner"_zc))));
  ZC_EXPECT(fact != differentPath && fact != differentScope && fact != moduleRootFact);

  auto otherDefinitionScope = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x12)),
      ScopeRole::Declaration));
  ZC_EXPECT(StableNodeScopeFact::from(fact.root().clone(), localPath(),
                                      zc::mv(otherDefinitionScope)) == zc::none);

  auto implementation = digestKey<identity::ImplKey>(0x22);
  auto firstOccurrence = require(StableImplementationOccurrenceQueryKey::from(
      module("owner"_zc), implementationOccurrence(implementation, 1)));
  auto secondOccurrence = require(StableImplementationOccurrenceQueryKey::from(
      module("owner"_zc), implementationOccurrence(implementation, 2)));
  auto matchingImplementationScope = require(StableScopeOwnerKey::implementationOccurrence(
      firstOccurrence.clone(), ScopeRole::Implementation));
  auto implementationFact = require(
      StableNodeScopeFact::from(StableNodeSyntaxRoot::implementationHeader(firstOccurrence.clone()),
                                localPath(), zc::mv(matchingImplementationScope)));
  ZC_EXPECT(fact != implementationFact);
  auto implementationScope = require(StableScopeOwnerKey::implementationOccurrence(
      zc::mv(secondOccurrence), ScopeRole::Implementation));
  ZC_EXPECT(
      StableNodeScopeFact::from(StableNodeSyntaxRoot::implementationHeader(zc::mv(firstOccurrence)),
                                localPath(), zc::mv(implementationScope)) == zc::none);

  ZC_EXPECT(StableNodeScopeFact::from(fact.root().clone(), localPath(),
                                      StableScopeOwnerKey::module(module("foreign"_zc))) ==
            zc::none);
}

ZC_TEST("StableBindingFacts.BodyScopeFactsEnforceExactOwnerRouting") {
  auto owner = ownerBody();
  auto scope = StableScopeOwnerKey::body(owner.clone(), localPath());
  auto fact = require(StableBodyScopeFact::from(owner.clone(), scope.clone(),
                                                StableScopeOwnerKey::module(module("owner"_zc)),
                                                ScopeKind::Function));
  ZC_EXPECT(fact == fact.clone() && fact.owner() == owner && fact.scope() == scope &&
            fact.parent() == StableScopeOwnerKey::module(module("owner"_zc)) &&
            fact.kind() == ScopeKind::Function);

  auto differentScope = require(StableBodyScopeFact::from(
      owner.clone(), StableScopeOwnerKey::body(owner.clone(), localPath(2)),
      StableScopeOwnerKey::module(module("owner"_zc)), ScopeKind::Function));
  auto differentParent = require(StableBodyScopeFact::from(
      owner.clone(), scope.clone(), StableScopeOwnerKey::body(owner.clone(), localPath(3)),
      ScopeKind::Function));
  auto differentKind = require(
      StableBodyScopeFact::from(owner.clone(), scope.clone(),
                                StableScopeOwnerKey::module(module("owner"_zc)), ScopeKind::Block));
  auto definition = digestKey<identity::DefinitionKey>(0x31);
  auto definitionOwner = require(StableOwnerBodyQueryKey::from(
      module("owner"_zc), StableBodyOwnerKey::definition(definition.clone())));
  auto definitionParent = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), definition.clone()),
      ScopeRole::Declaration));
  auto differentOwner = require(StableBodyScopeFact::from(
      definitionOwner.clone(), StableScopeOwnerKey::body(definitionOwner.clone(), localPath()),
      zc::mv(definitionParent), ScopeKind::Function));
  ZC_EXPECT(fact != differentScope && fact != differentParent && fact != differentKind &&
            fact != differentOwner);

  ZC_EXPECT(StableBodyScopeFact::from(
                owner.clone(), StableScopeOwnerKey::module(module("owner"_zc)),
                StableScopeOwnerKey::module(module("owner"_zc)), ScopeKind::Function) == zc::none);
  ZC_EXPECT(StableBodyScopeFact::from(owner.clone(), scope.clone(),
                                      StableScopeOwnerKey::module(module("owner"_zc)),
                                      ScopeKind::Module) == zc::none);
  ZC_EXPECT(StableBodyScopeFact::from(owner.clone(), scope.clone(),
                                      StableScopeOwnerKey::module(module("owner"_zc)),
                                      static_cast<ScopeKind>(0xff)) == zc::none);
  ZC_EXPECT(StableBodyScopeFact::from(owner.clone(), scope.clone(),
                                      StableScopeOwnerKey::module(module("foreign"_zc)),
                                      ScopeKind::Function) == zc::none);
  ZC_EXPECT(StableBodyScopeFact::from(owner.clone(), scope.clone(), scope.clone(),
                                      ScopeKind::Function) == zc::none);
  ZC_EXPECT(StableBodyScopeFact::from(
                owner.clone(), StableScopeOwnerKey::body(ownerBody("foreign"_zc), localPath()),
                StableScopeOwnerKey::module(module("owner"_zc)), ScopeKind::Function) == zc::none);
  ZC_EXPECT(StableBodyScopeFact::from(
                definitionOwner.clone(),
                StableScopeOwnerKey::body(definitionOwner.clone(), localPath()),
                StableScopeOwnerKey::module(module("owner"_zc)), ScopeKind::Function) == zc::none);
  auto leakedDefinitionParent = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), definition.clone()),
      ScopeRole::Declaration));
  ZC_EXPECT(StableBodyScopeFact::from(owner.clone(), scope.clone(), zc::mv(leakedDefinitionParent),
                                      ScopeKind::Function) == zc::none);
  auto differentDefinitionParent = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x32)),
      ScopeRole::Declaration));
  ZC_EXPECT(StableBodyScopeFact::from(
                definitionOwner.clone(),
                StableScopeOwnerKey::body(definitionOwner.clone(), localPath()),
                zc::mv(differentDefinitionParent), ScopeKind::Function) == zc::none);
}

ZC_TEST("StableBindingFacts.BodyNodeScopeFactsRejectForeignOwners") {
  auto owner = ownerBody();
  auto scope = StableScopeOwnerKey::body(owner.clone(), localPath());
  auto fact = require(StableBodyNodeScopeFact::from(owner.clone(), localPath(2), scope.clone()));
  ZC_EXPECT(fact == fact.clone() && fact.owner() == owner && fact.nodePath() == localPath(2) &&
            fact.scope() == scope);

  auto differentPath =
      require(StableBodyNodeScopeFact::from(owner.clone(), localPath(3), scope.clone()));
  auto differentScope = require(StableBodyNodeScopeFact::from(
      owner.clone(), localPath(2), StableScopeOwnerKey::body(owner.clone(), localPath(4))));
  auto definitionOwner = require(StableOwnerBodyQueryKey::from(
      module("owner"_zc),
      StableBodyOwnerKey::definition(digestKey<identity::DefinitionKey>(0x31))));
  auto differentOwner = require(StableBodyNodeScopeFact::from(
      definitionOwner.clone(), localPath(2),
      StableScopeOwnerKey::body(definitionOwner.clone(), localPath())));
  ZC_EXPECT(fact != differentPath && fact != differentScope && fact != differentOwner);

  auto inheritedModuleScope = require(StableBodyNodeScopeFact::from(
      owner.clone(), localPath(), StableScopeOwnerKey::module(module("owner"_zc))));
  ZC_EXPECT(inheritedModuleScope.scope() == StableScopeOwnerKey::module(module("owner"_zc)));
  auto definitionScope = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x31)),
      ScopeRole::Parameters));
  ZC_EXPECT(StableBodyNodeScopeFact::from(definitionOwner.clone(), localPath(),
                                          zc::mv(definitionScope)) != zc::none);
  ZC_EXPECT(StableBodyNodeScopeFact::from(definitionOwner.clone(), localPath(),
                                          StableScopeOwnerKey::module(module("owner"_zc))) ==
            zc::none);
  ZC_EXPECT(StableBodyNodeScopeFact::from(
                owner.clone(), localPath(),
                StableScopeOwnerKey::body(ownerBody("foreign"_zc), localPath())) == zc::none);
}

ZC_TEST("StableBindingFacts.OwnerLocalBindingsEnforceExactKeyAndScope") {
  auto owner = ownerBody();
  auto key = require(OwnerLocalBindingKey::from(
      owner.owner().clone(), localPath(), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("value"_zc)));
  auto scope = StableScopeOwnerKey::body(owner.clone(), localPath(9));
  auto fact = require(StableOwnerLocalBindingFact::from(
      owner.clone(), key.clone(), OwnerLocalBindingKind::Local, declaredName("value"_zc),
      Namespace::Value, scope.clone(), DefinitionActivation::ExpressionIntroduction));
  ZC_EXPECT(fact == fact.clone() && fact.owner() == owner && fact.key() == key &&
            fact.kind() == OwnerLocalBindingKind::Local && fact.name().text() == "value"_zc &&
            fact.nameSpace() == Namespace::Value && fact.declaringScope() == scope &&
            fact.activation() == DefinitionActivation::ExpressionIntroduction);

  auto differentKey = require(OwnerLocalBindingKey::from(
      owner.owner().clone(), localPath(2), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("value"_zc)));
  auto keyFact = require(StableOwnerLocalBindingFact::from(
      owner.clone(), zc::mv(differentKey), OwnerLocalBindingKind::Local, declaredName("value"_zc),
      Namespace::Value, scope.clone(), DefinitionActivation::ExpressionIntroduction));
  auto scopeFact = require(StableOwnerLocalBindingFact::from(
      owner.clone(), key.clone(), OwnerLocalBindingKind::Local, declaredName("value"_zc),
      Namespace::Value, StableScopeOwnerKey::body(owner.clone(), localPath(10)),
      DefinitionActivation::ExpressionIntroduction));
  auto activationFact = require(StableOwnerLocalBindingFact::from(
      owner.clone(), key.clone(), OwnerLocalBindingKind::Local, declaredName("value"_zc),
      Namespace::Value, scope.clone(), DefinitionActivation::AfterInitializer));
  auto genericActivation = require(StableOwnerLocalBindingFact::from(
      owner.clone(), key.clone(), OwnerLocalBindingKind::Local, declaredName("value"_zc),
      Namespace::Value, scope.clone(), DefinitionActivation::GenericList));
  auto loopActivation = require(StableOwnerLocalBindingFact::from(
      owner.clone(), key.clone(), OwnerLocalBindingKind::Local, declaredName("value"_zc),
      Namespace::Value, scope.clone(), DefinitionActivation::LoopPattern));
  auto foreignOwner = ownerBody("foreign"_zc);
  auto foreignKey = require(OwnerLocalBindingKey::from(
      foreignOwner.owner().clone(), localPath(), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("value"_zc)));
  auto foreignScope = StableScopeOwnerKey::body(foreignOwner.clone(), localPath(9));
  auto ownerFact = require(StableOwnerLocalBindingFact::from(
      foreignOwner.clone(), foreignKey.clone(), OwnerLocalBindingKind::Local,
      declaredName("value"_zc), Namespace::Value, foreignScope.clone(),
      DefinitionActivation::ExpressionIntroduction));
  ZC_EXPECT(fact != keyFact && fact != scopeFact && fact != activationFact &&
            fact != genericActivation && fact != loopActivation && fact != ownerFact);

  ZC_EXPECT(StableOwnerLocalBindingFact::from(
                foreignOwner.clone(), key.clone(), OwnerLocalBindingKind::Local,
                declaredName("value"_zc), Namespace::Value, foreignScope.clone(),
                DefinitionActivation::ExpressionIntroduction) == zc::none);
  ZC_EXPECT(StableOwnerLocalBindingFact::from(
                owner.clone(), key.clone(), OwnerLocalBindingKind::Local, declaredName("value"_zc),
                Namespace::Value, zc::mv(foreignScope),
                DefinitionActivation::ExpressionIntroduction) == zc::none);
  ZC_EXPECT(StableOwnerLocalBindingFact::from(
                owner.clone(), key.clone(), OwnerLocalBindingKind::PatternBinding,
                declaredName("value"_zc), Namespace::Value, scope.clone(),
                DefinitionActivation::ExpressionIntroduction) == zc::none);
  ZC_EXPECT(
      StableOwnerLocalBindingFact::from(owner.clone(), key.clone(), OwnerLocalBindingKind::Local,
                                        declaredName("other"_zc), Namespace::Value, scope.clone(),
                                        DefinitionActivation::ExpressionIntroduction) == zc::none);
  ZC_EXPECT(
      StableOwnerLocalBindingFact::from(owner.clone(), key.clone(), OwnerLocalBindingKind::Local,
                                        declaredName("value"_zc), Namespace::Type, scope.clone(),
                                        DefinitionActivation::ExpressionIntroduction) == zc::none);
  auto moduleRootFact = require(StableOwnerLocalBindingFact::from(
      owner.clone(), key.clone(), OwnerLocalBindingKind::Local, declaredName("value"_zc),
      Namespace::Value, StableScopeOwnerKey::module(module("owner"_zc)),
      DefinitionActivation::ExpressionIntroduction));
  ZC_EXPECT(moduleRootFact.declaringScope() == StableScopeOwnerKey::module(module("owner"_zc)));
  ZC_EXPECT(StableOwnerLocalBindingFact::from(
                owner.clone(), key.clone(), OwnerLocalBindingKind::Local, declaredName("value"_zc),
                Namespace::Value, scope.clone(), DefinitionActivation::ModuleSkeleton) == zc::none);
  ZC_EXPECT(StableOwnerLocalBindingFact::from(
                owner.clone(), key.clone(), static_cast<OwnerLocalBindingKind>(0xff),
                declaredName("value"_zc), Namespace::Value, scope.clone(),
                DefinitionActivation::ExpressionIntroduction) == zc::none);
  ZC_EXPECT(StableOwnerLocalBindingFact::from(
                owner.clone(), key.clone(), OwnerLocalBindingKind::Local, declaredName("value"_zc),
                static_cast<Namespace>(0xff), scope.clone(),
                DefinitionActivation::ExpressionIntroduction) == zc::none);
  ZC_EXPECT(
      StableOwnerLocalBindingFact::from(owner.clone(), key.clone(), OwnerLocalBindingKind::Local,
                                        declaredName("value"_zc), Namespace::Value, scope.clone(),
                                        static_cast<DefinitionActivation>(0xff)) == zc::none);
}

ZC_TEST("StableBindingFacts.ResolutionsRejectForeignBodyTargets") {
  auto owner = ownerBody();
  auto binding = StableBindingTargetKey::module(module("binding"_zc));
  auto canonical = StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
      module("target"_zc), digestKey<identity::DefinitionKey>(0x11)));
  auto fact = require(StableResolutionFact::from(owner.clone(), localPath(), Namespace::Value,
                                                 binding.clone(), canonical.clone(),
                                                 BindingOrigin::LocalDeclaration));
  ZC_EXPECT(fact == fact.clone() && fact.owner() == owner && fact.usePath() == localPath() &&
            fact.nameSpace() == Namespace::Value && fact.binding() == binding &&
            fact.canonicalTarget() == canonical &&
            fact.origin() == BindingOrigin::LocalDeclaration);

  auto ownerFact = require(StableResolutionFact::from(
      ownerBody("foreign"_zc), localPath(), Namespace::Value, binding.clone(), canonical.clone(),
      BindingOrigin::LocalDeclaration));
  auto pathFact = require(StableResolutionFact::from(owner.clone(), localPath(2), Namespace::Value,
                                                     binding.clone(), canonical.clone(),
                                                     BindingOrigin::LocalDeclaration));
  auto namespaceFact = require(
      StableResolutionFact::from(owner.clone(), localPath(), Namespace::Type, binding.clone(),
                                 canonical.clone(), BindingOrigin::LocalDeclaration));
  auto bindingFact =
      require(StableResolutionFact::from(owner.clone(), localPath(), Namespace::Value,
                                         StableBindingTargetKey::module(module("other"_zc)),
                                         canonical.clone(), BindingOrigin::LocalDeclaration));
  auto canonicalFact = require(StableResolutionFact::from(
      owner.clone(), localPath(), Namespace::Value, binding.clone(),
      StableBindingTargetKey::module(module("other"_zc)), BindingOrigin::LocalDeclaration));
  auto originFact = require(StableResolutionFact::from(owner.clone(), localPath(), Namespace::Value,
                                                       binding.clone(), canonical.clone(),
                                                       BindingOrigin::Prelude));
  ZC_EXPECT(fact != ownerFact && fact != pathFact && fact != namespaceFact && fact != bindingFact &&
            fact != canonicalFact && fact != originFact);

  auto localKey = require(OwnerLocalBindingKey::from(
      owner.owner().clone(), localPath(), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("value"_zc)));
  auto localTarget = require(StableBindingTargetKey::ownerLocal(owner.clone(), localKey.clone()));
  ZC_EXPECT(StableResolutionFact::from(owner.clone(), localPath(), Namespace::Value,
                                       zc::mv(localTarget), canonical.clone(),
                                       BindingOrigin::LocalDeclaration) != zc::none);

  auto foreignOwner = ownerBody("foreign"_zc);
  auto foreignKey = require(OwnerLocalBindingKey::from(
      foreignOwner.owner().clone(), localPath(), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("value"_zc)));
  auto foreignTarget =
      require(StableBindingTargetKey::ownerLocal(foreignOwner.clone(), zc::mv(foreignKey)));
  ZC_EXPECT(StableResolutionFact::from(owner.clone(), localPath(), Namespace::Value,
                                       zc::mv(foreignTarget), canonical.clone(),
                                       BindingOrigin::LocalDeclaration) == zc::none);
  auto anonymous = require(AnonymousOwnerLocalKey::from(foreignOwner.owner().clone(), localPath(),
                                                        AnonymousOwnerLocalRole::Closure));
  auto foreignAnonymous =
      require(StableBindingTargetKey::anonymousOwner(zc::mv(foreignOwner), zc::mv(anonymous)));
  ZC_EXPECT(StableResolutionFact::from(owner.clone(), localPath(), Namespace::Value,
                                       binding.clone(), zc::mv(foreignAnonymous),
                                       BindingOrigin::LocalDeclaration) == zc::none);
  ZC_EXPECT(StableResolutionFact::from(owner.clone(), localPath(), static_cast<Namespace>(0xff),
                                       binding.clone(), canonical.clone(),
                                       BindingOrigin::LocalDeclaration) == zc::none);
  ZC_EXPECT(StableResolutionFact::from(owner.clone(), localPath(), Namespace::Value,
                                       binding.clone(), canonical.clone(),
                                       static_cast<BindingOrigin>(0xff)) == zc::none);
}

ZC_TEST("StableBindingFacts.DeferredMembersRetainClosedAccessAndCanonicalPaths") {
  auto namespaces = [] {
    zc::Vector<Namespace> values;
    values.add(Namespace::Value);
    values.add(Namespace::Type);
    return require(StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(values)));
  };
  auto emptyArguments = [] { return CanonicalSequence<LocalSyntaxPath>::empty(); };

  auto owner = ownerBody();
  auto fact = require(StableDeferredMemberFact::from(
      owner.clone(), localPath(1), localPath(2), MemberAccessKind::Dot, declaredName("member"_zc),
      namespaces(), emptyArguments()));
  ZC_EXPECT(fact == fact.clone() && fact.owner() == owner && fact.usePath() == localPath(1) &&
            fact.basePath() == localPath(2) && fact.accessKind() == MemberAccessKind::Dot &&
            fact.member().text() == "member"_zc && fact.expectedNamespaces().values().size() == 2 &&
            fact.expectedNamespaces().values()[0] == Namespace::Value &&
            fact.expectedNamespaces().values()[1] == Namespace::Type &&
            fact.genericArgumentPaths().values().size() == 0);

  auto ownerFact = require(StableDeferredMemberFact::from(
      ownerBody("foreign"_zc), localPath(1), localPath(2), MemberAccessKind::Dot,
      declaredName("member"_zc), namespaces(), emptyArguments()));
  auto useFact = require(StableDeferredMemberFact::from(
      owner.clone(), localPath(4), localPath(2), MemberAccessKind::Dot, declaredName("member"_zc),
      namespaces(), emptyArguments()));
  auto baseFact = require(StableDeferredMemberFact::from(
      owner.clone(), localPath(1), localPath(4), MemberAccessKind::Dot, declaredName("member"_zc),
      namespaces(), emptyArguments()));
  auto optionalFact = require(StableDeferredMemberFact::from(
      owner.clone(), localPath(1), localPath(2), MemberAccessKind::Optional,
      declaredName("member"_zc), namespaces(), emptyArguments()));
  auto qualifiedFact = require(StableDeferredMemberFact::from(
      owner.clone(), localPath(1), localPath(2), MemberAccessKind::Qualified,
      declaredName("member"_zc), namespaces(), emptyArguments()));
  auto memberFact = require(StableDeferredMemberFact::from(
      owner.clone(), localPath(1), localPath(2), MemberAccessKind::Dot, declaredName("other"_zc),
      namespaces(), emptyArguments()));
  auto namespacesFact = require(StableDeferredMemberFact::from(
      owner.clone(), localPath(1), localPath(2), MemberAccessKind::Dot, declaredName("member"_zc),
      [] {
        zc::Vector<Namespace> values;
        values.add(Namespace::Value);
        return require(StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(values)));
      }(),
      emptyArguments()));
  ZC_EXPECT(fact != ownerFact && fact != useFact && fact != baseFact && fact != optionalFact &&
            fact != qualifiedFact && fact != memberFact && fact != namespacesFact);

  ZC_EXPECT(StableDeferredMemberFact::from(owner.clone(), localPath(1), localPath(1),
                                           MemberAccessKind::Dot, declaredName("member"_zc),
                                           namespaces(), emptyArguments()) == zc::none);
  ZC_EXPECT(StableDeferredMemberFact::from(
                owner.clone(), localPath(1), localPath(2), static_cast<MemberAccessKind>(0xff),
                declaredName("member"_zc), namespaces(), emptyArguments()) == zc::none);
}

ZC_TEST("StableBindingFacts.ContextualSelfAndReceiverEnforceModuleRouting") {
  auto definition =
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11));
  auto nominal = StableSelfOwner::nominal(definition.clone());
  auto interfaceOwner = StableSelfOwner::interface(definition.clone());
  auto occurrenceOwner = require(
      StableImplementationOccurrenceQueryKey::from(module("owner"_zc), occurrence("owner"_zc)));
  auto implementationOwner = StableSelfOwner::implementationOccurrence(occurrenceOwner.clone());
  ZC_EXPECT(nominal == nominal.clone() && interfaceOwner == interfaceOwner.clone() &&
            implementationOwner == implementationOwner.clone());
  ZC_EXPECT(nominal != interfaceOwner && nominal != implementationOwner &&
            interfaceOwner != implementationOwner);
  ZC_EXPECT(nominal.value().is<StableNominalSelfOwner>() &&
            nominal.value().get<StableNominalSelfOwner>().definition == definition);
  ZC_EXPECT(interfaceOwner.value().is<StableInterfaceSelfOwner>() &&
            interfaceOwner.value().get<StableInterfaceSelfOwner>().definition == definition);
  ZC_EXPECT(implementationOwner.value().is<StableImplementationOccurrenceSelfOwner>() &&
            implementationOwner.value().get<StableImplementationOccurrenceSelfOwner>().occurrence ==
                occurrenceOwner);
  auto otherNominal = StableSelfOwner::nominal(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x22)));
  ZC_EXPECT(nominal != otherNominal);

  auto owner = ownerBody();
  auto nominalFact =
      require(StableSelfTypeFact::from(owner.clone(), localPath(1), nominal.clone()));
  auto interfaceFact =
      require(StableSelfTypeFact::from(owner.clone(), localPath(1), interfaceOwner.clone()));
  auto implementationFact =
      require(StableSelfTypeFact::from(owner.clone(), localPath(1), implementationOwner.clone()));
  auto pathFact = require(StableSelfTypeFact::from(owner.clone(), localPath(2), nominal.clone()));
  auto ownerFact = require(StableSelfTypeFact::from(
      ownerBody("foreign"_zc), localPath(1),
      StableSelfOwner::nominal(StableDefinitionQueryKey::from(
          module("foreign"_zc), digestKey<identity::DefinitionKey>(0x11)))));
  ZC_EXPECT(nominalFact == nominalFact.clone() && nominalFact.owner() == owner &&
            nominalFact.syntaxPath() == localPath(1) && nominalFact.selfOwner() == nominal);
  ZC_EXPECT(nominalFact != interfaceFact && nominalFact != implementationFact &&
            nominalFact != pathFact && nominalFact != ownerFact);
  ZC_EXPECT(StableSelfTypeFact::from(
                owner.clone(), localPath(1),
                StableSelfOwner::nominal(StableDefinitionQueryKey::from(
                    module("foreign"_zc), digestKey<identity::DefinitionKey>(0x11)))) == zc::none);
  ZC_EXPECT(StableSelfTypeFact::from(
                owner.clone(), localPath(1),
                StableSelfOwner::interface(StableDefinitionQueryKey::from(
                    module("foreign"_zc), digestKey<identity::DefinitionKey>(0x11)))) == zc::none);
  ZC_EXPECT(StableSelfTypeFact::from(owner.clone(), localPath(1),
                                     StableSelfOwner::implementationOccurrence(
                                         require(StableImplementationOccurrenceQueryKey::from(
                                             module("foreign"_zc), occurrence("foreign"_zc))))) ==
            zc::none);

  auto receiver = StableCallableParameterQueryKey::from(
      module("owner"_zc), digestKey<identity::CallableParameterKey>(0x33));
  auto thisFact =
      require(StableThisBindingFact::from(owner.clone(), localPath(3), receiver.clone()));
  auto thisPathFact =
      require(StableThisBindingFact::from(owner.clone(), localPath(4), receiver.clone()));
  auto otherReceiver = StableCallableParameterQueryKey::from(
      module("owner"_zc), digestKey<identity::CallableParameterKey>(0x44));
  auto receiverFact =
      require(StableThisBindingFact::from(owner.clone(), localPath(3), zc::mv(otherReceiver)));
  ZC_EXPECT(thisFact == thisFact.clone() && thisFact.owner() == owner &&
            thisFact.expressionPath() == localPath(3) && thisFact.receiver() == receiver);
  ZC_EXPECT(thisFact != thisPathFact && thisFact != receiverFact);
  ZC_EXPECT(StableThisBindingFact::from(
                owner.clone(), localPath(3),
                StableCallableParameterQueryKey::from(
                    module("foreign"_zc), digestKey<identity::CallableParameterKey>(0x33))) ==
            zc::none);
}

ZC_TEST("StableBindingFacts.ShadowTargetsRequireDistinctOwnerRoutedTargets") {
  auto owner = ownerBody();
  auto binding = StableBindingTargetKey::module(module("binding"_zc));
  auto shadowed = StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
      module("target"_zc), digestKey<identity::DefinitionKey>(0x11)));
  auto fact =
      require(StableShadowTargetFact::from(owner.clone(), binding.clone(), shadowed.clone()));
  ZC_EXPECT(fact == fact.clone() && fact.owner() == owner && fact.binding() == binding &&
            fact.shadowed() == shadowed);

  auto ownerFact = require(
      StableShadowTargetFact::from(ownerBody("foreign"_zc), binding.clone(), shadowed.clone()));
  auto bindingFact = require(StableShadowTargetFact::from(
      owner.clone(), StableBindingTargetKey::module(module("other"_zc)), shadowed.clone()));
  auto shadowedFact = require(StableShadowTargetFact::from(
      owner.clone(), binding.clone(), StableBindingTargetKey::module(module("other"_zc))));
  ZC_EXPECT(fact != ownerFact && fact != bindingFact && fact != shadowedFact);
  ZC_EXPECT(StableShadowTargetFact::from(owner.clone(), binding.clone(), binding.clone()) ==
            zc::none);

  auto localKey = require(OwnerLocalBindingKey::from(
      owner.owner().clone(), localPath(1), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("local"_zc)));
  auto localTarget = require(StableBindingTargetKey::ownerLocal(owner.clone(), zc::mv(localKey)));
  ZC_EXPECT(StableShadowTargetFact::from(owner.clone(), zc::mv(localTarget), shadowed.clone()) !=
            zc::none);
  auto anonymousKey = require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(2),
                                                           AnonymousOwnerLocalRole::Closure));
  auto anonymousTarget =
      require(StableBindingTargetKey::anonymousOwner(owner.clone(), zc::mv(anonymousKey)));
  ZC_EXPECT(StableShadowTargetFact::from(owner.clone(), binding.clone(), zc::mv(anonymousTarget)) !=
            zc::none);

  auto foreignOwner = ownerBody("foreign"_zc);
  auto foreignLocalKey = require(OwnerLocalBindingKey::from(
      foreignOwner.owner().clone(), localPath(1), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("local"_zc)));
  auto foreignLocal =
      require(StableBindingTargetKey::ownerLocal(foreignOwner.clone(), zc::mv(foreignLocalKey)));
  ZC_EXPECT(StableShadowTargetFact::from(owner.clone(), zc::mv(foreignLocal), shadowed.clone()) ==
            zc::none);
  auto foreignAnonymousKey = require(AnonymousOwnerLocalKey::from(
      foreignOwner.owner().clone(), localPath(2), AnonymousOwnerLocalRole::Closure));
  auto foreignAnonymous = require(
      StableBindingTargetKey::anonymousOwner(zc::mv(foreignOwner), zc::mv(foreignAnonymousKey)));
  ZC_EXPECT(StableShadowTargetFact::from(owner.clone(), binding.clone(),
                                         zc::mv(foreignAnonymous)) == zc::none);
}

ZC_TEST("StableBindingFacts.LabelsRequireExactOwnerAndStatementScope") {
  auto owner = ownerBody();
  auto key = StableLabelKey::from(owner.clone(), localPath(1));
  auto block = StableLabelTarget::block(StableScopeOwnerKey::body(owner.clone(), localPath(2)));
  auto loop = StableLabelTarget::loop(StableScopeOwnerKey::body(owner.clone(), localPath(2)));
  auto otherBlock =
      StableLabelTarget::block(StableScopeOwnerKey::body(owner.clone(), localPath(3)));
  auto otherLoop = StableLabelTarget::loop(StableScopeOwnerKey::body(owner.clone(), localPath(3)));
  auto fact = require(
      StableLabelFact::from(key.clone(), declaredName("label"_zc), localPath(2), block.clone()));
  auto loopFact = require(
      StableLabelFact::from(key.clone(), declaredName("label"_zc), localPath(2), loop.clone()));

  ZC_EXPECT(key == key.clone() && key.owner() == owner && key.declarationPath() == localPath(1));
  ZC_EXPECT(block == block.clone() && loop == loop.clone() && block != loop);
  ZC_EXPECT(block != otherBlock && loop != otherLoop);
  ZC_EXPECT(block.value().is<StableBlockLabelTarget>() &&
            loop.value().is<StableLoopLabelTarget>() &&
            block.scope() == StableScopeOwnerKey::body(owner.clone(), localPath(2)));
  ZC_EXPECT(fact == fact.clone() && fact.key() == key && fact.name().text() == "label"_zc &&
            fact.statementPath() == localPath(2) && fact.target() == block);
  ZC_EXPECT(fact != loopFact);

  auto ownerKey = StableLabelKey::from(ownerBody("foreign"_zc), localPath(1));
  auto pathKey = StableLabelKey::from(owner.clone(), localPath(3));
  ZC_EXPECT(key != ownerKey && key != pathKey);
  auto pathFact = require(StableLabelFact::from(pathKey.clone(), declaredName("label"_zc),
                                                localPath(2), block.clone()));
  auto nameFact = require(
      StableLabelFact::from(key.clone(), declaredName("other"_zc), localPath(2), block.clone()));
  auto statementFact = require(StableLabelFact::from(
      key.clone(), declaredName("label"_zc), localPath(3),
      StableLabelTarget::block(StableScopeOwnerKey::body(owner.clone(), localPath(3)))));
  auto ownerFact = require(StableLabelFact::from(
      ownerKey.clone(), declaredName("label"_zc), localPath(2),
      StableLabelTarget::block(StableScopeOwnerKey::body(ownerKey.owner().clone(), localPath(2)))));
  ZC_EXPECT(fact != pathFact && fact != nameFact && fact != statementFact && fact != ownerFact);

  ZC_EXPECT(StableLabelFact::from(key.clone(), declaredName("label"_zc), localPath(2),
                                  StableLabelTarget::block(StableScopeOwnerKey::body(
                                      ownerBody("foreign"_zc), localPath(2)))) == zc::none);
  ZC_EXPECT(StableLabelFact::from(key.clone(), declaredName("label"_zc), localPath(2),
                                  StableLabelTarget::block(StableScopeOwnerKey::module(
                                      module("owner"_zc)))) == zc::none);
  ZC_EXPECT(StableLabelFact::from(key.clone(), declaredName("label"_zc), localPath(3),
                                  block.clone()) == zc::none);
  ZC_EXPECT(StableLabelFact::from(StableLabelKey::from(owner.clone(), localPath(2)),
                                  declaredName("label"_zc), localPath(2),
                                  block.clone()) == zc::none);
}

ZC_TEST("StableBindingFacts.ControlTransfersRequireExactOwnerAndTargetClass") {
  auto owner = ownerBody();
  auto explicitTarget =
      StableControlTarget::explicitLabel(StableLabelKey::from(owner.clone(), localPath(1)));
  auto loopTarget =
      StableControlTarget::loop(StableScopeOwnerKey::body(owner.clone(), localPath(2)));
  auto matchTarget =
      StableControlTarget::match(StableScopeOwnerKey::body(owner.clone(), localPath(3)));
  ZC_EXPECT(explicitTarget == explicitTarget.clone() && loopTarget == loopTarget.clone() &&
            matchTarget == matchTarget.clone());
  ZC_EXPECT(explicitTarget.value().is<StableExplicitLabelControlTarget>() &&
            loopTarget.value().is<StableLoopControlTarget>() &&
            matchTarget.value().is<StableMatchControlTarget>() && explicitTarget != loopTarget &&
            loopTarget != matchTarget);
  ZC_EXPECT(explicitTarget !=
            StableControlTarget::explicitLabel(StableLabelKey::from(owner.clone(), localPath(4))));
  ZC_EXPECT(loopTarget !=
            StableControlTarget::loop(StableScopeOwnerKey::body(owner.clone(), localPath(4))));
  ZC_EXPECT(matchTarget !=
            StableControlTarget::match(StableScopeOwnerKey::body(owner.clone(), localPath(4))));

  auto fact = require(StableControlTransferFact::from(
      owner.clone(), localPath(5), ControlTransferKind::Break, loopTarget.clone()));
  ZC_EXPECT(fact == fact.clone() && fact.owner() == owner && fact.transferPath() == localPath(5) &&
            fact.kind() == ControlTransferKind::Break && fact.target() == loopTarget);
  auto ownerFact = require(StableControlTransferFact::from(
      ownerBody("foreign"_zc), localPath(5), ControlTransferKind::Break,
      StableControlTarget::loop(StableScopeOwnerKey::body(ownerBody("foreign"_zc), localPath(2)))));
  auto pathFact = require(StableControlTransferFact::from(
      owner.clone(), localPath(6), ControlTransferKind::Break, loopTarget.clone()));
  auto kindFact = require(StableControlTransferFact::from(
      owner.clone(), localPath(5), ControlTransferKind::Continue, loopTarget.clone()));
  auto targetFact = require(StableControlTransferFact::from(
      owner.clone(), localPath(5), ControlTransferKind::Break, matchTarget.clone()));
  ZC_EXPECT(fact != ownerFact && fact != pathFact && fact != kindFact && fact != targetFact);

  ZC_EXPECT(StableControlTransferFact::from(owner.clone(), localPath(5),
                                            static_cast<ControlTransferKind>(0xff),
                                            loopTarget.clone()) == zc::none);
  ZC_EXPECT(StableControlTransferFact::from(owner.clone(), localPath(5),
                                            ControlTransferKind::Continue,
                                            matchTarget.clone()) == zc::none);
  ZC_EXPECT(StableControlTransferFact::from(owner.clone(), localPath(5),
                                            ControlTransferKind::Continue,
                                            explicitTarget.clone()) != zc::none);
  ZC_EXPECT(StableControlTransferFact::from(owner.clone(), localPath(1), ControlTransferKind::Break,
                                            explicitTarget.clone()) == zc::none);
  ZC_EXPECT(StableControlTransferFact::from(owner.clone(), localPath(2), ControlTransferKind::Break,
                                            loopTarget.clone()) == zc::none);
  ZC_EXPECT(StableControlTransferFact::from(owner.clone(), localPath(3), ControlTransferKind::Break,
                                            matchTarget.clone()) == zc::none);

  auto foreignOwner = ownerBody("foreign"_zc);
  ZC_EXPECT(StableControlTransferFact::from(owner.clone(), localPath(5), ControlTransferKind::Break,
                                            StableControlTarget::explicitLabel(StableLabelKey::from(
                                                foreignOwner.clone(), localPath(1)))) == zc::none);
  ZC_EXPECT(StableControlTransferFact::from(owner.clone(), localPath(5), ControlTransferKind::Break,
                                            StableControlTarget::loop(StableScopeOwnerKey::body(
                                                foreignOwner.clone(), localPath(2)))) == zc::none);
  ZC_EXPECT(StableControlTransferFact::from(owner.clone(), localPath(5), ControlTransferKind::Break,
                                            StableControlTarget::match(StableScopeOwnerKey::module(
                                                module("owner"_zc)))) == zc::none);
}

ZC_TEST("StableBindingFacts.ClosuresRequireExactOwnerScopeAndReferences") {
  auto owner = ownerBody();
  auto closure = require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(2),
                                                      AnonymousOwnerLocalRole::Closure));
  auto scope = StableScopeOwnerKey::body(owner.clone(), localPath(2));
  auto fact = require(StableClosureFact::from(owner.clone(), closure.clone(), scope.clone()));
  ZC_EXPECT(fact == fact.clone() && fact.owner() == owner && fact.closure() == closure &&
            fact.scope() == scope);
  auto ownerFact = require(StableClosureFact::from(
      ownerBody("foreign"_zc),
      require(AnonymousOwnerLocalKey::from(ownerBody("foreign"_zc).owner().clone(), localPath(2),
                                           AnonymousOwnerLocalRole::Closure)),
      StableScopeOwnerKey::body(ownerBody("foreign"_zc), localPath(2))));
  auto closureFact = require(StableClosureFact::from(
      owner.clone(),
      require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(3),
                                           AnonymousOwnerLocalRole::Closure)),
      StableScopeOwnerKey::body(owner.clone(), localPath(3))));
  ZC_EXPECT(fact != ownerFact && fact != closureFact);

  ZC_EXPECT(StableClosureFact::from(
                owner.clone(),
                require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(2),
                                                     AnonymousOwnerLocalRole::FunctionExpression)),
                scope.clone()) == zc::none);
  ZC_EXPECT(StableClosureFact::from(owner.clone(),
                                    require(AnonymousOwnerLocalKey::from(
                                        ownerBody("foreign"_zc).owner().clone(), localPath(2),
                                        AnonymousOwnerLocalRole::Closure)),
                                    scope.clone()) == zc::none);
  ZC_EXPECT(StableClosureFact::from(
                owner.clone(), closure.clone(),
                StableScopeOwnerKey::body(ownerBody("foreign"_zc), localPath(2))) == zc::none);
  ZC_EXPECT(StableClosureFact::from(owner.clone(), closure.clone(),
                                    StableScopeOwnerKey::body(owner.clone(), localPath(3))) ==
            zc::none);
  ZC_EXPECT(StableClosureFact::from(owner.clone(), closure.clone(),
                                    StableScopeOwnerKey::module(module("owner"_zc))) == zc::none);

  zc::Vector<LocalSyntaxPath> referenceValues;
  referenceValues.add(localPath(4));
  referenceValues.add(localPath(5));
  auto references =
      require(StableBindingSequenceBuilder<LocalSyntaxPath>::fromNonEmpty(zc::mv(referenceValues)));
  auto variable = StableClosureFreeVariable::from(
      StableBindingTargetKey::module(module("target"_zc)), zc::mv(references));
  ZC_EXPECT(variable == variable.clone() &&
            variable.target() == StableBindingTargetKey::module(module("target"_zc)) &&
            variable.referencePaths().values().size() == 2);
  zc::Vector<LocalSyntaxPath> otherReferenceValues;
  otherReferenceValues.add(localPath(6));
  auto otherReferences = require(
      StableBindingSequenceBuilder<LocalSyntaxPath>::fromNonEmpty(zc::mv(otherReferenceValues)));
  auto targetVariable = StableClosureFreeVariable::from(
      StableBindingTargetKey::module(module("other"_zc)), variable.referencePaths().clone());
  auto pathVariable =
      StableClosureFreeVariable::from(variable.target().clone(), zc::mv(otherReferences));
  ZC_EXPECT(variable != targetVariable && variable != pathVariable);

  auto inventory = require(StableClosureFreeVariableFact::from(
      owner.clone(), closure.clone(), CanonicalSequence<StableClosureFreeVariable>::empty()));
  auto inventoryOwnerFact = require(StableClosureFreeVariableFact::from(
      ownerBody("foreign"_zc),
      require(AnonymousOwnerLocalKey::from(ownerBody("foreign"_zc).owner().clone(), localPath(2),
                                           AnonymousOwnerLocalRole::Closure)),
      CanonicalSequence<StableClosureFreeVariable>::empty()));
  auto inventoryClosureFact = require(StableClosureFreeVariableFact::from(
      owner.clone(),
      require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(3),
                                           AnonymousOwnerLocalRole::Closure)),
      CanonicalSequence<StableClosureFreeVariable>::empty()));
  ZC_EXPECT(inventory == inventory.clone() && inventory.owner() == owner &&
            inventory.closure() == closure && inventory.variables().values().size() == 0);
  ZC_EXPECT(inventory != inventoryOwnerFact && inventory != inventoryClosureFact);
  ZC_EXPECT(StableClosureFreeVariableFact::from(
                ownerBody("foreign"_zc), closure.clone(),
                CanonicalSequence<StableClosureFreeVariable>::empty()) == zc::none);
  ZC_EXPECT(StableClosureFreeVariableFact::from(
                owner.clone(),
                require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(2),
                                                     AnonymousOwnerLocalRole::FunctionExpression)),
                CanonicalSequence<StableClosureFreeVariable>::empty()) == zc::none);
}

ZC_TEST("StableBindingFacts.ExplicitCapturesRequireClosedModesAndFunctionOwner") {
  auto owner = ownerBody();
  auto byValue = require(StableExplicitCaptureBindingFact::from(
      localPath(4), StableBindingTargetKey::module(module("value"_zc)),
      StableExplicitCaptureMode::ByValue));
  auto byReference = require(StableExplicitCaptureBindingFact::from(
      localPath(5), StableBindingTargetKey::module(module("reference"_zc)),
      StableExplicitCaptureMode::ByReference));
  auto receiver = StableBindingTargetKey::callableParameter(StableCallableParameterQueryKey::from(
      module("owner"_zc), digestKey<identity::CallableParameterKey>(0x33)));
  auto captureThis = require(StableExplicitCaptureBindingFact::from(
      localPath(6), receiver.clone(), StableExplicitCaptureMode::This));
  ZC_EXPECT(byValue == byValue.clone() && byValue.itemPath() == localPath(4) &&
            byValue.target() == StableBindingTargetKey::module(module("value"_zc)) &&
            byValue.mode() == StableExplicitCaptureMode::ByValue);
  ZC_EXPECT(byValue != byReference && byReference != captureThis);
  auto pathCapture = require(StableExplicitCaptureBindingFact::from(
      localPath(7), byValue.target().clone(), byValue.mode()));
  auto targetCapture = require(StableExplicitCaptureBindingFact::from(
      byValue.itemPath().clone(), StableBindingTargetKey::module(module("other"_zc)),
      byValue.mode()));
  auto modeCapture = require(
      StableExplicitCaptureBindingFact::from(byValue.itemPath().clone(), byValue.target().clone(),
                                             StableExplicitCaptureMode::ByReference));
  ZC_EXPECT(byValue != pathCapture && byValue != targetCapture && byValue != modeCapture);
  ZC_EXPECT(StableExplicitCaptureBindingFact::from(localPath(4), byValue.target().clone(),
                                                   static_cast<StableExplicitCaptureMode>(0xff)) ==
            zc::none);
  ZC_EXPECT(StableExplicitCaptureBindingFact::from(
                localPath(4), StableBindingTargetKey::module(module("owner"_zc)),
                StableExplicitCaptureMode::This) == zc::none);

  auto closure = require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(2),
                                                      AnonymousOwnerLocalRole::FunctionExpression));
  auto fact = require(StableExplicitClosureCaptureFact::from(
      owner.clone(), closure.clone(), localPath(3),
      CanonicalSequence<StableExplicitCaptureBindingFact>::empty()));
  ZC_EXPECT(fact == fact.clone() && fact.owner() == owner && fact.closure() == closure &&
            fact.captureListPath() == localPath(3) && fact.captures().values().size() == 0);
  auto foreignOwner = ownerBody("foreign"_zc);
  auto ownerFact = require(StableExplicitClosureCaptureFact::from(
      foreignOwner.clone(),
      require(AnonymousOwnerLocalKey::from(foreignOwner.owner().clone(), localPath(2),
                                           AnonymousOwnerLocalRole::FunctionExpression)),
      localPath(3), CanonicalSequence<StableExplicitCaptureBindingFact>::empty()));
  auto closureFact = require(StableExplicitClosureCaptureFact::from(
      owner.clone(),
      require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(8),
                                           AnonymousOwnerLocalRole::FunctionExpression)),
      localPath(3), CanonicalSequence<StableExplicitCaptureBindingFact>::empty()));
  auto pathFact = require(StableExplicitClosureCaptureFact::from(
      owner.clone(), closure.clone(), localPath(9),
      CanonicalSequence<StableExplicitCaptureBindingFact>::empty()));
  ZC_EXPECT(fact != ownerFact && fact != closureFact && fact != pathFact);

  ZC_EXPECT(StableExplicitClosureCaptureFact::from(
                owner.clone(),
                require(AnonymousOwnerLocalKey::from(foreignOwner.owner().clone(), localPath(2),
                                                     AnonymousOwnerLocalRole::FunctionExpression)),
                localPath(3),
                CanonicalSequence<StableExplicitCaptureBindingFact>::empty()) == zc::none);
  ZC_EXPECT(StableExplicitClosureCaptureFact::from(
                owner.clone(),
                require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(2),
                                                     AnonymousOwnerLocalRole::Closure)),
                localPath(3),
                CanonicalSequence<StableExplicitCaptureBindingFact>::empty()) == zc::none);
  ZC_EXPECT(StableExplicitClosureCaptureFact::from(
                owner.clone(), closure.clone(), closure.path().clone(),
                CanonicalSequence<StableExplicitCaptureBindingFact>::empty()) == zc::none);
}

BoundOwnerBody populatedOwnerBody() {
  auto owner = ownerBody();
  auto rootScope = StableScopeOwnerKey::body(owner.clone(), localPath(1));
  auto loopScope = StableScopeOwnerKey::body(owner.clone(), localPath(2));
  auto closureScope = StableScopeOwnerKey::body(owner.clone(), localPath(3));
  zc::Vector<StableBodyScopeFact> scopeValues;
  scopeValues.add(require(StableBodyScopeFact::from(owner.clone(), rootScope.clone(),
                                                    StableScopeOwnerKey::module(module("owner"_zc)),
                                                    ScopeKind::Function)));
  scopeValues.add(require(StableBodyScopeFact::from(owner.clone(), loopScope.clone(),
                                                    rootScope.clone(), ScopeKind::Loop)));
  scopeValues.add(require(StableBodyScopeFact::from(owner.clone(), closureScope.clone(),
                                                    loopScope.clone(), ScopeKind::Closure)));
  auto scopes =
      require(StableBindingSequenceBuilder<StableBodyScopeFact>::from(zc::mv(scopeValues)));

  zc::Vector<StableBodyNodeScopeFact> nodeValues;
  for (uint32_t component = 1; component <= 17; ++component) {
    const bool inClosure = component == 3 || (component >= 6 && component <= 8);
    const auto& scope = inClosure                           ? closureScope
                        : component == 2 || component == 15 ? loopScope
                                                            : rootScope;
    nodeValues.add(
        require(StableBodyNodeScopeFact::from(owner.clone(), localPath(component), scope.clone())));
  }
  auto nodeScopes =
      require(StableBindingSequenceBuilder<StableBodyNodeScopeFact>::from(zc::mv(nodeValues)));

  auto localKey = require(OwnerLocalBindingKey::from(
      owner.owner().clone(), localPath(9), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("value"_zc)));
  auto localTarget = require(StableBindingTargetKey::ownerLocal(owner.clone(), localKey.clone()));
  auto bindings = singletonSequence(require(StableOwnerLocalBindingFact::from(
      owner.clone(), localKey.clone(), OwnerLocalBindingKind::Local, declaredName("value"_zc),
      Namespace::Value, rootScope.clone(), DefinitionActivation::ExpressionIntroduction)));
  auto resolutions = singletonSequence(require(StableResolutionFact::from(
      owner.clone(), localPath(10), Namespace::Value, localTarget.clone(),
      StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
          module("owner"_zc), digestKey<identity::DefinitionKey>(0x41))),
      BindingOrigin::LocalDeclaration)));

  zc::Vector<Namespace> expectedNamespaceValues;
  expectedNamespaceValues.add(Namespace::Value);
  auto expectedNamespaces = require(
      StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(expectedNamespaceValues)));
  auto deferredMembers = singletonSequence(require(StableDeferredMemberFact::from(
      owner.clone(), localPath(11), localPath(12), MemberAccessKind::Dot, declaredName("member"_zc),
      zc::mv(expectedNamespaces), CanonicalSequence<LocalSyntaxPath>::empty())));
  auto selfTypes = singletonSequence(require(StableSelfTypeFact::from(
      owner.clone(), localPath(13),
      StableSelfOwner::nominal(StableDefinitionQueryKey::from(
          module("owner"_zc), digestKey<identity::DefinitionKey>(0x42))))));
  auto thisBindings = singletonSequence(require(StableThisBindingFact::from(
      owner.clone(), localPath(14),
      StableCallableParameterQueryKey::from(module("owner"_zc),
                                            digestKey<identity::CallableParameterKey>(0x43)))));
  auto shadowTargets = singletonSequence(require(StableShadowTargetFact::from(
      owner.clone(), localTarget.clone(),
      StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
          module("owner"_zc), digestKey<identity::DefinitionKey>(0x44))))));

  auto labelKey = StableLabelKey::from(owner.clone(), localPath(5));
  auto labels = singletonSequence(
      require(StableLabelFact::from(labelKey.clone(), declaredName("loop"_zc), localPath(2),
                                    StableLabelTarget::loop(loopScope.clone()))));
  auto controlTransfers = singletonSequence(require(
      StableControlTransferFact::from(owner.clone(), localPath(15), ControlTransferKind::Continue,
                                      StableControlTarget::explicitLabel(labelKey.clone()))));

  auto closure = require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(3),
                                                      AnonymousOwnerLocalRole::Closure));
  auto closures = singletonSequence(
      require(StableClosureFact::from(owner.clone(), closure.clone(), closureScope.clone())));
  zc::Vector<LocalSyntaxPath> referenceValues;
  referenceValues.add(localPath(6));
  auto references =
      require(StableBindingSequenceBuilder<LocalSyntaxPath>::fromNonEmpty(zc::mv(referenceValues)));
  zc::Vector<StableClosureFreeVariable> variableValues;
  variableValues.add(StableClosureFreeVariable::from(localTarget.clone(), zc::mv(references)));
  auto variables = require(
      StableBindingSequenceBuilder<StableClosureFreeVariable>::from(zc::mv(variableValues)));
  auto closureFreeVariables = singletonSequence(require(
      StableClosureFreeVariableFact::from(owner.clone(), closure.clone(), zc::mv(variables))));

  auto functionExpression = require(AnonymousOwnerLocalKey::from(
      owner.owner().clone(), localPath(3), AnonymousOwnerLocalRole::FunctionExpression));
  auto capture = require(StableExplicitCaptureBindingFact::from(
      localPath(8), localTarget.clone(), StableExplicitCaptureMode::ByReference));
  auto explicitClosureCaptures = singletonSequence(require(
      StableExplicitClosureCaptureFact::from(owner.clone(), zc::mv(functionExpression),
                                             localPath(7), singletonSequence(zc::mv(capture)))));
  auto failedLookups = singletonSequence(require(StableFailedLookupFact::from(
      BinderQueryOwner::body(owner.clone()), localPath(16), Namespace::Value,
      declaredName("missing"_zc), StableFailedLookupOutcome::missing())));

  return require(BoundOwnerBody::from(
      owner.clone(), zc::mv(scopes), zc::mv(nodeScopes), zc::mv(bindings), zc::mv(resolutions),
      zc::mv(deferredMembers), zc::mv(selfTypes), zc::mv(thisBindings), zc::mv(shadowTargets),
      zc::mv(labels), zc::mv(controlTransfers), zc::mv(closures), zc::mv(closureFreeVariables),
      zc::mv(explicitClosureCaptures), zc::mv(failedLookups)));
}

ZC_TEST("StableBindingFacts.BoundOwnerBodyAdmitsEveryPopulatedFactFamily") {
  auto owner = ownerBody();
  auto body = populatedOwnerBody();
  ZC_EXPECT(body == body.clone() && body.owner() == owner && body.scopes().values().size() == 3 &&
            body.nodeScopes().values().size() == 17 && body.bindings().values().size() == 1 &&
            body.resolutions().values().size() == 1 &&
            body.deferredMembers().values().size() == 1 && body.selfTypes().values().size() == 1 &&
            body.thisBindings().values().size() == 1 && body.shadowTargets().values().size() == 1 &&
            body.labels().values().size() == 1 && body.controlTransfers().values().size() == 1 &&
            body.closures().values().size() == 1 &&
            body.closureFreeVariables().values().size() == 1 &&
            body.explicitClosureCaptures().values().size() == 1 &&
            body.failedLookups().values().size() == 1);
}

ZC_TEST("StableBindingFacts.BoundOwnerBodyAdmitsEmptyOwnerBody") {
  auto owner = ownerBody();
  auto body =
      require(BoundOwnerBody::from(owner.clone(), CanonicalSequence<StableBodyScopeFact>::empty(),
                                   CanonicalSequence<StableBodyNodeScopeFact>::empty(),
                                   CanonicalSequence<StableOwnerLocalBindingFact>::empty(),
                                   CanonicalSequence<StableResolutionFact>::empty(),
                                   CanonicalSequence<StableDeferredMemberFact>::empty(),
                                   CanonicalSequence<StableSelfTypeFact>::empty(),
                                   CanonicalSequence<StableThisBindingFact>::empty(),
                                   CanonicalSequence<StableShadowTargetFact>::empty(),
                                   CanonicalSequence<StableLabelFact>::empty(),
                                   CanonicalSequence<StableControlTransferFact>::empty(),
                                   CanonicalSequence<StableClosureFact>::empty(),
                                   CanonicalSequence<StableClosureFreeVariableFact>::empty(),
                                   CanonicalSequence<StableExplicitClosureCaptureFact>::empty(),
                                   CanonicalSequence<StableFailedLookupFact>::empty()));

  ZC_EXPECT(body.owner() == owner);
  ZC_EXPECT(body.scopes().values().size() == 0);
  ZC_EXPECT(body.nodeScopes().values().size() == 0);
  ZC_EXPECT(body.bindings().values().size() == 0);
  ZC_EXPECT(body.resolutions().values().size() == 0);
  ZC_EXPECT(body.deferredMembers().values().size() == 0);
  ZC_EXPECT(body.selfTypes().values().size() == 0);
  ZC_EXPECT(body.thisBindings().values().size() == 0);
  ZC_EXPECT(body.shadowTargets().values().size() == 0);
  ZC_EXPECT(body.labels().values().size() == 0);
  ZC_EXPECT(body.controlTransfers().values().size() == 0);
  ZC_EXPECT(body.closures().values().size() == 0);
  ZC_EXPECT(body.closureFreeVariables().values().size() == 0);
  ZC_EXPECT(body.explicitClosureCaptures().values().size() == 0);
  ZC_EXPECT(body.failedLookups().values().size() == 0);
}

ZC_TEST("StableBindingFacts.BoundOwnerBodyAdmitsNodesInOwningModuleScope") {
  auto owner = ownerBody();
  auto nodeScope = singletonSequence(require(StableBodyNodeScopeFact::from(
      owner.clone(), localPath(), StableScopeOwnerKey::module(module("owner"_zc)))));
  auto body = BoundOwnerBody::from(owner.clone(), CanonicalSequence<StableBodyScopeFact>::empty(),
                                   zc::mv(nodeScope),
                                   CanonicalSequence<StableOwnerLocalBindingFact>::empty(),
                                   CanonicalSequence<StableResolutionFact>::empty(),
                                   CanonicalSequence<StableDeferredMemberFact>::empty(),
                                   CanonicalSequence<StableSelfTypeFact>::empty(),
                                   CanonicalSequence<StableThisBindingFact>::empty(),
                                   CanonicalSequence<StableShadowTargetFact>::empty(),
                                   CanonicalSequence<StableLabelFact>::empty(),
                                   CanonicalSequence<StableControlTransferFact>::empty(),
                                   CanonicalSequence<StableClosureFact>::empty(),
                                   CanonicalSequence<StableClosureFreeVariableFact>::empty(),
                                   CanonicalSequence<StableExplicitClosureCaptureFact>::empty(),
                                   CanonicalSequence<StableFailedLookupFact>::empty());
  ZC_EXPECT(body != zc::none);
}

enum class OwnerBodyRelationMutation {
  None,
  ScopeCycle,
  MissingScopeParent,
  MissingScopeCoverage,
  NodeUnknownScope,
  BindingUnknownScope,
  ResolutionUnknownBinding,
  LookupCollision,
  DeferredUnknownBase,
  DuplicateSelfPath,
  LabelTargetKind,
  ClosureScopeKind,
  FreeReferenceOutsideClosure,
  MissingFreeVariableInventory,
  CaptureUnknownClosure,
  ControlTargetNotAncestor,
  ControlCrossesCallable,
  ShadowNamespace,
  FailedCandidateNamespace
};

zc::Maybe<BoundOwnerBody> relationOwnerBody(OwnerBodyRelationMutation mutation) {
  auto owner = ownerBody();
  auto rootScope = StableScopeOwnerKey::body(owner.clone(), localPath(1));
  auto childScope = StableScopeOwnerKey::body(owner.clone(), localPath(2));
  auto closureScope = StableScopeOwnerKey::body(owner.clone(), localPath(3));
  auto scopes = singletonSequence(require(StableBodyScopeFact::from(
      owner.clone(), rootScope.clone(), StableScopeOwnerKey::module(module("owner"_zc)),
      ScopeKind::Function)));
  const bool hasChild = mutation == OwnerBodyRelationMutation::LabelTargetKind ||
                        mutation == OwnerBodyRelationMutation::ClosureScopeKind ||
                        mutation == OwnerBodyRelationMutation::FreeReferenceOutsideClosure ||
                        mutation == OwnerBodyRelationMutation::MissingFreeVariableInventory ||
                        mutation == OwnerBodyRelationMutation::ControlTargetNotAncestor ||
                        mutation == OwnerBodyRelationMutation::ControlCrossesCallable;
  if (mutation == OwnerBodyRelationMutation::ScopeCycle) {
    scopes = pairSequence(require(StableBodyScopeFact::from(owner.clone(), rootScope.clone(),
                                                            childScope.clone(), ScopeKind::Block)),
                          require(StableBodyScopeFact::from(owner.clone(), childScope.clone(),
                                                            rootScope.clone(), ScopeKind::Block)));
  } else if (mutation == OwnerBodyRelationMutation::MissingScopeParent) {
    scopes = singletonSequence(require(StableBodyScopeFact::from(
        owner.clone(), rootScope.clone(), StableScopeOwnerKey::body(owner.clone(), localPath(99)),
        ScopeKind::Block)));
  } else if (hasChild) {
    auto childKind = ScopeKind::Closure;
    if (mutation == OwnerBodyRelationMutation::LabelTargetKind)
      childKind = ScopeKind::Block;
    else if (mutation == OwnerBodyRelationMutation::ClosureScopeKind ||
             mutation == OwnerBodyRelationMutation::ControlTargetNotAncestor ||
             mutation == OwnerBodyRelationMutation::ControlCrossesCallable)
      childKind = ScopeKind::Loop;
    scopes =
        pairSequence(require(StableBodyScopeFact::from(
                         owner.clone(), rootScope.clone(),
                         StableScopeOwnerKey::module(module("owner"_zc)), ScopeKind::Function)),
                     require(StableBodyScopeFact::from(owner.clone(), childScope.clone(),
                                                       rootScope.clone(), childKind)));
    if (mutation == OwnerBodyRelationMutation::ControlCrossesCallable) {
      zc::Vector<StableBodyScopeFact> values;
      values.add(require(StableBodyScopeFact::from(owner.clone(), rootScope.clone(),
                                                   StableScopeOwnerKey::module(module("owner"_zc)),
                                                   ScopeKind::Function)));
      values.add(require(StableBodyScopeFact::from(owner.clone(), childScope.clone(),
                                                   rootScope.clone(), ScopeKind::Loop)));
      values.add(require(StableBodyScopeFact::from(owner.clone(), closureScope.clone(),
                                                   childScope.clone(), ScopeKind::Closure)));
      scopes = require(StableBindingSequenceBuilder<StableBodyScopeFact>::from(zc::mv(values)));
    }
  }

  zc::Vector<StableBodyNodeScopeFact> nodeValues;
  if (mutation != OwnerBodyRelationMutation::MissingScopeCoverage)
    nodeValues.add(
        require(StableBodyNodeScopeFact::from(owner.clone(), localPath(1), rootScope.clone())));
  if (hasChild)
    nodeValues.add(
        require(StableBodyNodeScopeFact::from(owner.clone(), localPath(2), childScope.clone())));
  if (mutation == OwnerBodyRelationMutation::ControlCrossesCallable)
    nodeValues.add(
        require(StableBodyNodeScopeFact::from(owner.clone(), localPath(3), closureScope.clone())));
  auto useScope = mutation == OwnerBodyRelationMutation::ControlCrossesCallable
                      ? closureScope.clone()
                      : rootScope.clone();
  if (mutation == OwnerBodyRelationMutation::NodeUnknownScope)
    useScope = StableScopeOwnerKey::body(owner.clone(), localPath(99));
  nodeValues.add(
      require(StableBodyNodeScopeFact::from(owner.clone(), localPath(20), zc::mv(useScope))));
  auto nodeScopes =
      require(StableBindingSequenceBuilder<StableBodyNodeScopeFact>::from(zc::mv(nodeValues)));

  auto bindings = CanonicalSequence<StableOwnerLocalBindingFact>::empty();
  auto resolutions = CanonicalSequence<StableResolutionFact>::empty();
  auto deferredMembers = CanonicalSequence<StableDeferredMemberFact>::empty();
  auto selfTypes = CanonicalSequence<StableSelfTypeFact>::empty();
  auto shadowTargets = CanonicalSequence<StableShadowTargetFact>::empty();
  auto labels = CanonicalSequence<StableLabelFact>::empty();
  auto controlTransfers = CanonicalSequence<StableControlTransferFact>::empty();
  auto closures = CanonicalSequence<StableClosureFact>::empty();
  auto closureFreeVariables = CanonicalSequence<StableClosureFreeVariableFact>::empty();
  auto explicitClosureCaptures = CanonicalSequence<StableExplicitClosureCaptureFact>::empty();
  auto failedLookups = CanonicalSequence<StableFailedLookupFact>::empty();

  if (mutation == OwnerBodyRelationMutation::BindingUnknownScope ||
      mutation == OwnerBodyRelationMutation::ShadowNamespace) {
    auto localKey = require(OwnerLocalBindingKey::from(
        owner.owner().clone(), localPath(20), OwnerLocalBindingNamespace::Value,
        OwnerLocalBindingKind::Local, declaredName("value"_zc)));
    auto declaringScope = mutation == OwnerBodyRelationMutation::BindingUnknownScope
                              ? StableScopeOwnerKey::body(owner.clone(), localPath(99))
                              : rootScope.clone();
    bindings = singletonSequence(require(StableOwnerLocalBindingFact::from(
        owner.clone(), localKey.clone(), OwnerLocalBindingKind::Local, declaredName("value"_zc),
        Namespace::Value, zc::mv(declaringScope), DefinitionActivation::ExpressionIntroduction)));
    if (mutation == OwnerBodyRelationMutation::ShadowNamespace) {
      shadowTargets = singletonSequence(require(StableShadowTargetFact::from(
          owner.clone(),
          require(StableBindingTargetKey::ownerLocal(owner.clone(), zc::mv(localKey))),
          StableBindingTargetKey::genericParameter(StableGenericParameterQueryKey::from(
              module("owner"_zc), digestKey<identity::GenericParameterKey>(0x51))))));
    }
  }
  if (mutation == OwnerBodyRelationMutation::ResolutionUnknownBinding ||
      mutation == OwnerBodyRelationMutation::LookupCollision) {
    auto target =
        mutation == OwnerBodyRelationMutation::ResolutionUnknownBinding
            ? require(StableBindingTargetKey::ownerLocal(
                  owner.clone(),
                  require(OwnerLocalBindingKey::from(
                      owner.owner().clone(), localPath(21), OwnerLocalBindingNamespace::Value,
                      OwnerLocalBindingKind::Local, declaredName("absent"_zc)))))
            : StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
                  module("owner"_zc), digestKey<identity::DefinitionKey>(0x52)));
    resolutions = singletonSequence(require(StableResolutionFact::from(
        owner.clone(), localPath(20), Namespace::Value, zc::mv(target),
        StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
            module("owner"_zc), digestKey<identity::DefinitionKey>(0x53))),
        BindingOrigin::LocalDeclaration)));
    if (mutation == OwnerBodyRelationMutation::LookupCollision)
      failedLookups = singletonSequence(require(StableFailedLookupFact::from(
          BinderQueryOwner::body(owner.clone()), localPath(20), Namespace::Value,
          declaredName("missing"_zc), StableFailedLookupOutcome::missing())));
  }
  if (mutation == OwnerBodyRelationMutation::DeferredUnknownBase) {
    zc::Vector<Namespace> namespaceValues;
    namespaceValues.add(Namespace::Value);
    deferredMembers = singletonSequence(require(StableDeferredMemberFact::from(
        owner.clone(), localPath(20), localPath(99), MemberAccessKind::Dot,
        declaredName("member"_zc),
        require(StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(namespaceValues))),
        CanonicalSequence<LocalSyntaxPath>::empty())));
  }
  if (mutation == OwnerBodyRelationMutation::DuplicateSelfPath) {
    selfTypes =
        pairSequence(require(StableSelfTypeFact::from(
                         owner.clone(), localPath(20),
                         StableSelfOwner::nominal(StableDefinitionQueryKey::from(
                             module("owner"_zc), digestKey<identity::DefinitionKey>(0x54))))),
                     require(StableSelfTypeFact::from(
                         owner.clone(), localPath(20),
                         StableSelfOwner::interface(StableDefinitionQueryKey::from(
                             module("owner"_zc), digestKey<identity::DefinitionKey>(0x55))))));
  }
  if (mutation == OwnerBodyRelationMutation::LabelTargetKind) {
    labels = singletonSequence(require(StableLabelFact::from(
        StableLabelKey::from(owner.clone(), localPath(20)), declaredName("label"_zc), localPath(2),
        StableLabelTarget::loop(childScope.clone()))));
  }

  const bool hasClosure = mutation == OwnerBodyRelationMutation::ClosureScopeKind ||
                          mutation == OwnerBodyRelationMutation::FreeReferenceOutsideClosure ||
                          mutation == OwnerBodyRelationMutation::MissingFreeVariableInventory;
  if (hasClosure) {
    auto closure = require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(2),
                                                        AnonymousOwnerLocalRole::Closure));
    closures = singletonSequence(
        require(StableClosureFact::from(owner.clone(), closure.clone(), childScope.clone())));
    if (mutation != OwnerBodyRelationMutation::MissingFreeVariableInventory) {
      auto variables = CanonicalSequence<StableClosureFreeVariable>::empty();
      if (mutation == OwnerBodyRelationMutation::FreeReferenceOutsideClosure) {
        zc::Vector<LocalSyntaxPath> references;
        references.add(localPath(20));
        variables = singletonSequence(StableClosureFreeVariable::from(
            StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
                module("owner"_zc), digestKey<identity::DefinitionKey>(0x56))),
            require(
                StableBindingSequenceBuilder<LocalSyntaxPath>::fromNonEmpty(zc::mv(references)))));
      }
      closureFreeVariables = singletonSequence(require(
          StableClosureFreeVariableFact::from(owner.clone(), zc::mv(closure), zc::mv(variables))));
    }
  }
  if (mutation == OwnerBodyRelationMutation::CaptureUnknownClosure) {
    explicitClosureCaptures = singletonSequence(require(StableExplicitClosureCaptureFact::from(
        owner.clone(),
        require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(2),
                                             AnonymousOwnerLocalRole::FunctionExpression)),
        localPath(20), CanonicalSequence<StableExplicitCaptureBindingFact>::empty())));
  }
  if (mutation == OwnerBodyRelationMutation::ControlTargetNotAncestor ||
      mutation == OwnerBodyRelationMutation::ControlCrossesCallable) {
    controlTransfers = singletonSequence(require(
        StableControlTransferFact::from(owner.clone(), localPath(20), ControlTransferKind::Break,
                                        StableControlTarget::loop(childScope.clone()))));
  }
  if (mutation == OwnerBodyRelationMutation::FailedCandidateNamespace) {
    zc::Vector<StableBindingTargetKey> candidates;
    addCanonicalPair(candidates,
                     StableBindingTargetKey::genericParameter(StableGenericParameterQueryKey::from(
                         module("owner"_zc), digestKey<identity::GenericParameterKey>(0x57))),
                     StableBindingTargetKey::module(module("target"_zc)));
    auto admitted = require(
        StableBindingSequenceBuilder<StableBindingTargetKey>::fromNonEmpty(zc::mv(candidates)));
    auto outcome = require(StableFailedLookupOutcome::ambiguous(zc::mv(admitted)));
    failedLookups = singletonSequence(require(StableFailedLookupFact::from(
        BinderQueryOwner::body(owner.clone()), localPath(20), Namespace::Value,
        declaredName("ambiguous"_zc), zc::mv(outcome))));
  }
  return BoundOwnerBody::from(
      zc::mv(owner), zc::mv(scopes), zc::mv(nodeScopes), zc::mv(bindings), zc::mv(resolutions),
      zc::mv(deferredMembers), zc::mv(selfTypes), CanonicalSequence<StableThisBindingFact>::empty(),
      zc::mv(shadowTargets), zc::mv(labels), zc::mv(controlTransfers), zc::mv(closures),
      zc::mv(closureFreeVariables), zc::mv(explicitClosureCaptures), zc::mv(failedLookups));
}

ZC_TEST("StableBindingFacts.BoundOwnerBodyRejectsStructuralAndRelationalDrift") {
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::None) != zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::ScopeCycle) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::MissingScopeParent) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::MissingScopeCoverage) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::NodeUnknownScope) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::BindingUnknownScope) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::ResolutionUnknownBinding) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::LookupCollision) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::DeferredUnknownBase) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::DuplicateSelfPath) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::LabelTargetKind) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::ClosureScopeKind) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::FreeReferenceOutsideClosure) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::MissingFreeVariableInventory) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::CaptureUnknownClosure) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::ControlTargetNotAncestor) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::ControlCrossesCallable) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::ShadowNamespace) == zc::none);
  ZC_EXPECT(relationOwnerBody(OwnerBodyRelationMutation::FailedCandidateNamespace) == zc::none);
}

enum class OwnerBodyForeignFamily {
  Scope,
  NodeScope,
  Binding,
  Resolution,
  DeferredMember,
  SelfType,
  ThisBinding,
  ShadowTarget,
  Label,
  ControlTransfer,
  Closure,
  ClosureFreeVariable,
  ExplicitClosureCapture,
  FailedLookup
};

zc::Maybe<BoundOwnerBody> foreignOwnerBody(OwnerBodyForeignFamily family) {
  auto owner = ownerBody();
  auto foreign = ownerBody("foreign"_zc);
  auto rootScope = StableScopeOwnerKey::body(owner.clone(), localPath(1));
  auto foreignRoot = StableScopeOwnerKey::body(foreign.clone(), localPath(1));
  auto scopes = singletonSequence(require(StableBodyScopeFact::from(
      owner.clone(), rootScope.clone(), StableScopeOwnerKey::module(module("owner"_zc)),
      ScopeKind::Function)));
  auto nodeScopes = singletonSequence(
      require(StableBodyNodeScopeFact::from(owner.clone(), localPath(1), rootScope.clone())));
  auto bindings = CanonicalSequence<StableOwnerLocalBindingFact>::empty();
  auto resolutions = CanonicalSequence<StableResolutionFact>::empty();
  auto deferredMembers = CanonicalSequence<StableDeferredMemberFact>::empty();
  auto selfTypes = CanonicalSequence<StableSelfTypeFact>::empty();
  auto thisBindings = CanonicalSequence<StableThisBindingFact>::empty();
  auto shadowTargets = CanonicalSequence<StableShadowTargetFact>::empty();
  auto labels = CanonicalSequence<StableLabelFact>::empty();
  auto controlTransfers = CanonicalSequence<StableControlTransferFact>::empty();
  auto closures = CanonicalSequence<StableClosureFact>::empty();
  auto closureFreeVariables = CanonicalSequence<StableClosureFreeVariableFact>::empty();
  auto explicitClosureCaptures = CanonicalSequence<StableExplicitClosureCaptureFact>::empty();
  auto failedLookups = CanonicalSequence<StableFailedLookupFact>::empty();

  switch (family) {
    case OwnerBodyForeignFamily::Scope:
      scopes = singletonSequence(require(StableBodyScopeFact::from(
          foreign.clone(), foreignRoot.clone(), StableScopeOwnerKey::module(module("foreign"_zc)),
          ScopeKind::Function)));
      break;
    case OwnerBodyForeignFamily::NodeScope:
      nodeScopes = singletonSequence(require(
          StableBodyNodeScopeFact::from(foreign.clone(), localPath(1), foreignRoot.clone())));
      break;
    case OwnerBodyForeignFamily::Binding: {
      auto key = require(OwnerLocalBindingKey::from(
          foreign.owner().clone(), localPath(20), OwnerLocalBindingNamespace::Value,
          OwnerLocalBindingKind::Local, declaredName("value"_zc)));
      bindings = singletonSequence(require(StableOwnerLocalBindingFact::from(
          foreign.clone(), zc::mv(key), OwnerLocalBindingKind::Local, declaredName("value"_zc),
          Namespace::Value, foreignRoot.clone(), DefinitionActivation::ExpressionIntroduction)));
      break;
    }
    case OwnerBodyForeignFamily::Resolution:
      resolutions = singletonSequence(require(StableResolutionFact::from(
          foreign.clone(), localPath(20), Namespace::Value,
          StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
              module("foreign"_zc), digestKey<identity::DefinitionKey>(0x61))),
          StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
              module("foreign"_zc), digestKey<identity::DefinitionKey>(0x62))),
          BindingOrigin::LocalDeclaration)));
      break;
    case OwnerBodyForeignFamily::DeferredMember: {
      zc::Vector<Namespace> values;
      values.add(Namespace::Value);
      deferredMembers = singletonSequence(require(StableDeferredMemberFact::from(
          foreign.clone(), localPath(20), localPath(21), MemberAccessKind::Dot,
          declaredName("member"_zc),
          require(StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(values))),
          CanonicalSequence<LocalSyntaxPath>::empty())));
      break;
    }
    case OwnerBodyForeignFamily::SelfType:
      selfTypes = singletonSequence(require(StableSelfTypeFact::from(
          foreign.clone(), localPath(20),
          StableSelfOwner::nominal(StableDefinitionQueryKey::from(
              module("foreign"_zc), digestKey<identity::DefinitionKey>(0x63))))));
      break;
    case OwnerBodyForeignFamily::ThisBinding:
      thisBindings = singletonSequence(require(StableThisBindingFact::from(
          foreign.clone(), localPath(20),
          StableCallableParameterQueryKey::from(module("foreign"_zc),
                                                digestKey<identity::CallableParameterKey>(0x64)))));
      break;
    case OwnerBodyForeignFamily::ShadowTarget:
      shadowTargets = singletonSequence(require(StableShadowTargetFact::from(
          foreign.clone(),
          StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
              module("foreign"_zc), digestKey<identity::DefinitionKey>(0x65))),
          StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
              module("foreign"_zc), digestKey<identity::DefinitionKey>(0x66))))));
      break;
    case OwnerBodyForeignFamily::Label:
      labels = singletonSequence(require(StableLabelFact::from(
          StableLabelKey::from(foreign.clone(), localPath(20)), declaredName("label"_zc),
          localPath(21),
          StableLabelTarget::block(StableScopeOwnerKey::body(foreign.clone(), localPath(21))))));
      break;
    case OwnerBodyForeignFamily::ControlTransfer:
      controlTransfers = singletonSequence(require(StableControlTransferFact::from(
          foreign.clone(), localPath(20), ControlTransferKind::Break,
          StableControlTarget::loop(StableScopeOwnerKey::body(foreign.clone(), localPath(21))))));
      break;
    case OwnerBodyForeignFamily::Closure: {
      auto closure = require(AnonymousOwnerLocalKey::from(foreign.owner().clone(), localPath(20),
                                                          AnonymousOwnerLocalRole::Closure));
      closures = singletonSequence(require(
          StableClosureFact::from(foreign.clone(), zc::mv(closure),
                                  StableScopeOwnerKey::body(foreign.clone(), localPath(20)))));
      break;
    }
    case OwnerBodyForeignFamily::ClosureFreeVariable: {
      auto closure = require(AnonymousOwnerLocalKey::from(foreign.owner().clone(), localPath(20),
                                                          AnonymousOwnerLocalRole::Closure));
      closureFreeVariables = singletonSequence(require(StableClosureFreeVariableFact::from(
          foreign.clone(), zc::mv(closure),
          CanonicalSequence<StableClosureFreeVariable>::empty())));
      break;
    }
    case OwnerBodyForeignFamily::ExplicitClosureCapture: {
      auto closure = require(AnonymousOwnerLocalKey::from(
          foreign.owner().clone(), localPath(20), AnonymousOwnerLocalRole::FunctionExpression));
      explicitClosureCaptures = singletonSequence(require(StableExplicitClosureCaptureFact::from(
          foreign.clone(), zc::mv(closure), localPath(21),
          CanonicalSequence<StableExplicitCaptureBindingFact>::empty())));
      break;
    }
    case OwnerBodyForeignFamily::FailedLookup:
      failedLookups = singletonSequence(require(StableFailedLookupFact::from(
          BinderQueryOwner::body(foreign.clone()), localPath(20), Namespace::Value,
          declaredName("missing"_zc), StableFailedLookupOutcome::missing())));
      break;
  }
  return BoundOwnerBody::from(
      zc::mv(owner), zc::mv(scopes), zc::mv(nodeScopes), zc::mv(bindings), zc::mv(resolutions),
      zc::mv(deferredMembers), zc::mv(selfTypes), zc::mv(thisBindings), zc::mv(shadowTargets),
      zc::mv(labels), zc::mv(controlTransfers), zc::mv(closures), zc::mv(closureFreeVariables),
      zc::mv(explicitClosureCaptures), zc::mv(failedLookups));
}

zc::Maybe<BoundOwnerBody> scaledOwnerBody(uint32_t depth) {
  auto owner = ownerBody();
  zc::Vector<StableBodyScopeFact> scopeValues;
  zc::Vector<StableBodyNodeScopeFact> nodeValues;
  for (uint32_t component = 1; component <= depth; ++component) {
    auto scope = StableScopeOwnerKey::body(owner.clone(), localPath(component));
    auto parent = component == 1
                      ? StableScopeOwnerKey::module(module("owner"_zc))
                      : StableScopeOwnerKey::body(owner.clone(), localPath(component - 1));
    const auto kind = component == 1       ? ScopeKind::Function
                      : component == depth ? ScopeKind::Closure
                                           : ScopeKind::Block;
    scopeValues.add(
        require(StableBodyScopeFact::from(owner.clone(), scope.clone(), zc::mv(parent), kind)));
    nodeValues.add(
        require(StableBodyNodeScopeFact::from(owner.clone(), localPath(component), zc::mv(scope))));
  }
  auto closure = require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(depth),
                                                      AnonymousOwnerLocalRole::Closure));
  zc::Vector<LocalSyntaxPath> referenceValues;
  referenceValues.add(localPath(depth));
  auto variable = StableClosureFreeVariable::from(
      StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
          module("owner"_zc), digestKey<identity::DefinitionKey>(0x67))),
      require(
          StableBindingSequenceBuilder<LocalSyntaxPath>::fromNonEmpty(zc::mv(referenceValues))));
  return BoundOwnerBody::from(
      owner.clone(),
      require(StableBindingSequenceBuilder<StableBodyScopeFact>::from(zc::mv(scopeValues))),
      require(StableBindingSequenceBuilder<StableBodyNodeScopeFact>::from(zc::mv(nodeValues))),
      CanonicalSequence<StableOwnerLocalBindingFact>::empty(),
      CanonicalSequence<StableResolutionFact>::empty(),
      CanonicalSequence<StableDeferredMemberFact>::empty(),
      CanonicalSequence<StableSelfTypeFact>::empty(),
      CanonicalSequence<StableThisBindingFact>::empty(),
      CanonicalSequence<StableShadowTargetFact>::empty(),
      CanonicalSequence<StableLabelFact>::empty(),
      CanonicalSequence<StableControlTransferFact>::empty(),
      singletonSequence(require(
          StableClosureFact::from(owner.clone(), closure.clone(),
                                  StableScopeOwnerKey::body(owner.clone(), localPath(depth))))),
      singletonSequence(require(StableClosureFreeVariableFact::from(
          owner.clone(), zc::mv(closure), singletonSequence(zc::mv(variable))))),
      CanonicalSequence<StableExplicitClosureCaptureFact>::empty(),
      CanonicalSequence<StableFailedLookupFact>::empty());
}

ZC_TEST("StableBindingFacts.BoundOwnerBodyRejectsEveryForeignFactFamily") {
  constexpr OwnerBodyForeignFamily families[] = {OwnerBodyForeignFamily::Scope,
                                                 OwnerBodyForeignFamily::NodeScope,
                                                 OwnerBodyForeignFamily::Binding,
                                                 OwnerBodyForeignFamily::Resolution,
                                                 OwnerBodyForeignFamily::DeferredMember,
                                                 OwnerBodyForeignFamily::SelfType,
                                                 OwnerBodyForeignFamily::ThisBinding,
                                                 OwnerBodyForeignFamily::ShadowTarget,
                                                 OwnerBodyForeignFamily::Label,
                                                 OwnerBodyForeignFamily::ControlTransfer,
                                                 OwnerBodyForeignFamily::Closure,
                                                 OwnerBodyForeignFamily::ClosureFreeVariable,
                                                 OwnerBodyForeignFamily::ExplicitClosureCapture,
                                                 OwnerBodyForeignFamily::FailedLookup};
  for (const auto family : families) ZC_EXPECT(foreignOwnerBody(family) == zc::none);
}

ZC_TEST("StableBindingFacts.BoundOwnerBodyRequiresCanonicalAndSemanticMultiplicity") {
  auto owner = ownerBody();
  auto root = StableScopeOwnerKey::body(owner.clone(), localPath(1));
  auto child = StableScopeOwnerKey::body(owner.clone(), localPath(2));
  auto node = require(StableBodyNodeScopeFact::from(owner.clone(), localPath(20), root.clone()));
  zc::Vector<StableBodyNodeScopeFact> exactDuplicates;
  exactDuplicates.add(node.clone());
  exactDuplicates.add(zc::mv(node));
  ZC_EXPECT(StableBindingSequenceBuilder<StableBodyNodeScopeFact>::from(zc::mv(exactDuplicates)) ==
            zc::none);
  auto scopes =
      pairSequence(require(StableBodyScopeFact::from(
                       owner.clone(), root.clone(), StableScopeOwnerKey::module(module("owner"_zc)),
                       ScopeKind::Function)),
                   require(StableBodyScopeFact::from(owner.clone(), child.clone(), root.clone(),
                                                     ScopeKind::Block)));
  auto nodeScopes = pairSequence(
      require(StableBodyNodeScopeFact::from(owner.clone(), localPath(20), root.clone())),
      require(StableBodyNodeScopeFact::from(owner.clone(), localPath(20), child.clone())));
  ZC_EXPECT(BoundOwnerBody::from(zc::mv(owner), zc::mv(scopes), zc::mv(nodeScopes),
                                 CanonicalSequence<StableOwnerLocalBindingFact>::empty(),
                                 CanonicalSequence<StableResolutionFact>::empty(),
                                 CanonicalSequence<StableDeferredMemberFact>::empty(),
                                 CanonicalSequence<StableSelfTypeFact>::empty(),
                                 CanonicalSequence<StableThisBindingFact>::empty(),
                                 CanonicalSequence<StableShadowTargetFact>::empty(),
                                 CanonicalSequence<StableLabelFact>::empty(),
                                 CanonicalSequence<StableControlTransferFact>::empty(),
                                 CanonicalSequence<StableClosureFact>::empty(),
                                 CanonicalSequence<StableClosureFreeVariableFact>::empty(),
                                 CanonicalSequence<StableExplicitClosureCaptureFact>::empty(),
                                 CanonicalSequence<StableFailedLookupFact>::empty()) == zc::none);
}

ZC_TEST("StableBindingFacts.BoundOwnerBodyAdmitsIterativeReferenceCompleteScale") {
  constexpr uint32_t depth = 2048;
  auto body = require(scaledOwnerBody(depth));
  ZC_EXPECT(body == body.clone() && body.owner() == ownerBody() &&
            body.scopes().values().size() == depth && body.nodeScopes().values().size() == depth &&
            body.bindings().values().size() == 0 && body.resolutions().values().size() == 0 &&
            body.deferredMembers().values().size() == 0 && body.selfTypes().values().size() == 0 &&
            body.thisBindings().values().size() == 0 && body.shadowTargets().values().size() == 0 &&
            body.labels().values().size() == 0 && body.controlTransfers().values().size() == 0 &&
            body.closures().values().size() == 1 &&
            body.closureFreeVariables().values().size() == 1 &&
            body.explicitClosureCaptures().values().size() == 0 &&
            body.failedLookups().values().size() == 0);
}

ZC_TEST("StableBindingFacts.OwnerAllocationRangeAdmitsCompleteDenseRanges") {
  constexpr uint32_t limit = 0xffffffffU;
  auto range = require(OwnerAllocationRange::from(ownerBody(), 4, 3, 0, 2, 0, 1, limit, 0));
  ZC_EXPECT(range == range.clone() && range.owner() == ownerBody() && range.scopeBegin() == 4 &&
            range.scopeCount() == 3 && range.ownerLocalBegin() == 0 &&
            range.ownerLocalCount() == 2 && range.anonymousBegin() == 0 &&
            range.anonymousCount() == 1 && range.labelBegin() == limit && range.labelCount() == 0);
  ZC_EXPECT(range != require(OwnerAllocationRange::from(ownerBody(), 4, 2, 0, 2, 0, 1, limit, 0)));
  ZC_EXPECT(OwnerAllocationRange::from(ownerBody(), 0, limit, 0, limit, 0, limit, 0, limit) !=
            zc::none);
}

ZC_TEST("StableBindingFacts.OwnerAllocationRangeRejectsEveryOverflowDomain") {
  constexpr uint32_t limit = 0xffffffffU;
  ZC_EXPECT(OwnerAllocationRange::from(ownerBody(), limit, 1, 0, 0, 0, 0, 0, 0) == zc::none);
  ZC_EXPECT(OwnerAllocationRange::from(ownerBody(), 0, 0, limit, 1, 0, 0, 0, 0) == zc::none);
  ZC_EXPECT(OwnerAllocationRange::from(ownerBody(), 0, 0, 0, 0, limit, 1, 0, 0) == zc::none);
  ZC_EXPECT(OwnerAllocationRange::from(ownerBody(), 0, 0, 0, 0, 0, 0, limit, 1) == zc::none);
}

ZC_TEST("StableBindingFacts.ModuleBindingAllocationPlanAdmitsEmptyDensePlan") {
  auto plan = require(ModuleBindingAllocationPlan::from(
      module("owner"_zc), 7, 3, CanonicalSequence<OwnerAllocationRange>::empty()));
  ZC_EXPECT(plan == plan.clone() &&
            plan.key().encode().asPtr() == module("owner"_zc).encode().asPtr() &&
            plan.skeletonScopeCount() == 7 && plan.implementationOccurrenceCount() == 3 &&
            plan.owners().values().size() == 0);
  auto different = require(ModuleBindingAllocationPlan::from(
      module("owner"_zc), 8, 3, CanonicalSequence<OwnerAllocationRange>::empty()));
  ZC_EXPECT(plan != different);
}

enum class AllocationPlanMutation {
  None,
  ScopeGap,
  OwnerLocalGap,
  AnonymousGap,
  LabelGap,
  ForeignOwner,
  DuplicateOwner
};

StableOwnerBodyQueryKey definitionOwnerBody(uint8_t byte) {
  return require(StableOwnerBodyQueryKey::from(
      module("owner"_zc),
      StableBodyOwnerKey::definition(digestKey<identity::DefinitionKey>(byte))));
}

zc::Maybe<ModuleBindingAllocationPlan> allocationPlan(AllocationPlanMutation mutation) {
  zc::TreeMap<FixtureOrderKey, StableOwnerBodyQueryKey> orderedOwners;
  orderFixture(orderedOwners, ownerBody());
  orderFixture(orderedOwners, definitionOwnerBody(0x31));
  orderFixture(orderedOwners, definitionOwnerBody(0x41));
  zc::Vector<StableOwnerBodyQueryKey> owners;
  for (auto& entry : orderedOwners) { owners.add(zc::mv(entry.value)); }

  zc::TreeMap<FixtureOrderKey, OwnerAllocationRange> orderedRanges;
  uint32_t scopeBegin = 5;
  uint32_t ownerLocalBegin = 0;
  uint32_t anonymousBegin = 0;
  uint32_t labelBegin = 0;
  for (size_t index = 0; index < owners.size(); ++index) {
    const uint32_t scopeCount = index == 0 ? 2 : index == 1 ? 0 : 4;
    const uint32_t ownerLocalCount = index == 0 ? 1 : index == 1 ? 2 : 0;
    const uint32_t anonymousCount = index == 0 ? 0 : index == 1 ? 3 : 1;
    const uint32_t labelCount = index == 0 ? 1 : index == 1 ? 0 : 2;
    auto owner =
        mutation == AllocationPlanMutation::ForeignOwner && index == 1     ? ownerBody("foreign"_zc)
        : mutation == AllocationPlanMutation::DuplicateOwner && index == 1 ? owners[0].clone()
                                                                           : owners[index].clone();
    auto range = require(OwnerAllocationRange::from(
        zc::mv(owner),
        scopeBegin + (mutation == AllocationPlanMutation::ScopeGap && index == 1 ? 1 : 0),
        scopeCount,
        ownerLocalBegin + (mutation == AllocationPlanMutation::OwnerLocalGap && index == 1 ? 1 : 0),
        ownerLocalCount,
        anonymousBegin + (mutation == AllocationPlanMutation::AnonymousGap && index == 1 ? 1 : 0),
        anonymousCount,
        labelBegin + (mutation == AllocationPlanMutation::LabelGap && index == 1 ? 1 : 0),
        labelCount));
    orderFixture(orderedRanges, zc::mv(range));
    scopeBegin += scopeCount;
    ownerLocalBegin += ownerLocalCount;
    anonymousBegin += anonymousCount;
    labelBegin += labelCount;
  }
  return ModuleBindingAllocationPlan::from(module("owner"_zc), 5, 3,
                                           admitOrderedFixtures(orderedRanges));
}

BoundOwnerBody emptyOwnerBodyForAllocation(StableOwnerBodyQueryKey&& owner) {
  return require(BoundOwnerBody::from(zc::mv(owner),
                                      CanonicalSequence<StableBodyScopeFact>::empty(),
                                      CanonicalSequence<StableBodyNodeScopeFact>::empty(),
                                      CanonicalSequence<StableOwnerLocalBindingFact>::empty(),
                                      CanonicalSequence<StableResolutionFact>::empty(),
                                      CanonicalSequence<StableDeferredMemberFact>::empty(),
                                      CanonicalSequence<StableSelfTypeFact>::empty(),
                                      CanonicalSequence<StableThisBindingFact>::empty(),
                                      CanonicalSequence<StableShadowTargetFact>::empty(),
                                      CanonicalSequence<StableLabelFact>::empty(),
                                      CanonicalSequence<StableControlTransferFact>::empty(),
                                      CanonicalSequence<StableClosureFact>::empty(),
                                      CanonicalSequence<StableClosureFreeVariableFact>::empty(),
                                      CanonicalSequence<StableExplicitClosureCaptureFact>::empty(),
                                      CanonicalSequence<StableFailedLookupFact>::empty()));
}

zc::Vector<BoundOwnerBody> allocationBodies(const BoundModuleSkeleton& skeleton) {
  zc::Vector<BoundOwnerBody> bodies;
  const auto owners = skeleton.bodyOwners().values();
  for (size_t index = owners.size(); index > 0; --index) {
    const auto& owner = owners[index - 1];
    bodies.add(owner.owner().kind() == StableBodyOwnerKind::Module
                   ? populatedOwnerBody()
                   : emptyOwnerBodyForAllocation(owner.clone()));
  }
  return bodies;
}

ZC_TEST("ModuleBindingAllocationPlanner.ComputesCanonicalFiveDomainRanges") {
  auto skeleton = require(moduleSkeleton(LocalExportMutation::None));
  auto bodies = allocationBodies(skeleton);
  auto plan = require(ModuleBindingAllocationPlanner::from(skeleton, bodies.asPtr().asConst()));

  ZC_EXPECT(ModuleBindingAllocationPlanner::verify(skeleton, bodies.asPtr().asConst(), plan));
  ZC_REQUIRE(plan.key().encode().asPtr() == skeleton.module().encode().asPtr());
  ZC_REQUIRE(plan.skeletonScopeCount() == skeleton.scopes().values().size());
  ZC_REQUIRE(plan.implementationOccurrenceCount() ==
             skeleton.implementationOccurrences().values().size());
  ZC_REQUIRE(plan.owners().values().size() == skeleton.bodyOwners().values().size());
  const auto ranges = plan.owners().values();
  const auto owners = skeleton.bodyOwners().values();
  for (size_t index = 0; index < ranges.size(); ++index) {
    ZC_EXPECT(ranges[index].owner() == owners[index]);
  }
  for (const auto& range : ranges) {
    if (range.owner().owner().kind() != StableBodyOwnerKind::Module) { continue; }
    ZC_EXPECT(range.scopeCount() == 3 && range.ownerLocalCount() == 1 &&
              range.anonymousCount() == 2 && range.labelCount() == 1);
  }
}

ZC_TEST("ModuleBindingAllocationPlanner.RejectsIncompleteForeignAndTamperedInputs") {
  auto skeleton = require(moduleSkeleton(LocalExportMutation::None));
  auto bodies = allocationBodies(skeleton);
  auto plan = require(ModuleBindingAllocationPlanner::from(skeleton, bodies.asPtr().asConst()));

  zc::Vector<BoundOwnerBody> missing;
  missing.add(bodies[0].clone());
  ZC_EXPECT(ModuleBindingAllocationPlanner::from(skeleton, missing.asPtr().asConst()) == zc::none);

  zc::Vector<BoundOwnerBody> duplicate;
  duplicate.add(bodies[0].clone());
  duplicate.add(bodies[0].clone());
  ZC_EXPECT(ModuleBindingAllocationPlanner::from(skeleton, duplicate.asPtr().asConst()) ==
            zc::none);

  zc::Vector<BoundOwnerBody> foreign;
  foreign.add(bodies[0].clone());
  foreign.add(emptyOwnerBodyForAllocation(ownerBody("foreign"_zc)));
  ZC_EXPECT(ModuleBindingAllocationPlanner::from(skeleton, foreign.asPtr().asConst()) == zc::none);
  ZC_EXPECT(!ModuleBindingAllocationPlanner::verify(
      skeleton, bodies.asPtr().asConst(), require(allocationPlan(AllocationPlanMutation::None))));
  ZC_EXPECT(!ModuleBindingAllocationPlanner::verify(skeleton, duplicate.asPtr().asConst(), plan));
}

ZC_TEST("StableBindingFacts.ModuleBindingAllocationPlanEnforcesEveryDenseDomain") {
  auto plan = require(allocationPlan(AllocationPlanMutation::None));
  ZC_EXPECT(plan == plan.clone() && plan.owners().values().size() == 3 &&
            plan.owners().values()[0].scopeBegin() == plan.skeletonScopeCount());
  ZC_EXPECT(allocationPlan(AllocationPlanMutation::ScopeGap) == zc::none);
  ZC_EXPECT(allocationPlan(AllocationPlanMutation::OwnerLocalGap) == zc::none);
  ZC_EXPECT(allocationPlan(AllocationPlanMutation::AnonymousGap) == zc::none);
  ZC_EXPECT(allocationPlan(AllocationPlanMutation::LabelGap) == zc::none);
  ZC_EXPECT(allocationPlan(AllocationPlanMutation::ForeignOwner) == zc::none);
  ZC_EXPECT(allocationPlan(AllocationPlanMutation::DuplicateOwner) == zc::none);

  zc::Vector<OwnerAllocationRange> reversed;
  for (size_t index = plan.owners().values().size(); index > 0; --index) {
    reversed.add(plan.owners().values()[index - 1].clone());
  }
  ZC_EXPECT(StableBindingSequenceBuilder<OwnerAllocationRange>::from(zc::mv(reversed)) == zc::none);

  constexpr uint32_t limit = 0xffffffffU;
  zc::Vector<OwnerAllocationRange> boundaryValues;
  boundaryValues.add(
      require(OwnerAllocationRange::from(ownerBody(), 0, limit, 0, limit, 0, limit, 0, limit)));
  auto boundaryOwners =
      require(StableBindingSequenceBuilder<OwnerAllocationRange>::from(zc::mv(boundaryValues)));
  ZC_EXPECT(ModuleBindingAllocationPlan::from(module("owner"_zc), 0, limit,
                                              zc::mv(boundaryOwners)) != zc::none);
}

ZC_TEST("StableBindingFacts.DeclarationFactsEnforceIdentityAndScopeRelations") {
  auto fact = require(declarationFact(DeclarationFactMutation::None));
  ZC_EXPECT(fact == fact.clone() &&
            fact.queryKey().definition() == identity::DefinitionKey::compute(fact.record()) &&
            fact.declaringScope() == StableScopeOwnerKey::module(module("owner"_zc)) &&
            fact.kind() == identity::DefinitionKind::Function &&
            fact.nameSpace() == Namespace::Value && fact.name().text() == "run"_zc &&
            fact.activation() == DefinitionActivation::ModuleSkeleton &&
            fact.visibility() == MemberVisibility::Public);

  ZC_EXPECT(fact != require(declarationFact(DeclarationFactMutation::OtherIdentity)));
  auto sameShape = require(declarationFact(DeclarationFactMutation::OtherSameShapeIdentity));
  ZC_EXPECT(fact != sameShape && fact.queryKey() != sameShape.queryKey() &&
            fact.record().encode().asPtr() != sameShape.record().encode().asPtr() &&
            fact.kind() == sameShape.kind() && fact.nameSpace() == sameShape.nameSpace() &&
            fact.name() == sameShape.name());
  ZC_EXPECT(fact != require(declarationFact(DeclarationFactMutation::NestedScope)));
  ZC_EXPECT(fact != require(declarationFact(DeclarationFactMutation::ImportActivation)));
  ZC_EXPECT(fact != require(declarationFact(DeclarationFactMutation::PrivateVisibility)));
  auto withoutVisibility = require(declarationFact(DeclarationFactMutation::WithoutVisibility));
  ZC_EXPECT(fact != withoutVisibility && withoutVisibility.visibility() == zc::none);
  ZC_EXPECT(declarationFact(DeclarationFactMutation::QueryIdentity) == zc::none);
  ZC_EXPECT(declarationFact(DeclarationFactMutation::QueryModule) == zc::none);
  ZC_EXPECT(declarationFact(DeclarationFactMutation::Record) == zc::none);
  ZC_EXPECT(declarationFact(DeclarationFactMutation::Scope) == zc::none);
  ZC_EXPECT(declarationFact(DeclarationFactMutation::Kind) == zc::none);
  ZC_EXPECT(declarationFact(DeclarationFactMutation::Namespace) == zc::none);
  ZC_EXPECT(declarationFact(DeclarationFactMutation::Name) == zc::none);
  ZC_EXPECT(declarationFact(DeclarationFactMutation::Activation) == zc::none);
  ZC_EXPECT(declarationFact(DeclarationFactMutation::Visibility) == zc::none);
}

ZC_TEST("StableBindingFacts.ImplementationFactsEnforceAuthorityAndScopeRelations") {
  auto record = implementationRecord("owner"_zc);
  auto implementation = identity::ImplKey::compute(record);
  auto authority = StableImplementationQueryKey::from(module("owner"_zc), implementation.clone());
  auto occurrenceKey = require(StableImplementationOccurrenceQueryKey::from(
      module("owner"_zc), implementationOccurrence(implementation, 1)));
  auto fact = require(StableImplementationOccurrenceFact::from(
      occurrenceKey.clone(), authority.clone(), record.clone(),
      StableScopeOwnerKey::module(module("owner"_zc))));
  ZC_EXPECT(fact == fact.clone() && fact.occurrence() == occurrenceKey &&
            fact.authority() == authority &&
            identity::ImplKey::compute(fact.record()) == implementation &&
            fact.declaringScope() == StableScopeOwnerKey::module(module("owner"_zc)));

  auto secondOccurrence = require(StableImplementationOccurrenceQueryKey::from(
      module("owner"_zc), implementationOccurrence(implementation, 2)));
  auto differentOccurrence = require(StableImplementationOccurrenceFact::from(
      zc::mv(secondOccurrence), authority.clone(), record.clone(), fact.declaringScope().clone()));
  auto nestedScope = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x44)),
      ScopeRole::Declaration));
  auto differentScope = require(StableImplementationOccurrenceFact::from(
      occurrenceKey.clone(), authority.clone(), record.clone(), zc::mv(nestedScope)));
  ZC_EXPECT(fact != differentOccurrence && fact != differentScope);
  auto otherRecord = alternativeImplementationRecord();
  auto otherImplementation = identity::ImplKey::compute(otherRecord);
  auto otherAuthority =
      StableImplementationQueryKey::from(module("owner"_zc), otherImplementation.clone());
  auto otherOccurrence = require(StableImplementationOccurrenceQueryKey::from(
      module("owner"_zc), implementationOccurrence(otherImplementation, 1)));
  auto otherFact = require(
      StableImplementationOccurrenceFact::from(zc::mv(otherOccurrence), zc::mv(otherAuthority),
                                               zc::mv(otherRecord), fact.declaringScope().clone()));
  ZC_EXPECT(fact != otherFact && fact.occurrence() != otherFact.occurrence() &&
            fact.authority() != otherFact.authority() &&
            fact.record().encode().asPtr() != otherFact.record().encode().asPtr());
  auto wrongOccurrence = require(StableImplementationOccurrenceQueryKey::from(
      module("owner"_zc), implementationOccurrence(digestKey<identity::ImplKey>(0x77), 1)));
  ZC_EXPECT(StableImplementationOccurrenceFact::from(zc::mv(wrongOccurrence), authority.clone(),
                                                     record.clone(),
                                                     fact.declaringScope().clone()) == zc::none);
  auto wrongAuthority =
      StableImplementationQueryKey::from(module("owner"_zc), digestKey<identity::ImplKey>(0x77));
  ZC_EXPECT(StableImplementationOccurrenceFact::from(occurrenceKey.clone(), zc::mv(wrongAuthority),
                                                     record.clone(),
                                                     fact.declaringScope().clone()) == zc::none);
  auto foreignAuthority =
      StableImplementationQueryKey::from(module("foreign"_zc), implementation.clone());
  ZC_EXPECT(StableImplementationOccurrenceFact::from(occurrenceKey.clone(),
                                                     zc::mv(foreignAuthority), record.clone(),
                                                     fact.declaringScope().clone()) == zc::none);
  ZC_EXPECT(StableImplementationOccurrenceFact::from(occurrenceKey.clone(), authority.clone(),
                                                     implementationRecord("foreign"_zc),
                                                     fact.declaringScope().clone()) == zc::none);
  ZC_EXPECT(StableImplementationOccurrenceFact::from(
                zc::mv(occurrenceKey), zc::mv(authority), zc::mv(record),
                StableScopeOwnerKey::module(module("foreign"_zc))) == zc::none);
}

ZC_TEST("StableBindingFacts.GenericParameterDeclarationsEnforceHeaderAndScopeRelations") {
  auto fact = require(genericDeclaration(GenericDeclarationMutation::None));
  ZC_EXPECT(fact == fact.clone() &&
            fact.queryKey().parameter() == identity::GenericParameterKey::compute(fact.record()) &&
            fact.headerSite().value().is<DefinitionAuthoritySite>() &&
            fact.declaringScope() == StableScopeOwnerKey::module(module("owner"_zc)) &&
            fact.name().text() == "T"_zc);
  ZC_EXPECT(fact != require(genericDeclaration(GenericDeclarationMutation::OtherIdentity)));
  ZC_EXPECT(fact != require(genericDeclaration(GenericDeclarationMutation::Implementation)));
  ZC_EXPECT(fact != require(genericDeclaration(GenericDeclarationMutation::HeaderSite)));
  ZC_EXPECT(fact != require(genericDeclaration(GenericDeclarationMutation::Scope)));
  ZC_EXPECT(fact != require(genericDeclaration(GenericDeclarationMutation::Name)));
  ZC_EXPECT(genericDeclaration(GenericDeclarationMutation::QueryKey) == zc::none);
  ZC_EXPECT(genericDeclaration(GenericDeclarationMutation::QueryModule) == zc::none);
  ZC_EXPECT(genericDeclaration(GenericDeclarationMutation::HeaderSiteModule) == zc::none);
  ZC_EXPECT(genericDeclaration(GenericDeclarationMutation::SiteKind) == zc::none);
  ZC_EXPECT(genericDeclaration(GenericDeclarationMutation::ImplementationOwner) == zc::none);
  ZC_EXPECT(genericDeclaration(GenericDeclarationMutation::ScopeModule) == zc::none);
}

ZC_TEST("StableBindingFacts.CallableParameterDeclarationsEnforceNameAndSiteRelations") {
  auto fact = require(callableDeclaration(CallableDeclarationMutation::None));
  ZC_EXPECT(fact == fact.clone() &&
            fact.queryKey().parameter() == identity::CallableParameterKey::compute(fact.record()) &&
            fact.headerSite().value().is<DefinitionAuthoritySite>() &&
            fact.declaringScope() == StableScopeOwnerKey::module(module("owner"_zc)) &&
            ZC_ASSERT_NONNULL(fact.name()).text() == "value"_zc);
  ZC_EXPECT(fact != require(callableDeclaration(CallableDeclarationMutation::OtherIdentity)));
  auto receiver = require(callableDeclaration(CallableDeclarationMutation::Receiver));
  ZC_EXPECT(fact != receiver && receiver == receiver.clone() && receiver.name() == zc::none);
  ZC_EXPECT(fact != require(callableDeclaration(CallableDeclarationMutation::HeaderSite)));
  ZC_EXPECT(fact != require(callableDeclaration(CallableDeclarationMutation::Scope)));
  ZC_EXPECT(fact != require(callableDeclaration(CallableDeclarationMutation::Name)));
  ZC_EXPECT(callableDeclaration(CallableDeclarationMutation::QueryKey) == zc::none);
  ZC_EXPECT(callableDeclaration(CallableDeclarationMutation::QueryModule) == zc::none);
  ZC_EXPECT(callableDeclaration(CallableDeclarationMutation::HeaderSiteModule) == zc::none);
  ZC_EXPECT(callableDeclaration(CallableDeclarationMutation::ImplementationSite) == zc::none);
  ZC_EXPECT(callableDeclaration(CallableDeclarationMutation::ScopeModule) == zc::none);
  ZC_EXPECT(callableDeclaration(CallableDeclarationMutation::ReceiverName) == zc::none);
  ZC_EXPECT(callableDeclaration(CallableDeclarationMutation::OrdinaryWithoutName) == zc::none);
}

ZC_TEST("StableBindingFacts.ExportSurfaceRevisionAdmitsOpaqueDigest") {
  const auto digest = digestKey<identity::Sha256Digest>(0x5a);
  const auto revision = ExportSurfaceRevision::fromDigest(digest);
  ZC_EXPECT(revision.digest() == digest);
}

ZC_TEST("StableBindingFacts.BindingNameKeysAdmitOnlyClosedNamespaces") {
  constexpr Namespace values[] = {Namespace::Value, Namespace::Type, Namespace::Module,
                                  Namespace::Label, Namespace::Attribute};
  for (const auto value : values) {
    auto key = BindingNameKey::from(value, declaredName("name"_zc));
    ZC_REQUIRE(key != zc::none);
    ZC_EXPECT(ZC_ASSERT_NONNULL(key).nameSpace() == value &&
              ZC_ASSERT_NONNULL(key).name().text() == "name"_zc);
  }
  ZC_EXPECT(BindingNameKey::from(static_cast<Namespace>(0xff), declaredName("name"_zc)) ==
            zc::none);
}

ZC_TEST("StableBindingFacts.ExportedBindingsRequireExportedClosedVisibilityFacts") {
  auto exportedBinding = [](zc::StringPtr name, zc::StringPtr binding, zc::StringPtr target,
                            zc::Maybe<MemberVisibility>&& visibility) {
    return require(StableExportedBinding::from(
        require(BindingNameKey::from(Namespace::Value, declaredName(name))),
        StableBindingTargetKey::module(module(binding)),
        StableBindingTargetKey::module(module(target)), zc::mv(visibility), true));
  };
  zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
  auto fact = exportedBinding("name"_zc, "binding"_zc, "target"_zc, zc::mv(visibility));
  ZC_EXPECT(fact == fact.clone() && fact.name().nameSpace() == Namespace::Value &&
            fact.name().name().text() == "name"_zc &&
            fact.binding() == StableBindingTargetKey::module(module("binding"_zc)) &&
            fact.canonicalTarget() == StableBindingTargetKey::module(module("target"_zc)) &&
            fact.visibility() == MemberVisibility::Public && fact.exported());

  zc::Maybe<MemberVisibility> privateVisibility = MemberVisibility::Private;
  auto otherVisibility =
      exportedBinding("name"_zc, "binding"_zc, "target"_zc, zc::mv(privateVisibility));
  zc::Maybe<MemberVisibility> protectedVisibility = MemberVisibility::Protected;
  auto protectedFact =
      exportedBinding("name"_zc, "binding"_zc, "target"_zc, zc::mv(protectedVisibility));
  zc::Maybe<MemberVisibility> noVisibility;
  auto unqualifiedFact =
      exportedBinding("name"_zc, "binding"_zc, "target"_zc, zc::mv(noVisibility));
  ZC_EXPECT(protectedFact.visibility() == MemberVisibility::Protected &&
            unqualifiedFact.visibility() == zc::none);

  zc::Maybe<MemberVisibility> otherNameVisibility = MemberVisibility::Public;
  auto otherName =
      exportedBinding("other"_zc, "binding"_zc, "target"_zc, zc::mv(otherNameVisibility));
  zc::Maybe<MemberVisibility> otherBindingVisibility = MemberVisibility::Public;
  auto otherBinding =
      exportedBinding("name"_zc, "other"_zc, "target"_zc, zc::mv(otherBindingVisibility));
  zc::Maybe<MemberVisibility> otherTargetVisibility = MemberVisibility::Public;
  auto otherTarget =
      exportedBinding("name"_zc, "binding"_zc, "other"_zc, zc::mv(otherTargetVisibility));
  ZC_EXPECT(fact != otherName && fact != otherBinding && fact != otherTarget &&
            fact != otherVisibility);

  zc::Maybe<MemberVisibility> invalidVisibility = static_cast<MemberVisibility>(0xff);
  ZC_EXPECT(StableExportedBinding::from(
                require(BindingNameKey::from(Namespace::Value, declaredName("name"_zc))),
                StableBindingTargetKey::module(module("binding"_zc)),
                StableBindingTargetKey::module(module("target"_zc)), zc::mv(invalidVisibility),
                true) == zc::none);
  zc::Maybe<MemberVisibility> absentVisibility;
  ZC_EXPECT(StableExportedBinding::from(
                require(BindingNameKey::from(Namespace::Value, declaredName("name"_zc))),
                StableBindingTargetKey::module(module("binding"_zc)),
                StableBindingTargetKey::module(module("target"_zc)), zc::mv(absentVisibility),
                false) == zc::none);
}

ZC_TEST("StableBindingFacts.LookupProjectionKeysRetainRoutesAndRejectBodyScopes") {
  auto exported = StableExportedBindingQueryKey::from(
      module("owner"_zc), require(BindingNameKey::from(Namespace::Value, declaredName("name"_zc))));
  auto otherModule = StableExportedBindingQueryKey::from(
      module("other"_zc), require(BindingNameKey::from(Namespace::Value, declaredName("name"_zc))));
  auto otherName = StableExportedBindingQueryKey::from(
      module("owner"_zc), require(BindingNameKey::from(Namespace::Type, declaredName("name"_zc))));
  ZC_EXPECT(exported == exported.clone() && exported != otherModule && exported != otherName &&
            exported.module().encode().asPtr() == module("owner"_zc).encode().asPtr() &&
            exported.name().nameSpace() == Namespace::Value);

  auto moduleScope = require(StableScopeNameBucketQueryKey::from(
      StableScopeOwnerKey::module(module("owner"_zc)),
      require(BindingNameKey::from(Namespace::Value, declaredName("name"_zc)))));
  auto definitionScope = require(StableScopeNameBucketQueryKey::from(
      require(StableScopeOwnerKey::definition(
          StableDefinitionQueryKey::from(module("owner"_zc),
                                         digestKey<identity::DefinitionKey>(0x11)),
          ScopeRole::Members)),
      require(BindingNameKey::from(Namespace::Value, declaredName("name"_zc)))));
  auto implementationScope = require(StableScopeNameBucketQueryKey::from(
      require(StableScopeOwnerKey::implementationOccurrence(
          require(StableImplementationOccurrenceQueryKey::from(module("owner"_zc),
                                                               occurrence("owner"_zc))),
          ScopeRole::Implementation)),
      require(BindingNameKey::from(Namespace::Value, declaredName("name"_zc)))));
  auto otherScopeName = require(StableScopeNameBucketQueryKey::from(
      StableScopeOwnerKey::module(module("owner"_zc)),
      require(BindingNameKey::from(Namespace::Type, declaredName("name"_zc)))));
  ZC_EXPECT(moduleScope == moduleScope.clone() && moduleScope != definitionScope &&
            moduleScope != implementationScope && moduleScope != otherScopeName &&
            moduleScope.scope() == StableScopeOwnerKey::module(module("owner"_zc)) &&
            moduleScope.name().name().text() == "name"_zc);
  ZC_EXPECT(StableScopeNameBucketQueryKey::from(
                StableScopeOwnerKey::body(ownerBody(), localPath()),
                require(BindingNameKey::from(Namespace::Value, declaredName("name"_zc)))) ==
            zc::none);
}

ZC_TEST("StableBindingFacts.ImportsEnforceSemanticSlotsAndRetainResolvedTargets") {
  auto fact = require(importFact(ImportFactMutation::None));
  ZC_EXPECT(fact == fact.clone() && fact.nameSpace() == Namespace::Value &&
            fact.origin() == BindingOrigin::ImportAlias &&
            fact.visibility() == MemberVisibility::Public && !fact.exported());
  ZC_EXPECT(fact != require(importFact(ImportFactMutation::Query)));
  ZC_EXPECT(fact != require(importFact(ImportFactMutation::Scope)));
  ZC_EXPECT(fact != require(importFact(ImportFactMutation::Target)));
  ZC_EXPECT(fact != require(importFact(ImportFactMutation::CanonicalTarget)));
  auto typeImport = require(importFact(ImportFactMutation::TypeNamespace));
  ZC_EXPECT(fact != typeImport && typeImport.nameSpace() == Namespace::Type);
  ZC_EXPECT(fact != require(importFact(ImportFactMutation::Prelude)));
  ZC_EXPECT(fact != require(importFact(ImportFactMutation::NoVisibility)));
  ZC_EXPECT(fact != require(importFact(ImportFactMutation::PrivateVisibility)));
  auto reexport = require(importFact(ImportFactMutation::Reexport));
  ZC_EXPECT(fact != reexport && reexport.exported() &&
            reexport.origin() == BindingOrigin::ReexportAlias);
  ZC_EXPECT(importFact(ImportFactMutation::NamespaceMismatch) == zc::none);
  ZC_EXPECT(importFact(ImportFactMutation::ScopeModule) == zc::none);
  ZC_EXPECT(importFact(ImportFactMutation::Origin) == zc::none);
  ZC_EXPECT(importFact(ImportFactMutation::Visibility) == zc::none);
  ZC_EXPECT(importFact(ImportFactMutation::ImportedExported) == zc::none);
  ZC_EXPECT(importFact(ImportFactMutation::ReexportNotExported) == zc::none);
}

ZC_TEST("StableBindingFacts.ModuleAliasesEnforceRequesterAndModuleNamespace") {
  auto fact = require(moduleAliasFact(ModuleAliasMutation::None));
  ZC_EXPECT(fact == fact.clone() &&
            fact.declaringScope() == StableScopeOwnerKey::module(module("owner"_zc)) &&
            fact.alias().module().encode().asPtr() == module("owner"_zc).encode().asPtr() &&
            fact.canonicalModule().encode().asPtr() == module("target"_zc).encode().asPtr());
  ZC_EXPECT(fact != require(moduleAliasFact(ModuleAliasMutation::Query)));
  ZC_EXPECT(fact != require(moduleAliasFact(ModuleAliasMutation::Scope)));
  ZC_EXPECT(fact != require(moduleAliasFact(ModuleAliasMutation::Alias)));
  ZC_EXPECT(fact != require(moduleAliasFact(ModuleAliasMutation::CanonicalModule)));
  ZC_EXPECT(fact != require(moduleAliasFact(ModuleAliasMutation::Revision)));
  ZC_EXPECT(moduleAliasFact(ModuleAliasMutation::Namespace) == zc::none);
  ZC_EXPECT(moduleAliasFact(ModuleAliasMutation::ScopeModule) == zc::none);
  ZC_EXPECT(moduleAliasFact(ModuleAliasMutation::AliasModule) == zc::none);
}

ZC_TEST("StableBindingFacts.ReexportStepsRetainCompleteProvenance") {
  auto step = reexportStep(ReexportStepMutation::None);
  ZC_EXPECT(step == step.clone() &&
            step.module().encode().asPtr() == module("owner"_zc).encode().asPtr() &&
            step.exportPath() == localPath() &&
            step.binding() == StableBindingTargetKey::module(module("binding"_zc)) &&
            step.canonicalTarget() ==
                StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
                    module("target"_zc), digestKey<identity::DefinitionKey>(0x11))));
  ZC_EXPECT(step != reexportStep(ReexportStepMutation::Module));
  ZC_EXPECT(step != reexportStep(ReexportStepMutation::ExportPath));
  ZC_EXPECT(step != reexportStep(ReexportStepMutation::Binding));
  ZC_EXPECT(step != reexportStep(ReexportStepMutation::CanonicalTarget));
}

ZC_TEST("StableBindingFacts.LocalExportsRetainExactSurfaceFacts") {
  auto fact = require(localExportFact(LocalExportMutation::None));
  ZC_EXPECT(fact == fact.clone() &&
            fact.declaringModule().encode().asPtr() == module("owner"_zc).encode().asPtr() &&
            fact.exportPath() == localPath() && fact.name().nameSpace() == Namespace::Value &&
            fact.name().name().text() == "name"_zc &&
            fact.binding() == StableBindingTargetKey::module(module("binding"_zc)) &&
            fact.canonicalTarget() ==
                StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
                    module("target"_zc), digestKey<identity::DefinitionKey>(0x11))) &&
            fact.visibility() == MemberVisibility::Public &&
            fact.reexportChain().values().size() == 0);
  ZC_EXPECT(fact != require(localExportFact(LocalExportMutation::DeclaringModule)));
  ZC_EXPECT(fact != require(localExportFact(LocalExportMutation::ExportPath)));
  ZC_EXPECT(fact != require(localExportFact(LocalExportMutation::Namespace)));
  ZC_EXPECT(fact != require(localExportFact(LocalExportMutation::Name)));
  ZC_EXPECT(fact != require(localExportFact(LocalExportMutation::Binding)));
  ZC_EXPECT(fact != require(localExportFact(LocalExportMutation::CanonicalTarget)));
  ZC_EXPECT(fact != require(localExportFact(LocalExportMutation::NoVisibility)));
  ZC_EXPECT(fact != require(localExportFact(LocalExportMutation::PrivateVisibility)));
  ZC_EXPECT(fact != require(localExportFact(LocalExportMutation::ReexportChain)));
  ZC_EXPECT(localExportFact(LocalExportMutation::Visibility) == zc::none);
}

ZC_TEST("StableBindingFacts.FailedLookupOutcomesAreClosedAndRequireAmbiguity") {
  auto missing = StableFailedLookupOutcome::missing();
  auto namespaceMismatch = namespaceMismatchLookupOutcome();
  auto ambiguous = ambiguousLookupOutcome();
  ZC_EXPECT(missing == missing.clone() && missing.value().is<StableMissingLookupOutcome>());
  ZC_EXPECT(namespaceMismatch == namespaceMismatch.clone() &&
            namespaceMismatch.value().is<StableNamespaceMismatchLookupOutcome>() &&
            namespaceMismatch.value()
                    .get<StableNamespaceMismatchLookupOutcome>()
                    .availableNamespaces.values()[0] == Namespace::Type);
  ZC_EXPECT(ambiguous == ambiguous.clone() &&
            ambiguous.value().is<StableAmbiguousLookupOutcome>() &&
            ambiguous.value().get<StableAmbiguousLookupOutcome>().candidates.values().size() == 2);
  ZC_EXPECT(missing != namespaceMismatch && missing != ambiguous && namespaceMismatch != ambiguous);

  zc::Vector<StableBindingTargetKey> oneCandidate;
  oneCandidate.add(StableBindingTargetKey::module(module("only"_zc)));
  auto admitted = require(
      StableBindingSequenceBuilder<StableBindingTargetKey>::fromNonEmpty(zc::mv(oneCandidate)));
  ZC_EXPECT(StableFailedLookupOutcome::ambiguous(zc::mv(admitted)) == zc::none);
}

ZC_TEST("StableBindingFacts.FailedLookupsRetainExactUseAndOutcome") {
  auto fact = require(failedLookupFact(FailedLookupMutation::None));
  ZC_EXPECT(fact == fact.clone() && fact.owner() == BinderQueryOwner::module(module("owner"_zc)) &&
            fact.usePath() == localPath() && fact.nameSpace() == Namespace::Value &&
            fact.name().text() == "name"_zc &&
            fact.outcome().value().is<StableMissingLookupOutcome>());
  ZC_EXPECT(fact != require(failedLookupFact(FailedLookupMutation::Owner)));
  ZC_EXPECT(fact != require(failedLookupFact(FailedLookupMutation::Path)));
  ZC_EXPECT(fact != require(failedLookupFact(FailedLookupMutation::Namespace)));
  ZC_EXPECT(fact != require(failedLookupFact(FailedLookupMutation::Name)));
  ZC_EXPECT(fact != require(failedLookupFact(FailedLookupMutation::Outcome)));
  ZC_EXPECT(fact != require(failedLookupFact(FailedLookupMutation::NamespaceMismatch)));
  ZC_EXPECT(StableFailedLookupFact::from(BinderQueryOwner::module(module("owner"_zc)), localPath(),
                                         Namespace::Value, declaredName("name"_zc),
                                         namespaceMismatchLookupOutcome(Namespace::Value)) ==
            zc::none);
  ZC_EXPECT(failedLookupFact(FailedLookupMutation::InvalidNamespace) == zc::none);
}

ZC_TEST("StableBindingFacts.BindingTargetsRetainRoutesAndRejectForeignBodyOwners") {
  auto definition = StableBindingTargetKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11)));
  auto implementation = StableBindingTargetKey::implementation(
      StableImplementationQueryKey::from(module("owner"_zc), digestKey<identity::ImplKey>(0x22)));
  auto moduleTarget = StableBindingTargetKey::module(module("owner"_zc));
  auto importTarget = StableBindingTargetKey::semanticImport(
      require(StableSemanticImportQueryKey::from(module("owner"_zc), importBinding("owner"_zc))));
  auto body = ownerBody();
  auto local = require(OwnerLocalBindingKey::from(
      body.owner().clone(), localPath(), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("value"_zc)));
  auto ownerLocal = require(StableBindingTargetKey::ownerLocal(zc::mv(body), zc::mv(local)));
  auto anonymousBody = ownerBody();
  auto anonymous = require(AnonymousOwnerLocalKey::from(anonymousBody.owner().clone(), localPath(),
                                                        AnonymousOwnerLocalRole::Closure));
  auto anonymousOwner =
      require(StableBindingTargetKey::anonymousOwner(zc::mv(anonymousBody), zc::mv(anonymous)));
  auto generic = StableBindingTargetKey::genericParameter(StableGenericParameterQueryKey::from(
      module("owner"_zc), digestKey<identity::GenericParameterKey>(0x33)));
  auto callable = StableBindingTargetKey::callableParameter(StableCallableParameterQueryKey::from(
      module("owner"_zc), digestKey<identity::CallableParameterKey>(0x44)));
  ZC_EXPECT(definition != implementation);
  ZC_EXPECT(
      definition.value().is<StableDefinitionBindingTarget>() && definition == definition.clone() &&
      implementation.value().is<StableImplementationBindingTarget>() &&
      implementation == implementation.clone() &&
      moduleTarget.value().is<StableModuleBindingTarget>() &&
      moduleTarget == moduleTarget.clone() &&
      importTarget.value().is<StableSemanticImportBindingTarget>() &&
      importTarget == importTarget.clone() &&
      ownerLocal.value().is<StableOwnerLocalBindingTarget>() && ownerLocal == ownerLocal.clone() &&
      anonymousOwner.value().is<StableAnonymousOwnerBindingTarget>() &&
      anonymousOwner == anonymousOwner.clone() &&
      generic.value().is<StableGenericParameterBindingTarget>() && generic == generic.clone() &&
      callable.value().is<StableCallableParameterBindingTarget>() && callable == callable.clone());
  expectSumWires(definition, implementation, moduleTarget, importTarget, ownerLocal, anonymousOwner,
                 generic, callable);
  auto foreignOwner = ownerBody();
  auto foreignLocal = require(OwnerLocalBindingKey::from(
      StableBodyOwnerKey::module(module("foreign"_zc)), localPath(),
      OwnerLocalBindingNamespace::Value, OwnerLocalBindingKind::Local, declaredName("value"_zc)));
  auto retainedOwner = foreignOwner.clone();
  auto retainedLocal = foreignLocal.clone();
  ZC_EXPECT(StableBindingTargetKey::ownerLocal(zc::mv(foreignOwner), zc::mv(foreignLocal)) ==
            zc::none);
  ZC_EXPECT(foreignOwner == retainedOwner);
  ZC_EXPECT(foreignLocal == retainedLocal);
  ZC_EXPECT(StableBindingCodec<StableBindingTargetKey>::decode(
                ownerLocalTargetWire(foreignOwner, foreignLocal).asPtr()) == zc::none);

  auto foreignAnonymousOwner = ownerBody();
  auto foreignAnonymous =
      require(AnonymousOwnerLocalKey::from(StableBodyOwnerKey::module(module("foreign"_zc)),
                                           localPath(), AnonymousOwnerLocalRole::Closure));
  auto retainedAnonymousOwner = foreignAnonymousOwner.clone();
  auto retainedAnonymous = foreignAnonymous.clone();
  ZC_EXPECT(StableBindingTargetKey::anonymousOwner(zc::mv(foreignAnonymousOwner),
                                                   zc::mv(foreignAnonymous)) == zc::none);
  ZC_EXPECT(foreignAnonymousOwner == retainedAnonymousOwner);
  ZC_EXPECT(foreignAnonymous == retainedAnonymous);
  ZC_EXPECT(StableBindingCodec<StableBindingTargetKey>::decode(
                anonymousTargetWire(foreignAnonymousOwner, foreignAnonymous).asPtr()) == zc::none);
}

ZC_TEST("StableBindingFacts.RoutedKeysRejectForeignOwners") {
  auto accepted = StableOwnerBodyQueryKey::from(module("owner"_zc),
                                                StableBodyOwnerKey::module(module("owner"_zc)));
  ZC_EXPECT(accepted != zc::none);
  auto rejected = StableOwnerBodyQueryKey::from(module("owner"_zc),
                                                StableBodyOwnerKey::module(module("foreign"_zc)));
  ZC_EXPECT(rejected == zc::none);
  ZC_EXPECT(StableImplementationOccurrenceQueryKey::from(module("owner"_zc),
                                                         occurrence("owner"_zc)) != zc::none);
  ZC_EXPECT(StableImplementationOccurrenceQueryKey::from(module("foreign"_zc),
                                                         occurrence("owner"_zc)) == zc::none);
  ZC_EXPECT(StableSemanticImportQueryKey::from(module("owner"_zc), importBinding("owner"_zc)) !=
            zc::none);
  ZC_EXPECT(StableSemanticImportQueryKey::from(module("foreign"_zc), importBinding("owner"_zc)) ==
            zc::none);
}

ZC_TEST("StableBindingFacts.CanonicalEmptySequenceClonesSemantically") {
  auto empty = CanonicalSequence<uint32_t>::empty();
  auto clone = empty.clone();
  ZC_EXPECT(empty.values().size() == 0);
  ZC_EXPECT(empty == clone);
}

ZC_TEST("StableBindingFacts.HeaderEnumsRejectUnknownValues") {
  ZC_EXPECT(static_cast<uint8_t>(DefinitionBodyDisposition::NoExecutableBody) == 0x01);
  ZC_EXPECT(static_cast<uint8_t>(DefinitionBodyDisposition::ExecutableBody) == 0x02);
  ZC_EXPECT(isStableBindingValue(DefinitionBodyDisposition::NoExecutableBody));
  ZC_EXPECT(isStableBindingValue(DefinitionBodyDisposition::ExecutableBody));
  ZC_EXPECT(!isStableBindingValue(static_cast<DefinitionBodyDisposition>(0xff)));
  ZC_EXPECT(static_cast<uint8_t>(ImplementationSourceForm::Ordinary) == 0x01);
  ZC_EXPECT(static_cast<uint8_t>(ImplementationSourceForm::BodylessMarker) == 0x02);
  ZC_EXPECT(isStableBindingValue(ImplementationSourceForm::Ordinary));
  ZC_EXPECT(isStableBindingValue(ImplementationSourceForm::BodylessMarker));
  ZC_EXPECT(!isStableBindingValue(static_cast<ImplementationSourceForm>(0xff)));
  ZC_EXPECT(static_cast<uint8_t>(ScopeRole::Declaration) == 0x01);
  ZC_EXPECT(static_cast<uint8_t>(ScopeRole::Generic) == 0x02);
  ZC_EXPECT(static_cast<uint8_t>(ScopeRole::Parameters) == 0x03);
  ZC_EXPECT(static_cast<uint8_t>(ScopeRole::Members) == 0x04);
  ZC_EXPECT(static_cast<uint8_t>(ScopeRole::Implementation) == 0x05);
  ZC_EXPECT(isStableBindingValue(ScopeRole::Declaration));
  ZC_EXPECT(isStableBindingValue(ScopeRole::Generic));
  ZC_EXPECT(isStableBindingValue(ScopeRole::Parameters));
  ZC_EXPECT(isStableBindingValue(ScopeRole::Members));
  ZC_EXPECT(isStableBindingValue(ScopeRole::Implementation));
  ZC_EXPECT(!isStableBindingValue(static_cast<ScopeRole>(0xff)));
}

ZC_TEST("StableBindingFacts.HeaderSitesCloneAndDistinguishVariants") {
  auto definition = StableHeaderSite::definition(headerSite("owner"_zc));
  auto implementation = StableHeaderSite::implementation(occurrence("owner"_zc));
  ZC_EXPECT(definition == definition.clone());
  ZC_EXPECT(implementation == implementation.clone());
  ZC_EXPECT(definition != implementation);
  ZC_EXPECT(definition.value().is<DefinitionAuthoritySite>());
  ZC_EXPECT(implementation.value().is<ImplementationOccurrenceSite>());
}

ZC_TEST("StableBindingFacts.GenericParametersEnforceKeyOwnerSiteAndOrdinal") {
  auto definition = identity::DefinitionKey::compute(definitionRecord("owner"_zc));
  auto record = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::definition(definition.clone()), 0);
  auto accepted = StableHeaderGenericParameter::from(
      identity::GenericParameterKey::compute(record), record.clone(),
      StableHeaderSite::definition(headerSite("owner"_zc)), declaredName("T"_zc), 0);
  ZC_REQUIRE(accepted != zc::none);
  auto clone = ZC_ASSERT_NONNULL(accepted).clone();
  ZC_EXPECT(clone == ZC_ASSERT_NONNULL(accepted));
  ZC_EXPECT(clone.key() == identity::GenericParameterKey::compute(record));
  ZC_EXPECT(clone.ordinal() == 0);

  ZC_EXPECT(StableHeaderGenericParameter::from(digestKey<identity::GenericParameterKey>(0x77),
                                               record.clone(),
                                               StableHeaderSite::definition(headerSite("owner"_zc)),
                                               declaredName("T"_zc), 0) == zc::none);
  ZC_EXPECT(StableHeaderGenericParameter::from(identity::GenericParameterKey::compute(record),
                                               record.clone(),
                                               StableHeaderSite::definition(headerSite("owner"_zc)),
                                               declaredName("T"_zc), 1) == zc::none);
  ZC_EXPECT(StableHeaderGenericParameter::from(
                identity::GenericParameterKey::compute(record), record.clone(),
                StableHeaderSite::implementation(occurrence("owner"_zc)), declaredName("T"_zc),
                0) == zc::none);

  auto implementation = digestKey<identity::ImplKey>(0x33);
  auto implementationRecord = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::implementation(implementation.clone()), 1);
  auto matchingSite = ImplSourceOccurrenceKey::from(implementation.clone(), headerSite("owner"_zc));
  ZC_EXPECT(StableHeaderGenericParameter::from(
                identity::GenericParameterKey::compute(implementationRecord),
                implementationRecord.clone(),
                StableHeaderSite::implementation(zc::mv(matchingSite)), declaredName("U"_zc),
                1) != zc::none);
  auto foreignSite =
      ImplSourceOccurrenceKey::from(digestKey<identity::ImplKey>(0x44), headerSite("owner"_zc));
  ZC_EXPECT(StableHeaderGenericParameter::from(
                identity::GenericParameterKey::compute(implementationRecord),
                implementationRecord.clone(), StableHeaderSite::implementation(zc::mv(foreignSite)),
                declaredName("U"_zc), 1) == zc::none);

  auto renamed = StableHeaderGenericParameter::from(
      identity::GenericParameterKey::compute(record), record.clone(),
      StableHeaderSite::definition(headerSite("owner"_zc)), declaredName("Renamed"_zc), 0);
  ZC_REQUIRE(renamed != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(accepted) != ZC_ASSERT_NONNULL(renamed));
}

ZC_TEST("StableBindingFacts.CallableParametersEnforceKeySitePositionAndName") {
  auto definition = identity::DefinitionKey::compute(definitionRecord("owner"_zc));
  auto receiverRecord = identity::CallableParameterIdentityRecord::from(
      definition.clone(), identity::CallableParameterPosition::receiver());
  zc::Maybe<identity::DeclaredDefinitionName> noName;
  auto receiver = StableHeaderCallableParameter::from(
      identity::CallableParameterKey::compute(receiverRecord), receiverRecord.clone(),
      StableHeaderSite::definition(headerSite("owner"_zc)), zc::mv(noName),
      identity::CallableParameterPosition::receiver());
  ZC_REQUIRE(receiver != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(receiver) == ZC_ASSERT_NONNULL(receiver).clone());
  ZC_EXPECT(ZC_ASSERT_NONNULL(receiver).name() == zc::none);

  auto receiverName = optionalName("self"_zc);
  ZC_EXPECT(StableHeaderCallableParameter::from(
                identity::CallableParameterKey::compute(receiverRecord), receiverRecord.clone(),
                StableHeaderSite::definition(headerSite("owner"_zc)), zc::mv(receiverName),
                identity::CallableParameterPosition::receiver()) == zc::none);

  auto ordinaryRecord = identity::CallableParameterIdentityRecord::from(
      definition.clone(), identity::CallableParameterPosition::ordinary(0));
  auto ordinaryName = optionalName("value"_zc);
  auto ordinary = StableHeaderCallableParameter::from(
      identity::CallableParameterKey::compute(ordinaryRecord), ordinaryRecord.clone(),
      StableHeaderSite::definition(headerSite("owner"_zc)), zc::mv(ordinaryName),
      identity::CallableParameterPosition::ordinary(0));
  ZC_REQUIRE(ordinary != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(ordinary).position().kind() ==
            identity::CallableParameterPositionKind::Ordinary);
  ZC_EXPECT(ZC_ASSERT_NONNULL(ordinary) != ZC_ASSERT_NONNULL(receiver));

  zc::Maybe<identity::DeclaredDefinitionName> missingName;
  ZC_EXPECT(StableHeaderCallableParameter::from(
                identity::CallableParameterKey::compute(ordinaryRecord), ordinaryRecord.clone(),
                StableHeaderSite::definition(headerSite("owner"_zc)), zc::mv(missingName),
                identity::CallableParameterPosition::ordinary(0)) == zc::none);
  auto wrongKeyName = optionalName("value"_zc);
  ZC_EXPECT(StableHeaderCallableParameter::from(
                digestKey<identity::CallableParameterKey>(0x77), ordinaryRecord.clone(),
                StableHeaderSite::definition(headerSite("owner"_zc)), zc::mv(wrongKeyName),
                identity::CallableParameterPosition::ordinary(0)) == zc::none);
  auto wrongPositionName = optionalName("value"_zc);
  ZC_EXPECT(StableHeaderCallableParameter::from(
                identity::CallableParameterKey::compute(ordinaryRecord), ordinaryRecord.clone(),
                StableHeaderSite::definition(headerSite("owner"_zc)), zc::mv(wrongPositionName),
                identity::CallableParameterPosition::ordinary(1)) == zc::none);
  auto wrongSiteName = optionalName("value"_zc);
  ZC_EXPECT(StableHeaderCallableParameter::from(
                identity::CallableParameterKey::compute(ordinaryRecord), ordinaryRecord.clone(),
                StableHeaderSite::implementation(occurrence("owner"_zc)), zc::mv(wrongSiteName),
                identity::CallableParameterPosition::ordinary(0)) == zc::none);
}

ZC_TEST("StableBindingFacts.DefinitionHeadersEnforceIdentityAndParameterRelations") {
  auto accepted = definitionHeader(DefinitionHeaderMutation::None);
  ZC_REQUIRE(accepted != zc::none);
  auto clone = ZC_ASSERT_NONNULL(accepted).clone();
  ZC_EXPECT(clone == ZC_ASSERT_NONNULL(accepted));
  ZC_EXPECT(clone.queryKey().definition() == identity::DefinitionKey::compute(clone.record()));
  ZC_EXPECT(clone.authoritySite().sameAs(headerSite("owner"_zc)));
  ZC_EXPECT(clone.kind() == identity::DefinitionKind::Function);
  ZC_EXPECT(clone.nameSpace() == Namespace::Value);
  ZC_EXPECT(clone.name().text() == "run"_zc);
  ZC_EXPECT(clone.activation() == DefinitionActivation::ModuleSkeleton);
  ZC_EXPECT(clone.visibility() == MemberVisibility::Public);
  ZC_EXPECT(clone.bodyDisposition() == DefinitionBodyDisposition::ExecutableBody);
  ZC_EXPECT(clone.genericParameters().values().size() == 1);
  ZC_EXPECT(clone.callableParameters().values().size() == 1);
  ZC_EXPECT(clone.declaredScopeRoles().values().size() == 1);
  ZC_EXPECT(definitionHeader(DefinitionHeaderMutation::WithoutVisibility) != zc::none);
  DefinitionHeaderMutation mutations[] = {
      DefinitionHeaderMutation::QueryIdentity,     DefinitionHeaderMutation::QueryOwner,
      DefinitionHeaderMutation::AuthoritySite,     DefinitionHeaderMutation::Kind,
      DefinitionHeaderMutation::Namespace,         DefinitionHeaderMutation::Name,
      DefinitionHeaderMutation::Activation,        DefinitionHeaderMutation::Visibility,
      DefinitionHeaderMutation::BodyDisposition,   DefinitionHeaderMutation::GenericOwner,
      DefinitionHeaderMutation::GenericSite,       DefinitionHeaderMutation::GenericOrdinal,
      DefinitionHeaderMutation::GenericDuplicate,  DefinitionHeaderMutation::CallableOwner,
      DefinitionHeaderMutation::CallableSite,      DefinitionHeaderMutation::CallablePosition,
      DefinitionHeaderMutation::CallableDuplicate, DefinitionHeaderMutation::ScopeRole};
  for (const auto mutation : mutations) { ZC_EXPECT(definitionHeader(mutation) == zc::none); }
}

ZC_TEST("StableBindingFacts.ImplementationHeadersEnforceOccurrenceAndGenericRelations") {
  auto accepted = implementationHeader(ImplementationHeaderMutation::None);
  ZC_REQUIRE(accepted != zc::none);
  auto clone = ZC_ASSERT_NONNULL(accepted).clone();
  ZC_EXPECT(clone == ZC_ASSERT_NONNULL(accepted));
  ZC_EXPECT(clone.queryKey().occurrence().implementation() == clone.authority().implementation());
  ZC_EXPECT(clone.authority().implementation() == identity::ImplKey::compute(clone.record()));
  ZC_EXPECT(clone.genericParameters().values().size() == 1);
  ZC_EXPECT(clone.declaredScopeRoles().values().size() == 1);
  ZC_EXPECT(clone.sourceForm() == ImplementationSourceForm::Ordinary);
  ZC_EXPECT(implementationHeader(ImplementationHeaderMutation::Bodyless) != zc::none);
  ImplementationHeaderMutation mutations[] = {ImplementationHeaderMutation::QueryImplementation,
                                              ImplementationHeaderMutation::OccurrenceSite,
                                              ImplementationHeaderMutation::AuthorityModule,
                                              ImplementationHeaderMutation::AuthorityImplementation,
                                              ImplementationHeaderMutation::Record,
                                              ImplementationHeaderMutation::GenericOwner,
                                              ImplementationHeaderMutation::GenericSite,
                                              ImplementationHeaderMutation::GenericOrdinal,
                                              ImplementationHeaderMutation::GenericDuplicate,
                                              ImplementationHeaderMutation::ScopeRole,
                                              ImplementationHeaderMutation::SourceForm};
  for (const auto mutation : mutations) { ZC_EXPECT(implementationHeader(mutation) == zc::none); }
}

template <typename T>
void expectRoundTrip(const T& value) {
  auto encoded = StableBindingCodec<T>::encode(value);
  auto decoded = StableBindingCodec<T>::decode(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decoded) == value);
}

template <typename T>
void expectClosedEnumCodec(T value) {
  uint8_t expected[] = {static_cast<uint8_t>(value)};
  ZC_EXPECT(StableBindingCodec<T>::encode(value).asPtr() == zc::arrayPtr(expected));
  expectRoundTrip(value);
  uint8_t unknown[] = {0xff};
  uint8_t trailing[] = {static_cast<uint8_t>(value), 0x00};
  ZC_EXPECT(StableBindingCodec<T>::decode(zc::arrayPtr(unknown)) == zc::none);
  ZC_EXPECT(StableBindingCodec<T>::decode(zc::arrayPtr(trailing)) == zc::none);
}

zc::Array<uint8_t> expectedWire(zc::StringPtr domain, const identity::ModuleKey& module,
                                zc::ArrayPtr<const uint8_t> field, bool framed) {
  identity::CanonicalEncoder record;
  const auto moduleBytes = module.encode();
  record.encodeByteString(moduleBytes.asPtr());
  if (framed) { record.encodeByteString(field); }
  const auto prefix = record.finish();
  zc::Vector<uint8_t> expected(domain.size() + 1 + prefix.size() + (framed ? 0 : field.size()));
  expected.addAll(domain.asBytes());
  expected.add(0x00);
  expected.addAll(prefix.asPtr());
  if (!framed) { expected.addAll(field); }
  return expected.releaseAsArray();
}

zc::Array<uint8_t> expectedDomainRecord(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> record) {
  zc::Vector<uint8_t> expected(domain.size() + 1 + record.size());
  expected.addAll(domain.asBytes());
  expected.add(0x00);
  expected.addAll(record);
  return expected.releaseAsArray();
}

void encodeOracleFrame(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const uint8_t> bytes) {
  encoder.encodeByteString(bytes);
}
template <typename T>
void encodeStableOracleFrame(identity::CanonicalEncoder& encoder, const T& value) {
  encodeOracleFrame(encoder, StableBindingCodec<T>::encode(value).asPtr());
}
zc::Array<uint8_t> scopeOwnerWire(const StableScopeOwnerKey& value) {
  identity::CanonicalEncoder record;
  if (value.value().is<StableModuleScope>()) {
    record.encodeUint8(0x01);
    encodeOracleFrame(record, value.value().get<StableModuleScope>().module.encode().asPtr());
  } else if (value.value().is<StableDefinitionScope>()) {
    const auto& scope = value.value().get<StableDefinitionScope>();
    record.encodeUint8(0x02);
    encodeStableOracleFrame(record, scope.definition);
    record.encodeUint8(static_cast<uint8_t>(scope.role));
  } else if (value.value().is<StableImplementationOccurrenceScope>()) {
    const auto& scope = value.value().get<StableImplementationOccurrenceScope>();
    record.encodeUint8(0x03);
    encodeStableOracleFrame(record, scope.occurrence);
    record.encodeUint8(static_cast<uint8_t>(scope.role));
  } else {
    const auto& scope = value.value().get<StableBodyScope>();
    record.encodeUint8(0x04);
    encodeStableOracleFrame(record, scope.owner);
    encodeOracleFrame(record, scope.path.encode().asPtr());
  }
  return expectedDomainRecord("zom.binder.scope-owner-key"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> syntaxRootWire(const StableNodeSyntaxRoot& value) {
  identity::CanonicalEncoder record;
  if (value.value().is<StableModuleBodySyntaxRoot>()) {
    record.encodeUint8(0x01);
    encodeOracleFrame(record,
                      value.value().get<StableModuleBodySyntaxRoot>().module.encode().asPtr());
  } else if (value.value().is<StableDefinitionHeaderSyntaxRoot>()) {
    record.encodeUint8(0x02);
    encodeStableOracleFrame(record,
                            value.value().get<StableDefinitionHeaderSyntaxRoot>().definition);
  } else {
    record.encodeUint8(0x03);
    encodeStableOracleFrame(record,
                            value.value().get<StableImplementationHeaderSyntaxRoot>().occurrence);
  }
  return expectedDomainRecord("zom.binder.node-syntax-root"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> scopeFactWire(const StableScopeFact& value) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, value.owner());
  ZC_IF_SOME(parent, value.parent()) {
    record.encodeUint8(0x01);
    encodeStableOracleFrame(record, parent);
  } else {
    record.encodeUint8(0x00);
  }
  record.encodeUint8(static_cast<uint8_t>(value.kind()));
  return expectedDomainRecord("zom.binder.skeleton-scope"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> nodeScopeFactWire(const StableNodeSyntaxRoot& root,
                                     const LocalSyntaxPath& nodePath,
                                     const StableScopeOwnerKey& scope) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, root);
  encodeOracleFrame(record, nodePath.encode().asPtr());
  encodeStableOracleFrame(record, scope);
  return expectedDomainRecord("zom.binder.skeleton-node-scope"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> bodyScopeFactWire(const StableOwnerBodyQueryKey& owner,
                                     const StableScopeOwnerKey& scope,
                                     const StableScopeOwnerKey& parent, ScopeKind kind) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, owner);
  encodeStableOracleFrame(record, scope);
  encodeStableOracleFrame(record, parent);
  record.encodeUint8(static_cast<uint8_t>(kind));
  return expectedDomainRecord("zom.binder.body-scope"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> bodyNodeScopeFactWire(const StableOwnerBodyQueryKey& owner,
                                         const LocalSyntaxPath& nodePath,
                                         const StableScopeOwnerKey& scope) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, owner);
  encodeOracleFrame(record, nodePath.encode().asPtr());
  encodeStableOracleFrame(record, scope);
  return expectedDomainRecord("zom.binder.body-node-scope"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> ownerLocalBindingFactWire(
    const StableOwnerBodyQueryKey& owner, const OwnerLocalBindingKey& key,
    OwnerLocalBindingKind kind, const identity::DeclaredDefinitionName& name, Namespace nameSpace,
    const StableScopeOwnerKey& declaringScope, DefinitionActivation activation) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, owner);
  encodeOracleFrame(record, key.encode().asPtr());
  record.encodeUint8(static_cast<uint8_t>(kind));
  name.encode(record);
  record.encodeUint8(static_cast<uint8_t>(nameSpace));
  encodeStableOracleFrame(record, declaringScope);
  record.encodeUint8(static_cast<uint8_t>(activation));
  return expectedDomainRecord("zom.binder.body-owner-local-binding"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> resolutionFactWire(const StableOwnerBodyQueryKey& owner,
                                      const LocalSyntaxPath& usePath, Namespace nameSpace,
                                      const StableBindingTargetKey& binding,
                                      const StableBindingTargetKey& canonicalTarget,
                                      BindingOrigin origin) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, owner);
  encodeOracleFrame(record, usePath.encode().asPtr());
  record.encodeUint8(static_cast<uint8_t>(nameSpace));
  encodeStableOracleFrame(record, binding);
  encodeStableOracleFrame(record, canonicalTarget);
  record.encodeUint8(static_cast<uint8_t>(origin));
  return expectedDomainRecord("zom.binder.body-resolution"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> deferredMemberFactWire(
    const StableOwnerBodyQueryKey& owner, const LocalSyntaxPath& usePath,
    const LocalSyntaxPath& basePath, MemberAccessKind accessKind,
    const identity::DeclaredDefinitionName& member,
    const CanonicalNonEmptySequence<Namespace>& expectedNamespaces,
    const CanonicalSequence<LocalSyntaxPath>& genericArgumentPaths) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, owner);
  encodeOracleFrame(record, usePath.encode().asPtr());
  encodeOracleFrame(record, basePath.encode().asPtr());
  record.encodeUint8(static_cast<uint8_t>(accessKind));
  member.encode(record);
  record.encodeSequenceSize(expectedNamespaces.values().size());
  for (const auto nameSpace : expectedNamespaces.values()) {
    uint8_t encoded[] = {static_cast<uint8_t>(nameSpace)};
    record.encodeByteString(zc::arrayPtr(encoded));
  }
  record.encodeSequenceSize(genericArgumentPaths.values().size());
  for (const auto& path : genericArgumentPaths.values()) {
    record.encodeByteString(path.encode().asPtr());
  }
  return expectedDomainRecord("zom.binder.body-deferred-member"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> selfOwnerWire(const StableSelfOwner& value) {
  identity::CanonicalEncoder record;
  if (value.value().is<StableNominalSelfOwner>()) {
    record.encodeUint8(0x01);
    encodeStableOracleFrame(record, value.value().get<StableNominalSelfOwner>().definition);
  } else if (value.value().is<StableInterfaceSelfOwner>()) {
    record.encodeUint8(0x02);
    encodeStableOracleFrame(record, value.value().get<StableInterfaceSelfOwner>().definition);
  } else {
    record.encodeUint8(0x03);
    encodeStableOracleFrame(
        record, value.value().get<StableImplementationOccurrenceSelfOwner>().occurrence);
  }
  return expectedDomainRecord("zom.binder.self-owner"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> selfTypeFactWire(const StableOwnerBodyQueryKey& owner,
                                    const LocalSyntaxPath& syntaxPath,
                                    const StableSelfOwner& selfOwner) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, owner);
  encodeOracleFrame(record, syntaxPath.encode().asPtr());
  encodeStableOracleFrame(record, selfOwner);
  return expectedDomainRecord("zom.binder.body-self-type"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> thisBindingFactWire(const StableOwnerBodyQueryKey& owner,
                                       const LocalSyntaxPath& expressionPath,
                                       const StableCallableParameterQueryKey& receiver) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, owner);
  encodeOracleFrame(record, expressionPath.encode().asPtr());
  encodeStableOracleFrame(record, receiver);
  return expectedDomainRecord("zom.binder.body-this-binding"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> shadowTargetFactWire(const StableOwnerBodyQueryKey& owner,
                                        const StableBindingTargetKey& binding,
                                        const StableBindingTargetKey& shadowed) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, owner);
  encodeStableOracleFrame(record, binding);
  encodeStableOracleFrame(record, shadowed);
  return expectedDomainRecord("zom.binder.body-shadow-target"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> declarationFactWire(const StableDeclarationFact& value) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, value.queryKey());
  encodeOracleFrame(record, value.record().encode().asPtr());
  encodeStableOracleFrame(record, value.declaringScope());
  record.encodeUint8(static_cast<uint8_t>(value.kind()));
  record.encodeUint8(static_cast<uint8_t>(value.nameSpace()));
  value.name().encode(record);
  record.encodeUint8(static_cast<uint8_t>(value.activation()));
  ZC_IF_SOME(visibility, value.visibility()) {
    record.encodeUint8(0x01);
    record.encodeUint8(static_cast<uint8_t>(visibility));
  } else {
    record.encodeUint8(0x00);
  }
  return expectedDomainRecord("zom.binder.skeleton-declaration"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> implementationFactWire(const StableImplementationOccurrenceFact& value) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, value.occurrence());
  encodeStableOracleFrame(record, value.authority());
  encodeOracleFrame(record, value.record().encode().asPtr());
  encodeStableOracleFrame(record, value.declaringScope());
  return expectedDomainRecord("zom.binder.skeleton-implementation-occurrence"_zc,
                              record.finish().asPtr());
}
zc::Array<uint8_t> genericDeclarationWire(const StableGenericParameterDeclarationFact& value) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, value.queryKey());
  value.record().encode(record);
  encodeHeaderSiteOracle(record, value.headerSite());
  encodeStableOracleFrame(record, value.declaringScope());
  value.name().encode(record);
  return expectedDomainRecord("zom.binder.skeleton-generic-parameter-declaration"_zc,
                              record.finish().asPtr());
}
zc::Array<uint8_t> callableDeclarationWire(const StableCallableParameterDeclarationFact& value) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, value.queryKey());
  value.record().encode(record);
  encodeHeaderSiteOracle(record, value.headerSite());
  encodeStableOracleFrame(record, value.declaringScope());
  ZC_IF_SOME(name, value.name()) {
    record.encodeUint8(0x01);
    name.encode(record);
  } else {
    record.encodeUint8(0x00);
  }
  return expectedDomainRecord("zom.binder.skeleton-callable-parameter-declaration"_zc,
                              record.finish().asPtr());
}
zc::Array<uint8_t> bindingTargetWire(const StableBindingTargetKey& value) {
  identity::CanonicalEncoder record;
#define ZOM_TARGET_ORACLE(Variant, Tag, Field)                           \
  if (value.value().is<Variant>()) {                                     \
    record.encodeUint8(Tag);                                             \
    encodeStableOracleFrame(record, value.value().get<Variant>().Field); \
  } else
  ZOM_TARGET_ORACLE(StableDefinitionBindingTarget, 0x01, definition)
  ZOM_TARGET_ORACLE(StableImplementationBindingTarget, 0x02, implementation)
  if (value.value().is<StableModuleBindingTarget>()) {
    record.encodeUint8(0x03);
    encodeOracleFrame(record,
                      value.value().get<StableModuleBindingTarget>().module.encode().asPtr());
  } else
    ZOM_TARGET_ORACLE(StableSemanticImportBindingTarget, 0x04, import)
  if (value.value().is<StableOwnerLocalBindingTarget>()) {
    const auto& target = value.value().get<StableOwnerLocalBindingTarget>();
    record.encodeUint8(0x05);
    encodeStableOracleFrame(record, target.owner);
    encodeOracleFrame(record, target.binding.encode().asPtr());
  } else if (value.value().is<StableAnonymousOwnerBindingTarget>()) {
    const auto& target = value.value().get<StableAnonymousOwnerBindingTarget>();
    record.encodeUint8(0x06);
    encodeStableOracleFrame(record, target.owner);
    encodeOracleFrame(record, target.binding.encode().asPtr());
  } else
    ZOM_TARGET_ORACLE(StableGenericParameterBindingTarget, 0x07, parameter) {
      record.encodeUint8(0x08);
      encodeStableOracleFrame(record,
                              value.value().get<StableCallableParameterBindingTarget>().parameter);
    }
#undef ZOM_TARGET_ORACLE
  return expectedDomainRecord("zom.binder.binding-target-key"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> labelKeyWire(const StableLabelKey& value) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, value.owner());
  encodeOracleFrame(record, value.declarationPath().encode().asPtr());
  return expectedDomainRecord("zom.binder.label-key"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> labelTargetWire(const StableLabelTarget& value) {
  identity::CanonicalEncoder record;
  record.encodeUint8(value.value().is<StableBlockLabelTarget>() ? 0x01 : 0x02);
  encodeStableOracleFrame(record, value.scope());
  return expectedDomainRecord("zom.binder.label-target"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> labelFactWire(const StableLabelKey& key,
                                 const identity::DeclaredDefinitionName& name,
                                 const LocalSyntaxPath& statementPath,
                                 const StableLabelTarget& target) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, key);
  name.encode(record);
  encodeOracleFrame(record, statementPath.encode().asPtr());
  encodeStableOracleFrame(record, target);
  return expectedDomainRecord("zom.binder.body-label"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> controlTargetWire(const StableControlTarget& value) {
  identity::CanonicalEncoder record;
  if (value.value().is<StableExplicitLabelControlTarget>()) {
    record.encodeUint8(0x01);
    encodeStableOracleFrame(record, value.value().get<StableExplicitLabelControlTarget>().label);
  } else if (value.value().is<StableLoopControlTarget>()) {
    record.encodeUint8(0x02);
    encodeStableOracleFrame(record, value.value().get<StableLoopControlTarget>().scope);
  } else {
    record.encodeUint8(0x03);
    encodeStableOracleFrame(record, value.value().get<StableMatchControlTarget>().scope);
  }
  return expectedDomainRecord("zom.binder.control-target"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> controlTransferFactWire(const StableOwnerBodyQueryKey& owner,
                                           const LocalSyntaxPath& transferPath,
                                           ControlTransferKind kind,
                                           const StableControlTarget& target) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, owner);
  encodeOracleFrame(record, transferPath.encode().asPtr());
  record.encodeUint8(static_cast<uint8_t>(kind));
  encodeStableOracleFrame(record, target);
  return expectedDomainRecord("zom.binder.body-control-transfer"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> importFactWire(const StableImportFact& value) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, value.queryKey());
  encodeStableOracleFrame(record, value.declaringScope());
  encodeStableOracleFrame(record, value.target());
  encodeStableOracleFrame(record, value.canonicalTarget());
  record.encodeUint8(static_cast<uint8_t>(value.nameSpace()));
  record.encodeUint8(static_cast<uint8_t>(value.origin()));
  ZC_IF_SOME(visibility, value.visibility()) {
    record.encodeUint8(0x01);
    record.encodeUint8(static_cast<uint8_t>(visibility));
  } else {
    record.encodeUint8(0x00);
  }
  record.encodeUint8(value.exported() ? 0x01 : 0x00);
  return expectedDomainRecord("zom.binder.skeleton-import"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> moduleAliasFactWire(const StableModuleAliasFact& value) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, value.queryKey());
  encodeStableOracleFrame(record, value.declaringScope());
  encodeStableOracleFrame(record, value.alias());
  encodeOracleFrame(record, value.canonicalModule().encode().asPtr());
  record.encodeDigest(value.targetExportNamesRevision().digest());
  return expectedDomainRecord("zom.binder.skeleton-module-alias"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> reexportStepWire(const StableReexportStep& value) {
  identity::CanonicalEncoder record;
  encodeOracleFrame(record, value.module().encode().asPtr());
  encodeOracleFrame(record, value.exportPath().encode().asPtr());
  encodeStableOracleFrame(record, value.binding());
  encodeStableOracleFrame(record, value.canonicalTarget());
  return expectedDomainRecord("zom.binder.skeleton-reexport-step"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> failedLookupOutcomeWire(const StableFailedLookupOutcome& value) {
  identity::CanonicalEncoder record;
  if (value.value().is<StableMissingLookupOutcome>()) {
    record.encodeUint8(0x01);
  } else if (value.value().is<StableNamespaceMismatchLookupOutcome>()) {
    record.encodeUint8(0x02);
    const auto& namespaces =
        value.value().get<StableNamespaceMismatchLookupOutcome>().availableNamespaces;
    record.encodeSequenceSize(namespaces.values().size());
    for (const auto nameSpace : namespaces.values()) {
      uint8_t encoded[] = {static_cast<uint8_t>(nameSpace)};
      record.encodeByteString(zc::arrayPtr(encoded));
    }
  } else {
    record.encodeUint8(0x03);
    const auto& candidates = value.value().get<StableAmbiguousLookupOutcome>().candidates;
    record.encodeSequenceSize(candidates.values().size());
    for (const auto& candidate : candidates.values()) {
      record.encodeByteString(bindingTargetWire(candidate).asPtr());
    }
  }
  return expectedDomainRecord("zom.binder.failed-lookup-outcome"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> queryOwnerWire(const BinderQueryOwner& value);
zc::Array<uint8_t> failedLookupFactWire(const StableFailedLookupFact& value) {
  identity::CanonicalEncoder record;
  encodeOracleFrame(record, queryOwnerWire(value.owner()).asPtr());
  encodeOracleFrame(record, value.usePath().encode().asPtr());
  record.encodeUint8(static_cast<uint8_t>(value.nameSpace()));
  value.name().encode(record);
  encodeOracleFrame(record, failedLookupOutcomeWire(value.outcome()).asPtr());
  return expectedDomainRecord("zom.binder.failed-lookup"_zc, record.finish().asPtr());
}
template <typename T>
void expectByteMutation(zc::ArrayPtr<const uint8_t> encoded, size_t offset);
template <typename T>
void expectSumWireImpl(const T& value, zc::StringPtr domain,
                       zc::Array<uint8_t> (*oracle)(const T&)) {
  const auto encoded = StableBindingCodec<T>::encode(value);
  ZC_EXPECT(encoded.asPtr() == oracle(value).asPtr());
  expectRoundTrip(value);
  auto wrongDomain = zc::heapArray(encoded.asPtr());
  wrongDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<T>::decode(wrongDomain.asPtr()) == zc::none);
  expectByteMutation<T>(encoded.asPtr(), domain.size() + 1);
  expectByteMutation<T>(encoded.asPtr(), domain.size() + 1 + 1 + sizeof(uint64_t));
  ZC_EXPECT(StableBindingCodec<T>::decode(encoded.slice(0, encoded.size() - 1)) == zc::none);
  zc::Vector<uint8_t> trailing(encoded.size() + 1);
  trailing.addAll(encoded.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<T>::decode(trailing.asPtr()) == zc::none);
  uint8_t backing = 0;
  ZC_EXPECT(StableBindingCodec<T>::decode(zc::ArrayPtr<const uint8_t>(&backing, 134217729)) ==
            zc::none);
}
void expectSumWire(const StableScopeOwnerKey& value) {
  expectSumWireImpl(value, "zom.binder.scope-owner-key"_zc, &scopeOwnerWire);
}
void expectSumWire(const StableNodeSyntaxRoot& value) {
  expectSumWireImpl(value, "zom.binder.node-syntax-root"_zc, &syntaxRootWire);
}
void expectSumWire(const StableBindingTargetKey& value) {
  expectSumWireImpl(value, "zom.binder.binding-target-key"_zc, &bindingTargetWire);
}
void expectSumWire(const StableSelfOwner& value) {
  expectSumWireImpl(value, "zom.binder.self-owner"_zc, &selfOwnerWire);
}
void expectSumWire(const StableLabelTarget& value) {
  expectSumWireImpl(value, "zom.binder.label-target"_zc, &labelTargetWire);
}
void expectSumWire(const StableControlTarget& value) {
  expectSumWireImpl(value, "zom.binder.control-target"_zc, &controlTargetWire);
}
template <typename Binding>
zc::Array<uint8_t> bodyTargetWire(uint8_t tag, const StableOwnerBodyQueryKey& owner,
                                  const Binding& binding) {
  identity::CanonicalEncoder record;
  record.encodeUint8(tag);
  encodeStableOracleFrame(record, owner);
  encodeOracleFrame(record, binding.encode().asPtr());
  return expectedDomainRecord("zom.binder.binding-target-key"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> ownerLocalTargetWire(const StableOwnerBodyQueryKey& owner,
                                        const OwnerLocalBindingKey& binding) {
  return bodyTargetWire(0x05, owner, binding);
}
zc::Array<uint8_t> anonymousTargetWire(const StableOwnerBodyQueryKey& owner,
                                       const AnonymousOwnerLocalKey& binding) {
  return bodyTargetWire(0x06, owner, binding);
}
void encodeHeaderSiteOracle(identity::CanonicalEncoder& encoder, const StableHeaderSite& site) {
  if (site.value().is<DefinitionAuthoritySite>()) {
    encoder.encodeUint8(0x01);
    site.value().get<DefinitionAuthoritySite>().site.encode(encoder);
  } else {
    encoder.encodeUint8(0x02);
    site.value().get<ImplementationOccurrenceSite>().site.encode(encoder);
  }
}

template <typename T>
void encodeSequenceOracle(identity::CanonicalEncoder& encoder, const CanonicalSequence<T>& values) {
  encoder.encodeSequenceSize(values.values().size());
  for (const auto& value : values.values()) {
    const auto bytes = StableBindingCodec<T>::encode(value);
    encoder.encodeByteString(bytes.asPtr());
  }
}

zc::Array<uint8_t> closureFactWire(const StableClosureFact& value) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, value.owner());
  encodeOracleFrame(record, value.closure().encode().asPtr());
  encodeStableOracleFrame(record, value.scope());
  return expectedDomainRecord("zom.binder.body-closure"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> closureFreeVariableWire(const StableBindingTargetKey& target,
                                           zc::ArrayPtr<const LocalSyntaxPath> referencePaths) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, target);
  record.encodeSequenceSize(referencePaths.size());
  for (const auto& path : referencePaths) { encodeOracleFrame(record, path.encode().asPtr()); }
  return expectedDomainRecord("zom.binder.closure-free-variable"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> closureFreeVariableWire(const StableClosureFreeVariable& value) {
  return closureFreeVariableWire(value.target(), value.referencePaths().values());
}
zc::Array<uint8_t> closureFreeVariableFactWire(
    const StableOwnerBodyQueryKey& owner, const AnonymousOwnerLocalKey& closure,
    zc::ArrayPtr<const StableClosureFreeVariable> variables) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, owner);
  encodeOracleFrame(record, closure.encode().asPtr());
  record.encodeSequenceSize(variables.size());
  for (const auto& variable : variables) { encodeStableOracleFrame(record, variable); }
  return expectedDomainRecord("zom.binder.body-closure-free-variables"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> closureFreeVariableFactWire(const StableClosureFreeVariableFact& value) {
  return closureFreeVariableFactWire(value.owner(), value.closure(), value.variables().values());
}
zc::Array<uint8_t> explicitCaptureBindingWire(const LocalSyntaxPath& itemPath,
                                              const StableBindingTargetKey& target,
                                              StableExplicitCaptureMode mode) {
  identity::CanonicalEncoder record;
  encodeOracleFrame(record, itemPath.encode().asPtr());
  encodeStableOracleFrame(record, target);
  record.encodeUint8(static_cast<uint8_t>(mode));
  return expectedDomainRecord("zom.binder.explicit-capture-binding"_zc, record.finish().asPtr());
}
zc::Array<uint8_t> explicitClosureCaptureFactWire(
    const StableOwnerBodyQueryKey& owner, const AnonymousOwnerLocalKey& closure,
    const LocalSyntaxPath& captureListPath,
    zc::ArrayPtr<const StableExplicitCaptureBindingFact> captures) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, owner);
  encodeOracleFrame(record, closure.encode().asPtr());
  encodeOracleFrame(record, captureListPath.encode().asPtr());
  record.encodeSequenceSize(captures.size());
  for (const auto& capture : captures) { encodeStableOracleFrame(record, capture); }
  return expectedDomainRecord("zom.binder.body-explicit-closure-capture"_zc,
                              record.finish().asPtr());
}
zc::Array<uint8_t> explicitClosureCaptureFactWire(const StableExplicitClosureCaptureFact& value) {
  return explicitClosureCaptureFactWire(value.owner(), value.closure(), value.captureListPath(),
                                        value.captures().values());
}

zc::Array<uint8_t> localExportFactWire(const StableLocalExportFact& value) {
  identity::CanonicalEncoder record;
  encodeOracleFrame(record, value.declaringModule().encode().asPtr());
  encodeOracleFrame(record, value.exportPath().encode().asPtr());
  record.encodeUint8(static_cast<uint8_t>(value.name().nameSpace()));
  value.name().name().encode(record);
  encodeStableOracleFrame(record, value.binding());
  encodeStableOracleFrame(record, value.canonicalTarget());
  ZC_IF_SOME(visibility, value.visibility()) {
    record.encodeUint8(0x01);
    record.encodeUint8(static_cast<uint8_t>(visibility));
  } else {
    record.encodeUint8(0x00);
  }
  record.encodeSequenceSize(value.reexportChain().values().size());
  for (const auto& step : value.reexportChain().values()) {
    record.encodeByteString(reexportStepWire(step).asPtr());
  }
  return expectedDomainRecord("zom.binder.skeleton-local-export"_zc, record.finish().asPtr());
}

void encodeBindingNameOracle(identity::CanonicalEncoder& encoder, const BindingNameKey& value) {
  encoder.encodeUint8(static_cast<uint8_t>(value.nameSpace()));
  value.name().encode(encoder);
}

zc::Array<uint8_t> bindingNameWire(const BindingNameKey& value) {
  identity::CanonicalEncoder record;
  encodeBindingNameOracle(record, value);
  return record.finish();
}

zc::Array<uint8_t> exportedBindingWire(const StableExportedBinding& value) {
  identity::CanonicalEncoder record;
  encodeBindingNameOracle(record, value.name());
  encodeStableOracleFrame(record, value.binding());
  encodeStableOracleFrame(record, value.canonicalTarget());
  ZC_IF_SOME(visibility, value.visibility()) {
    record.encodeUint8(0x01);
    record.encodeUint8(static_cast<uint8_t>(visibility));
  } else {
    record.encodeUint8(0x00);
  }
  record.encodeUint8(value.exported() ? 0x01 : 0x00);
  return expectedDomainRecord("zom.binder.exported-binding"_zc, record.finish().asPtr());
}

zc::Array<uint8_t> exportedBindingKeyWire(const StableExportedBindingQueryKey& value) {
  identity::CanonicalEncoder record;
  encodeOracleFrame(record, value.module().encode().asPtr());
  encodeBindingNameOracle(record, value.name());
  return expectedDomainRecord("zom.query.exported-binding-key"_zc, record.finish().asPtr());
}

zc::Array<uint8_t> scopeNameBucketKeyWire(const StableScopeOwnerKey& scope,
                                          const BindingNameKey& name) {
  identity::CanonicalEncoder record;
  encodeStableOracleFrame(record, scope);
  encodeBindingNameOracle(record, name);
  return expectedDomainRecord("zom.query.scope-name-bucket-key"_zc, record.finish().asPtr());
}

zc::Array<uint8_t> nodeFactWire(const StableNodeScopeFact& value) {
  return nodeScopeFactWire(value.root(), value.nodePath(), value.scope());
}

zc::Array<uint8_t> ownerBodyWire(const StableOwnerBodyQueryKey& value) {
  return expectedWire("zom.binder.owner-body-query-key"_zc, value.module(),
                      value.owner().encode().asPtr(), true);
}

zc::Array<uint8_t> ownerAllocationRangeWire(const OwnerAllocationRange& value) {
  identity::CanonicalEncoder record;
  encodeOracleFrame(record, ownerBodyWire(value.owner()).asPtr());
  record.encodeUint32(value.scopeBegin());
  record.encodeUint32(value.scopeCount());
  record.encodeUint32(value.ownerLocalBegin());
  record.encodeUint32(value.ownerLocalCount());
  record.encodeUint32(value.anonymousBegin());
  record.encodeUint32(value.anonymousCount());
  record.encodeUint32(value.labelBegin());
  record.encodeUint32(value.labelCount());
  return expectedDomainRecord("zom.binder.owner-allocation-range"_zc, record.finish().asPtr());
}

zc::Array<uint8_t> moduleAllocationPlanWire(const ModuleBindingAllocationPlan& value,
                                            bool reverseOwners = false) {
  identity::CanonicalEncoder record;
  encodeOracleFrame(record, value.key().encode().asPtr());
  record.encodeUint32(value.skeletonScopeCount());
  record.encodeUint32(value.implementationOccurrenceCount());
  record.encodeSequenceSize(value.owners().values().size());
  if (reverseOwners) {
    for (size_t index = value.owners().values().size(); index > 0; --index) {
      record.encodeByteString(ownerAllocationRangeWire(value.owners().values()[index - 1]).asPtr());
    }
  } else {
    for (const auto& range : value.owners().values()) {
      record.encodeByteString(ownerAllocationRangeWire(range).asPtr());
    }
  }
  return expectedDomainRecord("zom.binder.module-allocation-plan"_zc, record.finish().asPtr());
}

template <typename Sequence, typename Oracle>
void encodeAggregateSequenceOracle(identity::CanonicalEncoder& encoder, const Sequence& values,
                                   Oracle&& oracle, bool reverse = false) {
  encoder.encodeSequenceSize(values.values().size());
  if (reverse) {
    for (size_t index = values.values().size(); index > 0; --index) {
      encoder.encodeByteString(oracle(values.values()[index - 1]).asPtr());
    }
  } else {
    for (const auto& value : values.values()) { encoder.encodeByteString(oracle(value).asPtr()); }
  }
}

template <typename Sequence, typename Oracle>
zc::Array<uint8_t> aggregateSequenceWire(const Sequence& values, Oracle&& oracle) {
  identity::CanonicalEncoder encoder;
  encodeAggregateSequenceOracle(encoder, values, zc::fwd<Oracle>(oracle));
  return encoder.finish();
}

zc::Array<uint8_t> ownerBodyAggregateWire(const BoundOwnerBody& value, bool reverseScopes = false) {
  identity::CanonicalEncoder record;
  encodeOracleFrame(record, ownerBodyWire(value.owner()).asPtr());
  encodeAggregateSequenceOracle(
      record, value.scopes(),
      [](const StableBodyScopeFact& fact) {
        return bodyScopeFactWire(fact.owner(), fact.scope(), fact.parent(), fact.kind());
      },
      reverseScopes);
  encodeAggregateSequenceOracle(
      record, value.nodeScopes(), [](const StableBodyNodeScopeFact& fact) {
        return bodyNodeScopeFactWire(fact.owner(), fact.nodePath(), fact.scope());
      });
  encodeAggregateSequenceOracle(
      record, value.bindings(), [](const StableOwnerLocalBindingFact& fact) {
        return ownerLocalBindingFactWire(fact.owner(), fact.key(), fact.kind(), fact.name(),
                                         fact.nameSpace(), fact.declaringScope(),
                                         fact.activation());
      });
  encodeAggregateSequenceOracle(record, value.resolutions(), [](const StableResolutionFact& fact) {
    return resolutionFactWire(fact.owner(), fact.usePath(), fact.nameSpace(), fact.binding(),
                              fact.canonicalTarget(), fact.origin());
  });
  encodeAggregateSequenceOracle(
      record, value.deferredMembers(), [](const StableDeferredMemberFact& fact) {
        return deferredMemberFactWire(fact.owner(), fact.usePath(), fact.basePath(),
                                      fact.accessKind(), fact.member(), fact.expectedNamespaces(),
                                      fact.genericArgumentPaths());
      });
  encodeAggregateSequenceOracle(record, value.selfTypes(), [](const StableSelfTypeFact& fact) {
    return selfTypeFactWire(fact.owner(), fact.syntaxPath(), fact.selfOwner());
  });
  encodeAggregateSequenceOracle(
      record, value.thisBindings(), [](const StableThisBindingFact& fact) {
        return thisBindingFactWire(fact.owner(), fact.expressionPath(), fact.receiver());
      });
  encodeAggregateSequenceOracle(
      record, value.shadowTargets(), [](const StableShadowTargetFact& fact) {
        return shadowTargetFactWire(fact.owner(), fact.binding(), fact.shadowed());
      });
  encodeAggregateSequenceOracle(record, value.labels(), [](const StableLabelFact& fact) {
    return labelFactWire(fact.key(), fact.name(), fact.statementPath(), fact.target());
  });
  encodeAggregateSequenceOracle(record, value.controlTransfers(),
                                [](const StableControlTransferFact& fact) {
                                  return controlTransferFactWire(fact.owner(), fact.transferPath(),
                                                                 fact.kind(), fact.target());
                                });
  encodeAggregateSequenceOracle(record, value.closures(), [](const StableClosureFact& fact) {
    return closureFactWire(fact);
  });
  encodeAggregateSequenceOracle(
      record, value.closureFreeVariables(),
      [](const StableClosureFreeVariableFact& fact) { return closureFreeVariableFactWire(fact); });
  encodeAggregateSequenceOracle(record, value.explicitClosureCaptures(),
                                [](const StableExplicitClosureCaptureFact& fact) {
                                  return explicitClosureCaptureFactWire(fact);
                                });
  encodeAggregateSequenceOracle(
      record, value.failedLookups(),
      [](const StableFailedLookupFact& fact) { return failedLookupFactWire(fact); });
  return expectedDomainRecord("zom.binder.owner-body"_zc, record.finish().asPtr());
}

zc::Array<uint8_t> moduleSkeletonWire(const BoundModuleSkeleton& value,
                                      bool reverseBodyOwners = false) {
  identity::CanonicalEncoder record;
  encodeOracleFrame(record, value.module().encode().asPtr());
  encodeAggregateSequenceOracle(record, value.scopes(), &scopeFactWire);
  encodeAggregateSequenceOracle(record, value.nodeScopes(), &nodeFactWire);
  encodeAggregateSequenceOracle(record, value.declarations(), &declarationFactWire);
  encodeAggregateSequenceOracle(record, value.implementationOccurrences(), &implementationFactWire);
  encodeAggregateSequenceOracle(record, value.genericParameterDeclarations(),
                                &genericDeclarationWire);
  encodeAggregateSequenceOracle(record, value.callableParameterDeclarations(),
                                &callableDeclarationWire);
  encodeAggregateSequenceOracle(record, value.moduleAliases(), &moduleAliasFactWire);
  encodeAggregateSequenceOracle(record, value.imports(), &importFactWire);
  encodeAggregateSequenceOracle(record, value.localExports(), &localExportFactWire);
  encodeAggregateSequenceOracle(record, value.bodyOwners(), &ownerBodyWire, reverseBodyOwners);
  encodeAggregateSequenceOracle(record, value.failedLookups(), &failedLookupFactWire);
  return expectedDomainRecord("zom.binder.module-skeleton"_zc, record.finish().asPtr());
}

zc::Array<uint8_t> definitionHeaderWire(
    const StableDefinitionHeader& value,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> genericOverride = nullptr,
    uint8_t bodyTag = 0x00, uint64_t genericCount = 0) {
  identity::CanonicalEncoder record;
  const auto queryKey = value.queryKey().encodeCanonical();
  const auto identityRecord = value.record().encode();
  record.encodeByteString(queryKey.asPtr());
  record.encodeByteString(identityRecord.asPtr());
  value.authoritySite().encode(record);
  record.encodeUint8(static_cast<uint8_t>(value.kind()));
  record.encodeUint8(static_cast<uint8_t>(value.nameSpace()));
  value.name().encode(record);
  record.encodeUint8(static_cast<uint8_t>(value.activation()));
  ZC_IF_SOME(visibility, value.visibility()) {
    record.encodeUint8(0x01);
    record.encodeUint8(static_cast<uint8_t>(visibility));
  } else {
    record.encodeUint8(0x00);
  }
  record.encodeUint8(bodyTag == 0x00 ? static_cast<uint8_t>(value.bodyDisposition()) : bodyTag);
  if (genericCount != 0) {
    record.encodeSequenceSize(genericCount);
  } else if (genericOverride == nullptr) {
    encodeSequenceOracle(record, value.genericParameters());
  } else {
    record.encodeSequenceSize(genericOverride.size());
    for (const auto bytes : genericOverride) { record.encodeByteString(bytes); }
  }
  encodeSequenceOracle(record, value.callableParameters());
  encodeSequenceOracle(record, value.declaredScopeRoles());
  const auto bytes = record.finish();
  return expectedDomainRecord("zom.binder.definition-header"_zc, bytes.asPtr());
}

zc::Array<uint8_t> implementationHeaderWire(
    const StableImplementationOccurrenceHeader& value,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> genericOverride = nullptr,
    uint8_t sourceTag = 0x00, uint64_t genericCount = 0,
    zc::ArrayPtr<const uint8_t> identityRecordOverride = nullptr) {
  identity::CanonicalEncoder record;
  const auto queryKey = value.queryKey().encodeCanonical();
  const auto authority = value.authority().encodeCanonical();
  const auto identityRecord = value.record().encode();
  record.encodeByteString(queryKey.asPtr());
  record.encodeByteString(authority.asPtr());
  record.encodeByteString(identityRecordOverride == nullptr ? identityRecord.asPtr()
                                                            : identityRecordOverride);
  if (genericCount != 0) {
    record.encodeSequenceSize(genericCount);
  } else if (genericOverride == nullptr) {
    encodeSequenceOracle(record, value.genericParameters());
  } else {
    record.encodeSequenceSize(genericOverride.size());
    for (const auto bytes : genericOverride) { record.encodeByteString(bytes); }
  }
  encodeSequenceOracle(record, value.declaredScopeRoles());
  record.encodeUint8(sourceTag == 0x00 ? static_cast<uint8_t>(value.sourceForm()) : sourceTag);
  const auto bytes = record.finish();
  return expectedDomainRecord("zom.binder.implementation-occurrence-header"_zc, bytes.asPtr());
}

template <typename T>
void expectByteMutation(zc::ArrayPtr<const uint8_t> encoded, size_t offset) {
  auto mutated = zc::heapArray(encoded);
  mutated[offset] = 0xff;
  ZC_EXPECT(StableBindingCodec<T>::decode(mutated.asPtr()) == zc::none);
}

template <typename T>
void expectFrameLengthMutation(zc::ArrayPtr<const uint8_t> encoded, size_t offset,
                               uint64_t length) {
  auto mutated = zc::heapArray(encoded);
  for (size_t index = 0; index < sizeof(uint64_t); ++index) {
    mutated[offset + index] = static_cast<uint8_t>(length >> ((sizeof(uint64_t) - index - 1) * 8));
  }
  ZC_EXPECT(StableBindingCodec<T>::decode(mutated.asPtr()) == zc::none);
}

template <typename T>
void expectRecordMutations(const T& value, zc::StringPtr domain, size_t ownerOffset,
                           size_t siteOffset) {
  auto encoded = StableBindingCodec<T>::encode(value);
  const size_t recordOffset = domain.size() + 1;
  auto wrongDomain = zc::heapArray(encoded.asPtr());
  wrongDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<T>::decode(wrongDomain.asPtr()) == zc::none);
  ZC_EXPECT(StableBindingCodec<T>::decode(encoded.slice(0, encoded.size() - 1)) == zc::none);
  expectByteMutation<T>(encoded.asPtr(), recordOffset);
  expectByteMutation<T>(encoded.asPtr(), recordOffset + ownerOffset);
  expectByteMutation<T>(encoded.asPtr(), siteOffset);
  expectByteMutation<T>(encoded.asPtr(), encoded.size() - 1);
  auto reordered = zc::heapArray(encoded.asPtr());
  for (size_t index = 0; index < 32; ++index) {
    const uint8_t retained = reordered[recordOffset + index];
    reordered[recordOffset + index] = reordered[recordOffset + ownerOffset + index];
    reordered[recordOffset + ownerOffset + index] = retained;
  }
  ZC_EXPECT(StableBindingCodec<T>::decode(reordered.asPtr()) == zc::none);
  zc::Vector<uint8_t> trailing(encoded.size() + 1);
  trailing.addAll(encoded.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<T>::decode(trailing.asPtr()) == zc::none);
}

ZC_TEST("StableBindingCodec.HeaderPrimitivesRoundTripAndRejectTags") {
  expectClosedEnumCodec(DefinitionBodyDisposition::NoExecutableBody);
  expectClosedEnumCodec(ImplementationSourceForm::Ordinary);
  expectClosedEnumCodec(ScopeRole::Declaration);
  expectRoundTrip(DefinitionBodyDisposition::ExecutableBody);
  expectRoundTrip(ImplementationSourceForm::BodylessMarker);
  expectRoundTrip(ScopeRole::Generic);
  expectRoundTrip(ScopeRole::Parameters);
  expectRoundTrip(ScopeRole::Members);
  expectRoundTrip(ScopeRole::Implementation);

  auto definitionSite = StableHeaderSite::definition(headerSite("owner"_zc));
  auto implementationSite = StableHeaderSite::implementation(occurrence("owner"_zc));
  expectRoundTrip(definitionSite);
  expectRoundTrip(implementationSite);
  auto definitionBytes = StableBindingCodec<StableHeaderSite>::encode(definitionSite);
  auto implementationBytes = StableBindingCodec<StableHeaderSite>::encode(implementationSite);
  identity::CanonicalEncoder definitionOracle;
  encodeHeaderSiteOracle(definitionOracle, definitionSite);
  ZC_EXPECT(definitionBytes.asPtr() == definitionOracle.finish().asPtr());
  identity::CanonicalEncoder implementationOracle;
  encodeHeaderSiteOracle(implementationOracle, implementationSite);
  ZC_EXPECT(implementationBytes.asPtr() == implementationOracle.finish().asPtr());
  expectByteMutation<StableHeaderSite>(definitionBytes.asPtr(), 0);
  expectByteMutation<StableHeaderSite>(definitionBytes.asPtr(), definitionBytes.size() - 1);
  ZC_EXPECT(StableBindingCodec<StableHeaderSite>::decode(
                implementationBytes.slice(0, implementationBytes.size() - 1)) == zc::none);
}

ZC_TEST("StableBindingCodec.HeaderParametersMatchWireAndRejectMutations") {
  using Callable = StableHeaderCallableParameter;
  using Generic = StableHeaderGenericParameter;
  auto definition = identity::DefinitionKey::compute(definitionRecord("owner"_zc));
  auto genericRecord = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::definition(definition.clone()), 0);
  auto generic = require(
      Generic::from(identity::GenericParameterKey::compute(genericRecord), genericRecord.clone(),
                    StableHeaderSite::definition(headerSite("owner"_zc)), declaredName("T"_zc), 0));
  auto callableRecord = identity::CallableParameterIdentityRecord::from(
      definition.clone(), identity::CallableParameterPosition::ordinary(0));
  auto callableName = optionalName("value"_zc);
  auto callable = require(
      Callable::from(identity::CallableParameterKey::compute(callableRecord),
                     callableRecord.clone(), StableHeaderSite::definition(headerSite("owner"_zc)),
                     zc::mv(callableName), identity::CallableParameterPosition::ordinary(0)));
  expectRoundTrip(generic);
  expectRoundTrip(callable);

  identity::CanonicalEncoder genericOracle;
  generic.key().encode(genericOracle);
  generic.record().encode(genericOracle);
  encodeHeaderSiteOracle(genericOracle, generic.site());
  generic.name().encode(genericOracle);
  genericOracle.encodeUint32(generic.ordinal());
  const auto genericRecordBytes = genericOracle.finish();
  constexpr auto genericDomain = "zom.binder.header-generic-parameter"_zc;
  ZC_EXPECT(StableBindingCodec<Generic>::encode(generic).asPtr() ==
            expectedDomainRecord(genericDomain, genericRecordBytes.asPtr()).asPtr());
  identity::CanonicalEncoder callableOracle;
  callable.key().encode(callableOracle);
  callable.record().encode(callableOracle);
  encodeHeaderSiteOracle(callableOracle, callable.site());
  callableOracle.encodeUint8(0x01);
  ZC_ASSERT_NONNULL(callable.name()).encode(callableOracle);
  callable.position().encode(callableOracle);
  const auto callableRecordBytes = callableOracle.finish();
  constexpr auto callableDomain = "zom.binder.header-callable-parameter"_zc;
  ZC_EXPECT(StableBindingCodec<Callable>::encode(callable).asPtr() ==
            expectedDomainRecord(callableDomain, callableRecordBytes.asPtr()).asPtr());
  expectRecordMutations(generic, genericDomain, 33, genericDomain.size() + 1 + 70);
  expectRecordMutations(callable, callableDomain, 32, callableDomain.size() + 1 + 69);
  auto genericBytes = StableBindingCodec<Generic>::encode(generic);
  expectByteMutation<Generic>(genericBytes.asPtr(), genericDomain.size() + 1 + 32);
  expectByteMutation<Generic>(genericBytes.asPtr(), genericDomain.size() + 1 + 65);
  auto callableBytes = StableBindingCodec<Callable>::encode(callable);
  const auto callableSiteBytes = StableBindingCodec<StableHeaderSite>::encode(callable.site());
  expectByteMutation<Callable>(callableBytes.asPtr(), callableDomain.size() + 1 + 64);
  expectByteMutation<Callable>(callableBytes.asPtr(),
                               callableDomain.size() + 1 + 69 + callableSiteBytes.size());
  expectByteMutation<Callable>(callableBytes.asPtr(), callableBytes.size() - 5);
  auto receiverRecord = identity::CallableParameterIdentityRecord::from(
      definition.clone(), identity::CallableParameterPosition::receiver());
  zc::Maybe<identity::DeclaredDefinitionName> receiverName;
  auto receiver = require(
      Callable::from(identity::CallableParameterKey::compute(receiverRecord),
                     receiverRecord.clone(), StableHeaderSite::definition(headerSite("owner"_zc)),
                     zc::mv(receiverName), identity::CallableParameterPosition::receiver()));
  expectRoundTrip(receiver);
  auto implementation = digestKey<identity::ImplKey>(0x33);
  auto implementationRecord = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::implementation(implementation.clone()), 1);
  auto implementationGeneric = require(Generic::from(
      identity::GenericParameterKey::compute(implementationRecord), implementationRecord.clone(),
      StableHeaderSite::implementation(
          ImplSourceOccurrenceKey::from(implementation.clone(), headerSite("owner"_zc))),
      declaredName("U"_zc), 1));
  expectRoundTrip(implementationGeneric);
  auto implementationBytes = StableBindingCodec<Generic>::encode(implementationGeneric);
  expectByteMutation<Generic>(implementationBytes.asPtr(), genericDomain.size() + 1 + 71);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<Generic>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<Callable>::decode(oversized) == zc::none);
  zc::Vector<Generic> generics;
  generics.add(generic.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<Generic>::from(zc::mv(generics)) != zc::none);
  zc::Vector<Callable> callables;
  callables.add(callable.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<Callable>::from(zc::mv(callables)) != zc::none);
}

ZC_TEST("StableBindingCodec.DefinitionHeaderMatchesWireAndRejectsSequenceMutations") {
  auto value = require(definitionHeader(DefinitionHeaderMutation::None));
  expectRoundTrip(value);
  auto encoded = StableBindingCodec<StableDefinitionHeader>::encode(value);
  ZC_EXPECT(encoded.asPtr() == definitionHeaderWire(value).asPtr());
  auto withoutVisibility = require(definitionHeader(DefinitionHeaderMutation::WithoutVisibility));
  expectRoundTrip(withoutVisibility);

  const auto genericBytes = StableBindingCodec<StableHeaderGenericParameter>::encode(
      value.genericParameters().values()[0]);
  zc::ArrayPtr<const uint8_t> duplicate[] = {genericBytes.asPtr(), genericBytes.asPtr()};
  auto duplicateWire = definitionHeaderWire(value, zc::arrayPtr(duplicate));
  ZC_EXPECT(StableBindingCodec<StableDefinitionHeader>::decode(duplicateWire.asPtr()) == zc::none);

  auto secondRecord = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::definition(value.queryKey().definition().clone()),
      1);
  auto second = require(StableHeaderGenericParameter::from(
      identity::GenericParameterKey::compute(secondRecord), secondRecord.clone(),
      StableHeaderSite::definition(value.authoritySite().clone()), declaredName("U"_zc), 1));
  const auto secondBytes = StableBindingCodec<StableHeaderGenericParameter>::encode(second);
  zc::ArrayPtr<const uint8_t> reversed[2];
  const bool genericFirst =
      stable_binding_codec_detail::compareBytes(genericBytes.asPtr(), secondBytes.asPtr()) < 0;
  reversed[0] = genericFirst ? secondBytes.asPtr() : genericBytes.asPtr();
  reversed[1] = genericFirst ? genericBytes.asPtr() : secondBytes.asPtr();
  auto reorderedWire = definitionHeaderWire(value, zc::arrayPtr(reversed));
  ZC_EXPECT(StableBindingCodec<StableDefinitionHeader>::decode(reorderedWire.asPtr()) == zc::none);

  auto wrongDomain = zc::heapArray(encoded.asPtr());
  wrongDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<StableDefinitionHeader>::decode(wrongDomain.asPtr()) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableDefinitionHeader>::decode(
                encoded.slice(0, encoded.size() - 1)) == zc::none);
  zc::Vector<uint8_t> trailing(encoded.size() + 1);
  trailing.addAll(encoded.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableDefinitionHeader>::decode(trailing.asPtr()) == zc::none);
  auto unknownTag = definitionHeaderWire(value, nullptr, 0xff);
  ZC_EXPECT(StableBindingCodec<StableDefinitionHeader>::decode(unknownTag.asPtr()) == zc::none);
  auto hostileCount = definitionHeaderWire(value, nullptr, 0x00, 1048577);
  ZC_EXPECT(StableBindingCodec<StableDefinitionHeader>::decode(hostileCount.asPtr()) == zc::none);
  uint8_t oversizedBacking = 0;
  ZC_EXPECT(StableBindingCodec<StableDefinitionHeader>::decode(
                zc::ArrayPtr<const uint8_t>(&oversizedBacking, 134217729)) == zc::none);
}

ZC_TEST("StableBindingCodec.ImplementationHeaderMatchesWireAndRejectsSequenceMutations") {
  auto value = require(implementationHeader(ImplementationHeaderMutation::None));
  expectRoundTrip(value);
  auto encoded = StableBindingCodec<StableImplementationOccurrenceHeader>::encode(value);
  ZC_EXPECT(encoded.asPtr() == implementationHeaderWire(value).asPtr());
  auto bodyless = require(implementationHeader(ImplementationHeaderMutation::Bodyless));
  expectRoundTrip(bodyless);

  const auto genericBytes = StableBindingCodec<StableHeaderGenericParameter>::encode(
      value.genericParameters().values()[0]);
  zc::ArrayPtr<const uint8_t> duplicate[] = {genericBytes.asPtr(), genericBytes.asPtr()};
  auto duplicateWire = implementationHeaderWire(value, zc::arrayPtr(duplicate));
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceHeader>::decode(
                duplicateWire.asPtr()) == zc::none);

  auto secondRecord = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::implementation(
          value.authority().implementation().clone()),
      1);
  auto second = require(StableHeaderGenericParameter::from(
      identity::GenericParameterKey::compute(secondRecord), secondRecord.clone(),
      StableHeaderSite::implementation(value.queryKey().occurrence().clone()), declaredName("U"_zc),
      1));
  const auto secondBytes = StableBindingCodec<StableHeaderGenericParameter>::encode(second);
  zc::ArrayPtr<const uint8_t> reversed[2];
  const bool genericFirst =
      stable_binding_codec_detail::compareBytes(genericBytes.asPtr(), secondBytes.asPtr()) < 0;
  reversed[0] = genericFirst ? secondBytes.asPtr() : genericBytes.asPtr();
  reversed[1] = genericFirst ? genericBytes.asPtr() : secondBytes.asPtr();
  auto reorderedWire = implementationHeaderWire(value, zc::arrayPtr(reversed));
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceHeader>::decode(
                reorderedWire.asPtr()) == zc::none);

  constexpr auto domain = "zom.binder.implementation-occurrence-header"_zc;
  const auto queryKeyBytes = value.queryKey().encodeCanonical();
  const auto authorityBytes = value.authority().encodeCanonical();
  const size_t queryKeyOffset = domain.size() + 1 + sizeof(uint64_t);
  const size_t authorityOffset = queryKeyOffset + queryKeyBytes.size() + sizeof(uint64_t);
  const size_t identityRecordOffset = authorityOffset + authorityBytes.size() + sizeof(uint64_t);
  expectByteMutation<StableImplementationOccurrenceHeader>(encoded.asPtr(), queryKeyOffset);
  expectByteMutation<StableImplementationOccurrenceHeader>(encoded.asPtr(), authorityOffset);
  expectByteMutation<StableImplementationOccurrenceHeader>(encoded.asPtr(), identityRecordOffset);
  auto oversizedIdentityRecord = zc::heapArray(encoded.asPtr());
  const size_t identityRecordLengthOffset = identityRecordOffset - sizeof(uint64_t);
  for (size_t index = 0; index < sizeof(uint64_t); ++index) {
    oversizedIdentityRecord[identityRecordLengthOffset + index] = 0;
  }
  oversizedIdentityRecord[identityRecordLengthOffset + 5] = 0x40;
  oversizedIdentityRecord[identityRecordLengthOffset + 7] = 0x01;
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceHeader>::decode(
                oversizedIdentityRecord.asPtr()) == zc::none);
  auto oversizedIdentityPayload = zc::heapArray<uint8_t>(4 * 1024 * 1024 + 1);
  auto oversizedIdentityWire =
      implementationHeaderWire(value, nullptr, 0x00, 0, oversizedIdentityPayload.asPtr());
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceHeader>::decode(
                oversizedIdentityWire.asPtr()) == zc::none);

  auto wrongDomain = zc::heapArray(encoded.asPtr());
  wrongDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceHeader>::decode(wrongDomain.asPtr()) ==
            zc::none);
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceHeader>::decode(
                encoded.slice(0, encoded.size() - 1)) == zc::none);
  zc::Vector<uint8_t> trailing(encoded.size() + 1);
  trailing.addAll(encoded.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceHeader>::decode(trailing.asPtr()) ==
            zc::none);
  auto unknownTag = implementationHeaderWire(value, nullptr, 0xff);
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceHeader>::decode(unknownTag.asPtr()) ==
            zc::none);
  auto hostileCount = implementationHeaderWire(value, nullptr, 0x00, 1048576);
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceHeader>::decode(
                hostileCount.asPtr()) == zc::none);
  uint8_t oversizedBacking = 0;
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceHeader>::decode(
                zc::ArrayPtr<const uint8_t>(&oversizedBacking, 134217729)) == zc::none);
}

ZC_TEST("StableBindingCodec.ScopeFactsMatchWireAndRejectMutations") {
  zc::Maybe<StableScopeOwnerKey> noParent;
  auto moduleFact = require(StableScopeFact::from(StableScopeOwnerKey::module(module("owner"_zc)),
                                                  zc::mv(noParent), ScopeKind::Module));
  auto definitionKey =
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11));
  auto definitionOwner =
      require(StableScopeOwnerKey::definition(definitionKey.clone(), ScopeRole::Declaration));
  auto scopeFact = require(StableScopeFact::from(definitionOwner.clone(),
                                                 moduleFact.owner().clone(), ScopeKind::Function));
  auto nodeFact = require(
      StableNodeScopeFact::from(StableNodeSyntaxRoot::definitionHeader(zc::mv(definitionKey)),
                                localPath(), definitionOwner.clone()));
  expectRoundTrip(moduleFact);
  expectRoundTrip(scopeFact);
  expectRoundTrip(nodeFact);
  expectClosedEnumCodec(ScopeKind::Module);
  expectRoundTrip(ScopeKind::UnsafeBlock);

  auto scopeBytes = StableBindingCodec<StableScopeFact>::encode(scopeFact);
  auto moduleBytes = StableBindingCodec<StableScopeFact>::encode(moduleFact);
  auto nodeBytes = StableBindingCodec<StableNodeScopeFact>::encode(nodeFact);
  ZC_EXPECT(scopeBytes.asPtr() == scopeFactWire(scopeFact).asPtr());
  ZC_EXPECT(moduleBytes.asPtr() == scopeFactWire(moduleFact).asPtr());
  ZC_EXPECT(nodeBytes.asPtr() ==
            nodeScopeFactWire(nodeFact.root(), nodeFact.nodePath(), nodeFact.scope()).asPtr());
  constexpr auto scopeDomain = "zom.binder.skeleton-scope"_zc;
  constexpr auto nodeDomain = "zom.binder.skeleton-node-scope"_zc;
  const auto ownerBytes = StableBindingCodec<StableScopeOwnerKey>::encode(scopeFact.owner());
  const size_t scopeRecord = scopeDomain.size() + 1;
  expectByteMutation<StableScopeFact>(scopeBytes.asPtr(), 0);
  expectByteMutation<StableScopeFact>(scopeBytes.asPtr(), scopeRecord + sizeof(uint64_t));
  expectByteMutation<StableScopeFact>(scopeBytes.asPtr(),
                                      scopeRecord + sizeof(uint64_t) + ownerBytes.size());
  const auto parentBytes =
      StableBindingCodec<StableScopeOwnerKey>::encode(ZC_ASSERT_NONNULL(scopeFact.parent()));
  expectByteMutation<StableScopeFact>(scopeBytes.asPtr(),
                                      scopeRecord + 2 * sizeof(uint64_t) + ownerBytes.size() + 1);
  expectByteMutation<StableScopeFact>(scopeBytes.asPtr(), scopeBytes.size() - 1);
  auto missingParentFrame = zc::heapArray(moduleBytes.asPtr());
  const auto moduleOwnerBytes = StableBindingCodec<StableScopeOwnerKey>::encode(moduleFact.owner());
  missingParentFrame[scopeRecord + sizeof(uint64_t) + moduleOwnerBytes.size()] = 0x01;
  ZC_EXPECT(StableBindingCodec<StableScopeFact>::decode(missingParentFrame.asPtr()) == zc::none);
  ZC_EXPECT(parentBytes.size() != 0);
  const auto rootBytes = StableBindingCodec<StableNodeSyntaxRoot>::encode(nodeFact.root());
  const auto pathBytes = nodeFact.nodePath().encode();
  const size_t nodeRecord = nodeDomain.size() + 1;
  expectByteMutation<StableNodeScopeFact>(nodeBytes.asPtr(), 0);
  expectByteMutation<StableNodeScopeFact>(nodeBytes.asPtr(), nodeRecord + sizeof(uint64_t));
  expectByteMutation<StableNodeScopeFact>(
      nodeBytes.asPtr(), nodeRecord + sizeof(uint64_t) + rootBytes.size() + sizeof(uint64_t));
  expectByteMutation<StableNodeScopeFact>(
      nodeBytes.asPtr(),
      nodeRecord + 2 * sizeof(uint64_t) + rootBytes.size() + pathBytes.size() + sizeof(uint64_t));

  auto wrongScope = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x12)),
      ScopeRole::Declaration));
  auto mismatchedWire = nodeScopeFactWire(nodeFact.root(), nodeFact.nodePath(), wrongScope);
  ZC_EXPECT(StableBindingCodec<StableNodeScopeFact>::decode(mismatchedWire.asPtr()) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableScopeFact>::decode(
                scopeBytes.slice(0, scopeBytes.size() - 1)) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableNodeScopeFact>::decode(
                nodeBytes.slice(0, nodeBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> scopeTrailing(scopeBytes.size() + 1);
  scopeTrailing.addAll(scopeBytes.asPtr());
  scopeTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableScopeFact>::decode(scopeTrailing.asPtr()) == zc::none);
  zc::Vector<uint8_t> nodeTrailing(nodeBytes.size() + 1);
  nodeTrailing.addAll(nodeBytes.asPtr());
  nodeTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableNodeScopeFact>::decode(nodeTrailing.asPtr()) == zc::none);

  zc::Vector<StableScopeFact> scopes;
  addCanonicalPair(scopes, moduleFact.clone(), scopeFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableScopeFact>::from(zc::mv(scopes)) != zc::none);
  zc::Vector<StableScopeFact> duplicateScopes;
  duplicateScopes.add(scopeFact.clone());
  duplicateScopes.add(scopeFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableScopeFact>::from(zc::mv(duplicateScopes)) ==
            zc::none);
  zc::Vector<StableNodeScopeFact> nodes;
  nodes.add(nodeFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableNodeScopeFact>::from(zc::mv(nodes)) != zc::none);
  zc::Vector<StableNodeScopeFact> duplicateNodes;
  duplicateNodes.add(nodeFact.clone());
  duplicateNodes.add(nodeFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableNodeScopeFact>::from(zc::mv(duplicateNodes)) ==
            zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableScopeFact>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableNodeScopeFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.BodyScopeFactsMatchWireAndRejectMutations") {
  auto owner = ownerBody();
  auto scope = StableScopeOwnerKey::body(owner.clone(), localPath());
  auto parent = StableScopeOwnerKey::module(module("owner"_zc));
  auto scopeFact = require(
      StableBodyScopeFact::from(owner.clone(), scope.clone(), parent.clone(), ScopeKind::Function));
  auto nodeFact =
      require(StableBodyNodeScopeFact::from(owner.clone(), localPath(2), scope.clone()));
  expectRoundTrip(scopeFact);
  expectRoundTrip(nodeFact);

  auto scopeBytes = StableBindingCodec<StableBodyScopeFact>::encode(scopeFact);
  auto nodeBytes = StableBindingCodec<StableBodyNodeScopeFact>::encode(nodeFact);
  ZC_EXPECT(scopeBytes.asPtr() == bodyScopeFactWire(scopeFact.owner(), scopeFact.scope(),
                                                    scopeFact.parent(), scopeFact.kind())
                                      .asPtr());
  ZC_EXPECT(nodeBytes.asPtr() ==
            bodyNodeScopeFactWire(nodeFact.owner(), nodeFact.nodePath(), nodeFact.scope()).asPtr());

  constexpr auto scopeDomain = "zom.binder.body-scope"_zc;
  const auto ownerBytes = StableBindingCodec<StableOwnerBodyQueryKey>::encode(scopeFact.owner());
  const auto bodyScopeBytes = StableBindingCodec<StableScopeOwnerKey>::encode(scopeFact.scope());
  const auto parentBytes = StableBindingCodec<StableScopeOwnerKey>::encode(scopeFact.parent());
  size_t offset = scopeDomain.size() + 1;
  expectByteMutation<StableBodyScopeFact>(scopeBytes.asPtr(), 0);
  expectFrameLengthMutation<StableBodyScopeFact>(scopeBytes.asPtr(), offset, ownerBytes.size() - 1);
  expectByteMutation<StableBodyScopeFact>(scopeBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectByteMutation<StableBodyScopeFact>(scopeBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + bodyScopeBytes.size();
  expectByteMutation<StableBodyScopeFact>(scopeBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + parentBytes.size();
  expectByteMutation<StableBodyScopeFact>(scopeBytes.asPtr(), offset);

  constexpr auto nodeDomain = "zom.binder.body-node-scope"_zc;
  const auto pathBytes = nodeFact.nodePath().encode();
  offset = nodeDomain.size() + 1;
  expectByteMutation<StableBodyNodeScopeFact>(nodeBytes.asPtr(), 0);
  expectByteMutation<StableBodyNodeScopeFact>(nodeBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectByteMutation<StableBodyNodeScopeFact>(nodeBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + pathBytes.size();
  expectByteMutation<StableBodyNodeScopeFact>(nodeBytes.asPtr(), offset + sizeof(uint64_t));

  auto foreignScope = StableScopeOwnerKey::body(ownerBody("foreign"_zc), localPath());
  auto mismatchedScopeWire =
      bodyScopeFactWire(scopeFact.owner(), foreignScope, scopeFact.parent(), scopeFact.kind());
  ZC_EXPECT(StableBindingCodec<StableBodyScopeFact>::decode(mismatchedScopeWire.asPtr()) ==
            zc::none);
  auto mismatchedNodeWire =
      bodyNodeScopeFactWire(nodeFact.owner(), nodeFact.nodePath(), foreignScope);
  ZC_EXPECT(StableBindingCodec<StableBodyNodeScopeFact>::decode(mismatchedNodeWire.asPtr()) ==
            zc::none);
  auto unknownKindWire = bodyScopeFactWire(scopeFact.owner(), scopeFact.scope(), scopeFact.parent(),
                                           static_cast<ScopeKind>(0xff));
  ZC_EXPECT(StableBindingCodec<StableBodyScopeFact>::decode(unknownKindWire.asPtr()) == zc::none);

  ZC_EXPECT(StableBindingCodec<StableBodyScopeFact>::decode(
                scopeBytes.slice(0, scopeBytes.size() - 1)) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableBodyNodeScopeFact>::decode(
                nodeBytes.slice(0, nodeBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> scopeTrailing(scopeBytes.size() + 1);
  scopeTrailing.addAll(scopeBytes.asPtr());
  scopeTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableBodyScopeFact>::decode(scopeTrailing.asPtr()) == zc::none);
  zc::Vector<uint8_t> nodeTrailing(nodeBytes.size() + 1);
  nodeTrailing.addAll(nodeBytes.asPtr());
  nodeTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableBodyNodeScopeFact>::decode(nodeTrailing.asPtr()) == zc::none);

  zc::Vector<StableBodyScopeFact> scopes;
  scopes.add(scopeFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableBodyScopeFact>::from(zc::mv(scopes)) != zc::none);
  zc::Vector<StableBodyScopeFact> duplicateScopes;
  duplicateScopes.add(scopeFact.clone());
  duplicateScopes.add(scopeFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableBodyScopeFact>::from(zc::mv(duplicateScopes)) ==
            zc::none);
  zc::Vector<StableBodyNodeScopeFact> nodes;
  nodes.add(nodeFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableBodyNodeScopeFact>::from(zc::mv(nodes)) != zc::none);
  zc::Vector<StableBodyNodeScopeFact> duplicateNodes;
  duplicateNodes.add(nodeFact.clone());
  duplicateNodes.add(nodeFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableBodyNodeScopeFact>::from(zc::mv(duplicateNodes)) ==
            zc::none);

  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableBodyScopeFact>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableBodyNodeScopeFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.OwnerBindingAndResolutionMatchIndependentWires") {
  auto owner = ownerBody();
  auto key = require(OwnerLocalBindingKey::from(
      owner.owner().clone(), localPath(), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("value"_zc)));
  auto scope = StableScopeOwnerKey::body(owner.clone(), localPath(9));
  auto bindingFact = require(StableOwnerLocalBindingFact::from(
      owner.clone(), key.clone(), OwnerLocalBindingKind::Local, declaredName("value"_zc),
      Namespace::Value, scope.clone(), DefinitionActivation::ExpressionIntroduction));
  auto binding = StableBindingTargetKey::module(module("binding"_zc));
  auto canonical = StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
      module("target"_zc), digestKey<identity::DefinitionKey>(0x11)));
  auto resolution = require(
      StableResolutionFact::from(owner.clone(), localPath(2), Namespace::Value, binding.clone(),
                                 canonical.clone(), BindingOrigin::LocalDeclaration));
  expectRoundTrip(bindingFact);
  expectRoundTrip(resolution);

  auto bindingBytes = StableBindingCodec<StableOwnerLocalBindingFact>::encode(bindingFact);
  auto resolutionBytes = StableBindingCodec<StableResolutionFact>::encode(resolution);
  ZC_EXPECT(bindingBytes.asPtr() ==
            ownerLocalBindingFactWire(bindingFact.owner(), bindingFact.key(), bindingFact.kind(),
                                      bindingFact.name(), bindingFact.nameSpace(),
                                      bindingFact.declaringScope(), bindingFact.activation())
                .asPtr());
  ZC_EXPECT(resolutionBytes.asPtr() ==
            resolutionFactWire(resolution.owner(), resolution.usePath(), resolution.nameSpace(),
                               resolution.binding(), resolution.canonicalTarget(),
                               resolution.origin())
                .asPtr());

  constexpr auto bindingDomain = "zom.binder.body-owner-local-binding"_zc;
  const auto ownerBytes = StableBindingCodec<StableOwnerBodyQueryKey>::encode(bindingFact.owner());
  const auto keyBytes = bindingFact.key().encode();
  identity::CanonicalEncoder nameEncoder;
  bindingFact.name().encode(nameEncoder);
  const auto nameBytes = nameEncoder.finish();
  const auto scopeBytes =
      StableBindingCodec<StableScopeOwnerKey>::encode(bindingFact.declaringScope());
  size_t offset = bindingDomain.size() + 1;
  expectByteMutation<StableOwnerLocalBindingFact>(bindingBytes.asPtr(), 0);
  expectByteMutation<StableOwnerLocalBindingFact>(bindingBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectByteMutation<StableOwnerLocalBindingFact>(bindingBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + keyBytes.size();
  expectByteMutation<StableOwnerLocalBindingFact>(bindingBytes.asPtr(), offset++);
  expectByteMutation<StableOwnerLocalBindingFact>(bindingBytes.asPtr(), offset);
  offset += nameBytes.size();
  expectByteMutation<StableOwnerLocalBindingFact>(bindingBytes.asPtr(), offset++);
  expectByteMutation<StableOwnerLocalBindingFact>(bindingBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + scopeBytes.size();
  expectByteMutation<StableOwnerLocalBindingFact>(bindingBytes.asPtr(), offset);

  constexpr auto resolutionDomain = "zom.binder.body-resolution"_zc;
  const auto pathBytes = resolution.usePath().encode();
  const auto targetBytes = StableBindingCodec<StableBindingTargetKey>::encode(resolution.binding());
  const auto canonicalBytes =
      StableBindingCodec<StableBindingTargetKey>::encode(resolution.canonicalTarget());
  offset = resolutionDomain.size() + 1;
  expectByteMutation<StableResolutionFact>(resolutionBytes.asPtr(), 0);
  expectByteMutation<StableResolutionFact>(resolutionBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectByteMutation<StableResolutionFact>(resolutionBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + pathBytes.size();
  expectByteMutation<StableResolutionFact>(resolutionBytes.asPtr(), offset++);
  expectByteMutation<StableResolutionFact>(resolutionBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + targetBytes.size();
  expectByteMutation<StableResolutionFact>(resolutionBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + canonicalBytes.size();
  expectByteMutation<StableResolutionFact>(resolutionBytes.asPtr(), offset);

  auto mismatchedKindWire = ownerLocalBindingFactWire(
      bindingFact.owner(), bindingFact.key(), OwnerLocalBindingKind::PatternBinding,
      bindingFact.name(), bindingFact.nameSpace(), bindingFact.declaringScope(),
      bindingFact.activation());
  ZC_EXPECT(StableBindingCodec<StableOwnerLocalBindingFact>::decode(mismatchedKindWire.asPtr()) ==
            zc::none);
  auto foreignScope = StableScopeOwnerKey::body(ownerBody("foreign"_zc), localPath());
  auto mismatchedScopeWire = ownerLocalBindingFactWire(
      bindingFact.owner(), bindingFact.key(), bindingFact.kind(), bindingFact.name(),
      bindingFact.nameSpace(), foreignScope, bindingFact.activation());
  ZC_EXPECT(StableBindingCodec<StableOwnerLocalBindingFact>::decode(mismatchedScopeWire.asPtr()) ==
            zc::none);

  auto foreignOwner = ownerBody("foreign"_zc);
  auto foreignKey = require(OwnerLocalBindingKey::from(
      foreignOwner.owner().clone(), localPath(), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("value"_zc)));
  auto foreignTarget =
      require(StableBindingTargetKey::ownerLocal(zc::mv(foreignOwner), zc::mv(foreignKey)));
  auto foreignTargetWire =
      resolutionFactWire(resolution.owner(), resolution.usePath(), resolution.nameSpace(),
                         foreignTarget, resolution.canonicalTarget(), resolution.origin());
  ZC_EXPECT(StableBindingCodec<StableResolutionFact>::decode(foreignTargetWire.asPtr()) ==
            zc::none);
  auto unknownNamespaceWire =
      resolutionFactWire(resolution.owner(), resolution.usePath(), static_cast<Namespace>(0xff),
                         resolution.binding(), resolution.canonicalTarget(), resolution.origin());
  ZC_EXPECT(StableBindingCodec<StableResolutionFact>::decode(unknownNamespaceWire.asPtr()) ==
            zc::none);
  auto unknownOriginWire = resolutionFactWire(
      resolution.owner(), resolution.usePath(), resolution.nameSpace(), resolution.binding(),
      resolution.canonicalTarget(), static_cast<BindingOrigin>(0xff));
  ZC_EXPECT(StableBindingCodec<StableResolutionFact>::decode(unknownOriginWire.asPtr()) ==
            zc::none);

  ZC_EXPECT(StableBindingCodec<StableOwnerLocalBindingFact>::decode(
                bindingBytes.slice(0, bindingBytes.size() - 1)) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableResolutionFact>::decode(
                resolutionBytes.slice(0, resolutionBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> bindingTrailing(bindingBytes.size() + 1);
  bindingTrailing.addAll(bindingBytes.asPtr());
  bindingTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableOwnerLocalBindingFact>::decode(bindingTrailing.asPtr()) ==
            zc::none);
  zc::Vector<uint8_t> resolutionTrailing(resolutionBytes.size() + 1);
  resolutionTrailing.addAll(resolutionBytes.asPtr());
  resolutionTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableResolutionFact>::decode(resolutionTrailing.asPtr()) ==
            zc::none);

  zc::Vector<StableOwnerLocalBindingFact> bindings;
  bindings.add(bindingFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableOwnerLocalBindingFact>::from(zc::mv(bindings)) !=
            zc::none);
  zc::Vector<StableOwnerLocalBindingFact> duplicateBindings;
  duplicateBindings.add(bindingFact.clone());
  duplicateBindings.add(bindingFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableOwnerLocalBindingFact>::from(
                zc::mv(duplicateBindings)) == zc::none);
  zc::Vector<StableResolutionFact> resolutions;
  resolutions.add(resolution.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableResolutionFact>::from(zc::mv(resolutions)) !=
            zc::none);
  zc::Vector<StableResolutionFact> duplicateResolutions;
  duplicateResolutions.add(resolution.clone());
  duplicateResolutions.add(resolution.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableResolutionFact>::from(
                zc::mv(duplicateResolutions)) == zc::none);

  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableOwnerLocalBindingFact>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableResolutionFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.DeferredMembersMatchPopulatedCanonicalWire") {
  zc::Vector<Namespace> namespaceValues;
  namespaceValues.add(Namespace::Value);
  namespaceValues.add(Namespace::Type);
  auto namespaces =
      require(StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(namespaceValues)));
  zc::Vector<LocalSyntaxPath> genericValues;
  genericValues.add(localPath(3));
  genericValues.add(localPath(4));
  auto genericPaths =
      require(StableBindingSequenceBuilder<LocalSyntaxPath>::from(zc::mv(genericValues)));
  auto fact = require(StableDeferredMemberFact::from(
      ownerBody(), localPath(1), localPath(2), MemberAccessKind::Optional,
      declaredName("member"_zc), zc::mv(namespaces), zc::mv(genericPaths)));
  expectRoundTrip(fact);
  expectRoundTrip(localPath());

  auto bytes = StableBindingCodec<StableDeferredMemberFact>::encode(fact);
  ZC_EXPECT(bytes.asPtr() == deferredMemberFactWire(fact.owner(), fact.usePath(), fact.basePath(),
                                                    fact.accessKind(), fact.member(),
                                                    fact.expectedNamespaces(),
                                                    fact.genericArgumentPaths())
                                 .asPtr());

  constexpr auto domain = "zom.binder.body-deferred-member"_zc;
  const auto ownerBytes = StableBindingCodec<StableOwnerBodyQueryKey>::encode(fact.owner());
  const auto useBytes = fact.usePath().encode();
  const auto baseBytes = fact.basePath().encode();
  identity::CanonicalEncoder memberEncoder;
  fact.member().encode(memberEncoder);
  const auto memberBytes = memberEncoder.finish();
  size_t offset = domain.size() + 1;
  expectByteMutation<StableDeferredMemberFact>(bytes.asPtr(), 0);
  expectFrameLengthMutation<StableDeferredMemberFact>(bytes.asPtr(), offset, ownerBytes.size() - 1);
  expectByteMutation<StableDeferredMemberFact>(bytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectFrameLengthMutation<StableDeferredMemberFact>(bytes.asPtr(), offset, useBytes.size() + 1);
  expectByteMutation<StableDeferredMemberFact>(bytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + useBytes.size();
  expectFrameLengthMutation<StableDeferredMemberFact>(bytes.asPtr(), offset, baseBytes.size() - 1);
  expectByteMutation<StableDeferredMemberFact>(bytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + baseBytes.size();
  expectByteMutation<StableDeferredMemberFact>(bytes.asPtr(), offset++);
  expectByteMutation<StableDeferredMemberFact>(bytes.asPtr(), offset + sizeof(uint64_t));
  offset += memberBytes.size();

  const size_t namespaceCountOffset = offset;
  expectFrameLengthMutation<StableDeferredMemberFact>(bytes.asPtr(), namespaceCountOffset, 0);
  expectFrameLengthMutation<StableDeferredMemberFact>(
      bytes.asPtr(), namespaceCountOffset,
      stable_binding_codec_detail::kBinderSemanticSequenceRecords + 1);
  const size_t firstNamespaceOffset = namespaceCountOffset + sizeof(uint64_t) * 2;
  const size_t secondNamespaceOffset = firstNamespaceOffset + 1 + sizeof(uint64_t);
  expectByteMutation<StableDeferredMemberFact>(bytes.asPtr(), firstNamespaceOffset);
  auto duplicateNamespaces = zc::heapArray(bytes.asPtr());
  duplicateNamespaces[secondNamespaceOffset] = duplicateNamespaces[firstNamespaceOffset];
  ZC_EXPECT(StableBindingCodec<StableDeferredMemberFact>::decode(duplicateNamespaces.asPtr()) ==
            zc::none);
  auto reversedNamespaces = zc::heapArray(bytes.asPtr());
  const uint8_t firstNamespace = reversedNamespaces[firstNamespaceOffset];
  reversedNamespaces[firstNamespaceOffset] = reversedNamespaces[secondNamespaceOffset];
  reversedNamespaces[secondNamespaceOffset] = firstNamespace;
  ZC_EXPECT(StableBindingCodec<StableDeferredMemberFact>::decode(reversedNamespaces.asPtr()) ==
            zc::none);
  offset += sizeof(uint64_t) + fact.expectedNamespaces().values().size() * (sizeof(uint64_t) + 1);

  const size_t genericCountOffset = offset;
  expectFrameLengthMutation<StableDeferredMemberFact>(
      bytes.asPtr(), genericCountOffset,
      stable_binding_codec_detail::kBinderSemanticSequenceRecords + 1);
  const size_t firstGenericOffset = genericCountOffset + sizeof(uint64_t) * 2;
  const size_t secondGenericOffset = firstGenericOffset + useBytes.size() + sizeof(uint64_t);
  expectByteMutation<StableDeferredMemberFact>(bytes.asPtr(), firstGenericOffset);
  auto duplicateGenericPaths = zc::heapArray(bytes.asPtr());
  for (size_t index = 0; index < useBytes.size(); ++index) {
    duplicateGenericPaths[secondGenericOffset + index] =
        duplicateGenericPaths[firstGenericOffset + index];
  }
  ZC_EXPECT(StableBindingCodec<StableDeferredMemberFact>::decode(duplicateGenericPaths.asPtr()) ==
            zc::none);
  auto reversedGenericPaths = zc::heapArray(bytes.asPtr());
  const size_t componentOffset = sizeof(uint64_t) + sizeof(uint32_t) - 1;
  const uint8_t firstComponent = reversedGenericPaths[firstGenericOffset + componentOffset];
  reversedGenericPaths[firstGenericOffset + componentOffset] =
      reversedGenericPaths[secondGenericOffset + componentOffset];
  reversedGenericPaths[secondGenericOffset + componentOffset] = firstComponent;
  ZC_EXPECT(StableBindingCodec<StableDeferredMemberFact>::decode(reversedGenericPaths.asPtr()) ==
            zc::none);

  auto samePathWire =
      deferredMemberFactWire(fact.owner(), fact.usePath(), fact.usePath(), fact.accessKind(),
                             fact.member(), fact.expectedNamespaces(), fact.genericArgumentPaths());
  ZC_EXPECT(StableBindingCodec<StableDeferredMemberFact>::decode(samePathWire.asPtr()) == zc::none);
  auto unknownAccessWire = deferredMemberFactWire(
      fact.owner(), fact.usePath(), fact.basePath(), static_cast<MemberAccessKind>(0xff),
      fact.member(), fact.expectedNamespaces(), fact.genericArgumentPaths());
  ZC_EXPECT(StableBindingCodec<StableDeferredMemberFact>::decode(unknownAccessWire.asPtr()) ==
            zc::none);
  zc::Vector<LocalSyntaxPath> conflictingValues;
  conflictingValues.add(fact.usePath().clone());
  auto conflictingPaths =
      require(StableBindingSequenceBuilder<LocalSyntaxPath>::from(zc::mv(conflictingValues)));
  auto conflictingWire =
      deferredMemberFactWire(fact.owner(), fact.usePath(), fact.basePath(), fact.accessKind(),
                             fact.member(), fact.expectedNamespaces(), conflictingPaths);
  ZC_EXPECT(StableBindingCodec<StableDeferredMemberFact>::decode(conflictingWire.asPtr()) ==
            zc::none);
  zc::Vector<LocalSyntaxPath> baseConflictingValues;
  baseConflictingValues.add(fact.basePath().clone());
  auto baseConflictingPaths =
      require(StableBindingSequenceBuilder<LocalSyntaxPath>::from(zc::mv(baseConflictingValues)));
  auto baseConflictingWire =
      deferredMemberFactWire(fact.owner(), fact.usePath(), fact.basePath(), fact.accessKind(),
                             fact.member(), fact.expectedNamespaces(), baseConflictingPaths);
  ZC_EXPECT(StableBindingCodec<StableDeferredMemberFact>::decode(baseConflictingWire.asPtr()) ==
            zc::none);

  ZC_EXPECT(StableBindingCodec<StableDeferredMemberFact>::decode(
                bytes.slice(0, bytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> trailing(bytes.size() + 1);
  trailing.addAll(bytes.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableDeferredMemberFact>::decode(trailing.asPtr()) == zc::none);
  zc::Vector<StableDeferredMemberFact> duplicateFacts;
  duplicateFacts.add(fact.clone());
  duplicateFacts.add(fact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableDeferredMemberFact>::from(zc::mv(duplicateFacts)) ==
            zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<LocalSyntaxPath>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableDeferredMemberFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.ContextualSelfMatchesClosedTagsAndModuleRoutes") {
  auto definition =
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11));
  auto nominal = StableSelfOwner::nominal(definition.clone());
  auto interfaceOwner = StableSelfOwner::interface(definition.clone());
  auto occurrenceKey = require(
      StableImplementationOccurrenceQueryKey::from(module("owner"_zc), occurrence("owner"_zc)));
  auto implementation = StableSelfOwner::implementationOccurrence(zc::mv(occurrenceKey));
  expectSumWires(nominal, interfaceOwner, implementation);

  constexpr auto selfOwnerDomain = "zom.binder.self-owner"_zc;
  auto nominalBytes = StableBindingCodec<StableSelfOwner>::encode(nominal);
  const size_t selfOwnerFrameOffset = selfOwnerDomain.size() + 2;
  expectFrameLengthMutation<StableSelfOwner>(nominalBytes.asPtr(), selfOwnerFrameOffset, 1);

  auto owner = ownerBody();
  auto selfFact = require(StableSelfTypeFact::from(owner.clone(), localPath(1), nominal.clone()));
  auto receiver = StableCallableParameterQueryKey::from(
      module("owner"_zc), digestKey<identity::CallableParameterKey>(0x33));
  auto thisFact =
      require(StableThisBindingFact::from(owner.clone(), localPath(2), receiver.clone()));
  expectRoundTrip(selfFact);
  expectRoundTrip(thisFact);

  auto selfBytes = StableBindingCodec<StableSelfTypeFact>::encode(selfFact);
  auto thisBytes = StableBindingCodec<StableThisBindingFact>::encode(thisFact);
  ZC_EXPECT(
      selfBytes.asPtr() ==
      selfTypeFactWire(selfFact.owner(), selfFact.syntaxPath(), selfFact.selfOwner()).asPtr());
  ZC_EXPECT(thisBytes.asPtr() ==
            thisBindingFactWire(thisFact.owner(), thisFact.expressionPath(), thisFact.receiver())
                .asPtr());

  const auto ownerBytes = StableBindingCodec<StableOwnerBodyQueryKey>::encode(owner);
  const auto selfPathBytes = selfFact.syntaxPath().encode();
  const auto stableSelfOwnerBytes = StableBindingCodec<StableSelfOwner>::encode(nominal);
  constexpr auto selfDomain = "zom.binder.body-self-type"_zc;
  size_t offset = selfDomain.size() + 1;
  expectByteMutation<StableSelfTypeFact>(selfBytes.asPtr(), 0);
  expectFrameLengthMutation<StableSelfTypeFact>(selfBytes.asPtr(), offset, ownerBytes.size() - 1);
  expectByteMutation<StableSelfTypeFact>(selfBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectFrameLengthMutation<StableSelfTypeFact>(selfBytes.asPtr(), offset,
                                                selfPathBytes.size() + 1);
  expectByteMutation<StableSelfTypeFact>(selfBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + selfPathBytes.size();
  expectFrameLengthMutation<StableSelfTypeFact>(selfBytes.asPtr(), offset,
                                                stableSelfOwnerBytes.size() - 1);
  expectByteMutation<StableSelfTypeFact>(selfBytes.asPtr(), offset + sizeof(uint64_t));

  const auto thisPathBytes = thisFact.expressionPath().encode();
  const auto receiverBytes =
      StableBindingCodec<StableCallableParameterQueryKey>::encode(thisFact.receiver());
  constexpr auto thisDomain = "zom.binder.body-this-binding"_zc;
  offset = thisDomain.size() + 1;
  expectByteMutation<StableThisBindingFact>(thisBytes.asPtr(), 0);
  expectFrameLengthMutation<StableThisBindingFact>(thisBytes.asPtr(), offset,
                                                   ownerBytes.size() - 1);
  expectByteMutation<StableThisBindingFact>(thisBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectFrameLengthMutation<StableThisBindingFact>(thisBytes.asPtr(), offset,
                                                   thisPathBytes.size() + 1);
  expectByteMutation<StableThisBindingFact>(thisBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + thisPathBytes.size();
  expectFrameLengthMutation<StableThisBindingFact>(thisBytes.asPtr(), offset,
                                                   receiverBytes.size() - 1);
  expectByteMutation<StableThisBindingFact>(thisBytes.asPtr(), offset + sizeof(uint64_t));

  auto foreignDefinition = StableDefinitionQueryKey::from(module("foreign"_zc),
                                                          digestKey<identity::DefinitionKey>(0x11));
  auto foreignNominalWire = selfTypeFactWire(owner, selfFact.syntaxPath(),
                                             StableSelfOwner::nominal(foreignDefinition.clone()));
  auto foreignInterfaceWire = selfTypeFactWire(
      owner, selfFact.syntaxPath(), StableSelfOwner::interface(foreignDefinition.clone()));
  auto foreignImplementationWire =
      selfTypeFactWire(owner, selfFact.syntaxPath(),
                       StableSelfOwner::implementationOccurrence(
                           require(StableImplementationOccurrenceQueryKey::from(
                               module("foreign"_zc), occurrence("foreign"_zc)))));
  ZC_EXPECT(StableBindingCodec<StableSelfTypeFact>::decode(foreignNominalWire.asPtr()) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableSelfTypeFact>::decode(foreignInterfaceWire.asPtr()) ==
            zc::none);
  ZC_EXPECT(StableBindingCodec<StableSelfTypeFact>::decode(foreignImplementationWire.asPtr()) ==
            zc::none);
  auto foreignThisWire = thisBindingFactWire(
      owner, thisFact.expressionPath(),
      StableCallableParameterQueryKey::from(module("foreign"_zc),
                                            digestKey<identity::CallableParameterKey>(0x33)));
  ZC_EXPECT(StableBindingCodec<StableThisBindingFact>::decode(foreignThisWire.asPtr()) == zc::none);

  ZC_EXPECT(StableBindingCodec<StableSelfTypeFact>::decode(
                selfBytes.slice(0, selfBytes.size() - 1)) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableThisBindingFact>::decode(
                thisBytes.slice(0, thisBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> selfTrailing(selfBytes.size() + 1);
  selfTrailing.addAll(selfBytes.asPtr());
  selfTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableSelfTypeFact>::decode(selfTrailing.asPtr()) == zc::none);
  zc::Vector<uint8_t> thisTrailing(thisBytes.size() + 1);
  thisTrailing.addAll(thisBytes.asPtr());
  thisTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableThisBindingFact>::decode(thisTrailing.asPtr()) == zc::none);

  zc::Vector<StableSelfTypeFact> duplicateSelfFacts;
  duplicateSelfFacts.add(selfFact.clone());
  duplicateSelfFacts.add(selfFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableSelfTypeFact>::from(zc::mv(duplicateSelfFacts)) ==
            zc::none);
  zc::Vector<StableThisBindingFact> duplicateThisFacts;
  duplicateThisFacts.add(thisFact.clone());
  duplicateThisFacts.add(thisFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableThisBindingFact>::from(zc::mv(duplicateThisFacts)) ==
            zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableSelfOwner>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableSelfTypeFact>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableThisBindingFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.ShadowTargetsMatchCanonicalWireAndOwnerRoutes") {
  auto owner = ownerBody();
  auto binding = StableBindingTargetKey::module(module("binding"_zc));
  auto shadowed = StableBindingTargetKey::definition(StableDefinitionQueryKey::from(
      module("target"_zc), digestKey<identity::DefinitionKey>(0x11)));
  auto fact =
      require(StableShadowTargetFact::from(owner.clone(), binding.clone(), shadowed.clone()));
  expectRoundTrip(fact);

  auto bytes = StableBindingCodec<StableShadowTargetFact>::encode(fact);
  ZC_EXPECT(bytes.asPtr() == shadowTargetFactWire(owner, binding, shadowed).asPtr());
  constexpr auto domain = "zom.binder.body-shadow-target"_zc;
  const auto ownerBytes = StableBindingCodec<StableOwnerBodyQueryKey>::encode(owner);
  const auto bindingBytes = StableBindingCodec<StableBindingTargetKey>::encode(binding);
  const auto shadowedBytes = StableBindingCodec<StableBindingTargetKey>::encode(shadowed);
  size_t offset = domain.size() + 1;
  expectByteMutation<StableShadowTargetFact>(bytes.asPtr(), 0);
  expectFrameLengthMutation<StableShadowTargetFact>(bytes.asPtr(), offset, ownerBytes.size() - 1);
  expectByteMutation<StableShadowTargetFact>(bytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectFrameLengthMutation<StableShadowTargetFact>(bytes.asPtr(), offset, bindingBytes.size() + 1);
  expectByteMutation<StableShadowTargetFact>(bytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + bindingBytes.size();
  expectFrameLengthMutation<StableShadowTargetFact>(bytes.asPtr(), offset,
                                                    shadowedBytes.size() - 1);
  expectByteMutation<StableShadowTargetFact>(bytes.asPtr(), offset + sizeof(uint64_t));

  auto sameTargetWire = shadowTargetFactWire(owner, binding, binding);
  ZC_EXPECT(StableBindingCodec<StableShadowTargetFact>::decode(sameTargetWire.asPtr()) == zc::none);

  auto foreignOwner = ownerBody("foreign"_zc);
  auto foreignLocalKey = require(OwnerLocalBindingKey::from(
      foreignOwner.owner().clone(), localPath(1), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("local"_zc)));
  auto foreignLocal =
      require(StableBindingTargetKey::ownerLocal(foreignOwner.clone(), zc::mv(foreignLocalKey)));
  auto foreignBindingWire = shadowTargetFactWire(owner, foreignLocal, shadowed);
  ZC_EXPECT(StableBindingCodec<StableShadowTargetFact>::decode(foreignBindingWire.asPtr()) ==
            zc::none);
  auto foreignAnonymousKey = require(AnonymousOwnerLocalKey::from(
      foreignOwner.owner().clone(), localPath(2), AnonymousOwnerLocalRole::Closure));
  auto foreignAnonymous = require(
      StableBindingTargetKey::anonymousOwner(zc::mv(foreignOwner), zc::mv(foreignAnonymousKey)));
  auto foreignShadowedWire = shadowTargetFactWire(owner, binding, foreignAnonymous);
  ZC_EXPECT(StableBindingCodec<StableShadowTargetFact>::decode(foreignShadowedWire.asPtr()) ==
            zc::none);

  ZC_EXPECT(StableBindingCodec<StableShadowTargetFact>::decode(bytes.slice(0, bytes.size() - 1)) ==
            zc::none);
  zc::Vector<uint8_t> trailing(bytes.size() + 1);
  trailing.addAll(bytes.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableShadowTargetFact>::decode(trailing.asPtr()) == zc::none);
  zc::Vector<StableShadowTargetFact> duplicateFacts;
  duplicateFacts.add(fact.clone());
  duplicateFacts.add(fact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableShadowTargetFact>::from(zc::mv(duplicateFacts)) ==
            zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableShadowTargetFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.LabelsMatchClosedWireAndOwnerRelations") {
  auto owner = ownerBody();
  auto key = StableLabelKey::from(owner.clone(), localPath(1));
  auto block = StableLabelTarget::block(StableScopeOwnerKey::body(owner.clone(), localPath(2)));
  auto loop = StableLabelTarget::loop(StableScopeOwnerKey::body(owner.clone(), localPath(2)));
  auto fact = require(
      StableLabelFact::from(key.clone(), declaredName("label"_zc), localPath(2), block.clone()));
  expectRoundTrip(key);
  expectSumWires(block, loop);
  expectRoundTrip(fact);

  auto keyBytes = StableBindingCodec<StableLabelKey>::encode(key);
  ZC_EXPECT(keyBytes.asPtr() == labelKeyWire(key).asPtr());
  constexpr auto keyDomain = "zom.binder.label-key"_zc;
  const auto ownerBytes = StableBindingCodec<StableOwnerBodyQueryKey>::encode(owner);
  const auto declarationPathBytes = key.declarationPath().encode();
  size_t offset = keyDomain.size() + 1;
  expectByteMutation<StableLabelKey>(keyBytes.asPtr(), 0);
  expectFrameLengthMutation<StableLabelKey>(keyBytes.asPtr(), offset, ownerBytes.size() - 1);
  expectByteMutation<StableLabelKey>(keyBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectFrameLengthMutation<StableLabelKey>(keyBytes.asPtr(), offset,
                                            declarationPathBytes.size() + 1);
  expectByteMutation<StableLabelKey>(keyBytes.asPtr(), offset + sizeof(uint64_t));
  ZC_EXPECT(StableBindingCodec<StableLabelKey>::decode(keyBytes.slice(0, keyBytes.size() - 1)) ==
            zc::none);
  zc::Vector<uint8_t> keyTrailing(keyBytes.size() + 1);
  keyTrailing.addAll(keyBytes.asPtr());
  keyTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableLabelKey>::decode(keyTrailing.asPtr()) == zc::none);

  auto blockBytes = StableBindingCodec<StableLabelTarget>::encode(block);
  constexpr auto targetDomain = "zom.binder.label-target"_zc;
  const auto scopeBytes = StableBindingCodec<StableScopeOwnerKey>::encode(block.scope());
  expectFrameLengthMutation<StableLabelTarget>(blockBytes.asPtr(), targetDomain.size() + 2,
                                               scopeBytes.size() - 1);

  auto factBytes = StableBindingCodec<StableLabelFact>::encode(fact);
  ZC_EXPECT(factBytes.asPtr() ==
            labelFactWire(fact.key(), fact.name(), fact.statementPath(), fact.target()).asPtr());
  constexpr auto factDomain = "zom.binder.body-label"_zc;
  const auto stableKeyBytes = StableBindingCodec<StableLabelKey>::encode(fact.key());
  identity::CanonicalEncoder nameEncoder;
  fact.name().encode(nameEncoder);
  const auto nameBytes = nameEncoder.finish();
  const auto statementPathBytes = fact.statementPath().encode();
  const auto targetBytes = StableBindingCodec<StableLabelTarget>::encode(fact.target());
  offset = factDomain.size() + 1;
  expectByteMutation<StableLabelFact>(factBytes.asPtr(), 0);
  expectFrameLengthMutation<StableLabelFact>(factBytes.asPtr(), offset, stableKeyBytes.size() - 1);
  expectByteMutation<StableLabelFact>(factBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + stableKeyBytes.size();
  expectFrameLengthMutation<StableLabelFact>(factBytes.asPtr(), offset, 0);
  expectByteMutation<StableLabelFact>(factBytes.asPtr(), offset + sizeof(uint64_t));
  offset += nameBytes.size();
  expectFrameLengthMutation<StableLabelFact>(factBytes.asPtr(), offset,
                                             statementPathBytes.size() + 1);
  expectByteMutation<StableLabelFact>(factBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + statementPathBytes.size();
  expectFrameLengthMutation<StableLabelFact>(factBytes.asPtr(), offset, targetBytes.size() - 1);
  expectByteMutation<StableLabelFact>(factBytes.asPtr(), offset + sizeof(uint64_t));

  auto foreignTarget =
      StableLabelTarget::block(StableScopeOwnerKey::body(ownerBody("foreign"_zc), localPath(2)));
  auto foreignWire = labelFactWire(key, fact.name(), fact.statementPath(), foreignTarget);
  ZC_EXPECT(StableBindingCodec<StableLabelFact>::decode(foreignWire.asPtr()) == zc::none);
  auto nonBodyTarget = StableLabelTarget::block(StableScopeOwnerKey::module(module("owner"_zc)));
  auto nonBodyWire = labelFactWire(key, fact.name(), fact.statementPath(), nonBodyTarget);
  ZC_EXPECT(StableBindingCodec<StableLabelFact>::decode(nonBodyWire.asPtr()) == zc::none);
  auto wrongStatementWire = labelFactWire(key, fact.name(), localPath(3), block);
  ZC_EXPECT(StableBindingCodec<StableLabelFact>::decode(wrongStatementWire.asPtr()) == zc::none);
  auto samePathKey = StableLabelKey::from(owner.clone(), localPath(2));
  auto samePathWire = labelFactWire(samePathKey, fact.name(), localPath(2), block);
  ZC_EXPECT(StableBindingCodec<StableLabelFact>::decode(samePathWire.asPtr()) == zc::none);

  ZC_EXPECT(StableBindingCodec<StableLabelFact>::decode(factBytes.slice(0, factBytes.size() - 1)) ==
            zc::none);
  zc::Vector<uint8_t> trailing(factBytes.size() + 1);
  trailing.addAll(factBytes.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableLabelFact>::decode(trailing.asPtr()) == zc::none);
  zc::Vector<StableLabelFact> duplicateFacts;
  duplicateFacts.add(fact.clone());
  duplicateFacts.add(fact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableLabelFact>::from(zc::mv(duplicateFacts)) ==
            zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableLabelKey>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableLabelFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.ControlTransfersMatchClosedWireAndRelations") {
  auto owner = ownerBody();
  auto explicitTarget =
      StableControlTarget::explicitLabel(StableLabelKey::from(owner.clone(), localPath(1)));
  auto loopTarget =
      StableControlTarget::loop(StableScopeOwnerKey::body(owner.clone(), localPath(2)));
  auto matchTarget =
      StableControlTarget::match(StableScopeOwnerKey::body(owner.clone(), localPath(3)));
  expectSumWires(explicitTarget, loopTarget, matchTarget);
  auto fact = require(StableControlTransferFact::from(
      owner.clone(), localPath(5), ControlTransferKind::Break, loopTarget.clone()));
  expectRoundTrip(fact);

  constexpr auto targetDomain = "zom.binder.control-target"_zc;
  auto explicitBytes = StableBindingCodec<StableControlTarget>::encode(explicitTarget);
  auto loopBytes = StableBindingCodec<StableControlTarget>::encode(loopTarget);
  auto matchBytes = StableBindingCodec<StableControlTarget>::encode(matchTarget);
  const auto labelBytes = StableBindingCodec<StableLabelKey>::encode(
      explicitTarget.value().get<StableExplicitLabelControlTarget>().label);
  const auto loopScopeBytes = StableBindingCodec<StableScopeOwnerKey>::encode(
      loopTarget.value().get<StableLoopControlTarget>().scope);
  const auto matchScopeBytes = StableBindingCodec<StableScopeOwnerKey>::encode(
      matchTarget.value().get<StableMatchControlTarget>().scope);
  expectFrameLengthMutation<StableControlTarget>(explicitBytes.asPtr(), targetDomain.size() + 2,
                                                 labelBytes.size() - 1);
  expectFrameLengthMutation<StableControlTarget>(loopBytes.asPtr(), targetDomain.size() + 2,
                                                 loopScopeBytes.size() + 1);
  expectFrameLengthMutation<StableControlTarget>(matchBytes.asPtr(), targetDomain.size() + 2,
                                                 matchScopeBytes.size() - 1);

  auto factBytes = StableBindingCodec<StableControlTransferFact>::encode(fact);
  ZC_EXPECT(factBytes.asPtr() ==
            controlTransferFactWire(fact.owner(), fact.transferPath(), fact.kind(), fact.target())
                .asPtr());
  constexpr auto factDomain = "zom.binder.body-control-transfer"_zc;
  const auto ownerBytes = StableBindingCodec<StableOwnerBodyQueryKey>::encode(owner);
  const auto pathBytes = fact.transferPath().encode();
  const auto targetBytes = StableBindingCodec<StableControlTarget>::encode(fact.target());
  size_t offset = factDomain.size() + 1;
  expectByteMutation<StableControlTransferFact>(factBytes.asPtr(), 0);
  expectFrameLengthMutation<StableControlTransferFact>(factBytes.asPtr(), offset,
                                                       ownerBytes.size() - 1);
  expectByteMutation<StableControlTransferFact>(factBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectFrameLengthMutation<StableControlTransferFact>(factBytes.asPtr(), offset,
                                                       pathBytes.size() + 1);
  expectByteMutation<StableControlTransferFact>(factBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + pathBytes.size();
  expectByteMutation<StableControlTransferFact>(factBytes.asPtr(), offset++);
  expectFrameLengthMutation<StableControlTransferFact>(factBytes.asPtr(), offset,
                                                       targetBytes.size() - 1);
  expectByteMutation<StableControlTransferFact>(factBytes.asPtr(), offset + sizeof(uint64_t));

  auto unknownKindWire = controlTransferFactWire(
      owner, fact.transferPath(), static_cast<ControlTransferKind>(0xff), loopTarget);
  ZC_EXPECT(StableBindingCodec<StableControlTransferFact>::decode(unknownKindWire.asPtr()) ==
            zc::none);
  auto continueMatchWire = controlTransferFactWire(owner, fact.transferPath(),
                                                   ControlTransferKind::Continue, matchTarget);
  ZC_EXPECT(StableBindingCodec<StableControlTransferFact>::decode(continueMatchWire.asPtr()) ==
            zc::none);
  auto foreignOwner = ownerBody("foreign"_zc);
  auto foreignExplicit =
      StableControlTarget::explicitLabel(StableLabelKey::from(foreignOwner.clone(), localPath(1)));
  auto foreignWire = controlTransferFactWire(owner, fact.transferPath(), ControlTransferKind::Break,
                                             foreignExplicit);
  ZC_EXPECT(StableBindingCodec<StableControlTransferFact>::decode(foreignWire.asPtr()) == zc::none);
  auto foreignLoop =
      StableControlTarget::loop(StableScopeOwnerKey::body(foreignOwner.clone(), localPath(2)));
  auto foreignLoopWire =
      controlTransferFactWire(owner, fact.transferPath(), ControlTransferKind::Break, foreignLoop);
  ZC_EXPECT(StableBindingCodec<StableControlTransferFact>::decode(foreignLoopWire.asPtr()) ==
            zc::none);
  auto nonBodyTarget = StableControlTarget::match(StableScopeOwnerKey::module(module("owner"_zc)));
  auto nonBodyWire = controlTransferFactWire(owner, fact.transferPath(), ControlTransferKind::Break,
                                             nonBodyTarget);
  ZC_EXPECT(StableBindingCodec<StableControlTransferFact>::decode(nonBodyWire.asPtr()) == zc::none);
  auto pathConflictWire =
      controlTransferFactWire(owner, localPath(2), ControlTransferKind::Break, loopTarget);
  ZC_EXPECT(StableBindingCodec<StableControlTransferFact>::decode(pathConflictWire.asPtr()) ==
            zc::none);
  auto labelPathConflictWire =
      controlTransferFactWire(owner, localPath(1), ControlTransferKind::Break, explicitTarget);
  ZC_EXPECT(StableBindingCodec<StableControlTransferFact>::decode(labelPathConflictWire.asPtr()) ==
            zc::none);

  ZC_EXPECT(StableBindingCodec<StableControlTransferFact>::decode(
                factBytes.slice(0, factBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> trailing(factBytes.size() + 1);
  trailing.addAll(factBytes.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableControlTransferFact>::decode(trailing.asPtr()) == zc::none);
  zc::Vector<StableControlTransferFact> duplicateFacts;
  duplicateFacts.add(fact.clone());
  duplicateFacts.add(fact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableControlTransferFact>::from(zc::mv(duplicateFacts)) ==
            zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableControlTransferFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.ClosuresMatchPopulatedCanonicalWireAndOwnership") {
  auto owner = ownerBody();
  auto closure = require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(2),
                                                      AnonymousOwnerLocalRole::Closure));
  auto closureFact = require(StableClosureFact::from(
      owner.clone(), closure.clone(), StableScopeOwnerKey::body(owner.clone(), localPath(2))));
  expectRoundTrip(closureFact);
  auto closureBytes = StableBindingCodec<StableClosureFact>::encode(closureFact);
  ZC_EXPECT(closureBytes.asPtr() == closureFactWire(closureFact).asPtr());

  zc::Vector<LocalSyntaxPath> firstReferenceValues;
  firstReferenceValues.add(localPath(4));
  firstReferenceValues.add(localPath(5));
  auto firstReferences = require(
      StableBindingSequenceBuilder<LocalSyntaxPath>::fromNonEmpty(zc::mv(firstReferenceValues)));
  auto firstVariable = StableClosureFreeVariable::from(
      StableBindingTargetKey::module(module("a"_zc)), zc::mv(firstReferences));
  zc::Vector<LocalSyntaxPath> secondReferenceValues;
  secondReferenceValues.add(localPath(6));
  auto secondReferences = require(
      StableBindingSequenceBuilder<LocalSyntaxPath>::fromNonEmpty(zc::mv(secondReferenceValues)));
  auto secondVariable = StableClosureFreeVariable::from(
      StableBindingTargetKey::module(module("b"_zc)), zc::mv(secondReferences));
  expectRoundTrip(firstVariable);
  auto variableBytes = StableBindingCodec<StableClosureFreeVariable>::encode(firstVariable);
  ZC_EXPECT(variableBytes.asPtr() == closureFreeVariableWire(firstVariable).asPtr());

  zc::Vector<StableClosureFreeVariable> variableValues;
  variableValues.add(firstVariable.clone());
  variableValues.add(secondVariable.clone());
  auto variables = require(
      StableBindingSequenceBuilder<StableClosureFreeVariable>::from(zc::mv(variableValues)));
  auto variableFact = require(
      StableClosureFreeVariableFact::from(owner.clone(), closure.clone(), zc::mv(variables)));
  expectRoundTrip(variableFact);
  auto factBytes = StableBindingCodec<StableClosureFreeVariableFact>::encode(variableFact);
  ZC_EXPECT(factBytes.asPtr() == closureFreeVariableFactWire(variableFact).asPtr());
  ZC_EXPECT(variableFact.variables().values().size() == 2);

  constexpr auto closureDomain = "zom.binder.body-closure"_zc;
  const auto ownerBytes = StableBindingCodec<StableOwnerBodyQueryKey>::encode(owner);
  const auto closureKeyBytes = closure.encode();
  const auto scopeBytes = StableBindingCodec<StableScopeOwnerKey>::encode(closureFact.scope());
  size_t offset = closureDomain.size() + 1;
  expectByteMutation<StableClosureFact>(closureBytes.asPtr(), 0);
  expectFrameLengthMutation<StableClosureFact>(closureBytes.asPtr(), offset, ownerBytes.size() - 1);
  expectByteMutation<StableClosureFact>(closureBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectFrameLengthMutation<StableClosureFact>(closureBytes.asPtr(), offset,
                                               closureKeyBytes.size() + 1);
  expectByteMutation<StableClosureFact>(closureBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + closureKeyBytes.size();
  expectFrameLengthMutation<StableClosureFact>(closureBytes.asPtr(), offset, scopeBytes.size() - 1);
  expectByteMutation<StableClosureFact>(closureBytes.asPtr(), offset + sizeof(uint64_t));

  constexpr auto variableDomain = "zom.binder.closure-free-variable"_zc;
  const auto targetBytes =
      StableBindingCodec<StableBindingTargetKey>::encode(firstVariable.target());
  const auto firstPathBytes = firstVariable.referencePaths().values()[0].encode();
  offset = variableDomain.size() + 1;
  expectByteMutation<StableClosureFreeVariable>(variableBytes.asPtr(), 0);
  expectFrameLengthMutation<StableClosureFreeVariable>(variableBytes.asPtr(), offset,
                                                       targetBytes.size() - 1);
  expectByteMutation<StableClosureFreeVariable>(variableBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + targetBytes.size();
  expectFrameLengthMutation<StableClosureFreeVariable>(variableBytes.asPtr(), offset, 0);
  expectFrameLengthMutation<StableClosureFreeVariable>(
      variableBytes.asPtr(), offset,
      stable_binding_codec_detail::kBinderSemanticSequenceRecords + 1);
  offset += sizeof(uint64_t);
  expectFrameLengthMutation<StableClosureFreeVariable>(variableBytes.asPtr(), offset,
                                                       firstPathBytes.size() + 1);
  expectByteMutation<StableClosureFreeVariable>(variableBytes.asPtr(), offset + sizeof(uint64_t));

  constexpr auto factDomain = "zom.binder.body-closure-free-variables"_zc;
  offset = factDomain.size() + 1;
  expectByteMutation<StableClosureFreeVariableFact>(factBytes.asPtr(), 0);
  expectFrameLengthMutation<StableClosureFreeVariableFact>(factBytes.asPtr(), offset,
                                                           ownerBytes.size() - 1);
  expectByteMutation<StableClosureFreeVariableFact>(factBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectFrameLengthMutation<StableClosureFreeVariableFact>(factBytes.asPtr(), offset,
                                                           closureKeyBytes.size() + 1);
  expectByteMutation<StableClosureFreeVariableFact>(factBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + closureKeyBytes.size();
  expectFrameLengthMutation<StableClosureFreeVariableFact>(
      factBytes.asPtr(), offset, stable_binding_codec_detail::kBinderSemanticSequenceRecords + 1);
  const size_t firstVariableOffset = offset + sizeof(uint64_t);
  expectFrameLengthMutation<StableClosureFreeVariableFact>(
      factBytes.asPtr(), firstVariableOffset,
      StableBindingCodec<StableClosureFreeVariable>::encode(firstVariable).size() - 1);
  expectByteMutation<StableClosureFreeVariableFact>(factBytes.asPtr(),
                                                    firstVariableOffset + sizeof(uint64_t));

  zc::Vector<StableClosureFreeVariable> duplicateValues;
  duplicateValues.add(firstVariable.clone());
  duplicateValues.add(firstVariable.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableClosureFreeVariable>::from(
                zc::mv(duplicateValues)) == zc::none);
  zc::Vector<StableClosureFreeVariable> reversedValues;
  reversedValues.add(secondVariable.clone());
  reversedValues.add(firstVariable.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableClosureFreeVariable>::from(zc::mv(reversedValues)) ==
            zc::none);
  zc::Vector<LocalSyntaxPath> duplicateReferenceWireValues;
  duplicateReferenceWireValues.add(localPath(4));
  duplicateReferenceWireValues.add(localPath(4));
  auto duplicateReferenceWire = closureFreeVariableWire(
      firstVariable.target(), duplicateReferenceWireValues.asPtr().asConst());
  ZC_EXPECT(StableBindingCodec<StableClosureFreeVariable>::decode(duplicateReferenceWire.asPtr()) ==
            zc::none);
  zc::Vector<LocalSyntaxPath> reversedReferenceWireValues;
  reversedReferenceWireValues.add(localPath(5));
  reversedReferenceWireValues.add(localPath(4));
  auto reversedReferenceWire = closureFreeVariableWire(
      firstVariable.target(), reversedReferenceWireValues.asPtr().asConst());
  ZC_EXPECT(StableBindingCodec<StableClosureFreeVariable>::decode(reversedReferenceWire.asPtr()) ==
            zc::none);

  auto foreignOwner = ownerBody("foreign"_zc);
  auto foreignKey = require(OwnerLocalBindingKey::from(
      foreignOwner.owner().clone(), localPath(7), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("captured"_zc)));
  auto foreignTarget =
      require(StableBindingTargetKey::ownerLocal(foreignOwner.clone(), zc::mv(foreignKey)));
  zc::Vector<LocalSyntaxPath> foreignReferenceValues;
  foreignReferenceValues.add(localPath(8));
  auto foreignReferences = require(
      StableBindingSequenceBuilder<LocalSyntaxPath>::fromNonEmpty(zc::mv(foreignReferenceValues)));
  zc::Vector<StableClosureFreeVariable> foreignValues;
  foreignValues.add(
      StableClosureFreeVariable::from(zc::mv(foreignTarget), zc::mv(foreignReferences)));
  auto foreignWire = closureFreeVariableFactWire(owner, closure, foreignValues.asPtr().asConst());
  ZC_EXPECT(StableBindingCodec<StableClosureFreeVariableFact>::decode(foreignWire.asPtr()) ==
            zc::none);
  auto foreignVariables =
      require(StableBindingSequenceBuilder<StableClosureFreeVariable>::from(zc::mv(foreignValues)));
  ZC_EXPECT(StableClosureFreeVariableFact::from(owner.clone(), closure.clone(),
                                                zc::mv(foreignVariables)) == zc::none);

  ZC_EXPECT(StableBindingCodec<StableClosureFact>::decode(
                closureBytes.slice(0, closureBytes.size() - 1)) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableClosureFreeVariable>::decode(
                variableBytes.slice(0, variableBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> closureTrailing(closureBytes.size() + 1);
  closureTrailing.addAll(closureBytes.asPtr());
  closureTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableClosureFact>::decode(closureTrailing.asPtr()) == zc::none);
  zc::Vector<uint8_t> variableTrailing(variableBytes.size() + 1);
  variableTrailing.addAll(variableBytes.asPtr());
  variableTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableClosureFreeVariable>::decode(variableTrailing.asPtr()) ==
            zc::none);
  ZC_EXPECT(StableBindingCodec<StableClosureFreeVariableFact>::decode(
                factBytes.slice(0, factBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> trailing(factBytes.size() + 1);
  trailing.addAll(factBytes.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableClosureFreeVariableFact>::decode(trailing.asPtr()) ==
            zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableClosureFact>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableClosureFreeVariable>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableClosureFreeVariableFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.ExplicitCapturesMatchPopulatedCanonicalWire") {
  expectClosedEnumCodec(StableExplicitCaptureMode::ByValue);
  expectClosedEnumCodec(StableExplicitCaptureMode::ByReference);
  expectClosedEnumCodec(StableExplicitCaptureMode::This);

  auto owner = ownerBody();
  auto byValue = require(StableExplicitCaptureBindingFact::from(
      localPath(4), StableBindingTargetKey::module(module("a"_zc)),
      StableExplicitCaptureMode::ByValue));
  auto byReference = require(StableExplicitCaptureBindingFact::from(
      localPath(5), StableBindingTargetKey::module(module("b"_zc)),
      StableExplicitCaptureMode::ByReference));
  auto captureThis = require(StableExplicitCaptureBindingFact::from(
      localPath(6),
      StableBindingTargetKey::callableParameter(StableCallableParameterQueryKey::from(
          module("owner"_zc), digestKey<identity::CallableParameterKey>(0x33))),
      StableExplicitCaptureMode::This));
  expectRoundTrip(byValue);
  expectRoundTrip(byReference);
  expectRoundTrip(captureThis);

  zc::Vector<StableExplicitCaptureBindingFact> captureValues;
  captureValues.add(byValue.clone());
  captureValues.add(byReference.clone());
  captureValues.add(captureThis.clone());
  auto captures = require(
      StableBindingSequenceBuilder<StableExplicitCaptureBindingFact>::from(zc::mv(captureValues)));
  auto closure = require(AnonymousOwnerLocalKey::from(owner.owner().clone(), localPath(2),
                                                      AnonymousOwnerLocalRole::FunctionExpression));
  auto fact = require(StableExplicitClosureCaptureFact::from(owner.clone(), closure.clone(),
                                                             localPath(3), zc::mv(captures)));
  auto emptyFact = require(StableExplicitClosureCaptureFact::from(
      owner.clone(), closure.clone(), localPath(3),
      CanonicalSequence<StableExplicitCaptureBindingFact>::empty()));
  expectRoundTrip(fact);
  ZC_EXPECT(fact.captures().values().size() == 3 && fact != emptyFact);

  auto bindingBytes = StableBindingCodec<StableExplicitCaptureBindingFact>::encode(byValue);
  ZC_EXPECT(
      bindingBytes.asPtr() ==
      explicitCaptureBindingWire(byValue.itemPath(), byValue.target(), byValue.mode()).asPtr());
  constexpr auto bindingDomain = "zom.binder.explicit-capture-binding"_zc;
  const auto itemPathBytes = byValue.itemPath().encode();
  const auto targetBytes = StableBindingCodec<StableBindingTargetKey>::encode(byValue.target());
  size_t offset = bindingDomain.size() + 1;
  expectByteMutation<StableExplicitCaptureBindingFact>(bindingBytes.asPtr(), 0);
  expectFrameLengthMutation<StableExplicitCaptureBindingFact>(bindingBytes.asPtr(), offset,
                                                              itemPathBytes.size() + 1);
  expectByteMutation<StableExplicitCaptureBindingFact>(bindingBytes.asPtr(),
                                                       offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + itemPathBytes.size();
  expectFrameLengthMutation<StableExplicitCaptureBindingFact>(bindingBytes.asPtr(), offset,
                                                              targetBytes.size() - 1);
  expectByteMutation<StableExplicitCaptureBindingFact>(bindingBytes.asPtr(),
                                                       offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + targetBytes.size();
  expectByteMutation<StableExplicitCaptureBindingFact>(bindingBytes.asPtr(), offset);
  auto invalidThisWire = explicitCaptureBindingWire(byValue.itemPath(), byValue.target(),
                                                    StableExplicitCaptureMode::This);
  ZC_EXPECT(StableBindingCodec<StableExplicitCaptureBindingFact>::decode(invalidThisWire.asPtr()) ==
            zc::none);

  auto factBytes = StableBindingCodec<StableExplicitClosureCaptureFact>::encode(fact);
  ZC_EXPECT(factBytes.asPtr() == explicitClosureCaptureFactWire(fact).asPtr());
  constexpr auto factDomain = "zom.binder.body-explicit-closure-capture"_zc;
  const auto ownerBytes = StableBindingCodec<StableOwnerBodyQueryKey>::encode(owner);
  const auto closureBytes = closure.encode();
  const auto listPathBytes = fact.captureListPath().encode();
  offset = factDomain.size() + 1;
  expectByteMutation<StableExplicitClosureCaptureFact>(factBytes.asPtr(), 0);
  expectFrameLengthMutation<StableExplicitClosureCaptureFact>(factBytes.asPtr(), offset,
                                                              ownerBytes.size() - 1);
  expectByteMutation<StableExplicitClosureCaptureFact>(factBytes.asPtr(),
                                                       offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectFrameLengthMutation<StableExplicitClosureCaptureFact>(factBytes.asPtr(), offset,
                                                              closureBytes.size() + 1);
  expectByteMutation<StableExplicitClosureCaptureFact>(factBytes.asPtr(),
                                                       offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + closureBytes.size();
  expectFrameLengthMutation<StableExplicitClosureCaptureFact>(factBytes.asPtr(), offset,
                                                              listPathBytes.size() - 1);
  expectByteMutation<StableExplicitClosureCaptureFact>(factBytes.asPtr(),
                                                       offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + listPathBytes.size();
  expectFrameLengthMutation<StableExplicitClosureCaptureFact>(
      factBytes.asPtr(), offset, stable_binding_codec_detail::kBinderSemanticSequenceRecords + 1);
  const size_t firstCaptureOffset = offset + sizeof(uint64_t);
  expectFrameLengthMutation<StableExplicitClosureCaptureFact>(
      factBytes.asPtr(), firstCaptureOffset,
      StableBindingCodec<StableExplicitCaptureBindingFact>::encode(byValue).size() - 1);
  expectByteMutation<StableExplicitClosureCaptureFact>(factBytes.asPtr(),
                                                       firstCaptureOffset + sizeof(uint64_t));

  zc::Vector<StableExplicitCaptureBindingFact> duplicateValues;
  duplicateValues.add(byValue.clone());
  duplicateValues.add(byValue.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableExplicitCaptureBindingFact>::from(
                zc::mv(duplicateValues)) == zc::none);
  zc::Vector<StableExplicitCaptureBindingFact> reversedValues;
  reversedValues.add(byReference.clone());
  reversedValues.add(byValue.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableExplicitCaptureBindingFact>::from(
                zc::mv(reversedValues)) == zc::none);
  zc::Vector<StableExplicitCaptureBindingFact> duplicatePathValues;
  duplicatePathValues.add(byValue.clone());
  duplicatePathValues.add(require(StableExplicitCaptureBindingFact::from(
      byValue.itemPath().clone(), StableBindingTargetKey::module(module("b"_zc)),
      StableExplicitCaptureMode::ByReference)));
  auto duplicatePathWire = explicitClosureCaptureFactWire(owner, closure, fact.captureListPath(),
                                                          duplicatePathValues.asPtr().asConst());
  ZC_EXPECT(StableBindingCodec<StableExplicitClosureCaptureFact>::decode(
                duplicatePathWire.asPtr()) == zc::none);
  zc::Vector<StableExplicitCaptureBindingFact> closurePathValues;
  closurePathValues.add(require(StableExplicitCaptureBindingFact::from(
      closure.path().clone(), byValue.target().clone(), byValue.mode())));
  auto closurePathWire = explicitClosureCaptureFactWire(owner, closure, fact.captureListPath(),
                                                        closurePathValues.asPtr().asConst());
  ZC_EXPECT(StableBindingCodec<StableExplicitClosureCaptureFact>::decode(closurePathWire.asPtr()) ==
            zc::none);
  zc::Vector<StableExplicitCaptureBindingFact> listPathValues;
  listPathValues.add(require(StableExplicitCaptureBindingFact::from(
      fact.captureListPath().clone(), byValue.target().clone(), byValue.mode())));
  auto listPathWire = explicitClosureCaptureFactWire(owner, closure, fact.captureListPath(),
                                                     listPathValues.asPtr().asConst());
  ZC_EXPECT(StableBindingCodec<StableExplicitClosureCaptureFact>::decode(listPathWire.asPtr()) ==
            zc::none);

  auto foreignOwner = ownerBody("foreign"_zc);
  auto foreignKey = require(OwnerLocalBindingKey::from(
      foreignOwner.owner().clone(), localPath(7), OwnerLocalBindingNamespace::Value,
      OwnerLocalBindingKind::Local, declaredName("captured"_zc)));
  zc::Vector<StableExplicitCaptureBindingFact> foreignValues;
  foreignValues.add(require(StableExplicitCaptureBindingFact::from(
      localPath(7),
      require(StableBindingTargetKey::ownerLocal(foreignOwner.clone(), zc::mv(foreignKey))),
      StableExplicitCaptureMode::ByValue)));
  auto foreignWire = explicitClosureCaptureFactWire(owner, closure, fact.captureListPath(),
                                                    foreignValues.asPtr().asConst());
  ZC_EXPECT(StableBindingCodec<StableExplicitClosureCaptureFact>::decode(foreignWire.asPtr()) ==
            zc::none);

  ZC_EXPECT(StableBindingCodec<StableExplicitCaptureBindingFact>::decode(
                bindingBytes.slice(0, bindingBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> bindingTrailing(bindingBytes.size() + 1);
  bindingTrailing.addAll(bindingBytes.asPtr());
  bindingTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableExplicitCaptureBindingFact>::decode(bindingTrailing.asPtr()) ==
            zc::none);
  ZC_EXPECT(StableBindingCodec<StableExplicitClosureCaptureFact>::decode(
                factBytes.slice(0, factBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> factTrailing(factBytes.size() + 1);
  factTrailing.addAll(factBytes.asPtr());
  factTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableExplicitClosureCaptureFact>::decode(factTrailing.asPtr()) ==
            zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableExplicitCaptureBindingFact>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableExplicitClosureCaptureFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.DeclarationFactsMatchWireAndRejectIndependentMutations") {
  auto declaration = require(declarationFact(DeclarationFactMutation::None));
  auto withoutVisibility = require(declarationFact(DeclarationFactMutation::WithoutVisibility));
  auto implementationRecord = alternativeImplementationRecord();
  auto implementation = identity::ImplKey::compute(implementationRecord);
  auto authority = StableImplementationQueryKey::from(module("owner"_zc), implementation.clone());
  auto occurrenceKey = require(StableImplementationOccurrenceQueryKey::from(
      module("owner"_zc), implementationOccurrence(implementation, 1)));
  auto implementationFact = require(StableImplementationOccurrenceFact::from(
      zc::mv(occurrenceKey), zc::mv(authority), zc::mv(implementationRecord),
      StableScopeOwnerKey::module(module("owner"_zc))));
  expectRoundTrip(declaration);
  expectRoundTrip(withoutVisibility);
  expectRoundTrip(implementationFact);

  constexpr auto declarationDomain = "zom.binder.skeleton-declaration"_zc;
  auto declarationBytes = StableBindingCodec<StableDeclarationFact>::encode(declaration);
  auto noVisibilityBytes = StableBindingCodec<StableDeclarationFact>::encode(withoutVisibility);
  ZC_EXPECT(declarationBytes.asPtr() == declarationFactWire(declaration).asPtr());
  ZC_EXPECT(noVisibilityBytes.asPtr() == declarationFactWire(withoutVisibility).asPtr());
  const auto queryBytes =
      StableBindingCodec<StableDefinitionQueryKey>::encode(declaration.queryKey());
  const auto recordBytes = declaration.record().encode();
  const auto scopeBytes =
      StableBindingCodec<StableScopeOwnerKey>::encode(declaration.declaringScope());
  identity::CanonicalEncoder nameEncoder;
  declaration.name().encode(nameEncoder);
  const auto nameBytes = nameEncoder.finish();
  size_t offset = declarationDomain.size() + 1;
  expectFrameLengthMutation<StableDeclarationFact>(declarationBytes.asPtr(), offset,
                                                   queryBytes.size() - 1);
  expectByteMutation<StableDeclarationFact>(declarationBytes.asPtr(),
                                            offset + sizeof(uint64_t) + queryBytes.size() - 1);
  offset += sizeof(uint64_t) + queryBytes.size();
  expectFrameLengthMutation<StableDeclarationFact>(declarationBytes.asPtr(), offset, 4194305);
  expectByteMutation<StableDeclarationFact>(declarationBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + recordBytes.size();
  expectFrameLengthMutation<StableDeclarationFact>(declarationBytes.asPtr(), offset,
                                                   scopeBytes.size() + 1);
  expectByteMutation<StableDeclarationFact>(declarationBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + scopeBytes.size();
  expectByteMutation<StableDeclarationFact>(declarationBytes.asPtr(), offset);
  expectByteMutation<StableDeclarationFact>(declarationBytes.asPtr(), offset + 1);
  expectFrameLengthMutation<StableDeclarationFact>(declarationBytes.asPtr(), offset + 2, 4097);
  expectByteMutation<StableDeclarationFact>(declarationBytes.asPtr(),
                                            offset + 2 + nameBytes.size() - 1);
  offset += 2 + nameBytes.size();
  expectByteMutation<StableDeclarationFact>(declarationBytes.asPtr(), offset);
  expectByteMutation<StableDeclarationFact>(declarationBytes.asPtr(), offset + 1);
  expectByteMutation<StableDeclarationFact>(declarationBytes.asPtr(), offset + 2);
  auto missingVisibility = zc::heapArray(noVisibilityBytes.asPtr());
  missingVisibility[missingVisibility.size() - 1] = 0x01;
  ZC_EXPECT(StableBindingCodec<StableDeclarationFact>::decode(missingVisibility.asPtr()) ==
            zc::none);

  constexpr auto implementationDomain = "zom.binder.skeleton-implementation-occurrence"_zc;
  auto implementationBytes =
      StableBindingCodec<StableImplementationOccurrenceFact>::encode(implementationFact);
  ZC_EXPECT(implementationBytes.asPtr() == implementationFactWire(implementationFact).asPtr());
  const auto occurrenceBytes = StableBindingCodec<StableImplementationOccurrenceQueryKey>::encode(
      implementationFact.occurrence());
  const auto authorityBytes =
      StableBindingCodec<StableImplementationQueryKey>::encode(implementationFact.authority());
  const auto implementationRecordBytes = implementationFact.record().encode();
  const auto implementationScopeBytes =
      StableBindingCodec<StableScopeOwnerKey>::encode(implementationFact.declaringScope());
  offset = implementationDomain.size() + 1;
  expectFrameLengthMutation<StableImplementationOccurrenceFact>(implementationBytes.asPtr(), offset,
                                                                occurrenceBytes.size() - 1);
  constexpr auto occurrenceDomain = "zom.binder.implementation-occurrence-query-key"_zc;
  const auto occurrenceModuleBytes = implementationFact.occurrence().module().encode();
  const size_t occurrenceImplementation = offset + sizeof(uint64_t) + occurrenceDomain.size() + 1 +
                                          sizeof(uint64_t) + occurrenceModuleBytes.size() +
                                          sizeof(uint64_t);
  expectByteMutation<StableImplementationOccurrenceFact>(implementationBytes.asPtr(),
                                                         occurrenceImplementation);
  offset += sizeof(uint64_t) + occurrenceBytes.size();
  expectFrameLengthMutation<StableImplementationOccurrenceFact>(implementationBytes.asPtr(), offset,
                                                                authorityBytes.size() + 1);
  expectByteMutation<StableImplementationOccurrenceFact>(
      implementationBytes.asPtr(), offset + sizeof(uint64_t) + authorityBytes.size() - 1);
  offset += sizeof(uint64_t) + authorityBytes.size();
  expectFrameLengthMutation<StableImplementationOccurrenceFact>(implementationBytes.asPtr(), offset,
                                                                4194305);
  expectByteMutation<StableImplementationOccurrenceFact>(implementationBytes.asPtr(),
                                                         offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + implementationRecordBytes.size();
  expectFrameLengthMutation<StableImplementationOccurrenceFact>(
      implementationBytes.asPtr(), offset, implementationScopeBytes.size() - 1);
  expectByteMutation<StableImplementationOccurrenceFact>(implementationBytes.asPtr(),
                                                         offset + sizeof(uint64_t));
  ZC_EXPECT(implementationScopeBytes.size() != 0);

  auto wrongDeclarationDomain = zc::heapArray(declarationBytes.asPtr());
  wrongDeclarationDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<StableDeclarationFact>::decode(wrongDeclarationDomain.asPtr()) ==
            zc::none);
  auto wrongImplementationDomain = zc::heapArray(implementationBytes.asPtr());
  wrongImplementationDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceFact>::decode(
                wrongImplementationDomain.asPtr()) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableDeclarationFact>::decode(
                declarationBytes.slice(0, declarationBytes.size() - 1)) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceFact>::decode(
                implementationBytes.slice(0, implementationBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> declarationTrailing(declarationBytes.size() + 1);
  declarationTrailing.addAll(declarationBytes.asPtr());
  declarationTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableDeclarationFact>::decode(declarationTrailing.asPtr()) ==
            zc::none);
  zc::Vector<uint8_t> implementationTrailing(implementationBytes.size() + 1);
  implementationTrailing.addAll(implementationBytes.asPtr());
  implementationTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceFact>::decode(
                implementationTrailing.asPtr()) == zc::none);

  zc::Vector<StableDeclarationFact> declarations;
  declarations.add(declaration.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableDeclarationFact>::from(zc::mv(declarations)) !=
            zc::none);
  zc::Vector<StableDeclarationFact> duplicateDeclarations;
  duplicateDeclarations.add(declaration.clone());
  duplicateDeclarations.add(declaration.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableDeclarationFact>::from(
                zc::mv(duplicateDeclarations)) == zc::none);
  zc::Vector<StableImplementationOccurrenceFact> implementations;
  implementations.add(implementationFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableImplementationOccurrenceFact>::from(
                zc::mv(implementations)) != zc::none);
  zc::Vector<StableImplementationOccurrenceFact> duplicateImplementations;
  duplicateImplementations.add(implementationFact.clone());
  duplicateImplementations.add(implementationFact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableImplementationOccurrenceFact>::from(
                zc::mv(duplicateImplementations)) == zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableDeclarationFact>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableImplementationOccurrenceFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.ParameterDeclarationsMatchWireAndRejectIndependentMutations") {
  auto generic = require(genericDeclaration(GenericDeclarationMutation::None));
  auto implementationGeneric =
      require(genericDeclaration(GenericDeclarationMutation::Implementation));
  auto callable = require(callableDeclaration(CallableDeclarationMutation::None));
  auto receiver = require(callableDeclaration(CallableDeclarationMutation::Receiver));
  expectRoundTrip(generic);
  expectRoundTrip(implementationGeneric);
  expectRoundTrip(callable);
  expectRoundTrip(receiver);

  constexpr auto genericDomain = "zom.binder.skeleton-generic-parameter-declaration"_zc;
  auto genericBytes = StableBindingCodec<StableGenericParameterDeclarationFact>::encode(generic);
  ZC_EXPECT(genericBytes.asPtr() == genericDeclarationWire(generic).asPtr());
  ZC_EXPECT(StableBindingCodec<StableGenericParameterDeclarationFact>::encode(implementationGeneric)
                .asPtr() == genericDeclarationWire(implementationGeneric).asPtr());
  const auto genericQueryBytes =
      StableBindingCodec<StableGenericParameterQueryKey>::encode(generic.queryKey());
  identity::CanonicalEncoder genericRecordEncoder;
  generic.record().encode(genericRecordEncoder);
  const auto genericRecordBytes = genericRecordEncoder.finish();
  identity::CanonicalEncoder genericSiteEncoder;
  encodeHeaderSiteOracle(genericSiteEncoder, generic.headerSite());
  const auto genericSiteBytes = genericSiteEncoder.finish();
  const auto genericScopeBytes =
      StableBindingCodec<StableScopeOwnerKey>::encode(generic.declaringScope());
  identity::CanonicalEncoder genericNameEncoder;
  generic.name().encode(genericNameEncoder);
  const auto genericNameBytes = genericNameEncoder.finish();
  size_t offset = genericDomain.size() + 1;
  expectFrameLengthMutation<StableGenericParameterDeclarationFact>(genericBytes.asPtr(), offset,
                                                                   genericQueryBytes.size() - 1);
  expectByteMutation<StableGenericParameterDeclarationFact>(
      genericBytes.asPtr(), offset + sizeof(uint64_t) + genericQueryBytes.size() - 1);
  offset += sizeof(uint64_t) + genericQueryBytes.size();
  expectByteMutation<StableGenericParameterDeclarationFact>(genericBytes.asPtr(), offset);
  offset += genericRecordBytes.size();
  expectByteMutation<StableGenericParameterDeclarationFact>(genericBytes.asPtr(), offset);
  offset += genericSiteBytes.size();
  expectFrameLengthMutation<StableGenericParameterDeclarationFact>(genericBytes.asPtr(), offset,
                                                                   genericScopeBytes.size() + 1);
  expectByteMutation<StableGenericParameterDeclarationFact>(genericBytes.asPtr(),
                                                            offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + genericScopeBytes.size();
  expectFrameLengthMutation<StableGenericParameterDeclarationFact>(genericBytes.asPtr(), offset,
                                                                   4097);
  expectByteMutation<StableGenericParameterDeclarationFact>(genericBytes.asPtr(),
                                                            offset + genericNameBytes.size() - 1);

  constexpr auto callableDomain = "zom.binder.skeleton-callable-parameter-declaration"_zc;
  auto callableBytes = StableBindingCodec<StableCallableParameterDeclarationFact>::encode(callable);
  auto receiverBytes = StableBindingCodec<StableCallableParameterDeclarationFact>::encode(receiver);
  ZC_EXPECT(callableBytes.asPtr() == callableDeclarationWire(callable).asPtr());
  ZC_EXPECT(receiverBytes.asPtr() == callableDeclarationWire(receiver).asPtr());
  const auto callableQueryBytes =
      StableBindingCodec<StableCallableParameterQueryKey>::encode(callable.queryKey());
  identity::CanonicalEncoder callableRecordEncoder;
  callable.record().encode(callableRecordEncoder);
  const auto callableRecordBytes = callableRecordEncoder.finish();
  identity::CanonicalEncoder callableSiteEncoder;
  encodeHeaderSiteOracle(callableSiteEncoder, callable.headerSite());
  const auto callableSiteBytes = callableSiteEncoder.finish();
  const auto callableScopeBytes =
      StableBindingCodec<StableScopeOwnerKey>::encode(callable.declaringScope());
  identity::CanonicalEncoder callableNameEncoder;
  ZC_ASSERT_NONNULL(callable.name()).encode(callableNameEncoder);
  const auto callableNameBytes = callableNameEncoder.finish();
  offset = callableDomain.size() + 1;
  expectFrameLengthMutation<StableCallableParameterDeclarationFact>(callableBytes.asPtr(), offset,
                                                                    callableQueryBytes.size() - 1);
  expectByteMutation<StableCallableParameterDeclarationFact>(callableBytes.asPtr(),
                                                             offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + callableQueryBytes.size();
  expectByteMutation<StableCallableParameterDeclarationFact>(callableBytes.asPtr(), offset);
  offset += callableRecordBytes.size();
  expectByteMutation<StableCallableParameterDeclarationFact>(callableBytes.asPtr(), offset);
  offset += callableSiteBytes.size();
  expectFrameLengthMutation<StableCallableParameterDeclarationFact>(callableBytes.asPtr(), offset,
                                                                    callableScopeBytes.size() + 1);
  expectByteMutation<StableCallableParameterDeclarationFact>(callableBytes.asPtr(),
                                                             offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + callableScopeBytes.size();
  expectByteMutation<StableCallableParameterDeclarationFact>(callableBytes.asPtr(), offset);
  expectFrameLengthMutation<StableCallableParameterDeclarationFact>(callableBytes.asPtr(),
                                                                    offset + 1, 4097);
  expectByteMutation<StableCallableParameterDeclarationFact>(callableBytes.asPtr(),
                                                             offset + callableNameBytes.size());
  auto missingName = zc::heapArray(receiverBytes.asPtr());
  missingName[missingName.size() - 1] = 0x01;
  ZC_EXPECT(StableBindingCodec<StableCallableParameterDeclarationFact>::decode(
                missingName.asPtr()) == zc::none);

  auto wrongGenericDomain = zc::heapArray(genericBytes.asPtr());
  wrongGenericDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<StableGenericParameterDeclarationFact>::decode(
                wrongGenericDomain.asPtr()) == zc::none);
  auto wrongCallableDomain = zc::heapArray(callableBytes.asPtr());
  wrongCallableDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<StableCallableParameterDeclarationFact>::decode(
                wrongCallableDomain.asPtr()) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableGenericParameterDeclarationFact>::decode(
                genericBytes.slice(0, genericBytes.size() - 1)) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableCallableParameterDeclarationFact>::decode(
                callableBytes.slice(0, callableBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> genericTrailing(genericBytes.size() + 1);
  genericTrailing.addAll(genericBytes.asPtr());
  genericTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableGenericParameterDeclarationFact>::decode(
                genericTrailing.asPtr()) == zc::none);
  zc::Vector<uint8_t> callableTrailing(callableBytes.size() + 1);
  callableTrailing.addAll(callableBytes.asPtr());
  callableTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableCallableParameterDeclarationFact>::decode(
                callableTrailing.asPtr()) == zc::none);

  zc::Vector<StableGenericParameterDeclarationFact> genericValues;
  genericValues.add(generic.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableGenericParameterDeclarationFact>::from(
                zc::mv(genericValues)) != zc::none);
  zc::Vector<StableGenericParameterDeclarationFact> duplicateGenerics;
  duplicateGenerics.add(generic.clone());
  duplicateGenerics.add(generic.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableGenericParameterDeclarationFact>::from(
                zc::mv(duplicateGenerics)) == zc::none);
  zc::Vector<StableCallableParameterDeclarationFact> callableValues;
  callableValues.add(callable.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableCallableParameterDeclarationFact>::from(
                zc::mv(callableValues)) != zc::none);
  zc::Vector<StableCallableParameterDeclarationFact> duplicateCallables;
  duplicateCallables.add(callable.clone());
  duplicateCallables.add(callable.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableCallableParameterDeclarationFact>::from(
                zc::mv(duplicateCallables)) == zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableGenericParameterDeclarationFact>::decode(oversized) ==
            zc::none);
  ZC_EXPECT(StableBindingCodec<StableCallableParameterDeclarationFact>::decode(oversized) ==
            zc::none);
}

ZC_TEST("StableBindingCodec.ImportAndAliasFactsMatchWireAndRejectIndependentMutations") {
  auto imported = require(importFact(ImportFactMutation::None));
  auto typed = require(importFact(ImportFactMutation::TypeNamespace));
  auto prelude = require(importFact(ImportFactMutation::Prelude));
  auto noVisibility = require(importFact(ImportFactMutation::NoVisibility));
  auto privateVisibility = require(importFact(ImportFactMutation::PrivateVisibility));
  auto reexport = require(importFact(ImportFactMutation::Reexport));
  auto alias = require(moduleAliasFact(ModuleAliasMutation::None));
  expectRoundTrip(imported);
  expectRoundTrip(typed);
  expectRoundTrip(prelude);
  expectRoundTrip(noVisibility);
  expectRoundTrip(privateVisibility);
  expectRoundTrip(reexport);
  expectRoundTrip(alias);

  constexpr auto importDomain = "zom.binder.skeleton-import"_zc;
  auto importBytes = StableBindingCodec<StableImportFact>::encode(imported);
  auto noVisibilityBytes = StableBindingCodec<StableImportFact>::encode(noVisibility);
  auto reexportBytes = StableBindingCodec<StableImportFact>::encode(reexport);
  ZC_EXPECT(importBytes.asPtr() == importFactWire(imported).asPtr());
  ZC_EXPECT(noVisibilityBytes.asPtr() == importFactWire(noVisibility).asPtr());
  ZC_EXPECT(reexportBytes.asPtr() == importFactWire(reexport).asPtr());
  const auto queryBytes =
      StableBindingCodec<StableSemanticImportQueryKey>::encode(imported.queryKey());
  const auto scopeBytes =
      StableBindingCodec<StableScopeOwnerKey>::encode(imported.declaringScope());
  const auto targetBytes = StableBindingCodec<StableBindingTargetKey>::encode(imported.target());
  const auto canonicalTargetBytes =
      StableBindingCodec<StableBindingTargetKey>::encode(imported.canonicalTarget());
  size_t offset = importDomain.size() + 1;
  expectFrameLengthMutation<StableImportFact>(importBytes.asPtr(), offset, queryBytes.size() - 1);
  expectByteMutation<StableImportFact>(importBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + queryBytes.size();
  expectFrameLengthMutation<StableImportFact>(importBytes.asPtr(), offset, scopeBytes.size() + 1);
  expectByteMutation<StableImportFact>(importBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + scopeBytes.size();
  expectFrameLengthMutation<StableImportFact>(importBytes.asPtr(), offset, targetBytes.size() - 1);
  expectByteMutation<StableImportFact>(importBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + targetBytes.size();
  expectFrameLengthMutation<StableImportFact>(importBytes.asPtr(), offset,
                                              canonicalTargetBytes.size() + 1);
  expectByteMutation<StableImportFact>(importBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + canonicalTargetBytes.size();
  expectByteMutation<StableImportFact>(importBytes.asPtr(), offset);
  expectByteMutation<StableImportFact>(importBytes.asPtr(), offset + 1);
  expectByteMutation<StableImportFact>(importBytes.asPtr(), offset + 2);
  expectByteMutation<StableImportFact>(importBytes.asPtr(), offset + 3);
  expectByteMutation<StableImportFact>(importBytes.asPtr(), offset + 4);
  auto missingVisibility = zc::heapArray(noVisibilityBytes.asPtr());
  missingVisibility[missingVisibility.size() - 2] = 0x01;
  ZC_EXPECT(StableBindingCodec<StableImportFact>::decode(missingVisibility.asPtr()) == zc::none);
  expectByteMutation<StableImportFact>(reexportBytes.asPtr(), reexportBytes.size() - 1);

  constexpr auto aliasDomain = "zom.binder.skeleton-module-alias"_zc;
  auto aliasBytes = StableBindingCodec<StableModuleAliasFact>::encode(alias);
  ZC_EXPECT(aliasBytes.asPtr() == moduleAliasFactWire(alias).asPtr());
  const auto aliasQueryBytes =
      StableBindingCodec<StableSemanticImportQueryKey>::encode(alias.queryKey());
  const auto aliasScopeBytes =
      StableBindingCodec<StableScopeOwnerKey>::encode(alias.declaringScope());
  const auto definitionBytes = StableBindingCodec<StableDefinitionQueryKey>::encode(alias.alias());
  const auto moduleBytes = alias.canonicalModule().encode();
  offset = aliasDomain.size() + 1;
  expectFrameLengthMutation<StableModuleAliasFact>(aliasBytes.asPtr(), offset,
                                                   aliasQueryBytes.size() - 1);
  expectByteMutation<StableModuleAliasFact>(aliasBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + aliasQueryBytes.size();
  expectFrameLengthMutation<StableModuleAliasFact>(aliasBytes.asPtr(), offset,
                                                   aliasScopeBytes.size() + 1);
  expectByteMutation<StableModuleAliasFact>(aliasBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + aliasScopeBytes.size();
  expectFrameLengthMutation<StableModuleAliasFact>(aliasBytes.asPtr(), offset,
                                                   definitionBytes.size() - 1);
  expectByteMutation<StableModuleAliasFact>(aliasBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + definitionBytes.size();
  expectFrameLengthMutation<StableModuleAliasFact>(aliasBytes.asPtr(), offset,
                                                   moduleBytes.size() + 1);
  expectByteMutation<StableModuleAliasFact>(aliasBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + moduleBytes.size();
  auto otherRevision = zc::heapArray(aliasBytes.asPtr());
  otherRevision[offset] ^= 0x01;
  auto decodedRevision = StableBindingCodec<StableModuleAliasFact>::decode(otherRevision.asPtr());
  ZC_EXPECT(decodedRevision != zc::none &&
            ZC_ASSERT_NONNULL(decodedRevision).targetExportNamesRevision().digest() !=
                alias.targetExportNamesRevision().digest());

  auto wrongImportDomain = zc::heapArray(importBytes.asPtr());
  wrongImportDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<StableImportFact>::decode(wrongImportDomain.asPtr()) == zc::none);
  auto wrongAliasDomain = zc::heapArray(aliasBytes.asPtr());
  wrongAliasDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<StableModuleAliasFact>::decode(wrongAliasDomain.asPtr()) ==
            zc::none);
  ZC_EXPECT(StableBindingCodec<StableImportFact>::decode(
                importBytes.slice(0, importBytes.size() - 1)) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableModuleAliasFact>::decode(
                aliasBytes.slice(0, aliasBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> importTrailing(importBytes.size() + 1);
  importTrailing.addAll(importBytes.asPtr());
  importTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableImportFact>::decode(importTrailing.asPtr()) == zc::none);
  zc::Vector<uint8_t> aliasTrailing(aliasBytes.size() + 1);
  aliasTrailing.addAll(aliasBytes.asPtr());
  aliasTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableModuleAliasFact>::decode(aliasTrailing.asPtr()) == zc::none);

  zc::Vector<StableImportFact> imports;
  imports.add(imported.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableImportFact>::from(zc::mv(imports)) != zc::none);
  zc::Vector<StableImportFact> duplicateImports;
  duplicateImports.add(imported.clone());
  duplicateImports.add(imported.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableImportFact>::from(zc::mv(duplicateImports)) ==
            zc::none);
  zc::Vector<StableModuleAliasFact> aliases;
  aliases.add(alias.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableModuleAliasFact>::from(zc::mv(aliases)) != zc::none);
  zc::Vector<StableModuleAliasFact> duplicateAliases;
  duplicateAliases.add(alias.clone());
  duplicateAliases.add(alias.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableModuleAliasFact>::from(zc::mv(duplicateAliases)) ==
            zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableImportFact>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableModuleAliasFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.ExportFactsMatchWireAndRejectIndependentMutations") {
  auto step = reexportStep(ReexportStepMutation::None);
  auto localExport = require(localExportFact(LocalExportMutation::None));
  auto noVisibility = require(localExportFact(LocalExportMutation::NoVisibility));
  auto reexport = require(localExportFact(LocalExportMutation::ReexportChain));
  expectRoundTrip(step);
  expectRoundTrip(localExport);
  expectRoundTrip(noVisibility);
  expectRoundTrip(reexport);

  constexpr auto stepDomain = "zom.binder.skeleton-reexport-step"_zc;
  auto stepBytes = StableBindingCodec<StableReexportStep>::encode(step);
  ZC_EXPECT(stepBytes.asPtr() == reexportStepWire(step).asPtr());
  const auto stepModuleBytes = step.module().encode();
  const auto stepPathBytes = step.exportPath().encode();
  const auto stepBindingBytes = StableBindingCodec<StableBindingTargetKey>::encode(step.binding());
  const auto stepCanonicalBytes =
      StableBindingCodec<StableBindingTargetKey>::encode(step.canonicalTarget());
  size_t offset = stepDomain.size() + 1;
  expectFrameLengthMutation<StableReexportStep>(stepBytes.asPtr(), offset,
                                                stepModuleBytes.size() - 1);
  expectByteMutation<StableReexportStep>(stepBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + stepModuleBytes.size();
  expectFrameLengthMutation<StableReexportStep>(stepBytes.asPtr(), offset,
                                                stepPathBytes.size() + 1);
  expectByteMutation<StableReexportStep>(stepBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + stepPathBytes.size();
  expectFrameLengthMutation<StableReexportStep>(stepBytes.asPtr(), offset,
                                                stepBindingBytes.size() - 1);
  expectByteMutation<StableReexportStep>(stepBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + stepBindingBytes.size();
  expectFrameLengthMutation<StableReexportStep>(stepBytes.asPtr(), offset,
                                                stepCanonicalBytes.size() + 1);
  expectByteMutation<StableReexportStep>(stepBytes.asPtr(), offset + sizeof(uint64_t));

  constexpr auto exportDomain = "zom.binder.skeleton-local-export"_zc;
  auto exportBytes = StableBindingCodec<StableLocalExportFact>::encode(localExport);
  auto noVisibilityBytes = StableBindingCodec<StableLocalExportFact>::encode(noVisibility);
  auto reexportBytes = StableBindingCodec<StableLocalExportFact>::encode(reexport);
  ZC_EXPECT(exportBytes.asPtr() == localExportFactWire(localExport).asPtr());
  ZC_EXPECT(noVisibilityBytes.asPtr() == localExportFactWire(noVisibility).asPtr());
  ZC_EXPECT(reexportBytes.asPtr() == localExportFactWire(reexport).asPtr());
  const auto moduleBytes = localExport.declaringModule().encode();
  const auto pathBytes = localExport.exportPath().encode();
  identity::CanonicalEncoder nameEncoder;
  localExport.name().name().encode(nameEncoder);
  const auto nameBytes = nameEncoder.finish();
  const auto bindingBytes =
      StableBindingCodec<StableBindingTargetKey>::encode(localExport.binding());
  const auto canonicalBytes =
      StableBindingCodec<StableBindingTargetKey>::encode(localExport.canonicalTarget());
  offset = exportDomain.size() + 1;
  expectFrameLengthMutation<StableLocalExportFact>(exportBytes.asPtr(), offset,
                                                   moduleBytes.size() - 1);
  expectByteMutation<StableLocalExportFact>(exportBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + moduleBytes.size();
  expectFrameLengthMutation<StableLocalExportFact>(exportBytes.asPtr(), offset,
                                                   pathBytes.size() + 1);
  expectByteMutation<StableLocalExportFact>(exportBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + pathBytes.size();
  expectByteMutation<StableLocalExportFact>(exportBytes.asPtr(), offset);
  ++offset;
  expectFrameLengthMutation<StableLocalExportFact>(exportBytes.asPtr(), offset, 4097);
  offset += nameBytes.size();
  expectFrameLengthMutation<StableLocalExportFact>(exportBytes.asPtr(), offset,
                                                   bindingBytes.size() - 1);
  expectByteMutation<StableLocalExportFact>(exportBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + bindingBytes.size();
  expectFrameLengthMutation<StableLocalExportFact>(exportBytes.asPtr(), offset,
                                                   canonicalBytes.size() + 1);
  expectByteMutation<StableLocalExportFact>(exportBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + canonicalBytes.size();
  expectByteMutation<StableLocalExportFact>(exportBytes.asPtr(), offset);
  expectByteMutation<StableLocalExportFact>(exportBytes.asPtr(), offset + 1);
  auto missingVisibility = zc::heapArray(noVisibilityBytes.asPtr());
  missingVisibility[missingVisibility.size() - sizeof(uint64_t) - 1] = 0x01;
  ZC_EXPECT(StableBindingCodec<StableLocalExportFact>::decode(missingVisibility.asPtr()) ==
            zc::none);
  auto wrongSequenceCount = zc::heapArray(reexportBytes.asPtr());
  const size_t sequenceOffset =
      reexportBytes.size() - sizeof(uint64_t) - sizeof(uint64_t) - stepBytes.size();
  for (size_t index = 0; index < sizeof(uint64_t); ++index) {
    wrongSequenceCount[sequenceOffset + index] = 0x00;
  }
  ZC_EXPECT(StableBindingCodec<StableLocalExportFact>::decode(wrongSequenceCount.asPtr()) ==
            zc::none);
  expectFrameLengthMutation<StableLocalExportFact>(
      reexportBytes.asPtr(), sequenceOffset + sizeof(uint64_t), stepBytes.size() - 1);
  expectByteMutation<StableLocalExportFact>(reexportBytes.asPtr(),
                                            sequenceOffset + sizeof(uint64_t) * 2);

  auto wrongStepDomain = zc::heapArray(stepBytes.asPtr());
  wrongStepDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<StableReexportStep>::decode(wrongStepDomain.asPtr()) == zc::none);
  auto wrongExportDomain = zc::heapArray(exportBytes.asPtr());
  wrongExportDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<StableLocalExportFact>::decode(wrongExportDomain.asPtr()) ==
            zc::none);
  ZC_EXPECT(StableBindingCodec<StableReexportStep>::decode(
                stepBytes.slice(0, stepBytes.size() - 1)) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableLocalExportFact>::decode(
                exportBytes.slice(0, exportBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> stepTrailing(stepBytes.size() + 1);
  stepTrailing.addAll(stepBytes.asPtr());
  stepTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableReexportStep>::decode(stepTrailing.asPtr()) == zc::none);
  zc::Vector<uint8_t> exportTrailing(exportBytes.size() + 1);
  exportTrailing.addAll(exportBytes.asPtr());
  exportTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableLocalExportFact>::decode(exportTrailing.asPtr()) == zc::none);

  zc::Vector<StableReexportStep> steps;
  steps.add(step.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableReexportStep>::from(zc::mv(steps)) != zc::none);
  zc::Vector<StableReexportStep> duplicateSteps;
  duplicateSteps.add(step.clone());
  duplicateSteps.add(step.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableReexportStep>::from(zc::mv(duplicateSteps)) ==
            zc::none);
  zc::Vector<StableLocalExportFact> exports;
  exports.add(localExport.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableLocalExportFact>::from(zc::mv(exports)) != zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableReexportStep>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableLocalExportFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.FailedLookupsMatchWireAndRejectIndependentMutations") {
  constexpr Namespace namespaces[] = {Namespace::Value, Namespace::Type, Namespace::Module,
                                      Namespace::Label, Namespace::Attribute};
  for (const auto value : namespaces) { expectClosedEnumCodec(value); }

  auto missing = StableFailedLookupOutcome::missing();
  auto mismatch = namespaceMismatchLookupOutcome();
  auto ambiguous = ambiguousLookupOutcome();
  expectRoundTrip(missing);
  expectRoundTrip(mismatch);
  expectRoundTrip(ambiguous);
  auto missingBytes = StableBindingCodec<StableFailedLookupOutcome>::encode(missing);
  auto mismatchBytes = StableBindingCodec<StableFailedLookupOutcome>::encode(mismatch);
  auto ambiguousBytes = StableBindingCodec<StableFailedLookupOutcome>::encode(ambiguous);
  ZC_EXPECT(missingBytes.asPtr() == failedLookupOutcomeWire(missing).asPtr());
  ZC_EXPECT(mismatchBytes.asPtr() == failedLookupOutcomeWire(mismatch).asPtr());
  ZC_EXPECT(ambiguousBytes.asPtr() == failedLookupOutcomeWire(ambiguous).asPtr());

  constexpr auto outcomeDomain = "zom.binder.failed-lookup-outcome"_zc;
  expectByteMutation<StableFailedLookupOutcome>(missingBytes.asPtr(), outcomeDomain.size() + 1);
  auto emptyMismatch = zc::heapArray(mismatchBytes.asPtr());
  const size_t mismatchCountOffset = outcomeDomain.size() + 2;
  for (size_t index = 0; index < sizeof(uint64_t); ++index) {
    emptyMismatch[mismatchCountOffset + index] = 0x00;
  }
  ZC_EXPECT(StableBindingCodec<StableFailedLookupOutcome>::decode(emptyMismatch.asPtr()) ==
            zc::none);
  expectFrameLengthMutation<StableFailedLookupOutcome>(mismatchBytes.asPtr(),
                                                       mismatchCountOffset + sizeof(uint64_t), 2);
  expectFrameLengthMutation<StableFailedLookupOutcome>(
      mismatchBytes.asPtr(), mismatchCountOffset,
      stable_binding_codec_detail::kBinderSemanticSequenceRecords + 1);
  expectByteMutation<StableFailedLookupOutcome>(mismatchBytes.asPtr(),
                                                mismatchCountOffset + sizeof(uint64_t) * 2);
  auto oneCandidate = zc::heapArray(ambiguousBytes.asPtr());
  const size_t ambiguousCountOffset = outcomeDomain.size() + 2;
  for (size_t index = 0; index < sizeof(uint64_t); ++index) {
    oneCandidate[ambiguousCountOffset + index] =
        static_cast<uint8_t>(uint64_t{1} >> ((sizeof(uint64_t) - index - 1) * 8));
  }
  ZC_EXPECT(StableBindingCodec<StableFailedLookupOutcome>::decode(oneCandidate.asPtr()) ==
            zc::none);
  expectFrameLengthMutation<StableFailedLookupOutcome>(ambiguousBytes.asPtr(),
                                                       ambiguousCountOffset + sizeof(uint64_t), 1);
  expectFrameLengthMutation<StableFailedLookupOutcome>(
      ambiguousBytes.asPtr(), ambiguousCountOffset,
      stable_binding_codec_detail::kAmbiguityCandidates + 1);

  auto fact = require(failedLookupFact(FailedLookupMutation::NamespaceMismatch));
  auto ambiguousFact = require(failedLookupFact(FailedLookupMutation::Outcome));
  expectRoundTrip(fact);
  expectRoundTrip(ambiguousFact);
  constexpr auto factDomain = "zom.binder.failed-lookup"_zc;
  auto factBytes = StableBindingCodec<StableFailedLookupFact>::encode(fact);
  ZC_EXPECT(factBytes.asPtr() == failedLookupFactWire(fact).asPtr());
  const auto ownerBytes = StableBindingCodec<BinderQueryOwner>::encode(fact.owner());
  const auto pathBytes = fact.usePath().encode();
  identity::CanonicalEncoder nameEncoder;
  fact.name().encode(nameEncoder);
  const auto nameBytes = nameEncoder.finish();
  const auto outcomeBytes = StableBindingCodec<StableFailedLookupOutcome>::encode(fact.outcome());
  size_t offset = factDomain.size() + 1;
  expectFrameLengthMutation<StableFailedLookupFact>(factBytes.asPtr(), offset,
                                                    ownerBytes.size() - 1);
  expectByteMutation<StableFailedLookupFact>(factBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + ownerBytes.size();
  expectFrameLengthMutation<StableFailedLookupFact>(factBytes.asPtr(), offset,
                                                    pathBytes.size() + 1);
  expectByteMutation<StableFailedLookupFact>(factBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + pathBytes.size();
  auto conflictingNamespace = zc::heapArray(factBytes.asPtr());
  conflictingNamespace[offset] = static_cast<uint8_t>(Namespace::Type);
  ZC_EXPECT(StableBindingCodec<StableFailedLookupFact>::decode(conflictingNamespace.asPtr()) ==
            zc::none);
  expectByteMutation<StableFailedLookupFact>(factBytes.asPtr(), offset);
  ++offset;
  expectFrameLengthMutation<StableFailedLookupFact>(factBytes.asPtr(), offset, 4097);
  offset += nameBytes.size();
  expectFrameLengthMutation<StableFailedLookupFact>(factBytes.asPtr(), offset,
                                                    outcomeBytes.size() - 1);
  expectByteMutation<StableFailedLookupFact>(factBytes.asPtr(), offset + sizeof(uint64_t));

  auto wrongOutcomeDomain = zc::heapArray(missingBytes.asPtr());
  wrongOutcomeDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<StableFailedLookupOutcome>::decode(wrongOutcomeDomain.asPtr()) ==
            zc::none);
  auto wrongFactDomain = zc::heapArray(factBytes.asPtr());
  wrongFactDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<StableFailedLookupFact>::decode(wrongFactDomain.asPtr()) ==
            zc::none);
  ZC_EXPECT(StableBindingCodec<StableFailedLookupOutcome>::decode(
                missingBytes.slice(0, missingBytes.size() - 1)) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableFailedLookupFact>::decode(
                factBytes.slice(0, factBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> outcomeTrailing(missingBytes.size() + 1);
  outcomeTrailing.addAll(missingBytes.asPtr());
  outcomeTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableFailedLookupOutcome>::decode(outcomeTrailing.asPtr()) ==
            zc::none);
  zc::Vector<uint8_t> factTrailing(factBytes.size() + 1);
  factTrailing.addAll(factBytes.asPtr());
  factTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<StableFailedLookupFact>::decode(factTrailing.asPtr()) == zc::none);

  zc::Vector<Namespace> orderedNamespaces;
  orderedNamespaces.add(Namespace::Value);
  orderedNamespaces.add(Namespace::Type);
  ZC_EXPECT(StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(orderedNamespaces)) !=
            zc::none);
  zc::Vector<Namespace> duplicateNamespaces;
  duplicateNamespaces.add(Namespace::Value);
  duplicateNamespaces.add(Namespace::Value);
  ZC_EXPECT(StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(duplicateNamespaces)) ==
            zc::none);
  zc::Vector<Namespace> reversedNamespaces;
  reversedNamespaces.add(Namespace::Type);
  reversedNamespaces.add(Namespace::Value);
  ZC_EXPECT(StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(reversedNamespaces)) ==
            zc::none);
  zc::Vector<Namespace> invalidNamespaces;
  invalidNamespaces.add(static_cast<Namespace>(0xff));
  ZC_EXPECT(StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(invalidNamespaces)) ==
            zc::none);
  const auto& candidates =
      ambiguous.value().get<StableAmbiguousLookupOutcome>().candidates.values();
  zc::Vector<StableBindingTargetKey> duplicateCandidates;
  duplicateCandidates.add(candidates[0].clone());
  duplicateCandidates.add(candidates[0].clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableBindingTargetKey>::fromNonEmpty(
                zc::mv(duplicateCandidates)) == zc::none);
  zc::Vector<StableBindingTargetKey> reversedCandidates;
  reversedCandidates.add(candidates[1].clone());
  reversedCandidates.add(candidates[0].clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableBindingTargetKey>::fromNonEmpty(
                zc::mv(reversedCandidates)) == zc::none);
  zc::Vector<StableFailedLookupFact> facts;
  facts.add(fact.clone());
  ZC_EXPECT(StableBindingSequenceBuilder<StableFailedLookupFact>::from(zc::mv(facts)) != zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<StableFailedLookupOutcome>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<StableFailedLookupFact>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.OwnerBodyMatchesIndependentAggregateWire") {
  auto body = populatedOwnerBody();
  auto encoded = StableBindingCodec<BoundOwnerBody>::encode(body);
  ZC_EXPECT(encoded.asPtr() == ownerBodyAggregateWire(body).asPtr());
  expectRoundTrip(body);

  constexpr auto domain = "zom.binder.owner-body"_zc;
  const auto ownerBytes = ownerBodyWire(body.owner());
  size_t offset = domain.size() + 1;
  expectFrameLengthMutation<BoundOwnerBody>(encoded.asPtr(), offset, ownerBytes.size() - 1);
  expectByteMutation<BoundOwnerBody>(encoded.asPtr(), offset + sizeof(uint64_t));
  auto foreignOwnership = zc::heapArray(encoded.asPtr());
  const auto foreignOwner = ownerBodyWire(foreignOwnerBody(body.owner()));
  ZC_REQUIRE(foreignOwner.size() == ownerBytes.size());
  foreignOwnership.slice(offset + sizeof(uint64_t), offset + sizeof(uint64_t) + foreignOwner.size())
      .copyFrom(foreignOwner.asPtr());
  ZC_EXPECT(StableBindingCodec<BoundOwnerBody>::decode(foreignOwnership.asPtr()) == zc::none);
  offset += sizeof(uint64_t) + ownerBytes.size();

  zc::Vector<zc::Array<uint8_t>> sequences;
  sequences.add(aggregateSequenceWire(body.scopes(), [](const StableBodyScopeFact& fact) {
    return bodyScopeFactWire(fact.owner(), fact.scope(), fact.parent(), fact.kind());
  }));
  sequences.add(aggregateSequenceWire(body.nodeScopes(), [](const StableBodyNodeScopeFact& fact) {
    return bodyNodeScopeFactWire(fact.owner(), fact.nodePath(), fact.scope());
  }));
  sequences.add(aggregateSequenceWire(body.bindings(), [](const StableOwnerLocalBindingFact& fact) {
    return ownerLocalBindingFactWire(fact.owner(), fact.key(), fact.kind(), fact.name(),
                                     fact.nameSpace(), fact.declaringScope(), fact.activation());
  }));
  sequences.add(aggregateSequenceWire(body.resolutions(), [](const StableResolutionFact& fact) {
    return resolutionFactWire(fact.owner(), fact.usePath(), fact.nameSpace(), fact.binding(),
                              fact.canonicalTarget(), fact.origin());
  }));
  sequences.add(
      aggregateSequenceWire(body.deferredMembers(), [](const StableDeferredMemberFact& fact) {
        return deferredMemberFactWire(fact.owner(), fact.usePath(), fact.basePath(),
                                      fact.accessKind(), fact.member(), fact.expectedNamespaces(),
                                      fact.genericArgumentPaths());
      }));
  sequences.add(aggregateSequenceWire(body.selfTypes(), [](const StableSelfTypeFact& fact) {
    return selfTypeFactWire(fact.owner(), fact.syntaxPath(), fact.selfOwner());
  }));
  sequences.add(aggregateSequenceWire(body.thisBindings(), [](const StableThisBindingFact& fact) {
    return thisBindingFactWire(fact.owner(), fact.expressionPath(), fact.receiver());
  }));
  sequences.add(aggregateSequenceWire(body.shadowTargets(), [](const StableShadowTargetFact& fact) {
    return shadowTargetFactWire(fact.owner(), fact.binding(), fact.shadowed());
  }));
  sequences.add(aggregateSequenceWire(body.labels(), [](const StableLabelFact& fact) {
    return labelFactWire(fact.key(), fact.name(), fact.statementPath(), fact.target());
  }));
  sequences.add(
      aggregateSequenceWire(body.controlTransfers(), [](const StableControlTransferFact& fact) {
        return controlTransferFactWire(fact.owner(), fact.transferPath(), fact.kind(),
                                       fact.target());
      }));
  sequences.add(aggregateSequenceWire(
      body.closures(), [](const StableClosureFact& fact) { return closureFactWire(fact); }));
  sequences.add(aggregateSequenceWire(
      body.closureFreeVariables(),
      [](const StableClosureFreeVariableFact& fact) { return closureFreeVariableFactWire(fact); }));
  sequences.add(aggregateSequenceWire(body.explicitClosureCaptures(),
                                      [](const StableExplicitClosureCaptureFact& fact) {
                                        return explicitClosureCaptureFactWire(fact);
                                      }));
  sequences.add(aggregateSequenceWire(body.failedLookups(), [](const StableFailedLookupFact& fact) {
    return failedLookupFactWire(fact);
  }));
  for (const auto& sequence : sequences) {
    expectByteMutation<BoundOwnerBody>(encoded.asPtr(), offset);
    expectFrameLengthMutation<BoundOwnerBody>(encoded.asPtr(), offset + sizeof(uint64_t), 0);
    expectByteMutation<BoundOwnerBody>(encoded.asPtr(), offset + 2 * sizeof(uint64_t));
    offset += sequence.size();
  }
  ZC_EXPECT(offset == encoded.size());
  ZC_EXPECT(StableBindingCodec<BoundOwnerBody>::decode(
                ownerBodyAggregateWire(body, true).asPtr()) == zc::none);

  auto wrongDomain = zc::heapArray(encoded.asPtr());
  wrongDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<BoundOwnerBody>::decode(wrongDomain.asPtr()) == zc::none);
  ZC_EXPECT(StableBindingCodec<BoundOwnerBody>::decode(encoded.slice(0, encoded.size() - 1)) ==
            zc::none);
  zc::Vector<uint8_t> trailing(encoded.size() + 1);
  trailing.addAll(encoded.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<BoundOwnerBody>::decode(trailing.asPtr()) == zc::none);
  uint8_t oversizedBacking = 0;
  ZC_EXPECT(StableBindingCodec<BoundOwnerBody>::decode(
                zc::ArrayPtr<const uint8_t>(&oversizedBacking, 134217729)) == zc::none);
}

ZC_TEST("StableBindingCodec.AllocationPlanMatchesIndependentWireAndRejectsMutations") {
  auto plan = require(allocationPlan(AllocationPlanMutation::None));
  const auto& first = plan.owners().values()[0];
  auto rangeBytes = StableBindingCodec<OwnerAllocationRange>::encode(first);
  ZC_EXPECT(rangeBytes.asPtr() == ownerAllocationRangeWire(first).asPtr());
  expectRoundTrip(first);
  auto planBytes = StableBindingCodec<ModuleBindingAllocationPlan>::encode(plan);
  ZC_EXPECT(planBytes.asPtr() == moduleAllocationPlanWire(plan).asPtr());
  expectRoundTrip(plan);

  constexpr auto rangeDomain = "zom.binder.owner-allocation-range"_zc;
  const auto ownerBytes = ownerBodyWire(first.owner());
  size_t rangeFieldOffset = rangeDomain.size() + 1 + sizeof(uint64_t) + ownerBytes.size();
  expectFrameLengthMutation<OwnerAllocationRange>(rangeBytes.asPtr(), rangeDomain.size() + 1,
                                                  ownerBytes.size() - 1);
  expectByteMutation<OwnerAllocationRange>(rangeBytes.asPtr(),
                                           rangeDomain.size() + 1 + sizeof(uint64_t));
  for (size_t field = 0; field < 4; ++field) {
    auto overflow = zc::heapArray(rangeBytes.asPtr());
    const size_t beginOffset = rangeFieldOffset + field * 2 * sizeof(uint32_t);
    for (size_t byte = 0; byte < sizeof(uint32_t); ++byte) {
      overflow[beginOffset + byte] = 0xff;
      overflow[beginOffset + sizeof(uint32_t) + byte] = byte + 1 == sizeof(uint32_t) ? 0x01 : 0x00;
    }
    ZC_EXPECT(StableBindingCodec<OwnerAllocationRange>::decode(overflow.asPtr()) == zc::none);
  }

  constexpr auto planDomain = "zom.binder.module-allocation-plan"_zc;
  const auto keyBytes = plan.key().encode();
  size_t planOffset = planDomain.size() + 1;
  expectFrameLengthMutation<ModuleBindingAllocationPlan>(planBytes.asPtr(), planOffset,
                                                         keyBytes.size() - 1);
  expectByteMutation<ModuleBindingAllocationPlan>(planBytes.asPtr(), planOffset + sizeof(uint64_t));
  planOffset += sizeof(uint64_t) + keyBytes.size();
  expectByteMutation<ModuleBindingAllocationPlan>(planBytes.asPtr(), planOffset);
  planOffset += 2 * sizeof(uint32_t);
  expectByteMutation<ModuleBindingAllocationPlan>(planBytes.asPtr(), planOffset);
  const size_t firstRangeFrame = planOffset + sizeof(uint64_t);
  expectFrameLengthMutation<ModuleBindingAllocationPlan>(planBytes.asPtr(), firstRangeFrame,
                                                         rangeBytes.size() - 1);
  const size_t embeddedRangeRecord = firstRangeFrame + sizeof(uint64_t) + rangeDomain.size() + 1;
  const size_t embeddedOwner = embeddedRangeRecord + sizeof(uint64_t);
  auto foreignOwner = ownerBodyWire(foreignOwnerBody(first.owner()));
  ZC_REQUIRE(foreignOwner.size() == ownerBytes.size());
  auto foreignPlan = zc::heapArray(planBytes.asPtr());
  foreignPlan.slice(embeddedOwner, embeddedOwner + foreignOwner.size())
      .copyFrom(foreignOwner.asPtr());
  ZC_EXPECT(StableBindingCodec<ModuleBindingAllocationPlan>::decode(foreignPlan.asPtr()) ==
            zc::none);
  const size_t embeddedFields = embeddedOwner + ownerBytes.size();
  for (size_t field = 0; field < 4; ++field) {
    expectByteMutation<ModuleBindingAllocationPlan>(planBytes.asPtr(),
                                                    embeddedFields + field * 2 * sizeof(uint32_t));
    expectByteMutation<ModuleBindingAllocationPlan>(
        planBytes.asPtr(), embeddedFields + (field * 2 + 1) * sizeof(uint32_t));
  }
  ZC_EXPECT(StableBindingCodec<ModuleBindingAllocationPlan>::decode(
                moduleAllocationPlanWire(plan, true).asPtr()) == zc::none);

  auto rangeWrongDomain = zc::heapArray(rangeBytes.asPtr());
  rangeWrongDomain[0] ^= 0x01;
  auto planWrongDomain = zc::heapArray(planBytes.asPtr());
  planWrongDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<OwnerAllocationRange>::decode(rangeWrongDomain.asPtr()) == zc::none);
  ZC_EXPECT(StableBindingCodec<ModuleBindingAllocationPlan>::decode(planWrongDomain.asPtr()) ==
            zc::none);
  ZC_EXPECT(StableBindingCodec<OwnerAllocationRange>::decode(
                rangeBytes.slice(0, rangeBytes.size() - 1)) == zc::none);
  ZC_EXPECT(StableBindingCodec<ModuleBindingAllocationPlan>::decode(
                planBytes.slice(0, planBytes.size() - 1)) == zc::none);
  zc::Vector<uint8_t> rangeTrailing(rangeBytes.size() + 1);
  rangeTrailing.addAll(rangeBytes.asPtr());
  rangeTrailing.add(0x00);
  zc::Vector<uint8_t> planTrailing(planBytes.size() + 1);
  planTrailing.addAll(planBytes.asPtr());
  planTrailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<OwnerAllocationRange>::decode(rangeTrailing.asPtr()) == zc::none);
  ZC_EXPECT(StableBindingCodec<ModuleBindingAllocationPlan>::decode(planTrailing.asPtr()) ==
            zc::none);
  uint8_t oversizedBacking = 0;
  zc::ArrayPtr<const uint8_t> oversized(&oversizedBacking, 134217729);
  ZC_EXPECT(StableBindingCodec<OwnerAllocationRange>::decode(oversized) == zc::none);
  ZC_EXPECT(StableBindingCodec<ModuleBindingAllocationPlan>::decode(oversized) == zc::none);
}

ZC_TEST("StableBindingCodec.ModuleSkeletonMatchesWireAndRejectsIndependentMutations") {
  auto skeleton = require(moduleSkeleton(LocalExportMutation::None));
  auto encoded = StableBindingCodec<BoundModuleSkeleton>::encode(skeleton);
  ZC_EXPECT(encoded.asPtr() == moduleSkeletonWire(skeleton).asPtr());
  expectRoundTrip(skeleton);

  constexpr auto domain = "zom.binder.module-skeleton"_zc;
  const auto moduleBytes = skeleton.module().encode();
  size_t offset = domain.size() + 1;
  expectFrameLengthMutation<BoundModuleSkeleton>(encoded.asPtr(), offset, moduleBytes.size() - 1);
  expectByteMutation<BoundModuleSkeleton>(encoded.asPtr(), offset + sizeof(uint64_t));
  auto foreignOwnership = zc::heapArray(encoded.asPtr());
  const auto foreignModule = module("other"_zc).encode();
  ZC_REQUIRE(foreignModule.size() == moduleBytes.size());
  foreignOwnership
      .slice(offset + sizeof(uint64_t), offset + sizeof(uint64_t) + foreignModule.size())
      .copyFrom(foreignModule.asPtr());
  ZC_EXPECT(StableBindingCodec<BoundModuleSkeleton>::decode(foreignOwnership.asPtr()) == zc::none);
  offset += sizeof(uint64_t) + moduleBytes.size();

  zc::Vector<zc::Array<uint8_t>> sequences;
  sequences.add(aggregateSequenceWire(skeleton.scopes(), &scopeFactWire));
  sequences.add(aggregateSequenceWire(skeleton.nodeScopes(), &nodeFactWire));
  sequences.add(aggregateSequenceWire(skeleton.declarations(), &declarationFactWire));
  sequences.add(
      aggregateSequenceWire(skeleton.implementationOccurrences(), &implementationFactWire));
  sequences.add(
      aggregateSequenceWire(skeleton.genericParameterDeclarations(), &genericDeclarationWire));
  sequences.add(
      aggregateSequenceWire(skeleton.callableParameterDeclarations(), &callableDeclarationWire));
  sequences.add(aggregateSequenceWire(skeleton.moduleAliases(), &moduleAliasFactWire));
  sequences.add(aggregateSequenceWire(skeleton.imports(), &importFactWire));
  sequences.add(aggregateSequenceWire(skeleton.localExports(), &localExportFactWire));
  sequences.add(aggregateSequenceWire(skeleton.bodyOwners(), &ownerBodyWire));
  sequences.add(aggregateSequenceWire(skeleton.failedLookups(), &failedLookupFactWire));
  for (const auto& sequence : sequences) {
    expectByteMutation<BoundModuleSkeleton>(encoded.asPtr(), offset);
    expectFrameLengthMutation<BoundModuleSkeleton>(encoded.asPtr(), offset + sizeof(uint64_t), 0);
    expectByteMutation<BoundModuleSkeleton>(encoded.asPtr(), offset + 2 * sizeof(uint64_t));
    offset += sequence.size();
  }
  ZC_EXPECT(offset == encoded.size());
  ZC_EXPECT(StableBindingCodec<BoundModuleSkeleton>::decode(
                moduleSkeletonWire(skeleton, true).asPtr()) == zc::none);

  auto wrongDomain = zc::heapArray(encoded.asPtr());
  wrongDomain[0] ^= 0x01;
  ZC_EXPECT(StableBindingCodec<BoundModuleSkeleton>::decode(wrongDomain.asPtr()) == zc::none);
  ZC_EXPECT(StableBindingCodec<BoundModuleSkeleton>::decode(encoded.slice(0, encoded.size() - 1)) ==
            zc::none);
  zc::Vector<uint8_t> trailing(encoded.size() + 1);
  trailing.addAll(encoded.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(StableBindingCodec<BoundModuleSkeleton>::decode(trailing.asPtr()) == zc::none);
  uint8_t oversizedBacking = 0;
  ZC_EXPECT(StableBindingCodec<BoundModuleSkeleton>::decode(
                zc::ArrayPtr<const uint8_t>(&oversizedBacking, 134217729)) == zc::none);
}

ZC_TEST("StableBindingCodec.LookupProjectionFactsAndKeysRejectWireMutations") {
  auto exportedBinding = [](zc::Maybe<MemberVisibility>&& visibility) {
    return require(StableExportedBinding::from(
        require(BindingNameKey::from(Namespace::Value, declaredName("name"_zc))),
        StableBindingTargetKey::module(module("binding"_zc)),
        StableBindingTargetKey::module(module("target"_zc)), zc::mv(visibility), true));
  };
  zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
  auto exported = exportedBinding(zc::mv(visibility));
  zc::Maybe<MemberVisibility> absentVisibility;
  auto unqualified = exportedBinding(zc::mv(absentVisibility));
  auto exportedKey = StableExportedBindingQueryKey::from(
      module("owner"_zc), require(BindingNameKey::from(Namespace::Value, declaredName("name"_zc))));
  auto scopeKey = require(StableScopeNameBucketQueryKey::from(
      StableScopeOwnerKey::module(module("owner"_zc)),
      require(BindingNameKey::from(Namespace::Value, declaredName("name"_zc)))));
  expectRoundTrip(exported);
  expectRoundTrip(unqualified);
  expectRoundTrip(exportedKey);
  expectRoundTrip(scopeKey);

  auto exportedBytes = StableBindingCodec<StableExportedBinding>::encode(exported);
  auto unqualifiedBytes = StableBindingCodec<StableExportedBinding>::encode(unqualified);
  auto exportedKeyBytes = StableBindingCodec<StableExportedBindingQueryKey>::encode(exportedKey);
  auto scopeKeyBytes = StableBindingCodec<StableScopeNameBucketQueryKey>::encode(scopeKey);
  ZC_EXPECT(exportedBytes.asPtr() == exportedBindingWire(exported).asPtr());
  ZC_EXPECT(unqualifiedBytes.asPtr() == exportedBindingWire(unqualified).asPtr());
  ZC_EXPECT(exportedKeyBytes.asPtr() == exportedBindingKeyWire(exportedKey).asPtr());
  ZC_EXPECT(scopeKeyBytes.asPtr() ==
            scopeNameBucketKeyWire(scopeKey.scope(), scopeKey.name()).asPtr());

  constexpr auto exportedDomain = "zom.binder.exported-binding"_zc;
  const auto exportedNameBytes = bindingNameWire(exported.name());
  const auto bindingBytes = StableBindingCodec<StableBindingTargetKey>::encode(exported.binding());
  const auto canonicalBytes =
      StableBindingCodec<StableBindingTargetKey>::encode(exported.canonicalTarget());
  size_t offset = exportedDomain.size() + 1;
  expectByteMutation<StableExportedBinding>(exportedBytes.asPtr(), offset);
  expectFrameLengthMutation<StableExportedBinding>(exportedBytes.asPtr(), offset + 1, 4097);
  expectByteMutation<StableExportedBinding>(exportedBytes.asPtr(), offset + 1 + sizeof(uint64_t));
  offset += exportedNameBytes.size();
  expectFrameLengthMutation<StableExportedBinding>(exportedBytes.asPtr(), offset,
                                                   bindingBytes.size() - 1);
  expectByteMutation<StableExportedBinding>(exportedBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + bindingBytes.size();
  expectFrameLengthMutation<StableExportedBinding>(exportedBytes.asPtr(), offset,
                                                   canonicalBytes.size() + 1);
  expectByteMutation<StableExportedBinding>(exportedBytes.asPtr(), offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + canonicalBytes.size();
  expectByteMutation<StableExportedBinding>(exportedBytes.asPtr(), offset);
  expectByteMutation<StableExportedBinding>(exportedBytes.asPtr(), offset + 1);
  expectByteMutation<StableExportedBinding>(exportedBytes.asPtr(), offset + 2);
  auto notExported = zc::heapArray(exportedBytes.asPtr());
  notExported[offset + 2] = 0x00;
  ZC_EXPECT(StableBindingCodec<StableExportedBinding>::decode(notExported.asPtr()) == zc::none);
  auto missingVisibility = zc::heapArray(unqualifiedBytes.asPtr());
  missingVisibility[unqualifiedBytes.size() - 2] = 0x01;
  ZC_EXPECT(StableBindingCodec<StableExportedBinding>::decode(missingVisibility.asPtr()) ==
            zc::none);

  constexpr auto exportedKeyDomain = "zom.query.exported-binding-key"_zc;
  const auto moduleBytes = exportedKey.module().encode();
  const auto exportedKeyNameBytes = bindingNameWire(exportedKey.name());
  offset = exportedKeyDomain.size() + 1;
  expectFrameLengthMutation<StableExportedBindingQueryKey>(exportedKeyBytes.asPtr(), offset,
                                                           moduleBytes.size() - 1);
  expectByteMutation<StableExportedBindingQueryKey>(exportedKeyBytes.asPtr(),
                                                    offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + moduleBytes.size();
  expectByteMutation<StableExportedBindingQueryKey>(exportedKeyBytes.asPtr(), offset);
  expectFrameLengthMutation<StableExportedBindingQueryKey>(exportedKeyBytes.asPtr(), offset + 1,
                                                           4097);
  expectByteMutation<StableExportedBindingQueryKey>(exportedKeyBytes.asPtr(),
                                                    offset + 1 + sizeof(uint64_t));
  ZC_EXPECT(offset + exportedKeyNameBytes.size() == exportedKeyBytes.size());

  constexpr auto scopeKeyDomain = "zom.query.scope-name-bucket-key"_zc;
  const auto scopeBytes = StableBindingCodec<StableScopeOwnerKey>::encode(scopeKey.scope());
  const auto scopeNameBytes = bindingNameWire(scopeKey.name());
  offset = scopeKeyDomain.size() + 1;
  expectFrameLengthMutation<StableScopeNameBucketQueryKey>(scopeKeyBytes.asPtr(), offset,
                                                           scopeBytes.size() + 1);
  expectByteMutation<StableScopeNameBucketQueryKey>(scopeKeyBytes.asPtr(),
                                                    offset + sizeof(uint64_t));
  offset += sizeof(uint64_t) + scopeBytes.size();
  expectByteMutation<StableScopeNameBucketQueryKey>(scopeKeyBytes.asPtr(), offset);
  expectFrameLengthMutation<StableScopeNameBucketQueryKey>(scopeKeyBytes.asPtr(), offset + 1, 4097);
  expectByteMutation<StableScopeNameBucketQueryKey>(scopeKeyBytes.asPtr(),
                                                    offset + 1 + sizeof(uint64_t));
  ZC_EXPECT(offset + scopeNameBytes.size() == scopeKeyBytes.size());
  auto bodyScope = StableScopeOwnerKey::body(ownerBody(), localPath());
  auto bodyName = require(BindingNameKey::from(Namespace::Value, declaredName("name"_zc)));
  ZC_EXPECT(StableBindingCodec<StableScopeNameBucketQueryKey>::decode(
                scopeNameBucketKeyWire(bodyScope, bodyName).asPtr()) == zc::none);

  auto expectEnvelopeRejection = []<typename T>(zc::ArrayPtr<const uint8_t> bytes,
                                                uint64_t maximumBytes) {
    auto wrongDomain = zc::heapArray(bytes);
    wrongDomain[0] ^= 0x01;
    ZC_EXPECT(StableBindingCodec<T>::decode(wrongDomain.asPtr()) == zc::none);
    ZC_EXPECT(StableBindingCodec<T>::decode(bytes.slice(0, bytes.size() - 1)) == zc::none);
    zc::Vector<uint8_t> trailing(bytes.size() + 1);
    trailing.addAll(bytes);
    trailing.add(0x00);
    ZC_EXPECT(StableBindingCodec<T>::decode(trailing.asPtr()) == zc::none);
    uint8_t backing = 0;
    ZC_EXPECT(StableBindingCodec<T>::decode(
                  zc::ArrayPtr<const uint8_t>(&backing, maximumBytes + 1)) == zc::none);
  };
  expectEnvelopeRejection.template operator()<StableExportedBinding>(exportedBytes.asPtr(),
                                                                     134217728);
  expectEnvelopeRejection.template operator()<StableExportedBindingQueryKey>(
      exportedKeyBytes.asPtr(), 65536);
  expectEnvelopeRejection.template operator()<StableScopeNameBucketQueryKey>(scopeKeyBytes.asPtr(),
                                                                             65536);
}

ZC_TEST("StableBindingCodec.RoutingKeysRoundTripAndRejectFramingMutations") {
  auto definitionKey =
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11));
  auto implementationKey =
      StableImplementationQueryKey::from(module("owner"_zc), digestKey<identity::ImplKey>(0x22));
  auto occurrenceKey = require(
      StableImplementationOccurrenceQueryKey::from(module("owner"_zc), occurrence("owner"_zc)));
  auto genericKey = StableGenericParameterQueryKey::from(
      module("owner"_zc), digestKey<identity::GenericParameterKey>(0x33));
  auto callableKey = StableCallableParameterQueryKey::from(
      module("owner"_zc), digestKey<identity::CallableParameterKey>(0x44));
  auto importKey =
      require(StableSemanticImportQueryKey::from(module("owner"_zc), importBinding("owner"_zc)));
  auto bodyKey = require(StableOwnerBodyQueryKey::from(
      module("owner"_zc), StableBodyOwnerKey::module(module("owner"_zc))));
  expectRoundTrip(definitionKey);
  expectRoundTrip(implementationKey);
  expectRoundTrip(occurrenceKey);
  expectRoundTrip(genericKey);
  expectRoundTrip(callableKey);
  expectRoundTrip(importKey);
  expectRoundTrip(bodyKey);

  auto encoded = definitionKey.encodeCanonical();
  ZC_EXPECT(encoded.asPtr() == expectedWire("zom.binder.definition-query-key"_zc,
                                            definitionKey.module(),
                                            definitionKey.definition().bytes(), false)
                                   .asPtr());
  ZC_EXPECT(implementationKey.encodeCanonical().asPtr() ==
            expectedWire("zom.binder.implementation-query-key"_zc, implementationKey.module(),
                         implementationKey.implementation().bytes(), false)
                .asPtr());
  ZC_EXPECT(occurrenceKey.encodeCanonical().asPtr() ==
            expectedWire("zom.binder.implementation-occurrence-query-key"_zc,
                         occurrenceKey.module(), occurrenceKey.occurrence().encode().asPtr(), true)
                .asPtr());
  ZC_EXPECT(genericKey.encodeCanonical().asPtr() ==
            expectedWire("zom.binder.generic-parameter-query-key"_zc, genericKey.module(),
                         genericKey.parameter().bytes(), false)
                .asPtr());
  ZC_EXPECT(callableKey.encodeCanonical().asPtr() ==
            expectedWire("zom.binder.callable-parameter-query-key"_zc, callableKey.module(),
                         callableKey.parameter().bytes(), false)
                .asPtr());
  ZC_EXPECT(importKey.encodeCanonical().asPtr() ==
            expectedWire("zom.binder.semantic-import-query-key"_zc, importKey.requester(),
                         importKey.binding().encode().asPtr(), true)
                .asPtr());
  ZC_EXPECT(bodyKey.encodeCanonical().asPtr() ==
            expectedWire("zom.binder.owner-body-query-key"_zc, bodyKey.module(),
                         bodyKey.owner().encode().asPtr(), true)
                .asPtr());
  auto wrongDomain = zc::heapArray(encoded.asPtr());
  wrongDomain[0] ^= 0x01;
  ZC_EXPECT(StableDefinitionQueryKey::decodeCanonical(wrongDomain.asPtr()) == zc::none);
  ZC_EXPECT(StableDefinitionQueryKey::decodeCanonical(encoded.slice(0, encoded.size() - 1)) ==
            zc::none);
  zc::Vector<uint8_t> trailing(encoded.size() + 1);
  trailing.addAll(encoded.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(StableDefinitionQueryKey::decodeCanonical(trailing.asPtr()) == zc::none);
}

ZC_TEST("StableBindingCodec.SequenceBuilderRequiresStrictWireOrder") {
  zc::Vector<StableDefinitionQueryKey> sorted;
  sorted.add(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11)));
  sorted.add(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x22)));
  ZC_EXPECT(StableBindingSequenceBuilder<StableDefinitionQueryKey>::from(zc::mv(sorted)) !=
            zc::none);

  zc::Vector<StableDefinitionQueryKey> reversed;
  reversed.add(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x22)));
  reversed.add(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11)));
  ZC_EXPECT(StableBindingSequenceBuilder<StableDefinitionQueryKey>::from(zc::mv(reversed)) ==
            zc::none);

  zc::Vector<StableDefinitionQueryKey> duplicate;
  duplicate.add(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11)));
  duplicate.add(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11)));
  ZC_EXPECT(StableBindingSequenceBuilder<StableDefinitionQueryKey>::from(zc::mv(duplicate)) ==
            zc::none);

  zc::Vector<StableDefinitionQueryKey> nonempty;
  nonempty.add(
      StableDefinitionQueryKey::from(module("owner"_zc), digestKey<identity::DefinitionKey>(0x11)));
  ZC_EXPECT(StableBindingSequenceBuilder<StableDefinitionQueryKey>::fromNonEmpty(
                zc::mv(nonempty)) != zc::none);

  zc::Vector<StableDefinitionQueryKey> empty;
  ZC_EXPECT(StableBindingSequenceBuilder<StableDefinitionQueryKey>::fromNonEmpty(zc::mv(empty)) ==
            zc::none);
}

ZC_TEST("StableBindingFacts.QueryOwnersCoverEveryStableRoutingAlternative") {
  auto implementation = digestKey<identity::ImplKey>(0x22);
  zc::Vector<BinderQueryOwner> owners;
  owners.add(BinderQueryOwner::module(module("owner"_zc)));
  owners.add(BinderQueryOwner::definitionHeader(StableDefinitionQueryKey::from(
      module("owner"_zc), digestKey<identity::DefinitionKey>(0x11))));
  owners.add(
      BinderQueryOwner::implementationHeader(require(StableImplementationOccurrenceQueryKey::from(
          module("owner"_zc), implementationOccurrence(implementation)))));
  owners.add(BinderQueryOwner::body(ownerBody()));
  ZC_REQUIRE(owners.size() == 4);
  ZC_EXPECT(owners[0].value().is<BinderModuleQueryOwner>());
  ZC_EXPECT(owners[1].value().is<BinderDefinitionHeaderQueryOwner>());
  ZC_EXPECT(owners[2].value().is<BinderImplementationHeaderQueryOwner>());
  ZC_EXPECT(owners[3].value().is<BinderBodyQueryOwner>());
  for (const auto& owner : owners) {
    auto clone = owner.clone();
    ZC_EXPECT(clone == owner);
  }
  ZC_EXPECT(owners[0] != owners[1]);
}

ZC_TEST("StableBindingFacts.KeyFailuresEnforceClosedKindAndPathRelation") {
  BinderKeyFailureKind kinds[] = {BinderKeyFailureKind::MissingSelectedModuleSource,
                                  BinderKeyFailureKind::InactiveOwner,
                                  BinderKeyFailureKind::ForeignOwner,
                                  BinderKeyFailureKind::DefinitionWithoutBody,
                                  BinderKeyFailureKind::BoundaryMismatch,
                                  BinderKeyFailureKind::NonSelectedSource,
                                  BinderKeyFailureKind::CrossBoundaryPath};
  for (const auto kind : kinds) {
    const bool requiresPath = kind >= BinderKeyFailureKind::BoundaryMismatch;
    zc::Maybe<LocalSyntaxPath> validPath;
    zc::Maybe<LocalSyntaxPath> invalidPath;
    if (requiresPath) {
      validPath = localPath();
    } else {
      invalidPath = localPath();
    }
    auto accepted = BinderKeyFailure::from(kind, BinderQueryOwner::module(module("owner"_zc)),
                                           zc::mv(validPath));
    ZC_REQUIRE(accepted != zc::none);
    auto clone = ZC_ASSERT_NONNULL(accepted).clone();
    ZC_EXPECT(clone == ZC_ASSERT_NONNULL(accepted));
    ZC_EXPECT(clone.kind() == kind);
    ZC_EXPECT((clone.path() != zc::none) == requiresPath);
    ZC_EXPECT(BinderKeyFailure::from(kind, BinderQueryOwner::module(module("owner"_zc)),
                                     zc::mv(invalidPath)) == zc::none);
  }
  zc::Maybe<LocalSyntaxPath> noPath;
  auto unknownOwner = BinderQueryOwner::module(module("owner"_zc));
  auto unknownOwnerExpected = unknownOwner.clone();
  ZC_EXPECT(BinderKeyFailure::from(static_cast<BinderKeyFailureKind>(0xff), zc::mv(unknownOwner),
                                   zc::mv(noPath)) == zc::none);
  ZC_EXPECT(unknownOwner == unknownOwnerExpected);

  auto missingPathOwner = BinderQueryOwner::module(module("owner"_zc));
  auto missingPathOwnerExpected = missingPathOwner.clone();
  zc::Maybe<LocalSyntaxPath> missingPath;
  ZC_EXPECT(BinderKeyFailure::from(BinderKeyFailureKind::BoundaryMismatch, zc::mv(missingPathOwner),
                                   zc::mv(missingPath)) == zc::none);
  ZC_EXPECT(missingPathOwner == missingPathOwnerExpected);

  auto forbiddenPathOwner = BinderQueryOwner::module(module("owner"_zc));
  auto forbiddenPathOwnerExpected = forbiddenPathOwner.clone();
  zc::Maybe<LocalSyntaxPath> forbiddenPath = localPath(9);
  auto forbiddenPathExpected = ZC_ASSERT_NONNULL(forbiddenPath).clone();
  ZC_EXPECT(BinderKeyFailure::from(BinderKeyFailureKind::InactiveOwner, zc::mv(forbiddenPathOwner),
                                   zc::mv(forbiddenPath)) == zc::none);
  ZC_EXPECT(forbiddenPathOwner == forbiddenPathOwnerExpected);
  ZC_REQUIRE(forbiddenPath != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(forbiddenPath) == forbiddenPathExpected);
}

ZC_TEST("StableBindingFacts.QueryResultsAreExclusiveAndPreserveDiagnostics") {
  auto value = BinderQueryResult<uint32_t>::value(
      uint32_t{7}, CanonicalSequence<diagnostics::DiagnosticFact>::empty());
  auto valueClone = value.clone();
  ZC_EXPECT(valueClone == value);
  ZC_REQUIRE(value.storage().is<BinderQueryValue<uint32_t>>());
  ZC_EXPECT(value.storage().get<BinderQueryValue<uint32_t>>().value == 7);
  ZC_EXPECT(value.storage().get<BinderQueryValue<uint32_t>>().diagnostics.values().size() == 0);

  zc::Maybe<LocalSyntaxPath> noPath;
  auto failure = require(BinderKeyFailure::from(
      BinderKeyFailureKind::InactiveOwner, BinderQueryOwner::body(ownerBody()), zc::mv(noPath)));
  auto keyRejected = BinderQueryResult<uint32_t>::keyRejected(zc::mv(failure));
  auto keyClone = keyRejected.clone();
  ZC_EXPECT(keyClone == keyRejected);
  ZC_REQUIRE(keyRejected.storage().is<BinderKeyRejected>());
  ZC_EXPECT(keyRejected.storage().get<BinderKeyRejected>().failure.kind() ==
            BinderKeyFailureKind::InactiveOwner);
  ZC_EXPECT(keyRejected != value);
}

diagnostics::DiagnosticFact diagnosticFact(uint64_t occurrenceValue = 0) {
  const auto occurrence = static_cast<uint32_t>(occurrenceValue);
  auto occurrenceKey = require(diagnostics::DiagnosticOccurrenceKey::from(
      tests::test_identity_detail::source(), diagnostics::SourceDiagnosticPhase::Lex,
      diagnostics::SourceDiagnosticEmitter::Lexer, occurrence));
  zc::Vector<uint32_t> path;
  path.add(occurrence);
  path.add(0);
  auto primary = require(diagnostics::DiagnosticProvenanceKey::from(
      tests::test_identity_detail::source(), diagnostics::SourceDiagnosticPhase::Lex,
      diagnostics::SourceDiagnosticEmitter::Lexer, zc::mv(path)));
  zc::Vector<zc::String> arguments;
  zc::Vector<diagnostics::DiagnosticSecondary> secondary;
  return require(diagnostics::DiagnosticFact::from(
      zc::mv(occurrenceKey), diagnostics::DiagID::InvalidCharacter, zc::mv(arguments),
      zc::mv(primary), zc::mv(secondary)));
}
zc::Array<uint8_t> diagnosticWire(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  return require(diagnostics::encodeDiagnosticFacts(
      zc::none, facts, stable_binding_codec_detail::kBinderDiagnosticLimits));
}
zc::Array<uint8_t> queryOwnerWire(const BinderQueryOwner& value) {
  identity::CanonicalEncoder record;
#define ZOM_OWNER_ORACLE(Variant, Tag, Field)                            \
  if (value.value().is<Variant>()) {                                     \
    record.encodeUint8(Tag);                                             \
    encodeStableOracleFrame(record, value.value().get<Variant>().Field); \
  } else
  if (value.value().is<BinderModuleQueryOwner>()) {
    record.encodeUint8(0x01);
    encodeOracleFrame(record, value.value().get<BinderModuleQueryOwner>().module.encode().asPtr());
  } else
    ZOM_OWNER_ORACLE(BinderDefinitionHeaderQueryOwner, 0x02, definition)
  ZOM_OWNER_ORACLE(BinderImplementationHeaderQueryOwner, 0x03, implementation) {
    record.encodeUint8(0x04);
    encodeStableOracleFrame(record, value.value().get<BinderBodyQueryOwner>().body);
  }
#undef ZOM_OWNER_ORACLE
  return record.finish();
}
zc::Array<uint8_t> keyFailureWire(const BinderKeyFailure& value) {
  identity::CanonicalEncoder record;
  record.encodeUint8(static_cast<uint8_t>(value.kind()));
  encodeStableOracleFrame(record, value.owner());
  ZC_IF_SOME(path, value.path()) {
    record.encodeUint8(0x01);
    encodeOracleFrame(record, path.encode().asPtr());
  } else {
    record.encodeUint8(0x00);
  }
  return expectedDomainRecord("zom.binder.key-failure"_zc, record.finish().asPtr());
}
template <typename T>
zc::Array<uint8_t> queryResultWire(const BinderQueryResult<T>& value) {
  identity::CanonicalEncoder record;
  if (value.storage().template is<BinderQueryValue<T>>()) {
    const auto& result = value.storage().template get<BinderQueryValue<T>>();
    record.encodeUint8(0x01);
    encodeStableOracleFrame(record, result.value);
    encodeOracleFrame(record, diagnosticWire(result.diagnostics.values()).asPtr());
  } else if (value.storage().template is<BinderSourceRejected>()) {
    const auto& rejected = value.storage().template get<BinderSourceRejected>();
    record.encodeUint8(0x02);
    encodeOracleFrame(record, diagnosticWire(rejected.diagnostics.values()).asPtr());
  } else {
    record.encodeUint8(0x03);
    encodeStableOracleFrame(record, value.storage().template get<BinderKeyRejected>().failure);
  }
  return expectedDomainRecord(binderQueryResultDomain<T>(), record.finish().asPtr());
}
ZC_TEST("StableBindingCodec.QueryOwnersAndFailuresRejectWireMutations") {
  auto implementation = digestKey<identity::ImplKey>(0x22);
  zc::Vector<BinderQueryOwner> owners;
  owners.add(BinderQueryOwner::module(module("owner"_zc)));
  owners.add(BinderQueryOwner::definitionHeader(StableDefinitionQueryKey::from(
      module("owner"_zc), digestKey<identity::DefinitionKey>(0x11))));
  owners.add(
      BinderQueryOwner::implementationHeader(require(StableImplementationOccurrenceQueryKey::from(
          module("owner"_zc), implementationOccurrence(implementation)))));
  owners.add(BinderQueryOwner::body(ownerBody()));
  for (const auto& owner : owners) {
    auto encoded = StableBindingCodec<BinderQueryOwner>::encode(owner);
    ZC_EXPECT(encoded.asPtr() == queryOwnerWire(owner).asPtr());
    expectRoundTrip(owner);
  }
  auto ownerBytes = StableBindingCodec<BinderQueryOwner>::encode(owners[0]);
  expectByteMutation<BinderQueryOwner>(ownerBytes.asPtr(), 0);
  ZC_EXPECT(StableBindingCodec<BinderQueryOwner>::decode(
                ownerBytes.slice(0, ownerBytes.size() - 1)) == zc::none);
  zc::Maybe<LocalSyntaxPath> path = localPath();
  auto failure = require(BinderKeyFailure::from(BinderKeyFailureKind::BoundaryMismatch,
                                                BinderQueryOwner::body(ownerBody()), zc::mv(path)));
  auto failureBytes = StableBindingCodec<BinderKeyFailure>::encode(failure);
  ZC_EXPECT(failureBytes.asPtr() == keyFailureWire(failure).asPtr());
  expectRoundTrip(failure);
  expectByteMutation<BinderKeyFailure>(failureBytes.asPtr(), 0);
  const size_t failureRecord = zc::StringPtr("zom.binder.key-failure"_zc).size() + 1;
  expectByteMutation<BinderKeyFailure>(failureBytes.asPtr(), failureRecord);
  expectByteMutation<BinderKeyFailure>(failureBytes.asPtr(), failureRecord + 1 + sizeof(uint64_t));
  const auto failureOwnerBytes = StableBindingCodec<BinderQueryOwner>::encode(failure.owner());
  expectByteMutation<BinderKeyFailure>(
      failureBytes.asPtr(), failureRecord + 1 + sizeof(uint64_t) + failureOwnerBytes.size());
  auto trailing = zc::heapArray<uint8_t>(failureBytes.size() + 1);
  trailing.first(failureBytes.size()).copyFrom(failureBytes.asPtr());
  trailing.back() = 0;
  ZC_EXPECT(StableBindingCodec<BinderKeyFailure>::decode(trailing.asPtr()) == zc::none);
}
ZC_TEST("StableBindingCodec.QueryResultsRejectAlternativeAndDiagnosticMutations") {
  auto value = BinderQueryResult<StableDefinitionHeader>::value(
      require(definitionHeader(DefinitionHeaderMutation::None)),
      CanonicalSequence<diagnostics::DiagnosticFact>::empty());
  zc::Vector<diagnostics::DiagnosticFact> sourceDiagnostics;
  sourceDiagnostics.add(diagnosticFact());
  auto source = BinderQueryResult<StableDefinitionHeader>::sourceRejected(
      require(StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::fromNonEmpty(
          zc::mv(sourceDiagnostics))));
  zc::Maybe<LocalSyntaxPath> noPath;
  auto failure = require(BinderKeyFailure::from(
      BinderKeyFailureKind::InactiveOwner, BinderQueryOwner::body(ownerBody()), zc::mv(noPath)));
  auto key = BinderQueryResult<StableDefinitionHeader>::keyRejected(zc::mv(failure));
  const BinderQueryResult<StableDefinitionHeader>* results[] = {&value, &source, &key};
  for (const auto* result : results) {
    auto encoded = StableBindingCodec<BinderQueryResult<StableDefinitionHeader>>::encode(*result);
    ZC_EXPECT(encoded.asPtr() == queryResultWire(*result).asPtr());
    expectRoundTrip(*result);
  }
  auto encoded = StableBindingCodec<BinderQueryResult<StableDefinitionHeader>>::encode(value);
  const size_t recordOffset = binderQueryResultDomain<StableDefinitionHeader>().size() + 1;
  expectByteMutation<BinderQueryResult<StableDefinitionHeader>>(encoded.asPtr(), 0);
  expectByteMutation<BinderQueryResult<StableDefinitionHeader>>(encoded.asPtr(), recordOffset);
  expectByteMutation<BinderQueryResult<StableDefinitionHeader>>(
      encoded.asPtr(), recordOffset + 1 + sizeof(uint64_t));
  ZC_EXPECT(StableBindingCodec<BinderQueryResult<StableDefinitionHeader>>::decode(
                encoded.slice(0, encoded.size() - 1)) == zc::none);
  auto trailing = zc::heapArray<uint8_t>(encoded.size() + 1);
  trailing.first(encoded.size()).copyFrom(encoded.asPtr());
  trailing.back() = 0;
  ZC_EXPECT(StableBindingCodec<BinderQueryResult<StableDefinitionHeader>>::decode(
                trailing.asPtr()) == zc::none);
  auto sourceBytes = StableBindingCodec<BinderQueryResult<StableDefinitionHeader>>::encode(source);
  expectByteMutation<BinderQueryResult<StableDefinitionHeader>>(
      sourceBytes.asPtr(), recordOffset + 1 + sizeof(uint64_t));
  identity::CanonicalEncoder invalidSourceRecord;
  invalidSourceRecord.encodeUint8(0x02);
  zc::Vector<diagnostics::DiagnosticFact> empty;
  invalidSourceRecord.encodeByteString(diagnosticWire(empty.asPtr()).asPtr());
  auto invalidSource = expectedDomainRecord(binderQueryResultDomain<StableDefinitionHeader>(),
                                            invalidSourceRecord.finish().asPtr());
  ZC_EXPECT(StableBindingCodec<BinderQueryResult<StableDefinitionHeader>>::decode(
                invalidSource.asPtr()) == zc::none);
  zc::Vector<diagnostics::DiagnosticFact> duplicateDiagnostics;
  duplicateDiagnostics.add(diagnosticFact());
  duplicateDiagnostics.add(diagnosticFact());
  ZC_EXPECT(StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::fromNonEmpty(
                zc::mv(duplicateDiagnostics)) == zc::none);
  auto later = diagnosticFact(1);
  zc::Vector<diagnostics::DiagnosticFact> reorderedDiagnostics;
  reorderedDiagnostics.add(zc::mv(later));
  reorderedDiagnostics.add(diagnosticFact());
  ZC_EXPECT(StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::fromNonEmpty(
                zc::mv(reorderedDiagnostics)) == zc::none);
  auto implementation = BinderQueryResult<StableImplementationOccurrenceHeader>::value(
      require(implementationHeader(ImplementationHeaderMutation::None)),
      CanonicalSequence<diagnostics::DiagnosticFact>::empty());
  auto implementationBytes =
      StableBindingCodec<BinderQueryResult<StableImplementationOccurrenceHeader>>::encode(
          implementation);
  ZC_EXPECT(implementationBytes.asPtr() == queryResultWire(implementation).asPtr());
  expectRoundTrip(implementation);
}
ZC_TEST("StableBindingCodec.QueryResultsAdmit4097DiagnosticFacts") {
  zc::Vector<diagnostics::DiagnosticFact> facts(4097);
  for (uint64_t index = 0; index < 4097; ++index) { facts.add(diagnosticFact(index)); }
  auto value = BinderQueryResult<StableDefinitionHeader>::value(
      require(definitionHeader(DefinitionHeaderMutation::None)),
      require(StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::from(zc::mv(facts))));
  auto decoded = require(StableBindingCodec<BinderQueryResult<StableDefinitionHeader>>::decode(
      StableBindingCodec<BinderQueryResult<StableDefinitionHeader>>::encode(value).asPtr()));
  const auto& diagnostics =
      decoded.storage().get<BinderQueryValue<StableDefinitionHeader>>().diagnostics;
  ZC_EXPECT(diagnostics.values().size() == 4097);
}
}  // namespace
}  // namespace zomlang::compiler::binder
