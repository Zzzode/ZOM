// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/module-skeleton-query.h"

#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/binder/stable-binding-diagnostic-fact.h"
#include "zomlang/compiler/binder/stable-definition-header-producer.h"
#include "zomlang/compiler/binder/stable-header-verifier.h"
#include "zomlang/compiler/binder/stable-implementation-occurrence-header-producer.h"
#include "zomlang/compiler/driver/module-graph-query-input.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/parser/parse-source-query.h"

namespace zomlang::compiler::binder {
namespace {

namespace binding_query = driver::incremental_binding_query;
namespace graph_query = driver::module_graph_query;

struct HeaderCommonAuthority final {
  CanonicalParsedModule parsed;
  RevisionLocalDefinitionSites definitionSites;
  RevisionLocalImplementationSites implementationSites;
};

template <typename Inventory>
struct HeaderProviderAuthority final {
  CanonicalParsedModule parsed;
  Inventory inventory;
  RevisionLocalDefinitionSites definitionSites;
  RevisionLocalImplementationSites implementationSites;
};

using DefinitionHeaderAuthority = HeaderProviderAuthority<NamedDefinitionInventory>;
using ImplementationHeaderAuthority = HeaderProviderAuthority<NamedImplementationInventory>;

struct HeaderVerificationAuthority final {
  CanonicalParsedModule parsed;
  NamedDefinitionInventory definitions;
  NamedImplementationInventory implementations;
  RevisionLocalDefinitionSites definitionSites;
  RevisionLocalImplementationSites implementationSites;
};

struct HeaderSourceRejection final {
  CanonicalNonEmptySequence<diagnostics::DiagnosticFact> diagnostics;
};

struct HeaderKeyRejection final {
  BinderKeyFailure failure;
};

struct HeaderRuntimeRejection final {
  query::QueryRuntimeFailure failure;
};

template <typename Authority>
using HeaderAuthorityRead =
    zc::OneOf<Authority, HeaderSourceRejection, HeaderKeyRejection, HeaderRuntimeRejection>;

zc::Maybe<CanonicalNonEmptySequence<diagnostics::DiagnosticFact>> cloneDiagnostics(
    zc::ArrayPtr<const diagnostics::DiagnosticFact> diagnostics) {
  zc::Vector<diagnostics::DiagnosticFact> copies(diagnostics.size());
  for (const auto& diagnostic : diagnostics) { copies.add(diagnostic.clone()); }
  return StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::fromNonEmpty(zc::mv(copies));
}

zc::Maybe<CanonicalNonEmptySequence<diagnostics::DiagnosticFact>> decodeDiagnostics(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto diagnostics = decodeStableBindingDiagnosticFacts(bytes);
  if (diagnostics == zc::none) { return zc::none; }
  return StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::fromNonEmpty(
      zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
}

template <typename Authority>
HeaderAuthorityRead<Authority> runtimeRejection(query::QueryRuntimeFailure failure) {
  return HeaderAuthorityRead<Authority>(HeaderRuntimeRejection{failure});
}

template <typename Authority>
HeaderAuthorityRead<Authority> sourceRejection(
    CanonicalNonEmptySequence<diagnostics::DiagnosticFact>&& diagnostics) {
  return HeaderAuthorityRead<Authority>(HeaderSourceRejection{zc::mv(diagnostics)});
}

template <typename Authority>
HeaderAuthorityRead<Authority> keyRejection(BinderKeyFailure&& failure) {
  return HeaderAuthorityRead<Authority>(HeaderKeyRejection{zc::mv(failure)});
}

template <typename Authority>
HeaderAuthorityRead<Authority> makeKeyRejection(BinderKeyFailureKind kind,
                                                BinderQueryOwner&& owner) {
  zc::Maybe<LocalSyntaxPath> noPath;
  auto failure = BinderKeyFailure::from(kind, zc::mv(owner), zc::mv(noPath));
  if (failure == zc::none) {
    return runtimeRejection<Authority>(query::QueryRuntimeFailure::InvariantViolation);
  }
  return keyRejection<Authority>(zc::mv(ZC_ASSERT_NONNULL(failure)));
}

template <typename Authority, typename Value>
HeaderAuthorityRead<Authority> semanticReadFailure(const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure()) { return runtimeRejection<Authority>(result.runtimeFailure()); }
  if (result.kind() != query::QueryValueKind::SemanticFailure) {
    return runtimeRejection<Authority>(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto diagnostics = decodeDiagnostics(result.semanticFailureBytes());
  if (diagnostics == zc::none) {
    return runtimeRejection<Authority>(query::QueryRuntimeFailure::InvariantViolation);
  }
  return sourceRejection<Authority>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
}

template <typename ToAuthority, typename FromAuthority>
HeaderAuthorityRead<ToAuthority> propagateHeaderFailure(HeaderAuthorityRead<FromAuthority>&& read) {
  if (read.template is<HeaderSourceRejection>()) {
    return sourceRejection<ToAuthority>(
        zc::mv(read.template get<HeaderSourceRejection>().diagnostics));
  }
  if (read.template is<HeaderKeyRejection>()) {
    return keyRejection<ToAuthority>(zc::mv(read.template get<HeaderKeyRejection>().failure));
  }
  if (read.template is<HeaderRuntimeRejection>()) {
    return runtimeRejection<ToAuthority>(read.template get<HeaderRuntimeRejection>().failure);
  }
  return runtimeRejection<ToAuthority>(query::QueryRuntimeFailure::InvariantViolation);
}

template <typename Context>
HeaderAuthorityRead<HeaderCommonAuthority> readHeaderCommonAuthority(
    Context& context, const identity::ModuleKey& module,
    const binding_query::StableModuleQueryKey& moduleKey, const BinderQueryOwner& owner) {
  auto selected = context.template get<graph_query::SelectedModuleSourceQuery>(module);
  if (selected.isRuntimeFailure()) {
    return runtimeRejection<HeaderCommonAuthority>(selected.runtimeFailure());
  }
  if (selected.kind() == query::QueryValueKind::Absence) {
    return makeKeyRejection<HeaderCommonAuthority>(
        BinderKeyFailureKind::MissingSelectedModuleSource, owner.clone());
  }
  if (selected.kind() != query::QueryValueKind::Value) {
    return runtimeRejection<HeaderCommonAuthority>(query::QueryRuntimeFailure::ProviderRejected);
  }

  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
  if (sourceKey == zc::none) {
    return runtimeRejection<HeaderCommonAuthority>(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed =
      context.template getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
  if (parsed.isRuntimeRejected()) {
    return runtimeRejection<HeaderCommonAuthority>(parsed.runtimeFailure());
  }
  if (parsed.isSourceRejected()) {
    auto diagnostics = cloneDiagnostics(parsed.diagnostics().values());
    if (diagnostics == zc::none) {
      return runtimeRejection<HeaderCommonAuthority>(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return sourceRejection<HeaderCommonAuthority>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (!parsed.isPublished()) {
    return runtimeRejection<HeaderCommonAuthority>(query::QueryRuntimeFailure::ProviderRejected);
  }

  auto definitionSites =
      context.template getCapability<binding_query::RevisionLocalDefinitionSitesQuery>(moduleKey);
  if (definitionSites.isRuntimeRejected()) {
    return runtimeRejection<HeaderCommonAuthority>(definitionSites.runtimeFailure());
  }
  if (definitionSites.isSourceRejected()) {
    auto diagnostics = cloneDiagnostics(definitionSites.diagnostics().values());
    if (diagnostics == zc::none) {
      return runtimeRejection<HeaderCommonAuthority>(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return sourceRejection<HeaderCommonAuthority>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (definitionSites.isKeyRejected()) {
    return keyRejection<HeaderCommonAuthority>(definitionSites.keyFailure().clone());
  }
  if (!definitionSites.isPublished()) {
    return runtimeRejection<HeaderCommonAuthority>(query::QueryRuntimeFailure::ProviderRejected);
  }

  auto implementationSites =
      context.template getCapability<binding_query::RevisionLocalImplementationSitesQuery>(
          moduleKey);
  if (implementationSites.isRuntimeRejected()) {
    return runtimeRejection<HeaderCommonAuthority>(implementationSites.runtimeFailure());
  }
  if (implementationSites.isSourceRejected()) {
    auto diagnostics = cloneDiagnostics(implementationSites.diagnostics().values());
    if (diagnostics == zc::none) {
      return runtimeRejection<HeaderCommonAuthority>(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return sourceRejection<HeaderCommonAuthority>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (implementationSites.isKeyRejected()) {
    return keyRejection<HeaderCommonAuthority>(implementationSites.keyFailure().clone());
  }
  if (!implementationSites.isPublished()) {
    return runtimeRejection<HeaderCommonAuthority>(query::QueryRuntimeFailure::ProviderRejected);
  }

  auto canonical = CanonicalParsedModule::fromQueryResult(parsed.lease().capability().clone());
  if (canonical == zc::none) {
    return runtimeRejection<HeaderCommonAuthority>(query::QueryRuntimeFailure::InvariantViolation);
  }
  return HeaderAuthorityRead<HeaderCommonAuthority>(HeaderCommonAuthority{
      zc::mv(ZC_ASSERT_NONNULL(canonical)), definitionSites.lease().capability().clone(),
      implementationSites.lease().capability().clone()});
}

template <typename InventoryQuery, typename Inventory, typename Context>
HeaderAuthorityRead<HeaderProviderAuthority<Inventory>> readHeaderProviderAuthority(
    Context& context, const identity::ModuleKey& module, BinderQueryOwner&& owner) {
  using Authority = HeaderProviderAuthority<Inventory>;
  auto moduleKey = binding_query::StableModuleQueryKey::fromVerified(module);
  if (moduleKey == zc::none) {
    return runtimeRejection<Authority>(query::QueryRuntimeFailure::InvalidKeyEncoding);
  }

  auto inventory = context.template get<InventoryQuery>(ZC_ASSERT_NONNULL(moduleKey));
  if (inventory.isRuntimeFailure() || inventory.kind() == query::QueryValueKind::SemanticFailure) {
    return semanticReadFailure<Authority>(inventory);
  }
  auto common = readHeaderCommonAuthority(context, module, ZC_ASSERT_NONNULL(moduleKey), owner);
  if (!common.template is<HeaderCommonAuthority>()) {
    return propagateHeaderFailure<Authority>(zc::mv(common));
  }
  if (inventory.kind() == query::QueryValueKind::Absence) {
    return makeKeyRejection<Authority>(BinderKeyFailureKind::InactiveOwner, zc::mv(owner));
  }
  if (inventory.kind() != query::QueryValueKind::Value) {
    return runtimeRejection<Authority>(query::QueryRuntimeFailure::InvariantViolation);
  }

  auto& authority = common.template get<HeaderCommonAuthority>();
  return HeaderAuthorityRead<Authority>(
      Authority{zc::mv(authority.parsed), inventory.value().clone(),
                zc::mv(authority.definitionSites), zc::mv(authority.implementationSites)});
}

template <typename Context>
HeaderAuthorityRead<HeaderVerificationAuthority> readHeaderVerificationAuthority(
    Context& context, const identity::ModuleKey& module, BinderQueryOwner&& owner) {
  auto moduleKey = binding_query::StableModuleQueryKey::fromVerified(module);
  if (moduleKey == zc::none) {
    return runtimeRejection<HeaderVerificationAuthority>(
        query::QueryRuntimeFailure::InvalidKeyEncoding);
  }

  auto definitions = context.template get<binding_query::NamedDefinitionInventoryQuery>(
      ZC_ASSERT_NONNULL(moduleKey));
  if (definitions.isRuntimeFailure() ||
      definitions.kind() == query::QueryValueKind::SemanticFailure) {
    return semanticReadFailure<HeaderVerificationAuthority>(definitions);
  }
  auto implementations = context.template get<binding_query::NamedImplementationInventoryQuery>(
      ZC_ASSERT_NONNULL(moduleKey));
  if (implementations.isRuntimeFailure() ||
      implementations.kind() == query::QueryValueKind::SemanticFailure) {
    return semanticReadFailure<HeaderVerificationAuthority>(implementations);
  }
  auto common = readHeaderCommonAuthority(context, module, ZC_ASSERT_NONNULL(moduleKey), owner);
  if (!common.template is<HeaderCommonAuthority>()) {
    return propagateHeaderFailure<HeaderVerificationAuthority>(zc::mv(common));
  }
  if (definitions.kind() == query::QueryValueKind::Absence ||
      implementations.kind() == query::QueryValueKind::Absence) {
    return makeKeyRejection<HeaderVerificationAuthority>(BinderKeyFailureKind::InactiveOwner,
                                                         zc::mv(owner));
  }
  if (definitions.kind() != query::QueryValueKind::Value ||
      implementations.kind() != query::QueryValueKind::Value) {
    return runtimeRejection<HeaderVerificationAuthority>(
        query::QueryRuntimeFailure::InvariantViolation);
  }

  auto& authority = common.template get<HeaderCommonAuthority>();
  return HeaderAuthorityRead<HeaderVerificationAuthority>(HeaderVerificationAuthority{
      zc::mv(authority.parsed), definitions.value().clone(), implementations.value().clone(),
      zc::mv(authority.definitionSites), zc::mv(authority.implementationSites)});
}

template <typename Value, typename Authority>
query::TypedQueryResult<Value> publishReadFailure(HeaderAuthorityRead<Authority>&& read) {
  if (read.template is<HeaderSourceRejection>()) {
    return query::TypedQueryResult<Value>::value(
        Value::sourceRejected(zc::mv(read.template get<HeaderSourceRejection>().diagnostics)));
  }
  if (read.template is<HeaderKeyRejection>()) {
    return query::TypedQueryResult<Value>::value(
        Value::keyRejected(zc::mv(read.template get<HeaderKeyRejection>().failure)));
  }
  if (read.template is<HeaderRuntimeRejection>()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        read.template get<HeaderRuntimeRejection>().failure);
  }
  return query::TypedQueryResult<Value>::runtimeFailure(
      query::QueryRuntimeFailure::InvariantViolation);
}

template <typename Value, typename Authority>
bool verifyReadFailure(const HeaderAuthorityRead<Authority>& read,
                       const query::TypedQueryResult<Value>& result) {
  if (read.template is<HeaderRuntimeRejection>() || result.kind() != query::QueryValueKind::Value) {
    return false;
  }
  if (read.template is<HeaderSourceRejection>()) {
    return result.value().storage().template is<BinderSourceRejected>() &&
           result.value().storage().template get<BinderSourceRejected>().diagnostics ==
               read.template get<HeaderSourceRejection>().diagnostics;
  }
  if (read.template is<HeaderKeyRejection>()) {
    return result.value().storage().template is<BinderKeyRejected>() &&
           result.value().storage().template get<BinderKeyRejected>().failure ==
               read.template get<HeaderKeyRejection>().failure;
  }
  return false;
}

CanonicalSequence<diagnostics::DiagnosticFact> emptyDiagnostics() {
  zc::Vector<diagnostics::DiagnosticFact> diagnostics;
  auto sequence =
      StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::from(zc::mv(diagnostics));
  return zc::mv(ZC_ASSERT_NONNULL(sequence));
}

}  // namespace

zc::Array<uint8_t> DefinitionHeaderSyntax::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<DefinitionHeaderSyntax::Key> DefinitionHeaderSyntax::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return Key::decodeCanonical(bytes);
}

zc::Array<uint8_t> DefinitionHeaderSyntax::encodeValue(const Value& value) {
  return StableBindingCodec<Value>::encode(value);
}

zc::Maybe<DefinitionHeaderSyntax::Value> DefinitionHeaderSyntax::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Value>::decode(bytes);
}

query::TypedQueryResult<DefinitionHeaderSyntax::Value> DefinitionHeaderSyntax::provide(
    query::QueryContext& context, const Key& key) {
  auto read = readHeaderProviderAuthority<binding_query::NamedDefinitionInventoryQuery,
                                          NamedDefinitionInventory>(
      context, key.module(), BinderQueryOwner::definitionHeader(key.clone()));
  if (!read.is<DefinitionHeaderAuthority>()) { return publishReadFailure<Value>(zc::mv(read)); }
  const auto& authority = read.get<DefinitionHeaderAuthority>();

  zc::Maybe<const NamedDefinitionInventoryEntry&> selectedEntry;
  for (const auto& entry : authority.inventory.entries()) {
    if (entry.key() != key.definition()) { continue; }
    if (selectedEntry != zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    selectedEntry = entry;
  }
  zc::Maybe<const RevisionLocalDefinitionSite&> selectedSite;
  for (const auto& site : authority.definitionSites.entries()) {
    if (site.definition() != key.definition()) { continue; }
    if (selectedSite != zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    selectedSite = site;
  }
  if (selectedEntry == zc::none && selectedSite == zc::none) {
    return publishReadFailure<Value>(makeKeyRejection<DefinitionHeaderAuthority>(
        BinderKeyFailureKind::InactiveOwner, BinderQueryOwner::definitionHeader(key.clone())));
  }
  if (selectedEntry == zc::none || selectedSite == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }

  auto header = StableDefinitionHeaderProducer::produce(StableDefinitionHeaderProductionInput{
      authority.parsed, key, ZC_ASSERT_NONNULL(selectedEntry), ZC_ASSERT_NONNULL(selectedSite),
      authority.definitionSites, authority.implementationSites});
  if (header == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(
      Value::value(zc::mv(ZC_ASSERT_NONNULL(header)), emptyDiagnostics()));
}

bool DefinitionHeaderSyntax::verify(query::QueryContext& context, const Key& key,
                                    const query::TypedQueryResult<Value>& result) {
  auto read = readHeaderVerificationAuthority(context, key.module(),
                                              BinderQueryOwner::definitionHeader(key.clone()));
  if (!read.is<HeaderVerificationAuthority>()) { return verifyReadFailure(read, result); }
  if (result.kind() != query::QueryValueKind::Value) { return false; }
  const auto& authority = read.get<HeaderVerificationAuthority>();

  size_t entryCount = 0;
  size_t siteCount = 0;
  for (const auto& entry : authority.definitions.entries()) {
    if (entry.key() == key.definition()) { ++entryCount; }
  }
  for (const auto& site : authority.definitionSites.entries()) {
    if (site.definition() == key.definition()) { ++siteCount; }
  }
  if (entryCount == 0 && siteCount == 0) {
    auto expected = makeKeyRejection<HeaderVerificationAuthority>(
        BinderKeyFailureKind::InactiveOwner, BinderQueryOwner::definitionHeader(key.clone()));
    return verifyReadFailure(expected, result);
  }
  if (entryCount != 1 || siteCount != 1) { return false; }
  if (!result.value().storage().is<BinderQueryValue<StableDefinitionHeader>>()) { return false; }
  const auto& value = result.value().storage().get<BinderQueryValue<StableDefinitionHeader>>();
  if (value.diagnostics.values().size() != 0) { return false; }
  return StableHeaderVerifier::verifyDefinition(
      StableHeaderVerificationContext{authority.parsed, authority.definitions,
                                      authority.implementations, authority.definitionSites,
                                      authority.implementationSites},
      key, value.value);
}

zc::Array<uint8_t> ImplementationOccurrenceHeaderSyntax::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<ImplementationOccurrenceHeaderSyntax::Key>
ImplementationOccurrenceHeaderSyntax::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return Key::decodeCanonical(bytes);
}

zc::Array<uint8_t> ImplementationOccurrenceHeaderSyntax::encodeValue(const Value& value) {
  return StableBindingCodec<Value>::encode(value);
}

zc::Maybe<ImplementationOccurrenceHeaderSyntax::Value>
ImplementationOccurrenceHeaderSyntax::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Value>::decode(bytes);
}

query::TypedQueryResult<ImplementationOccurrenceHeaderSyntax::Value>
ImplementationOccurrenceHeaderSyntax::provide(query::QueryContext& context, const Key& key) {
  auto read = readHeaderProviderAuthority<binding_query::NamedImplementationInventoryQuery,
                                          NamedImplementationInventory>(
      context, key.module(), BinderQueryOwner::implementationHeader(key.clone()));
  if (!read.is<ImplementationHeaderAuthority>()) { return publishReadFailure<Value>(zc::mv(read)); }
  const auto& authority = read.get<ImplementationHeaderAuthority>();

  zc::Maybe<const NamedImplementationInventoryEntry&> selectedEntry;
  for (const auto& entry : authority.inventory.entries()) {
    if (entry.key() != key.occurrence().implementation()) { continue; }
    if (selectedEntry != zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    selectedEntry = entry;
  }
  zc::Maybe<const RevisionLocalImplementationSite&> selectedSite;
  for (const auto& site : authority.implementationSites.entries()) {
    if (!site.occurrence().sameAs(key.occurrence())) { continue; }
    if (selectedSite != zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    selectedSite = site;
  }
  if (selectedEntry == zc::none && selectedSite == zc::none) {
    return publishReadFailure<Value>(makeKeyRejection<ImplementationHeaderAuthority>(
        BinderKeyFailureKind::InactiveOwner, BinderQueryOwner::implementationHeader(key.clone())));
  }
  if (selectedEntry == zc::none || selectedSite == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }

  auto header = StableImplementationOccurrenceHeaderProducer::produce(
      StableImplementationOccurrenceHeaderProductionInput{
          authority.parsed, key, ZC_ASSERT_NONNULL(selectedEntry), ZC_ASSERT_NONNULL(selectedSite),
          authority.definitionSites, authority.implementationSites});
  if (header == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(
      Value::value(zc::mv(ZC_ASSERT_NONNULL(header)), emptyDiagnostics()));
}

bool ImplementationOccurrenceHeaderSyntax::verify(query::QueryContext& context, const Key& key,
                                                  const query::TypedQueryResult<Value>& result) {
  auto read = readHeaderVerificationAuthority(context, key.module(),
                                              BinderQueryOwner::implementationHeader(key.clone()));
  if (!read.is<HeaderVerificationAuthority>()) { return verifyReadFailure(read, result); }
  if (result.kind() != query::QueryValueKind::Value) { return false; }
  const auto& authority = read.get<HeaderVerificationAuthority>();

  size_t entryCount = 0;
  size_t siteCount = 0;
  for (const auto& entry : authority.implementations.entries()) {
    if (entry.key() == key.occurrence().implementation()) { ++entryCount; }
  }
  for (const auto& site : authority.implementationSites.entries()) {
    if (site.occurrence().sameAs(key.occurrence())) { ++siteCount; }
  }
  if (entryCount == 0 && siteCount == 0) {
    auto expected = makeKeyRejection<HeaderVerificationAuthority>(
        BinderKeyFailureKind::InactiveOwner, BinderQueryOwner::implementationHeader(key.clone()));
    return verifyReadFailure(expected, result);
  }
  if (entryCount != 1 || siteCount != 1) { return false; }
  if (!result.value().storage().is<BinderQueryValue<StableImplementationOccurrenceHeader>>()) {
    return false;
  }
  const auto& value =
      result.value().storage().get<BinderQueryValue<StableImplementationOccurrenceHeader>>();
  if (value.diagnostics.values().size() != 0) { return false; }
  return StableHeaderVerifier::verifyImplementationOccurrence(
      StableHeaderVerificationContext{authority.parsed, authority.definitions,
                                      authority.implementations, authority.definitionSites,
                                      authority.implementationSites},
      key, value.value);
}

bool registerStableHeaderSyntaxQueries(query::QueryDatabase& database) {
  if (!database.registerDescriptor<DefinitionHeaderSyntax>().isRegistered()) { return false; }
  return database.registerDescriptor<ImplementationOccurrenceHeaderSyntax>().isRegistered();
}

}  // namespace zomlang::compiler::binder
