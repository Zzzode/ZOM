// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mir/cfg-utils.h"

namespace zomlang::compiler::mir {

namespace {

MirBlockId blockId(uint32_t ordinal) {
  auto value = MirBlockId::fromOrdinal(ordinal);
  ZC_IF_SOME(id, value) { return id; }
  ZC_UNREACHABLE
}

}  // namespace

const MirBasicBlock* findBlockById(const MirFunction& function, MirBlockId id) {
  for (const auto& block : function.blocks) {
    if (block.id == id) return &block;
  }
  return nullptr;
}

bool resolveDiamondCfg(const MirFunction& function, const MirBasicBlock*& thenBlock,
                       const MirBasicBlock*& elseBlock, const MirBasicBlock*& joinBlock) {
  if (function.blocks.size() != 4) return false;
  const auto& entry = function.blocks[0];
  if (entry.id != blockId(1) || entry.terminator.kind() != MirTerminatorKind::SwitchInt) {
    return false;
  }
  const auto& switchInt = entry.terminator.switchIntValue();
  if (switchInt.arms.size() != 2) return false;
  thenBlock = findBlockById(function, switchInt.arms[0].target);
  elseBlock = findBlockById(function, switchInt.arms[1].target);
  if (thenBlock == nullptr || elseBlock == nullptr || thenBlock == elseBlock) return false;
  if (thenBlock->terminator.kind() != MirTerminatorKind::Goto ||
      elseBlock->terminator.kind() != MirTerminatorKind::Goto) {
    return false;
  }
  const auto joinId = thenBlock->terminator.gotoValue().target;
  if (elseBlock->terminator.gotoValue().target != joinId) return false;
  joinBlock = findBlockById(function, joinId);
  if (joinBlock == nullptr || joinBlock == thenBlock || joinBlock == elseBlock) return false;
  return true;
}

bool resolveLoopCfg(const MirFunction& function, const MirBasicBlock*& header,
                    const MirBasicBlock*& body, const MirBasicBlock*& exit) {
  if (function.blocks.size() != 4) return false;
  const auto& entry = function.blocks[0];
  if (entry.id != blockId(1) || entry.terminator.kind() != MirTerminatorKind::Goto) {
    return false;
  }
  header = findBlockById(function, entry.terminator.gotoValue().target);
  if (header == nullptr || header == &entry) return false;
  if (header->terminator.kind() != MirTerminatorKind::SwitchInt) return false;
  const auto& switchInt = header->terminator.switchIntValue();
  if (switchInt.arms.size() != 1) return false;
  body = findBlockById(function, switchInt.arms[0].target);
  exit = findBlockById(function, switchInt.defaultTarget);
  if (body == nullptr || exit == nullptr || body == exit || body == header || exit == header) {
    return false;
  }
  if (body->terminator.kind() != MirTerminatorKind::Goto) return false;
  return true;
}

bool resolveDoWhileLoopCfg(const MirFunction& function, const MirBasicBlock*& body,
                           const MirBasicBlock*& condition, const MirBasicBlock*& exit) {
  if (function.blocks.size() != 4) return false;
  const auto& entry = function.blocks[0];
  if (entry.id != blockId(1) || entry.terminator.kind() != MirTerminatorKind::Goto) {
    return false;
  }
  body = findBlockById(function, entry.terminator.gotoValue().target);
  if (body == nullptr || body == &entry) return false;
  if (body->terminator.kind() != MirTerminatorKind::Goto) return false;
  condition = findBlockById(function, body->terminator.gotoValue().target);
  if (condition == nullptr || condition == &entry || condition == body) return false;
  if (condition->terminator.kind() != MirTerminatorKind::SwitchInt) return false;
  const auto& switchInt = condition->terminator.switchIntValue();
  if (switchInt.arms.size() != 1) return false;
  // True arm must target the body (back-edge).
  if (switchInt.arms[0].target != body->id) return false;
  exit = findBlockById(function, switchInt.defaultTarget);
  if (exit == nullptr || exit == &entry || exit == body || exit == condition) return false;
  return true;
}

bool resolveGuardedLoopCfg(const MirFunction& function, const MirBasicBlock*& header,
                           const MirBasicBlock*& guard, const MirBasicBlock*& body,
                           const MirBasicBlock*& exit) {
  if (function.blocks.size() != 5) return false;
  const auto& entry = function.blocks[0];
  if (entry.id != blockId(1) || entry.terminator.kind() != MirTerminatorKind::Goto) {
    return false;
  }
  header = findBlockById(function, entry.terminator.gotoValue().target);
  if (header == nullptr || header == &entry) return false;
  if (header->terminator.kind() != MirTerminatorKind::SwitchInt) return false;
  const auto& headerSwitch = header->terminator.switchIntValue();
  if (headerSwitch.arms.size() != 1) return false;
  guard = findBlockById(function, headerSwitch.arms[0].target);
  exit = findBlockById(function, headerSwitch.defaultTarget);
  if (guard == nullptr || exit == nullptr || guard == exit || guard == header || exit == header) {
    return false;
  }
  if (guard->terminator.kind() != MirTerminatorKind::SwitchInt) return false;
  const auto& guardSwitch = guard->terminator.switchIntValue();
  if (guardSwitch.arms.size() != 1) return false;
  // Guard true arm targets the exit (break).
  if (guardSwitch.arms[0].target != exit->id) return false;
  body = findBlockById(function, guardSwitch.defaultTarget);
  if (body == nullptr || body == guard || body == exit || body == header) return false;
  if (body->terminator.kind() != MirTerminatorKind::Goto) return false;
  // Body back-edge to header.
  if (body->terminator.gotoValue().target != header->id) return false;
  return true;
}

bool resolveNestedLoopCfg(const MirFunction& function, const MirBasicBlock*& outerHeader,
                          const MirBasicBlock*& innerEntry, const MirBasicBlock*& innerHeader,
                          const MirBasicBlock*& innerBody, const MirBasicBlock*& outerCont,
                          const MirBasicBlock*& exit) {
  if (function.blocks.size() != 7) return false;
  const auto& entry = function.blocks[0];
  if (entry.id != blockId(1) || entry.terminator.kind() != MirTerminatorKind::Goto) {
    return false;
  }
  outerHeader = findBlockById(function, entry.terminator.gotoValue().target);
  if (outerHeader == nullptr || outerHeader == &entry) return false;
  if (outerHeader->terminator.kind() != MirTerminatorKind::SwitchInt) return false;
  const auto& outerSwitch = outerHeader->terminator.switchIntValue();
  if (outerSwitch.arms.size() != 1) return false;
  innerEntry = findBlockById(function, outerSwitch.arms[0].target);
  exit = findBlockById(function, outerSwitch.defaultTarget);
  if (innerEntry == nullptr || exit == nullptr || innerEntry == exit || innerEntry == outerHeader ||
      exit == outerHeader) {
    return false;
  }
  if (innerEntry->terminator.kind() != MirTerminatorKind::Goto) return false;
  innerHeader = findBlockById(function, innerEntry->terminator.gotoValue().target);
  if (innerHeader == nullptr || innerHeader == &entry || innerHeader == innerEntry ||
      innerHeader == outerHeader || innerHeader == exit) {
    return false;
  }
  if (innerHeader->terminator.kind() != MirTerminatorKind::SwitchInt) return false;
  const auto& innerSwitch = innerHeader->terminator.switchIntValue();
  if (innerSwitch.arms.size() != 1) return false;
  innerBody = findBlockById(function, innerSwitch.arms[0].target);
  outerCont = findBlockById(function, innerSwitch.defaultTarget);
  if (innerBody == nullptr || outerCont == nullptr || innerBody == outerCont ||
      innerBody == innerHeader || outerCont == innerHeader || innerBody == exit ||
      outerCont == exit) {
    return false;
  }
  if (innerBody->terminator.kind() != MirTerminatorKind::Goto) return false;
  if (outerCont->terminator.kind() != MirTerminatorKind::Goto) return false;
  // Outer continuation back-edge to outer header.
  if (outerCont->terminator.gotoValue().target != outerHeader->id) return false;
  // Inner body Goto target is not checked here — the caller validates whether
  // it is a back-edge to innerHeader, a continue to outerCont, or a break to exit.
  return true;
}

const MirBasicBlock* findCallContinuation(const MirFunction& function, const MirBasicBlock& entry) {
  MirBlockId target;
  if (entry.terminator.kind() == MirTerminatorKind::Call) {
    target = entry.terminator.callValue().normalTarget;
  } else if (entry.terminator.kind() == MirTerminatorKind::IndirectCall) {
    target = entry.terminator.indirectCallValue().normalTarget;
  } else {
    return nullptr;
  }
  const auto* continuation = findBlockById(function, target);
  if (continuation == nullptr || continuation == &entry) return nullptr;
  return continuation;
}

}  // namespace zomlang::compiler::mir
