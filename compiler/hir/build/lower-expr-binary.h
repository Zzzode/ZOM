// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/hir/build/hir-fn-builder.h"
#include "compiler/hir/build/hir-pending.h"

namespace zomlang::compiler::hir {
namespace detail {

/// \brief Lowers one `return <a OP b>` primitive-binary function through the
/// recursive driver. The two operands lower into their own destination ids, the
/// primitive-binary node lowers the operation into its id, and the return takes
/// the binary node as its value. Node stride: function, body, left, right,
/// binary, return.
void lowerComparisonReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

}  // namespace detail
}  // namespace zomlang::compiler::hir
