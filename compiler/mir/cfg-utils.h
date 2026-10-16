// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/mir/built-mir.h"

namespace zomlang::compiler::mir {

/// \brief Finds a block by its id, following CFG edges instead of positional
/// indices. Returns nullptr when the id is not present in the function.
const MirBasicBlock* findBlockById(const MirFunction& function, MirBlockId id);

/// \brief Resolves a 4-block conditional diamond CFG: entry (SwitchInt) ->
/// then/else (Goto) -> join. The entry is always blocks[0] with id blockId(1).
/// On success, sets thenBlock/elseBlock/joinBlock to the blocks found via
/// terminator targets and returns true. Returns false on any structural
/// mismatch (wrong block count, missing terminator, dangling target, aliasing).
bool resolveDiamondCfg(const MirFunction& function, const MirBasicBlock*& thenBlock,
                       const MirBasicBlock*& elseBlock, const MirBasicBlock*& joinBlock);

/// \brief Resolves a 4-block reducible loop CFG: entry (Goto) -> header
/// (SwitchInt) -> body (Goto) / exit (default). The entry is always blocks[0]
/// with id blockId(1). On success, sets header/body/exit to the blocks found
/// via terminator targets and returns true. The body's Goto target is not
/// checked here — the caller validates whether it is a back-edge to the header
/// or a break to the exit. Returns false on any structural mismatch.
bool resolveLoopCfg(const MirFunction& function, const MirBasicBlock*& header,
                    const MirBasicBlock*& body, const MirBasicBlock*& exit);

/// \brief Resolves a 4-block do-while loop CFG: entry (Goto) -> body (Goto) ->
/// condition (SwitchInt) -> body (true, back-edge) / exit (default). The entry
/// is always blocks[0] with id blockId(1). The condition's true arm must
/// target the body (back-edge); the default target is the exit. Returns false
/// on any structural mismatch.
bool resolveDoWhileLoopCfg(const MirFunction& function, const MirBasicBlock*& body,
                           const MirBasicBlock*& condition, const MirBasicBlock*& exit);

/// \brief Resolves a 5-block guarded loop CFG: entry (Goto) -> header
/// (SwitchInt) -> guard (SwitchInt) -> body (default) / exit (true), body
/// (Goto) -> header (back-edge). The header's true arm targets the guard; the
/// guard's true arm targets the exit (break); the guard's default target is
/// the body. Returns false on any structural mismatch.
bool resolveGuardedLoopCfg(const MirFunction& function, const MirBasicBlock*& header,
                           const MirBasicBlock*& guard, const MirBasicBlock*& body,
                           const MirBasicBlock*& exit);

/// \brief Resolves a 7-block nested for-loop CFG: entry (Goto) -> outerHeader
/// (SwitchInt) -> innerEntry (true) / exit (default), innerEntry (Goto) ->
/// innerHeader (SwitchInt) -> innerBody (true) / outerCont (default),
/// innerBody (Goto) -> innerHeader | outerCont | exit (back-edge / continue /
/// break), outerCont (Goto) -> outerHeader (back-edge), exit (Return).
/// The inner body's Goto target is not checked here — the caller validates
/// whether it is a back-edge, continue, or break. Returns false on any
/// structural mismatch.
bool resolveNestedLoopCfg(const MirFunction& function, const MirBasicBlock*& outerHeader,
                          const MirBasicBlock*& innerEntry, const MirBasicBlock*& innerHeader,
                          const MirBasicBlock*& innerBody, const MirBasicBlock*& outerCont,
                          const MirBasicBlock*& exit);

/// \brief Follows the normalTarget edge of a Call or IndirectCall terminator
/// to find the continuation block. Returns nullptr if the entry terminator is
/// not a call, the target does not exist, or the target is the entry itself.
const MirBasicBlock* findCallContinuation(const MirFunction& function, const MirBasicBlock& entry);

}  // namespace zomlang::compiler::mir
