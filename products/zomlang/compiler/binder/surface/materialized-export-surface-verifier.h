// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/metadata/binding-metadata.h"
#include "zomlang/compiler/binder/graph/materialized-module-skeleton.h"

namespace zomlang::compiler::binder {

/// \brief One retained dependency export surface and its canonical definitions.
struct MaterializedDependencyExportSurface final {
  identity::ModuleKey moduleKey;
  identity::ModuleId module;
  VerifiedExportSurface surface;
  zc::Vector<MaterializedDefinitionIdentityEntry> definitions;
};

/// \brief Independently validates and seals one materialized module export surface.
class MaterializedExportSurfaceVerifier final {
public:
  ZC_NODISCARD static zc::Maybe<VerifiedExportSurface> from(
      identity::SemanticContextBrand context,
      const identity::ContextFingerprint& fingerprint, const identity::ModuleKey& moduleKey,
      identity::ModuleId module, const identity::CompilationUnitIdentity& compilationUnitKey,
      identity::CompilationUnitId compilationUnit, const identity::SourceFileKey& source,
      const BoundModuleSkeleton& stableWitness,
      zc::ArrayPtr<const MaterializedDefinitionIdentityEntry> identities,
      zc::ArrayPtr<const DefinitionFact> definitions,
      zc::ArrayPtr<const MaterializedDependencyExportSurface> dependencies,
      zc::ArrayPtr<const ImportBindingFact> imports,
      zc::ArrayPtr<const LocalExportFact> localExports);
};

}  // namespace zomlang::compiler::binder
