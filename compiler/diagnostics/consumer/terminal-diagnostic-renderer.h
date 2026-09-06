// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "compiler/diagnostics/consumer/diagnostic-policy.h"
#include "compiler/diagnostics/consumer/diagnostic-presentation.h"
#include "zc/core/common.h"
#include "zc/core/io.h"

namespace zomlang::compiler::diagnostics {

enum class TerminalDiagnosticRenderFailure : uint8_t {
  MissingPresentationAuthority = 0x01,
  InvalidPresentationRange = 0x02,
};

/// \brief Renders one immutable policy result and performs one terminal write on success.
///
/// All presentation locations are resolved before output. Stream write failures propagate.
ZC_NODISCARD zc::Maybe<TerminalDiagnosticRenderFailure> renderTerminalDiagnostics(
    const DiagnosticPolicyResult& diagnostics, const DiagnosticPresentationResolver& resolver,
    zc::OutputStream& output, bool useColors);

}  // namespace zomlang::compiler::diagnostics
