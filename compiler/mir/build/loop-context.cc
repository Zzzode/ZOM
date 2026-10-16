// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mir/build/loop-context.h"

namespace zomlang::compiler::mir {
namespace detail {

void LoopContextStack::push(LoopContext context) { contexts.add(zc::mv(context)); }

void LoopContextStack::pop() noexcept {
  if (!contexts.empty()) contexts.removeLast();
}

zc::Maybe<const LoopContext&> LoopContextStack::innermost() const noexcept {
  if (contexts.empty()) return zc::none;
  return contexts.back();
}

zc::Maybe<const LoopContext&> LoopContextStack::findLabel(ast::IdentId label) const noexcept {
  for (size_t i = contexts.size(); i > 0; --i) {
    const auto& ctx = contexts[i - 1];
    ZC_IF_SOME(ctxLabel, ctx.label) {
      if (ctxLabel == label) return ctx;
    }
  }
  return zc::none;
}

zc::Maybe<const LoopContext&> LoopContextStack::resolveTarget(
    zc::Maybe<ast::IdentId> label) const noexcept {
  ZC_IF_SOME(targetLabel, label) { return findLabel(targetLabel); }
  return innermost();
}

bool LoopContextStack::empty() const noexcept { return contexts.empty(); }

size_t LoopContextStack::size() const noexcept { return contexts.size(); }

}  // namespace detail
}  // namespace zomlang::compiler::mir
