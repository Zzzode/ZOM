// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/loop-context.h"

namespace zomlang::compiler::hir {
namespace detail {

void LoopContextStack::push(LoopContext context) { contexts.add(zc::mv(context)); }

void LoopContextStack::pop() noexcept {
  if (contexts.size() != 0) { contexts.removeLast(); }
}

zc::Maybe<const LoopContext&> LoopContextStack::innermost() const noexcept {
  if (contexts.size() == 0) { return zc::none; }
  return contexts[contexts.size() - 1];
}

zc::Maybe<const LoopContext&> LoopContextStack::findLabel(ast::IdentId label) const noexcept {
  // Search from the innermost loop outward so the innermost match wins when
  // multiple active loops share the same label.
  for (size_t index = contexts.size(); index > 0; --index) {
    const auto& context = contexts[index - 1];
    ZC_IF_SOME(contextLabel, context.label) {
      if (contextLabel == label) { return context; }
    }
  }
  return zc::none;
}

zc::Maybe<const LoopContext&> LoopContextStack::resolveTarget(
    zc::Maybe<ast::IdentId> label) const noexcept {
  ZC_IF_SOME(targetLabel, label) { return findLabel(targetLabel); }
  return innermost();
}

bool LoopContextStack::empty() const noexcept { return contexts.size() == 0; }

size_t LoopContextStack::size() const noexcept { return contexts.size(); }

}  // namespace detail
}  // namespace zomlang::compiler::hir
