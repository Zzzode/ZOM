// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/materialized-module-skeleton.h"

namespace zomlang::compiler::binder {
namespace {

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

template <typename Entry>
void sortByKey(zc::Vector<Entry>& entries) {
  for (size_t index = 1; index < entries.size(); ++index) {
    auto current = zc::mv(entries[index]);
    size_t insertion = index;
    while (insertion != 0 && compareBytes(current.key().encode().asPtr(),
                                          entries[insertion - 1].key().encode().asPtr()) < 0) {
      entries[insertion] = zc::mv(entries[insertion - 1]);
      --insertion;
    }
    entries[insertion] = zc::mv(current);
  }
}

template <typename Entry, typename Handle, typename Key, typename Record, typename Intern,
          typename Lookup>
bool appendIdentity(zc::Vector<Entry>& entries, identity::SemanticContextBrand context,
                    const Key& key, const Record& record, Intern intern, Lookup lookup) {
  for (const auto& entry : entries) {
    if (entry.key() != key) { continue; }
    return entry.record().encode().asPtr() == record.encode().asPtr();
  }
  auto result = intern(key, record);
  if (!result.template is<Handle>()) { return false; }
  const auto handle = result.template get<Handle>();
  auto reverse = lookup(handle);
  if (reverse == zc::none || !handle.belongsTo(context) ||
      ZC_ASSERT_NONNULL(reverse).handle() != handle || ZC_ASSERT_NONNULL(reverse).key() != key ||
      ZC_ASSERT_NONNULL(reverse).record().encode().asPtr() != record.encode().asPtr()) {
    return false;
  }
  entries.add(Entry::fromVerified(ZC_ASSERT_NONNULL(reverse).key().clone(),
                                  ZC_ASSERT_NONNULL(reverse).record().clone(), handle));
  return true;
}

}  // namespace

struct MaterializedModuleSkeletonIdentities::Impl final {
  Impl(identity::SemanticContextBrand context, query::DatabaseRevision revision,
       identity::SemanticContextFingerprint&& fingerprint, identity::ModuleId module,
       BoundModuleSkeleton&& stableWitness,
       zc::Vector<MaterializedDefinitionIdentityEntry>&& definitions,
       zc::Vector<MaterializedImplementationIdentityEntry>&& implementations,
       zc::Vector<MaterializedGenericParameterIdentityEntry>&& genericParameters,
       zc::Vector<MaterializedCallableParameterIdentityEntry>&& callableParameters) noexcept
      : context(context),
        revision(revision),
        fingerprint(zc::mv(fingerprint)),
        module(module),
        stableWitness(zc::mv(stableWitness)),
        definitions(zc::mv(definitions)),
        implementations(zc::mv(implementations)),
        genericParameters(zc::mv(genericParameters)),
        callableParameters(zc::mv(callableParameters)) {}

  identity::SemanticContextBrand context;
  query::DatabaseRevision revision;
  identity::SemanticContextFingerprint fingerprint;
  identity::ModuleId module;
  BoundModuleSkeleton stableWitness;
  zc::Vector<MaterializedDefinitionIdentityEntry> definitions;
  zc::Vector<MaterializedImplementationIdentityEntry> implementations;
  zc::Vector<MaterializedGenericParameterIdentityEntry> genericParameters;
  zc::Vector<MaterializedCallableParameterIdentityEntry> callableParameters;
};

MaterializedModuleSkeletonIdentities::MaterializedModuleSkeletonIdentities(
    zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

MaterializedModuleSkeletonIdentities::~MaterializedModuleSkeletonIdentities() noexcept(false) =
    default;
MaterializedModuleSkeletonIdentities::MaterializedModuleSkeletonIdentities(
    MaterializedModuleSkeletonIdentities&&) noexcept = default;
MaterializedModuleSkeletonIdentities& MaterializedModuleSkeletonIdentities::operator=(
    MaterializedModuleSkeletonIdentities&&) noexcept = default;

zc::Maybe<MaterializedModuleSkeletonIdentities> MaterializedModuleSkeletonIdentities::from(
    identity::SemanticContextBrand context, query::DatabaseRevision revision,
    const identity::SemanticContextFingerprint& fingerprint, const BoundModuleSkeleton& skeleton,
    identity::CanonicalIdentityInternerSet& interners) {
  if (!context.isValid() || revision.value() == 0 || interners.context() != context) {
    return zc::none;
  }

  auto moduleResult = interners.internModule(context, skeleton.module());
  if (!moduleResult.is<identity::ModuleId>()) { return zc::none; }
  const auto module = moduleResult.get<identity::ModuleId>();
  auto reverseModule = interners.module(module);
  if (reverseModule == zc::none || !module.belongsTo(context) ||
      ZC_ASSERT_NONNULL(reverseModule).handle() != module ||
      !sameModule(ZC_ASSERT_NONNULL(reverseModule).key(), skeleton.module()) ||
      !sameModule(ZC_ASSERT_NONNULL(reverseModule).record(), skeleton.module())) {
    return zc::none;
  }

  zc::Vector<MaterializedDefinitionIdentityEntry> definitions;
  for (const auto& declaration : skeleton.declarations().values()) {
    if (!appendIdentity<MaterializedDefinitionIdentityEntry, identity::DefId,
                        identity::DefinitionKey, identity::DefinitionIdentityRecord>(
            definitions, context, declaration.queryKey().definition(), declaration.record(),
            [&](const identity::DefinitionKey& key,
                const identity::DefinitionIdentityRecord& record) {
              return interners.internDefinition(context, key, record);
            },
            [&](identity::DefId handle) { return interners.definition(handle); })) {
      return zc::none;
    }
  }

  zc::Vector<MaterializedImplementationIdentityEntry> implementations;
  for (const auto& occurrence : skeleton.implementationOccurrences().values()) {
    if (!appendIdentity<MaterializedImplementationIdentityEntry, identity::ImplId,
                        identity::ImplKey, identity::ImplIdentityRecord>(
            implementations, context, occurrence.authority().implementation(), occurrence.record(),
            [&](const identity::ImplKey& key, const identity::ImplIdentityRecord& record) {
              return interners.internImplementation(context, key, record);
            },
            [&](identity::ImplId handle) { return interners.implementation(handle); })) {
      return zc::none;
    }
  }

  zc::Vector<MaterializedGenericParameterIdentityEntry> genericParameters;
  for (const auto& parameter : skeleton.genericParameterDeclarations().values()) {
    if (!appendIdentity<MaterializedGenericParameterIdentityEntry, identity::GenericParameterId,
                        identity::GenericParameterKey, identity::GenericParameterIdentityRecord>(
            genericParameters, context, parameter.queryKey().parameter(), parameter.record(),
            [&](const identity::GenericParameterKey& key,
                const identity::GenericParameterIdentityRecord& record) {
              return interners.internGenericParameter(context, key, record);
            },
            [&](identity::GenericParameterId handle) {
              return interners.genericParameter(handle);
            })) {
      return zc::none;
    }
  }

  zc::Vector<MaterializedCallableParameterIdentityEntry> callableParameters;
  for (const auto& parameter : skeleton.callableParameterDeclarations().values()) {
    if (!appendIdentity<MaterializedCallableParameterIdentityEntry, identity::CallableParameterId,
                        identity::CallableParameterKey, identity::CallableParameterIdentityRecord>(
            callableParameters, context, parameter.queryKey().parameter(), parameter.record(),
            [&](const identity::CallableParameterKey& key,
                const identity::CallableParameterIdentityRecord& record) {
              return interners.internCallableParameter(context, key, record);
            },
            [&](identity::CallableParameterId handle) {
              return interners.callableParameter(handle);
            })) {
      return zc::none;
    }
  }

  sortByKey(definitions);
  sortByKey(implementations);
  sortByKey(genericParameters);
  sortByKey(callableParameters);
  return MaterializedModuleSkeletonIdentities(zc::heap<Impl>(
      context, revision, fingerprint.clone(), module, skeleton.clone(), zc::mv(definitions),
      zc::mv(implementations), zc::mv(genericParameters), zc::mv(callableParameters)));
}

MaterializedModuleSkeletonIdentities MaterializedModuleSkeletonIdentities::clone() const {
  zc::Vector<MaterializedDefinitionIdentityEntry> definitions;
  for (const auto& entry : impl->definitions) definitions.add(entry.clone());
  zc::Vector<MaterializedImplementationIdentityEntry> implementations;
  for (const auto& entry : impl->implementations) implementations.add(entry.clone());
  zc::Vector<MaterializedGenericParameterIdentityEntry> genericParameters;
  for (const auto& entry : impl->genericParameters) genericParameters.add(entry.clone());
  zc::Vector<MaterializedCallableParameterIdentityEntry> callableParameters;
  for (const auto& entry : impl->callableParameters) callableParameters.add(entry.clone());
  return MaterializedModuleSkeletonIdentities(
      zc::heap<Impl>(impl->context, impl->revision, impl->fingerprint.clone(), impl->module,
                     impl->stableWitness.clone(), zc::mv(definitions), zc::mv(implementations),
                     zc::mv(genericParameters), zc::mv(callableParameters)));
}

identity::SemanticContextBrand MaterializedModuleSkeletonIdentities::context() const noexcept {
  return impl->context;
}

query::DatabaseRevision MaterializedModuleSkeletonIdentities::revision() const noexcept {
  return impl->revision;
}

const identity::SemanticContextFingerprint& MaterializedModuleSkeletonIdentities::fingerprint()
    const noexcept {
  return impl->fingerprint;
}

identity::ModuleId MaterializedModuleSkeletonIdentities::module() const noexcept {
  return impl->module;
}

const BoundModuleSkeleton& MaterializedModuleSkeletonIdentities::stableWitness() const noexcept {
  return impl->stableWitness;
}

zc::ArrayPtr<const MaterializedDefinitionIdentityEntry>
MaterializedModuleSkeletonIdentities::definitions() const noexcept {
  return impl->definitions.asPtr();
}

zc::ArrayPtr<const MaterializedImplementationIdentityEntry>
MaterializedModuleSkeletonIdentities::implementations() const noexcept {
  return impl->implementations.asPtr();
}

zc::ArrayPtr<const MaterializedGenericParameterIdentityEntry>
MaterializedModuleSkeletonIdentities::genericParameters() const noexcept {
  return impl->genericParameters.asPtr();
}

zc::ArrayPtr<const MaterializedCallableParameterIdentityEntry>
MaterializedModuleSkeletonIdentities::callableParameters() const noexcept {
  return impl->callableParameters.asPtr();
}

}  // namespace zomlang::compiler::binder
