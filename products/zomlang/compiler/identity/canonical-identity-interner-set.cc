// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/identity/canonical-identity-interner-set.h"

#include "zc/core/arena.h"
#include "zc/core/map.h"
#include "zc/core/mutex.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::identity {
namespace {

template <typename T>
zc::Array<uint8_t> encode(const T& value) {
  return value.encode();
}

template <typename Key, typename Record, typename Handle>
struct StoredCanonicalIdentity final {
  StoredCanonicalIdentity(Key&& key, Record&& record, Handle handle, zc::Array<uint8_t>&& keyBytes,
                          zc::Array<uint8_t>&& recordBytes) noexcept
      : key(zc::mv(key)),
        record(zc::mv(record)),
        handle(handle),
        keyBytes(zc::mv(keyBytes)),
        recordBytes(zc::mv(recordBytes)) {}
  StoredCanonicalIdentity(StoredCanonicalIdentity&&) noexcept = default;
  StoredCanonicalIdentity& operator=(StoredCanonicalIdentity&&) noexcept = default;
  ZC_DISALLOW_COPY(StoredCanonicalIdentity);

  Key key;
  Record record;
  Handle handle;
  zc::Array<uint8_t> keyBytes;
  zc::Array<uint8_t> recordBytes;
};

template <typename Key, typename Record, typename Handle>
struct CanonicalIdentityInternerData final {
  using Stored = StoredCanonicalIdentity<Key, Record, Handle>;

  zc::Arena arena;
  zc::Vector<zc::Maybe<const Stored&>> entries;
  zc::HashMap<zc::ArrayPtr<const uint8_t>, Handle> handlesByKey;
};

}  // namespace

template <typename Key, typename Record, typename Tag>
class CanonicalIdentityInterner final {
public:
  using Handle = ContextHandle<Tag>;
  using Entry = CanonicalIdentityEntry<Key, Record, Handle>;
  using Data = CanonicalIdentityInternerData<Key, Record, Handle>;
  using Stored = StoredCanonicalIdentity<Key, Record, Handle>;

  explicit CanonicalIdentityInterner(SemanticContextBrand context) noexcept : owner(context) {}
  ZC_DISALLOW_COPY_AND_MOVE(CanonicalIdentityInterner);

  IdentityInternResult<Handle> intern(SemanticContextBrand context, const Key& key,
                                      const Record& record, bool wellFormed) {
    if (!owner.isValid() || context != owner) { return IdentityInternerFailure::ForeignBrand; }
    auto keyBytes = encode(key);
    auto recordBytes = encode(record);
    auto locked = data.lockExclusive();
    ZC_IF_SOME(existing, locked->handlesByKey.find(keyBytes.asPtr())) {
      const uint32_t slot = existing.slot;
      if (slot >= locked->entries.size()) { return IdentityInternerFailure::MalformedRecord; }
      ZC_IF_SOME(stored, locked->entries[slot]) {
        if (stored.keyBytes.asPtr() != keyBytes.asPtr()) {
          return IdentityInternerFailure::MalformedRecord;
        }
        return stored.recordBytes.asPtr() == recordBytes.asPtr()
                   ? IdentityInternResult<Handle>(existing)
                   : IdentityInternResult<Handle>(IdentityInternerFailure::CanonicalCollision);
      }
      return IdentityInternerFailure::MalformedRecord;
    }
    if (!wellFormed) { return IdentityInternerFailure::MalformedRecord; }
    if (locked->entries.size() > static_cast<uint64_t>(UINT32_MAX)) {
      return IdentityInternerFailure::SlotOverflow;
    }
    const auto handle = Handle(owner, static_cast<uint32_t>(locked->entries.size()));
    auto& stored = locked->arena.template allocate<Stored>(key.clone(), record.clone(), handle,
                                                           zc::mv(keyBytes), zc::mv(recordBytes));
    locked->entries.add(stored);
    locked->handlesByKey.insert(stored.keyBytes.asPtr(), handle);
    return handle;
  }

  zc::Maybe<Entry> lookup(Handle handle) const {
    if (!handle.isValid() || !handle.belongsTo(owner)) { return zc::none; }
    auto locked = data.lockShared();
    if (handle.slot >= locked->entries.size()) { return zc::none; }
    ZC_IF_SOME(stored, locked->entries[handle.slot]) {
      if (stored.handle != handle) { return zc::none; }
      return Entry(stored.key.clone(), stored.record.clone(), handle);
    }
    return zc::none;
  }

  zc::Maybe<Entry> lookup(const Key& key) const {
    zc::Maybe<Handle> handle;
    {
      const auto keyBytes = encode(key);
      auto locked = data.lockShared();
      ZC_IF_SOME(value, locked->handlesByKey.find(keyBytes.asPtr())) { handle = value; }
    }
    ZC_IF_SOME(value, handle) { return lookup(value); }
    return zc::none;
  }

private:
  SemanticContextBrand owner;
  zc::MutexGuarded<Data> data;
};

struct CanonicalIdentityInternerSet::Impl final {
  explicit Impl(SemanticContextBrand context) noexcept
      : context(context),
        compilationUnits(context),
        crates(context),
        sourceFiles(context),
        modules(context),
        definitions(context),
        implementations(context),
        genericParameters(context),
        callableParameters(context) {}

  SemanticContextBrand context;
  CanonicalIdentityInterner<CompilationUnitIdentity, CompilationUnitIdentity,
                            CompilationUnitIdentityTag>
      compilationUnits;
  CanonicalIdentityInterner<CrateKey, CrateKey, CrateIdentityTag> crates;
  CanonicalIdentityInterner<SourceFileKey, SourceFileKey, SourceFileIdentityTag> sourceFiles;
  CanonicalIdentityInterner<ModuleKey, ModuleKey, ModuleIdentityTag> modules;
  CanonicalIdentityInterner<DefinitionKey, DefinitionIdentityRecord, DefinitionIdentityTag>
      definitions;
  CanonicalIdentityInterner<ImplKey, ImplIdentityRecord, ImplIdentityTag> implementations;
  CanonicalIdentityInterner<GenericParameterKey, GenericParameterIdentityRecord,
                            GenericParameterIdentityTag>
      genericParameters;
  CanonicalIdentityInterner<CallableParameterKey, CallableParameterIdentityRecord,
                            CallableParameterIdentityTag>
      callableParameters;
};

CanonicalIdentityInternerSet::~CanonicalIdentityInternerSet() noexcept(false) = default;
CanonicalIdentityInternerSet::CanonicalIdentityInternerSet(
    CanonicalIdentityInternerSet&&) noexcept = default;
CanonicalIdentityInternerSet& CanonicalIdentityInternerSet::operator=(
    CanonicalIdentityInternerSet&&) noexcept = default;
CanonicalIdentityInternerSet::CanonicalIdentityInternerSet(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<CanonicalIdentityInternerSet> CanonicalIdentityInternerSet::create(
    SemanticContextFactory& factory, SemanticContextBrand context) {
  if (!context.isValid()) { return zc::none; }
  auto candidate = zc::heap<Impl>(context);
  if (!factory.claimCanonicalIdentityInternerSet(context)) { return zc::none; }
  return CanonicalIdentityInternerSet(zc::mv(candidate));
}

SemanticContextBrand CanonicalIdentityInternerSet::context() const noexcept {
  return impl->context;
}

IdentityInternResult<CompilationUnitId> CanonicalIdentityInternerSet::internCompilationUnit(
    SemanticContextBrand context, const CompilationUnitIdentity& key) {
  return impl->compilationUnits.intern(context, key, key, true);
}

IdentityInternResult<CrateId> CanonicalIdentityInternerSet::internCrate(
    SemanticContextBrand context, const CrateKey& key) {
  return impl->crates.intern(context, key, key, true);
}

IdentityInternResult<SourceFileId> CanonicalIdentityInternerSet::internSourceFile(
    SemanticContextBrand context, const SourceFileKey& key) {
  return impl->sourceFiles.intern(context, key, key, true);
}

IdentityInternResult<ModuleId> CanonicalIdentityInternerSet::internModule(
    SemanticContextBrand context, const ModuleKey& key) {
  return impl->modules.intern(context, key, key, true);
}

IdentityInternResult<DefId> CanonicalIdentityInternerSet::internDefinition(
    SemanticContextBrand context, const DefinitionKey& key,
    const DefinitionIdentityRecord& record) {
  return impl->definitions.intern(context, key, record, DefinitionKey::compute(record) == key);
}

IdentityInternResult<ImplId> CanonicalIdentityInternerSet::internImplementation(
    SemanticContextBrand context, const ImplKey& key, const ImplIdentityRecord& record) {
  return impl->implementations.intern(context, key, record, ImplKey::compute(record) == key);
}

IdentityInternResult<GenericParameterId> CanonicalIdentityInternerSet::internGenericParameter(
    SemanticContextBrand context, const GenericParameterKey& key,
    const GenericParameterIdentityRecord& record) {
  return impl->genericParameters.intern(context, key, record,
                                        GenericParameterKey::compute(record) == key);
}

IdentityInternResult<CallableParameterId> CanonicalIdentityInternerSet::internCallableParameter(
    SemanticContextBrand context, const CallableParameterKey& key,
    const CallableParameterIdentityRecord& record) {
  return impl->callableParameters.intern(context, key, record,
                                         CallableParameterKey::compute(record) == key);
}

zc::Maybe<CompilationUnitIdentityEntry> CanonicalIdentityInternerSet::compilationUnit(
    CompilationUnitId handle) const {
  return impl->compilationUnits.lookup(handle);
}

zc::Maybe<CompilationUnitIdentityEntry> CanonicalIdentityInternerSet::compilationUnit(
    const CompilationUnitIdentity& key) const {
  return impl->compilationUnits.lookup(key);
}

zc::Maybe<CrateIdentityEntry> CanonicalIdentityInternerSet::crate(CrateId handle) const {
  return impl->crates.lookup(handle);
}

zc::Maybe<CrateIdentityEntry> CanonicalIdentityInternerSet::crate(const CrateKey& key) const {
  return impl->crates.lookup(key);
}

zc::Maybe<SourceFileIdentityEntry> CanonicalIdentityInternerSet::sourceFile(
    SourceFileId handle) const {
  return impl->sourceFiles.lookup(handle);
}

zc::Maybe<SourceFileIdentityEntry> CanonicalIdentityInternerSet::sourceFile(
    const SourceFileKey& key) const {
  return impl->sourceFiles.lookup(key);
}

zc::Maybe<ModuleIdentityEntry> CanonicalIdentityInternerSet::module(ModuleId handle) const {
  return impl->modules.lookup(handle);
}

zc::Maybe<ModuleIdentityEntry> CanonicalIdentityInternerSet::module(const ModuleKey& key) const {
  return impl->modules.lookup(key);
}

zc::Maybe<DefinitionIdentityEntry> CanonicalIdentityInternerSet::definition(DefId handle) const {
  return impl->definitions.lookup(handle);
}

zc::Maybe<DefinitionIdentityEntry> CanonicalIdentityInternerSet::definition(
    const DefinitionKey& key) const {
  return impl->definitions.lookup(key);
}

zc::Maybe<ImplementationIdentityEntry> CanonicalIdentityInternerSet::implementation(
    ImplId handle) const {
  return impl->implementations.lookup(handle);
}

zc::Maybe<ImplementationIdentityEntry> CanonicalIdentityInternerSet::implementation(
    const ImplKey& key) const {
  return impl->implementations.lookup(key);
}

zc::Maybe<GenericParameterIdentityEntry> CanonicalIdentityInternerSet::genericParameter(
    GenericParameterId handle) const {
  return impl->genericParameters.lookup(handle);
}

zc::Maybe<GenericParameterIdentityEntry> CanonicalIdentityInternerSet::genericParameter(
    const GenericParameterKey& key) const {
  return impl->genericParameters.lookup(key);
}

zc::Maybe<CallableParameterIdentityEntry> CanonicalIdentityInternerSet::callableParameter(
    CallableParameterId handle) const {
  return impl->callableParameters.lookup(handle);
}

zc::Maybe<CallableParameterIdentityEntry> CanonicalIdentityInternerSet::callableParameter(
    const CallableParameterKey& key) const {
  return impl->callableParameters.lookup(key);
}

}  // namespace zomlang::compiler::identity
