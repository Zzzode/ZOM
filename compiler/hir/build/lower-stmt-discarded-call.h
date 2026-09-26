// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/hir/build/hir-fn-builder.h"
#include "compiler/hir/build/hir-pending.h"

namespace zomlang::compiler::hir {
namespace detail {

/// \brief Lowers one void mutating-receiver method body:
/// `mutating fun set(this, x) { this.field = x; }`. Four node ids in source
/// preorder: function, body, write statement, write value parameter reference.
/// No return is materialized; the callable result is Unit.
void lowerVoidReceiverFieldWriteFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one discarded receiver-call statement followed by a trailing
/// receiver-call return:
/// `mut cell = T { .. }; cell.set(<args>); return cell.get();`. Nine node ids
/// in source preorder: function, body, local, aggregate initializer,
/// statement receiver reference, statement call, trailing receiver reference,
/// trailing call, return. The statement call node is referenced only from the
/// block statement list.
void lowerDiscardedReceiverCallFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

}  // namespace detail
}  // namespace zomlang::compiler::hir
