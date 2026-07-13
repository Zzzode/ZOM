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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "zomlang/compiler/binder/internal/scope-arena.h"

namespace zomlang::compiler::binder {

struct DefinitionSkeletonCandidate;

/// \brief Source failure discovered while resolving one body identifier.
struct BodyBindingFailureFact final {
  BinderDiagnosticCode diagnostic;
  ast::NodeId node;
  identity::SemanticIdentifier name;
  Namespace expectedNamespace;
  identity::SourceSpan source;
  uint32_t schemaPreorderOrdinal;
};

/// \brief Deterministic body facts awaiting candidate-wide failure indexing.
struct BodyBindingCandidate final {
  zc::Vector<BindingResolution> nodeBindings;
  zc::Vector<BodyBindingFailureFact> failures;
  zc::Vector<ShadowTargetFact> shadowTargets;
};

using BodyBindingBuildResult = zc::OneOf<BodyBindingCandidate, BinderInvariantFact>;

/// \brief Resolves body names and activates source-ordered lexical definitions.
class BodyBindingBuilder final {
public:
  ZC_NODISCARD static BodyBindingBuildResult build(const VerifiedBindingInput& input,
                                                   ScopeArenaCandidate& arena,
                                                   DefinitionSkeletonCandidate& skeleton);
};

}  // namespace zomlang::compiler::binder
