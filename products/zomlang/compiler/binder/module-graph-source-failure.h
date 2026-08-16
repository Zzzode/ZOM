// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/binder/local-identity.h"
#include "zomlang/compiler/binder/parsed-module-graph-input.h"
#include "zomlang/compiler/diagnostics/toolchain/module-root-argument.h"
#include "zomlang/compiler/identity/handle.h"
#include "zomlang/compiler/identity/key/source-key.h"

namespace zomlang::compiler::binder {

/// \brief One canonical module endpoint supplied to structural graph validation.
class ModuleGraphModule final {
public:
  ModuleGraphModule(identity::ModuleKey&& key, identity::ModuleId module) noexcept;
  ModuleGraphModule(ModuleGraphModule&&) noexcept = default;
  ModuleGraphModule& operator=(ModuleGraphModule&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleGraphModule);

  ZC_NODISCARD const identity::ModuleKey& key() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;

private:
  identity::ModuleKey keyValue;
  identity::ModuleId moduleValue;
};

/// \brief One canonically anchored source failure that prevents graph publication.
class ModuleGraphSourceFailure final {
public:
  ~ModuleGraphSourceFailure() noexcept(false);
  ModuleGraphSourceFailure(ModuleGraphSourceFailure&&) noexcept;
  ModuleGraphSourceFailure& operator=(ModuleGraphSourceFailure&&) noexcept;
  ZC_DISALLOW_COPY(ModuleGraphSourceFailure);

  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& declaredNamePath() const noexcept;
  ZC_NODISCARD uint32_t schemaPreorderOrdinal() const noexcept;
  ZC_NODISCARD const diagnostics::ModuleRootArgument& argument() const noexcept;

private:
  ModuleGraphSourceFailure(identity::ModuleKey&& module, identity::SourceFileKey&& source,
                           LocalSyntaxPath&& declaredNamePath, uint32_t schemaPreorderOrdinal,
                           diagnostics::ModuleRootArgument&& argument) noexcept;

  struct Impl;
  zc::Own<Impl> impl;

  friend class ModuleGraphSourceFailureBuilder;
};

/// \brief Builds source-root reservation failures from verified parser input.
class ModuleGraphSourceFailureBuilder final {
public:
  ZC_NODISCARD static zc::Maybe<ModuleGraphSourceFailure> buildToolchainModuleRootReserved(
      const ModuleGraphModule& module, const ParsedModuleGraphInput& parsed);
};

}  // namespace zomlang::compiler::binder
