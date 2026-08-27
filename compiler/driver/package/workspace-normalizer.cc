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

#include "compiler/driver/package/workspace-normalizer.h"

#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

zc::Array<uint8_t> encodePath(const identity::CanonicalWorkspaceRelativePath& path) {
  identity::CanonicalEncoder encoder;
  path.encode(encoder);
  return encoder.finish();
}

identity::CanonicalWorkspaceRelativePath rootManifestPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto segment = identity::CanonicalPathSegment::fromCanonical("Zom.toml"_zc);
  ZC_IF_SOME(admitted, segment) { segments.add(zc::mv(admitted)); }
  ZC_IREQUIRE(segments.size() == 1, "Zom.toml must be a canonical path segment");
  return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
}

identity::CanonicalWorkspaceRelativePath memberManifestPath(
    const identity::CanonicalWorkspaceRelativePath& directory) {
  ZC_IREQUIRE(directory.leadingParents() == 0,
              "workspace member directory must remain below the workspace root");
  zc::Vector<identity::CanonicalPathSegment> segments(directory.segments().size() + 1);
  for (const auto& segment : directory.segments()) { segments.add(segment.clone()); }
  auto manifest = identity::CanonicalPathSegment::fromCanonical("Zom.toml"_zc);
  ZC_IF_SOME(admitted, manifest) { segments.add(zc::mv(admitted)); }
  return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
}

void sortInputs(zc::Vector<WorkspaceMemberInput>& inputs) {
  for (size_t index = 1; index < inputs.size(); ++index) {
    auto current = zc::mv(inputs[index]);
    size_t insertion = index;
    while (insertion > 0 && encodePath(current.packageDirectory()).asPtr() <
                                encodePath(inputs[insertion - 1].packageDirectory()).asPtr()) {
      inputs[insertion] = zc::mv(inputs[insertion - 1]);
      --insertion;
    }
    inputs[insertion] = zc::mv(current);
  }
}

struct SeenPackage final {
  zc::String name;
  DiagnosticAnchor origin;
};

ManifestFailure workspaceFailure(ManifestIssue issue, DiagnosticAnchor&& primary,
                                 zc::Maybe<DiagnosticAnchor>&& related = zc::none) {
  zc::Vector<DiagnosticAnchor> relatedAnchors;
  ZC_IF_SOME(anchor, related) { relatedAnchors.add(zc::mv(anchor)); }
  auto provenance = DiagnosticProvenance::from(zc::mv(primary), zc::mv(relatedAnchors));
  ZC_IF_SOME(admitted, provenance) { return ManifestFailure::invalid(zc::mv(admitted), issue); }
  ZC_IREQUIRE(false, "workspace failure provenance must remain unique");
  ZC_UNREACHABLE
}

zc::Maybe<ManifestFailure> admitPackage(const NormalizedManifest& manifest,
                                        zc::Vector<SeenPackage>& seen) {
  for (const auto& package : seen) {
    if (package.name == manifest.packageName()) {
      return workspaceFailure(ManifestIssue::DuplicateWorkspacePackageName,
                              manifest.packageNameOrigin().clone(), package.origin.clone());
    }
  }
  seen.add(
      SeenPackage{zc::heapString(manifest.packageName()), manifest.packageNameOrigin().clone()});
  return zc::none;
}

}  // namespace

WorkspaceMemberInput::WorkspaceMemberInput(
    identity::CanonicalWorkspaceRelativePath&& packageDirectory, zc::String&& manifestSource,
    PackageSourceInventory&& inventory) noexcept
    : packageDirectoryValue(zc::mv(packageDirectory)),
      manifestSourceValue(zc::mv(manifestSource)),
      inventoryValue(zc::mv(inventory)) {}

WorkspaceMemberInput WorkspaceMemberInput::from(
    identity::CanonicalWorkspaceRelativePath&& packageDirectory, zc::String&& manifestSource,
    PackageSourceInventory&& inventory) {
  return WorkspaceMemberInput(zc::mv(packageDirectory), zc::mv(manifestSource), zc::mv(inventory));
}

const identity::CanonicalWorkspaceRelativePath& WorkspaceMemberInput::packageDirectory()
    const noexcept {
  return packageDirectoryValue;
}
zc::StringPtr WorkspaceMemberInput::manifestSource() const noexcept { return manifestSourceValue; }
const PackageSourceInventory& WorkspaceMemberInput::inventory() const noexcept {
  return inventoryValue;
}

NormalizedWorkspaceMember::NormalizedWorkspaceMember(
    identity::CanonicalWorkspaceRelativePath&& packageDirectory,
    NormalizedManifest&& manifest) noexcept
    : packageDirectoryValue(zc::mv(packageDirectory)), manifestValue(zc::mv(manifest)) {}

const identity::CanonicalWorkspaceRelativePath& NormalizedWorkspaceMember::packageDirectory()
    const noexcept {
  return packageDirectoryValue;
}
const NormalizedManifest& NormalizedWorkspaceMember::manifest() const noexcept {
  return manifestValue;
}

NormalizedWorkspace::NormalizedWorkspace(NormalizedManifest&& root,
                                         zc::Vector<NormalizedWorkspaceMember>&& members) noexcept
    : rootValue(zc::mv(root)), memberValues(zc::mv(members)) {}

const NormalizedManifest& NormalizedWorkspace::root() const noexcept { return rootValue; }
zc::ArrayPtr<const NormalizedWorkspaceMember> NormalizedWorkspace::members() const noexcept {
  return memberValues.asPtr();
}

WorkspaceNormalizeResult normalizeWorkspace(zc::StringPtr rootManifestSource,
                                            const PackageSourceInventory& rootInventory,
                                            zc::Vector<WorkspaceMemberInput>&& availableMembers) {
  ManifestParser parser;
  auto rootResult =
      parser.parseWorkspaceManifest(rootManifestPath(), rootManifestSource, rootInventory);
  if (rootResult.is<ManifestFailure>()) { return zc::mv(rootResult.get<ManifestFailure>()); }
  auto root = zc::mv(rootResult.get<NormalizedManifest>());
  if (!root.hasWorkspace()) {
    ZC_IREQUIRE(root.hasPackage(), "normalized root must contain a package or workspace");
    zc::Vector<NormalizedWorkspaceMember> noMembers;
    return NormalizedWorkspace(zc::mv(root), zc::mv(noMembers));
  }

  sortInputs(availableMembers);
  for (size_t index = 1; index < availableMembers.size(); ++index) {
    if (encodePath(availableMembers[index - 1].packageDirectory()).asPtr() ==
        encodePath(availableMembers[index].packageDirectory()).asPtr()) {
      return workspaceFailure(ManifestIssue::DuplicateCanonicalValue,
                              root.workspaceOrigin().clone());
    }
  }

  zc::Vector<SeenPackage> seenPackages;
  if (root.hasPackage()) {
    ZC_IF_SOME(issue, admitPackage(root, seenPackages)) { return zc::mv(issue); }
  }

  zc::Vector<NormalizedWorkspaceMember> members(root.workspaceMembers().size());
  for (size_t memberIndex = 0; memberIndex < root.workspaceMembers().size(); ++memberIndex) {
    const auto& requiredPath = root.workspaceMembers()[memberIndex];
    zc::Maybe<const WorkspaceMemberInput&> matching;
    const auto requiredBytes = encodePath(requiredPath);
    for (const auto& candidate : availableMembers) {
      if (encodePath(candidate.packageDirectory()).asPtr() == requiredBytes.asPtr()) {
        matching = candidate;
        break;
      }
    }
    if (matching == zc::none) {
      return workspaceFailure(ManifestIssue::WorkspaceMemberMissing,
                              root.workspaceMemberOrigin(memberIndex).clone());
    }

    ZC_IF_SOME(candidate, matching) {
      auto memberResult = parser.parseWorkspaceManifest(
          memberManifestPath(requiredPath), candidate.manifestSource(), candidate.inventory());
      if (memberResult.is<ManifestFailure>()) {
        return zc::mv(memberResult.get<ManifestFailure>());
      }
      auto member = zc::mv(memberResult.get<NormalizedManifest>());
      if (member.hasWorkspace()) {
        return workspaceFailure(ManifestIssue::NestedWorkspace, member.workspaceOrigin().clone());
      }
      ZC_IREQUIRE(member.hasPackage(), "non-workspace member manifest must contain a package");
      ZC_IF_SOME(issue, admitPackage(member, seenPackages)) { return zc::mv(issue); }
      members.add(NormalizedWorkspaceMember(requiredPath.clone(), zc::mv(member)));
      continue;
    }
    ZC_UNREACHABLE
  }

  return NormalizedWorkspace(zc::mv(root), zc::mv(members));
}

}  // namespace zomlang::compiler::driver::package
