// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "compiler/checker/facts/cross-module-facts.h"
#include "compiler/driver/interface/interface-source.h"
#include "compiler/driver/query/module-graph/materialized-module-graph-query.h"

namespace zomlang::compiler::ownership {
class OwnershipAdmittedBoundModule;
}

namespace zomlang::compiler::driver {

/// \brief Projects the exact requester-visible signature capability from prior interfaces.
class ImportedSignatureViewProjector final {
public:
  ZC_NODISCARD static zc::Maybe<checker::cross_module::ImportedSignatureView> build(
      const ownership::OwnershipAdmittedBoundModule& requester,
      zc::ArrayPtr<const VerifiedInterfaceSource> dependencyInterfaces,
      const type::SemanticTypeStore& semanticTypes,
      const checker::CheckerIdentityAuthority& identities);

private:
  ZC_NODISCARD static zc::Maybe<checker::cross_module::ImportedSignatureModule> projectCore(
      const ownership::OwnershipAdmittedBoundModule& requester,
      const core_library_query::VerifiedCoreModuleInterface& source,
      checker::cross_module::SignatureViewOrigin origin,
      zc::ArrayPtr<const checker::cross_module::ImportedDefinitionBindingSelection>
          definitionBindings,
      zc::ArrayPtr<const checker::cross_module::ImportedModuleTargetSelection> moduleTargetNames,
      const checker::CheckerIdentityAuthority& identities);
};

}  // namespace zomlang::compiler::driver
