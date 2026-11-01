// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/debug.h"
#include "compiler/checker/checker-identity-authority.h"
#include "compiler/hir/hir-module.h"
#include "compiler/mir/built-mir.h"
#include "compiler/ir/ir-identity.h"

namespace zomlang::compiler::mir {

/// \brief Resolves one HIR direct-call record to its MIR call-edge identity.
///
/// An ordinary call carries no substitution and becomes a Source edge naming
/// the callee definition. A checker-resolved generic call carries a
/// substitution and the empty witness and becomes an Instance edge whose
/// components are context-validated here; canonical edge bytes are assembled
/// later from the frozen record view.
/// \param call HIR direct-call record.
/// \param identities Checker identity authority for the function's context.
/// \return The call-edge identity, or none when instance handles are foreign
/// or invalid.
ZC_NODISCARD zc::Maybe<MirCallableIdentity> callIdentityFor(
    const hir::HirDirectCallExpression& call,
    const checker::CheckerIdentityAuthority& identities) noexcept;

}  // namespace zomlang::compiler::mir
