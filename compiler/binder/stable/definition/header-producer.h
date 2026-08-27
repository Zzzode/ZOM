// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "compiler/binder/identity/named-identity-inventory.h"
#include "compiler/binder/graph/parsed-module.h"
#include "compiler/binder/identity/revision-local-identity-sites.h"

namespace zomlang::compiler::binder {

/// \brief Closed borrowed authority required to produce one stable definition header.
struct DefinitionHeaderInput final {
  const CanonicalParsedModule& parsed;
  const StableDefinitionQueryKey& queryKey;
  const NamedDefinitionInventoryEntry& entry;
  const RevisionLocalDefinitionSite& authoritySite;
  const RevisionLocalDefinitionSites& definitionSites;
  const RevisionLocalImplementationSites& implementationSites;
};

/// \brief Produces a definition header only after exact current-source authority validation.
class DefinitionHeaderProducer final {
public:
  ZC_NODISCARD static zc::Maybe<StableDefinitionHeader> produce(const DefinitionHeaderInput& input);
};

}  // namespace zomlang::compiler::binder
