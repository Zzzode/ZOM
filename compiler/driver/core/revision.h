// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::driver::core_library_query {

class CoreModuleInterfaceRecord;

/// \brief Stable revision of one flat finalized core module interface.
class CoreModuleInterfaceRevision final {
public:
  ZC_NODISCARD static CoreModuleInterfaceRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD CoreModuleInterfaceRevision clone() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  bool operator==(const CoreModuleInterfaceRevision& other) const noexcept;
  bool operator!=(const CoreModuleInterfaceRevision& other) const noexcept {
    return !(*this == other);
  }

private:
  explicit CoreModuleInterfaceRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest digestValue;

  friend class CoreModuleInterfaceRecord;
};

/// \brief Stable revision of one core module's canonical binding surface.
class CoreBindingSurfaceRevision final {
public:
  ZC_NODISCARD static CoreBindingSurfaceRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD CoreBindingSurfaceRevision clone() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  bool operator==(const CoreBindingSurfaceRevision& other) const noexcept;
  bool operator!=(const CoreBindingSurfaceRevision& other) const noexcept {
    return !(*this == other);
  }

private:
  explicit CoreBindingSurfaceRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest digestValue;

  friend class CoreModuleInterfaceRecord;
};

}  // namespace zomlang::compiler::driver::core_library_query
