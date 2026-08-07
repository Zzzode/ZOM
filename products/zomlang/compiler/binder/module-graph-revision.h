// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::binder {

/// \brief Immutable revision of one verified module dependency graph.
class ModuleGraphRevision final {
public:
  /// \brief Reconstructs a graph revision from an independently verified canonical digest.
  ZC_NODISCARD static ModuleGraphRevision fromCanonicalDigest(
      const identity::Sha256Digest& digest) noexcept;

  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

private:
  explicit ModuleGraphRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest value;
};

}  // namespace zomlang::compiler::binder
