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

#include "compiler/driver/package/verified-package-inputs.h"

#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/key/package-key.h"

namespace zomlang::compiler::driver::package {
namespace {

// Canonical byte encoding of a registered target selection, for byte-equality.
zc::Array<uint8_t> targetSelectionBytes(const RegisteredTargetSelection& selection) {
  identity::CanonicalEncoder encoder;
  selection.encode(encoder);
  return encoder.finish();
}

// True when two registered target selections encode to identical canonical bytes.
bool sameTargetSelection(const RegisteredTargetSelection& left,
                         const RegisteredTargetSelection& right) {
  return targetSelectionBytes(left).asPtr() == targetSelectionBytes(right).asPtr();
}

// True when a resolved package key names the same source, name, and version as a
// package base key.
bool packageMatchesBase(const identity::PackageKey& package, const identity::PackageBaseKey& base) {
  identity::CanonicalEncoder packageSource;
  identity::CanonicalEncoder baseSource;
  package.source().encode(packageSource);
  base.source().encode(baseSource);
  return packageSource.finish().asPtr() == baseSource.finish().asPtr() &&
         package.name() == base.name() && package.version() == base.version();
}

// True when the resolution output contains the given package.
bool graphContainsPackage(const ResolutionOutput& graph, const identity::PackageKey& package) {
  const auto expected = package.encode();
  for (const auto& selected : graph.packages()) {
    if (expected.asPtr() == selected.key().encode().asPtr()) { return true; }
  }
  return false;
}

// True when the resolution output and the resolved snapshots cover exactly the
// same package set.
bool graphAndSnapshotsMatch(const ResolutionOutput& graph,
                            zc::ArrayPtr<const ResolvedPackageSourceSnapshot> snapshots) {
  if (graph.packages().size() == 0 || snapshots.size() == 0) { return false; }
  for (const auto& selected : graph.packages()) {
    bool found = false;
    for (const auto& snapshot : snapshots) {
      if (packageMatchesBase(selected.key(), snapshot.package())) {
        found = true;
        break;
      }
    }
    if (!found) { return false; }
  }
  for (const auto& snapshot : snapshots) {
    bool found = false;
    for (const auto& selected : graph.packages()) {
      if (packageMatchesBase(selected.key(), snapshot.package())) {
        found = true;
        break;
      }
    }
    if (!found) { return false; }
  }
  return true;
}

}  // namespace

VerifiedPackageInputsResult verifyAndBuildPackageInputs(
    VerifiedPackageCompilationRequest&& request, ir::VerifiedTargetSelection&& hostTarget,
    ir::VerifiedTargetSelection&& target, ResolutionOutput&& graph,
    zc::Vector<ResolvedPackageSourceSnapshot>&& snapshots) {
  const auto& requestHost = request.hostTarget();
  const auto& requestTarget = request.target();
  const auto& verifiedHost = hostTarget.packageSelection();
  const auto& verifiedTarget = target.packageSelection();

  if (requestHost.registryRevision() != verifiedHost.registryRevision()) {
    return VerifyFailure(
        RegistryRevisionMismatch{RegistryRevisionMismatchKind::HostRequestVersusVerified});
  }
  if (requestTarget.registryRevision() != verifiedTarget.registryRevision()) {
    return VerifyFailure(
        RegistryRevisionMismatch{RegistryRevisionMismatchKind::TargetRequestVersusVerified});
  }
  if (verifiedHost.registryRevision() != verifiedTarget.registryRevision()) {
    return VerifyFailure(RegistryRevisionMismatch{RegistryRevisionMismatchKind::CrossVerified});
  }
  if (!sameTargetSelection(requestHost, verifiedHost)) {
    return VerifyFailure(TargetSelectionMismatch{TargetSelectionMismatchSide::Host});
  }
  if (!sameTargetSelection(requestTarget, verifiedTarget)) {
    return VerifyFailure(TargetSelectionMismatch{TargetSelectionMismatchSide::Target});
  }
  if (!graphAndSnapshotsMatch(graph, snapshots)) { return VerifyFailure(GraphSnapshotMismatch{}); }
  for (const auto& root : request.roots()) {
    if (!graphContainsPackage(graph, root.packageKey())) {
      return VerifyFailure(RootPackageMissing{});
    }
  }

  auto plan = VerifiedPreparatoryCrateGraph::buildPlan(request, graph);
  if (!plan.is<VerifiedBuildScriptPlan>()) {
    return VerifyFailure(BuildPlanFailed{plan.get<CrateGraphIssue>()});
  }

  auto built = buildInstalledPackageInputs(
      zc::mv(request), zc::mv(hostTarget), zc::mv(target), zc::mv(graph),
      zc::mv(plan.get<VerifiedBuildScriptPlan>()), zc::mv(snapshots));
  if (built.is<CrateGraphIssue>()) {
    return VerifyFailure(CrateGraphExpansionFailed{built.get<CrateGraphIssue>()});
  }
  return zc::mv(built.get<InstalledPackageInputs>());
}

}  // namespace zomlang::compiler::driver::package
