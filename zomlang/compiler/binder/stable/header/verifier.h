// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zomlang/compiler/binder/identity/named-identity-inventory.h"
#include "zomlang/compiler/binder/graph/parsed-module.h"
#include "zomlang/compiler/binder/identity/revision-local-identity-sites.h"

namespace zomlang::compiler::binder {

/// \brief Complete borrowed authority used to verify stable header candidates.
struct StableHeaderVerificationContext final {
  const CanonicalParsedModule& parsed;
  const NamedDefinitionInventory& definitionInventory;
  const NamedImplementationInventory& implementationInventory;
  const RevisionLocalDefinitionSites& definitionSites;
  const RevisionLocalImplementationSites& implementationSites;
};

/// \brief Independently verifies stable headers against complete current-source authority.
class StableHeaderVerifier final {
public:
  ZC_NODISCARD static bool verifyDefinition(const StableHeaderVerificationContext& context,
                                            const StableDefinitionQueryKey& queryKey,
                                            const StableDefinitionHeader& candidate);
  ZC_NODISCARD static bool verifyImplementationOccurrence(
      const StableHeaderVerificationContext& context,
      const StableImplementationOccurrenceQueryKey& queryKey,
      const StableImplementationOccurrenceHeader& candidate);
};

}  // namespace zomlang::compiler::binder
