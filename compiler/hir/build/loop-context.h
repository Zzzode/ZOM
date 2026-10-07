// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstddef>

#include "compiler/ast/node-id.h"
#include "compiler/ast/tree.h"
#include "compiler/hir/hir-node-id.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::hir {
namespace detail {

/// One active loop's context during HIR construction.
///
/// Tracks the loop's label (if any), its HIR node id, and its AST node for
/// source correspondence. The loop context stack uses these entries to
/// resolve which loop a break or continue statement targets: an unlabeled
/// break/continue targets the innermost loop, and a labeled break/continue
/// targets the innermost loop with the matching label.
struct LoopContext final {
  /// The loop's label identifier, if any. Empty for an unlabeled loop.
  zc::Maybe<ast::IdentId> label;
  /// The HirNodeId of the loop statement this context tracks.
  HirNodeId loopNode;
  /// The AST node of the loop statement, for source correspondence.
  ast::NodeId astNode;
};

/// A stack of active loop contexts during HIR construction.
///
/// The innermost loop is at the top of the stack. Labeled break/continue
/// look up the target loop by label; unlabeled break/continue target the
/// innermost loop. The stack supports arbitrary nesting through push/pop.
///
/// This is the first step toward labeled break/continue: the HIR builder
/// pushes a context when it enters a loop and pops it when it leaves, so a
/// break/continue anywhere in the body can resolve its target loop. MIR CFG
/// changes (dynamic block-id computation for cross-loop transfers) are a
/// separate step.
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
}  // namespace zomlang::compiler::hir
