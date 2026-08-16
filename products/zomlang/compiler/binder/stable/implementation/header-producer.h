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

/// \brief Closed borrowed authority required to produce one implementation occurrence header.
struct ImplementationHeaderInput final {
  const CanonicalParsedModule& parsed;
  const StableImplementationOccurrenceQueryKey& queryKey;
  const NamedImplementationInventoryEntry& entry;
  const RevisionLocalImplementationSite& occurrenceSite;
  const RevisionLocalDefinitionSites& definitionSites;
  const RevisionLocalImplementationSites& implementationSites;
};

/// \brief Produces an implementation header after exact current-source occurrence validation.
class ImplementationHeaderProducer final {
public:
  ZC_NODISCARD static zc::Maybe<StableImplementationOccurrenceHeader> produce(
      const ImplementationHeaderInput& input);
};

}  // namespace zomlang::compiler::binder
