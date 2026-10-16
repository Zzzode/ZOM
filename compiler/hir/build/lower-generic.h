// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/hir/build/hir-fn-builder.h"
#include "compiler/hir/build/hir-pending.h"

namespace zomlang::compiler::hir {
namespace detail {

/// \brief Lowers one catch-all GenericShape body through the recursive driver.
/// Handles comparison returns, optional local bindings with aggregate
/// initializers, N local writes (literal/parameter/binary values), and the
/// full set of return value forms (literal, local reference, field projection,
/// parameter reference, parameter index, reborrow, borrow). Node ids follow
/// the same source-preorder allocation as the former inline materializer.
void lowerGenericFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

}  // namespace detail
}  // namespace zomlang::compiler::hir
