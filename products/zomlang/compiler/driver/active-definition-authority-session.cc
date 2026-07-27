// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/active-definition-authority-session.h"

#include "zomlang/compiler/driver/active-definition-authority-query.h"
#include "zomlang/compiler/driver/module-graph-query.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

bool isValue(const query::TypedQueryResult<ActiveDefinitionAuthorityReadyInput::Value>& result) {
  return !result.isRuntimeFailure() && result.kind() == query::QueryValueKind::Value;
}

}  // namespace

zc::Maybe<query::InputTransaction> ActiveDefinitionAuthorityProjectionState::beginBaseMutation(
    query::QueryDatabase& database) {
  bool removeReadiness = false;
  ZC_IF_SOME(contextRoots, contextRootsField) {
    auto snapshot = database.snapshot();
    auto readiness = snapshot.probeInput<ActiveDefinitionAuthorityReadyInput>(contextRoots);
    if (readiness.isRuntimeFailure() || (readiness.kind() != query::QueryValueKind::Value &&
                                         readiness.kind() != query::QueryValueKind::Absence)) {
      return zc::none;
    }
    removeReadiness = isValue(readiness);
  }
  auto pending = database.beginInputTransaction();
  if (pending == zc::none) { return zc::none; }
  ZC_IF_SOME(transaction, pending) {
    if (removeReadiness) {
      ZC_IF_SOME(contextRoots, contextRootsField) {
        if (!transaction.erase<ActiveDefinitionAuthorityReadyInput>(contextRoots)) {
          return zc::none;
        }
      }
    }
    return zc::mv(transaction);
  }
  return zc::none;
}

bool ActiveDefinitionAuthorityProjectionState::refresh(
    query::QueryDatabase& database, const CompilationRootSetQueryKey& contextRoots) {
  auto snapshot = database.snapshot();
  auto readiness = snapshot.probeInput<ActiveDefinitionAuthorityReadyInput>(contextRoots);
  if (readiness.isRuntimeFailure() || readiness.kind() != query::QueryValueKind::Absence) {
    return false;
  }

  auto graph = snapshot.get<module_graph_query::ModuleGraphQuery>(contextRoots);
  auto scc = snapshot.get<module_graph_query::ModuleGraphSccQuery>(contextRoots);
  if (graph.isRuntimeFailure() || scc.isRuntimeFailure() ||
      graph.kind() != query::QueryValueKind::Value || scc.kind() != query::QueryValueKind::Value ||
      graph.value().modules().size() == 0 || scc.value().hasCycle(graph.value())) {
    return false;
  }

  size_t authorityCount = 0;
  zc::Vector<ActiveDefinitionAuthorityRecord> authorityRecords;
  for (const auto& module : graph.value().modules()) {
    auto stableModule = StableModuleQueryKey::fromVerified(module);
    if (stableModule == zc::none) { return false; }
    auto inventory = snapshot.get<NamedDefinitionInventoryQuery>(ZC_ASSERT_NONNULL(stableModule));
    if (inventory.isRuntimeFailure() || inventory.kind() != query::QueryValueKind::Value) {
      return false;
    }
    authorityCount += inventory.value().entries().size();
    for (const auto& entry : inventory.value().entries()) {
      auto record = identity::DefinitionIdentityRecord::decodeCanonical(entry.canonicalRecord());
      if (record == zc::none ||
          ZC_ASSERT_NONNULL(record).module().encode().asPtr() != module.encode().asPtr()) {
        return false;
      }
      auto authority = ActiveDefinitionAuthorityRecord::from(entry.key().clone(),
                                                             zc::mv(ZC_ASSERT_NONNULL(record)));
      if (authority == zc::none) { return false; }
      authorityRecords.add(zc::mv(ZC_ASSERT_NONNULL(authority)));
    }
  }

  auto projection =
      ActiveDefinitionAuthorityProjection::from(contextRoots, zc::mv(authorityRecords));
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
    ZC_IF_SOME(priorContext, contextRootsField) {
      for (const auto& prior : keyLedgerField) {
        auto key = ContextualDefinitionKey::from(priorContext.clone(), prior.clone());
        if (!transaction.erase<ActiveDefinitionAuthorityInput>(key)) { return false; }
      }
    }
    for (const auto& authority : ZC_ASSERT_NONNULL(projection).records()) {
      auto key = ContextualDefinitionKey::from(contextRoots.clone(), authority.key().clone());
      if (!transaction.set<ActiveDefinitionAuthorityInput>(key, authority.record())) {
        return false;
      }
    }
    if (!transaction.set<ActiveDefinitionAuthorityReadyInput>(
            contextRoots, ZC_ASSERT_NONNULL(projection).fingerprint()) ||
        transaction.commit() == zc::none) {
      return false;
    }
    keyLedgerField = zc::mv(nextKeyLedger);
    contextRootsField = contextRoots.clone();
    return true;
  }
  return false;
}

zc::ArrayPtr<const identity::DefinitionKey> ActiveDefinitionAuthorityProjectionState::keyLedger()
    const {
  return keyLedgerField.asPtr();
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
