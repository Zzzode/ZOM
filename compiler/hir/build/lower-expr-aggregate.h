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

}  // namespace detail
}  // namespace zomlang::compiler::hir
