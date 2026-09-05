// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/basic/incident/compiler-incident.h"
#include "zc/core/common.h"
#include "zc/core/string.h"

namespace zomlang::compiler::diagnostics {

/// \brief Renders one privacy-preserving request-level compiler incident.
/// \return The complete ASCII record, or none for an empty descriptor set.
ZC_NODISCARD zc::Maybe<zc::String> renderCompilerIncident(
    zc::StringPtr compilerVersion, const basic::BoundedIncidentSet& incidents);

}  // namespace zomlang::compiler::diagnostics
