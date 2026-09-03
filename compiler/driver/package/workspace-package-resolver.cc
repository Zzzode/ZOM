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

#include "compiler/driver/package/workspace-package-resolver.h"

#include "compiler/driver/package/source-record.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/key/package-key.h"

namespace zomlang::compiler::driver::package {
namespace {

// Renders a canonical workspace-relative path back to a filesystem path.
zc::Path filesystemPath(const identity::CanonicalWorkspaceRelativePath& path) {
  zc::Path result(nullptr);
  for (uint32_t index = 0; index < path.leadingParents(); ++index) {
    result = zc::mv(result).eval(".."_zc);
  }
  for (const auto& segment : path.segments()) { result = zc::mv(result).append(segment.text()); }
  return result;
}

// Rebuilds a package base key from a package key, in the resolver arena.
identity::PackageBaseKey packageBase(zc::MemoryResource& resource,
                                     const identity::PackageKey& packageKey) {
  auto name = identity::PackageName::fromCanonical(resource, packageKey.name());
  auto version = identity::ResolvedVersion::fromCanonical(resource, packageKey.version());
  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(versionValue, version) {
      return identity::PackageBaseKey::from(packageKey.source().clone(resource), zc::mv(nameValue),
                                            zc::mv(versionValue));
    }
  }
  ZC_UNREACHABLE;
}

// Rebuilds the sorted feature set of a package key, in the resolver arena.
identity::SortedFeatureSet packageFeatures(zc::MemoryResource& resource,
                                           const identity::PackageKey& packageKey) {
  zc::Vector<identity::FeatureName> features(resource, packageKey.features().size());
  for (const auto& feature : packageKey.features()) { features.add(feature.clone(resource)); }
  auto result = identity::SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

}  // namespace

WorkspacePackageResolveResult resolveWorkspacePackageInput(
    const zc::Filesystem& filesystem, zc::PathPtr workspaceRoot, zc::MemoryResource& resolverMemory,
    const NormalizedPackageCompilationRequest& normalizedRequest,
    VerifiedPackageCompilationRequest&& request, ir::VerifiedTargetSelection&& hostTarget,
    ir::VerifiedTargetSelection&& target, const NormalizedWorkspace& workspace,
    FreshSourceDirectoryFactory& snapshotFactory) {
  zc::Vector<ResolverRelease> releases(resolverMemory);
  zc::Vector<ResolvedPackageSourceSnapshot> snapshots;

  zc::Maybe<MaterializationIssue> admissionMaterializationIssue;
  bool admissionRecordRejected = false;
  bool admissionNameOrVersionInvalid = false;
  auto admitPackage = [&](const NormalizedManifest& manifest,
                          identity::CanonicalWorkspaceRelativePath&& relativePath) -> bool {
    auto name = identity::PackageName::fromCanonical(manifest.packageName());
    auto version = identity::ResolvedVersion::fromCanonical(manifest.packageVersion());
    if (name == zc::none || version == zc::none) {
      admissionNameOrVersionInvalid = true;
      return false;
    }
    identity::PackageBaseKey base = packageBase(resolverMemory, request.roots()[0].packageKey());
    ZC_IF_SOME(nameValue, name) {
      ZC_IF_SOME(versionValue, version) {
        base = identity::PackageBaseKey::from(
            identity::CanonicalPackageSource::localPath(relativePath.clone()), zc::mv(nameValue),
            zc::mv(versionValue));
      }
    }
    auto packageRoot = workspaceRoot.clone().eval(filesystemPath(relativePath).toString());
    auto directory = filesystem.getRoot().openSubdir(packageRoot);
    SourceDirectoryMaterializer materializer;
    auto snapshot = materializer.materialize(*directory, snapshotFactory);
    if (snapshot.is<MaterializationIssue>()) {
      admissionMaterializationIssue = snapshot.get<MaterializationIssue>();
      return false;
    }
    auto& snapshotValue = snapshot.get<DigestVerifiedSourceSnapshot>();
    auto record = LocalPackageRecord::from(base.clone(), manifest.clone(), snapshotValue);
    if (record == zc::none) {
      admissionRecordRejected = true;
      return false;
    }
    ZC_IF_SOME(recordValue, record) {
      releases.add(ResolverRelease::fromLocal(resolverMemory, recordValue));
    }
    snapshots.add(ResolvedPackageSourceSnapshot::from(base.clone(), zc::mv(snapshotValue)));
    return true;
  };

  auto admissionFailure = [&]() -> ResolveFailure {
    ZC_IF_SOME(issue, admissionMaterializationIssue) {
      return ResolveFailure(SourceMaterializationFailed{issue});
    }
    if (admissionRecordRejected) { return ResolveFailure(LocalRecordRejected{}); }
    return ResolveFailure(PackageNameOrVersionInvalid{});
  };

  if (workspace.root().hasPackage()) {
    zc::Vector<identity::CanonicalPathSegment> noSegments;
    if (!admitPackage(workspace.root(),
                      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(noSegments)))) {
      return admissionFailure();
    }
  }
  for (const auto& member : workspace.members()) {
    if (!admitPackage(member.manifest(), member.packageDirectory().clone())) {
      return admissionFailure();
    }
  }

  const auto workspaceDirectory =
      filesystem.getRoot().openSubdir(workspaceRoot, zc::WriteMode::MODIFY);
  bool includeDevelopment = false;
  for (const auto& requested : normalizedRequest.requestedTargets()) {
    includeDevelopment = includeDevelopment || requested.kind == identity::CrateTargetKind::Test ||
                         requested.kind == identity::CrateTargetKind::Benchmark ||
                         requested.kind == identity::CrateTargetKind::Example;
  }
  zc::Vector<ResolverRoot> roots(resolverMemory);
  roots.add(ResolverRoot::from(packageBase(resolverMemory, request.roots()[0].packageKey()),
                               packageFeatures(resolverMemory, request.roots()[0].packageKey()),
                               false, includeDevelopment));
  const bool useLocked = normalizedRequest.lockMode() == PackageLockMode::LockedOnly ||
                         (normalizedRequest.lockMode() == PackageLockMode::PreferLocked &&
                          workspaceDirectory->exists(zc::Path("Zom.lock"_zc)));
  if (useLocked) {
    auto locked = LockfileCodec::read(*workspaceDirectory);
    if (locked.is<LockIssue>()) { return ResolveFailure(LockReadFailed{locked.get<LockIssue>()}); }
    const auto& lockedGraph = locked.get<VerifiedLockGraph>();
    LockReplayMetrics metrics;
    auto resolved =
        PackageResolver::resolveLocked(resolverMemory, roots, releases, lockedGraph, metrics);
    if (resolved.is<PackageResolverFailure>()) {
      return ResolveFailure(LockedResolveFailed{zc::mv(resolved.get<PackageResolverFailure>())});
    }
    if (metrics.solverInvocations != 0) { return ResolveFailure(LockedResolveFailed{zc::none}); }
    auto& graph = resolved.get<ResolutionOutput>();
    zc::Vector<ResolvedPackageSourceSnapshot> selectedSnapshots;
    for (auto& snapshot : snapshots) {
      for (const auto& selected : graph.packages()) {
        identity::CanonicalEncoder snapshotEncoder(resolverMemory);
        identity::CanonicalEncoder selectedEncoder(resolverMemory);
        snapshot.package().encode(snapshotEncoder);
        packageBase(resolverMemory, selected.key()).encode(selectedEncoder);
        if (snapshotEncoder.finish().asPtr() == selectedEncoder.finish().asPtr()) {
          selectedSnapshots.add(zc::mv(snapshot));
          break;
        }
      }
    }
    return verifyAndBuildPackageInputs(zc::mv(request), zc::mv(hostTarget), zc::mv(target),
                                       zc::mv(graph), zc::mv(selectedSnapshots));
  }

  auto resolved = PackageResolver::resolve(resolverMemory, roots, releases);
  if (resolved.is<PackageResolverFailure>()) {
    return ResolveFailure(ResolveFailed{zc::mv(resolved.get<PackageResolverFailure>())});
  }
  auto& graph = resolved.get<ResolutionOutput>();
  if (normalizedRequest.lockMode() == PackageLockMode::Update) {
    const auto canonical = LockfileCodec::write(graph.lockGraph());
    if (AtomicLockfileWriter::write(*workspaceDirectory, canonical) != zc::none) {
      return ResolveFailure(LockWriteFailed{});
    }
  }
  zc::Vector<ResolvedPackageSourceSnapshot> selectedSnapshots;
  for (auto& snapshot : snapshots) {
    for (const auto& selected : graph.packages()) {
      identity::CanonicalEncoder snapshotEncoder(resolverMemory);
      identity::CanonicalEncoder selectedEncoder(resolverMemory);
      snapshot.package().encode(snapshotEncoder);
      packageBase(resolverMemory, selected.key()).encode(selectedEncoder);
      if (snapshotEncoder.finish().asPtr() == selectedEncoder.finish().asPtr()) {
        selectedSnapshots.add(zc::mv(snapshot));
        break;
      }
    }
  }
  return verifyAndBuildPackageInputs(zc::mv(request), zc::mv(hostTarget), zc::mv(target),
                                     zc::mv(graph), zc::mv(selectedSnapshots));
}

}  // namespace zomlang::compiler::driver::package
