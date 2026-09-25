// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/hir/build/hir-fn-builder.h"
#include "compiler/hir/build/hir-pending.h"

namespace zomlang::compiler::hir {
namespace detail {

/// \brief Lowers one shared-receiver field-arithmetic method through the
/// recursive driver: `return this.<field> OP <literal>;`. The field projects
/// off the implicit receiver parameter into its own id, the scalar literal
/// lowers into its own id, the primitive-binary node combines them into its
/// id, and the return takes the binary node as its value. Node stride:
/// function, body, field projection, literal, binary, return.
void lowerReceiverFieldArithmeticFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

}  // namespace detail
}  // namespace zomlang::compiler::hir
