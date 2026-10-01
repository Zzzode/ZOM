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

/// \brief Lowers K leading scalar-local bindings followed by one comparison
/// conditional return. Node stride is 9 + 2K: function, body, then per binding
/// one local and one initializer id, then the two comparison operands, the
/// comparison, the two arm literals, the conditional, and the return. The body
/// block lists each binding local followed by the return.
void lowerLeadingLocalConditionalReturnFunction(PendingFunctionDeclaration&& function,
                                                HirFnCtx& ctx);

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

/// \brief Lowers one C-style for-loop return through the recursive driver:
/// `for (let id = <lit>; <ident> <cmp> <lit>; <ident> = <binary>) {}` followed
/// by a scalar return. The for-loop desugars to a leading local binding, a loop
/// whose condition is the comparison and whose body is the update write, and
/// the scalar return. Fourteen node ids in fixed layout: function, body, local,
/// initializer, comparison left, comparison right, comparison condition,
/// arithmetic left, arithmetic right, arithmetic write value, write, loop,
/// return, return value. The body block lists [local, loop, return]; the loop
/// statement carries the write node id as its body.
void lowerForLoopReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one C-style for-loop accumulator return through the recursive
/// driver: a leading scalar `let` accumulator local, a `for (let id = <lit>;
/// <ident> <cmp> <lit>; <ident> = <binary>) { <accumulator> = <accumulator>
/// <bin> <ident>; }` loop, and a trailing `return <accumulator-local>;`. The
/// for-loop desugars to a leading accumulator binding, a loop-init binding, a
/// loop whose condition is the comparison and whose body is the accumulator
/// write plus the update write, and the local-reference return. Twenty node
/// ids in fixed layout: function, body, accumulator local, accumulator init
/// literal, loop-init local, loop-init literal, comparison left, comparison
/// right, comparison condition, body-write left, body-write right, body-write
/// binary, body write, update left, update right, update binary, update write,
/// loop, return, return value reference. The body block lists [accumulator
/// local, loop-init local, loop, return]; the loop statement carries the
/// body-write and update-write node ids as its body.
void lowerForLoopAccumulatorReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

}  // namespace detail
}  // namespace zomlang::compiler::hir
