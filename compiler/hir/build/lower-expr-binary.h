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

/// \brief Lowers one `let x: T = <lit>; return !x;` (or `+x` / `-x` / `~x`)
/// primitive-unary function through the recursive driver. The unary desugars to
/// an equivalent binary with a synthetic constant operand. The local binding
/// lowers first, then the binary operands, the binary node, and the return.
/// Node stride: function, body, local, initializer, left, right, binary, return.
void lowerPrimitiveUnaryReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

}  // namespace detail
}  // namespace zomlang::compiler::hir
