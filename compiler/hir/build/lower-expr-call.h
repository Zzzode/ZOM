// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/hir/build/hir-fn-builder.h"
#include "compiler/hir/build/hir-pending.h"

namespace zomlang::compiler::hir {
namespace detail {

/// \brief Lowers one bare direct-call return through the recursive driver:
/// `return callee(<args>);`. Node stride: function, body, return, call. The call
/// node is the return value; each argument is already a resolved scalar constant
/// or parameter key in the pending call record.
void lowerDirectCallReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one direct-call local initializer through the recursive driver:
/// `let value = callee(<args>); return value;`. Node stride: function, body,
/// local, call initializer, return, local reference. The call lowers into the
/// local's initializer destination and the returned place lowers into the return
/// destination.
void lowerDirectCallInitializerFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one mutable-receiver call body through the recursive driver:
/// `mut cell = T { .. }; return cell.method(<args>);`. Node stride: function,
/// body, local, aggregate initializer, return, receiver reference, receiver
/// call. The aggregate lowers into the local's initializer destination, the
/// receiver reference lowers into its own destination, and the receiver call is
/// the return value.
void lowerReceiverCallFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one shared-receiver self-call body through the recursive
/// driver: `return this.method();`. Node stride: function, body, return, self
/// call. The implicit receiver is a header parameter forwarded directly, so the
/// receiver allocates no body node and the self-call record's receiver slot is
/// unset.
void lowerReceiverSelfCallFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

}  // namespace detail
}  // namespace zomlang::compiler::hir
