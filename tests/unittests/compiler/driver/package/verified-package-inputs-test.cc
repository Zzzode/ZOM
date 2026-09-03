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

// Cover verifyAndBuildPackageInputs, the single session-agnostic authority for
// admitting a verified package input. The happy path returns an installed inputs
// bundle; a verification failure is reported as a typed VerifyFailure arm so both
// the CLI and the IDE service can locate the exact rejected check. The verified
// inputs are built through the shared checker authority fixture; the target and
// registry mismatch arms are additionally exercised by the driver session package
// suite, which drives them through this same authority via
// VerifiedPackageSessionInput::from.

#include "compiler/driver/package/verified-package-inputs.h"

#include "compiler/driver/package/source-snapshot.h"
#include "tests/unittests/compiler/checker/checker-authority-test-fixture.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

namespace fixture = ::zomlang::compiler::tests::checker_fixture;
constexpr zc::StringPtr kSource = "fn main() {}\n"_zc;

// A resolved snapshot whose package base names a different package than the one
// the resolution output covers, so the graph and snapshots cannot match.
zc::Vector<ResolvedPackageSourceSnapshot> mismatchedSnapshots() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto base = identity::PackageBaseKey::from(
      identity::CanonicalPackageSource::localPath(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments))),
      fixture::scalar<identity::PackageName>("other"_zc),
      fixture::scalar<identity::ResolvedVersion>("1.0.0"_zc));
  zc::Vector<ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(
      ResolvedPackageSourceSnapshot::from(zc::mv(base), fixture::sourceSnapshot(kSource)));
  return snapshots;
}

ZC_TEST("verifyAndBuildPackageInputs returns the installed inputs for a valid request") {
  auto registry = fixture::targetRegistry();
  zc::MemoryResource resource;
  auto result = verifyAndBuildPackageInputs(
      fixture::compilationRequest(registry), fixture::verifiedTargetSelection(registry),
      fixture::verifiedTargetSelection(registry), fixture::resolution(resource, kSource),
      fixture::resolvedSnapshots(kSource));
  ZC_REQUIRE(result.is<InstalledPackageInputs>());
  ZC_EXPECT(result.get<InstalledPackageInputs>().crateGraph.roots().size() == 1);
}

ZC_TEST("verifyAndBuildPackageInputs reports a typed mismatch when snapshots leave the graph") {
  auto registry = fixture::targetRegistry();
  zc::MemoryResource resource;
  auto result = verifyAndBuildPackageInputs(
      fixture::compilationRequest(registry), fixture::verifiedTargetSelection(registry),
      fixture::verifiedTargetSelection(registry), fixture::resolution(resource, kSource),
      mismatchedSnapshots());
  ZC_REQUIRE(result.is<VerifyFailure>());
  ZC_EXPECT(result.get<VerifyFailure>().is<GraphSnapshotMismatch>());
}

}  // namespace
}  // namespace zomlang::compiler::driver::package
