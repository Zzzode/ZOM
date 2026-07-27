// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/module-resolution.h"
#include "zomlang/compiler/binder/parsed-module.h"

namespace zomlang::compiler::binder {

using ModuleDependencyRequestDerivationResult =
    zc::OneOf<zc::Vector<ModuleDependencyRequest>, ModuleResolutionInvariantFact>;

/// \brief Purely derives canonical source-backed module requests from one verified AST.
class ModuleDependencyRequestDeriver final {
public:
  /// \brief Derive stable requests for imports, foreign re-exports, and module aliases.
  /// \param requester Module that owns `parsedModule`.
  /// \param parsedModule Immutable parser result to traverse in generated-schema preorder.
  /// \param resolver Frozen catalog used only to expand the requester key for canonical sorting.
  /// \return Sorted requests, or a closed invariant fact when derivation is not exact.
  ZC_NODISCARD static ModuleDependencyRequestDerivationResult derive(
      identity::ModuleId requester, const VerifiedParsedModule& parsedModule,
      const StructuralModuleResolver& resolver);
};

}  // namespace zomlang::compiler::binder
