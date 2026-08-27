// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "compiler/identity/canonical/canonical-scalar.h"

namespace zomlang::compiler::identity {
class CanonicalDecoder;
class CanonicalEncoder;
}  // namespace zomlang::compiler::identity

namespace zomlang::compiler::diagnostics {

/// \brief Typed canonical argument for the compiler-reserved toolchain module root.
class ModuleRootArgument final {
public:
  ModuleRootArgument(ModuleRootArgument&&) noexcept = default;
  ModuleRootArgument& operator=(ModuleRootArgument&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleRootArgument);

  /// \brief Admits exactly the single canonical module-path segment `core`.
  /// \param path Canonical module path reconstructed from an admitted producer input.
  /// \return The typed argument, or none when the path is not exactly `core`.
  ZC_NODISCARD static zc::Maybe<ModuleRootArgument> fromCanonicalPath(
      zc::Vector<identity::ModulePathSegment>&& path);

  /// \brief Decodes and validates the exact one-segment canonical argument record.
  /// \param decoder Canonical decoder positioned at the argument record.
  /// \return The typed argument, or none for any non-canonical or non-core record.
  ZC_NODISCARD static zc::Maybe<ModuleRootArgument> decodeCanonical(
      identity::CanonicalDecoder& decoder);

  ZC_NODISCARD ModuleRootArgument clone() const;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModulePathSegment> path() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  bool operator==(const ModuleRootArgument& other) const noexcept;
  bool operator!=(const ModuleRootArgument& other) const noexcept {
    return !(*this == other);
  }

private:
  explicit ModuleRootArgument(zc::Vector<identity::ModulePathSegment>&& path) noexcept;

  zc::Vector<identity::ModulePathSegment> pathValue;
};

}  // namespace zomlang::compiler::diagnostics
