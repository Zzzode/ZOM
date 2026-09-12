// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/hir/build/hir-fn-builder.h"
#include "compiler/hir/build/hir-pending.h"

namespace zomlang::compiler::hir {
namespace detail {

/// \brief Lowers one mutable-local write body through the recursive driver:
/// one initialized mut local, one or more non-field writes whose value is a
/// scalar literal, parameter reference, or primitive binary, and a
/// local-reference return (`mut x: T = <leaf>; x = <value>; return x;`).
void lowerLocalWriteFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

}  // namespace detail
}  // namespace zomlang::compiler::hir
