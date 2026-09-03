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

#include <cstdint>

#include "compiler/driver/graph/crate-graph.h"
#include "compiler/driver/package/package-compilation-request.h"
#include "compiler/driver/package/package-input-installer.h"
#include "compiler/driver/package/package-resolver.h"
#include "compiler/driver/package/source-snapshot.h"
#include "compiler/ir/target-registry.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::driver::package {

/// \brief Which registry-revision agreement failed while verifying package inputs.
enum class RegistryRevisionMismatchKind : uint8_t {
  /// The host request selection and its verified selection disagree.
  HostRequestVersusVerified = 0,
  /// The target request selection and its verified selection disagree.
  TargetRequestVersusVerified = 1,
  /// The two verified selections were issued by different registry revisions.
  CrossVerified = 2,
};

/// \brief Which target selection failed byte-for-byte agreement.
enum class TargetSelectionMismatchSide : uint8_t { Host = 0, Target = 1 };

/// \brief The verified request and a selection disagree on registry revision.
struct RegistryRevisionMismatch final {
  RegistryRevisionMismatchKind kind;
};
/// \brief A request target selection and its verified selection differ.
struct TargetSelectionMismatch final {
  TargetSelectionMismatchSide side;
};
/// \brief The resolution output and the resolved snapshots do not cover the same
/// package set.
struct GraphSnapshotMismatch final {};
/// \brief A requested root package is absent from the resolution output.
struct RootPackageMissing final {};
/// \brief Deriving the build-script plan for the request failed.
struct BuildPlanFailed final {
  CrateGraphIssue issue;
};
/// \brief Expanding the final crate graph failed.
struct CrateGraphExpansionFailed final {
  CrateGraphIssue issue;
};

/// \brief A closed set of package-input verification failures. Each alternative's
/// payload is determined by its type: the mismatch arms carry the exact side or
/// kind that failed, and the two crate-graph arms carry the `CrateGraphIssue` the
/// expansion produced, so a caller can always locate the specific check that
/// rejected the inputs.
using VerifyFailure =
    zc::OneOf<RegistryRevisionMismatch, TargetSelectionMismatch, GraphSnapshotMismatch,
              RootPackageMissing, BuildPlanFailed, CrateGraphExpansionFailed>;

/// \brief The outcome of verifying and building package inputs: the installed
/// inputs bundle or a typed verification failure.
using VerifiedPackageInputsResult = zc::OneOf<InstalledPackageInputs, VerifyFailure>;

/// \brief Verifies one atomic package session input and expands its crate graph.
///
/// This is the single session-agnostic authority for admitting a verified package
/// input. It checks registry-revision agreement, target-selection agreement, and
/// graph/snapshot coverage, derives the build-script plan, and expands the final
/// crate graph through `buildInstalledPackageInputs` (the sole
/// `VerifiedCrateGraph::buildFinal` call site). Every rejection is reported as a
/// typed `VerifyFailure` so both the CLI and the IDE service can locate the exact
/// check that failed; on success it returns the `InstalledPackageInputs` bundle a
/// session or an IDE workspace commits. It owns no session state and commits
/// nothing.
///
/// \param request The verified package compilation request; consumed.
/// \param hostTarget The verified host target selection; consumed.
/// \param target The verified target selection; consumed.
/// \param graph The package resolution output; consumed.
/// \param snapshots The resolved package source snapshots; consumed.
/// \return The installed inputs bundle, or the typed verification failure.
ZC_NODISCARD VerifiedPackageInputsResult verifyAndBuildPackageInputs(
    VerifiedPackageCompilationRequest&& request, ir::VerifiedTargetSelection&& hostTarget,
    ir::VerifiedTargetSelection&& target, ResolutionOutput&& graph,
    zc::Vector<ResolvedPackageSourceSnapshot>&& snapshots);

}  // namespace zomlang::compiler::driver::package
