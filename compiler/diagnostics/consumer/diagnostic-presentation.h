// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/diagnostics/fact/diagnostic-materializer.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"

namespace zomlang::compiler::diagnostics {

/// \brief One presentation-authorized source view for a resolved location.
struct DiagnosticPresentationLocation final {
  zc::StringPtr displayName;
  zc::ArrayPtr<const zc::byte> source;
  DiagnosticSourceRange range;
};

/// \brief Resolves source and document identities without changing diagnostic semantics.
class DiagnosticPresentationResolver {
public:
  virtual ~DiagnosticPresentationResolver() noexcept(false) = default;
  ZC_DISALLOW_COPY(DiagnosticPresentationResolver);

  ZC_NODISCARD virtual zc::Maybe<DiagnosticPresentationLocation> resolve(
      const ResolvedDiagnosticLocation& location) const noexcept = 0;

protected:
  DiagnosticPresentationResolver() = default;
  DiagnosticPresentationResolver(DiagnosticPresentationResolver&&) noexcept = default;
  DiagnosticPresentationResolver& operator=(DiagnosticPresentationResolver&&) noexcept = default;
};

}  // namespace zomlang::compiler::diagnostics
