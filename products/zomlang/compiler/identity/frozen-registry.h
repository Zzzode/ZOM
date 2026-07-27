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

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/handle.h"

namespace zomlang::compiler::identity {

class SemanticIdentityRegistrySet;

/// \brief Internal failure classification returned before diagnostic adaptation.
enum class FrozenRegistryFailure : uint8_t {
  None,
  InvalidContext,
  InvalidHandle,
  ForeignContext,
  SlotOutOfRange,
  DuplicateCanonicalKey,
  DigestCollision,
  InvalidAuthority,
  UnknownOwner,
  OwnerModuleMismatch,
  OwnerPrefixMismatch,
  RepeatedOwner,
  SelfOwner,
  AncestorMismatch,
  PostFreezeMutation,
  RegistryNotFrozen
};

/// \brief Context-owned identity registry frozen in canonical encoded-key order.
template <typename Key, typename Tag>
class FrozenContextRegistry final {
public:
  using Handle = ContextHandle<Tag>;

  /// \brief Immutable owned key projection preserving deterministic handle-slot lookup.
  class FrozenKeyIndex final {
  public:
    FrozenKeyIndex(FrozenKeyIndex&&) noexcept = default;
    FrozenKeyIndex& operator=(FrozenKeyIndex&&) noexcept = default;
    ZC_DISALLOW_COPY(FrozenKeyIndex);

    /// \brief Looks up one canonical key without retaining the issuing registry.
    ZC_NODISCARD zc::Maybe<const Key&> lookup(Handle handle) const {
      return FrozenContextRegistry::lookupProjectedKey(owner, keys.asPtr(), handle);
    }

  private:
    FrozenKeyIndex(SemanticContextBrand owner, zc::Array<Key>&& keys) noexcept
        : owner(owner), keys(zc::mv(keys)) {}

    SemanticContextBrand owner;
    zc::Array<Key> keys;

    friend class FrozenContextRegistry;
  };

  FrozenContextRegistry(FrozenContextRegistry&&) noexcept = default;
  FrozenContextRegistry& operator=(FrozenContextRegistry&&) noexcept = default;
  ZC_DISALLOW_COPY(FrozenContextRegistry);

  /// \brief Collects one canonical key before registry freeze.
  ZC_NODISCARD FrozenRegistryFailure collect(Key&& key) {
    if (!owner.isValid()) { return fail(FrozenRegistryFailure::InvalidContext); }
    if (state != State::Collecting) { return fail(FrozenRegistryFailure::PostFreezeMutation); }
    entries.add(Entry(zc::mv(key)));
    return FrozenRegistryFailure::None;
  }

  /// \brief Sorts keys, rejects duplicates, and assigns deterministic slots.
  ZC_NODISCARD FrozenRegistryFailure freeze() {
    if (!owner.isValid()) { return fail(FrozenRegistryFailure::InvalidContext); }
    if (state != State::Collecting) { return fail(FrozenRegistryFailure::PostFreezeMutation); }
    if (entries.size() > static_cast<uint64_t>(0xffffffffu)) {
      return fail(FrozenRegistryFailure::SlotOutOfRange);
    }

    sortEntries();
    for (size_t index = 1; index < entries.size(); ++index) {
      if (sameBytes(entries[index - 1].encoded, entries[index].encoded)) {
        failureStructuralKeyValue = zc::heapArray(entries[index].encoded.asPtr());
        return fail(FrozenRegistryFailure::DuplicateCanonicalKey);
      }
    }
    state = State::Frozen;
    return FrozenRegistryFailure::None;
  }

  /// \brief Returns whether deterministic handles may be observed.
  ZC_NODISCARD bool isFrozen() const noexcept { return state == State::Frozen; }

  /// \brief Returns the terminal registry failure, if one invalidated this registry.
  ZC_NODISCARD FrozenRegistryFailure terminalFailure() const noexcept { return failure; }

  /// \brief Returns the canonical key associated with a terminal freeze failure.
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const uint8_t>> failureStructuralKey() const {
    ZC_IF_SOME(value, failureStructuralKeyValue) { return value.asPtr(); }
    return zc::none;
  }

  /// \brief Returns the number of collected canonical keys.
  ZC_NODISCARD size_t size() const noexcept { return entries.size(); }

  /// \brief Clones the frozen handle-to-key projection into independently owned storage.
  ZC_NODISCARD zc::Maybe<FrozenKeyIndex> snapshotKeys() const {
    if (!isFrozen()) { return zc::none; }
    zc::Vector<Key> keys;
    keys.reserve(entries.size());
    for (const auto& entry : entries) { keys.add(entry.key.clone()); }
    return FrozenKeyIndex(owner, keys.releaseAsArray());
  }

  /// \brief Returns the canonical key at one deterministic slot after freeze.
  ZC_NODISCARD zc::Maybe<const Key&> keyAt(size_t slot) const {
    if (!isFrozen() || slot >= entries.size()) { return zc::none; }
    return entries[slot].key;
  }

  /// \brief Resolves a canonical key to its deterministic context handle.
  ZC_NODISCARD zc::Maybe<Handle> find(const Key& key) const {
    if (!isFrozen()) { return zc::none; }
    const auto encoded = key.encode();
    for (size_t slot = 0; slot < entries.size(); ++slot) {
      if (sameBytes(entries[slot].encoded, encoded)) {
        return Handle(owner, static_cast<uint32_t>(slot));
      }
    }
    return zc::none;
  }

  /// \brief Validates one handle without dereferencing it.
  ZC_NODISCARD FrozenRegistryFailure validate(Handle handle) const noexcept {
    if (!isFrozen()) { return FrozenRegistryFailure::RegistryNotFrozen; }
    if (!handle.isValid()) { return FrozenRegistryFailure::InvalidHandle; }
    if (handle.context != owner) { return FrozenRegistryFailure::ForeignContext; }
    if (handle.slot >= entries.size()) { return FrozenRegistryFailure::SlotOutOfRange; }
    return FrozenRegistryFailure::None;
  }

  /// \brief Looks up a key only after context and range validation.
  ZC_NODISCARD zc::Maybe<const Key&> lookup(Handle handle) const {
    if (validate(handle) != FrozenRegistryFailure::None) { return zc::none; }
    return entries[handle.slot].key;
  }

private:
  explicit FrozenContextRegistry(SemanticContextBrand context) noexcept : owner(context) {}

  enum class State : uint8_t { Collecting, Frozen, Invalid };

  struct Entry final {
    explicit Entry(Key&& input) : key(zc::mv(input)), encoded(key.encode()) {}
    Entry(Entry&&) noexcept = default;
    Entry& operator=(Entry&&) noexcept = default;
    ZC_DISALLOW_COPY(Entry);

    Key key;
    zc::Array<uint8_t> encoded;
  };

  static bool lessBytes(zc::ArrayPtr<const uint8_t> left,
                        zc::ArrayPtr<const uint8_t> right) noexcept {
    const size_t sharedSize = left.size() < right.size() ? left.size() : right.size();
    for (size_t index = 0; index < sharedSize; ++index) {
      if (left[index] != right[index]) { return left[index] < right[index]; }
    }
    return left.size() < right.size();
  }

  static bool sameBytes(zc::ArrayPtr<const uint8_t> left,
                        zc::ArrayPtr<const uint8_t> right) noexcept {
    if (left.size() != right.size()) { return false; }
    for (size_t index = 0; index < left.size(); ++index) {
      if (left[index] != right[index]) { return false; }
    }
    return true;
  }

  static zc::Maybe<const Key&> lookupProjectedKey(SemanticContextBrand owner,
                                                  zc::ArrayPtr<const Key> keys, Handle handle) {
    if (!handle.belongsTo(owner) || handle.slot >= keys.size()) { return zc::none; }
    return keys[handle.slot];
  }

  void sortEntries() {
    for (size_t index = 1; index < entries.size(); ++index) {
      auto current = zc::mv(entries[index]);
      size_t insertion = index;
      while (insertion > 0 &&
             lessBytes(current.encoded.asPtr(), entries[insertion - 1].encoded.asPtr())) {
        entries[insertion] = zc::mv(entries[insertion - 1]);
        --insertion;
      }
      entries[insertion] = zc::mv(current);
    }
  }

  FrozenRegistryFailure fail(FrozenRegistryFailure value) noexcept {
    if (state != State::Invalid) {
      state = State::Invalid;
      failure = value;
    }
    return value;
  }

  SemanticContextBrand owner;
  State state = State::Collecting;
  FrozenRegistryFailure failure = FrozenRegistryFailure::None;
  zc::Maybe<zc::Array<uint8_t>> failureStructuralKeyValue;
  zc::Vector<Entry> entries;

  friend class SemanticIdentityRegistrySet;
};

/// \brief Context-owned stable authority registry with complete-record collision checks.
template <typename Key, typename Record, typename Authority, typename Tag>
class FrozenAuthorityRegistry final {
public:
  using Handle = ContextHandle<Tag>;

  class FrozenKeyIndex final {
  public:
    FrozenKeyIndex(FrozenKeyIndex&&) noexcept = default;
    FrozenKeyIndex& operator=(FrozenKeyIndex&&) noexcept = default;
    ZC_DISALLOW_COPY(FrozenKeyIndex);

    ZC_NODISCARD zc::Maybe<const Key&> lookup(Handle handle) const {
      if (!handle.belongsTo(owner) || handle.slot >= keys.size()) { return zc::none; }
      return keys[handle.slot];
    }

  private:
    FrozenKeyIndex(SemanticContextBrand owner, zc::Array<Key>&& keys) noexcept
        : owner(owner), keys(zc::mv(keys)) {}

    SemanticContextBrand owner;
    zc::Array<Key> keys;

    friend class FrozenAuthorityRegistry;
  };

  FrozenAuthorityRegistry(FrozenAuthorityRegistry&&) noexcept = default;
  FrozenAuthorityRegistry& operator=(FrozenAuthorityRegistry&&) noexcept = default;
  ZC_DISALLOW_COPY(FrozenAuthorityRegistry);

  /// \brief Admits one verified authority or coalesces an equal complete authority.
  ZC_NODISCARD FrozenRegistryFailure collect(Authority&& authority) {
    if (!owner.isValid()) { return fail(FrozenRegistryFailure::InvalidContext); }
    if (state != State::Collecting) { return fail(FrozenRegistryFailure::PostFreezeMutation); }
    if (!authority.verify()) { return fail(FrozenRegistryFailure::InvalidAuthority); }

    auto encodedKey = authority.key().encode();
    for (const auto& entry : entries) {
      if (!sameBytes(entry.encodedKey.asPtr(), encodedKey.asPtr())) { continue; }
      if (entry.authority.sameRecordAs(authority)) { return FrozenRegistryFailure::None; }
      failureStructuralKeyValue = zc::heapArray(encodedKey.asPtr());
      return fail(FrozenRegistryFailure::DigestCollision);
    }

    entries.add(Entry(zc::mv(authority), zc::mv(encodedKey)));
    return FrozenRegistryFailure::None;
  }

  /// \brief Assigns deterministic handles after all complete authorities are admitted.
  ZC_NODISCARD FrozenRegistryFailure freeze() {
    if (!owner.isValid()) { return fail(FrozenRegistryFailure::InvalidContext); }
    if (state != State::Collecting) { return fail(FrozenRegistryFailure::PostFreezeMutation); }
    if (entries.size() > static_cast<uint64_t>(0xffffffffu)) {
      return fail(FrozenRegistryFailure::SlotOutOfRange);
    }
    sortEntries();
    state = State::Frozen;
    return FrozenRegistryFailure::None;
  }

  ZC_NODISCARD bool isCollecting() const noexcept { return state == State::Collecting; }
  ZC_NODISCARD bool isFrozen() const noexcept { return state == State::Frozen; }
  ZC_NODISCARD FrozenRegistryFailure terminalFailure() const noexcept { return failure; }
  ZC_NODISCARD size_t size() const noexcept { return entries.size(); }

  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const uint8_t>> failureStructuralKey() const {
    ZC_IF_SOME(value, failureStructuralKeyValue) { return value.asPtr(); }
    return zc::none;
  }

  /// \brief Finds an already-admitted owner before or after deterministic handle freeze.
  ZC_NODISCARD zc::Maybe<const Authority&> admittedAuthority(const Key& key) const {
    if (state == State::Invalid) { return zc::none; }
    const auto encoded = key.encode();
    for (const auto& entry : entries) {
      if (sameBytes(entry.encodedKey.asPtr(), encoded.asPtr())) { return entry.authority; }
    }
    return zc::none;
  }

  ZC_NODISCARD zc::Maybe<FrozenKeyIndex> snapshotKeys() const {
    if (!isFrozen()) { return zc::none; }
    zc::Vector<Key> keys;
    keys.reserve(entries.size());
    for (const auto& entry : entries) { keys.add(entry.authority.key().clone()); }
    return FrozenKeyIndex(owner, keys.releaseAsArray());
  }

  ZC_NODISCARD zc::Maybe<const Key&> keyAt(size_t slot) const {
    if (!isFrozen() || slot >= entries.size()) { return zc::none; }
    return entries[slot].authority.key();
  }

  ZC_NODISCARD zc::Maybe<const Record&> recordAt(size_t slot) const {
    if (!isFrozen() || slot >= entries.size()) { return zc::none; }
    return entries[slot].authority.record();
  }

  ZC_NODISCARD zc::Maybe<const Authority&> authorityAt(size_t slot) const {
    if (!isFrozen() || slot >= entries.size()) { return zc::none; }
    return entries[slot].authority;
  }

  ZC_NODISCARD zc::Maybe<Handle> find(const Key& key) const {
    if (!isFrozen()) { return zc::none; }
    const auto encoded = key.encode();
    for (size_t slot = 0; slot < entries.size(); ++slot) {
      if (sameBytes(entries[slot].encodedKey.asPtr(), encoded.asPtr())) {
        return Handle(owner, static_cast<uint32_t>(slot));
      }
    }
    return zc::none;
  }

  ZC_NODISCARD FrozenRegistryFailure validate(Handle handle) const noexcept {
    if (!isFrozen()) { return FrozenRegistryFailure::RegistryNotFrozen; }
    if (!handle.isValid()) { return FrozenRegistryFailure::InvalidHandle; }
    if (handle.context != owner) { return FrozenRegistryFailure::ForeignContext; }
    if (handle.slot >= entries.size()) { return FrozenRegistryFailure::SlotOutOfRange; }
    return FrozenRegistryFailure::None;
  }

  ZC_NODISCARD zc::Maybe<const Key&> lookup(Handle handle) const {
    if (validate(handle) != FrozenRegistryFailure::None) { return zc::none; }
    return entries[handle.slot].authority.key();
  }

  ZC_NODISCARD zc::Maybe<const Record&> lookupRecord(Handle handle) const {
    if (validate(handle) != FrozenRegistryFailure::None) { return zc::none; }
    return entries[handle.slot].authority.record();
  }

  ZC_NODISCARD zc::Maybe<const Authority&> lookupAuthority(Handle handle) const {
    if (validate(handle) != FrozenRegistryFailure::None) { return zc::none; }
    return entries[handle.slot].authority;
  }

private:
  explicit FrozenAuthorityRegistry(SemanticContextBrand context) noexcept : owner(context) {}

  enum class State : uint8_t { Collecting, Frozen, Invalid };

  struct Entry final {
    Entry(Authority&& authority, zc::Array<uint8_t>&& encodedKey)
        : authority(zc::mv(authority)), encodedKey(zc::mv(encodedKey)) {}
    Entry(Entry&&) noexcept = default;
    Entry& operator=(Entry&&) noexcept = default;
    ZC_DISALLOW_COPY(Entry);

    Authority authority;
    zc::Array<uint8_t> encodedKey;
  };

  static bool lessBytes(zc::ArrayPtr<const uint8_t> left,
                        zc::ArrayPtr<const uint8_t> right) noexcept {
    const size_t sharedSize = left.size() < right.size() ? left.size() : right.size();
    for (size_t index = 0; index < sharedSize; ++index) {
      if (left[index] != right[index]) { return left[index] < right[index]; }
    }
    return left.size() < right.size();
  }

  static bool sameBytes(zc::ArrayPtr<const uint8_t> left,
                        zc::ArrayPtr<const uint8_t> right) noexcept {
    return left == right;
  }

  void sortEntries() {
    for (size_t index = 1; index < entries.size(); ++index) {
      auto current = zc::mv(entries[index]);
      size_t insertion = index;
      while (insertion > 0 &&
             lessBytes(current.encodedKey.asPtr(), entries[insertion - 1].encodedKey.asPtr())) {
        entries[insertion] = zc::mv(entries[insertion - 1]);
        --insertion;
      }
      entries[insertion] = zc::mv(current);
    }
  }

  FrozenRegistryFailure fail(FrozenRegistryFailure value) noexcept {
    if (state != State::Invalid) {
      state = State::Invalid;
      failure = value;
    }
    return value;
  }

  SemanticContextBrand owner;
  State state = State::Collecting;
  FrozenRegistryFailure failure = FrozenRegistryFailure::None;
  zc::Maybe<zc::Array<uint8_t>> failureStructuralKeyValue;
  zc::Vector<Entry> entries;

  friend class SemanticIdentityRegistrySet;
};

struct CompilationUnitIdentityTag final {};
struct CrateIdentityTag final {};
struct SourceFileIdentityTag final {};
struct ModuleIdentityTag final {};
struct DefinitionIdentityTag final {};
struct ImplIdentityTag final {};
struct GenericParameterIdentityTag final {};
struct CallableParameterIdentityTag final {};

using CompilationUnitId = ContextHandle<CompilationUnitIdentityTag>;
using CrateId = ContextHandle<CrateIdentityTag>;
using SourceFileId = ContextHandle<SourceFileIdentityTag>;
using ModuleId = ContextHandle<ModuleIdentityTag>;
using DefId = ContextHandle<DefinitionIdentityTag>;
using ImplId = ContextHandle<ImplIdentityTag>;
using GenericParameterId = ContextHandle<GenericParameterIdentityTag>;
using CallableParameterId = ContextHandle<CallableParameterIdentityTag>;

using CompilationUnitRegistry =
    FrozenContextRegistry<CompilationUnitIdentity, CompilationUnitIdentityTag>;
using CrateRegistry = FrozenContextRegistry<CrateKey, CrateIdentityTag>;
using SourceFileRegistry = FrozenContextRegistry<SourceFileKey, SourceFileIdentityTag>;
using ModuleRegistry = FrozenContextRegistry<ModuleKey, ModuleIdentityTag>;
using DefinitionRegistry =
    FrozenAuthorityRegistry<DefinitionKey, DefinitionIdentityRecord, DefinitionIdentityAuthority,
                            DefinitionIdentityTag>;
using ImplRegistry =
    FrozenAuthorityRegistry<ImplKey, ImplIdentityRecord, ImplIdentityAuthority, ImplIdentityTag>;
using GenericParameterRegistry =
    FrozenAuthorityRegistry<GenericParameterKey, GenericParameterIdentityRecord,
                            GenericParameterAuthority, GenericParameterIdentityTag>;
using CallableParameterRegistry =
    FrozenAuthorityRegistry<CallableParameterKey, CallableParameterIdentityRecord,
                            CallableParameterAuthority, CallableParameterIdentityTag>;

}  // namespace zomlang::compiler::identity
