// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/binder/binding-metadata.h"

namespace zomlang::compiler::diagnostics {
class DiagnosticEngine;
}

namespace zomlang::compiler::binder {

/// \brief Owned diagnostic argument admitted only from a canonical semantic identifier.
class VerifiedIdentifierArgument final {
public:
  VerifiedIdentifierArgument(VerifiedIdentifierArgument&&) noexcept = default;
  VerifiedIdentifierArgument& operator=(VerifiedIdentifierArgument&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedIdentifierArgument);

  ZC_NODISCARD static VerifiedIdentifierArgument from(
      const identity::SemanticIdentifier& identifier);
  ZC_NODISCARD zc::String take() &&;

private:
  explicit VerifiedIdentifierArgument(zc::String&& value) noexcept;
  zc::String value;
};

/// \brief Sole typed projection from binder source failures to user diagnostics.
class BindingDiagnosticAdapter final {
public:
  ZC_NODISCARD static bool emitLookupFailure(diagnostics::DiagnosticEngine& diagnostics,
                                             BinderDiagnosticCode code, source::SourceLoc primary,
                                             VerifiedIdentifierArgument&& identifier,
                                             Namespace expectedNamespace);
  ZC_NODISCARD static bool emitRedeclaration(diagnostics::DiagnosticEngine& diagnostics,
                                             BinderDiagnosticCode code, source::SourceLoc primary,
                                             source::SourceLoc previous,
                                             VerifiedIdentifierArgument&& identifier);
};

}  // namespace zomlang::compiler::binder
