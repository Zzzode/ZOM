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

#include "zc/ztest/test.h"
#include "zomlang/compiler/driver/interface/borrow-evidence.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/driver/session/compiler-session.h"
#include "zomlang/compiler/identity/source-snapshot.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/mir/built-mir.h"
#include "zomlang/compiler/ownership/facts/flow-subset.h"
#include "zomlang/compiler/ownership/facts/flow.h"
#include "zomlang/compiler/ownership/facts/init.h"
#include "zomlang/compiler/ownership/facts/inputs.h"
#include "zomlang/compiler/ownership/facts/region-membership.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"
#include "zomlang/tests/unittests/compiler/driver/core/core-library-test-fixture.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"
#include "zomlang/tests/unittests/compiler/test-semantic-type-context.h"

namespace zomlang::compiler::ownership {
namespace {

namespace mir = zomlang::compiler::mir;
namespace identity = zomlang::compiler::identity;
namespace checker = zomlang::compiler::checker;
namespace driver = zomlang::compiler::driver;
namespace package = driver::package;
namespace facts = zomlang::compiler::ownership::facts;

// ---------------------------------------------------------------------------
// Hand-built MIR helpers (mirror ownership-flow-subset-test.cc)
// ---------------------------------------------------------------------------

identity::SourceSpan testSpan() {
  auto snapshot = identity::ImmutableSourceSnapshot::from(tests::test_identity_detail::source(),
                                                          zc::heapArray<uint8_t>(8, uint8_t{0}));
  ZC_REQUIRE(snapshot != zc::none);
  ZC_IF_SOME(value, snapshot) {
    auto span = value.span(1, 7);
    ZC_IF_SOME(admitted, span) { return zc::mv(admitted); }
  }
  ZC_FAIL_REQUIRE("invalid loop-flow test source span");
}

mir::MirBlockId blockId(uint32_t ordinal) {
  auto result = mir::MirBlockId::fromOrdinal(ordinal);
  ZC_REQUIRE(result != zc::none);
  return ZC_REQUIRE_NONNULL(result);
}

mir::MirTerminator returnTerminator() { return mir::MirTerminator::returnVoid(testSpan()); }

mir::MirTerminator gotoTerminator(mir::MirBlockId target) {
  return mir::MirTerminator::gotoTarget(target, testSpan());
}

mir::MirTerminator switchIntTerminator(zc::Vector<mir::MirSwitchIntArm>&& arms,
                                       mir::MirBlockId defaultTarget) {
  const auto type = tests::testSemanticType();
  auto discriminant =
      mir::MirOperand::constant(type, checker::checked::CanonicalConstValue::boolean(true));
  return mir::MirTerminator::switchInt(zc::mv(discriminant), zc::mv(arms), defaultTarget,
                                       testSpan());
}

mir::MirBasicBlock makeBlock(mir::MirBlockId id, mir::MirTerminator&& terminator) {
  return mir::MirBasicBlock{id, mir::MirSourceScopeId{}, zc::Vector<mir::MirStatement>{},
                            zc::mv(terminator)};
}

mir::MirFunction makeFunction(zc::Vector<mir::MirBasicBlock>&& blocks) {
  return mir::MirFunction{tests::testDefinition(0),
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          tests::testSemanticType(),
                          testSpan(),
                          zc::Vector<mir::MirSourceScope>{},
                          zc::Vector<mir::MirLocalDeclaration>{},
                          zc::mv(blocks)};
}

// ---------------------------------------------------------------------------
// Reducible loops are admitted
// ---------------------------------------------------------------------------

ZC_TEST("Flow subset admits a simple SwitchInt loop with an exit") {
  // bb1 -> bb2 (header); bb2 switches to bb3 (body) or bb4 (exit); bb3 -> bb2.
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(3)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), gotoTerminator(blockId(2))));
  blocks.add(makeBlock(blockId(2), switchIntTerminator(zc::mv(arms), blockId(4))));
  blocks.add(makeBlock(blockId(3), gotoTerminator(blockId(2))));
  blocks.add(makeBlock(blockId(4), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(facts::isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset admits a SwitchInt self-loop") {
  // A node always dominates itself, so a self-loop is a reducible back edge.
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(1)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), switchIntTerminator(zc::mv(arms), blockId(2))));
  blocks.add(makeBlock(blockId(2), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(facts::isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset admits nested loops") {
  // bb1 -> bb2 (outer header); bb2 switches to bb3 (inner header) or bb5 (outer
  // exit); bb3 switches to bb3 (inner self-loop) or bb4; bb4 -> bb2 (outer
  // back edge); bb5 returns.
  zc::Vector<mir::MirSwitchIntArm> outerArms;
  outerArms.add(
      mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(3)});
  zc::Vector<mir::MirSwitchIntArm> innerArms;
  innerArms.add(
      mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(3)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), gotoTerminator(blockId(2))));
  blocks.add(makeBlock(blockId(2), switchIntTerminator(zc::mv(outerArms), blockId(5))));
  blocks.add(makeBlock(blockId(3), switchIntTerminator(zc::mv(innerArms), blockId(4))));
  blocks.add(makeBlock(blockId(4), gotoTerminator(blockId(2))));
  blocks.add(makeBlock(blockId(5), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(facts::isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset admits a loop whose header is a diamond join") {
  // bb1 switches to bb2 or bb3; both join at bb4; bb4 switches to bb4 (self
  // loop) or bb5 (exit); bb5 returns. The join feeds the loop header.
  zc::Vector<mir::MirSwitchIntArm> entryArms;
  entryArms.add(
      mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  zc::Vector<mir::MirSwitchIntArm> loopArms;
  loopArms.add(
      mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(4)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), switchIntTerminator(zc::mv(entryArms), blockId(3))));
  blocks.add(makeBlock(blockId(2), gotoTerminator(blockId(4))));
  blocks.add(makeBlock(blockId(3), gotoTerminator(blockId(4))));
  blocks.add(makeBlock(blockId(4), switchIntTerminator(zc::mv(loopArms), blockId(5))));
  blocks.add(makeBlock(blockId(5), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(facts::isAdmittedFlowSubset(function));
}

// ---------------------------------------------------------------------------
// Irreducible control flow is still rejected
// ---------------------------------------------------------------------------

ZC_TEST("Flow subset rejects an irreducible loop with two entries") {
  // bb1 switches to bb2 (arm) or bb3 (default); bb2 -> bb3; bb3 switches to
  // bb2 (back to the loop) or bb4 (exit); bb4 returns. The loop {bb2, bb3}
  // has two entry blocks (both reachable directly from bb1), so the
  // retreating edge bb3 -> bb2 does not dominate and the CFG is irreducible.
  zc::Vector<mir::MirSwitchIntArm> entryArms;
  entryArms.add(
      mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  zc::Vector<mir::MirSwitchIntArm> loopArms;
  loopArms.add(
      mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), switchIntTerminator(zc::mv(entryArms), blockId(3))));
  blocks.add(makeBlock(blockId(2), gotoTerminator(blockId(3))));
  blocks.add(makeBlock(blockId(3), switchIntTerminator(zc::mv(loopArms), blockId(4))));
  blocks.add(makeBlock(blockId(4), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(!facts::isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset rejects an irreducible loop entered from a branch body") {
  // bb1 -> bb2; bb2 switches to bb3 (arm) or bb4 (default); bb3 -> bb4;
  // bb4 -> bb3. The DFS reaches bb4 before bb3 on the arm path, so bb4 -> bb3
  // is the retreating edge; bb4 does not dominate bb3 (bb3 is reachable from
  // bb2 without bb4), so the CFG is irreducible.
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(3)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), gotoTerminator(blockId(2))));
  blocks.add(makeBlock(blockId(2), switchIntTerminator(zc::mv(arms), blockId(4))));
  blocks.add(makeBlock(blockId(3), gotoTerminator(blockId(4))));
  blocks.add(makeBlock(blockId(4), gotoTerminator(blockId(3))));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(!facts::isAdmittedFlowSubset(function));
}

// ---------------------------------------------------------------------------
// Coverage and target-resolution checks still apply to loops
// ---------------------------------------------------------------------------

ZC_TEST("Flow subset rejects a loop with an unreachable block") {
  // bb1 -> bb2; bb2 self-loops (arm) or exits to bb3 (default); bb3 returns.
  // bb4 returns but is never reached. The loop is reducible, yet the coverage
  // check fails because bb4 is unreachable.
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), gotoTerminator(blockId(2))));
  blocks.add(makeBlock(blockId(2), switchIntTerminator(zc::mv(arms), blockId(3))));
  blocks.add(makeBlock(blockId(3), returnTerminator()));
  blocks.add(makeBlock(blockId(4), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(!facts::isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset rejects a loop exit dangling to a nonexistent block") {
  // bb1 switches to bb1 (self-loop) or bb99 (dangling exit). The
  // target-resolution check rejects the nonexistent exit block.
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(1)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), switchIntTerminator(zc::mv(arms), blockId(99))));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(!facts::isAdmittedFlowSubset(function));
}

// ---------------------------------------------------------------------------
// Session pipeline fixture (for the builder/verifier regression on the most
// complex producible CFG shape). The frontend does not yet lower loops to
// MIR, so end-to-end loop flow graphs are not producible here; the gate
// tests above are the enforcement point for loop admission and irreducible
// rejection. The conditional-return diamond below is the closest producible
// shape and exercises the same DFS fan-out path.
// ---------------------------------------------------------------------------

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid loop-flow fixture scalar");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid loop-flow fixture feature set");
}

identity::CanonicalPackageSource packageSource() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  return identity::CanonicalPackageSource::localPath(
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments)));
}

identity::PackageBaseKey packageBase() {
  return identity::PackageBaseKey::from(packageSource(), scalar<identity::PackageName>("app"_zc),
                                        scalar<identity::ResolvedVersion>("1.0.0"_zc));
}

identity::PackageKey packageKey() {
  return identity::PackageKey::from(packageSource(), scalar<identity::PackageName>("app"_zc),
                                    scalar<identity::ResolvedVersion>("1.0.0"_zc), emptyFeatures());
}

identity::CanonicalRelativePath mainPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>("src"_zc));
  segments.add(scalar<identity::CanonicalPathSegment>("main.zom"_zc));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

identity::CanonicalTargetSpecificationKey targetProjection() {
  zc::Vector<identity::TargetFeatureName> features;
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(values, sorted) {
    auto result = identity::CanonicalTargetSpecificationKey::from(
        scalar<identity::TargetComponentName>("x86_64"_zc),
        scalar<identity::TargetComponentName>("zom"_zc),
        scalar<identity::TargetComponentName>("none"_zc),
        scalar<identity::TargetComponentName>("unknown"_zc),
        scalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
        zc::mv(values));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid loop-flow fixture target projection");
}

package::RegisteredTargetProfileName targetProfileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid loop-flow fixture target profile name");
}

ir::TargetRegistrySnapshot targetRegistry() {
  zc::Vector<ir::CanonicalTargetFeature> targetFeatures;
  auto targetSpec = ir::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(targetFeatures), "zom"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(targetSpec != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  ZC_IF_SOME(value, targetSpec) { specifications.add(zc::mv(value)); }
  auto profile = ir::RegisteredTargetProfileRecord::from(
      targetProfileName(), targetProjection(), zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  ZC_IF_SOME(value, profile) { profiles.add(zc::mv(value)); }
  auto registry = ir::TargetRegistrySnapshot::from(targetProfileName(), zc::mv(profiles));
  ZC_IF_SOME(value, registry) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid loop-flow fixture target registry");
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto result = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid loop-flow fixture target selection");
}

ir::VerifiedTargetSelection verifiedTargetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto result = registry.verify(targetSelection(registry));
  ZC_REQUIRE(result.is<ir::VerifiedTargetSelection>());
  return zc::mv(result.get<ir::VerifiedTargetSelection>());
}

package::VerifiedPackageCompilationRequest compilationRequest(
    const ir::TargetRegistrySnapshot& registry) {
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(packageKey(), identity::CrateTargetKind::Binary,
                                                   scalar<identity::TargetName>("app"_zc), 2026,
                                                   false, mainPath()));
  auto result = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), targetSelection(registry), targetSelection(registry),
      package::SelectedLanguageOptions{}, package::PackageLockMode::PreferLocked);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid loop-flow fixture compilation request");
}

class MemoryFreshDirectory final : public package::FreshSourceDirectory {
public:
  MemoryFreshDirectory() : rootValue(zc::newInMemoryDirectory(zc::nullClock())) {}
  ~MemoryFreshDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<package::MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<zc::Directory> rootValue;
};

class MemoryFreshDirectoryFactory final : public package::FreshSourceDirectoryFactory {
public:
  package::FreshSourceDirectoryResult create() override {
    zc::Own<package::FreshSourceDirectory> result = zc::heap<MemoryFreshDirectory>();
    return zc::mv(result);
  }
};

package::DigestVerifiedSourceSnapshot sourceSnapshot(zc::StringPtr sourceText) {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(sourceText);
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::ResolutionOutput resolution(zc::MemoryResource& resource, zc::StringPtr sourceText) {
  package::ManifestParser parser;
  zc::Vector<identity::CanonicalRelativePath> files;
  auto inventory = package::PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  package::NormalizedManifest normalized = [&]() {
    ZC_IF_SOME(sourceInventory, inventory) {
      zc::Vector<identity::CanonicalPathSegment> documentSegments;
      documentSegments.add(scalar<identity::CanonicalPathSegment>("Zom.toml"_zc));
      auto parsed = parser.parseWorkspaceManifest(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(documentSegments)),
          "[package]\nname = \"app\"\nversion = \"1.0.0\"\nedition = \"2026\"\n"_zc,
          sourceInventory);
      ZC_REQUIRE(parsed.is<package::NormalizedManifest>());
      return zc::mv(parsed.get<package::NormalizedManifest>());
    }
    ZC_UNREACHABLE
  }();
  auto record = package::LocalPackageRecord::from(packageBase(), zc::mv(normalized),
                                                  sourceSnapshot(sourceText));
  ZC_REQUIRE(record != zc::none);
  zc::Vector<package::ResolverRelease> releases;
  ZC_IF_SOME(value, record) { releases.add(package::ResolverRelease::fromLocal(value)); }
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(packageBase(), emptyFeatures(), false, false));
  auto result = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(result.is<package::ResolutionOutput>());
  return zc::mv(result.get<package::ResolutionOutput>());
}

zc::Vector<package::ResolvedPackageSourceSnapshot> resolvedSnapshots(zc::StringPtr sourceText) {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(
      package::ResolvedPackageSourceSnapshot::from(packageBase(), sourceSnapshot(sourceText)));
  return snapshots;
}

/// Fixture that runs the full session pipeline and requires success.
class LoopPipelineFixture final {
public:
  explicit LoopPipelineFixture(zc::StringPtr sourceText)
      : session(contextFactory, languageOptions, compilerOptions) {
    auto registry = targetRegistry();
    auto input = driver::VerifiedPackageSessionInput::from(
        compilationRequest(registry), verifiedTargetSelection(registry),
        verifiedTargetSelection(registry),
        resolution(session.getPackageResolutionMemoryResource(), sourceText),
        resolvedSnapshots(sourceText));
    ZC_REQUIRE(input != zc::none);
    ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
    driver::core_library_test::installCoreDistribution(session);
    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_REQUIRE(session.checkSources());
    ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
    ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  }

  const mir::VerifiedBuiltMir& builtMir() const {
    return session.getOwnershipCheckedMirModules()[0].builtMir();
  }

  const VerifiedOwnershipEventOverlay& overlay() const {
    return session.getOwnershipCheckedMirModules()[0].eventOverlay();
  }

  const facts::VerifiedOwnershipInputs& inputs() const {
    return session.getOwnershipCheckedMirModules()[0].facts();
  }

private:
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
};

/// Returns true when the flow graph carries the CFG edge point (from, ordinal, to).
bool flowHasEdgePoint(const facts::FlowFunction& flow, mir::MirBlockId from, uint32_t ordinal,
                      mir::MirBlockId to) {
  const auto edgePoint = facts::OwnershipPoint::cfg(MirPoint::edge(from, ordinal, to));
  for (const auto& point : flow.points) {
    if (point == edgePoint) return true;
  }
  return false;
}

// The conditional-return source lowers to a four-block Goto-joined SwitchInt
// diamond: bb1 switches to bb2 (true arm) or bb3 (false arm / default); each
// branch initializes the result local and jumps to the bb4 join, which
// performs the single return. The flow builder must emit the switch edge
// points and both branch->join edge points, and the independent verifier must
// reconstruct the same graph. This is the acyclic fan-out/fan-in path shared
// with loop back-edge handling.

ZC_TEST("Flow builder derives the edge points of a conditional-return diamond") {
  LoopPipelineFixture fixture(
      "fun choose(cond: bool) -> i32 { if (cond) { return 1; } else { return 2; } }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = fixture.overlay();
  ZC_REQUIRE(builtMir.functions().size() == 1);

  auto candidateResult = facts::FlowBuilder::build(builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  const auto& flow = candidate.functions[0];

  // The entry block switches on the condition parameter: true arm (ordinal 0)
  // to bb2, false arm (ordinal 1) and default (ordinal 2) to bb3.
  ZC_EXPECT(flowHasEdgePoint(flow, blockId(1), 0, blockId(2)));
  ZC_EXPECT(flowHasEdgePoint(flow, blockId(1), 1, blockId(3)));
  ZC_EXPECT(flowHasEdgePoint(flow, blockId(1), 2, blockId(3)));
  // Both branches Goto the join block bb4.
  ZC_EXPECT(flowHasEdgePoint(flow, blockId(2), 0, blockId(4)));
  ZC_EXPECT(flowHasEdgePoint(flow, blockId(3), 0, blockId(4)));

  auto verifiedResult = facts::FlowVerifier::verify(zc::mv(candidate), builtMir, overlay);
  ZC_EXPECT(verifiedResult.isVerified());
}

// The region-membership worklist fixpoint must converge on the conditional
// diamond exactly as the former topological pass did. The condition parameter
// seeds an Input region that propagates through both branches, so the
// converged inventory is non-empty; the assertion is that the fixpoint
// terminates on a multi-block SwitchInt function and the independent
// verifier reconstructs the same memberships.

ZC_TEST("Region membership fixpoint converges on a conditional diamond") {
  LoopPipelineFixture fixture(
      "fun choose(cond: bool) -> i32 { if (cond) { return 1; } else { return 2; } }"_zc);

  auto candidateResult = facts::RegionMembershipBuilder::build(
      fixture.inputs().flow(), fixture.inputs().loans(), fixture.builtMir(), fixture.overlay());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_EXPECT(candidate.memberships.size() != 0);

  auto verifiedResult = facts::RegionMembershipVerifier::verify(
      zc::mv(candidate), fixture.inputs().flow(), fixture.inputs().loans(), fixture.builtMir(),
      fixture.overlay());
  ZC_EXPECT(verifiedResult.isVerified());
}

}  // namespace

// ---------------------------------------------------------------------------
// Initialization fixpoint over reducible loops (hand-built MIR + facts, no
// session).
// ---------------------------------------------------------------------------
//
// These drive the InitializationBuilder::deriveFunctionForTesting seam
// directly. The frontend does not yet lower loops to MIR, so a VerifiedBuiltMir
// carrying a back edge is not producible through the pipeline; the monotone
// worklist fixpoint that converges initialization state on reducible loops is
// therefore exercised on hand-built functions, flow graphs, and move paths.
// The former single topological pass fail-closed on any back edge (its
// reachable-order helper returned none), even though the flow subset admits
// reducible loops. The scope here is the per-function InitializationFunction
// fact inventory only.

namespace {

mir::MirLocalDeclaration makeLocal(uint32_t ordinal, mir::MirLocalKind kind) {
  return mir::MirLocalDeclaration{ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(ordinal)), kind,
                                  identity::SemanticTypeId(), mir::MirSourceScopeId{}, testSpan()};
}

mir::MirFunction makeFunctionWithLocals(identity::DefId owner,
                                        zc::Vector<mir::MirLocalDeclaration>&& locals,
                                        zc::Vector<mir::MirBasicBlock>&& blocks) {
  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          identity::SemanticTypeId(),
                          testSpan(),
                          zc::Vector<mir::MirSourceScope>{},
                          zc::mv(locals),
                          zc::mv(blocks)};
}

mir::MirBasicBlock makeStmtBlock(mir::MirBlockId id, zc::Vector<mir::MirStatement>&& statements,
                                 mir::MirTerminator&& terminator) {
  return mir::MirBasicBlock{id, mir::MirSourceScopeId{}, zc::mv(statements), zc::mv(terminator)};
}

mir::MirPlace localPlace(uint32_t local) {
  return mir::MirPlace(ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(local)),
                       identity::SemanticTypeId(), zc::Vector<mir::MirProjection>{},
                       identity::SemanticTypeId());
}

mir::MirStatement storageLive(uint32_t local) {
  return mir::MirStatement::storageLive(ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(local)),
                                        testSpan());
}

mir::MirStatement assignConstant(uint32_t local, mir::MirInitializationKind kind) {
  auto value = mir::MirRvalue::use(mir::MirOperand::constant(
      identity::SemanticTypeId(), checker::checked::CanonicalConstValue::boolean(true)));
  return mir::MirStatement::assign(localPlace(local), zc::mv(value), kind, testSpan());
}

mir::MirStatement deinitialize(uint32_t local) {
  return mir::MirStatement::deinitialize(localPlace(local), testSpan());
}

facts::MovePathFunction makeMovePaths(identity::DefId owner, zc::ArrayPtr<const uint32_t> locals) {
  facts::MovePathFunction paths;
  paths.owner = owner;
  for (const auto local : locals) {
    paths.facts.add(facts::MovePathFact{facts::MovePathKey{owner, localPlace(local)}, zc::none});
  }
  return paths;
}

struct CfgEdge final {
  uint32_t from;
  uint32_t ordinal;
  uint32_t to;
};

facts::FlowFunction makeFlow(identity::DefId owner, zc::ArrayPtr<const CfgEdge> edges) {
  facts::FlowFunction flow;
  flow.owner = owner;
  for (const auto& edge : edges) {
    flow.points.add(facts::OwnershipPoint::cfg(
        MirPoint::edge(blockId(edge.from), edge.ordinal, blockId(edge.to))));
  }
  return flow;
}

zc::Maybe<facts::InitializationState> stateAt(const facts::InitializationFunction& function,
                                              const MirPoint& point, uint32_t local) {
  for (const auto& fact : function.facts) {
    if (fact.point == point && fact.key.place.local().ordinal() == local) return fact.state;
  }
  return zc::none;
}

}  // namespace

// A parameter is live and initialized on entry and never mutated, so it stays
// initialized around a reducible while-style loop. The point of the case is
// that deriveFunction now RETURNS a value on a CFG with a back edge (bb3 -> bb2)
// where the former topological pass fail-closed, and that the converged exit
// fact for the parameter is fully initialized.
ZC_TEST("Initialization fixpoint converges on a reducible SwitchInt loop") {
  const auto owner = tests::testDefinition(0);
  // bb1 -> bb2 (header); bb2 switches to bb3 (body) or bb4 (exit); bb3 -> bb2.
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(3)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), gotoTerminator(blockId(2))));
  blocks.add(makeBlock(blockId(2), switchIntTerminator(zc::mv(arms), blockId(4))));
  blocks.add(makeBlock(blockId(3), gotoTerminator(blockId(2))));
  blocks.add(makeBlock(blockId(4), returnTerminator()));

  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(makeLocal(1, mir::MirLocalKind::Parameter));
  auto function = makeFunctionWithLocals(owner, zc::mv(locals), zc::mv(blocks));

  zc::Vector<uint32_t> pathLocals;
  pathLocals.add(1);
  auto paths = makeMovePaths(owner, pathLocals.asPtr());

  zc::Vector<CfgEdge> edges;
  edges.add(CfgEdge{1, 0, 2});
  edges.add(CfgEdge{2, 0, 3});
  edges.add(CfgEdge{2, 1, 4});
  edges.add(CfgEdge{3, 0, 2});
  auto flow = makeFlow(owner, edges.asPtr());

  auto derived = facts::InitializationBuilder::deriveFunctionForTesting(function, flow, paths);
  ZC_REQUIRE(derived != zc::none);
  ZC_IF_SOME(result, derived) {
    ZC_EXPECT(result.facts.size() != 0);
    auto exit = stateAt(result, MirPoint::exit(blockId(4), MirExitKind::Return), 1);
    ZC_REQUIRE(exit != zc::none);
    ZC_IF_SOME(state, exit) { ZC_EXPECT(state == facts::InitializationState::initialized()); }
  }
}

// A local declared live in the preheader and initialized only on one arm of an
// acyclic SwitchInt diamond is fully initialized at the assigning block's exit
// but only maybe-initialized at the join. The two-phase emission reproduces the
// classic forward-dataflow result exactly on an acyclic function, so this pins
// the per-block transfer against regressions from the fixpoint restructuring.
ZC_TEST("Initialization emission is exact on an acyclic diamond") {
  const auto owner = tests::testDefinition(0);
  // bb1 switches to bb2 (init arm) or bb3 (bare arm); both Goto bb4; bb4 returns.
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  zc::Vector<mir::MirStatement> entryStatements;
  entryStatements.add(storageLive(1));
  zc::Vector<mir::MirStatement> initStatements;
  initStatements.add(assignConstant(1, mir::MirInitializationKind::Initialize));
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeStmtBlock(blockId(1), zc::mv(entryStatements),
                           switchIntTerminator(zc::mv(arms), blockId(3))));
  blocks.add(makeStmtBlock(blockId(2), zc::mv(initStatements), gotoTerminator(blockId(4))));
  blocks.add(makeBlock(blockId(3), gotoTerminator(blockId(4))));
  blocks.add(makeBlock(blockId(4), returnTerminator()));

  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(makeLocal(1, mir::MirLocalKind::UserLocal));
  auto function = makeFunctionWithLocals(owner, zc::mv(locals), zc::mv(blocks));

  zc::Vector<uint32_t> pathLocals;
  pathLocals.add(1);
  auto paths = makeMovePaths(owner, pathLocals.asPtr());

  zc::Vector<CfgEdge> edges;
  edges.add(CfgEdge{1, 0, 2});
  edges.add(CfgEdge{1, 1, 3});
  edges.add(CfgEdge{2, 0, 4});
  edges.add(CfgEdge{3, 0, 4});
  auto flow = makeFlow(owner, edges.asPtr());

  auto derived = facts::InitializationBuilder::deriveFunctionForTesting(function, flow, paths);
  ZC_REQUIRE(derived != zc::none);
  ZC_IF_SOME(result, derived) {
    // At the assigning block's exit the local must be fully initialized.
    auto initialized = stateAt(result, MirPoint::afterStatement(blockId(2), 0), 1);
    ZC_REQUIRE(initialized != zc::none);
    ZC_IF_SOME(state, initialized) {
      ZC_EXPECT(state == facts::InitializationState::initialized());
    }
    // At the return exit the join weakens must-initialized: the bare arm never
    // assigned the local, so it is maybe-initialized but not must-initialized.
    auto joined = stateAt(result, MirPoint::exit(blockId(4), MirExitKind::Return), 1);
    ZC_REQUIRE(joined != zc::none);
    ZC_IF_SOME(state, joined) {
      ZC_EXPECT(state.mayBeInitialized);
      ZC_EXPECT(!state.mustBeInitialized);
    }
  }
}

// A local initialized in the preheader but deinitialized inside the loop body
// is only maybe-initialized at the loop exit: the fixpoint must fold the body's
// back edge into the header, weakening must-initialized. A single topological
// pass could not reach this fact because it rejected the back edge outright.
ZC_TEST("Initialization fixpoint reports maybe-initialized after a loop body deinitialize") {
  const auto owner = tests::testDefinition(0);
  // bb1 (preheader): StorageLive; Assign init; -> bb2. bb2 (header): switch to
  // bb3 (body) or bb4 (exit). bb3 (body): Deinitialize; -> bb2. bb4: return.
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(3)});
  zc::Vector<mir::MirStatement> preheaderStatements;
  preheaderStatements.add(storageLive(1));
  preheaderStatements.add(assignConstant(1, mir::MirInitializationKind::Initialize));
  zc::Vector<mir::MirStatement> bodyStatements;
  bodyStatements.add(deinitialize(1));
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeStmtBlock(blockId(1), zc::mv(preheaderStatements), gotoTerminator(blockId(2))));
  blocks.add(makeBlock(blockId(2), switchIntTerminator(zc::mv(arms), blockId(4))));
  blocks.add(makeStmtBlock(blockId(3), zc::mv(bodyStatements), gotoTerminator(blockId(2))));
  blocks.add(makeBlock(blockId(4), returnTerminator()));

  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(makeLocal(1, mir::MirLocalKind::UserLocal));
  auto function = makeFunctionWithLocals(owner, zc::mv(locals), zc::mv(blocks));

  zc::Vector<uint32_t> pathLocals;
  pathLocals.add(1);
  auto paths = makeMovePaths(owner, pathLocals.asPtr());

  zc::Vector<CfgEdge> edges;
  edges.add(CfgEdge{1, 0, 2});
  edges.add(CfgEdge{2, 0, 3});
  edges.add(CfgEdge{2, 1, 4});
  edges.add(CfgEdge{3, 0, 2});
  auto flow = makeFlow(owner, edges.asPtr());

  auto derived = facts::InitializationBuilder::deriveFunctionForTesting(function, flow, paths);
  ZC_REQUIRE(derived != zc::none);
  ZC_IF_SOME(result, derived) {
    auto exit = stateAt(result, MirPoint::exit(blockId(4), MirExitKind::Return), 1);
    ZC_REQUIRE(exit != zc::none);
    ZC_IF_SOME(state, exit) {
      ZC_EXPECT(state.mayBeInitialized);
      ZC_EXPECT(!state.mustBeInitialized);
    }
  }
}

}  // namespace zomlang::compiler::ownership
