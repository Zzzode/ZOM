// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>

#include "compiler/driver/query/module-graph/materialized-module-graph-query.h"
#include "zc/core/array.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ownership {

/// \brief Parsed syntax that lacks an admitted complete frontend contract.
enum class SurfaceSyntaxKind : uint8_t {
  Spawn = 0x01,
  Suspend = 0x02,
  Match = 0x03,
  Loop = 0x04,
  LoopControl = 0x05,
  Label = 0x06,
  VoidReturn = 0x07,
  ExpressionStatement = 0x08,
  FunctionBody = 0x09,
};

/// \brief One source-ordered ownership-surface rejection.
struct SurfaceFailure final {
  SurfaceSyntaxKind kind;
  identity::SourceSpan primarySpan;
  uint32_t traversalOrdinal;
};

class SurfaceSourceRejected final {
public:
  ~SurfaceSourceRejected() noexcept(false);
  SurfaceSourceRejected(SurfaceSourceRejected&&) noexcept;
  SurfaceSourceRejected& operator=(SurfaceSourceRejected&&) noexcept;
  ZC_DISALLOW_COPY(SurfaceSourceRejected);

  ZC_NODISCARD zc::ArrayPtr<const SurfaceFailure> failures() const noexcept;

private:
  struct Impl;
  explicit SurfaceSourceRejected(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class SurfaceAdmissionBuilder;
};

/// \brief A bound-module lease admitted to the complete frontend surface.
class AdmittedBoundModule final {
public:
  ~AdmittedBoundModule() noexcept(false);
  AdmittedBoundModule(AdmittedBoundModule&&) noexcept;
  AdmittedBoundModule& operator=(AdmittedBoundModule&&) noexcept;
  ZC_DISALLOW_COPY(AdmittedBoundModule);

  ZC_NODISCARD const driver::module_graph_query::CheckerBoundModuleView& boundModule()
      const noexcept;
  ZC_NODISCARD AdmittedBoundModule retain() const;
  ZC_NODISCARD operator const driver::module_graph_query::CheckerBoundModuleView&() const noexcept;
  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::CompilationUnitId compilationUnit() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD identity::SourceFileId sourceFile() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& semanticFingerprint() const noexcept;
  ZC_NODISCARD const ast::Tree& tree() const noexcept;
  ZC_NODISCARD const binder::CanonicalParsedModule& parsedModule() const noexcept;
  ZC_NODISCARD const binder::ImmutableDefinitionInventory& definitions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::MaterializedDependencyExportSurface> dependencySurfaces()
      const noexcept;
  ZC_NODISCARD zc::Maybe<const binder::MaterializedDependencyExportSurface&> preludeSurface()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ImportBindingFact> resolvedImports() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ModuleAliasBindingFact> resolvedModuleAliases()
      const noexcept;
  ZC_NODISCARD const binder::ImmutableBindingMetadata& bindings() const noexcept;
  ZC_NODISCARD const binder::VerifiedExportSurface& bindingSurface() const noexcept;

private:
  struct Impl;
  explicit AdmittedBoundModule(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class SurfaceAdmissionBuilder;
};

using SurfaceAdmissionResult = zc::OneOf<AdmittedBoundModule, SurfaceSourceRejected>;

/// \brief Admits a bound module only when every parsed frontend form has a complete contract.
class SurfaceAdmissionBuilder final {
public:
  ZC_NODISCARD static SurfaceAdmissionResult admit(
      driver::module_graph_query::CheckerBoundModuleView&& boundModule);
};

}  // namespace zomlang::compiler::ownership
