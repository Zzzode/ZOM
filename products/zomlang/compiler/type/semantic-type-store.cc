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

#include "zc/core/map.h"
#include "zc/core/mutex.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/type/type-algebra.h"
#include "zomlang/compiler/type/type-canonical-key.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang::compiler::type {
namespace {

struct StoredSemanticType final {
  StoredSemanticType(zc::String&& canonicalKey, zc::Own<Type>&& data)
      : key(zc::mv(canonicalKey)), value(zc::mv(data)) {}
  StoredSemanticType(StoredSemanticType&&) noexcept = default;
  StoredSemanticType& operator=(StoredSemanticType&&) noexcept = default;
  ZC_DISALLOW_COPY(StoredSemanticType);

  zc::String key;
  zc::Own<Type> value;
};

struct SemanticTypeStoreData final {
  zc::HashMap<zc::String, identity::SemanticTypeId> idsByKey;
  zc::Vector<StoredSemanticType> entries;
};

}  // namespace

struct SemanticTypeStore::Impl final {
  explicit Impl(identity::SemanticContextBrand owner) : context(owner) {}

  identity::SemanticContextBrand context;
  zc::MutexGuarded<SemanticTypeStoreData> data;
};

SemanticTypeStore::SemanticTypeStore(identity::SemanticTypeStoreConstructionToken&& token)
    : impl(zc::heap<Impl>(token.consume())) {
  ZC_IREQUIRE(impl->context.isValid(),
              "SemanticTypeStore: construction token is invalid or already consumed");
}

SemanticTypeStore::~SemanticTypeStore() noexcept(false) = default;

identity::SemanticTypeId SemanticTypeStore::intern(const Type& type) {
  auto key = canonicalTypeKey(type);
  auto ownedType = cloneType(type);
  auto locked = impl->data.lockExclusive();
  ZC_IF_SOME(existing, locked->idsByKey.find(key)) { return existing; }

  ZC_IREQUIRE(locked->entries.size() <= static_cast<uint64_t>(0xffffffffu),
              "SemanticTypeStore::intern: semantic type slot space exhausted");
  const auto id = identity::SemanticTypeTag::issue(impl->context,
                                                   static_cast<uint32_t>(locked->entries.size()));
  locked->idsByKey.insert(zc::str(key), id);
  locked->entries.add(StoredSemanticType(zc::mv(key), zc::mv(ownedType)));
  return id;
}

identity::SemanticTypeId SemanticTypeStore::internUnion(const Type& first, const Type& second) {
  zc::Vector<zc::Own<Type>> alternatives;
  alternatives.add(cloneType(first));
  alternatives.add(cloneType(second));
  UnionType unionType(zc::mv(alternatives));
  return intern(unionType);
}

bool SemanticTypeStore::contains(identity::SemanticTypeId id) const {
  if (!id.isValid() || identity::SemanticTypeTag::context(id) != impl->context) { return false; }
  auto locked = impl->data.lockShared();
  return identity::SemanticTypeTag::slot(id) < locked->entries.size();
}

zc::Maybe<SemanticTypeLookup> SemanticTypeStore::get(identity::SemanticTypeId id) const {
  if (!id.isValid() || identity::SemanticTypeTag::context(id) != impl->context) { return zc::none; }
  auto locked = impl->data.lockShared();
  const auto slot = identity::SemanticTypeTag::slot(id);
  if (slot >= locked->entries.size()) { return zc::none; }
  const auto& entry = locked->entries[slot];
  return SemanticTypeLookup{*entry.value, entry.key};
}

zc::StringPtr SemanticTypeStore::getCanonicalKey(identity::SemanticTypeId id) const {
  ZC_IF_SOME(found, get(id)) { return found.canonicalKey; }
  ZC_IREQUIRE(false, "SemanticTypeStore::getCanonicalKey: identity does not belong to this store");
  return zc::StringPtr();
}

size_t SemanticTypeStore::size() const {
  auto locked = impl->data.lockShared();
  return locked->entries.size();
}

identity::SemanticContextBrand SemanticTypeStore::context() const noexcept { return impl->context; }

}  // namespace zomlang::compiler::type
