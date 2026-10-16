// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstddef>

#include "compiler/ast/node-id.h"
#include "compiler/mir/built-mir.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::mir {
namespace detail {

/// One active loop's context during MIR construction.
///
/// Tracks the block IDs that break/continue statements target. The loop
/// context stack uses these entries to resolve which blocks a break or
/// continue statement jumps to: an unlabeled break/continue targets the
/// innermost loop, and a labeled break/continue targets the innermost loop
/// with the matching label.
///
/// The block IDs are computed dynamically via MirFnCtx::peekBlockIdAt or
/// resolved from the CFG, replacing the hardcoded blockId(N) literals that
/// the legacy loop builders used.
struct LoopContext final {
  /// The loop's label identifier, if any. Empty for an unlabeled loop.
  zc::Maybe<ast::IdentId> label;
  /// The loop header block: SwitchInt for while/for, condition block for
  /// do-while. A `continue` to a while/for loop targets this block.
  MirBlockId header;
  /// The loop body block.
  MirBlockId body;
  /// The loop exit block. A `break` targets this block.
  MirBlockId exit;
  /// The block a `continue` statement jumps to. For while/for loops this is
  /// the header; for do-while loops it is the condition block.
  MirBlockId continueTarget;
};

/// A stack of active loop contexts during MIR construction.
///
/// The innermost loop is at the top of the stack. Labeled break/continue
/// look up the target loop by label; unlabeled break/continue target the
/// innermost loop. The stack supports arbitrary nesting through push/pop.
class LoopContextStack final {
public:
  LoopContextStack() = default;
  ~LoopContextStack() = default;
  LoopContextStack(LoopContextStack&&) = default;
  LoopContextStack& operator=(LoopContextStack&&) = default;
  ZC_DISALLOW_COPY(LoopContextStack);

  /// Pushes a loop context onto the stack, making it the innermost active
  /// loop.
  void push(LoopContext context);

  /// Pops the innermost loop context from the stack. Does nothing when the
  /// stack is empty.
  void pop() noexcept;

  /// Returns the innermost loop context, or none when the stack is empty.
  zc::Maybe<const LoopContext&> innermost() const noexcept;

  /// Returns the innermost loop context whose label matches `label`, or none
  /// when no active loop has that label.
  zc::Maybe<const LoopContext&> findLabel(ast::IdentId label) const noexcept;

  /// Resolves the target loop for a break/continue statement. When `label`
  /// is empty, returns the innermost loop context. When `label` is set,
  /// returns the innermost loop context with the matching label. Returns
  /// none when no matching loop is found.
  zc::Maybe<const LoopContext&> resolveTarget(zc::Maybe<ast::IdentId> label) const noexcept;

  /// Returns true when no loop is active.
  bool empty() const noexcept;

  /// Returns the number of active loops.
  size_t size() const noexcept;

private:
  zc::Vector<LoopContext> contexts;
};

}  // namespace detail
}  // namespace zomlang::compiler::mir
