// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/vector.h"
#include "zomlang/compiler/binder/stable-binding-facts.h"
#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::driver::incremental_binding_query {

/// \brief Session-owned readiness barrier and exact active-definition key ledger.
class ActiveDefinitionAuthorityProjectionState final {
public:
  ActiveDefinitionAuthorityProjectionState() noexcept = default;
  ZC_DISALLOW_COPY_AND_MOVE(ActiveDefinitionAuthorityProjectionState);

  /// \brief Opens a base-input transaction that first removes published readiness.
  ZC_NODISCARD zc::Maybe<query::InputTransaction> beginBaseMutation(query::QueryDatabase& database);

  /// \brief Reconstructs and atomically installs the complete active authority projection.
  ZC_NODISCARD bool refresh(query::QueryDatabase& database,
                            const CompilationRootSetQueryKey& contextRoots);

  /// \brief Returns the exact stable keys published by the last successful refresh.
  ZC_NODISCARD zc::ArrayPtr<const binder::StableDefinitionQueryKey> keyLedger() const
      ZC_LIFETIMEBOUND;

private:
  zc::Vector<binder::StableDefinitionQueryKey> keyLedgerField;
  zc::Maybe<CompilationRootSetQueryKey> contextRootsField;
};

}  // namespace zomlang::compiler::driver::incremental_binding_query
