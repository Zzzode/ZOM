// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/internal/scope-arena.h"

namespace zomlang::compiler::binder {

/// \brief Source failure discovered while resolving one control-transfer statement.
struct ControlTransferFailureFact final {
  BinderDiagnosticCode diagnostic;
  ast::NodeId node;
  identity::SourceSpan source;
  uint32_t schemaPreorderOrdinal;
};

/// \brief Deterministic control-transfer facts awaiting candidate verification.
struct ControlTransferCandidate final {
  zc::Vector<ControlTransferFact> controlTransfers;
  zc::Vector<ControlTransferFailureFact> failures;
};

using ControlTransferBuildResult = zc::OneOf<ControlTransferCandidate, BinderInvariantFact>;

/// \brief Resolves unlabeled break and continue statements against lexical scopes.
class ControlTransferBuilder final {
public:
  ZC_NODISCARD static ControlTransferBuildResult build(const VerifiedBindingInput& input,
                                                       const ScopeArenaCandidate& arena);
};

}  // namespace zomlang::compiler::binder
