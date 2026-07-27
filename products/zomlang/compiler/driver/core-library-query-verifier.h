// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/driver/core-library-query-provider.h"

namespace zomlang::compiler::driver::core_library_query {

/// \brief Independent verifier implementations for stable core query projections.
class CoreLibraryQueryVerifier final {
public:
  ZC_NODISCARD static bool verifyModuleGraph(
      query::QueryContext& context, const ContextualCoreCrateKey& key,
      const query::TypedQueryResult<CoreModuleGraphRecord>& result);
};

}  // namespace zomlang::compiler::driver::core_library_query
