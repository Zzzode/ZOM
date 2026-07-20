// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/active-definition-authority-session.h"

#include "zomlang/compiler/driver/active-definition-authority-query.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/identity/canonical-decoder.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

zc::Maybe<identity::CrateKey> decodeCrate(const StableCrateQueryKey& key) {
  identity::CanonicalDecoder decoder(key.canonicalCrateBytes());
  auto crate = identity::CrateKey::decodeCanonical(decoder);
  if (crate == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(crate).encode().asPtr() != key.canonicalCrateBytes()) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(crate));
}

zc::Maybe<identity::ModuleKey> decodeModule(const StableModuleQueryKey& key) {
  identity::CanonicalDecoder decoder(key.canonicalModuleBytes());
  auto module = identity::ModuleKey::decodeCanonical(decoder);
  if (module == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(module).encode().asPtr() != key.canonicalModuleBytes()) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(module));
}

bool moduleBelongsToCrate(const StableModuleQueryKey& moduleKey,
                          const StableCrateQueryKey& crateKey) {
  auto module = decodeModule(moduleKey);
  auto crate = decodeCrate(crateKey);
  return module != zc::none && crate != zc::none &&
         ZC_ASSERT_NONNULL(module).crate().encode().asPtr() ==
             ZC_ASSERT_NONNULL(crate).encode().asPtr();
}

bool isValue(const query::TypedQueryResult<ActiveDefinitionAuthorityReadyInput::Value>& result) {
  return !result.isRuntimeFailure() && result.kind() == query::QueryValueKind::Value;
}

}  // namespace

zc::Maybe<query::InputTransaction> ActiveDefinitionAuthorityProjectionState::beginBaseMutation(
    query::QueryDatabase& database) {
  auto snapshot = database.snapshot();
  auto readiness = snapshot.probeInput<ActiveDefinitionAuthorityReadyInput>(
      identity::source_query::CompilationUnitQueryKey::fixed());
  if (readiness.isRuntimeFailure() || (readiness.kind() != query::QueryValueKind::Value &&
                                       readiness.kind() != query::QueryValueKind::Absence)) {
    return zc::none;
  }
  const bool removeReadiness = isValue(readiness);
  auto pending = database.beginInputTransaction();
  if (pending == zc::none) { return zc::none; }
  ZC_IF_SOME(transaction, pending) {
    if (removeReadiness && !transaction.erase<ActiveDefinitionAuthorityReadyInput>(
                               identity::source_query::CompilationUnitQueryKey::fixed())) {
      return zc::none;
    }
    return zc::mv(transaction);
  }
  return zc::none;
}

bool ActiveDefinitionAuthorityProjectionState::refresh(query::QueryDatabase& database,
                                                       const PackageRootSetQueryKey& packageRoots) {
  auto snapshot = database.snapshot();
  auto readiness = snapshot.probeInput<ActiveDefinitionAuthorityReadyInput>(
      identity::source_query::CompilationUnitQueryKey::fixed());
  if (readiness.isRuntimeFailure() || readiness.kind() != query::QueryValueKind::Absence) {
    return false;
  }

  auto activeCrates = snapshot.get<ActiveCratesInput>(packageRoots);
  if (activeCrates.isRuntimeFailure() || activeCrates.kind() != query::QueryValueKind::Value ||
      activeCrates.value().crates().size() == 0) {
    return false;
  }

  size_t moduleCount = 0;
  zc::Vector<StableModuleQueryKey> modules;
  for (const auto& crate : activeCrates.value().crates()) {
    if (decodeCrate(crate) == zc::none) { return false; }
    auto activeModules = snapshot.get<ActiveModulesInput>(crate);
    if (activeModules.isRuntimeFailure() || activeModules.kind() != query::QueryValueKind::Value ||
        activeModules.value().modules().size() == 0) {
      return false;
    }
    moduleCount += activeModules.value().modules().size();
    for (const auto& module : activeModules.value().modules()) {
      if (!moduleBelongsToCrate(module, crate)) { return false; }
      modules.add(module.clone());
    }
  }
  auto canonicalModules = CanonicalModuleSet::from(zc::mv(modules));
  if (canonicalModules == zc::none ||
      ZC_ASSERT_NONNULL(canonicalModules).modules().size() != moduleCount) {
    return false;
  }

  auto bindingOrder = snapshot.get<ModuleBindingOrderQuery>(packageRoots);
  if (bindingOrder.isRuntimeFailure() || bindingOrder.kind() != query::QueryValueKind::Value ||
      bindingOrder.value().modules().size() != moduleCount) {
    return false;
  }
  for (const auto& module : bindingOrder.value().modules()) {
    if (!ZC_ASSERT_NONNULL(canonicalModules).contains(module)) { return false; }
  }

  size_t authorityCount = 0;
  zc::Vector<ActiveDefinitionAuthorityRecord> authorityRecords;
  for (const auto& module : ZC_ASSERT_NONNULL(canonicalModules).modules()) {
    auto inventory = snapshot.get<NamedDefinitionInventoryQuery>(module);
    if (inventory.isRuntimeFailure() || inventory.kind() != query::QueryValueKind::Value) {
      return false;
    }
    authorityCount += inventory.value().entries().size();
    for (const auto& entry : inventory.value().entries()) {
      auto record = identity::DefinitionIdentityRecord::decodeCanonical(entry.canonicalRecord());
      if (record == zc::none ||
          ZC_ASSERT_NONNULL(record).module().encode().asPtr() != module.canonicalModuleBytes()) {
        return false;
      }
      auto authority = ActiveDefinitionAuthorityRecord::from(entry.key().clone(),
                                                             zc::mv(ZC_ASSERT_NONNULL(record)));
      if (authority == zc::none) { return false; }
      authorityRecords.add(zc::mv(ZC_ASSERT_NONNULL(authority)));
    }
  }

  auto projection = ActiveDefinitionAuthorityProjection::from(zc::mv(authorityRecords));
  if (projection == zc::none || ZC_ASSERT_NONNULL(projection).records().size() != authorityCount) {
    return false;
  }
  zc::Vector<identity::DefinitionKey> nextKeyLedger(ZC_ASSERT_NONNULL(projection).records().size());
  for (const auto& authority : ZC_ASSERT_NONNULL(projection).records()) {
    nextKeyLedger.add(authority.key().clone());
  }

  auto pending = database.beginInputTransaction();
  if (pending == zc::none) { return false; }
  ZC_IF_SOME(transaction, pending) {
    for (const auto& prior : keyLedgerField) {
      if (!transaction.erase<ActiveDefinitionAuthorityInput>(prior)) { return false; }
    }
    for (const auto& authority : ZC_ASSERT_NONNULL(projection).records()) {
      if (!transaction.set<ActiveDefinitionAuthorityInput>(authority.key(), authority.record())) {
        return false;
      }
    }
    if (!transaction.set<ActiveDefinitionAuthorityReadyInput>(
            identity::source_query::CompilationUnitQueryKey::fixed(),
            ZC_ASSERT_NONNULL(projection).fingerprint()) ||
        transaction.commit() == zc::none) {
      return false;
    }
    keyLedgerField = zc::mv(nextKeyLedger);
    return true;
  }
  return false;
}

zc::ArrayPtr<const identity::DefinitionKey> ActiveDefinitionAuthorityProjectionState::keyLedger()
    const {
  return keyLedgerField.asPtr();
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
