// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/internal/scope-arena.h"

namespace zomlang::compiler::binder {

struct LabelDuplicateFact final {
  identity::SemanticIdentifier name;
  identity::SourceSpan primary;
  identity::SourceSpan previous;
  uint32_t schemaPreorderOrdinal;
};

struct LabelFactsCandidate final {
  zc::Vector<LabelFact> labels;
  zc::Vector<LabelDuplicateFact> duplicates;
};

using LabelFactsBuildResult = zc::OneOf<LabelFactsCandidate, BinderInvariantFact>;

/// \brief Returns a label index only when the owner-local allocation cannot truncate.
ZC_NODISCARD zc::Maybe<uint32_t> checkedLabelIndex(uint64_t value);

/// \brief Sole authority for owner-local label identities and structural label facts.
class LabelBuilder final {
public:
  ZC_NODISCARD static LabelFactsBuildResult build(const VerifiedBindingInput& input,
                                                  const ScopeArenaCandidate& arena);
};

}  // namespace zomlang::compiler::binder
