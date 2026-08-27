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

#include "compiler/ownership/facts/flow-subset.h"

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

  // Loops are admitted. A back edge (a successor still on the current DFS
  // path) is accepted when its target dominates its source, which is the
  // single-entry-loop characterization of a reducible CFG. Irreducible
  // control flow (a retreating edge whose target does not dominate its
  // source, i.e. a loop with multiple entry points) is still rejected, as
  // is any function whose blocks are not all reachable from block 0.
  const size_t blockCount = function.blocks.size();

  // Collects the successor block indices of one terminator in the
  // deterministic edge order used by the flow derivation (SwitchInt arms
  // in vector order, then the default target).
  auto successorsOf = [&](size_t index) -> zc::Vector<size_t> {
    zc::Vector<size_t> result;
    const auto& terminator = function.blocks[index].terminator;
    auto addTarget = [&](mir::MirBlockId id) {
      ZC_IF_SOME(next, blockIndex(function, id)) { result.add(next); }
    };
    if (terminator.kind() == mir::MirTerminatorKind::Call) {
      addTarget(terminator.callValue().normalTarget);
    } else if (terminator.kind() == mir::MirTerminatorKind::Goto) {
      addTarget(terminator.gotoValue().target);
    } else if (terminator.kind() == mir::MirTerminatorKind::SwitchInt) {
      const auto& switchInt = terminator.switchIntValue();
      for (const auto& arm : switchInt.arms) addTarget(arm.target);
      addTarget(switchInt.defaultTarget);
    }
    return result;
  };

  // Build predecessor lists for the dominator fixpoint.
  zc::Vector<zc::Vector<size_t>> predecessors;
  predecessors.resize(blockCount);
  for (size_t index = 0; index < blockCount; ++index) {
    for (const size_t successor : successorsOf(index)) { predecessors[successor].add(index); }
  }

  // Iterative dominator computation. The entry block dominates only itself;
  // every other block starts dominated by all blocks and is narrowed to
  // itself plus the intersection of its predecessors' dominator sets until
  // the fixpoint.
  zc::Vector<zc::Vector<bool>> dominated;
  dominated.resize(blockCount);
  for (size_t index = 0; index < blockCount; ++index) {
    dominated[index].resize(blockCount);
    for (size_t other = 0; other < blockCount; ++other) { dominated[index][other] = index != 0; }
  }
  dominated[0][0] = true;
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t index = 1; index < blockCount; ++index) {
      zc::Vector<bool> next;
      next.resize(blockCount);
      for (size_t other = 0; other < blockCount; ++other) next[other] = true;
      for (const size_t predecessor : predecessors[index]) {
        for (size_t other = 0; other < blockCount; ++other) {
          next[other] = next[other] && dominated[predecessor][other];
        }
      }
      next[index] = true;
      if (next != dominated[index]) {
        dominated[index] = zc::mv(next);
        changed = true;
      }
    }
  }

  // DFS from block 0 with gray (on-path) / black (done) coloring. A
  // successor still on the current path is a retreating edge: it is
  // admitted only when the target dominates the source (a reducible loop
  // back edge); otherwise the loop has multiple entries and the function
  // is rejected. A successor already fully expanded is a diamond join and
  // is expanded once.
  zc::Vector<bool> onPath;
  onPath.resize(blockCount);
  for (size_t index = 0; index < blockCount; ++index) onPath[index] = false;
  zc::Vector<bool> done;
  done.resize(blockCount);
  for (size_t index = 0; index < blockCount; ++index) done[index] = false;
  zc::Vector<Frame> stack;
  stack.add(Frame{0, false});
  size_t visitedCount = 0;
  while (stack.size() != 0) {
    auto frame = zc::mv(stack[stack.size() - 1]);
    stack.removeLast();
    if (frame.expanded) {
      onPath[frame.blockIndex] = false;
      done[frame.blockIndex] = true;
      continue;
    }
    if (done[frame.blockIndex]) continue;
    onPath[frame.blockIndex] = true;
    ++visitedCount;
    stack.add(Frame{frame.blockIndex, true});
    for (const size_t successor : successorsOf(frame.blockIndex)) {
      if (onPath[successor]) {
        // Retreating edge: the target must dominate the source so the loop
        // has a single-entry header; otherwise the CFG is irreducible.
        if (!dominated[frame.blockIndex][successor]) return false;
        continue;
      }
      if (done[successor]) continue;
      stack.add(Frame{successor, false});
    }
  }
  return visitedCount == blockCount;
}

}  // namespace zomlang::compiler::ownership::facts
