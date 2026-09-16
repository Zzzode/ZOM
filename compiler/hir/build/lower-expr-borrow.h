// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/hir/build/hir-fn-builder.h"
#include "compiler/hir/build/hir-pending.h"

namespace zomlang::compiler::hir {
namespace detail {

/// \brief Lowers one bare parameter reborrow return through the recursive
/// destination-driven driver: `return &*p;` / `return &mut *p;`, optionally
/// wrapped in one admitted unsafe block (`return unsafe { &*p };`). Node
/// stride: function, body, return, reborrow value, plus one trailing unsafe
/// block node when wrapped. The reborrow is rooted directly at a parameter; no
/// user local is declared.
void lowerParameterReborrowReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one unsafe-block-wrapped scalar return through the recursive
/// driver: `return unsafe { <literal> };`. Node stride: function, body, return,
/// scalar value, trailing unsafe block whose body is the value node.
void lowerUnsafeScalarReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one local-alias reborrow through the recursive driver:
/// `let y = p; return &*y;`, optionally wrapped in one admitted unsafe block.
/// The binding is initialized by the reborrowed parameter and the returned
/// reborrow carries the binding as its source alias. Node stride: function,
/// body, local, parameter-reference initializer, return, reborrow value, plus
/// one trailing unsafe block node when wrapped.
void lowerLocalAliasReborrowReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one local borrow return through the recursive driver:
/// `let x: T = <literal | parameter>; return &x;`, optionally wrapped in one
/// admitted unsafe block. The HIR is materialized identically whether the
/// borrow-source rail later accepts or rejects the escape. Node stride:
/// function, body, local, initializer, return, local-borrow value, plus one
/// trailing unsafe block node when wrapped.
void lowerLocalBorrowReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

}  // namespace detail
}  // namespace zomlang::compiler::hir
