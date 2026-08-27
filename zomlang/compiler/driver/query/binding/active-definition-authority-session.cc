// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/query/binding/active-definition-authority-session.h"

#include "zc/core/encoding.h"
#include "zomlang/compiler/binder/graph/parsed-module.h"
#include "zomlang/compiler/binder/stable/definition/header-producer.h"
#include "zomlang/compiler/binder/stable/implementation/header-producer.h"
#include "zomlang/compiler/driver/query/binding/active-definition-authority-query.h"
#include "zomlang/compiler/driver/query/module-graph/module-graph-query-input.h"
#include "zomlang/compiler/driver/query/module-graph/module-graph-query.h"
#include "zomlang/compiler/driver/query/binding/named-identity-inventory-query.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/parser/query/parse-source-query.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

constexpr zc::StringPtr kAuthorityTransactionDomain =
    "zom.query.input-transaction.contextual-identity-authority"_zc;
constexpr zc::StringPtr kDefinitionAuthoritySetDomain =
    "zom.binder.active-definition-authority-set"_zc;
constexpr zc::StringPtr kImplementationAuthoritySetDomain =
    "zom.binder.active-implementation-authority-set"_zc;
constexpr zc::StringPtr kGenericAuthoritySetDomain =
    "zom.binder.active-generic-parameter-authority-set"_zc;
constexpr zc::StringPtr kCallableAuthoritySetDomain =
    "zom.binder.active-callable-parameter-authority-set"_zc;
constexpr uint64_t kMaximumAuthorityEntries = UINT32_MAX;
constexpr uint64_t kMaximumAuthorityValueBytes = SIZE_MAX;

using DefinitionEntry = ContextualDefinitionAuthorityEntry;
using ImplementationEntry = ContextualImplementationAuthorityEntry;
using GenericEntry = ContextualGenericParameterAuthorityEntry;
using CallableEntry = ContextualCallableParameterAuthorityEntry;

template <typename Entry>
zc::Maybe<identity::Sha256Digest> authorityDigest(zc::StringPtr domain,
                                                  const CompilationRootSetQueryKey& contextRoots,
                                                  zc::ArrayPtr<const Entry> entries) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(contextRoots.encodeCanonical().asPtr());
  encoder.encodeSequenceSize(entries.size());
  for (const auto& entry : entries) {
    encoder.encodeByteString(entry.key().encodeCanonical().asPtr());
    if constexpr (zc::isSameType<Entry, DefinitionEntry>()) {
      encoder.encodeByteString(entry.value().encode().asPtr());
    } else {
      encoder.encodeByteString(entry.value().encodeCanonical().asPtr());
    }
  }
  identity::Sha256Hasher hasher;
  const uint8_t separator = 0;
  auto payload = encoder.finish();
  if (!hasher.update(domain.asBytes()) || !hasher.update(zc::arrayPtr(separator)) ||
      !hasher.update(payload.asPtr())) {
    return zc::none;
  }
  return hasher.finish();
}

template <typename Entry>
zc::Maybe<identity::Sha256Digest> authorityDigest(zc::StringPtr domain,
                                                  const CompilationRootSetQueryKey& contextRoots,
                                                  const zc::Vector<Entry>& entries) {
  return authorityDigest<Entry>(domain, contextRoots, entries.asPtr());
}

template <typename Key>
bool contains(const zc::Vector<Key>& values, const Key& candidate) {
  for (const auto& value : values) {
    if (value == candidate) { return true; }
  }
  return false;
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

zc::Array<uint8_t> framePayload(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> payload) {
  auto result = zc::heapArray<uint8_t>(domain.size() + 1 + payload.size());
  size_t cursor = 0;
  for (const auto byte : domain.asBytes()) { result[cursor++] = byte; }
  result[cursor++] = 0;
  for (const auto byte : payload) { result[cursor++] = byte; }
  return result;
}

zc::Maybe<zc::ArrayPtr<const uint8_t>> unframePayload(zc::StringPtr domain,
                                                      zc::ArrayPtr<const uint8_t> bytes) {
  const size_t prefixSize = domain.size() + 1;
  if (bytes.size() <= prefixSize || bytes.slice(0, domain.size()) != domain.asBytes() ||
      bytes[domain.size()] != 0) {
    return zc::none;
  }
  return bytes.slice(prefixSize, bytes.size());
}

template <typename Entry>
bool canonicalizeAuthorityEntries(zc::Vector<Entry>& entries) {
  for (size_t index = 1; index < entries.size(); ++index) {
    auto current = zc::mv(entries[index]);
    const auto currentBytes = current.key().encodeCanonical();
    size_t insertion = index;
    while (insertion != 0 &&
           compareBytes(currentBytes.asPtr(),
                        entries[insertion - 1].key().encodeCanonical().asPtr()) < 0) {
      entries[insertion] = zc::mv(entries[insertion - 1]);
      --insertion;
    }
    entries[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < entries.size(); ++index) {
    if (entries[index - 1].key().encodeCanonical().asPtr() ==
        entries[index].key().encodeCanonical().asPtr()) {
      return false;
    }
  }
  return true;
}

template <typename Entry, typename DecodeKey, typename DecodeValue>
zc::Maybe<zc::Vector<Entry>> decodeAuthorityEntries(identity::CanonicalDecoder& decoder,
                                                    DecodeKey decodeKey, DecodeValue decodeValue) {
  auto count = decoder.decodeSequenceSize(kMaximumAuthorityEntries);
  if (count == zc::none) { return zc::none; }
  zc::Vector<Entry> entries;
  entries.reserve(ZC_ASSERT_NONNULL(count));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto keyBytes = decoder.decodeByteString(kMaximumAuthorityValueBytes);
    auto valueBytes = decoder.decodeByteString(kMaximumAuthorityValueBytes);
    if (keyBytes == zc::none || valueBytes == zc::none) { return zc::none; }
    auto key = decodeKey(ZC_ASSERT_NONNULL(keyBytes).asPtr());
    auto value = decodeValue(ZC_ASSERT_NONNULL(valueBytes).asPtr());
    if (key == zc::none || value == zc::none) { return zc::none; }
    entries.add(Entry::from(zc::mv(ZC_ASSERT_NONNULL(key)), zc::mv(ZC_ASSERT_NONNULL(value))));
  }
  return zc::mv(entries);
}

void sortOccurrences(zc::Vector<binder::StableImplementationOccurrenceQueryKey>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto value = zc::mv(values[index]);
    const auto bytes = value.encodeCanonical();
    size_t insertion = index;
    while (insertion != 0 &&
           compareBytes(bytes.asPtr(), values[insertion - 1].encodeCanonical().asPtr()) < 0) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(value);
  }
}

zc::Maybe<binder::RevisionLocalDefinitionSites> projectDefinitionSites(
    const binder::StableIdentityAdmission& admission,
    const binder::NamedDefinitionInventory& inventory) {
  zc::Vector<binder::RevisionLocalDefinitionSite> sites(admission.definitions().size());
  for (const auto& definition : admission.definitions()) {
    auto site = binder::RevisionLocalDefinitionSite::from(
        definition.node, definition.authority.key().clone(), definition.site.key().clone(),
        definition.site.range().byteStart(), definition.site.range().byteEnd());
    if (site == zc::none) { return zc::none; }
    sites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
  }
  return binder::RevisionLocalDefinitionSites::fromVerified(admission.module(), admission.source(),
                                                            inventory, zc::mv(sites));
}

zc::Maybe<binder::RevisionLocalImplementationSites> projectImplementationSites(
    const binder::StableIdentityAdmission& admission,
    const binder::NamedImplementationInventory& inventory) {
  zc::Vector<binder::RevisionLocalImplementationSite> sites(admission.implementations().size());
  for (const auto& implementation : admission.implementations()) {
    auto site = binder::RevisionLocalImplementationSite::from(
        implementation.node,
        binder::ImplSourceOccurrenceKey::from(implementation.authority.key().clone(),
                                              implementation.site.key().clone()),
        implementation.site.range().byteStart(), implementation.site.range().byteEnd());
    if (site == zc::none) { return zc::none; }
    sites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
  }
  return binder::RevisionLocalImplementationSites::fromVerified(
      admission.module(), admission.source(), inventory, zc::mv(sites));
}

zc::Maybe<const binder::RevisionLocalDefinitionSite&> findDefinitionSite(
    const binder::RevisionLocalDefinitionSites& sites, const identity::DefinitionKey& definition) {
  zc::Maybe<const binder::RevisionLocalDefinitionSite&> result;
  for (const auto& site : sites.entries()) {
    if (site.definition() != definition) { continue; }
    if (result != zc::none) { return zc::none; }
    result = site;
  }
  return result;
}

zc::Maybe<const binder::RevisionLocalImplementationSite&> findImplementationSite(
    const binder::RevisionLocalImplementationSites& sites,
    const binder::ImplSourceOccurrenceKey& occurrence) {
  zc::Maybe<const binder::RevisionLocalImplementationSite&> result;
  for (const auto& site : sites.entries()) {
    if (!site.occurrence().sameAs(occurrence)) { continue; }
    if (result != zc::none) { return zc::none; }
    result = site;
  }
  return result;
}

zc::Vector<binder::StableImplementationOccurrenceQueryKey> cloneOccurrences(
    zc::ArrayPtr<const binder::StableImplementationOccurrenceQueryKey> values) {
  zc::Vector<binder::StableImplementationOccurrenceQueryKey> result(values.size());
  for (const auto& value : values) { result.add(value.clone()); }
  return result;
}

template <typename Entry>
void encodeEntries(identity::CanonicalEncoder& encoder, const zc::Vector<Entry>& entries) {
  encoder.encodeSequenceSize(entries.size());
  for (const auto& entry : entries) {
    encoder.encodeByteString(entry.key().encodeCanonical().asPtr());
    if constexpr (zc::isSameType<Entry, DefinitionEntry>()) {
      encoder.encodeByteString(entry.value().encode().asPtr());
    } else {
      encoder.encodeByteString(entry.value().encodeCanonical().asPtr());
    }
  }
}

}  // namespace

struct ContextualIdentityAuthorityInputPayload::Impl final {
  Impl(CompilationRootSetQueryKey&& contextRoots,
       zc::Vector<DefinitionEntry>&& definitionAuthorities,
       zc::Vector<ImplementationEntry>&& implementationAuthorities,
       zc::Vector<GenericEntry>&& genericParameterAuthorities,
       zc::Vector<CallableEntry>&& callableParameterAuthorities,
       CompleteRootIdentityReadiness&& completeRootReadiness) noexcept
      : contextRoots(zc::mv(contextRoots)),
        definitionAuthorities(zc::mv(definitionAuthorities)),
        implementationAuthorities(zc::mv(implementationAuthorities)),
        genericParameterAuthorities(zc::mv(genericParameterAuthorities)),
        callableParameterAuthorities(zc::mv(callableParameterAuthorities)),
        completeRootReadiness(zc::mv(completeRootReadiness)) {}

  CompilationRootSetQueryKey contextRoots;
  zc::Vector<DefinitionEntry> definitionAuthorities;
  zc::Vector<ImplementationEntry> implementationAuthorities;
  zc::Vector<GenericEntry> genericParameterAuthorities;
  zc::Vector<CallableEntry> callableParameterAuthorities;
  CompleteRootIdentityReadiness completeRootReadiness;
};

ContextualIdentityAuthorityInputPayload::ContextualIdentityAuthorityInputPayload(
    zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
ContextualIdentityAuthorityInputPayload::~ContextualIdentityAuthorityInputPayload() noexcept(
    false) = default;
ContextualIdentityAuthorityInputPayload::ContextualIdentityAuthorityInputPayload(
    ContextualIdentityAuthorityInputPayload&&) noexcept = default;
ContextualIdentityAuthorityInputPayload& ContextualIdentityAuthorityInputPayload::operator=(
    ContextualIdentityAuthorityInputPayload&&) noexcept = default;

zc::Maybe<ContextualIdentityAuthorityInputPayload> ContextualIdentityAuthorityInputPayload::from(
    CompilationRootSetQueryKey&& contextRoots, zc::Vector<DefinitionEntry>&& definitionAuthorities,
    zc::Vector<ImplementationEntry>&& implementationAuthorities,
    zc::Vector<GenericEntry>&& genericParameterAuthorities,
    zc::Vector<CallableEntry>&& callableParameterAuthorities,
    CompleteRootIdentityReadiness&& completeRootReadiness) {
  if (completeRootReadiness.contextRoots() != contextRoots ||
      !canonicalizeAuthorityEntries(definitionAuthorities) ||
      !canonicalizeAuthorityEntries(implementationAuthorities) ||
      !canonicalizeAuthorityEntries(genericParameterAuthorities) ||
      !canonicalizeAuthorityEntries(callableParameterAuthorities)) {
    return zc::none;
  }
  const auto rooted = [&](const auto& entries) {
    for (const auto& entry : entries) {
      if (entry.key().contextRoots() != contextRoots) { return false; }
    }
    return true;
  };
  if (!rooted(definitionAuthorities) || !rooted(implementationAuthorities) ||
      !rooted(genericParameterAuthorities) || !rooted(callableParameterAuthorities)) {
    return zc::none;
  }
  return ContextualIdentityAuthorityInputPayload(
      zc::heap<Impl>(zc::mv(contextRoots), zc::mv(definitionAuthorities),
                     zc::mv(implementationAuthorities), zc::mv(genericParameterAuthorities),
                     zc::mv(callableParameterAuthorities), zc::mv(completeRootReadiness)));
}

ContextualIdentityAuthorityInputPayload ContextualIdentityAuthorityInputPayload::clone() const {
  zc::Vector<DefinitionEntry> definitions(impl->definitionAuthorities.size());
  for (const auto& entry : impl->definitionAuthorities) { definitions.add(entry.clone()); }
  zc::Vector<ImplementationEntry> implementations(impl->implementationAuthorities.size());
  for (const auto& entry : impl->implementationAuthorities) { implementations.add(entry.clone()); }
  zc::Vector<GenericEntry> genericParameters(impl->genericParameterAuthorities.size());
  for (const auto& entry : impl->genericParameterAuthorities) {
    genericParameters.add(entry.clone());
  }
  zc::Vector<CallableEntry> callableParameters(impl->callableParameterAuthorities.size());
  for (const auto& entry : impl->callableParameterAuthorities) {
    callableParameters.add(entry.clone());
  }
  return ContextualIdentityAuthorityInputPayload(zc::heap<Impl>(
      impl->contextRoots.clone(), zc::mv(definitions), zc::mv(implementations),
      zc::mv(genericParameters), zc::mv(callableParameters), impl->completeRootReadiness.clone()));
}

const CompilationRootSetQueryKey& ContextualIdentityAuthorityInputPayload::contextRoots()
    const noexcept {
  return impl->contextRoots;
}
zc::ArrayPtr<const ContextualDefinitionAuthorityEntry>
ContextualIdentityAuthorityInputPayload::definitionAuthorities() const noexcept {
  return impl->definitionAuthorities.asPtr();
}
zc::ArrayPtr<const ContextualImplementationAuthorityEntry>
ContextualIdentityAuthorityInputPayload::implementationAuthorities() const noexcept {
  return impl->implementationAuthorities.asPtr();
}
zc::ArrayPtr<const ContextualGenericParameterAuthorityEntry>
ContextualIdentityAuthorityInputPayload::genericParameterAuthorities() const noexcept {
  return impl->genericParameterAuthorities.asPtr();
}
zc::ArrayPtr<const ContextualCallableParameterAuthorityEntry>
ContextualIdentityAuthorityInputPayload::callableParameterAuthorities() const noexcept {
  return impl->callableParameterAuthorities.asPtr();
}
const CompleteRootIdentityReadiness&
ContextualIdentityAuthorityInputPayload::completeRootReadiness() const noexcept {
  return impl->completeRootReadiness;
}

zc::Array<uint8_t> ContextualIdentityAuthorityInputPayload::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(impl->contextRoots.encodeCanonical().asPtr());
  encodeEntries(encoder, impl->definitionAuthorities);
  encodeEntries(encoder, impl->implementationAuthorities);
  encodeEntries(encoder, impl->genericParameterAuthorities);
  encodeEntries(encoder, impl->callableParameterAuthorities);
  encoder.encodeByteString(impl->completeRootReadiness.encodeCanonical().asPtr());
  return framePayload(kAuthorityTransactionDomain, encoder.finish().asPtr());
}

bool ContextualIdentityAuthorityInputPayload::operator==(
    const ContextualIdentityAuthorityInputPayload& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

zc::Maybe<ContextualIdentityAuthorityInputPayload>
ContextualIdentityAuthorityInputPayload::decodeCanonical(zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframePayload(kAuthorityTransactionDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto contextBytes = decoder.decodeByteString(kMaximumAuthorityValueBytes);
  if (contextBytes == zc::none) { return zc::none; }
  auto context =
      CompilationRootSetQueryKey::decodeCanonical(ZC_ASSERT_NONNULL(contextBytes).asPtr());
  if (context == zc::none) { return zc::none; }
  auto definitions = decodeAuthorityEntries<DefinitionEntry>(
      decoder,
      [](zc::ArrayPtr<const uint8_t> value) {
        return ContextualDefinitionKey::decodeCanonical(value);
      },
      [](zc::ArrayPtr<const uint8_t> value) {
        return identity::DefinitionIdentityRecord::decodeCanonical(value);
      });
  auto implementations = decodeAuthorityEntries<ImplementationEntry>(
      decoder,
      [](zc::ArrayPtr<const uint8_t> value) {
        return ContextualImplementationKey::decodeCanonical(value);
      },
      [](zc::ArrayPtr<const uint8_t> value) {
        return ActiveImplementationMembershipRecord::decodeCanonical(value);
      });
  auto genericParameters = decodeAuthorityEntries<GenericEntry>(
      decoder,
      [](zc::ArrayPtr<const uint8_t> value) {
        return ContextualGenericParameterKey::decodeCanonical(value);
      },
      [](zc::ArrayPtr<const uint8_t> value) {
        return ActiveGenericParameterMembership::decodeCanonical(value);
      });
  auto callableParameters = decodeAuthorityEntries<CallableEntry>(
      decoder,
      [](zc::ArrayPtr<const uint8_t> value) {
        return ContextualCallableParameterKey::decodeCanonical(value);
      },
      [](zc::ArrayPtr<const uint8_t> value) {
        return ActiveCallableParameterMembershipRecord::decodeCanonical(value);
      });
  auto readinessBytes = decoder.decodeByteString(kMaximumAuthorityValueBytes);
  if (definitions == zc::none || implementations == zc::none || genericParameters == zc::none ||
      callableParameters == zc::none || readinessBytes == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto readiness =
      CompleteRootIdentityReadiness::decodeCanonical(ZC_ASSERT_NONNULL(readinessBytes).asPtr());
  if (readiness == zc::none) { return zc::none; }
  auto result =
      from(zc::mv(ZC_ASSERT_NONNULL(context)), zc::mv(ZC_ASSERT_NONNULL(definitions)),
           zc::mv(ZC_ASSERT_NONNULL(implementations)), zc::mv(ZC_ASSERT_NONNULL(genericParameters)),
           zc::mv(ZC_ASSERT_NONNULL(callableParameters)), zc::mv(ZC_ASSERT_NONNULL(readiness)));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

bool ContextualIdentityAuthorityInputVerifier::verify(
    const query::QuerySnapshot& authorityStagingSnapshot,
    const ContextualIdentityAuthorityInputPayload& candidate) {
  auto encoded = candidate.encodeCanonical();
  auto decoded = ContextualIdentityAuthorityInputPayload::decodeCanonical(encoded.asPtr());
  auto graph =
      authorityStagingSnapshot.get<module_graph_query::ModuleGraph>(candidate.contextRoots());
  auto scc = authorityStagingSnapshot.get<module_graph_query::ModuleGraphScc>(
      candidate.contextRoots());
  if (decoded == zc::none || ZC_ASSERT_NONNULL(decoded) != candidate || graph.isRuntimeFailure() ||
      scc.isRuntimeFailure() || graph.kind() != query::QueryValueKind::Value ||
      scc.kind() != query::QueryValueKind::Value || graph.value().modules().size() == 0 ||
      scc.value().hasCycle(graph.value()) ||
      !CompleteRootIdentityReadinessVerifier::verify(candidate.contextRoots(),
                                                     candidate.completeRootReadiness())) {
    return false;
  }
  const auto moduleIsActive = [&](const identity::ModuleKey& module) {
    size_t matches = 0;
    for (const auto& active : graph.value().modules()) {
      if (active.encode().asPtr() == module.encode().asPtr()) { ++matches; }
    }
    return matches == 1;
  };
  for (const auto& entry : candidate.definitionAuthorities()) {
    if (!moduleIsActive(entry.key().definition().module()) ||
        !ActiveDefinitionAuthorityInputVerifier::verify(entry.key(), entry.value())) {
      return false;
    }
  }
  for (const auto& entry : candidate.implementationAuthorities()) {
    if (!moduleIsActive(entry.key().implementation().module()) ||
        !ActiveImplementationAuthorityInputVerifier::verify(entry.key(), entry.value())) {
      return false;
    }
  }
  for (const auto& entry : candidate.genericParameterAuthorities()) {
    if (!moduleIsActive(entry.key().parameter().module()) ||
        !ActiveGenericParameterAuthorityInputVerifier::verify(entry.key(), entry.value())) {
      return false;
    }
  }
  for (const auto& entry : candidate.callableParameterAuthorities()) {
    if (!moduleIsActive(entry.key().parameter().module()) ||
        !ActiveCallableParameterAuthorityInputVerifier::verify(entry.key(), entry.value())) {
      return false;
    }
  }
  auto definitionDigest = authorityDigest(kDefinitionAuthoritySetDomain, candidate.contextRoots(),
                                          candidate.definitionAuthorities());
  auto implementationDigest =
      authorityDigest(kImplementationAuthoritySetDomain, candidate.contextRoots(),
                      candidate.implementationAuthorities());
  auto genericDigest = authorityDigest(kGenericAuthoritySetDomain, candidate.contextRoots(),
                                       candidate.genericParameterAuthorities());
  auto callableDigest = authorityDigest(kCallableAuthoritySetDomain, candidate.contextRoots(),
                                        candidate.callableParameterAuthorities());
  return definitionDigest != zc::none && implementationDigest != zc::none &&
         genericDigest != zc::none && callableDigest != zc::none &&
         ZC_ASSERT_NONNULL(definitionDigest) ==
             candidate.completeRootReadiness().definitionAuthorityDigest() &&
         ZC_ASSERT_NONNULL(implementationDigest) ==
             candidate.completeRootReadiness().implementationAuthorityDigest() &&
         ZC_ASSERT_NONNULL(genericDigest) ==
             candidate.completeRootReadiness().genericParameterAuthorityDigest() &&
         ZC_ASSERT_NONNULL(callableDigest) ==
             candidate.completeRootReadiness().callableParameterAuthorityDigest();
}

zc::Maybe<query::InputTransaction> ContextualIdentityAuthorityInputLedger::beginBaseMutation(
    query::QueryDatabase& database) {
  auto snapshot = database.snapshot();
  auto pending = database.beginInputTransaction(snapshot.revision());
  if (!pending.isOpened()) { return zc::none; }
  auto transaction = zc::mv(pending).takeTransaction();
  ZC_IF_SOME(contextRoots, contextRootsField) {
    auto readiness = snapshot.probeInput<ActiveDefinitionAuthorityReadyInput>(contextRoots);
    auto complete = snapshot.probeInput<CompleteRootIdentityReadinessInput>(contextRoots);
    if (readiness.isRuntimeFailure() || complete.isRuntimeFailure()) {
      transaction.abandon();
      return zc::none;
    }
    if (readiness.kind() == query::QueryValueKind::Value) {
      auto mutation = transaction.erase<ActiveDefinitionAuthorityReadyInput>(contextRoots);
      if (!mutation.isApplied()) {
        transaction.abandon();
        return zc::none;
      }
    } else if (readiness.kind() != query::QueryValueKind::Absence) {
      transaction.abandon();
      return zc::none;
    }
    if (complete.kind() == query::QueryValueKind::Value) {
      auto mutation = transaction.erase<CompleteRootIdentityReadinessInput>(contextRoots);
      if (!mutation.isApplied()) {
        transaction.abandon();
        return zc::none;
      }
    } else if (complete.kind() != query::QueryValueKind::Absence) {
      transaction.abandon();
      return zc::none;
    }
  }
  return zc::mv(transaction);
}

zc::ArrayPtr<const binder::StableDefinitionQueryKey>
ContextualIdentityAuthorityInputLedger::definitionKeys() const {
  return definitionKeyFields.asPtr();
}

zc::ArrayPtr<const ContextualImplementationKey>
ContextualIdentityAuthorityInputLedger::implementationKeys() const {
  return implementationKeyFields.asPtr();
}

zc::ArrayPtr<const ContextualGenericParameterKey>
ContextualIdentityAuthorityInputLedger::genericParameterKeys() const {
  return genericParameterKeyFields.asPtr();
}

zc::ArrayPtr<const ContextualCallableParameterKey>
ContextualIdentityAuthorityInputLedger::callableParameterKeys() const {
  return callableParameterKeyFields.asPtr();
}

struct ContextualIdentityAuthorityInputTransaction::Impl final {
  query::DatabaseRevision expectedPreviousRevision;
  ContextualIdentityAuthorityInputPayload payload;
  ActiveDefinitionAuthoritySetFingerprint definitionFingerprint;
  ContextualIdentityAuthorityInputLedger priorLedger;
  ContextualIdentityAuthorityInputLedger nextLedger;
  binder::CanonicalInputPayloadDigest payloadDigest;
  bool committed = false;
};

ContextualIdentityAuthorityInputTransaction::ContextualIdentityAuthorityInputTransaction(
    zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
ContextualIdentityAuthorityInputTransaction::
    ~ContextualIdentityAuthorityInputTransaction() noexcept(false) = default;
ContextualIdentityAuthorityInputTransaction::ContextualIdentityAuthorityInputTransaction(
    ContextualIdentityAuthorityInputTransaction&&) noexcept = default;
ContextualIdentityAuthorityInputTransaction& ContextualIdentityAuthorityInputTransaction::operator=(
    ContextualIdentityAuthorityInputTransaction&&) noexcept = default;

zc::Maybe<ContextualIdentityAuthorityInputTransaction>
ContextualIdentityAuthorityInputTransaction::prepare(
    const query::QuerySnapshot& authorityStagingSnapshot,
    query::DatabaseRevision expectedPreviousRevision,
    const CompilationRootSetQueryKey& contextRoots,
    const ContextualIdentityAuthorityInputLedger& priorLedger) {
  if (authorityStagingSnapshot.revision() != expectedPreviousRevision) { return zc::none; }
  auto graph = authorityStagingSnapshot.get<module_graph_query::ModuleGraph>(contextRoots);
  auto scc = authorityStagingSnapshot.get<module_graph_query::ModuleGraphScc>(contextRoots);
  if (graph.isRuntimeFailure() || scc.isRuntimeFailure() ||
      graph.kind() != query::QueryValueKind::Value || scc.kind() != query::QueryValueKind::Value ||
      graph.value().modules().size() == 0 || scc.value().hasCycle(graph.value())) {
    return zc::none;
  }

  zc::Vector<ActiveDefinitionAuthorityRecord> projectionRecords;
  zc::Vector<DefinitionEntry> definitions;
  zc::Vector<ImplementationEntry> implementations;
  zc::Vector<GenericEntry> genericParameters;
  zc::Vector<CallableEntry> callableParameters;
  ContextualIdentityAuthorityInputLedger nextLedger;
  nextLedger.contextRootsField = contextRoots.clone();

  for (const auto& module : graph.value().modules()) {
    auto stableModule = StableModuleQueryKey::fromVerified(module);
    if (stableModule == zc::none) { return zc::none; }
    auto definitionInventory = authorityStagingSnapshot.get<NamedDefinitionInventoryQuery>(
        ZC_ASSERT_NONNULL(stableModule));
    auto implementationInventory = authorityStagingSnapshot.get<NamedImplementationInventoryQuery>(
        ZC_ASSERT_NONNULL(stableModule));
    auto admission = authorityStagingSnapshot.getCapability<StableIdentityAdmissionQuery>(
        ZC_ASSERT_NONNULL(stableModule));
    if (definitionInventory.isRuntimeFailure() || implementationInventory.isRuntimeFailure() ||
        definitionInventory.kind() != query::QueryValueKind::Value ||
        implementationInventory.kind() != query::QueryValueKind::Value ||
        !admission.isPublished()) {
      return zc::none;
    }
    const auto& admitted = admission.lease().capability();
    auto definitionSites = projectDefinitionSites(admitted, definitionInventory.value());
    auto implementationSites =
        projectImplementationSites(admitted, implementationInventory.value());
    auto selectedSource =
        authorityStagingSnapshot.get<module_graph_query::SelectedModuleSource>(module);
    if (definitionSites == zc::none || implementationSites == zc::none ||
        selectedSource.isRuntimeFailure() ||
        selectedSource.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    auto stableSource =
        identity::source_query::StableSourceQueryKey::fromVerified(selectedSource.value());
    if (stableSource == zc::none) { return zc::none; }
    auto parsed = authorityStagingSnapshot.getCapability<parser::ParseSourceQuery>(
        ZC_ASSERT_NONNULL(stableSource));
    if (!parsed.isPublished()) { return zc::none; }
    auto canonicalParsed =
        binder::CanonicalParsedModule::fromQueryResult(parsed.lease().capability().clone());
    if (canonicalParsed == zc::none) { return zc::none; }

    for (const auto& entry : definitionInventory.value().entries()) {
      auto stableDefinition =
          binder::StableDefinitionQueryKey::from(module.clone(), entry.key().clone());
      auto authoritySite = findDefinitionSite(ZC_ASSERT_NONNULL(definitionSites), entry.key());
      if (authoritySite == zc::none) { return zc::none; }
      auto header = binder::DefinitionHeaderProducer::produce(binder::DefinitionHeaderInput{
          ZC_ASSERT_NONNULL(canonicalParsed), stableDefinition, entry,
          ZC_ASSERT_NONNULL(authoritySite), ZC_ASSERT_NONNULL(definitionSites),
          ZC_ASSERT_NONNULL(implementationSites)});
      if (header == zc::none) { return zc::none; }
      const auto& headerValue = ZC_ASSERT_NONNULL(header);
      if (headerValue.queryKey() != stableDefinition ||
          headerValue.record().encode().asPtr() != entry.record().encode().asPtr()) {
        return zc::none;
      }

      auto contextualDefinition =
          ContextualDefinitionKey::from(contextRoots.clone(), stableDefinition.clone());
      definitions.add(DefinitionEntry::from(zc::mv(contextualDefinition), entry.record().clone()));
      nextLedger.definitionKeyFields.add(stableDefinition.clone());
      auto projection =
          ActiveDefinitionAuthorityRecord::from(entry.key().clone(), entry.record().clone());
      if (projection == zc::none) { return zc::none; }
      projectionRecords.add(zc::mv(ZC_ASSERT_NONNULL(projection)));

      for (const auto& parameter : headerValue.genericParameters().values()) {
        auto stableParameter =
            binder::StableGenericParameterQueryKey::from(module.clone(), parameter.key().clone());
        auto membership = ActiveGenericParameterMembership::from(
            stableParameter.clone(), parameter.record().clone(),
            ActiveGenericParameterOwner::definition(stableDefinition.clone(),
                                                    headerValue.authoritySite().clone()),
            parameter.ordinal());
        if (membership == zc::none) { return zc::none; }
        auto key =
            ContextualGenericParameterKey::from(contextRoots.clone(), stableParameter.clone());
        nextLedger.genericParameterKeyFields.add(key.clone());
        genericParameters.add(
            GenericEntry::from(zc::mv(key), zc::mv(ZC_ASSERT_NONNULL(membership))));
      }
      for (const auto& parameter : headerValue.callableParameters().values()) {
        auto stableParameter =
            binder::StableCallableParameterQueryKey::from(module.clone(), parameter.key().clone());
        auto membership = ActiveCallableParameterMembershipRecord::from(
            stableParameter.clone(), parameter.record().clone(), stableDefinition.clone(),
            headerValue.authoritySite().clone(), parameter.position(),
            parameter.name() == zc::none ? zc::Maybe<identity::DeclaredDefinitionName>()
                                         : zc::Maybe<identity::DeclaredDefinitionName>(
                                               ZC_ASSERT_NONNULL(parameter.name()).clone()),
            true);
        if (membership == zc::none) { return zc::none; }
        auto key =
            ContextualCallableParameterKey::from(contextRoots.clone(), stableParameter.clone());
        nextLedger.callableParameterKeyFields.add(key.clone());
        callableParameters.add(
            CallableEntry::from(zc::mv(key), zc::mv(ZC_ASSERT_NONNULL(membership))));
      }
    }

    for (const auto& entry : implementationInventory.value().entries()) {
      auto stableImplementation =
          binder::StableImplementationQueryKey::from(module.clone(), entry.key().clone());
      zc::Vector<binder::StableImplementationOccurrenceQueryKey> occurrences;
      for (const auto& candidate : admission.lease().capability().implementations()) {
        if (candidate.authority.key() != entry.key()) { continue; }
        auto occurrence = binder::StableImplementationOccurrenceQueryKey::from(
            module.clone(), binder::ImplSourceOccurrenceKey::from(candidate.authority.key().clone(),
                                                                  candidate.site.key().clone()));
        if (occurrence == zc::none) { return zc::none; }
        occurrences.add(zc::mv(ZC_ASSERT_NONNULL(occurrence)));
      }
      sortOccurrences(occurrences);
      if (occurrences.empty()) { return zc::none; }
      auto authorityOccurrence = occurrences[0].clone();
      auto membership = ActiveImplementationMembershipRecord::from(
          stableImplementation.clone(), entry.record().clone(), authorityOccurrence.clone(),
          cloneOccurrences(occurrences.asPtr()));
      if (membership == zc::none) { return zc::none; }
      auto key =
          ContextualImplementationKey::from(contextRoots.clone(), stableImplementation.clone());
      nextLedger.implementationKeyFields.add(key.clone());
      implementations.add(
          ImplementationEntry::from(zc::mv(key), zc::mv(ZC_ASSERT_NONNULL(membership))));

      auto occurrenceSite = findImplementationSite(ZC_ASSERT_NONNULL(implementationSites),
                                                   authorityOccurrence.occurrence());
      if (occurrenceSite == zc::none) { return zc::none; }
      auto header = binder::ImplementationHeaderProducer::produce(binder::ImplementationHeaderInput{
          ZC_ASSERT_NONNULL(canonicalParsed), authorityOccurrence, entry,
          ZC_ASSERT_NONNULL(occurrenceSite), ZC_ASSERT_NONNULL(definitionSites),
          ZC_ASSERT_NONNULL(implementationSites)});
      if (header == zc::none) { return zc::none; }
      const auto& headerValue = ZC_ASSERT_NONNULL(header);
      auto genericOwner = ImplementationGenericAuthority::from(
          stableImplementation.clone(), authorityOccurrence.clone(),
          cloneOccurrences(occurrences.asPtr()));
      if (genericOwner == zc::none) { return zc::none; }
      for (const auto& parameter : headerValue.genericParameters().values()) {
        auto stableParameter =
            binder::StableGenericParameterQueryKey::from(module.clone(), parameter.key().clone());
        auto parameterMembership = ActiveGenericParameterMembership::from(
            stableParameter.clone(), parameter.record().clone(),
            ActiveGenericParameterOwner::implementation(ZC_ASSERT_NONNULL(genericOwner).clone()),
            parameter.ordinal());
        if (parameterMembership == zc::none) { return zc::none; }
        auto parameterKey =
            ContextualGenericParameterKey::from(contextRoots.clone(), stableParameter.clone());
        nextLedger.genericParameterKeyFields.add(parameterKey.clone());
        genericParameters.add(GenericEntry::from(zc::mv(parameterKey),
                                                 zc::mv(ZC_ASSERT_NONNULL(parameterMembership))));
      }
    }
  }

  auto definitionProjection =
      ActiveDefinitionAuthorityProjection::from(contextRoots, zc::mv(projectionRecords));
  if (!canonicalizeAuthorityEntries(definitions) ||
      !canonicalizeAuthorityEntries(implementations) ||
      !canonicalizeAuthorityEntries(genericParameters) ||
      !canonicalizeAuthorityEntries(callableParameters)) {
    return zc::none;
  }
  auto definitionDigest = authorityDigest(kDefinitionAuthoritySetDomain, contextRoots, definitions);
  auto implementationDigest =
      authorityDigest(kImplementationAuthoritySetDomain, contextRoots, implementations);
  auto genericDigest = authorityDigest(kGenericAuthoritySetDomain, contextRoots, genericParameters);
  auto callableDigest =
      authorityDigest(kCallableAuthoritySetDomain, contextRoots, callableParameters);
  if (definitionProjection == zc::none || definitionDigest == zc::none ||
      implementationDigest == zc::none || genericDigest == zc::none || callableDigest == zc::none) {
    return zc::none;
  }
  auto readiness = CompleteRootIdentityReadiness::from(
      contextRoots.clone(), ZC_ASSERT_NONNULL(definitionDigest),
      ZC_ASSERT_NONNULL(implementationDigest), ZC_ASSERT_NONNULL(genericDigest),
      ZC_ASSERT_NONNULL(callableDigest));

  auto payload = ContextualIdentityAuthorityInputPayload::from(
      contextRoots.clone(), zc::mv(definitions), zc::mv(implementations), zc::mv(genericParameters),
      zc::mv(callableParameters), zc::mv(readiness));
  if (payload == zc::none || !ContextualIdentityAuthorityInputVerifier::verify(
                                 authorityStagingSnapshot, ZC_ASSERT_NONNULL(payload))) {
    return zc::none;
  }
  auto payloadBytes = ZC_ASSERT_NONNULL(payload).encodeCanonical();
  auto payloadDigest = module_graph_query::computeCanonicalInputPayloadDigest(
      kAuthorityTransactionDomain, payloadBytes.asPtr());
  if (payloadDigest == zc::none) { return zc::none; }

  ContextualIdentityAuthorityInputLedger prior;
  for (const auto& key : priorLedger.definitionKeyFields) {
    prior.definitionKeyFields.add(key.clone());
  }
  for (const auto& key : priorLedger.implementationKeyFields) {
    prior.implementationKeyFields.add(key.clone());
  }
  for (const auto& key : priorLedger.genericParameterKeyFields) {
    prior.genericParameterKeyFields.add(key.clone());
  }
  for (const auto& key : priorLedger.callableParameterKeyFields) {
    prior.callableParameterKeyFields.add(key.clone());
  }
  ZC_IF_SOME(priorRoots, priorLedger.contextRootsField) {
    prior.contextRootsField = priorRoots.clone();
  }

  auto data = zc::heap<Impl>(Impl{expectedPreviousRevision, zc::mv(ZC_ASSERT_NONNULL(payload)),
                                  ZC_ASSERT_NONNULL(definitionProjection).fingerprint().clone(),
                                  zc::mv(prior), zc::mv(nextLedger),
                                  zc::mv(ZC_ASSERT_NONNULL(payloadDigest)), false});
  return ContextualIdentityAuthorityInputTransaction(zc::mv(data));
}

ContextualIdentityAuthorityInputLedger
ContextualIdentityAuthorityInputTransaction::takeNextLedger() && {
  return zc::mv(impl->nextLedger);
}

const ContextualIdentityAuthorityInputPayload&
ContextualIdentityAuthorityInputTransaction::payload() const noexcept {
  return impl->payload;
}

query::InputCommitResult ContextualIdentityAuthorityInputTransaction::commit(
    query::QueryDatabase& database) {
  if (impl.get() == nullptr || impl->committed) {
    return query::InputCommitResult::rejected(query::InputTransactionFailure::TransactionClosed);
  }
  auto opened = database.beginInputTransaction(impl->expectedPreviousRevision);
  if (!opened.isOpened()) { return query::InputCommitResult::rejected(opened.failure()); }
  auto transaction = zc::mv(opened).takeTransaction();
  const auto reject = [&](query::InputTransactionFailure failure) {
    transaction.abandon();
    return query::InputCommitResult::rejected(failure);
  };

  ZC_IF_SOME(priorRoots, impl->priorLedger.contextRootsField) {
    for (const auto& key : impl->priorLedger.definitionKeyFields) {
      if (priorRoots == impl->payload.contextRoots() &&
          contains(impl->nextLedger.definitionKeyFields, key)) {
        continue;
      }
      auto contextual = ContextualDefinitionKey::from(priorRoots.clone(), key.clone());
      auto mutation = transaction.erase<ActiveDefinitionAuthorityInput>(contextual);
      if (!mutation.isApplied()) { return reject(mutation.failure()); }
    }
  }
  for (const auto& key : impl->priorLedger.implementationKeyFields) {
    if (contains(impl->nextLedger.implementationKeyFields, key)) { continue; }
    auto mutation = transaction.erase<ActiveImplementationAuthorityInput>(key);
    if (!mutation.isApplied()) { return reject(mutation.failure()); }
  }
  for (const auto& key : impl->priorLedger.genericParameterKeyFields) {
    if (contains(impl->nextLedger.genericParameterKeyFields, key)) { continue; }
    auto mutation = transaction.erase<ActiveGenericParameterAuthorityInput>(key);
    if (!mutation.isApplied()) { return reject(mutation.failure()); }
  }
  for (const auto& key : impl->priorLedger.callableParameterKeyFields) {
    if (contains(impl->nextLedger.callableParameterKeyFields, key)) { continue; }
    auto mutation = transaction.erase<ActiveCallableParameterAuthorityInput>(key);
    if (!mutation.isApplied()) { return reject(mutation.failure()); }
  }

  for (const auto& entry : impl->payload.definitionAuthorities()) {
    auto mutation = transaction.set<ActiveDefinitionAuthorityInput>(entry.key(), entry.value());
    if (!mutation.isApplied()) { return reject(mutation.failure()); }
  }
  for (const auto& entry : impl->payload.implementationAuthorities()) {
    auto mutation = transaction.set<ActiveImplementationAuthorityInput>(entry.key(), entry.value());
    if (!mutation.isApplied()) { return reject(mutation.failure()); }
  }
  for (const auto& entry : impl->payload.genericParameterAuthorities()) {
    auto mutation =
        transaction.set<ActiveGenericParameterAuthorityInput>(entry.key(), entry.value());
    if (!mutation.isApplied()) { return reject(mutation.failure()); }
  }
  for (const auto& entry : impl->payload.callableParameterAuthorities()) {
    auto mutation =
        transaction.set<ActiveCallableParameterAuthorityInput>(entry.key(), entry.value());
    if (!mutation.isApplied()) { return reject(mutation.failure()); }
  }

  auto readyMutation = transaction.set<ActiveDefinitionAuthorityReadyInput>(
      impl->payload.contextRoots(), impl->definitionFingerprint);
  if (!readyMutation.isApplied()) { return reject(readyMutation.failure()); }
  auto completeMutation = transaction.set<CompleteRootIdentityReadinessInput>(
      impl->payload.contextRoots(), impl->payload.completeRootReadiness());
  if (!completeMutation.isApplied()) { return reject(completeMutation.failure()); }
  auto witnessMutation =
      transaction.set<module_graph_query::ContextualIdentityAuthorityTransactionWitnessInput>(
          impl->payload.contextRoots(), impl->payloadDigest);
  if (!witnessMutation.isApplied()) { return reject(witnessMutation.failure()); }

  auto result = transaction.commit();
  if (!result.isCommitted()) { return result; }
  impl->committed = true;
  return result;
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
