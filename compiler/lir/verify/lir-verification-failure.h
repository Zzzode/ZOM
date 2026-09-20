// Copyright (c) 2026 Zode.Zang. All rights reserved
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

#include "compiler/basic/incident/compiler-incident.h"
#include "compiler/lir/verify/lir-verifier.h"
#include "compiler/lir/verify/translation-validator.h"
#include "zc/core/one-of.h"

namespace zomlang::compiler::lir {

/// \brief One closed result of attempting to publish an LIR verification
/// rejection to the internal incident rail.
struct RejectedLirVerification final {
  basic::BoundedIncidentSet incidents;
};

/// \brief Maps a structural-verifier finding to the internal compiler-incident
/// rail.
///
/// Structural verification is a compiler self-consistency check, not a source
/// diagnostic: a finding means lowering produced an LIR module that breaks the
/// verifier's own proof. The fact is admitted at
/// `IrFailurePhase::LirVerification` under session ownership (the pre-
/// monomorphization module has no InstanceId), kind `InvalidFact`. Returns none
/// only if the fact cannot be admitted or projected.
zc::Maybe<RejectedLirVerification> projectLirVerificationIncident(
    const LirVerificationFinding& finding) noexcept;

/// \brief Maps a translation-validation finding to the internal compiler-
/// incident rail at `IrFailurePhase::LirVerification`.
zc::Maybe<RejectedLirVerification> projectLirVerificationIncident(
    const TranslationFinding& finding) noexcept;

}  // namespace zomlang::compiler::lir
