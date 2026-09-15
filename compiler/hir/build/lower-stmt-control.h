// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/hir/build/hir-fn-builder.h"
#include "compiler/hir/build/hir-pending.h"

namespace zomlang::compiler::hir {
namespace detail {

/// \brief Lowers one if/else conditional return through the recursive
/// destination-driven driver. The condition is a bare bool parameter reference
/// or an `a CMP b` relational comparison of two literal/parameter operands, and
/// each branch returns a scalar literal or a parameter reference. Node strides
/// match the generic materializer exactly: seven ids for a parameter condition
/// (function, body, condition, then, else, conditional, return) and nine for a
/// comparison condition (plus left, right, equality before the arms).
void lowerConditionalReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one empty-body `while` loop followed by a scalar return
/// through the recursive driver. Six node ids: function, body, condition
/// parameter reference, return literal, loop, return. The body block lists the
/// loop statement then the return.
void lowerLoopReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one loop-body composite through the recursive driver: one
/// initialized mut local, an admitted `while` whose body writes that local
/// (literal, parameter, or primitive-binary write values), and a
/// local-reference return. The loop condition parameter reference and the loop
/// statement allocate in a trailing region, reproducing the generic
/// materializer's fixed-id layout byte-identically.
void lowerLoopBodyReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

}  // namespace detail
}  // namespace zomlang::compiler::hir
