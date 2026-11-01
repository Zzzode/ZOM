// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mir/build/call-identity.h"

namespace zomlang::compiler::mir {

zc::Maybe<MirCallableIdentity> callIdentityFor(
    const hir::HirDirectCallExpression& call,
    const checker::CheckerIdentityAuthority& identities) noexcept {
  if (call.substitution == zc::none) {
    return MirCallableIdentity::source(call.callee);
  }
  if (call.witnesses == zc::none) { return zc::none; }
  auto instance = ir::InstanceId::from(identities.semanticContext(), call.callee,
                                       ZC_ASSERT_NONNULL(call.substitution),
                                       ZC_ASSERT_NONNULL(call.witnesses));
  if (instance == zc::none) { return zc::none; }
  return MirCallableIdentity::instance(ZC_ASSERT_NONNULL(instance));
}

}  // namespace zomlang::compiler::mir
