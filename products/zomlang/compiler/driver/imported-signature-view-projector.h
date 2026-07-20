// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zomlang/compiler/binder/verified-bound-module-input.h"
#include "zomlang/compiler/checker/cross-module-facts.h"
#include "zomlang/compiler/driver/module-interface.h"

namespace zomlang::compiler::driver {

/// \brief Projects the exact requester-visible signature capability from prior interfaces.
class ImportedSignatureViewProjector final {
public:
  ZC_NODISCARD static zc::Maybe<checker::cross_module::ImportedSignatureView> build(
      const binder::VerifiedBoundModuleInput& requester,
      zc::ArrayPtr<const VerifiedModuleInterface> dependencyInterfaces,
      const identity::SemanticIdentityRegistrySet& registries,
      const type::SemanticTypeStore& semanticTypes);
};

}  // namespace zomlang::compiler::driver
