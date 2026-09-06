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

#include "compiler/checker/checker-identity-authority.h"
#include "compiler/diagnostics/fact/semantic-diagnostic-fact.h"
#include "compiler/ownership/admission/surface-admission.h"
#include "compiler/ownership/diagnostics/ownership-source-failure.h"
#include "zc/core/array.h"

namespace zomlang::compiler::ownership {

/// \brief Project frontend surface-admission failures into immutable diagnostic facts.
ZC_NODISCARD diagnostics::SemanticDiagnosticBatchProjectionResult projectSurfaceSourceFailures(
    identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
    zc::ArrayPtr<const SurfaceFailure> failures);

/// \brief Project closed ownership source failures with their cause-note diagnostics.
///
/// The switch over `SourceFailure` is compile-time exhaustive: adding a
/// variant without extending this projector produces a `-Wswitch` warning. Each
/// cause in the active variant's cause sequence becomes one child note.
ZC_NODISCARD diagnostics::SemanticDiagnosticBatchProjectionResult projectOwnershipSourceFailures(
    const checker::CheckerIdentityAuthority& identities,
    zc::ArrayPtr<const SourceFailure> failures);

}  // namespace zomlang::compiler::ownership
