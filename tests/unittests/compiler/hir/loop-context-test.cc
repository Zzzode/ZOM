// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/loop-context.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::hir {
namespace detail {
namespace {

// Helper: build a LoopContext with a label and a synthetic HIR node id.
LoopContext labeledContext(ast::IdentId label, uint32_t hirOrdinal) {
  auto id = HirNodeId::fromOrdinal(hirOrdinal);
  ZC_EXPECT(id != zc::none);
  return LoopContext{label, ZC_ASSERT_NONNULL(id), ast::NodeId(hirOrdinal)};
}

// Helper: build an unlabeled LoopContext with a synthetic HIR node id.
LoopContext unlabeledContext(uint32_t hirOrdinal) {
  auto id = HirNodeId::fromOrdinal(hirOrdinal);
  ZC_EXPECT(id != zc::none);
  return LoopContext{zc::none, ZC_ASSERT_NONNULL(id), ast::NodeId(hirOrdinal)};
}

ZC_TEST("LoopContextStack starts empty") {
  LoopContextStack stack;
  ZC_EXPECT(stack.empty());
  ZC_EXPECT(stack.size() == 0);
  ZC_EXPECT(stack.innermost() == zc::none);
  ZC_EXPECT(stack.findLabel(ast::IdentId(1)) == zc::none);
  ZC_EXPECT(stack.resolveTarget(zc::none) == zc::none);
  ZC_EXPECT(stack.resolveTarget(ast::IdentId(1)) == zc::none);
}

ZC_TEST("LoopContextStack push makes the loop innermost") {
  LoopContextStack stack;
  stack.push(unlabeledContext(1));
  ZC_EXPECT(!stack.empty());
  ZC_EXPECT(stack.size() == 1);
  auto inner = stack.innermost();
  ZC_EXPECT(inner != zc::none);
  ZC_IF_SOME(context, inner) {
    ZC_EXPECT(context.loopNode.ordinal() == 1);
    ZC_EXPECT(context.label == zc::none);
  }
}

ZC_TEST("LoopContextStack pop removes the innermost loop") {
  LoopContextStack stack;
  stack.push(unlabeledContext(1));
  stack.push(unlabeledContext(2));
  ZC_EXPECT(stack.size() == 2);
  ZC_IF_SOME(context, stack.innermost()) { ZC_EXPECT(context.loopNode.ordinal() == 2); }
  stack.pop();
  ZC_EXPECT(stack.size() == 1);
  ZC_IF_SOME(context, stack.innermost()) { ZC_EXPECT(context.loopNode.ordinal() == 1); }
  stack.pop();
  ZC_EXPECT(stack.empty());
  ZC_EXPECT(stack.innermost() == zc::none);
}

ZC_TEST("LoopContextStack pop on empty stack is a no-op") {
  LoopContextStack stack;
  stack.pop();
  ZC_EXPECT(stack.empty());
  stack.push(unlabeledContext(1));
  stack.pop();
  stack.pop();
  ZC_EXPECT(stack.empty());
}

ZC_TEST("LoopContextStack supports nested loops") {
  LoopContextStack stack;
  stack.push(unlabeledContext(1));
  stack.push(unlabeledContext(2));
  stack.push(unlabeledContext(3));
  ZC_EXPECT(stack.size() == 3);
  ZC_IF_SOME(context, stack.innermost()) { ZC_EXPECT(context.loopNode.ordinal() == 3); }
  stack.pop();
  ZC_IF_SOME(context, stack.innermost()) { ZC_EXPECT(context.loopNode.ordinal() == 2); }
  stack.pop();
  ZC_IF_SOME(context, stack.innermost()) { ZC_EXPECT(context.loopNode.ordinal() == 1); }
}

ZC_TEST("LoopContextStack findLabel returns the matching loop") {
  LoopContextStack stack;
  stack.push(labeledContext(ast::IdentId(10), 1));
  stack.push(labeledContext(ast::IdentId(20), 2));
  stack.push(labeledContext(ast::IdentId(30), 3));

  auto found = stack.findLabel(ast::IdentId(20));
  ZC_EXPECT(found != zc::none);
  ZC_IF_SOME(context, found) {
    ZC_EXPECT(context.loopNode.ordinal() == 2);
    ZC_IF_SOME(label, context.label) { ZC_EXPECT(label == ast::IdentId(20)); }
  }
}

ZC_TEST("LoopContextStack findLabel returns none for a missing label") {
  LoopContextStack stack;
  stack.push(labeledContext(ast::IdentId(10), 1));
  stack.push(labeledContext(ast::IdentId(20), 2));
  ZC_EXPECT(stack.findLabel(ast::IdentId(99)) == zc::none);
}

ZC_TEST("LoopContextStack findLabel skips unlabeled loops") {
  LoopContextStack stack;
  stack.push(unlabeledContext(1));
  stack.push(labeledContext(ast::IdentId(10), 2));
  stack.push(unlabeledContext(3));
  auto found = stack.findLabel(ast::IdentId(10));
  ZC_EXPECT(found != zc::none);
  ZC_IF_SOME(context, found) { ZC_EXPECT(context.loopNode.ordinal() == 2); }
}

ZC_TEST("LoopContextStack findLabel returns the innermost match") {
  LoopContextStack stack;
  // Two loops share the same label (shadowing). The innermost match wins.
  stack.push(labeledContext(ast::IdentId(10), 1));
  stack.push(labeledContext(ast::IdentId(10), 2));
  auto found = stack.findLabel(ast::IdentId(10));
  ZC_EXPECT(found != zc::none);
  ZC_IF_SOME(context, found) { ZC_EXPECT(context.loopNode.ordinal() == 2); }
}

ZC_TEST("LoopContextStack resolveTarget with no label returns innermost") {
  LoopContextStack stack;
  stack.push(unlabeledContext(1));
  stack.push(unlabeledContext(2));
  auto target = stack.resolveTarget(zc::none);
  ZC_EXPECT(target != zc::none);
  ZC_IF_SOME(context, target) { ZC_EXPECT(context.loopNode.ordinal() == 2); }
}

ZC_TEST("LoopContextStack resolveTarget with label returns the matching loop") {
  LoopContextStack stack;
  stack.push(labeledContext(ast::IdentId(10), 1));
  stack.push(labeledContext(ast::IdentId(20), 2));
  auto target = stack.resolveTarget(ast::IdentId(10));
  ZC_EXPECT(target != zc::none);
  ZC_IF_SOME(context, target) { ZC_EXPECT(context.loopNode.ordinal() == 1); }
}

ZC_TEST("LoopContextStack resolveTarget with a missing label returns none") {
  LoopContextStack stack;
  stack.push(labeledContext(ast::IdentId(10), 1));
  ZC_EXPECT(stack.resolveTarget(ast::IdentId(99)) == zc::none);
}

ZC_TEST("LoopContextStack resolveTarget on an empty stack returns none") {
  LoopContextStack stack;
  ZC_EXPECT(stack.resolveTarget(zc::none) == zc::none);
  ZC_EXPECT(stack.resolveTarget(ast::IdentId(1)) == zc::none);
}

ZC_TEST("LoopContextStack tracks the AST node for source correspondence") {
  LoopContextStack stack;
  stack.push(
      LoopContext{zc::none, HirNodeId::fromOrdinal(5).orDefault(HirNodeId{}), ast::NodeId(42)});
  ZC_IF_SOME(context, stack.innermost()) {
    ZC_EXPECT(context.astNode == ast::NodeId(42));
    ZC_EXPECT(context.loopNode.ordinal() == 5);
  }
}

}  // namespace
}  // namespace detail
}  // namespace zomlang::compiler::hir
