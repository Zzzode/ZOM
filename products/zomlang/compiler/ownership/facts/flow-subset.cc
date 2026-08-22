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

#include "zomlang/compiler/ownership/facts/flow-subset.h"

namespace zomlang::compiler::ownership::facts {
namespace {

zc::Maybe<size_t> blockIndex(const mir::MirFunction& function, mir::MirBlockId id) {
  for (size_t index = 0; index < function.blocks.size(); ++index) {
    if (function.blocks[index].id == id) return index;
  }
  return zc::none;
}

struct Frame final {
  size_t blockIndex;
  bool expanded;
};

/// \brief Pushes every successor block of one terminator in deterministic order.
///
/// SwitchInt arms are pushed in vector order, then the default target, matching the
/// edge-ordinal assignment used by the flow derivation.
void pushSuccessors(const mir::MirTerminator& terminator, const mir::MirFunction& function,
                    zc::Vector<Frame>& stack) {
  if (terminator.kind() == mir::MirTerminatorKind::Call) {
    ZC_IF_SOME(next, blockIndex(function, terminator.callValue().normalTarget)) {
      stack.add(Frame{next, false});
    }
    return;
  }
  if (terminator.kind() == mir::MirTerminatorKind::Goto) {
    ZC_IF_SOME(next, blockIndex(function, terminator.gotoValue().target)) {
      stack.add(Frame{next, false});
    }
    return;
  }
  if (terminator.kind() != mir::MirTerminatorKind::SwitchInt) return;
  const auto& switchInt = terminator.switchIntValue();
  for (const auto& arm : switchInt.arms) {
    ZC_IF_SOME(next, blockIndex(function, arm.target)) { stack.add(Frame{next, false}); }
  }
  ZC_IF_SOME(next, blockIndex(function, switchInt.defaultTarget)) {
    stack.add(Frame{next, false});
  }
}

}  // namespace

bool isAdmittedFlowSubset(const mir::MirFunction& function) {
  if (function.blocks.size() == 0) return false;

  // Every terminator must be Return, Unreachable, Call, Goto, or SwitchInt. Every
  // Call must have no unwind target. Every successor target must resolve to an
  // existing block.
  for (const auto& block : function.blocks) {
    const auto kind = block.terminator.kind();
    if (kind == mir::MirTerminatorKind::Return) continue;
    if (kind == mir::MirTerminatorKind::Unreachable) continue;
    if (kind == mir::MirTerminatorKind::Call) {
      const auto& call = block.terminator.callValue();
      if (call.unwindTarget != zc::none) return false;
      if (blockIndex(function, call.normalTarget) == zc::none) return false;
      continue;
    }
    if (kind == mir::MirTerminatorKind::Goto) {
      if (blockIndex(function, block.terminator.gotoValue().target) == zc::none) return false;
      continue;
    }
    if (kind != mir::MirTerminatorKind::SwitchInt) return false;
    const auto& switchInt = block.terminator.switchIntValue();
    for (const auto& arm : switchInt.arms) {
      if (blockIndex(function, arm.target) == zc::none) return false;
    }
    if (blockIndex(function, switchInt.defaultTarget) == zc::none) return false;
  }

  // The successor graph must be acyclic and cover every block. Joins are admitted:
  // a block reached through another path is expanded once by the flow derivation. A
  // back edge (a block reached while still on the current DFS path) rejects the
  // function until fixpoint iteration lands.
  zc::Vector<mir::MirBlockId> inProgress;
  zc::Vector<mir::MirBlockId> done;
  zc::Vector<Frame> stack;
  stack.add(Frame{0, false});
  size_t visitedCount = 0;
  while (stack.size() != 0) {
    auto frame = zc::mv(stack[stack.size() - 1]);
    stack.removeLast();
    if (frame.expanded) {
      const auto id = function.blocks[frame.blockIndex].id;
      for (size_t index = 0; index < inProgress.size(); ++index) {
        if (inProgress[index] == id) {
          inProgress[index] = inProgress[inProgress.size() - 1];
          inProgress.removeLast();
          break;
        }
      }
      done.add(id);
      continue;
    }
    const auto& block = function.blocks[frame.blockIndex];
    bool alreadyDone = false;
    for (const auto previous : done) {
      if (previous == block.id) {
        alreadyDone = true;
        break;
      }
    }
    if (alreadyDone) continue;
    for (const auto previous : inProgress) {
      if (previous == block.id) return false;
    }
    inProgress.add(block.id);
    ++visitedCount;
    stack.add(Frame{frame.blockIndex, true});
    pushSuccessors(block.terminator, function, stack);
  }
  return visitedCount == function.blocks.size();
}

}  // namespace zomlang::compiler::ownership::facts
