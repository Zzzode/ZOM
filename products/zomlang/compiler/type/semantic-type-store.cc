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

#include "zomlang/compiler/type/semantic-type-store.h"

#include <cstdint>

#include "zc/core/arena.h"
#include "zc/core/map.h"
#include "zc/core/mutex.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::type {
namespace {

struct StoredSemanticType final {
  StoredSemanticType(semantic::TypeData&& data, semantic::SemanticTypeKey&& key)
      : data(zc::mv(data)), key(zc::mv(key)) {}
  StoredSemanticType(StoredSemanticType&&) noexcept = default;
  StoredSemanticType& operator=(StoredSemanticType&&) noexcept = default;
  ZC_DISALLOW_COPY(StoredSemanticType);

  semantic::TypeData data;
  semantic::SemanticTypeKey key;
};

struct SemanticTypeStoreData final {
  zc::Arena arena;
  zc::Vector<zc::Maybe<const StoredSemanticType&>> entries;
  zc::HashMap<zc::ArrayPtr<const uint8_t>, identity::SemanticTypeId> idsByKey;
};

identity::IdentityInvariant invariant(identity::IdentityInvariantKind kind,
                                      identity::IdentityApiSite apiSite, uint32_t ordinal,
                                      zc::Maybe<zc::Array<uint8_t>>&& structuralKey = zc::none) {
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto result =
      identity::IdentityInvariant::from(kind, identity::IdentityAllocationPhase::SemanticType,
                                        zc::mv(structuralKey), zc::mv(noRange), apiSite, ordinal);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

const StoredSemanticType& entryAt(const SemanticTypeStoreData& data, uint32_t slot) {
  ZC_IF_SOME(entry, data.entries[slot]) { return entry; }
  ZC_UNREACHABLE
}

}  // namespace

struct SemanticTypeStore::Impl final {
  Impl(identity::SemanticContextBrand owner,
       const identity::CanonicalIdentityInternerSet& identities) noexcept
      : context(owner), identities(identities) {}

  identity::SemanticContextBrand context;
  const identity::CanonicalIdentityInternerSet& identities;
  zc::MutexGuarded<SemanticTypeStoreData> data;
};

SemanticTypeStore::SemanticTypeStore(identity::SemanticTypeStoreConstructionToken&& token,
                                     const identity::CanonicalIdentityInternerSet& identities)
    : impl(zc::heap<Impl>(token.consume(), identities)) {
  ZC_IREQUIRE(impl->context.isValid(),
              "SemanticTypeStore: construction token is invalid or already consumed");
  ZC_IREQUIRE(impl->identities.context() == impl->context,
              "SemanticTypeStore: identity authority belongs to a different semantic context");
}

SemanticTypeStore::~SemanticTypeStore() noexcept(false) = default;

SemanticTypeInternResult SemanticTypeStore::intern(semantic::CanonicalTypeData&& canonical) {
  if (canonical.admissionContext != impl->context) {
    zc::Maybe<zc::Array<uint8_t>> keyBytes = zc::heapArray(canonical.keyValue.bytes());
    return invariant(identity::IdentityInvariantKind::ForeignContext,
                     identity::IdentityApiSite::RegistryMutation, 0, zc::mv(keyBytes));
  }
  auto locked = impl->data.lockExclusive();
  ZC_IF_SOME(existing, locked->idsByKey.find(canonical.keyValue.bytes())) {
    const auto slot = identity::SemanticTypeTag::slot(existing);
    if (slot >= locked->entries.size() ||
        entryAt(*locked, slot).key.bytes() != canonical.keyValue.bytes()) {
      zc::Maybe<zc::Array<uint8_t>> keyBytes = zc::heapArray(canonical.keyValue.bytes());
      return invariant(identity::IdentityInvariantKind::DuplicateCanonicalKey,
                       identity::IdentityApiSite::RegistryMutation, slot, zc::mv(keyBytes));
    }
    return SemanticTypeInterned{existing};
  }

  if (locked->entries.size() > static_cast<uint64_t>(UINT32_MAX)) {
    zc::Maybe<zc::Array<uint8_t>> keyBytes = zc::heapArray(canonical.keyValue.bytes());
    return invariant(identity::IdentityInvariantKind::SlotOutOfRange,
                     identity::IdentityApiSite::RegistryMutation, UINT32_MAX, zc::mv(keyBytes));
  }
  const auto id = identity::SemanticTypeTag::issue(impl->context,
                                                   static_cast<uint32_t>(locked->entries.size()));
  auto& stored = locked->arena.allocate<StoredSemanticType>(zc::mv(canonical.dataValue),
                                                            zc::mv(canonical.keyValue));
  locked->entries.add(stored);
  locked->idsByKey.insert(stored.key.bytes(), id);
  return SemanticTypeInterned{id};
}

zc::Maybe<identity::IdentityInvariantKind> SemanticTypeStore::validateTypeForAdmission(
    identity::SemanticTypeId id) const {
  if (!id.isValid()) { return identity::IdentityInvariantKind::InvalidHandle; }
  if (identity::SemanticTypeTag::context(id) != impl->context) {
    return identity::IdentityInvariantKind::ForeignContext;
  }
  auto locked = impl->data.lockShared();
  if (identity::SemanticTypeTag::slot(id) >= locked->entries.size()) {
    return identity::IdentityInvariantKind::SlotOutOfRange;
  }
  return zc::none;
}

zc::Maybe<const semantic::TypeData&> SemanticTypeStore::typeDataForAdmission(
    identity::SemanticTypeId id) const {
  if (validateTypeForAdmission(id) != zc::none) { return zc::none; }
  auto locked = impl->data.lockShared();
  return entryAt(*locked, identity::SemanticTypeTag::slot(id)).data;
}

zc::Maybe<identity::IdentityInvariantKind> SemanticTypeStore::validateDefinitionForAdmission(
    identity::DefId id) const {
  if (!id.isValid()) { return identity::IdentityInvariantKind::InvalidHandle; }
  if (!id.belongsTo(impl->context)) { return identity::IdentityInvariantKind::ForeignContext; }
  return impl->identities.definition(id) == zc::none
             ? zc::Maybe<identity::IdentityInvariantKind>(
                   identity::IdentityInvariantKind::SlotOutOfRange)
             : zc::none;
}

zc::Maybe<identity::DefinitionKey> SemanticTypeStore::definitionKeyForAdmission(
    identity::DefId id) const {
  ZC_IF_SOME(entry, impl->identities.definition(id)) { return entry.key().clone(); }
  return zc::none;
}

zc::Maybe<identity::DefinitionIdentityRecord> SemanticTypeStore::definitionRecordForAdmission(
    identity::DefId id) const {
  ZC_IF_SOME(entry, impl->identities.definition(id)) { return entry.record().clone(); }
  return zc::none;
}

zc::Maybe<identity::IdentityInvariantKind> SemanticTypeStore::validateGenericParameterForAdmission(
    const identity::GenericParameterKey& key) const {
  return impl->identities.genericParameter(key) == zc::none
             ? zc::Maybe<identity::IdentityInvariantKind>(
                   identity::IdentityInvariantKind::InvalidHandle)
             : zc::none;
}

SemanticTypeLookupResult SemanticTypeStore::get(identity::SemanticTypeId id) const {
  if (!id.isValid()) {
    return invariant(identity::IdentityInvariantKind::InvalidHandle,
                     identity::IdentityApiSite::HandleLookup, 0);
  }
  if (identity::SemanticTypeTag::context(id) != impl->context) {
    return invariant(identity::IdentityInvariantKind::ForeignContext,
                     identity::IdentityApiSite::HandleLookup, 0);
  }
  auto locked = impl->data.lockShared();
  const auto slot = identity::SemanticTypeTag::slot(id);
  if (slot >= locked->entries.size()) {
    return invariant(identity::IdentityInvariantKind::SlotOutOfRange,
                     identity::IdentityApiSite::HandleLookup, slot);
  }
  const auto& entry = entryAt(*locked, slot);
  return SemanticTypeLookup(entry.data, entry.key);
}

SemanticTypeLookup::SemanticTypeLookup(const semantic::TypeData& data,
                                       const semantic::SemanticTypeKey& key) noexcept
    : dataValue(data), keyValue(key) {}

const semantic::TypeData& SemanticTypeLookup::data() const {
  ZC_IF_SOME(value, dataValue) { return value; }
  ZC_UNREACHABLE
}

const semantic::SemanticTypeKey& SemanticTypeLookup::key() const {
  ZC_IF_SOME(value, keyValue) { return value; }
  ZC_UNREACHABLE
}

size_t SemanticTypeStore::size() const {
  auto locked = impl->data.lockShared();
  return locked->entries.size();
}

identity::SemanticContextBrand SemanticTypeStore::context() const noexcept { return impl->context; }

}  // namespace zomlang::compiler::type
