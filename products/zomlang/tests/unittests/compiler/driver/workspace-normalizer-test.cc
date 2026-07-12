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

#include "zomlang/compiler/driver/package/workspace-normalizer.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

identity::CanonicalWorkspaceRelativePath directory(zc::StringPtr text) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  size_t start = 0;
  for (size_t index = 0; index <= text.size(); ++index) {
    if (index < text.size() && text[index] != '/') { continue; }
    const zc::String segmentText = zc::heapString(text.slice(start, index));
    auto segment = identity::CanonicalPathSegment::fromCanonical(segmentText);
    ZC_IF_SOME(admitted, segment) { segments.add(zc::mv(admitted)); }
    start = index + 1;
  }
  return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
}

PackageSourceInventory emptyInventory() {
  zc::Vector<identity::CanonicalRelativePath> files;
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_IF_SOME(admitted, inventory) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty source inventory was rejected");
}

WorkspaceMemberInput member(zc::StringPtr path, zc::StringPtr packageName) {
  return WorkspaceMemberInput::from(
      directory(path),
      zc::str("[package]\nname = \"", packageName, "\"\nversion = \"1.0.0\"\nedition = \"2026\"\n"),
      emptyInventory());
}

zc::Vector<WorkspaceMemberInput> orderedMembers(bool reverse) {
  zc::Vector<WorkspaceMemberInput> members;
  if (reverse) {
    members.add(member("packages/b"_zc, "bravo"_zc));
    members.add(member("packages/a"_zc, "alpha"_zc));
  } else {
    members.add(member("packages/a"_zc, "alpha"_zc));
    members.add(member("packages/b"_zc, "bravo"_zc));
  }
  return members;
}

constexpr zc::StringPtr kRoot = R"toml([workspace]
members = ["packages/b", "packages/a"]
)toml"_zc;

}  // namespace

ZC_TEST("WorkspaceNormalizer.ExpandsMembersInCanonicalOrder") {
  auto result = normalizeWorkspace(kRoot, emptyInventory(), orderedMembers(true));
  ZC_REQUIRE(result.is<NormalizedWorkspace>());
  const auto& workspace = result.get<NormalizedWorkspace>();
  ZC_REQUIRE(workspace.members().size() == 2);
  ZC_EXPECT(workspace.members()[0].packageDirectory().segments()[1].text() == "a"_zc);
  ZC_EXPECT(workspace.members()[0].manifest().packageName() == "alpha"_zc);
  ZC_EXPECT(workspace.members()[1].packageDirectory().segments()[1].text() == "b"_zc);
  ZC_EXPECT(workspace.members()[1].manifest().packageName() == "bravo"_zc);
}

ZC_TEST("WorkspaceNormalizer.IsIndependentOfAvailableInputOrder") {
  auto first = normalizeWorkspace(kRoot, emptyInventory(), orderedMembers(false));
  auto second = normalizeWorkspace(kRoot, emptyInventory(), orderedMembers(true));
  ZC_REQUIRE(first.is<NormalizedWorkspace>());
  ZC_REQUIRE(second.is<NormalizedWorkspace>());
  const auto& firstWorkspace = first.get<NormalizedWorkspace>();
  const auto& secondWorkspace = second.get<NormalizedWorkspace>();
  ZC_REQUIRE(firstWorkspace.members().size() == secondWorkspace.members().size());
  for (size_t index = 0; index < firstWorkspace.members().size(); ++index) {
    ZC_EXPECT(firstWorkspace.members()[index].packageDirectory().segments()[1].text() ==
              secondWorkspace.members()[index].packageDirectory().segments()[1].text());
    auto firstManifest = CanonicalManifestRecord::from(firstWorkspace.members()[index].manifest());
    auto secondManifest =
        CanonicalManifestRecord::from(secondWorkspace.members()[index].manifest());
    ZC_EXPECT(firstManifest.encode().asPtr() == secondManifest.encode().asPtr());
  }
}

ZC_TEST("WorkspaceNormalizer.RejectsMissingAndNestedMembers") {
  zc::Vector<WorkspaceMemberInput> missingMembers;
  missingMembers.add(member("packages/a"_zc, "alpha"_zc));
  auto missing = normalizeWorkspace(kRoot, emptyInventory(), zc::mv(missingMembers));
  ZC_REQUIRE(missing.is<ManifestFailure>());
  ZC_EXPECT(missing.get<ManifestFailure>().issue() == ManifestIssue::WorkspaceMemberMissing);
  ZC_EXPECT(missing.get<ManifestFailure>().provenance().related().size() == 0);

  zc::Vector<WorkspaceMemberInput> nestedMembers;
  nestedMembers.add(WorkspaceMemberInput::from(
      directory("packages/a"_zc), zc::heapString("[workspace]\nmembers = [\"child\"]\n"_zc),
      emptyInventory()));
  auto nested = normalizeWorkspace("[workspace]\nmembers = [\"packages/a\"]\n"_zc, emptyInventory(),
                                   zc::mv(nestedMembers));
  ZC_REQUIRE(nested.is<ManifestFailure>());
  ZC_EXPECT(nested.get<ManifestFailure>().issue() == ManifestIssue::NestedWorkspace);
  ZC_EXPECT(nested.get<ManifestFailure>().provenance().related().size() == 0);
}

ZC_TEST("WorkspaceNormalizer.RejectsDuplicatePackageNamesWithRelatedOrigin") {
  zc::Vector<WorkspaceMemberInput> members;
  members.add(member("packages/z"_zc, "shared"_zc));
  members.add(member("packages/a"_zc, "shared"_zc));
  auto result = normalizeWorkspace("[workspace]\nmembers = [\"packages/z\", \"packages/a\"]\n"_zc,
                                   emptyInventory(), zc::mv(members));
  ZC_REQUIRE(result.is<ManifestFailure>());
  const auto& error = result.get<ManifestFailure>();
  ZC_EXPECT(error.issue() == ManifestIssue::DuplicateWorkspacePackageName);
  ZC_EXPECT(error.provenance().primary().kind() == DiagnosticAnchorKind::Manifest);
  ZC_REQUIRE(error.provenance().related().size() == 1);
  ZC_EXPECT(error.provenance().related()[0].kind() == DiagnosticAnchorKind::Manifest);
}

ZC_TEST("WorkspaceNormalizer.RejectsDuplicateCandidatePaths") {
  zc::Vector<WorkspaceMemberInput> members;
  members.add(member("packages/a"_zc, "alpha"_zc));
  members.add(member("packages/a"_zc, "alpha"_zc));
  auto result = normalizeWorkspace("[workspace]\nmembers = [\"packages/a\"]\n"_zc, emptyInventory(),
                                   zc::mv(members));
  ZC_REQUIRE(result.is<ManifestFailure>());
  ZC_EXPECT(result.get<ManifestFailure>().issue() == ManifestIssue::DuplicateCanonicalValue);
}

}  // namespace zomlang::compiler::driver::package
