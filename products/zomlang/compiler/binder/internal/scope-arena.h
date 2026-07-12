// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/binding-input.h"
#include "zomlang/compiler/binder/binding-metadata.h"

namespace zomlang::compiler::binder {

/// \brief Complete deterministic scope allocation before name bindings are installed.
struct ScopeArenaCandidate final {
  zc::Vector<NodeScopeFact> nodeScopes;
  zc::Vector<ScopeRecord> scopes;
};

using ScopeArenaBuildResult = zc::OneOf<ScopeArenaCandidate, BinderInvariantFact>;

/// \brief Returns a scope index only when the allocation cannot truncate or wrap.
ZC_NODISCARD zc::Maybe<uint32_t> checkedScopeIndex(uint64_t value);

/// \brief Sole authority for module-local scope identity and structural allocation.
class ScopeArenaBuilder final {
public:
  ZC_NODISCARD static ScopeArenaBuildResult build(const VerifiedBindingInput& input);
};

}  // namespace zomlang::compiler::binder
