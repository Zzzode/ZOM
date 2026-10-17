// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/hir/build/hir-fn-builder.h"
#include "compiler/hir/build/hir-pending.h"

namespace zomlang::compiler::hir {
namespace detail {

/// \brief Lowers one local field-projection return whose local is initialized
/// by a nominal aggregate:
/// `let cell = T { field: <literal>, .. }; return cell.field;`.
///
/// Node stride function, body, local, aggregate initializer, return, value.
/// The aggregate lowers into the local's initializer destination and the field
/// projection lowers into the return value destination.
void lowerAggregateFieldProjectionFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one uninitialized local field-projection return:
/// `let cell: T; return cell.field;`.
///
/// Node stride function, body, local, return, value. The binding has no
/// initializer node; the ownership checker rejects the read (ZOM4093) but HIR
/// must still construct the binding for the checker to validate.
void lowerUninitializedFieldProjectionReturnFunction(PendingFunctionDeclaration&& function,
                                                     HirFnCtx& ctx);

/// \brief Lowers one direct enum construction return:
/// `return Result::Ok(41);`.
///
/// Node stride function, body, aggregate, return. The aggregate lowers into
/// the return value destination (no intermediate local).
void lowerEnumConstructionReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one existential return whose value is a concrete aggregate
/// construction:
/// `fn f() -> dyn I { return P { ..constants.. }; }`.
///
/// Node stride function, body, return, aggregate. The aggregate lowers into
/// the return value destination (no intermediate local). The function
/// declaration carries a HirErasureRecord so the MIR builder emits an Erase
/// rvalue and the LIR builder constructs the fat-pointer carrier.
void lowerExistentialReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

}  // namespace detail
}  // namespace zomlang::compiler::hir
