// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/internal/scope-arena.h"

namespace zomlang::compiler::binder {

using ClosureFreeVariableBuildResult =
    zc::OneOf<zc::Vector<ClosureFreeVariableFact>, BinderInvariantFact>;

/// \brief Derives deterministic capture-mode-free closure dependencies from bound value names.
class ClosureFreeVariableBuilder final {
public:
  ZC_NODISCARD static ClosureFreeVariableBuildResult build(
      const VerifiedBindingInput& input, const ScopeArenaCandidate& arena,
      zc::ArrayPtr<const DefinitionFact> definitions,
      zc::ArrayPtr<const BindingResolution> nodeBindings,
      zc::ArrayPtr<const BoundThis> thisBindings);
};

}  // namespace zomlang::compiler::binder
