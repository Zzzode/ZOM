// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/identity/brand.h"
#include "zomlang/compiler/identity/semantic-context-fingerprint.h"

namespace zomlang::compiler::ast {
class Tree;
}

namespace zomlang::compiler::driver {
class CompilerSession;
}

namespace zomlang::compiler::binder {

class FrozenDefinitionInventoryView;
class ResolvedImportEdge;
class ResolvedModuleAlias;
class VerifiedBindingInput;
class VerifiedBindingMetadata;
struct VerifiedBindingOutput;
class VerifiedExportSurface;
class VerifiedExportSurfaceView;
class VerifiedParsedModule;

/// \brief Sealed non-owning handoff for one exact verified RFC 0004 binder publication.
class VerifiedBoundModuleInput final {
public:
  ~VerifiedBoundModuleInput() noexcept(false);
  VerifiedBoundModuleInput(VerifiedBoundModuleInput&&) noexcept;
  VerifiedBoundModuleInput& operator=(VerifiedBoundModuleInput&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBoundModuleInput);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::PackageId package() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& semanticFingerprint() const noexcept;
  ZC_NODISCARD const ast::Tree& tree() const noexcept;
  ZC_NODISCARD const VerifiedParsedModule& parsedModule() const noexcept;
  ZC_NODISCARD const FrozenDefinitionInventoryView& definitions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const VerifiedExportSurfaceView> dependencySurfaces() const noexcept;
  ZC_NODISCARD zc::Maybe<const VerifiedExportSurfaceView&> preludeSurface() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ResolvedImportEdge> resolvedImports() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ResolvedModuleAlias> resolvedModuleAliases() const noexcept;
  ZC_NODISCARD const VerifiedBindingMetadata& bindings() const noexcept;
  ZC_NODISCARD const VerifiedExportSurface& bindingSurface() const noexcept;

private:
  struct Impl;
  explicit VerifiedBoundModuleInput(zc::Own<Impl>&& impl) noexcept;
  ZC_NODISCARD static zc::Maybe<VerifiedBoundModuleInput> from(const VerifiedBindingInput& input,
                                                               const VerifiedBindingOutput& output);
  zc::Own<Impl> impl;

  friend class driver::CompilerSession;
};

}  // namespace zomlang::compiler::binder
