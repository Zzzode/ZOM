// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/driver/core/query.h"

namespace zomlang::compiler::driver::core_library_query {

/// \brief Independent verifier implementations for stable core query projections.
class CoreLibraryQueryVerifier final {
public:
  ZC_NODISCARD static bool verifyModuleGraph(
      query::QueryContext& context, const ContextualCoreCrateKey& key,
      const query::TypedQueryResult<CoreModuleGraphRecord>& result);
  ZC_NODISCARD static bool verifyRoleSeed(
      query::QueryContext& context, const ContextualCoreCrateKey& key,
      const query::TypedQueryResult<CoreRoleSeedRecord>& result);
  ZC_NODISCARD static bool verifyBootstrapModuleInterfaceRecord(
      query::QueryContext& context, const ContextualCoreModuleKey& key,
      const query::TypedQueryResult<CoreBootstrapModuleInterfaceRecord>& result);
  ZC_NODISCARD static bool verifyExportSurface(
      query::QueryContext& context, const ContextualCoreModuleKey& key,
      const query::TypedQueryResult<CoreExportSurfaceRecord>& result);
  ZC_NODISCARD static bool verifyPreludeSurface(
      query::QueryContext& context, const ContextualCoreCrateKey& key,
      const query::TypedQueryResult<CorePreludeSurfaceRecord>& result);
  ZC_NODISCARD static bool verifyRoleAuthority(
      query::QueryContext& context, const ContextualCoreCrateKey& key,
      const query::TypedQueryResult<CoreRoleAuthorityRecord>& result);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verifyCoreAuthority(
      query::CapabilityQueryContext<MaterializeCoreAuthorityQuery>& context,
      const ContextualCoreCrateKey& key, const VerifiedCoreAuthorityBundle& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verifyFinalCoreModuleInterface(
      query::CapabilityQueryContext<FinalizeCoreModuleInterfaceQuery>& context,
      const ContextualCoreModuleKey& key, const VerifiedCoreModuleInterface& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verifyBootstrapModuleInterface(
      query::CapabilityQueryContext<MaterializeCoreBootstrapModuleInterfaceQuery>& context,
      const ContextualCoreModuleKey& key, const VerifiedCoreBootstrapModuleInterface& candidate);
};

}  // namespace zomlang::compiler::driver::core_library_query
