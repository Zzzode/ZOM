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

// Cover the session-agnostic package-input installer extracted from
// CompilerSession::installVerifiedPackageInput. The installer runs
// VerifiedCrateGraph::buildFinal over the verified request, resolution, and
// build-script plan and bundles the result; it commits nothing and owns no
// session state. The verified inputs are built through the shared checker
// authority fixture, the same minimal in-memory chain nine checker tests rely
// on, so the test exercises real verified objects rather than mocks.

#include "compiler/driver/package/package-input-installer.h"

#include "compiler/driver/graph/crate-graph.h"
#include "tests/unittests/compiler/checker/checker-authority-test-fixture.h"
#include "zc/core/memory.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

namespace fixture = ::zomlang::compiler::tests::checker_fixture;

// Derives the build-script plan the same way VerifiedPackageSessionInput::from
// does, so the installer receives exactly the plan a session would hand it.
VerifiedBuildScriptPlan buildPlanFor(const VerifiedPackageCompilationRequest& request,
                                     const ResolutionOutput& graph) {
  auto plan = VerifiedPreparatoryCrateGraph::buildPlan(request, graph);
  ZC_REQUIRE(plan.is<VerifiedBuildScriptPlan>());
  return zc::mv(plan.get<VerifiedBuildScriptPlan>());
}

ZC_TEST("buildInstalledPackageInputs expands the final crate graph and bundles the inputs") {
  auto registry = fixture::targetRegistry();
  zc::MemoryResource resource;
  auto request = fixture::compilationRequest(registry);
  auto graph = fixture::resolution(resource, "fn main() {}\n"_zc);
  auto buildScriptPlan = buildPlanFor(request, graph);

  auto installed = buildInstalledPackageInputs(
      zc::mv(request), fixture::verifiedTargetSelection(registry),
      fixture::verifiedTargetSelection(registry), zc::mv(graph), zc::mv(buildScriptPlan),
      fixture::resolvedSnapshots("fn main() {}\n"_zc));

  ZC_REQUIRE(installed.is<InstalledPackageInputs>());
  auto& bundle = installed.get<InstalledPackageInputs>();
  // The expanded crate graph carries exactly the one requested binary root.
  ZC_EXPECT(bundle.crateGraph.roots().size() == 1);
  ZC_EXPECT(bundle.snapshots.size() == 1);
}

}  // namespace
}  // namespace zomlang::compiler::driver::package
