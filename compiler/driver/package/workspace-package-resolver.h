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

#include "compiler/driver/package/lockfile.h"
#include "compiler/driver/package/materialization-issue.h"
#include "compiler/driver/package/package-compilation-request.h"
#include "compiler/driver/package/package-input-installer.h"
#include "compiler/driver/package/package-resolver.h"
#include "compiler/driver/package/source-snapshot.h"
#include "compiler/driver/package/verified-package-inputs.h"
#include "compiler/driver/package/workspace-normalizer.h"
#include "compiler/ir/target-registry.h"
#include "zc/core/filesystem.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"

namespace zomlang::compiler::driver::package {

/// \brief The writable snapshot parent directory could not be opened.
struct SnapshotParentUnavailable final {};
/// \brief A package manifest declared a name or version that is not canonical.
struct PackageNameOrVersionInvalid final {};
/// \brief Materializing one package source tree failed.
struct SourceMaterializationFailed final {
  MaterializationIssue issue;
};
/// \brief The verified source snapshot could not form a local package record.
struct LocalRecordRejected final {};
/// \brief The existing lockfile could not be read.
struct LockReadFailed final {
  LockIssue issue;
};
/// \brief Locked resolution failed. Carries the resolver failure when one was
/// produced; a `zc::none` failure means the locked graph unexpectedly required a
/// solver invocation, which violates the locked-replay invariant.
struct LockedResolveFailed final {
  zc::Maybe<PackageResolverFailure> failure;
};
/// \brief Writing the refreshed lockfile failed.
struct LockWriteFailed final {};
/// \brief Unlocked resolution failed.
struct ResolveFailed final {
  PackageResolverFailure failure;
};

/// \brief A closed set of workspace resolution failures. Each alternative carries
/// the most specific typed diagnostic the failing step produced, so a caller can
/// always locate and render the exact cause without a generic string.
using ResolveFailure = zc::OneOf<SnapshotParentUnavailable, PackageNameOrVersionInvalid,
                                 SourceMaterializationFailed, LocalRecordRejected, LockReadFailed,
                                 LockedResolveFailed, LockWriteFailed, ResolveFailed>;

/// \brief The outcome of resolving one workspace's package inputs: the installed
/// inputs bundle, a typed resolution failure, or a typed verification failure.
using WorkspacePackageResolveResult =
    zc::OneOf<InstalledPackageInputs, ResolveFailure, VerifyFailure>;

/// \brief Resolves, materializes, and verifies one workspace's package inputs.
///
/// This is the single session-agnostic authority for turning a verified request
/// and a loaded workspace into installed package inputs: it materializes each
/// package source tree through the injected `snapshotFactory`, resolves the
/// dependency graph (honoring the request's lock mode), and hands the result to
/// `verifyAndBuildPackageInputs`. It emits no diagnostics; every failure is a
/// typed `ResolveFailure` or `VerifyFailure` the caller renders at its own
/// boundary. The CLI injects a disk-backed replacement factory; an IDE workspace
/// injects an in-memory factory, so the same authority serves both without a
/// second copy of the flow.
///
/// \param filesystem The filesystem to read package sources and lockfiles from.
/// \param workspaceRoot The workspace root path.
/// \param resolverMemory The arena backing resolver-owned allocations.
/// \param normalizedRequest The normalized compilation request (lock mode, targets).
/// \param request The verified package compilation request; consumed.
/// \param hostTarget The verified host target selection; consumed.
/// \param target The verified target selection; consumed.
/// \param workspace The loaded and normalized workspace.
/// \param snapshotFactory The factory that materializes fresh source directories.
/// \return The installed inputs bundle or a typed resolution/verification failure.
ZC_NODISCARD WorkspacePackageResolveResult resolveWorkspacePackageInput(
    const zc::Filesystem& filesystem, zc::PathPtr workspaceRoot, zc::MemoryResource& resolverMemory,
    const NormalizedPackageCompilationRequest& normalizedRequest,
    VerifiedPackageCompilationRequest&& request, ir::VerifiedTargetSelection&& hostTarget,
    ir::VerifiedTargetSelection&& target, const NormalizedWorkspace& workspace,
    FreshSourceDirectoryFactory& snapshotFactory);

}  // namespace zomlang::compiler::driver::package
