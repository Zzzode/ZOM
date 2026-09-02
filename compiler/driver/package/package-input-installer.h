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
// See the License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "compiler/driver/graph/crate-graph.h"
#include "compiler/driver/package/build-script-plan.h"
#include "compiler/driver/package/package-compilation-request.h"
#include "compiler/driver/package/package-resolver.h"
#include "compiler/driver/package/source-snapshot.h"
#include "compiler/ir/target-registry.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::driver::package {

/// \brief The verified package inputs a session retains after admission.
///
/// Holds the exact values a successful package-input install commits to session
/// state: the verified compilation request, both verified target selections, the
/// resolution output, the verified build-script plan, the resolved source
/// snapshots, and the final-target crate graph expanded from them. The session
/// moves each member into its own field; this type carries no session identity.
struct InstalledPackageInputs final {
  VerifiedPackageCompilationRequest request;
  ir::VerifiedTargetSelection hostTarget;
  ir::VerifiedTargetSelection target;
  ResolutionOutput graph;
  VerifiedBuildScriptPlan buildScriptPlan;
  zc::Vector<ResolvedPackageSourceSnapshot> snapshots;
  VerifiedCrateGraph crateGraph;
};

/// \brief Expands the final crate graph and bundles the verified package inputs.
///
/// Runs `VerifiedCrateGraph::buildFinal` over the verified request, resolution,
/// and build-script plan, then moves every component into an
/// `InstalledPackageInputs`. Returns `zc::none` when crate-graph expansion
/// rejects the inputs, in which case the moved-from arguments are consumed and no
/// bundle is produced. This is the session-agnostic core of package-input
/// installation: it commits nothing and owns no session state.
///
/// \param request The verified package compilation request; consumed.
/// \param hostTarget The verified host target selection; consumed.
/// \param target The verified target selection; consumed.
/// \param graph The package resolution output; consumed.
/// \param buildScriptPlan The verified build-script plan; consumed.
/// \param snapshots The resolved package source snapshots; consumed.
/// \return The bundled inputs, or `zc::none` when crate-graph expansion fails.
ZC_NODISCARD zc::Maybe<InstalledPackageInputs> buildInstalledPackageInputs(
    VerifiedPackageCompilationRequest&& request, ir::VerifiedTargetSelection&& hostTarget,
    ir::VerifiedTargetSelection&& target, ResolutionOutput&& graph,
    VerifiedBuildScriptPlan&& buildScriptPlan,
    zc::Vector<ResolvedPackageSourceSnapshot>&& snapshots);

}  // namespace zomlang::compiler::driver::package
