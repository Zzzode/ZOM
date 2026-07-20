// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/internal/scope-arena.h"

namespace zomlang::compiler::binder {

struct ModuleSkeletonSurfaceSeed final {
  ModuleSkeletonSurfaceSeed(BindingNameKey&& name, identity::DefId identity,
                            identity::SourceSpan&& source, bool exported,
                            zc::Maybe<identity::SourceSpan>&& exportSpan) noexcept;
  ModuleSkeletonSurfaceSeed(ModuleSkeletonSurfaceSeed&&) noexcept = default;
  ModuleSkeletonSurfaceSeed& operator=(ModuleSkeletonSurfaceSeed&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleSkeletonSurfaceSeed);
  BindingNameKey name;
  identity::DefId identity;
  identity::SourceSpan source;
  bool exported;
  zc::Maybe<identity::SourceSpan> exportSpan;
};

struct BindingDuplicateFact final {
  BinderDiagnosticCode diagnostic;
  BinderEmitterSite emitterSite;
  identity::DeclaredDefinitionName name;
  BindingTarget rejected;
  ast::NodeId primaryNode;
  ast::NodeId previousNode;
  identity::SourceSpan primary;
  identity::SourceSpan previous;
};

struct DefinitionSkeletonCandidate final {
  zc::Vector<DefinitionFact> definitions;
  zc::Vector<GenericParameterFact> genericParameters;
  zc::Vector<CallableParameterFact> callableParameters;
  zc::Vector<OwnerLocalBindingFact> ownerLocalBindings;
  zc::Vector<ImplBindingFact> impls;
  zc::Vector<BindingDuplicateFact> duplicates;
  zc::Vector<ModuleSkeletonSurfaceSeed> moduleSurfaceSeeds;
};

using DefinitionSkeletonBuildResult = zc::OneOf<DefinitionSkeletonCandidate, BinderInvariantFact>;

/// \brief Sole authority for collision-free skeleton and scope-owning generic bindings.
class BindingSkeletonBuilder final {
public:
  ZC_NODISCARD static DefinitionSkeletonBuildResult build(const VerifiedBindingInput& input,
                                                          ScopeArenaCandidate& arena);
};

}  // namespace zomlang::compiler::binder
