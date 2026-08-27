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

#include "zc/core/memory.h"
#include "zc/ztest/test.h"
#include "compiler/identity/source-snapshot.h"
#include "compiler/mir/built-mir.h"
#include "compiler/ownership/facts/flow-subset.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "tests/unittests/compiler/test-semantic-type-context.h"

namespace zomlang::compiler::ownership::facts {
namespace {

namespace mir = zomlang::compiler::mir;
namespace identity = zomlang::compiler::identity;
namespace checker = zomlang::compiler::checker;

identity::SourceSpan testSpan() {
  auto snapshot = identity::ImmutableSourceSnapshot::from(tests::test_identity_detail::source(),
                                                          zc::heapArray<uint8_t>(8, uint8_t{0}));
  ZC_REQUIRE(snapshot != zc::none);
  ZC_IF_SOME(value, snapshot) {
    auto span = value.span(1, 7);
    ZC_IF_SOME(admitted, span) { return zc::mv(admitted); }
  }
  ZC_FAIL_REQUIRE("invalid flow-subset test source span");
}

mir::MirBlockId blockId(uint32_t ordinal) {
  auto result = mir::MirBlockId::fromOrdinal(ordinal);
  ZC_REQUIRE(result != zc::none);
  return ZC_REQUIRE_NONNULL(result);
}

mir::MirTerminator returnTerminator() { return mir::MirTerminator::returnVoid(testSpan()); }

mir::MirTerminator unreachableTerminator() { return mir::MirTerminator::unreachable(testSpan()); }

mir::MirTerminator gotoTerminator(mir::MirBlockId target) {
  return mir::MirTerminator::gotoTarget(target, testSpan());
}

mir::MirTerminator callTerminator(mir::MirBlockId normalTarget,
                                  zc::Maybe<mir::MirBlockId> unwindTarget) {
  const auto type = tests::testSemanticType();
  const auto local = ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(1));
  zc::Vector<mir::MirProjection> projections;
  auto callee = tests::testDefinition(1);
  zc::Vector<mir::MirOperand> arguments;
  auto effect = mir::MirCallEffect::noActivation();
  auto destination = mir::MirPlace(local, type, zc::mv(projections), type);
  return mir::MirTerminator::call(callee, zc::mv(arguments), zc::mv(effect), zc::mv(destination),
                                  normalTarget, zc::mv(unwindTarget), testSpan());
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
// Admitted subset
// ---------------------------------------------------------------------------

ZC_TEST("Flow subset admits a single Return block") {
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset admits a single Unreachable block") {
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), unreachableTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset admits a two-block Call chain without unwind") {
  // This is the mutable-receiver activation shape emitted by the production MIR
  // builder: block 0 calls and continues to block 1, which returns.
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), callTerminator(blockId(2), zc::none)));
  blocks.add(makeBlock(blockId(2), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset admits an acyclic Goto chain") {
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), gotoTerminator(blockId(2))));
  blocks.add(makeBlock(blockId(2), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset admits an acyclic SwitchInt diamond with a join") {
  // Block 1 switches to block 2 (arm) or block 3 (default); both join at
  // block 4, which returns. The join is admitted: block 4 is expanded once
  // by the flow derivation and skipped on the second reach.
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), switchIntTerminator(zc::mv(arms), blockId(3))));
  blocks.add(makeBlock(blockId(2), gotoTerminator(blockId(4))));
  blocks.add(makeBlock(blockId(3), gotoTerminator(blockId(4))));
  blocks.add(makeBlock(blockId(4), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(isAdmittedFlowSubset(function));
}

// ---------------------------------------------------------------------------
// Rejected: empty topology
// ---------------------------------------------------------------------------

ZC_TEST("Flow subset rejects a function with no blocks") {
  zc::Vector<mir::MirBasicBlock> blocks;
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(!isAdmittedFlowSubset(function));
}

// ---------------------------------------------------------------------------
// Admitted: reducible loops
// ---------------------------------------------------------------------------

ZC_TEST("Flow subset admits a self-loop Goto") {
  // A node always dominates itself, so a self-loop is a reducible back edge.
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), gotoTerminator(blockId(1))));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset admits a two-block Goto loop") {
  // bb0 dominates bb1, so the retreating edge bb1 -> bb0 is a reducible back
  // edge and the loop is admitted.
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), gotoTerminator(blockId(2))));
  blocks.add(makeBlock(blockId(2), gotoTerminator(blockId(1))));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset admits a Call loop back to the entry block") {
  // The mutable-receiver activation shape with a loop: bb0 calls to bb1, bb1
  // calls back to bb0. bb0 dominates bb1, so the back edge is reducible.
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), callTerminator(blockId(2), zc::none)));
  blocks.add(makeBlock(blockId(2), callTerminator(blockId(1), zc::none)));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(isAdmittedFlowSubset(function));
}

// ---------------------------------------------------------------------------
// Rejected: unwind targets
// ---------------------------------------------------------------------------

ZC_TEST("Flow subset rejects a Call with an unwind target") {
  zc::Maybe<mir::MirBlockId> unwind = blockId(2);
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), callTerminator(blockId(2), zc::mv(unwind))));
  blocks.add(makeBlock(blockId(2), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(!isAdmittedFlowSubset(function));
}

// ---------------------------------------------------------------------------
// Rejected: unreachable blocks
// ---------------------------------------------------------------------------

ZC_TEST("Flow subset rejects an unreachable block after Return") {
  // Block 0 returns (no successors), so block 1 is never reached. The DFS
  // visits only one block and the coverage check fails.
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), returnTerminator()));
  blocks.add(makeBlock(blockId(2), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(!isAdmittedFlowSubset(function));
}

// ---------------------------------------------------------------------------
// Rejected: dangling successor targets
// ---------------------------------------------------------------------------

ZC_TEST("Flow subset rejects a Goto to a nonexistent block") {
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), gotoTerminator(blockId(99))));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(!isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset rejects a Call to a nonexistent normal target") {
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), callTerminator(blockId(99), zc::none)));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(!isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset rejects a SwitchInt arm targeting a nonexistent block") {
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(99)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), switchIntTerminator(zc::mv(arms), blockId(2))));
  blocks.add(makeBlock(blockId(2), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(!isAdmittedFlowSubset(function));
}

ZC_TEST("Flow subset rejects a SwitchInt default targeting a nonexistent block") {
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(makeBlock(blockId(1), switchIntTerminator(zc::mv(arms), blockId(99))));
  blocks.add(makeBlock(blockId(2), returnTerminator()));
  auto function = makeFunction(zc::mv(blocks));
  ZC_EXPECT(!isAdmittedFlowSubset(function));
}

}  // namespace
}  // namespace zomlang::compiler::ownership::facts
