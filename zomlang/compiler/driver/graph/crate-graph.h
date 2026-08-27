// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/driver/package/build-script-plan.h"
#include "zomlang/compiler/driver/package/build-script-runtime.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"
#include "zomlang/compiler/driver/package/package-resolver.h"
#include "zomlang/compiler/identity/key/crate-key.h"

namespace zomlang::compiler::driver {

enum class CrateGraphIssue : uint8_t {
  BuildResultsRequired = 0x01,
  InvalidBuildResults = 0x02,
  RootOutsideResolution = 0x03,
  MissingProviderLibrary = 0x04,
  InvalidCrateIdentity = 0x05,
  DuplicateCrate = 0x06,
  DuplicateEdge = 0x07,
  DependencyCycle = 0x08
};

class VerifiedCrateGraph;
using CrateGraphBuildResult = zc::OneOf<VerifiedCrateGraph, CrateGraphIssue>;
class VerifiedPreparatoryCrateGraph;
using PreparatoryCrateGraphBuildResult = zc::OneOf<VerifiedPreparatoryCrateGraph, CrateGraphIssue>;
using BuildScriptPlanBuildResult = zc::OneOf<package::VerifiedBuildScriptPlan, CrateGraphIssue>;

/// \brief Immutable final-target crate graph expanded from verified package authority.
class VerifiedCrateGraph final {
public:
  ~VerifiedCrateGraph() noexcept(false);
  VerifiedCrateGraph(VerifiedCrateGraph&&) noexcept;
  VerifiedCrateGraph& operator=(VerifiedCrateGraph&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCrateGraph);

  /// \brief Expands the RFC 0012 package graph using the RFC 0008 target matrix.
  ZC_NODISCARD static CrateGraphBuildResult buildFinal(
      const package::VerifiedPackageCompilationRequest& request,
      const package::ResolutionOutput& resolution,
      const package::VerifiedBuildScriptPlan& buildPlan);

  ZC_NODISCARD zc::ArrayPtr<const package::FinalizedCompilationRoot> roots() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::PackageDependencyEdgeKey> packageEdges() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CrateKey> crates() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CrateDependencyEdgeKey> edges() const noexcept;

private:
  struct Impl;
  explicit VerifiedCrateGraph(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Immutable host crate graph for one isolated build-script compilation context.
class VerifiedPreparatoryCrateGraph final {
public:
  ~VerifiedPreparatoryCrateGraph() noexcept(false);
  VerifiedPreparatoryCrateGraph(VerifiedPreparatoryCrateGraph&&) noexcept;
  VerifiedPreparatoryCrateGraph& operator=(VerifiedPreparatoryCrateGraph&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedPreparatoryCrateGraph);

  /// \brief Derives the exact build-script plan required by the selected crate closures.
  ZC_NODISCARD static BuildScriptPlanBuildResult buildPlan(
      const package::VerifiedPackageCompilationRequest& request,
      const package::ResolutionOutput& resolution);

  /// \brief Expands one build-script root and its closed reachable host dependency graph.
  ZC_NODISCARD static PreparatoryCrateGraphBuildResult build(
      const package::VerifiedPackageCompilationRequest& request,
      const package::BuildScriptPlanNode& node, const package::ResolutionOutput& resolution,
      const package::VerifiedBuildScriptPlan& plan,
      zc::ArrayPtr<const package::VerifiedBuildScriptResult> completedResults);

  ZC_NODISCARD const identity::CrateKey& root() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::PackageKey> packages() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::PackageDependencyEdgeKey> packageEdges() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CrateKey> crates() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CrateDependencyEdgeKey> edges() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;

private:
  struct Impl;
  explicit VerifiedPreparatoryCrateGraph(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::driver
