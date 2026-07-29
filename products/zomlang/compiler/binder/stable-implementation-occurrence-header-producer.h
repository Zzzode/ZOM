// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zomlang/compiler/binder/named-identity-inventory.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/binder/revision-local-identity-sites.h"

namespace zomlang::compiler::binder {

/// \brief Closed borrowed authority required to produce one implementation occurrence header.
struct StableImplementationOccurrenceHeaderProductionInput final {
  const CanonicalParsedModule& parsed;
  const StableImplementationOccurrenceQueryKey& queryKey;
  const NamedImplementationInventoryEntry& entry;
  const RevisionLocalImplementationSite& occurrenceSite;
  const RevisionLocalDefinitionSites& definitionSites;
  const RevisionLocalImplementationSites& implementationSites;
};

/// \brief Produces an implementation header after exact current-source occurrence validation.
class StableImplementationOccurrenceHeaderProducer final {
public:
  ZC_NODISCARD static zc::Maybe<StableImplementationOccurrenceHeader> produce(
      const StableImplementationOccurrenceHeaderProductionInput& input);
};

}  // namespace zomlang::compiler::binder
