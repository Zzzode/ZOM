// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/driver/materialized-module-graph-query.h"

namespace zomlang::compiler::checker {

/// \brief Complete retained eight-domain identity authority for Checker consumers.
class CheckerIdentityAuthority final {
public:
  using GraphLease =
      query::QueryCapabilityLease<const driver::module_graph_query::MaterializedModuleGraph>;
  using BoundModuleView = driver::module_graph_query::CheckerBoundModuleView;

  ~CheckerIdentityAuthority() noexcept(false);
  CheckerIdentityAuthority(CheckerIdentityAuthority&&) noexcept;
  CheckerIdentityAuthority& operator=(CheckerIdentityAuthority&&) noexcept;
  ZC_DISALLOW_COPY(CheckerIdentityAuthority);

  /// \brief Retains one verified graph and every dependency-ordered bound-module view.
  ZC_NODISCARD static zc::Maybe<CheckerIdentityAuthority> from(
      zc::Vector<BoundModuleView>&& modules);
  /// \brief Retains an independent authority lease over the same sealed graph.
  ZC_NODISCARD CheckerIdentityAuthority clone() const;
  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD query::DatabaseRevision revision() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD const GraphLease& graphLease() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BoundModuleView> modules() const noexcept;
  /// \brief Returns the uniquely retained Checker view for one materialized module.
  ZC_NODISCARD zc::Maybe<const BoundModuleView&> boundModule(
      identity::ModuleId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const driver::module_graph_query::MaterializedCompilationUnitEntry&>
  compilationUnit(identity::CompilationUnitId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const driver::module_graph_query::MaterializedCompilationUnitEntry&>
  compilationUnit(const identity::CompilationUnitIdentity& key) const noexcept;
  ZC_NODISCARD zc::Maybe<const driver::module_graph_query::MaterializedCrateEntry&> crate(
      identity::CrateId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const driver::module_graph_query::MaterializedCrateEntry&> crate(
      const identity::CrateKey& key) const noexcept;
  ZC_NODISCARD zc::Maybe<const driver::module_graph_query::MaterializedSourceEntry&> sourceFile(
      identity::SourceFileId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const driver::module_graph_query::MaterializedSourceEntry&> sourceFile(
      const identity::SourceFileKey& key) const noexcept;
  ZC_NODISCARD zc::Maybe<const driver::module_graph_query::MaterializedModuleEntry&> module(
      identity::ModuleId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const driver::module_graph_query::MaterializedModuleEntry&> module(
      const identity::ModuleKey& key) const noexcept;
  ZC_NODISCARD zc::Maybe<const binder::MaterializedDefinitionIdentityEntry&> definition(
      identity::DefId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const binder::MaterializedDefinitionIdentityEntry&> definition(
      const identity::DefinitionKey& key) const noexcept;
  /// rief Returns the retained complete callable-header authority for one definition.
  ZC_NODISCARD zc::Maybe<const identity::DefinitionIdentityAuthority&> definitionAuthority(
      identity::DefId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const binder::MaterializedImplementationIdentityEntry&> implementation(
      identity::ImplId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const binder::MaterializedImplementationIdentityEntry&> implementation(
      const identity::ImplKey& key) const noexcept;
  ZC_NODISCARD zc::Maybe<const binder::MaterializedGenericParameterIdentityEntry&> genericParameter(
      identity::GenericParameterId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const binder::MaterializedGenericParameterIdentityEntry&> genericParameter(
      const identity::GenericParameterKey& key) const noexcept;
  ZC_NODISCARD zc::Maybe<const binder::MaterializedCallableParameterIdentityEntry&>
  callableParameter(identity::CallableParameterId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const binder::MaterializedCallableParameterIdentityEntry&>
  callableParameter(const identity::CallableParameterKey& key) const noexcept;

private:
  struct Impl;
  explicit CheckerIdentityAuthority(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::checker
